"""EXPERIMENT: re-estimate the MetaHuman body joints from the A-posed geometry.

DiagnoseBodySkeletons proved the exported FK hand chain is stranded at the
DAZ-pose hand location (hand_l 11cm, finger joints 15-20cm from the skin they
dominate) while the mcp/pip/wrist helper joints sit exactly ON the committed
geometry. Cause: estimate_body_joints_from_mesh ran against the DAZ-POSED
conform target, then commit_posed_state_as_a_pose re-posed the geometry to
the MetaHuman A pose and the FK joints never followed.

This script runs a SECOND conform pass whose target is the character's OWN
committed A-pose geometry (the <name>_MHCombined utility mesh — full face+body
in the final A pose). The shape solve is a no-op (target == own shape) but the
joint estimate now sees an A-POSED mesh, so the FK chain should land inside
the hands. Commits, re-exports the body to <pkg>/DiagRefit/, and measures the
refit skeleton (bone vs dominated-skin centroid) against the old one.

Does NOT save the character or overwrite the real _MHC_Body — inspect the
DiagRefit output first. If this passes, the fix folds into ConvertToMetaHuman
as a post-commit re-estimate pass before the final export.

Usage (editor python): py ReestimateBodyJoints.py --character=NewTest3

Deletable diagnostic; not part of the conversion pipeline (yet).
"""

import argparse
import math
import os
import unreal

parser = argparse.ArgumentParser(description='Re-estimate MetaHuman body joints from the committed A-pose geometry.')
parser.add_argument('--character', default='NewTest3', help='Character folder name under /Game/DazToUnreal/.')
args = parser.parse_args()

character_name = args.character.strip('"')
package_path = '/Game/DazToUnreal/' + character_name

convert_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ConvertToMetaHuman.py')
with open(convert_path, 'r', encoding='utf-8') as convert_file:
    convert_source = convert_file.read()
marker_index = convert_source.index('parser = argparse.ArgumentParser')
pipeline = {}
exec(compile(convert_source[:marker_index], convert_path, 'exec'), pipeline)

character = unreal.EditorAssetLibrary.load_asset(package_path + '/' + character_name + '_MHC')
if character is None:
    raise RuntimeError('character asset not found: ' + package_path + '/' + character_name + '_MHC')
old_body = unreal.EditorAssetLibrary.load_asset(package_path + '/' + character_name + '_MHC_Body')
if not isinstance(old_body, unreal.SkeletalMesh):
    raise RuntimeError('final body mesh not found')

# Target = the committed A-posed geometry. The combined utility mesh is the
# whole character in the final A pose; fall back to the headless body if the
# combined mesh is missing (neck/head joint estimates will be unreliable).
combined_mesh = unreal.EditorAssetLibrary.load_asset(package_path + '/' + character_name + '_MHCombined')
use_combined = isinstance(combined_mesh, unreal.SkeletalMesh)
target_mesh_asset = combined_mesh if use_combined else old_body
if not use_combined:
    unreal.log_warning('ReestimateBodyJoints: %s_MHCombined not found — using the headless body as the target'
                       % character_name)

subsystem = unreal.get_editor_subsystem(unreal.MetaHumanCharacterEditorSubsystem)
added_for_edit = False
if subsystem.is_object_added_for_editing(character):
    unreal.log('ReestimateBodyJoints: character already open for edit — reusing the session')
else:
    if not subsystem.try_add_object_to_edit(character):
        raise RuntimeError('unable to open the character for edit')
    added_for_edit = True

try:
    mesh_data = pipeline['_unwrap_conform_mesh_data'](
        subsystem.get_mesh_data_for_conforming(target_mesh_asset))
    target_vertices, target_indices = mesh_data
    unreal.log('ReestimateBodyJoints: conform target %s — %d verts, re-running joint estimate on the A pose...'
               % (target_mesh_asset.get_name(), len(target_vertices)))

    conform_params = unreal.ConformTargetParams()
    conform_params.conform_target_mesh.target_parts_type = \
        unreal.TargetPartsType.COMBINED if use_combined else unreal.TargetPartsType.BODY_ONLY
    conform_params.conform_target_mesh.body_vertices = target_vertices
    conform_params.conform_target_mesh.body_vertex_indices = target_indices
    conform_params.estimate_body_joints_from_mesh = True
    conform_params.auto_solve = True
    conform_params.body_conform_solve_settings.pipeline_name = 'combined' if use_combined else 'body_only'

    target_mesh_key = unreal.MetaHumanCharacterTargetMeshKey()
    if use_combined:
        target_mesh_key.combined_mesh = target_mesh_asset
    else:
        target_mesh_key.body_mesh = target_mesh_asset

    if not subsystem.conform_to_target_meshes(character, target_mesh_key, conform_params):
        raise RuntimeError('conform_to_target_meshes failed')
    subsystem.commit_posed_state_as_a_pose(character, target_mesh_key)

    refit_path = package_path + '/DiagRefit'
    geometry_params = unreal.MetaHumanGeometryExportParams()
    geometry_params.project_path = refit_path
    geometry_params.head_skeletal_mesh = False
    geometry_params.body_skeletal_mesh = True
    geometry_params.full_body_skeletal_mesh = False
    geometry_params.overwrite_existing_assets = True
    unreal.MetaHumanCharacterExportBlueprintLibrary.export_geometry(character, geometry_params)
finally:
    if added_for_edit and subsystem.is_object_added_for_editing(character):
        subsystem.remove_object_to_edit(character)

refit_body = None
for asset_path in unreal.EditorAssetLibrary.list_assets(refit_path, recursive=True):
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if isinstance(asset, unreal.SkeletalMesh) and asset.get_name().endswith('_Body'):
        refit_body = asset
        break
if refit_body is None:
    raise RuntimeError('no _Body skeletal mesh found under ' + refit_path)


def _bone_positions(skeletal_mesh):
    dynamic_mesh = pipeline['_copy_skeletal_mesh_to_dynamic_mesh'](skeletal_mesh)
    positions = {}
    for info in pipeline['_all_bones_info'](dynamic_mesh):
        translation = info.world_transform.translation
        positions[str(info.name)] = (translation.x, translation.y, translation.z)
    return positions


def _dominated_centroids(skeletal_mesh):
    dynamic_mesh = pipeline['_copy_skeletal_mesh_to_dynamic_mesh'](skeletal_mesh)
    bone_names = [str(info.name) for info in pipeline['_all_bones_info'](dynamic_mesh)]
    get_weights = pipeline['_named_call'](unreal.GeometryScript_BoneWeights, ('get_vertex_bone_weights',))
    num_vertices = pipeline['_first_if_tuple'](pipeline['_named_call'](
        unreal.GeometryScript_MeshQueries, ('get_num_vertex_i_ds', 'get_num_vertex_ids'))(dynamic_mesh))
    to_vectors = pipeline['_named_call'](unreal.GeometryScript_List, ('convert_vector_list_to_array',))
    vertex_positions = to_vectors(pipeline['_read_all_vertex_positions'](dynamic_mesh))
    sums, counts = {}, {}
    for vertex_id in range(num_vertices):
        result = get_weights(dynamic_mesh, vertex_id)
        weight_list, has_weights = None, False
        if isinstance(result, tuple):
            for entry in result:
                if isinstance(entry, bool):
                    has_weights = entry
                elif not isinstance(entry, unreal.DynamicMesh) and entry is not None:
                    weight_list = entry
        if not has_weights or not weight_list:
            continue
        best = max(weight_list, key=lambda entry: entry.weight)
        if not (0 <= best.bone_index < len(bone_names)):
            continue
        name = bone_names[best.bone_index]
        position = vertex_positions[vertex_id]
        if name in sums:
            sums[name][0] += position.x
            sums[name][1] += position.y
            sums[name][2] += position.z
            counts[name] += 1
        else:
            sums[name] = [position.x, position.y, position.z]
            counts[name] = 1
    return {name: (value[0] / counts[name], value[1] / counts[name], value[2] / counts[name])
            for name, value in sums.items()}


def _dist(a, b):
    if a is None or b is None:
        return None
    return math.sqrt(sum((a[i] - b[i]) ** 2 for i in range(3)))


old_positions = _bone_positions(old_body)
new_positions = _bone_positions(refit_body)
new_centroids = _dominated_centroids(refit_body)

PROBE_BONES = ['clavicle_l', 'upperarm_l', 'lowerarm_l', 'hand_l',
               'index_metacarpal_l', 'middle_metacarpal_l', 'pinky_metacarpal_l', 'thumb_01_l',
               'index_01_l', 'index_03_l', 'middle_01_l', 'middle_03_l', 'pinky_03_l',
               'thigh_l', 'foot_l', 'ball_l']
unreal.log('ReestimateBodyJoints: --- refit skeleton vs old, and refit bone vs its own skin ---')
unreal.log('ReestimateBodyJoints: %-24s %9s %9s' % ('bone', 'moved cm', 'skin cm'))
for bone in PROBE_BONES:
    moved = _dist(old_positions.get(bone), new_positions.get(bone))
    skin = _dist(new_positions.get(bone), new_centroids.get(bone))
    unreal.log('ReestimateBodyJoints: %-24s %9s %9s'
               % (bone,
                  '%7.2f' % moved if moved is not None else '      -',
                  '%7.2f' % skin if skin is not None else '      -'))
hand_bones = [name for name in new_positions
              if any(part in name for part in ('hand_', 'index_0', 'middle_0', 'ring_0', 'pinky_0', 'thumb_0'))
              and 'mcp' not in name and 'pip' not in name]
worst = max((_dist(new_positions[name], new_centroids.get(name)) or 0.0) for name in hand_bones)
unreal.log('ReestimateBodyJoints: worst FK hand-chain bone-to-own-skin distance after refit: %.2f cm '
           '(was 11-20 before; <5 = joints landed inside the hand)' % worst)
unreal.log('ReestimateBodyJoints: refit body: %s — character NOT saved; inspect before adopting'
           % refit_body.get_path_name())
