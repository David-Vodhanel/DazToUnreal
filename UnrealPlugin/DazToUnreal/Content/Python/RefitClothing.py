"""Surface-transport refit for split clothing on a converted MetaHuman.

The garments were re-posed into the MetaHuman A pose through their own Daz
skeletons, so they hug the RE-POSED DAZ FIGURE — not the committed MetaHuman
body (commit deltas + conform surface changes leave a visible offset). This
script moves each garment vertex by the local difference between those two
surfaces while preserving the authored garment-to-skin gap:

  1. Re-pose the Daz figure's SKIN into the MH A pose with the exact clothing
     machinery (_retarget_daz_pose_to_metahuman + vendor-weight LBS) — that is
     the surface the garments currently align to, by construction.
  2. For each garment vertex: closest point on the re-posed Daz skin gives the
     authored signed normal offset; the corresponding closest point on the
     final MH body plus that same offset is the new position.

THE WRAP IS NOW A PIPELINE STEP (ConvertToMetaHuman: split -> re-pose ->
wrap -> create asset) — fresh converts need no manual refit. This harness
remains for wrap iteration on an already-converted character. DEFAULT writes
preview assets to <pkg>/ClothingRefit/<name>_Refit for side-by-side judging;
--in-place rewrites the bound assets via copy_mesh_to_skeletal_mesh, which is
ANIMATION-UNSAFE: the render-data rebuild corrupted skin weights (garments
tore under animation while the A pose looked fine — bind-pose rendering
ignores weights entirely). Re-run RemaskBodyHider.py after any in-place run.

The slow vendor-weight capture is cached to Saved/DazToUnreal/ (same scheme as
ReposeDazBody.py); only the first run pays it.

Usage (editor python):
  py RefitClothing.py --character=Victoria9WithGloves6           (previews to <pkg>/ClothingRefit/)
  py RefitClothing.py --character=X --in-place       (rewrite the bound assets — ANIMATION-UNSAFE)
  py RefitClothing.py --character=X --items=glove    (only clothing assets whose name matches)
  py RefitClothing.py --character=X --exclude-slots=hair,lash  (extra Daz source slots to drop)

Deletable iteration harness; the production wrap lives in ConvertToMetaHuman.py.
"""

import argparse
import hashlib
import os
import pickle
import time
import unreal

parser = argparse.ArgumentParser(description='Refit split clothing to the committed MetaHuman body surface.')
parser.add_argument('--character', required=True, help='Character folder name under /Game/DazToUnreal/.')
parser.add_argument('--daz', default='', help='Daz skeletal mesh path; default /Game/DazToUnreal/<character>/<character>.')
parser.add_argument('--mh', default='', help='Converted MetaHuman body; default <pkg>/<character>_MHC_Body.')
parser.add_argument('--mh-head', default='',
                    help='Converted MetaHuman head; default <pkg>/<character>_MHC_Head. Appended to the '
                         'wrap target: the body mesh is HEADLESS, so without it collar/strap vertices '
                         'that bound to Daz neck/head skin snap to the body neck-opening rim instead.')
parser.add_argument('--items', default='', help='Comma-separated substrings; only refit matching clothing assets.')
parser.add_argument('--exclude-slots', default='',
                    help='Comma-separated substrings of EXTRA Daz material slots to drop from the wrap source '
                         '(wearable slots and non-skin slots are dropped automatically).')
parser.add_argument('--max-correspondence', type=float, default=10.0,
                    help='Reject a correspondence that jumps farther than this (cm); rejected vertices are '
                         'filled from their garment-topology neighbors.')
parser.add_argument('--min-normal-dot', type=float, default=0.2,
                    help='Reject a correspondence whose target surface normal disagrees with the source '
                         'normal by more than this dot threshold — catches closest-point grabbing the '
                         'WRONG side (opposite inner thigh under a crotch panel).')
parser.add_argument('--smooth-iterations', type=int, default=2,
                    help='Relaxation passes over the displacement field after filling (softens bunching '
                         'where the correspondence changes quickly, e.g. across the bust).')
parser.add_argument('--preview', action='store_true',
                    help='(default behavior; flag kept for compatibility)')
parser.add_argument('--in-place', action='store_true',
                    help='Rewrite the bound clothing assets instead of writing previews. ANIMATION-UNSAFE: '
                         'the copy-back render-data rebuild has corrupted skin weights (garments tear under '
                         'animation). A-pose iteration only — the pipeline wraps before binding.')
parser.add_argument('--force', action='store_true', help='Refit even if the asset is already tagged as refit.')
parser.add_argument('--refresh-weights', action='store_true', help='Ignore the cached vendor-weight capture.')
args = parser.parse_args()

character_name = args.character.strip('"')
package_path = '/Game/DazToUnreal/' + character_name
if args.in_place:
    unreal.log_warning('RefitClothing: --in-place rewrites bound assets — ANIMATION-UNSAFE (the copy-back '
                       'has corrupted skin weights; garments tear under animation). A-pose iteration only; '
                       'reconvert for production garments (the pipeline wraps before binding).')
daz_path = (args.daz.strip('"').split('.')[0]) if args.daz else package_path + '/' + character_name
mh_path = (args.mh.strip('"').split('.')[0]) if args.mh else package_path + '/' + character_name + '_MHC_Body'
REFIT_TAG = 'DazToUnrealRefit'

# Pull the pipeline helpers (retarget, geometry/list utils) out of
# ConvertToMetaHuman.py without running its main flow.
convert_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ConvertToMetaHuman.py')
with open(convert_path, 'r', encoding='utf-8') as convert_file:
    convert_source = convert_file.read()
marker_index = convert_source.index('parser = argparse.ArgumentParser')
pipeline = {}
exec(compile(convert_source[:marker_index], convert_path, 'exec'), pipeline)

_named_call = pipeline['_named_call']
_named_call_any = pipeline['_named_call_any']
_first_if_tuple = pipeline['_first_if_tuple']
vec_sub, vec_add, vec_scale = pipeline['_vec_sub'], pipeline['_vec_add'], pipeline['_vec_scale']
vec_dot, vec_length, vec_cross = pipeline['_vec_dot'], pipeline['_vec_length'], pipeline['_vec_cross']
vec_normalized = pipeline['_vec_normalized']
quat_rotate = pipeline['_quat_rotate']

start_time = time.time()


def _log_phase(message):
    unreal.log('RefitClothing: [%6.1fs] %s' % (time.time() - start_time, message))


daz_mesh = unreal.EditorAssetLibrary.load_asset(daz_path)
if not isinstance(daz_mesh, unreal.SkeletalMesh):
    raise RuntimeError('Daz skeletal mesh not found: ' + daz_path)
mh_mesh = unreal.EditorAssetLibrary.load_asset(mh_path)
if not isinstance(mh_mesh, unreal.SkeletalMesh):
    raise RuntimeError('MetaHuman body mesh not found: ' + mh_path)

# --- 1. Clothing assets + the wearable slots they own ------------------------
item_filters = [entry.strip().lower() for entry in args.items.split(',') if entry.strip()]
clothing_meshes = []
wearable_slots = set()
clothing_folder = package_path + '/Clothing'
for asset_path in unreal.EditorAssetLibrary.list_assets(clothing_folder, recursive=False):
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if not isinstance(asset, unreal.SkeletalMesh):
        continue
    # Every split garment's slots also exist on the merged Daz figure — collect
    # them ALL (not just filtered items) so no garment pollutes the wrap source.
    for material in asset.materials:
        wearable_slots.add(str(material.material_slot_name))
    if not item_filters or any(entry in asset.get_name().lower() for entry in item_filters):
        clothing_meshes.append(asset)
if not clothing_meshes:
    raise RuntimeError('no clothing skeletal meshes to refit under ' + clothing_folder)
_log_phase('%d clothing item(s): %s' % (len(clothing_meshes),
                                        ', '.join(mesh.get_name() for mesh in clothing_meshes)))

# --- 2. Wrap source: the Daz figure's SKIN re-posed into the MH A pose ------
exclude_filters = [entry.strip().lower() for entry in args.exclude_slots.split(',') if entry.strip()]


def _keep_source_slot(slot_name):
    if slot_name in wearable_slots:
        return False
    if any(entry in slot_name.lower() for entry in exclude_filters):
        return False
    return pipeline['_is_daz_skin_slot'](slot_name)


kept_indices = [index for index, material in enumerate(daz_mesh.materials)
                if _keep_source_slot(str(material.material_slot_name))]
dropped = [str(material.material_slot_name) for index, material in enumerate(daz_mesh.materials)
           if index not in kept_indices]
if not kept_indices:
    raise RuntimeError('no Daz skin slots survive the source filter')
_log_phase('wrap source keeps %d/%d slots (dropped: %s)'
           % (len(kept_indices), len(daz_mesh.materials), ', '.join(dropped) if dropped else 'none'))

source_dyn = pipeline['_copy_skeletal_mesh_to_dynamic_mesh'](daz_mesh)
pipeline['_prune_dynamic_mesh_to_slots'](source_dyn, daz_mesh,
                                         lambda name: _keep_source_slot(name))
compact = _named_call_any(('GeometryScript_MeshRepair', 'GeometryScript_MeshRepairFunctions'), ('compact_mesh',))
_first_if_tuple(compact(source_dyn))
target_dyn = pipeline['_copy_skeletal_mesh_to_dynamic_mesh'](mh_mesh)

daz_bones = pipeline['_all_bones_info'](source_dyn)
mh_bones = pipeline['_all_bones_info'](target_dyn)
daz_names = [str(info.name) for info in daz_bones]

# The exported body is HEADLESS (two-mesh MetaHuman) — append the head's skin
# so garment vertices that bound to Daz neck/head skin land on real neck/head
# surface instead of snapping to the body's neck-opening rim. Appended AFTER
# the bone read above so the retarget only sees the body skeleton.
mh_head_path = (args.mh_head.strip('"').split('.')[0]) if args.mh_head \
    else package_path + '/' + character_name + '_MHC_Head'
head_mesh = unreal.EditorAssetLibrary.load_asset(mh_head_path)
if isinstance(head_mesh, unreal.SkeletalMesh):
    head_dyn = pipeline['_copy_skeletal_mesh_to_dynamic_mesh'](head_mesh)
    # Skin sections only — eyeballs/teeth/lash cards must not attract bindings.
    pipeline['_prune_dynamic_mesh_to_slots'](head_dyn, head_mesh,
                                             lambda name: name.lower().startswith('head'))
    append_mesh = _named_call_any(
        ('GeometryScript_MeshEdits', 'GeometryScript_MeshBasicEdit', 'GeometryScript_BasicEdits'),
        ('append_mesh',))
    append_mesh(target_dyn, head_dyn, unreal.Transform())
    _log_phase('MetaHuman head skin appended to the wrap target (%s)' % mh_head_path)
else:
    unreal.log_warning('RefitClothing: head mesh not found at %s — collar/strap vertices near the '
                       'neck will distort toward the body neck rim' % mh_head_path)

bind_positions, deltas, new_positions, stats = \
    pipeline['_retarget_daz_pose_to_metahuman'](daz_bones, mh_bones)
_log_phase('retarget: %d/%d Daz bones aimed, max joint drift %.1f cm'
           % (stats['mapped'], len(daz_bones), stats['max_move']))

# Vendor weights for the pruned source (cached; same scheme as ReposeDazBody).
get_num_vertices = _named_call(unreal.GeometryScript_MeshQueries, ('get_num_vertex_i_ds', 'get_num_vertex_ids'))
num_source_vertices = _first_if_tuple(get_num_vertices(source_dyn))
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
        if cached.get('num_vertices') == num_source_vertices and cached.get('bone_names') == daz_names:
            captured = cached['weights']
            _log_phase('vendor weights loaded from cache (%s)' % cache_file)
        else:
            _log_phase('weight cache stale — recapturing')
    except Exception as cache_error:
        _log_phase('weight cache unreadable (%s) — recapturing' % cache_error)

if captured is None:
    get_weights = _named_call(unreal.GeometryScript_BoneWeights, ('get_vertex_bone_weights',))
    captured = []
    with unreal.ScopedSlowTask(num_source_vertices, 'RefitClothing: capturing vendor weights (one-time, cached)') as task:
        task.make_dialog(True)
        for vertex_id in range(num_source_vertices):
            result = get_weights(source_dyn, vertex_id)
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
    _log_phase('vendor weights captured for %d vertices' % num_source_vertices)
    try:
        os.makedirs(cache_dir, exist_ok=True)
        with open(cache_file, 'wb') as handle:
            pickle.dump({'num_vertices': num_source_vertices, 'bone_names': daz_names,
                         'weights': captured}, handle, protocol=pickle.HIGHEST_PROTOCOL)
    except Exception as cache_error:
        _log_phase('weight cache not written (%s)' % cache_error)

# LBS re-pose of the source skin through the Daz skeleton.
to_vectors = _named_call(unreal.GeometryScript_List, ('convert_vector_list_to_array',))
source_positions = to_vectors(pipeline['_read_all_vertex_positions'](source_dyn))
bone_count = len(daz_bones)
reposed = []
for vertex_id in range(num_source_vertices):
    position = source_positions[vertex_id]
    vertex = (position.x, position.y, position.z)
    accumulated = (0.0, 0.0, 0.0)
    total_weight = 0.0
    for bone_index, weight in captured[vertex_id]:
        if not (0 <= bone_index < bone_count) or weight <= 0.0:
            continue
        moved = vec_add(new_positions[bone_index],
                        quat_rotate(deltas[bone_index], vec_sub(vertex, bind_positions[bone_index])))
        accumulated = vec_add(accumulated, vec_scale(moved, weight))
        total_weight += weight
    if total_weight > 1e-6:
        vertex = vec_scale(accumulated, 1.0 / total_weight)
    reposed.append(unreal.Vector(vertex[0], vertex[1], vertex[2]))
to_list = _named_call(unreal.GeometryScript_List, ('convert_array_to_vector_list',))
vector_list = None
list_result = to_list(reposed)
for entry in (list_result if isinstance(list_result, tuple) else (list_result,)):
    if isinstance(entry, unreal.GeometryScriptVectorList):
        vector_list = entry
if vector_list is None:
    raise RuntimeError('convert_array_to_vector_list returned no list')
_named_call_any(('GeometryScript_MeshEdits', 'GeometryScript_MeshBasicEdit', 'GeometryScript_BasicEdits'),
                ('set_all_mesh_vertex_positions',))(source_dyn, vector_list)
_log_phase('source skin re-posed (%d vertices)' % num_source_vertices)

# --- 3. Spatial queries -------------------------------------------------------
spatial_owner_names = ('GeometryScript_MeshSpatial', 'GeometryScript_SpatialQuery', 'GeometryScript_Spatial')
build_bvh = _named_call_any(spatial_owner_names, ('build_bvh_for_mesh',))
find_nearest = _named_call_any(spatial_owner_names, ('find_nearest_point_on_mesh',))
get_triangle_positions = _named_call(unreal.GeometryScript_MeshQueries, ('get_triangle_positions',))
query_options = unreal.GeometryScriptSpatialQueryOptions()


def _mesh_bvh(dynamic_mesh):
    result = build_bvh(dynamic_mesh)
    for entry in (result if isinstance(result, tuple) else (result,)):
        if isinstance(entry, unreal.GeometryScriptDynamicMeshBVH):
            return entry
    raise RuntimeError('build_bvh_for_mesh returned no BVH')


def _nearest_point(dynamic_mesh, bvh, point):
    """GeometryScriptTrianglePoint nearest to `point`, or None."""
    result = find_nearest(dynamic_mesh, bvh, unreal.Vector(point[0], point[1], point[2]), query_options)
    for entry in (result if isinstance(result, tuple) else (result,)):
        if isinstance(entry, unreal.GeometryScriptTrianglePoint):
            if not getattr(entry, 'valid', getattr(entry, 'b_valid', True)):
                return None
            return entry
    return None


def _triangle_normal(dynamic_mesh, triangle_id, cache):
    normal = cache.get(triangle_id)
    if normal is None:
        result = get_triangle_positions(dynamic_mesh, triangle_id)
        corners = [entry for entry in (result if isinstance(result, tuple) else (result,))
                   if isinstance(entry, unreal.Vector)]
        if len(corners) < 3:
            return None
        a = (corners[0].x, corners[0].y, corners[0].z)
        b = (corners[1].x, corners[1].y, corners[1].z)
        c = (corners[2].x, corners[2].y, corners[2].z)
        normal = vec_normalized(vec_cross(vec_sub(b, a), vec_sub(c, a)))
        cache[triangle_id] = normal
    return normal


source_bvh = _mesh_bvh(source_dyn)
target_bvh = _mesh_bvh(target_dyn)
source_normals, target_normals = {}, {}
_log_phase('BVHs built')

# --- 4. Wrap each garment ------------------------------------------------------
recompute_normals = _named_call_any(('GeometryScript_Normals', 'GeometryScript_MeshNormals'), ('recompute_normals',))
set_positions_call = _named_call_any(('GeometryScript_MeshEdits', 'GeometryScript_MeshBasicEdit', 'GeometryScript_BasicEdits'),
                                     ('set_all_mesh_vertex_positions',))

for clothing_mesh in clothing_meshes:
    label = clothing_mesh.get_name()
    asset_path = clothing_folder + '/' + label
    if args.in_place and not args.force:
        if str(unreal.EditorAssetLibrary.get_metadata_tag(clothing_mesh, REFIT_TAG)) == 'applied':
            unreal.log_warning('RefitClothing: %s already refit — skipping (reconvert for fresh assets, '
                               'or pass --force if you really want to re-wrap moved geometry)' % label)
            continue

    cloth_dyn = pipeline['_copy_skeletal_mesh_to_dynamic_mesh'](clothing_mesh)
    cloth_positions = to_vectors(pipeline['_read_all_vertex_positions'](cloth_dyn))
    vertex_count = len(cloth_positions)

    # Pass 1: raw correspondence per vertex. A correspondence is REJECTED (not
    # applied) when the jump exceeds --max-correspondence or the target surface
    # faces away from the source surface — closest-point grabbing the WRONG
    # side, e.g. the opposite inner thigh under a crotch panel; applying that
    # drags the vertex across the gap and tears the garment.
    displacements = [None] * vertex_count
    skipped, capped, flipped = 0, 0, 0
    max_correspondence = 0.0
    with unreal.ScopedSlowTask(vertex_count, 'RefitClothing: wrapping %s' % label) as task:
        task.make_dialog(True)
        for vertex_id in range(vertex_count):
            position = cloth_positions[vertex_id]
            vertex = (position.x, position.y, position.z)
            source_hit = _nearest_point(source_dyn, source_bvh, vertex)
            source_normal = (None if source_hit is None
                             else _triangle_normal(source_dyn, source_hit.triangle_id, source_normals))
            if source_normal is None:
                skipped += 1
            else:
                source_point = (source_hit.position.x, source_hit.position.y, source_hit.position.z)
                target_hit = _nearest_point(target_dyn, target_bvh, source_point)
                target_normal = (None if target_hit is None
                                 else _triangle_normal(target_dyn, target_hit.triangle_id, target_normals))
                if target_normal is None:
                    skipped += 1
                else:
                    target_point = (target_hit.position.x, target_hit.position.y, target_hit.position.z)
                    correspondence = vec_length(vec_sub(target_point, source_point))
                    max_correspondence = max(max_correspondence, correspondence)
                    if correspondence > args.max_correspondence:
                        capped += 1
                    elif vec_dot(source_normal, target_normal) < args.min_normal_dot:
                        flipped += 1
                    else:
                        offset = vec_dot(vec_sub(vertex, source_point), source_normal)
                        new_vertex = vec_add(target_point, vec_scale(target_normal, offset))
                        displacements[vertex_id] = vec_sub(new_vertex, vertex)
            if vertex_id % 512 == 0:
                task.enter_progress_frame(512)

    # Pass 2: fill rejected/unresolved vertices from their garment-topology
    # neighbors (iterative flood fill of the displacement field). Leaving a
    # mid-garment vertex at its authored position IS a tear.
    triangles = pipeline['_read_all_triangles'](cloth_dyn)
    neighbors = [set() for _ in range(vertex_count)]
    if triangles is not None:
        for triangle in triangles:
            corners = (triangle.x, triangle.y, triangle.z)
            for a in range(3):
                for b in range(3):
                    if a != b and 0 <= corners[a] < vertex_count and 0 <= corners[b] < vertex_count:
                        neighbors[corners[a]].add(corners[b])
    neighbors = [list(entry) for entry in neighbors]
    unresolved = [v for v in range(vertex_count) if displacements[v] is None]
    unresolved_start = len(unresolved)
    fill_rounds = 0
    while unresolved and fill_rounds < 50:
        fill_rounds += 1
        filled_this_round = {}
        remaining = []
        for vertex_id in unresolved:
            total, count = (0.0, 0.0, 0.0), 0
            for other in neighbors[vertex_id]:
                if displacements[other] is not None:
                    total = vec_add(total, displacements[other])
                    count += 1
            if count:
                filled_this_round[vertex_id] = vec_scale(total, 1.0 / count)
            else:
                remaining.append(vertex_id)
        if not filled_this_round:
            break
        for vertex_id, value in filled_this_round.items():
            displacements[vertex_id] = value
        unresolved = remaining
    for vertex_id in unresolved:
        displacements[vertex_id] = (0.0, 0.0, 0.0)  # island with no resolved vertex — stays authored

    # Pass 3: relax the displacement field so correspondence discontinuities
    # (bust curvature, chart borders) blend instead of bunching.
    for _ in range(max(0, args.smooth_iterations)):
        smoothed = list(displacements)
        for vertex_id in range(vertex_count):
            if not neighbors[vertex_id]:
                continue
            total = (0.0, 0.0, 0.0)
            for other in neighbors[vertex_id]:
                total = vec_add(total, displacements[other])
            average = vec_scale(total, 1.0 / len(neighbors[vertex_id]))
            smoothed[vertex_id] = vec_add(vec_scale(displacements[vertex_id], 0.5),
                                          vec_scale(average, 0.5))
        displacements = smoothed

    moved = []
    max_move, total_move = 0.0, 0.0
    for vertex_id in range(vertex_count):
        position = cloth_positions[vertex_id]
        displacement = displacements[vertex_id]
        step = vec_length(displacement)
        max_move = max(max_move, step)
        total_move += step
        moved.append(unreal.Vector(position.x + displacement[0],
                                   position.y + displacement[1],
                                   position.z + displacement[2]))

    resolved = vertex_count - skipped - capped - flipped
    _log_phase('%s: %d/%d vertices resolved directly (%d capped, %d wrong-side, %d no-hit; '
               '%d filled from neighbors in %d rounds), mean move %.2f cm, max %.2f cm, '
               'max correspondence %.2f cm'
               % (label, resolved, vertex_count, capped, flipped, skipped,
                  unresolved_start, fill_rounds,
                  (total_move / vertex_count) if vertex_count else 0.0,
                  max_move, max_correspondence))

    list_result = to_list(moved)
    vector_list = None
    for entry in (list_result if isinstance(list_result, tuple) else (list_result,)):
        if isinstance(entry, unreal.GeometryScriptVectorList):
            vector_list = entry
    set_positions_call(cloth_dyn, vector_list)
    try:
        _first_if_tuple(recompute_normals(cloth_dyn, unreal.GeometryScriptCalculateNormalsOptions()))
    except Exception as normals_error:
        unreal.log_warning('RefitClothing: %s: normals not recomputed (%s)' % (label, normals_error))

    if not args.in_place:
        out_path = package_path + '/ClothingRefit/' + label + '_Refit'
        if unreal.EditorAssetLibrary.does_asset_exist(out_path):
            pipeline['_close_asset_editors'](out_path)  # GC-crash guard
            if not unreal.EditorAssetLibrary.delete_asset(out_path):
                raise RuntimeError('could not overwrite %s — close its editor tab and re-run' % out_path)
        create_asset = _named_call(unreal.GeometryScript_NewAssetUtils, ('create_new_skeletal_mesh_asset_from_mesh',))
        create_options = unreal.GeometryScriptCreateNewSkeletalMeshAssetOptions()
        try:
            create_options.set_editor_property('use_mesh_bone_proportions', True)
        except Exception:
            pass
        created = _first_if_tuple(create_asset(cloth_dyn, clothing_mesh.skeleton, out_path, create_options))
        if created is None:
            raise RuntimeError('preview asset creation failed at ' + out_path)
        try:
            created.set_editor_property('materials', list(clothing_mesh.materials))
        except Exception as material_error:
            unreal.log_warning('RefitClothing: %s: preview material assignment failed: %s' % (label, material_error))
        unreal.EditorAssetLibrary.save_asset(out_path, only_if_is_dirty=True)
        _log_phase('%s: preview written -> %s' % (label, out_path))
    else:
        pipeline['_close_asset_editors'](asset_path)  # GC-crash guard
        copy_to_asset = _named_call(unreal.GeometryScript_AssetUtils, ('copy_mesh_to_skeletal_mesh',))
        copy_options = unreal.GeometryScriptCopyMeshToAssetOptions()
        try:
            copy_options.set_editor_property('enable_recompute_normals', False)
            copy_options.set_editor_property('enable_recompute_tangents', True)
        except Exception:
            pass
        saved_materials = list(clothing_mesh.materials)
        outcome = copy_to_asset(cloth_dyn, clothing_mesh, copy_options,
                                unreal.GeometryScriptMeshWriteLOD(lod_index=0))
        del outcome  # non-tuple outcomes vary by version; failures raise instead
        # The asset copy can rebuild sections and drop slot assignments —
        # reassert the original material list unconditionally.
        try:
            clothing_mesh.set_editor_property('materials', saved_materials)
        except Exception as material_error:
            unreal.log_warning('RefitClothing: %s: material restore failed: %s' % (label, material_error))
        unreal.EditorAssetLibrary.set_metadata_tag(clothing_mesh, REFIT_TAG, 'applied')
        unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=True)
        _log_phase('%s: refit in place' % label)

pipeline['_flush_python_wrappers']('refit')
_log_phase('done. Re-run: py RemaskBodyHider.py --character=%s (the hide mask was baked pre-refit)'
           % character_name)
