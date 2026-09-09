"""Iteration harness for the Daz -> MetaHuman pose retarget.

Duplicates a Daz skeletal mesh and re-poses the duplicate's GEOMETRY into the
MetaHuman A pose using the exact machinery the clothing pipeline uses
(_retarget_daz_pose_to_metahuman + vendor-weight LBS through the Daz mesh's
OWN skeleton), then bakes the result to an overwritable asset for direct
visual comparison against the converted MetaHuman body. Because the conform
fitted the MetaHuman body to this same Daz shape, any offset between
<out> and the MH body is retarget error — proportions and weights are out of
the equation.

The retarget math itself lives in ConvertToMetaHuman.py (exec'd from there,
nothing forked) — tweak _retarget_daz_pose_to_metahuman / DAZ_TO_METAHUMAN_BONE
in that file and just re-run this script.

The slow per-vertex vendor-weight capture is cached to Saved/DazToUnreal/ so
only the first run pays it; every later run re-poses in seconds. Use
--refresh-weights after changing --slots filtering logic or reimporting.

Usage (editor python):
  py ReposeDazBody.py
  py ReposeDazBody.py --daz=/Game/DazToUnreal/NewTest3/NewTest3 --mh=/Game/DazToUnreal/NewTest3/NewTest3_MHC_Body
  py ReposeDazBody.py --slots=arm,hand,finger    (partial mesh for faster first capture)
  py ReposeDazBody.py --report=hand,index,thumb  (per-bone diagnostic filter)

Deletable diagnostic; not part of the conversion pipeline.
"""

import argparse
import hashlib
import math
import os
import pickle
import time
import unreal

parser = argparse.ArgumentParser(description='Re-pose a duplicate of a Daz skeletal mesh into the MetaHuman A pose.')
parser.add_argument('--daz', default='/Game/DazToUnreal/NewTest3/NewTest3',
                    help='Daz skeletal mesh (package or object path).')
parser.add_argument('--mh', default='/Game/DazToUnreal/NewTest3/NewTest3_MHC_Body',
                    help='Converted MetaHuman body mesh whose A pose is the retarget target.')
parser.add_argument('--out', default='',
                    help='Output asset path; default <daz>_Reposed. Overwritten every run. '
                         'Must be a SkeletalMesh under /Game/ unless --force is given.')
parser.add_argument('--force', action='store_true',
                    help='Allow --out to overwrite an existing asset that is NOT a SkeletalMesh. '
                         'Off by default so a mistyped path cannot delete an unrelated asset.')
parser.add_argument('--slots', default='',
                    help='Comma-separated substrings; keep only material slots matching one (default: whole mesh).')
parser.add_argument('--report', default='hand,carpal,thumb,index,mid,ring,pinky,forearm,upperarm,shldr,shoulder',
                    help='Comma-separated substrings of Daz bone names to list in the per-bone report.')
parser.add_argument('--refresh-weights', action='store_true',
                    help='Ignore the cached vendor-weight capture and re-read it.')
args = parser.parse_args()


def _package_path(path):
    path = path.strip('"').strip()
    return path.split('.')[0]


daz_path = _package_path(args.daz)
mh_path = _package_path(args.mh)
out_path = _package_path(args.out) if args.out else daz_path + '_Reposed'

# This script deletes whatever sits at --out before writing. Two cheap guards so
# a mistyped or pasted path cannot take an unrelated asset with it: the target
# must live under /Game/ in a subfolder, and an existing asset there has to be
# the SkeletalMesh this script produces unless --force says otherwise.
if not out_path.startswith('/Game/') or '..' in out_path.split('/') or out_path.count('/') < 3:
    raise RuntimeError('ReposeDazBody: --out must be an asset in a subfolder of /Game, got: %s' % out_path)
if unreal.EditorAssetLibrary.does_asset_exist(out_path) and not args.force:
    _existing = unreal.EditorAssetLibrary.load_asset(out_path)
    if not isinstance(_existing, unreal.SkeletalMesh):
        raise RuntimeError(
            'ReposeDazBody: %s already exists and is a %s, not a SkeletalMesh. '
            'Re-run with --force if you really mean to replace it.'
            % (out_path, type(_existing).__name__ if _existing else 'unknown asset'))

# Pull the pipeline helpers (retarget, bone-name map, geometry/list utils) out
# of ConvertToMetaHuman.py without running its main flow.
convert_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ConvertToMetaHuman.py')
with open(convert_path, 'r', encoding='utf-8') as convert_file:
    convert_source = convert_file.read()
marker_index = convert_source.index('parser = argparse.ArgumentParser')
pipeline = {}
exec(compile(convert_source[:marker_index], convert_path, 'exec'), pipeline)

daz_mesh = unreal.EditorAssetLibrary.load_asset(daz_path)
if not isinstance(daz_mesh, unreal.SkeletalMesh):
    raise RuntimeError('Daz skeletal mesh not found: ' + daz_path)
mh_mesh = unreal.EditorAssetLibrary.load_asset(mh_path)
if not isinstance(mh_mesh, unreal.SkeletalMesh):
    raise RuntimeError('MetaHuman body mesh not found: ' + mh_path)

start_time = time.time()


def _log_phase(message):
    unreal.log('ReposeDazBody: [%6.1fs] %s' % (time.time() - start_time, message))


# --- 1. Daz mesh -> dynamic mesh (optionally pruned to a slot subset) -------
daz_dyn = pipeline['_copy_skeletal_mesh_to_dynamic_mesh'](daz_mesh)
slot_filters = [entry.strip().lower() for entry in args.slots.split(',') if entry.strip()]
kept_indices = list(range(len(daz_mesh.materials)))
if slot_filters:
    def _keep_slot(slot_name):
        lowered = slot_name.lower()
        return any(entry in lowered for entry in slot_filters)
    kept_indices = [index for index, material in enumerate(daz_mesh.materials)
                    if _keep_slot(str(material.material_slot_name))]
    if not kept_indices:
        raise RuntimeError('--slots=%s matches no material slots on %s' % (args.slots, daz_path))
    pipeline['_prune_dynamic_mesh_to_slots'](daz_dyn, daz_mesh, _keep_slot)
    compact = pipeline['_named_call_any'](('GeometryScript_MeshRepair', 'GeometryScript_MeshRepairFunctions'),
                                          ('compact_mesh',))
    pipeline['_first_if_tuple'](compact(daz_dyn))
    remap_materials = pipeline['_named_call'](unreal.GeometryScript_Materials,
                                              ('remap_material_i_ds', 'remap_material_ids'))
    for new_id, old_id in enumerate(kept_indices):
        if new_id != old_id:
            remap_materials(daz_dyn, old_id, new_id)
    _log_phase('pruned to %d/%d material slots' % (len(kept_indices), len(daz_mesh.materials)))
mh_dyn = pipeline['_copy_skeletal_mesh_to_dynamic_mesh'](mh_mesh)

daz_bones = pipeline['_all_bones_info'](daz_dyn)
mh_bones = pipeline['_all_bones_info'](mh_dyn)
daz_names = [str(info.name) for info in daz_bones]
daz_parents = [info.parent_index for info in daz_bones]
_log_phase('meshes copied (%d Daz bones, %d MetaHuman bones)' % (len(daz_bones), len(mh_bones)))

# --- 2. Retarget the MetaHuman A pose onto the Daz skeleton -----------------
aim_debug = {}
bind_positions, deltas, new_positions, stats = \
    pipeline['_retarget_daz_pose_to_metahuman'](daz_bones, mh_bones, debug=aim_debug)
_log_phase('retarget: %d/%d Daz bones aimed, max joint drift %.1f cm, max rotation %.1f deg'
           % (stats['mapped'], len(daz_bones), stats['max_move'], stats['max_rotation']))

# --- 3. Per-bone diagnostic report ------------------------------------------
# Recreate the retarget's first-claimant mapping so each row can also show the
# gap between the re-posed Daz joint and its MetaHuman counterpart — the
# direct measure of parent-carry drift.
mh_position = {}
for info in mh_bones:
    translation = info.world_transform.translation
    mh_position[str(info.name)] = (translation.x, translation.y, translation.z)
mapping = {}
claimed = set()
for index, name in enumerate(daz_names):
    target = pipeline['DAZ_TO_METAHUMAN_BONE'].get(name)
    if target is not None and target in mh_position and target not in claimed:
        mapping[index] = target
        claimed.add(target)

vec_sub, vec_length = pipeline['_vec_sub'], pipeline['_vec_length']


def _rotation_degrees(quat):
    return math.degrees(2.0 * math.acos(min(1.0, abs(quat[3]))))


rows = []
for index, target in mapping.items():
    parent = daz_parents[index]
    inherited = parent >= 0 and (deltas[index] is deltas[parent])
    rows.append({
        'index': index,
        'daz': daz_names[index],
        'mh': target,
        'rotation': _rotation_degrees(deltas[index]),
        'drift': vec_length(vec_sub(new_positions[index], bind_positions[index])),
        'gap': vec_length(vec_sub(new_positions[index], mh_position[target])),
        'inherited': inherited,
        'debug': aim_debug.get(index),
    })


def _direction(vector):
    return '(%+.2f,%+.2f,%+.2f)' % vector


def _log_rows(title, selected, verbose=False):
    if not selected:
        return
    unreal.log('ReposeDazBody: --- %s ---' % title)
    unreal.log('ReposeDazBody: %-24s %-22s %9s %9s %9s  %s'
               % ('daz bone', 'mh bone', 'rot deg', 'drift cm', 'gap cm', 'aim'))
    for row in selected:
        unreal.log('ReposeDazBody: %-24s %-22s %9.1f %9.1f %9.1f  %s'
                   % (row['daz'], row['mh'], row['rotation'], row['drift'], row['gap'],
                      'INHERITED' if row['inherited'] else 'aimed'))
        if verbose and row['debug'] is not None:
            unreal.log('ReposeDazBody:     via %-20s bind %s -> mh %s  roll %s (%+.1f deg twist)'
                       % (row['debug']['via'], _direction(row['debug']['bind_dir']),
                          _direction(row['debug']['target_dir']),
                          row['debug'].get('roll', '?'), row['debug'].get('twist_deg', 0.0)))


report_filters = [entry.strip().lower() for entry in args.report.split(',') if entry.strip()]
report_rows = [row for row in rows
               if any(entry in row['daz'].lower() for entry in report_filters)]
_log_rows('report bones (--report=%s)' % args.report, report_rows, verbose=True)
_log_rows('top rotations', sorted(rows, key=lambda row: -row['rotation'])[:12])
_log_rows('largest joint gap vs MetaHuman (parent-carry drift)',
          sorted(rows, key=lambda row: -row['gap'])[:12])

# --- 4. Vendor weights (cached across runs) ---------------------------------
num_vertices = pipeline['_first_if_tuple'](pipeline['_named_call'](
    unreal.GeometryScript_MeshQueries, ('get_num_vertex_i_ds', 'get_num_vertex_ids'))(daz_dyn))
if isinstance(num_vertices, unreal.DynamicMesh):
    raise RuntimeError('could not read the vertex count')

try:
    cache_dir = os.path.join(unreal.Paths.project_saved_dir(), 'DazToUnreal')
except Exception:
    cache_dir = os.path.dirname(os.path.abspath(__file__))
cache_key = hashlib.md5('|'.join([daz_path] + [str(index) for index in kept_indices])
                        .encode('utf-8')).hexdigest()[:12]
cache_file = os.path.join(cache_dir, 'repose_weights_%s.pkl' % cache_key)

captured = None
if not args.refresh_weights and os.path.isfile(cache_file):
    try:
        with open(cache_file, 'rb') as handle:
            cached = pickle.load(handle)
        if cached.get('num_vertices') == num_vertices and cached.get('bone_names') == daz_names:
            captured = cached['weights']
            _log_phase('vendor weights loaded from cache (%s)' % cache_file)
        else:
            _log_phase('weight cache stale (mesh or skeleton changed) — recapturing')
    except Exception as cache_error:
        _log_phase('weight cache unreadable (%s) — recapturing' % cache_error)

if captured is None:
    get_weights = pipeline['_named_call'](unreal.GeometryScript_BoneWeights, ('get_vertex_bone_weights',))
    captured = []
    with unreal.ScopedSlowTask(num_vertices, 'ReposeDazBody: capturing vendor weights (one-time, cached)') as task:
        task.make_dialog(True)
        for vertex_id in range(num_vertices):
            result = get_weights(daz_dyn, vertex_id)
            weight_list, has_weights = None, False
            if isinstance(result, tuple):
                for entry in result:
                    if isinstance(entry, bool):
                        has_weights = entry
                    elif not isinstance(entry, unreal.DynamicMesh) and entry is not None:
                        weight_list = entry
            captured.append([(entry.bone_index, entry.weight) for entry in weight_list]
                            if (has_weights and weight_list is not None) else [])
            if vertex_id % 2048 == 0:
                task.enter_progress_frame(2048)
    _log_phase('vendor weights captured for %d vertices' % num_vertices)
    try:
        os.makedirs(cache_dir, exist_ok=True)
        with open(cache_file, 'wb') as handle:
            pickle.dump({'num_vertices': num_vertices, 'bone_names': daz_names,
                         'weights': captured}, handle, protocol=pickle.HIGHEST_PROTOCOL)
        _log_phase('vendor weights cached -> %s' % cache_file)
    except Exception as cache_error:
        _log_phase('weight cache not written (%s)' % cache_error)

# --- 5. LBS re-pose through the Daz skeleton --------------------------------
position_list = pipeline['_read_all_vertex_positions'](daz_dyn)
if position_list is None:
    raise RuntimeError('could not read the vertex positions')
to_vectors = pipeline['_named_call'](unreal.GeometryScript_List, ('convert_vector_list_to_array',))
positions = to_vectors(position_list)
if len(positions) != num_vertices:
    raise RuntimeError('vertex position list (%d) does not match the vertex count (%d)'
                       % (len(positions), num_vertices))

quat_rotate, vec_add, vec_scale = pipeline['_quat_rotate'], pipeline['_vec_add'], pipeline['_vec_scale']
bone_count = len(daz_bones)
reposed = []
unweighted = 0
for vertex_id in range(num_vertices):
    position = positions[vertex_id]
    vertex = (position.x, position.y, position.z)
    accumulated = (0.0, 0.0, 0.0)
    total_weight = 0.0
    for bone_index, weight in captured[vertex_id]:
        if not (0 <= bone_index < bone_count) or weight <= 0.0:
            continue
        moved = vec_add(new_positions[bone_index],
                        quat_rotate(deltas[bone_index],
                                    vec_sub(vertex, bind_positions[bone_index])))
        accumulated = vec_add(accumulated, vec_scale(moved, weight))
        total_weight += weight
    if total_weight > 1e-6:
        vertex = vec_scale(accumulated, 1.0 / total_weight)
    else:
        unweighted += 1
    reposed.append(unreal.Vector(vertex[0], vertex[1], vertex[2]))
if unweighted:
    unreal.log_warning('ReposeDazBody: %d vertices had no usable weights (left at bind position)' % unweighted)
_log_phase('re-posed %d vertices' % num_vertices)

try:
    to_list = pipeline['_named_call'](unreal.GeometryScript_List, ('convert_array_to_vector_list',))
    list_result = to_list(reposed)
    vector_list = None
    if isinstance(list_result, unreal.GeometryScriptVectorList):
        vector_list = list_result
    elif isinstance(list_result, tuple):
        for entry in list_result:
            if isinstance(entry, unreal.GeometryScriptVectorList):
                vector_list = entry
    if vector_list is None:
        raise RuntimeError('convert_array_to_vector_list returned no list')
    pipeline['_named_call_any'](('GeometryScript_MeshEdits', 'GeometryScript_MeshBasicEdit', 'GeometryScript_BasicEdits'),
                                ('set_all_mesh_vertex_positions',))(daz_dyn, vector_list)
except Exception:
    set_position = pipeline['_named_call_any'](
        ('GeometryScript_MeshEdits', 'GeometryScript_MeshBasicEdit', 'GeometryScript_BasicEdits'),
        ('set_vertex_position',))
    for vertex_id in range(num_vertices):
        set_position(daz_dyn, vertex_id, reposed[vertex_id])
try:
    recompute = pipeline['_named_call_any'](('GeometryScript_Normals', 'GeometryScript_MeshNormals'),
                                            ('recompute_normals',))
    pipeline['_first_if_tuple'](recompute(daz_dyn, unreal.GeometryScriptCalculateNormalsOptions()))
except Exception as normals_error:
    unreal.log_warning('ReposeDazBody: normals not recomputed (%s)' % normals_error)

# --- 6. Bake to the overwritable comparison asset ---------------------------
if unreal.EditorAssetLibrary.does_asset_exist(out_path):
    if not unreal.EditorAssetLibrary.delete_asset(out_path):
        raise RuntimeError('could not overwrite %s — close its editor tab and re-run' % out_path)
create_asset = pipeline['_named_call'](unreal.GeometryScript_NewAssetUtils,
                                       ('create_new_skeletal_mesh_asset_from_mesh',))
create_options = unreal.GeometryScriptCreateNewSkeletalMeshAssetOptions()
try:
    create_options.set_editor_property('use_mesh_bone_proportions', True)
except Exception:
    pass
created = pipeline['_first_if_tuple'](create_asset(daz_dyn, daz_mesh.skeleton, out_path, create_options))
if created is None:
    raise RuntimeError('skeletal mesh asset creation failed at ' + out_path)
try:
    materials = []
    for old_id in kept_indices:
        skeletal_material = unreal.SkeletalMaterial()
        skeletal_material.set_editor_property('material_interface',
                                              daz_mesh.materials[old_id].material_interface)
        skeletal_material.set_editor_property('material_slot_name',
                                              daz_mesh.materials[old_id].material_slot_name)
        materials.append(skeletal_material)
    created.set_editor_property('materials', materials)
except Exception as material_error:
    unreal.log_warning('ReposeDazBody: material assignment failed: %s' % material_error)
unreal.EditorAssetLibrary.save_asset(out_path, only_if_is_dirty=True)
_log_phase('done -> %s  (overlay it on %s in a level to judge the pose)' % (out_path, mh_path))
