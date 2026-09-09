"""Re-run ONLY the body hide-mask step (step 12 of ConvertToMetaHuman.py) on an
already-converted character, for fast iteration on the coverage reach and
erode/blur settings without a full ~5 min reconvert. Reuses the pipeline's own
helpers by exec'ing ConvertToMetaHuman.py's definitions (everything above its
argparse marker), so a constant tweak there is picked up here automatically.

Limitations: treats ALL split clothing items as opaque (transparency filtering
needs the DTU — run a full convert for outfits with sheer garments) and leaves
the stored DazMetaHumanSourceData settings untouched (same mask asset path, so
its soft reference stays valid).

Usage (editor python): py RemaskBodyHider.py --character=Victoria9TargetMH

Deletable diagnostic; not part of the conversion pipeline.
"""

import argparse
import os
import unreal

parser = argparse.ArgumentParser(description='Rebuild the body hide mask for a converted MetaHuman character.')
parser.add_argument('--character', default='Victoria9TargetMH', help='Character folder name under /Game/DazToUnreal/.')
args = parser.parse_args()

character_name = args.character.strip('"')
package_path = '/Game/DazToUnreal/' + character_name

# Pull the pipeline helpers out of ConvertToMetaHuman.py without running its
# main flow: exec everything above the argparse marker into a namespace.
convert_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ConvertToMetaHuman.py')
with open(convert_path, 'r', encoding='utf-8') as convert_file:
    convert_source = convert_file.read()
marker_index = convert_source.index('parser = argparse.ArgumentParser')
pipeline = {}
exec(compile(convert_source[:marker_index], convert_path, 'exec'), pipeline)

body_mesh_path = package_path + '/' + character_name + '_MHC_Body'
body_mesh = unreal.EditorAssetLibrary.load_asset(body_mesh_path)
if body_mesh is None:
    raise RuntimeError('body mesh not found: ' + body_mesh_path)

clothing_meshes = []
clothing_folder = package_path + '/Clothing'
for asset_path in unreal.EditorAssetLibrary.list_assets(clothing_folder, recursive=False):
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if isinstance(asset, unreal.SkeletalMesh):
        clothing_meshes.append(asset)
if not clothing_meshes:
    raise RuntimeError('no clothing skeletal meshes under ' + clothing_folder)
unreal.log('RemaskBodyHider: %d clothing item(s): %s' % (
    len(clothing_meshes), ', '.join(mesh.get_name() for mesh in clothing_meshes)))

coverage_items = [(mesh, lambda slot: True) for mesh in clothing_meshes]
coverage_texture = pipeline['_bake_clothing_coverage'](body_mesh, coverage_items)
if coverage_texture is None:
    raise RuntimeError('coverage bake produced nothing — see log for per-item warnings')

hide_mask_path = package_path + '/Skin/BakedTextures/T_' + character_name + '_BodyHideMask'
pipeline['_close_asset_editors'](hide_mask_path)  # GC-crash guard
hide_mask = pipeline['_create_hide_mask'](coverage_texture, hide_mask_path)
if hide_mask is None:
    raise RuntimeError('hide mask build failed')

unreal.DazToUnrealBlueprintUtils.set_body_hide_mask_material_params(
    body_mesh, hide_mask,
    pipeline['HIDE_MASK_MAX_CULL_VALUE'], pipeline['HIDE_MASK_MIN_KEEP_VALUE'],
    pipeline['HIDE_MASK_MAX_SHRINK_DISTANCE'])
unreal.EditorAssetLibrary.save_asset(hide_mask_path, only_if_is_dirty=True)
pipeline['_flush_python_wrappers']('remask')
unreal.log('RemaskBodyHider: done — reach %.1f, mask %s' % (
    pipeline['COVERAGE_PROJECTION_DISTANCE'], hide_mask_path))
