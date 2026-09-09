"""Apply the groom selections from a Daz-converted MetaHumanCharacter to the
converted character assets.

Run from the Apply Grooms button on the MetaHuman Character editor toolbar
(added by the DazToUnreal plugin), or manually:

    py ApplyGrooms.py --character=/Game/DazToUnreal/<name>/<name>_MHC

What it does:
    1. Reads the character's wardrobe slot selections (Hair, Eyebrows, Beard,
       Eyelashes, ... whatever slots have grooms selected in the MetaHuman
       Character editor).
    2. Resolves each selection to its UGroomAsset.
    3. Creates a UGroomBindingAsset per groom targeting the conversion's
       exported <name>_MHC_Head mesh. The grooms are authored against the
       MetaHuman face archetype and our head is that same topology conformed
       to the Daz likeness, so the RBF transfer is the same quality real
       MetaHuman assembly gets. Bindings land in <package>/Grooms.
    4. Attaches one GroomComponent per slot (named Groom_<Slot>) to the
       BP_<name>_MH character blueprint as children of the Face component,
       removing any Groom_* components from a previous run first.
    5. Builds the follicle mask (T_<name>_FollicleMask in <package>/Grooms) from
       the bound grooms and assigns it to the 'Hair Mask' parameter on every
       head material LOD - the scalp-darkening the official MetaHuman assembly
       does, without which scalp skin shows through the strands as bald spots.
       (The 'Use Hair Mask' static switch is already true via the parent
       material, so only the texture parameter is set - no shader recompile.)
       Because the likeness bake's 'Use Texture Override' branch bypasses that
       darkening, the mask is ALSO baked into the override basecolor
       (T_..._BaseColor_Baked_FollicleBaked) and the head MICs repointed at it -
       same approach official assembly takes when it bakes the follicle map into
       the face textures. Tune FOLLICLE_BAKE_* below and re-run to iterate.

Appearance parameters (hair dye/melanin etc. picked in the editor) are NOT
applied yet - grooms render with their default materials. That's the next
step once slot selection flow is verified.

Card eyelashes are part of the head model itself: if the character's eyelash
type was still None at conversion time the exported head has empty lash
sections, and this script can only add strand-groom lashes if an Eyelashes
wardrobe selection exists. Set the lash type before converting for card lashes.
"""

import gc
import json
import sys
import unreal

LOG_PREFIX = 'ApplyGrooms.py: '

# Official MetaHuman assembly channel mapping for the follicle mask
# (MetaHumanDefaultEditorPipelineBase.cpp): Hair=R, Eyebrows=G, Beard/Mustache=B.
# Slots not listed here (Eyelashes, ...) don't contribute to scalp darkening.
FOLLICLE_CHANNELS = {'hair': 0, 'eyebrows': 1, 'beard': 2, 'mustache': 2}
FOLLICLE_MASK_PARAM = 'Hair Mask'  # verified on the MH head shader MICs
FOLLICLE_MASK_RESOLUTION = 4096
FOLLICLE_MASK_ROOT_RADIUS = 8

# The likeness bake's 'Use Texture Override' branch bypasses the shader's Hair Mask
# darkening, so the follicle shadow must be baked INTO the override basecolor (official
# assembly bakes it into the face textures the same way). The engine's own multiply is
# subtle (0.86 / sat 0.5) because MetaHuman synthesized textures already carry a scalp
# tint - the Daz-baked albedo has none, so these defaults darken harder. Tune visually
# and re-run Apply Grooms; the bake always starts from the pristine _Baked texture.
FOLLICLE_BAKE_HAIR_MULTIPLY = (0.30, 0.25, 0.22)
FOLLICLE_BAKE_EYEBROWS_MULTIPLY = (0.766, 0.766, 0.766)
FOLLICLE_BAKE_BEARD_MULTIPLY = (0.766, 0.766, 0.766)
FOLLICLE_BAKE_STRENGTH = 1.0
# The mask's mip 0 is per-root dots; the bake fills between them with a blurred
# coverage field scaled by this gain (higher = scalp shadow saturates with fewer
# roots nearby; 0 = dots only, the pre-2026-08 behavior)
FOLLICLE_BAKE_BROAD_GAIN = 8.0

# Hair color: the MHC editor stores per-wardrobe-item color edits in the
# instance's property bags (only values the user actually touched). Bag property
# -> MI_Hair-family material parameter, from
# UMetaHumanDefaultGroomPipelineMaterialParameters (MetaHumanDefaultGroomPipeline.h,
# UE 5.8). Absent values keep the groom material defaults - same as the MHC preview.
GROOM_COLOR_SCALARS = {
    'Melanin': 'hairMelanin',
    'Redness': 'hairRedness',
    'Roughness': 'HairRoughness',
    'Whiteness': 'WhiteAmount',
    'Lightness': 'LightAmount',
    'OmbreU': 'OmbreMelanin',
    'OmbreV': 'OmbreRedness',
    'OmbreShift': 'OmbreShift',
    'OmbreContrast': 'OmbreContrast',
    'OmbreIntensity': 'OmbreIntensity',
    'RegionsU': 'RegionMelanin',
    'RegionsV': 'RegionRedness',
    'HighlightsU': 'HighlightsMelanin',
    'HighlightsV': 'HighlightsRedness',
    'HighlightsBlending': 'HighlightsBlending',
    'HighlightsIntensity': 'HighlightsIntensity',
    'HighlightsVariation': 'HighlightsVariationNumber',
}
GROOM_COLOR_BOOLS = {
    'bUseOmbre': 'Ombre',
    'bUseRegions': 'Region',
    'bUseHighlights': 'Highlights',
}
GROOM_COLOR_VECTORS = {
    'DyeColor': 'HairDye',
    'OmbreColor': 'OmbreHairDye',
    'RegionsColor': 'RegionhairDye',
    'HighlightsColor': 'HighlightsHairDye',
}
FOLLICLE_BAKE_SUFFIX = '_FollicleBaked'


def _log(message):
    unreal.log(LOG_PREFIX + message)


def _warn(message):
    unreal.log_warning(LOG_PREFIX + message)


def _parse_args():
    """--character=/Game/... ; tolerate literal quotes (see ConvertToMetaHuman notes)."""
    args = {}
    for token in sys.argv:
        if token.startswith('--') and '=' in token:
            key, value = token[2:].split('=', 1)
            args[key] = value.strip('"\'')
    return args


def _find_by_class_name(objects, class_name):
    return [o for o in objects if o and o.get_class().get_name() == class_name]


def _extract_asset_paths(text):
    """All plausible asset paths in an export_text blob, both Class'/Path' and
    plain "/Path" quoting forms."""
    import re
    paths = re.findall(r"'(/[^']+)'", text) + re.findall(r'"(/[^"]+)"', text)
    seen = []
    for path in paths:
        if path.startswith('/Script/'):
            continue
        if path not in seen:
            seen.append(path)
    return seen


def _load_path(asset_path):
    try:
        return unreal.load_object(name=asset_path, outer=None)
    except Exception:
        return None


def _resolve_asset_reference(reference):
    """Resolve FEditorOnlyAssetReference / soft pointers / plain objects to a
    loaded asset."""
    if reference is None:
        return None
    if isinstance(reference, unreal.Object):
        return reference
    if isinstance(reference, unreal.SoftObjectPath):
        return _load_path(str(reference.export_text()).strip("'\""))
    # FEditorOnlyAssetReference: Asset (TSoftObjectPtr, EditAnywhere) is script-readable
    for property_name in ('asset', 'asset_identifier'):
        try:
            inner = reference.get_editor_property(property_name)
        except Exception:
            continue
        if isinstance(inner, unreal.Object):
            return inner
        if inner is not None:
            resolved = _load_path(str(inner).strip("'\""))
            if resolved:
                return resolved
    try:
        text = reference.export_text()
    except Exception:
        return None
    for asset_path in _extract_asset_paths(text):
        resolved = _load_path(asset_path)
        if resolved:
            return resolved
    return None


def _resolve_principal_asset(wardrobe_item):
    """Resolve a UMetaHumanWardrobeItem to its principal asset (expected UGroomAsset)."""
    try:
        principal = wardrobe_item.get_editor_property('principal_asset')
    except Exception:
        return None
    return _resolve_asset_reference(principal)


def _get_prop(obj, *names):
    for name in names:
        try:
            value = obj.get_editor_property(name)
            if value is not None:
                return value
        except Exception:
            continue
    return None


def _resolve_groom_from_asset(asset):
    """Returns (groom_asset, transfer_source_mesh) or None.

    The MetaHuman wardrobe grooms ship as pre-authored GroomBindingAssets against
    the archetype face (/MetaHumanCharacter/Optional/Grooms/Bindings/...), so
    unwrapping one gives us both the groom and the exact mesh to use as the
    binding transfer source."""
    if asset is None:
        return None
    class_name = asset.get_class().get_name()
    if class_name == 'GroomAsset':
        return (asset, None)
    if class_name == 'GroomBindingAsset':
        groom = _get_prop(asset, 'groom')
        if groom is not None and groom.get_class().get_name() == 'GroomAsset':
            source = _get_prop(asset, 'source_skeletal_mesh')
            if source is None:
                source = _get_prop(asset, 'target_skeletal_mesh')
            return (groom, source)
        _warn('binding %s did not expose its groom' % asset.get_name())
        return None
    if class_name == 'MetaHumanWardrobeItem':
        return _resolve_groom_from_asset(_resolve_principal_asset(asset))
    return None


def _gather_groom_selections(character):
    """Return list of (slot_name, groom_asset) for every wardrobe selection whose
    principal asset is a GroomAsset."""
    collection = None
    try:
        collection = character.get_internal_collection()
    except Exception:
        try:
            collection = character.get_editor_property('internal_collection')
        except Exception:
            pass
    if collection is None:
        _warn('could not access the character internal collection')
        return []

    instance = None
    # DefaultInstance is a BlueprintReadOnly UPROPERTY; the Get*DefaultInstance()
    # accessors are native-only
    try:
        instance = collection.get_editor_property('default_instance')
    except Exception:
        pass
    if instance is None:
        for accessor in ('get_mutable_default_instance', 'get_default_instance'):
            try:
                instance = getattr(collection, accessor)()
                break
            except Exception:
                continue
    if instance is None:
        _warn('could not access the collection default instance')
        return []

    try:
        selections = instance.get_slot_selection_data()
    except Exception as error:
        _warn('could not read slot selections: ' + str(error))
        return []

    # The selection key (FMetaHumanPaletteItemKey) embeds the principal asset or
    # external wardrobe item reference, so parse it directly - the collection's
    # Items array is not script-accessible (plain protected UPROPERTY).
    grooms = []
    for selection_data in selections:
        try:
            selection = selection_data.get_editor_property('selection')
            slot_name = str(selection.get_editor_property('slot_name'))
            selected_key = selection.get_editor_property('selected_item')
        except Exception as error:
            _warn('unreadable slot selection entry: ' + str(error))
            continue

        key_text = None
        try:
            key_text = selected_key.export_text()
        except Exception:
            pass

        resolution = None
        resolved_names = []
        if key_text:
            for asset_path in _extract_asset_paths(key_text):
                asset = _load_path(asset_path)
                if asset is None:
                    continue
                resolved_names.append('%s (%s)' % (asset.get_name(), asset.get_class().get_name()))
                resolution = _resolve_groom_from_asset(asset)
                if resolution is not None:
                    break

        if resolution is None:
            if resolved_names:
                _log('slot %s: no groom in selection (%s) - skipping'
                     % (slot_name, ', '.join(resolved_names)))
            else:
                _log('slot %s: could not resolve selection key - skipping; raw key: %s'
                     % (slot_name, key_text if key_text else '<unreadable>'))
            continue
        groom_asset, transfer_source = resolution
        grooms.append((slot_name, groom_asset, transfer_source))

    return grooms


def _find_archetype_face_mesh(override_path):
    """The skeletal mesh the MetaHuman grooms were authored against, used as the
    binding transfer source."""
    if override_path:
        mesh = unreal.load_object(name=override_path, outer=None)
        if mesh:
            return mesh
        _warn('could not load --sourceFace override ' + override_path)

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    search_roots = ['/MetaHumanCharacter', '/MetaHumanCharacterAssets', '/Game/MetaHumans']
    candidates = []
    for root in search_roots:
        ar_filter = unreal.ARFilter(
            class_paths=[unreal.TopLevelAssetPath('/Script/Engine', 'SkeletalMesh')],
            package_paths=[root],
            recursive_paths=True)
        try:
            candidates.extend(registry.get_assets(ar_filter) or [])
        except Exception:
            continue

    best = None
    for asset in candidates:
        name = str(asset.asset_name).lower()
        if 'archetype' in name and 'face' in name:
            best = asset
            break
        if best is None and 'archetype' in name:
            best = asset
    if best is not None:
        mesh = unreal.load_object(name=str(best.package_name) + '.' + str(best.asset_name), outer=None)
        if mesh:
            _log('using archetype face mesh ' + mesh.get_path_name())
            return mesh
    _warn('no archetype face mesh found - bindings will be built without a transfer '
          'source, which is only correct if the groom rest pose matches the head')
    return None


def _create_binding(groom_asset, head_mesh, source_mesh, bindings_path):
    binding_name = '%s_Binding' % groom_asset.get_name()
    binding_asset_path = bindings_path + '/' + binding_name
    # Delete-before-create: replacing in place has the same staleness hazards the
    # bake textures hit
    if unreal.EditorAssetLibrary.does_asset_exist(binding_asset_path):
        unreal.EditorAssetLibrary.delete_asset(binding_asset_path)
    # UGroomBlueprintLibrary has ScriptName "GroomLibrary"
    binding = unreal.GroomLibrary.create_new_groom_binding_asset_with_path(
        binding_asset_path, groom_asset, head_mesh, 100, source_mesh, 0)
    if binding is None:
        _warn('binding creation failed for ' + groom_asset.get_name())
        return None
    unreal.EditorAssetLibrary.save_asset(binding.get_path_name().split('.')[0], only_if_is_dirty=False)
    return binding


def _apply_follicle_mask(grooms, bindings_path, asset_name, head_mesh):
    """Build the follicle mask from the follicle-mapped grooms and set it on the
    head materials. Non-fatal: bald spots are cosmetic, the grooms still work."""
    mask_grooms = []
    mask_channels = []
    for slot_name, groom_asset, _ in grooms:
        channel = FOLLICLE_CHANNELS.get(slot_name.lower())
        if channel is None:
            continue
        mask_grooms.append(groom_asset)
        mask_channels.append(channel)
    if not mask_grooms:
        _log('no follicle-mapped grooms (hair/eyebrows/beard) - skipping follicle mask')
        return

    mask_path = bindings_path + '/T_' + asset_name + '_FollicleMask'
    try:
        mask_texture = unreal.DazToUnrealBlueprintUtils.create_groom_follicle_mask(
            mask_grooms, mask_channels, mask_path,
            FOLLICLE_MASK_RESOLUTION, FOLLICLE_MASK_ROOT_RADIUS)
    except Exception as error:
        _warn('follicle mask build failed: ' + str(error))
        return
    if mask_texture is None:
        _warn('follicle mask build returned nothing (see LogDazToUnrealBlueprintUtils)')
        return

    # Assign to every head skin LOD material (slot names head, head_LOD1, ...).
    # Eye/teeth/lash slots don't take the follicle mask, matching official assembly.
    # The param only affects the shader's synthesized-texture branch; the likeness
    # bake's texture-override branch bypasses it, so ALSO bake the darkening into the
    # override basecolor and repoint the MICs at the derived texture.
    assigned = 0
    baked = 0
    derived_by_source = {}
    for material in head_mesh.get_editor_property('materials'):
        slot_name = str(material.get_editor_property('material_slot_name'))
        if not slot_name.lower().startswith('head'):
            continue
        mic = material.get_editor_property('material_interface')
        if mic is None or not isinstance(mic, unreal.MaterialInstanceConstant):
            continue
        # SetMaterialInstance*ParameterValue always returns false in 5.8 (dead bResult
        # in MaterialEditingLibrary.cpp) - the old "has no parameter" warning here was
        # bogus; the mask has been assigned all along
        unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
            mic, FOLLICLE_MASK_PARAM, mask_texture)
        assigned += 1

        derived = _bake_follicle_into_override(mic, slot_name, mask_texture, derived_by_source)
        if derived is not None:
            unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
                mic, 'Basecolor', derived)
            baked += 1
        unreal.EditorAssetLibrary.save_asset(mic.get_path_name().split('.')[0], only_if_is_dirty=True)
    for derived in derived_by_source.values():
        if derived is not None:
            unreal.EditorAssetLibrary.save_asset(derived.get_path_name().split('.')[0], only_if_is_dirty=True)
    unreal.EditorAssetLibrary.save_asset(mask_path, only_if_is_dirty=True)
    _log('follicle mask %s assigned to %d head material slot(s), baked into the override '
         'basecolor on %d' % (mask_texture.get_name(), assigned, baked))


def _bake_follicle_into_override(mic, slot_name, mask_texture, derived_by_source):
    """If this MIC uses the likeness-bake texture override, fold the follicle darkening
    into its basecolor. Returns the derived texture to assign, or None. Re-runs are
    idempotent: the bake always reads the pristine *_Baked texture (recovered by name
    if the MIC already points at a *_FollicleBaked derivative)."""
    try:
        override_on = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(
            mic, 'Use Texture Override')
    except Exception:
        override_on = 0.0
    if not override_on:
        return None
    source = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(mic, 'Basecolor')
    if source is None:
        return None
    source_path = source.get_path_name().split('.')[0]
    if not source_path.startswith('/Game/'):
        return None  # engine placeholder - no likeness bake on this slot
    if source_path.endswith(FOLLICLE_BAKE_SUFFIX):
        source_path = source_path[:-len(FOLLICLE_BAKE_SUFFIX)]
        source = unreal.load_object(name=source_path, outer=None)
        if source is None:
            _warn('head slot %s: original baked texture missing at %s - cannot re-bake follicle'
                  % (slot_name, source_path))
            return None
    if source_path in derived_by_source:
        return derived_by_source[source_path]

    derived_path = source_path + FOLLICLE_BAKE_SUFFIX
    try:
        derived = unreal.DazToUnrealBlueprintUtils.bake_follicle_mask_into_texture(
            source, mask_texture, derived_path,
            unreal.LinearColor(*FOLLICLE_BAKE_HAIR_MULTIPLY),
            unreal.LinearColor(*FOLLICLE_BAKE_EYEBROWS_MULTIPLY),
            unreal.LinearColor(*FOLLICLE_BAKE_BEARD_MULTIPLY),
            FOLLICLE_BAKE_STRENGTH, FOLLICLE_BAKE_BROAD_GAIN)
    except Exception as error:
        _warn('follicle bake into %s failed: %s' % (source.get_name(), error))
        derived = None
    if derived is None:
        _warn('follicle basecolor bake failed for %s (see LogDazToUnrealBlueprintUtils)'
              % source.get_name())
    derived_by_source[source_path] = derived
    return derived


def _gather_instance_color_entries(character):
    """All per-item authored parameter bags (hair color etc.) from the character's
    default instance, via the C++ JSON dump (the property bag itself is not
    python-accessible)."""
    try:
        raw = unreal.DazToUnrealBlueprintUtils.get_meta_human_instance_parameters_json(character)
        entries = json.loads(raw).get('entries', [])
    except Exception as error:
        _warn('could not read instance color parameters: ' + str(error))
        return []
    if entries:
        _log('found %d authored parameter bag(s): %s'
             % (len(entries), ', '.join(str(e.get('item', '?')) for e in entries)))
    return entries


def _match_color_params(entries, slot_name, groom_asset):
    """Best-effort match of a parameter bag to a groom slot - the item path debug
    text embeds the wardrobe item / groom asset naming. Most specific key first."""
    groom_name = groom_asset.get_name().lower()
    for key in (groom_name, 'wi_' + groom_name, slot_name.lower()):
        for entry in entries:
            if key in str(entry.get('item', '')).lower():
                params = entry.get('params', {})
                if params:
                    return params
    return {}


def _eyelash_color_params(character):
    """Strand-groom eyelashes have no wardrobe parameter bag; their authored color
    lives in HeadModelSettings.Eyelashes. Salt-and-pepper is the lash equivalent
    of white amount."""
    try:
        lashes = character.get_editor_property('head_model_settings').get_editor_property('eyelashes')
        dye = lashes.get_editor_property('dye_color')
        return {
            'Melanin': float(lashes.get_editor_property('melanin')),
            'Redness': float(lashes.get_editor_property('redness')),
            'Roughness': float(lashes.get_editor_property('roughness')),
            'Lightness': float(lashes.get_editor_property('lightness')),
            'Whiteness': float(lashes.get_editor_property('salt_and_pepper')),
            'DyeColor': [dye.r, dye.g, dye.b, dye.a],
        }
    except Exception as error:
        _warn('could not read eyelash color properties: ' + str(error))
        return {}


def _create_groom_color_materials(grooms_path, slot_name, groom_asset, params):
    """Create colored MIC children of the groom's own materials in <pkg>/Grooms and
    return them as a component override list aligned with the groom's material
    slots. Returns None if nothing applied (component then keeps the stock
    materials)."""
    try:
        material_entries = list(groom_asset.get_editor_property('hair_groups_materials'))
    except Exception as error:
        _warn('slot %s: could not read groom materials: %s' % (slot_name, error))
        return None
    overrides = []
    summary = []
    any_applied = False
    for entry in material_entries:
        base = entry.get_editor_property('material')
        mat_slot = str(entry.get_editor_property('slot_name'))
        if base is None:
            overrides.append(None)
            continue
        short = mat_slot[3:] if mat_slot.startswith('MI_') else mat_slot
        mic_path = '%s/MI_%s_%s' % (grooms_path, slot_name, short)
        # Delete-before-create, like the bindings and bake textures
        if unreal.EditorAssetLibrary.does_asset_exist(mic_path):
            unreal.EditorAssetLibrary.delete_asset(mic_path)
        mic = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            mic_path.rsplit('/', 1)[1], grooms_path, unreal.MaterialInstanceConstant,
            unreal.MaterialInstanceConstantFactoryNew())
        if mic is None:
            _warn('could not create ' + mic_path)
            overrides.append(None)
            continue
        unreal.MaterialEditingLibrary.set_material_instance_parent(mic, base)
        # SetMaterialInstance*ParameterValue ALWAYS returns false in 5.8 (bResult is
        # never assigned in MaterialEditingLibrary.cpp) - set blind and count the
        # mapped params; unknown names are harmless orphan overrides on the MIC
        applied = 0
        for bag_name, value in params.items():
            param = GROOM_COLOR_SCALARS.get(bag_name)
            if param is not None and isinstance(value, (int, float)) and not isinstance(value, bool):
                unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                    mic, param, float(value))
                applied += 1
                continue
            param = GROOM_COLOR_BOOLS.get(bag_name)
            if param is not None:
                unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                    mic, param, 1.0 if value else 0.0)
                applied += 1
                continue
            param = GROOM_COLOR_VECTORS.get(bag_name)
            if param is not None and isinstance(value, (list, tuple)) and len(value) >= 3:
                color = unreal.LinearColor(float(value[0]), float(value[1]), float(value[2]),
                                           float(value[3]) if len(value) > 3 else 1.0)
                unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
                    mic, param, color)
                applied += 1
        unreal.EditorAssetLibrary.save_asset(mic_path, only_if_is_dirty=False)
        overrides.append(mic)
        summary.append('%s:%d' % (short, applied))
        any_applied = any_applied or applied > 0
    if not any_applied:
        return None
    _log('slot %s: hair color materials created (%s params per slot)'
         % (slot_name, ', '.join(summary)))
    return overrides


class _SubobjectHelper(object):
    """Small wrapper around SubobjectDataSubsystem quirks."""

    def __init__(self, blueprint):
        self.blueprint = blueprint
        self.subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)

    def gather(self):
        return self.subsystem.k2_gather_subobject_data_for_blueprint(self.blueprint)

    def data_for(self, handle):
        for accessor in ('k2_find_subobject_data_from_handle', 'find_subobject_data_from_handle'):
            method = getattr(self.subsystem, accessor, None)
            if method is not None:
                try:
                    return method(handle)
                except Exception:
                    continue
        return None

    def object_for(self, handle):
        data = self.data_for(handle)
        if data is None:
            return None
        try:
            return unreal.SubobjectDataBlueprintFunctionLibrary.get_object(data)
        except Exception:
            return None

    def find_component_handle(self, name_fragment):
        for handle in self.gather():
            obj = self.object_for(handle)
            if obj is not None and name_fragment.lower() in obj.get_name().lower():
                return handle, obj
        return None, None

    def remove_components(self, name_prefix):
        removed = 0
        # Re-gather after each delete; handles go stale
        while True:
            target = None
            for handle in self.gather():
                obj = self.object_for(handle)
                if obj is not None and obj.get_name().startswith(name_prefix):
                    target = handle
                    break
            if target is None:
                break
            try:
                self.subsystem.delete_subobject(self.gather()[0], target, self.blueprint)
                removed += 1
            except Exception as error:
                _warn('could not remove existing %s component: %s' % (name_prefix, error))
                break
        return removed

    def add_groom_component(self, parent_handle, component_name, groom_asset, binding_asset,
                            override_materials=None):
        params = unreal.AddNewSubobjectParams(
            parent_handle=parent_handle,
            new_class=unreal.GroomComponent,
            blueprint_context=self.blueprint)
        new_handle, fail_reason = self.subsystem.add_new_subobject(params)
        # SubobjectDataHandle has no is_valid() in python; validity check lives on
        # the function library. Fall back to "did we get an object" below.
        handle_valid = True
        for check in ('is_handle_valid', 'is_valid'):
            checker = getattr(unreal.SubobjectDataBlueprintFunctionLibrary, check, None)
            if checker is not None:
                try:
                    handle_valid = bool(checker(new_handle))
                    break
                except Exception:
                    continue
        if not handle_valid:
            _warn('failed to add groom component %s: %s' % (component_name, fail_reason))
            return False
        self.subsystem.rename_subobject(new_handle, unreal.Text(component_name))
        component = self.object_for(new_handle)
        if component is None:
            _warn('added %s but could not access its template to set properties' % component_name)
            return False
        component.set_editor_property('groom_asset', groom_asset)
        component.set_editor_property('binding_asset', binding_asset)
        if override_materials:
            try:
                component.set_editor_property('override_materials', override_materials)
            except Exception as error:
                _warn('%s: could not assign colored materials: %s' % (component_name, error))
        return True


def main():
    args = _parse_args()
    character_path = args.get('character')
    if not character_path:
        _warn('missing --character argument')
        return

    character = unreal.load_object(name=character_path, outer=None)
    if character is None or character.get_class().get_name() != 'MetaHumanCharacter':
        _warn('%s is not a MetaHumanCharacter' % character_path)
        return

    character_asset_name = character.get_name()            # <name>_MHC
    package_path = character_path.rsplit('/', 1)[0]        # /Game/DazToUnreal/<name>
    if not character_asset_name.endswith('_MHC'):
        _warn('expected a Daz-converted character (name ending _MHC), got ' + character_asset_name)
        return
    asset_name = character_asset_name[:-len('_MHC')]

    head_mesh_path = package_path + '/' + character_asset_name + '_Head'
    head_mesh = unreal.load_object(name=head_mesh_path, outer=None)
    if head_mesh is None:
        _warn('exported head mesh not found at %s - run the conversion first' % head_mesh_path)
        return

    blueprint_path = package_path + '/BP_' + asset_name + '_MH'
    blueprint = unreal.load_object(name=blueprint_path, outer=None)
    if blueprint is None:
        _warn('character blueprint not found at %s - run the conversion first' % blueprint_path)
        return

    # Card-eyelash guidance (they are part of the head export, not a groom attach)
    try:
        lashes = character.get_editor_property('head_model_settings').get_editor_property('eyelashes')
        if str(lashes.get_editor_property('type')) in ('EMetaHumanCharacterEyelashesType.NONE', 'None'):
            _log('note: eyelash type is None on this character; card lashes bake into the '
                 'head only if a type is set before the conversion runs')
    except Exception:
        pass

    grooms = _gather_groom_selections(character)
    if not grooms:
        _log('no groom selections found on %s - pick hair/eyebrows in the MetaHuman '
             'Character editor first' % character_asset_name)
        return
    _log('found %d groom selection(s): %s' % (len(grooms), ', '.join(slot for slot, _, _ in grooms)))

    # Only search for the archetype face if some groom didn't come with its own
    # authored-against mesh (unwrapped from the shipped binding asset)
    fallback_source_mesh = None
    if any(transfer_source is None for _, _, transfer_source in grooms):
        fallback_source_mesh = _find_archetype_face_mesh(args.get('sourceFace'))

    bindings_path = package_path + '/Grooms'

    helper = _SubobjectHelper(blueprint)
    face_handle, face_component = helper.find_component_handle('Face')
    if face_handle is None:
        _warn('no Face component found on ' + blueprint_path)
        return

    removed = helper.remove_components('Groom_')
    if removed:
        _log('removed %d groom component(s) from a previous run' % removed)

    color_entries = _gather_instance_color_entries(character)

    attached = 0
    for slot_name, groom_asset, transfer_source in grooms:
        source_mesh = transfer_source if transfer_source is not None else fallback_source_mesh
        _log('binding %s for slot %s (transfer source: %s)'
             % (groom_asset.get_name(), slot_name,
                source_mesh.get_name() if source_mesh else 'none'))
        binding = _create_binding(groom_asset, head_mesh, source_mesh, bindings_path)
        if binding is None:
            continue
        color_params = _match_color_params(color_entries, slot_name, groom_asset)
        if not color_params and slot_name.lower() == 'eyelashes':
            color_params = _eyelash_color_params(character)
        if color_params:
            override_materials = _create_groom_color_materials(
                bindings_path, slot_name, groom_asset, color_params)
        else:
            override_materials = None
            _log('slot %s: no authored hair color - groom keeps its default materials' % slot_name)
        component_name = 'Groom_' + slot_name
        if helper.add_groom_component(face_handle, component_name, groom_asset, binding,
                                      override_materials):
            _log('attached %s (%s -> %s)' % (component_name, groom_asset.get_name(), binding.get_name()))
            attached += 1

    # Scalp coverage: without the follicle mask the head material has a flat-black
    # placeholder in 'Hair Mask' and scalp skin shows through the strands
    _apply_follicle_mask(grooms, bindings_path, asset_name, head_mesh)

    if attached:
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        unreal.EditorAssetLibrary.save_asset(blueprint_path, only_if_is_dirty=True)
        unreal.EditorAssetLibrary.save_directory(bindings_path, only_if_is_dirty=True)
        _log('done - %d groom(s) attached to %s' % (attached, blueprint_path))
    else:
        _warn('no grooms were attached (see warnings above)')

    # Flush the python wrapper garbage created by struct traversal (see
    # editor-crash notes: stale wrappers + editor GC is a crash)
    gc.collect()


if __name__ == '__main__':
    main()
