"""DazToUnreal — Convert to MetaHuman: body conform driver (Phase 1 step 3).

Invoked from C++ (FDazToUnrealModule import dispatch) via:
    py ConvertToMetaHuman.py --skeletalMesh=<pkg> --dtuFile="<path>"

What it does (all LOCAL — no Epic cloud / sign-in, per gating answer Q1):
    1. Load the imported bare Daz body skeletal mesh (the conform TARGET).
    2. Create a fresh UMetaHumanCharacter asset next to it.
    3. try_add_object_to_edit.
    4. get_mesh_data_for_conforming on the Daz mesh -> verts + tri indices.
    5. Build FConformTargetParams for a COMBINED solve (pipeline "combined" —
       fits face AND body to the one Daz mesh with head attached; estimate
       joints from mesh, auto-solve) and run conform_to_target_meshes.
    6. Export the posed DNA (canonical local output consumed downstream) and
       commit_posed_state_as_a_pose so the saved asset is in the MetaHuman A pose.
    7. Export the head + body skeletal meshes via the engine's own
       MetaHumanCharacterExportBlueprintLibrary.export_geometry (creates
       <char>_Head and <char>_Body with persistent topology materials; needs the
       open edit session). Real MetaHumans are TWO-mesh characters (face + body,
       leader-posed) — the head is never merged into the body mesh.
    8. Optionally create the combined face+body mesh (<name>_MHCombined) via the
       create_meta_human_combined_mesh C++ helper. NOTE this is a single-section
       outfit-fitting / DCC-export UTILITY (Epic applies a clay material to it and
       stores body measurements as asset user data) — NOT a renderable character.
    9. Assemble a placeable character blueprint (BP_<name>_MH) from the exported
       head + body meshes via assemble_meta_human_character_blueprint (duplicates
       the MetaHuman actor template from plugin settings and assigns the meshes
       to its Face/Body components — the standard two-mesh leader-posed pattern).
    10. Apply MetaHuman skin materials via apply_meta_human_skin_materials:
        estimates the skin tone from the Daz diffuse texture, synthesizes
        MetaHuman skin textures LOCALLY (the synthesis model ships with the
        plugin; hi-res cloud synthesis stays opt-in), persists textures + MICs
        under <package>/Skin, and assigns them to the exported meshes.
    11. Point the persisted skin MICs at Daz likeness textures baked onto the
        MetaHuman UV layout (GeometryScript mesh-to-mesh MultiTexture bake in
        step 7.5, run against the final exported meshes). Basecolor only; both
        MICs get 'Use Texture Override' = 1 so the shader samples the baked
        albedo instead of parametric content (body: flat bias/gain tone;
        head: stock Color_CHEST chest bib + underwear straps).
    12. Body culling preview (poke-through fix, non-destructive): bake where
        opaque clothing covers the body (A-pose-aligned via leader-posed temp
        components), build a hide-mask texture from the coverage (C++
        create_hide_mask_from_gap_coverage), and set Epic's HideMask* material
        params on the body MICs — covered skin stops rendering, the mesh is
        untouched. Conversion source data (original mesh, mask, settings,
        transparent items) is stored as editor-only asset user data on _Body;
        the 'Bake MetaHuman Body Culling' blueprint context action later
        makes the cookable culled copy (RemoveAndShrinkGeometry), 'Restore
        Original MetaHuman Body' reverses it. Transparent garments (DTU alpha
        rules) never cull the skin beneath them.
    13. Save the character asset.
    14. Wearables (from the DTU's "Wearables" array, exporter 2023.1.53+):
        clothing/hair material sections are excluded from the conform target
        (fit the figure, not the clothed silhouette) and from the likeness
        bake source (no garment albedo on skin), and each clothing item is
        split into its own skeletal mesh asset under <package>/Clothing,
        re-skinned to the MetaHuman body skeleton and attached to the
        character blueprint as a leader-posed child of Body. Hair conversion
        is its own future feature.

The MetaHumanCharacter is conformed TO the Daz mesh (same direction as the
engine reference example_conform_from_custom_mesh.py). The result is a MetaHuman
face + body carrying the Daz figure's shape, on the MetaHuman skeletons with
stock RBF correctives intact (Q3).

Deliberately NOT done here (kept out of the offline core on purpose):
    * Facial contour landmark tracking (2D image tracking; the interactive tool
      auto-tracks via scene capture + local face tracker — tool-private). The
      combined solve runs without it; 3D key_point_targets are the headless
      refinement path if likeness needs it.
    * Full skeletal-mesh assembly via BuildMetaHuman — that runs the heavy
      Cinematic/Optimized assembly pipeline to an absolute build path and pulls
      in face/groom/cloud concerns. The body extraction instead duplicates the
      subsystem's live edit-session body mesh through our own C++ helper
      (MetaHuman APIs like CreateCombinedFaceAndBodyMesh are not UFUNCTIONs,
      so plain Python cannot reach them — but our linked C++ module can).

Requires Unreal Engine 5.8+ (MetaHumanCharacterEditor subsystem).
"""
import argparse
import gc
import math
import unreal


def _flush_python_wrappers(phase):
    """Python-GC crash mitigation: the conversion creates millions of transient
    unreal struct wrappers (a Vector per vertex access, weight entries,
    triangle points). The editor's pre-GC python collect
    (FPythonScriptPlugin::OnPreGarbageCollect -> PyUtil::CollectGarbage) has
    crashed repeatedly traversing that graph on a later tick — collect NOW,
    inside the script, while everything the wrappers reference is still
    alive, so the editor's own collect finds almost nothing of ours."""
    try:
        collected = gc.collect()
        unreal.log('ConvertToMetaHuman.py: python gc flush after %s (%d objects)' % (phase, collected))
    except Exception as gc_error:
        unreal.log_warning('ConvertToMetaHuman.py: python gc flush failed after %s: %s' % (phase, gc_error))


def _unwrap_conform_mesh_data(result):
    """UE Python wraps `bool Func(In, &OutVerts, &OutIndices)` so the bool
    success is consumed by the binding and the call returns (verts, indices).
    Trailing *_ is defensive against a future binding shape surfacing more."""
    body_vertices, body_indices, *_ = result
    return body_vertices, body_indices


# ---------------------------------------------------------------------------
# Likeness texture bake (materials stage (b)).
#
# Projects the Daz character's diffuse textures onto the MetaHuman UV layout
# with GeometryScript's mesh-to-mesh nearest-point bake (MultiTexture bake type
# samples the right Daz texture per source material section — Daz skin spans
# several surfaces, each with its own map). The bake TARGET combines two
# exports: the FINAL step-7 meshes provide the UV atlas (the pre-commit posed
# export's BODY atlas differs — verified on Victoria94, 2026-07-05 — so it
# cannot be the target itself), while the PRE-COMMIT posed export provides the
# vertex positions (transplanted; same DNA topology), keeping the bake surfaces
# ~mm from the Daz source. Baking from the A-posed positions instead pulls the
# eye/nostril/lip texels onto the Daz socket/mouth interiors and smears pinch
# regions — that path survives only as the topology-mismatch fallback.
# ---------------------------------------------------------------------------

BAKE_RESOLUTION_NAMES = ('RESOLUTION4096', 'RESOLUTION_4096')
BAKE_SAMPLES_NAMES = ('SAMPLE4', 'SAMPLE_4')
# Object-space distance (cm) to search for the Daz surface from each MetaHuman
# texel. POSE-ALIGNED bake (the normal path): the bake target carries the
# final mesh's UVs but the PRE-COMMIT posed export's vertex positions, so the
# two surfaces sit ~mm apart everywhere and a tight reach is correct — a loose
# one lets texels around the eye/nostril/lip openings grab the greyish socket
# and mouth interiors painted on the Daz head texture, and pinch regions
# (armpits, elbows, inner thighs) grab the wrong nearby surface. FALLBACK
# (posed/final topology mismatch): bake against the A-posed final mesh; the
# head barely moves (3cm suffices) but the A-posed fingers sit well beyond
# 3cm from the Daz hand pose, so the body needs 15cm and accepts the pinch
# artifacts.
# Aligned reach is per-mesh: the head conforms to ~mm, but the BODY's
# parametric fit quantizes soft tissue (waist/bust sits within ~1-2cm of the
# Daz surface in the matched pose — measured 2026-07-26). The body reach is
# tuned for the FACING-FILTERED baker (see below): facing rejection is what
# holds the close-edge line (adjacent fingers, nail plates at steep angles),
# so the reach only needs to cover honest alignment error — 2.5 left big miss
# regions (crotch, inner thighs, instep — 3-4cm off) that neighbour-filled
# into visible smears (2026-08-08); 4.0 recovers real texture there. History:
# 5.0 pre-facing grabbed toe nail-cream (2026-08-05), 2.5 baked black holes.
BAKE_PROJECTION_DISTANCE_ALIGNED_HEAD = 2.0
BAKE_PROJECTION_DISTANCE_ALIGNED_BODY = 4.0
BAKE_PROJECTION_DISTANCE_HEAD = 3.0
BAKE_PROJECTION_DISTANCE_BODY = 15.0
# Dilation ring (px) around each baked chart; the default 4px leaves black
# background visible at chart seams in lower mips (streaks along the shins).
BAKE_GUTTER_SIZE = 16
# BODY bakes route through the C++ facing-filtered baker when the compiled
# plugin provides it (BakeBasecolorFacingFiltered — see the attribution/design
# comment in DazToUnrealBlueprintUtils.cpp). The engine's GeometryScript bake
# accepts hits of ANY orientation, so finger-side texels sampled the
# neighbouring finger and toe texels the nail plates at any workable reach,
# and texels beyond the reach baked BLACK (2026-08-08, fresh-Victoria feet and
# the finger-silhouette chest patches). The C++ baker rejects hits whose
# source normal disagrees with the texel normal (dot < BAKE_MIN_FACING_DOT)
# and fills any remaining miss from its baked UV neighbours, so misses smear
# instead of going black. The HEAD stays on the engine baker: the pose-aligned
# head bake at 2.0 has been artifact-free and is left untouched.
# Facing dot 0.35 (~70 degrees): the filter's job is rejecting OPPOSITE-facing
# surfaces (neighbouring finger walls, dot ~ -1) and perpendicular plates
# (nails vs side texels, dot ~ 0); 0.5 also rejected honest correspondence on
# strongly curved mismatched regions (finger webbing) and widened the miss
# smears, and anything a looser cone wrongly admits is near-parallel skin the
# nearest-point pick already prefers.
BAKE_RESOLUTION = 4096
BAKE_MIN_FACING_DOT = 0.35
# Floor (cm) of the baker's ray-acceptance limit: per texel the along-normal
# ray beats the nearest facing-compatible point whenever its hit lands within
# max(3 * nearest-point distance, this floor). The ray correspondence is the
# correct one for plate-on-skin detail (nail plates sit mm ABOVE the fingertip
# skin — nearest-point-first sampled the skin beside the plate and erased the
# nails, 2026-08-09) and for laterally-drifted spans (mid-calf source sits
# 1-4cm off — nearest-point collapsed texels onto the same source ridge =
# dash-band smears); on snap-fitted fingers the own-skin nearest point is
# ~1-2mm, so the limit stays under the neighbouring finger's far wall (a
# finger thickness+ away). Keep the floor under ~1cm for the same reason;
# 0 disables the ray preference (old nearest-point-first order).
BAKE_PREFER_RAY_DISTANCE = 0.8

# Body hide mask (poke-through culling preview). Gap-aware coverage (C++
# BakeClothingGapCoverage): the body's UV charts are rasterized and each texel
# raycasts ALONG THE BODY NORMAL — starting COVERAGE_INWARD_OFFSET inside the
# skin, so garments the conform locally swallowed (bikini at the butt apex)
# still register — against the merged leader-posed garment mesh, recording the
# per-texel GAP DISTANCE (0..reach → 0..254, 255 = bare) and REJECTING GRAZING
# HITS (|dot(garment normal, body normal)| < COVERAGE_MIN_FACING_DOT): cloth
# passing edge-on near skin no longer claims it, which was the 10cm-era
# pelvis/thigh over-cull mechanism (inner-thigh normals vs the crotch panel).
# CreateHideMaskFromGapCoverage then culls a covered texel only when it sits
# deeper inside the covered region (chamfer px to the nearest bare texel) than
# a gap-scaled margin:
#   margin_px = HIDE_MASK_ERODE_PIXELS + HIDE_MASK_GAP_ERODE_PIXELS_PER_CM * gap_cm
# Skin hugged by cloth culls up to a slim tuck border (base 8px ~= 16mm at
# 1024 ~= 2mm/px; also fully spares strips under thin straps), while skin
# under a loose opening (armhole / leg openings — the see-through-hole case)
# keeps a live band that widens with how far the cloth floats, so sightlines
# into the opening land on skin, not holes. With grazing hits filtered and
# margins gap-scaled, a long reach is safe again — 8cm covers loose cuffs
# (3-8cm off the skin) without per-item tuning. Iterate all of these with
# RemaskBodyHider.py (no reconvert).
COVERAGE_RESOLUTION = 1024
COVERAGE_PROJECTION_DISTANCE = 8.0
COVERAGE_MIN_FACING_DOT = 0.3
COVERAGE_INWARD_OFFSET = 2.0
HIDE_MASK_ERODE_PIXELS = 8
HIDE_MASK_GAP_ERODE_PIXELS_PER_CM = 4.0
HIDE_MASK_BLUR_PIXELS = 4
HIDE_MASK_MAX_CULL_VALUE = 0.1
HIDE_MASK_MIN_KEEP_VALUE = 0.9
HIDE_MASK_MAX_SHRINK_DISTANCE = 0.5

# Footwear encloses the foot completely, but the outward coverage rays cannot
# see garment above skin that locally protrudes THROUGH the shell (toe tips
# and instep knuckles pierced the boot leather and rendered bare — XiuLin
# 2026-08-13), and the erode ring re-exposed texels right at the shell. When
# a coverage item is FOOTWEAR (low bounds AND real weight on the foot bones —
# a floor-length skirt has neither), a PHANTOM shell — the body's own foot
# region offset outward along its vertex normals — joins the coverage source,
# so every foot texel reads as covered at a small gap regardless of local
# poke-through. Sides are detected per item, so a single-boot outfit keeps
# its bare foot.
PHANTOM_FOOT_COVER_OFFSET = 1.5        # cm stand-off of the phantom shell
PHANTOM_FOOT_COVER_MIN_WEIGHT = 0.25   # foot-bone weight fraction counted as foot region
FOOTWEAR_BOUNDS_MIN_Z = 8.0            # item bounds min Z below this -> check its foot weights

# Garment surface-transport wrap (split -> re-pose -> WRAP -> create asset).
# After their re-pose the garments hug the RE-POSED DAZ SKIN by construction;
# the wrap moves each vertex onto the final committed body+head surface,
# preserving the authored garment-to-skin offset, BEFORE the asset is created
# — skin weights are laid down once by the proven creation path and never
# rewritten (an in-place rewrite corrupted animation weights). See
# _wrap_clothing_to_final_body for the rejected-correspondence rules.
WRAP_MAX_CORRESPONDENCE = 10.0
WRAP_MIN_NORMAL_DOT = 0.2
WRAP_SMOOTH_ITERATIONS = 2

# Daz surfaces that must not contribute to a skin bake: cards/shells that hover
# over the skin (lashes, brows, hair) and interior surfaces (mouth, eyes) whose
# textures would bleed onto lids/lips via nearest-point projection.
NON_SKIN_SLOT_KEYWORDS = (
    'eyelash', 'lash', 'eyebrow', 'brow', 'hair', 'fur',
    'eye', 'cornea', 'iris', 'pupil', 'sclera', 'tear', 'moisture',
    'mouth', 'teeth', 'tongue', 'gum', 'oral',
)


def _first_if_tuple(result):
    """Functions with ExpandEnumAsExecs/out-params come back as tuples with the
    return value first; plain returns come back bare."""
    return result[0] if isinstance(result, tuple) else result


def _enum_value(enum_type, names):
    """Python enum spellings for numbered C++ names vary (RESOLUTION4096 vs
    RESOLUTION_4096) — take the first that exists."""
    for name in names:
        value = getattr(enum_type, name, None)
        if value is not None:
            return value
    raise AttributeError('%s has none of %s' % (enum_type, names))


def _named_call(owner, name_variants):
    for name in name_variants:
        func = getattr(owner, name, None)
        if func is not None:
            return func
    raise AttributeError('%s has none of %s' % (owner, name_variants))


def _translate_mesh_uvs(dynamic_mesh, translation, selection):
    _named_call(unreal.GeometryScript_UVs, ('translate_mesh_u_vs', 'translate_mesh_uvs'))(
        dynamic_mesh, 0, translation, selection)


def _material_uv_tile(dynamic_mesh, material_id):
    """UDIM tile (floor U, floor V) of the first VALID triangle of this
    material, or None if the material has no valid triangles left. After
    upstream triangle deletions, GetTrianglesByMaterialID can lead with
    invalid triangle ids ('TriangleIDList has invalid triangles') — reading
    UVs from those returns zeroed vectors, which would silently misdetect the
    tile as (0,0) and skip normalization (renders as clamped-edge garbage), so
    scan past them."""
    tri_result = unreal.GeometryScript_Materials.get_triangles_by_material_id(dynamic_mesh, material_id)
    tri_list = tri_result[1] if isinstance(tri_result, tuple) else tri_result
    get_uvs = _named_call(unreal.GeometryScript_MeshQueries, ('get_triangle_u_vs', 'get_triangle_uvs'))
    for list_index in range(512):
        item_result = unreal.GeometryScript_List.get_index_list_item(tri_list, list_index)
        tri_id, in_range = (item_result[0], item_result[1]) if isinstance(item_result, tuple) else (item_result, True)
        if not in_range:
            return None
        uv_result = get_uvs(dynamic_mesh, 0, tri_id)
        uv1, uv2, uv3, have_uvs = uv_result[0], uv_result[1], uv_result[2], uv_result[3]
        if not have_uvs:
            continue  # invalid triangle — try the next list entry
        return (math.floor(min(uv1.x, uv2.x, uv3.x)), math.floor(min(uv1.y, uv2.y, uv3.y)))
    return None


def _lod_material_slots(skeletal_mesh, lod_index=0):
    """The material SLOT behind each dynamic-mesh material ID, in ID order.

    GeometryScript numbers a copied mesh's material ids by mesh SECTION, and a
    section index equals a material-slot index only while the LOD's
    LODMaterialMap is empty. A REIMPORT into an existing asset keeps the slot
    array intact but can leave sections unmerged (a Daz left/right surface pair
    is one slot but two sections), so section N stops meaning slot N and every
    later slot reads its neighbour's geometry. That is the reconvert corruption
    of 2026-08-03: garment albedo baked onto the body and garments built from
    scrambled vertex groups, while the same figure converted under a fresh name
    came out perfect. The asset itself is fine — it renders through the map —
    so only code that indexes sections by slot position breaks.

    GetLODMaterialListFromSkeletalMesh resolves the map for us (including its
    INDEX_NONE == identity convention); fall back to the raw slot array when it
    cannot be read, which is the old behaviour and correct for fresh imports."""
    slots = list(skeletal_mesh.materials)
    try:
        results = unreal.GeometryScript_AssetUtils.get_lod_material_list_from_skeletal_mesh(
            skeletal_mesh, unreal.GeometryScriptMeshReadLOD(lod_index=lod_index))
        # Out params are (MaterialList, MaterialIndex, MaterialSlotNames,
        # Outcome). Pick the slot-index array by shape rather than by position
        # so an engine signature change cannot silently mis-key every section.
        slot_indices = None
        for entry in (results if isinstance(results, tuple) else (results,)):
            try:
                values = list(entry)
            except TypeError:
                continue
            if values and all(isinstance(value, int) for value in values):
                slot_indices = values
                break
        if slot_indices:
            mapped = [slots[index] for index in slot_indices if 0 <= index < len(slots)]
            if len(mapped) == len(slot_indices):
                if len(mapped) != len(slots):
                    unreal.log_warning(
                        'ConvertToMetaHuman.py: %s has %d mesh section(s) over %d material slot(s) — '
                        'resolving section ids through the LOD material map (reimported asset)'
                        % (skeletal_mesh.get_name(), len(mapped), len(slots)))
                return mapped
            unreal.log_warning('ConvertToMetaHuman.py: LOD material map for %s references missing slots — '
                               'assuming section id == slot index' % skeletal_mesh.get_name())
    except Exception as map_error:
        unreal.log_warning('ConvertToMetaHuman.py: could not read the LOD material map for %s (%s) — '
                           'assuming section id == slot index' % (skeletal_mesh.get_name(), map_error))
    return slots


def _normalize_udim_tiles(dynamic_mesh, material_count):
    """Translate each material's UV island into the [0,1] tile. Daz G9 lays its
    surfaces out in UDIM tiles (head 1001, body 1002, legs 1003, arms 1004,
    nails 1005) but the bake's source sampler CLAMPS UVs to [0,1], so any
    surface left in a higher tile would sample its texture's edge pixels."""
    for material_id in range(material_count):
        tile = _material_uv_tile(dynamic_mesh, material_id)
        unreal.log('ConvertToMetaHuman.py: source slot[%d] uv tile=%s' % (material_id, tile))
        if tile is None or tile == (0, 0):
            continue
        selection_result = unreal.GeometryScript_MeshSelection.select_mesh_elements_by_material_id(
            dynamic_mesh, material_id)
        selection = selection_result[1] if isinstance(selection_result, tuple) else selection_result
        _translate_mesh_uvs(dynamic_mesh, unreal.Vector2D(-tile[0], -tile[1]), selection)
    return dynamic_mesh


def _copy_skeletal_mesh_to_dynamic_mesh(skeletal_mesh):
    """LOD0 of a skeletal mesh as a UDynamicMesh (UVs + material IDs included)."""
    dynamic_mesh = unreal.new_object(type=unreal.DynamicMesh)
    result = unreal.GeometryScript_AssetUtils.copy_mesh_from_skeletal_mesh(
        skeletal_mesh, dynamic_mesh,
        unreal.GeometryScriptCopyMeshFromAssetOptions(),
        unreal.GeometryScriptMeshReadLOD(lod_index=0))
    return _first_if_tuple(result)


def _prune_dynamic_mesh_to_slots(dynamic_mesh, skeletal_mesh, keep_slot):
    """Delete every material section whose slot name fails keep_slot(name).
    Material ids are SECTION indices, which coincide with material-slot indices
    only on a freshly imported asset — _lod_material_slots resolves them."""
    for material_id, material in enumerate(_lod_material_slots(skeletal_mesh)):
        if not keep_slot(str(material.material_slot_name)):
            unreal.GeometryScript_Materials.delete_triangles_by_material_id(
                dynamic_mesh, material_id)
    return dynamic_mesh


def _material_diffuse_texture(material_interface):
    """The Daz diffuse texture on an imported material instance (override params
    only — DazToUnreal sets the diffuse directly on each created instance)."""
    if not isinstance(material_interface, unreal.MaterialInstance):
        return None
    for texture_param in material_interface.get_editor_property('texture_parameter_values'):
        if 'diffuse' in str(texture_param.parameter_info.name).lower():
            texture = texture_param.parameter_value
            if isinstance(texture, unreal.Texture2D):
                return texture
    return None


def _is_daz_skin_slot(slot_name):
    lowered = slot_name.lower()
    return not any(keyword in lowered for keyword in NON_SKIN_SLOT_KEYWORDS)


def _close_asset_editors(path_prefix):
    """GC-crash guard: an open asset editor (skeletal-mesh preview especially)
    holding references to an asset we delete or rebuild crashes the editor in
    garbage collection (observed repeatedly). Close editors for every edited
    asset whose path starts with path_prefix before touching them."""
    try:
        subsystem = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
        if subsystem is None:
            return
        getter = getattr(subsystem, 'get_all_edited_assets', None)
        if not callable(getter):
            if unreal.EditorAssetLibrary.does_asset_exist(path_prefix):
                asset = unreal.EditorAssetLibrary.load_asset(path_prefix)
                if asset is not None:
                    subsystem.close_all_editors_for_asset(asset)
            return
        closed = 0
        for asset in getter():
            if asset is not None and asset.get_path_name().startswith(path_prefix):
                subsystem.close_all_editors_for_asset(asset)
                closed += 1
        if closed:
            unreal.log('ConvertToMetaHuman.py: closed %d open asset editor(s) under %s (GC-crash guard)'
                       % (closed, path_prefix))
    except Exception as close_error:
        unreal.log_warning('ConvertToMetaHuman.py: could not close asset editors under %s: %s'
                           % (path_prefix, close_error))


# ---------------------------------------------------------------------------
# Wearables (Spike B phase 1).
#
# The Daz exporter (2023.1.53+) writes a top-level "Wearables" array into the
# DTU: one entry per follower figure (clothing, hair, anatomy attachments,
# geografts) with its content type and follow target. The import merges all of
# them into ONE skeletal mesh as extra material sections, so wearables are only
# addressable through the material slots their surfaces own. Slot names are
# Sanitize(Asset Label) + '_' + Sanitize(Material Name); the DTU "Materials"
# array carries both parts per section, keyed to the owning node by
# "Asset Name". ("Combine Identical" merges duplicate materials, but the
# surviving slot always matches one of the node's own entries.)
#
# Classification: Follower/Wardrobe + Follower/Accessory = clothing (excluded
# from the conform target and the skin-bake source, split into standalone
# assets); Follower/Hair = hair (excluded, but conversion to grooms is its own
# future feature); geografts and everything else (Follower/Attachment eyes,
# mouth, lashes, brows...) = anatomy that stays with the body.
# ---------------------------------------------------------------------------

def _sanitize_daz_name(name):
    """Mirror of FDazToUnrealUtils::SanitizeName — how the importer builds
    asset and material-slot names from Daz labels.

    The '/' matters for safety, not cosmetics: these names are concatenated into
    asset paths, and a DTU-supplied label carrying a slash would otherwise steer
    a delete-then-create into a folder the caller never named. Kept as a
    denylist rather than an allowlist so existing projects keep the asset names
    they already have.
    """
    sanitized = str(name).replace(' ', '')
    for character in '().&!*<>?\\/:\'"|':
        sanitized = sanitized.replace(character, '_')
    return sanitized


# --- Destructive-operation confinement -------------------------------------
# Every asset this script deletes is one it generated, and all of them live
# under the converted mesh's own package folder. _PIPELINE_ROOT records that
# folder once it is known; _safe_delete_asset refuses anything outside it, so a
# malformed name or an unexpected argument cannot take user content with it.
_PIPELINE_ROOT = None


def _set_pipeline_root(root):
    """Record the one folder this convert is allowed to delete inside."""
    global _PIPELINE_ROOT
    _PIPELINE_ROOT = str(root).rstrip('/')


def _is_owned_path(asset_path):
    """True only for a path strictly BELOW the convert's own package folder.

    Requiring 'below' rather than 'at or below' means a collapsed or empty name
    can never resolve to the folder root and take the whole tree.
    """
    if not _PIPELINE_ROOT or not asset_path:
        return False
    path = str(asset_path).rstrip('/')
    if not path.startswith('/Game/') or '..' in path.split('/'):
        return False
    return path.startswith(_PIPELINE_ROOT + '/') and len(path) > len(_PIPELINE_ROOT) + 1


def _safe_delete_asset(asset_path, what='asset'):
    """delete_asset, confined to the convert's own folder. True if gone."""
    if not _is_owned_path(asset_path):
        unreal.log_warning('ConvertToMetaHuman.py: refusing to delete %s outside the convert folder: %s'
                           % (what, asset_path))
        return False
    if not unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        return True
    if unreal.EditorAssetLibrary.delete_asset(asset_path):
        return True
    unreal.log_warning('ConvertToMetaHuman.py: could not delete %s at %s' % (what, asset_path))
    return False


def _dtu_material_is_transparent(properties):
    """Mirror of the importer's Alpha-material rules (DazToUnrealMaterials.cpp):
    a cutout opacity texture, cutout opacity / opacity strength != 1, or a
    refraction weight != 0 made the imported surface an alpha material."""
    for material_property in properties:
        name = material_property.get('Name')
        if name == 'Cutout Opacity Texture':
            return True
        try:
            number = float(str(material_property.get('Value', '')).strip())
        except (TypeError, ValueError):
            continue
        if name in ('Cutout Opacity', 'Opacity Strength') and number != 1.0:
            return True
        if name == 'Refraction Weight' and number != 0.0:
            return True
    return False


def _load_dtu_wearables(dtu_file):
    """Parse the DTU's Wearables + Materials arrays.

    Returns (clothing_items, exclude_slots):
        clothing_items — [{'node', 'label', 'slots', 'transparent_slots'}] one
            per top-level clothing item; clothing that follows other clothing
            (strap add-ons on a jacket) is merged into its root item.
            transparent_slots ⊆ slots: surfaces the importer classified as
            alpha materials — see-through, so they must not cull the body
            geometry beneath them.
        exclude_slots — every material slot owned by clothing or hair, for
            pruning the conform target and the bake source.
    Both empty when the DTU predates the Wearables array (old exporter) or
    cannot be read — callers fall back to whole-mesh behavior."""
    clothing_items = []
    exclude_slots = set()
    # The C++ dispatch passes --dtuFile="..." and the py-command tokenizer keeps
    # the quote characters in the argument value — strip them or open() fails.
    dtu_file = (dtu_file or '').strip().strip('"\'')
    if not dtu_file:
        return clothing_items, exclude_slots
    try:
        import json
        with open(dtu_file, 'r', encoding='utf-8-sig', errors='replace') as dtu_handle:
            dtu = json.load(dtu_handle)
    except Exception as dtu_error:
        unreal.log_warning('ConvertToMetaHuman.py: could not parse DTU %s: %s' % (dtu_file, dtu_error))
        return clothing_items, exclude_slots

    node_slots = {}
    transparent_slots = set()
    for material in dtu.get('Materials') or []:
        node_name = material.get('Asset Name')
        material_name = material.get('Material Name')
        label = material.get('Asset Label') or node_name
        if not node_name or not material_name:
            continue
        slot = _sanitize_daz_name(label) + '_' + _sanitize_daz_name(material_name)
        node_slots.setdefault(node_name, set()).add(slot)
        if _dtu_material_is_transparent(material.get('Properties') or []):
            transparent_slots.add(slot)

    entries = {}
    for wearable in dtu.get('Wearables') or []:
        node_name = wearable.get('Asset Name')
        if not node_name or wearable.get('Is Geograft'):
            continue  # geografts are grafted body skin, never clothing
        content_type = wearable.get('Content Type') or ''
        if content_type.startswith('Follower/Wardrobe') or content_type.startswith('Follower/Accessory'):
            kind = 'clothing'
        elif content_type.startswith('Follower/Hair'):
            kind = 'hair'
        else:
            continue  # anatomy attachments stay with the body
        entries[node_name] = {
            'node': node_name,
            'label': wearable.get('Asset Label') or node_name,
            'follows': wearable.get('Follows'),
            'kind': kind,
            'slots': set(node_slots.get(node_name) or ()),
        }

    for entry in entries.values():
        exclude_slots |= entry['slots']

    def _clothing_root(entry):
        seen = set()
        while True:
            parent = entries.get(entry['follows'])
            if parent is None or parent['kind'] != 'clothing' or parent['node'] in seen:
                return entry
            seen.add(parent['node'])
            entry = parent

    roots = {}
    for entry in entries.values():
        if entry['kind'] != 'clothing':
            continue
        root = _clothing_root(entry)
        item = roots.setdefault(root['node'], {'node': root['node'], 'label': root['label'],
                                               'slots': set(), 'transparent_slots': set()})
        item['slots'] |= entry['slots']
        item['transparent_slots'] |= entry['slots'] & transparent_slots
    clothing_items = list(roots.values())
    return clothing_items, exclude_slots


def _read_all_vertex_positions(dynamic_mesh):
    """GeometryScriptVectorList of every vertex position (indexed by vertex id),
    or None. skip_gaps is a required positional on 5.8."""
    get_positions = _named_call(unreal.GeometryScript_MeshQueries, ('get_all_vertex_positions',))
    try:
        result = get_positions(dynamic_mesh, False)
    except TypeError:
        result = get_positions(dynamic_mesh)
    if isinstance(result, tuple):
        for item in result:
            if isinstance(item, unreal.GeometryScriptVectorList):
                return item
        return None
    return result if isinstance(result, unreal.GeometryScriptVectorList) else None


def _read_all_triangles(dynamic_mesh):
    """Array of FIntVector triangle corner ids, or None."""
    get_triangles = _named_call(unreal.GeometryScript_MeshQueries,
                                ('get_all_triangle_indices', 'get_all_triangle_indexes'))
    try:
        result = get_triangles(dynamic_mesh, False)
    except TypeError:
        result = get_triangles(dynamic_mesh)
    triangle_list = None
    if isinstance(result, tuple):
        for item in result:
            if isinstance(item, unreal.GeometryScriptTriangleList):
                triangle_list = item
                break
    elif isinstance(result, unreal.GeometryScriptTriangleList):
        triangle_list = result
    if triangle_list is None:
        return None
    to_array = _named_call(unreal.GeometryScript_List, ('convert_triangle_list_to_array',))
    return to_array(triangle_list)


def _conform_geometry_excluding_slots(skeletal_mesh, exclude_slots, reference_vertices):
    """Conform-ready (vertices, flat_indices) of the mesh with the wearable
    slots removed, so the MetaHuman body is fitted to the FIGURE, not to the
    clothed silhouette (jacket shoulders, boot soles...).

    get_mesh_data_for_conforming cannot skip sections, so this reads the same
    geometry through GeometryScript, prunes, and compacts. reference_vertices
    (the subsystem's own full-mesh read) guards the assumption that both
    readers agree on vertex order and space — on any mismatch returns None and
    the caller conforms to the full mesh as before."""
    try:
        dynamic_mesh = _copy_skeletal_mesh_to_dynamic_mesh(skeletal_mesh)
        to_vectors = _named_call(unreal.GeometryScript_List, ('convert_vector_list_to_array',))
        position_list = _read_all_vertex_positions(dynamic_mesh)
        if position_list is None:
            raise RuntimeError('could not read vertex positions')
        full_positions = to_vectors(position_list)
        if len(full_positions) != len(reference_vertices):
            raise RuntimeError('GeometryScript copy has %d verts, the conform reader %d — not comparable'
                               % (len(full_positions), len(reference_vertices)))
        for probe_index in (0, len(full_positions) // 2, len(full_positions) - 1):
            probe = full_positions[probe_index]
            reference = reference_vertices[probe_index]
            if max(abs(probe.x - reference.x), abs(probe.y - reference.y), abs(probe.z - reference.z)) > 0.01:
                raise RuntimeError('vertex %d differs between the readers' % probe_index)
        _prune_dynamic_mesh_to_slots(dynamic_mesh, skeletal_mesh,
                                     lambda slot: slot not in exclude_slots)
        compact = _named_call_any(('GeometryScript_MeshRepair', 'GeometryScript_MeshRepairFunctions'),
                                  ('compact_mesh',))
        _first_if_tuple(compact(dynamic_mesh))
        position_list = _read_all_vertex_positions(dynamic_mesh)
        triangles = _read_all_triangles(dynamic_mesh)
        if position_list is None or triangles is None:
            raise RuntimeError('could not read the pruned geometry')
        # BodyVertices is a Vector3f (float) array; GeometryScript hands back
        # double-precision Vectors, which the binding refuses to nativize.
        vertices = [unreal.Vector3f(position.x, position.y, position.z)
                    for position in to_vectors(position_list)]
        indices = []
        for triangle in triangles:
            indices.extend((triangle.x, triangle.y, triangle.z))
        if not vertices or not indices:
            raise RuntimeError('pruned conform mesh is empty')
        return vertices, indices
    except Exception as prune_error:
        unreal.log_warning('ConvertToMetaHuman.py: wearable-pruned conform geometry failed '
                           '(conforming to the full mesh instead): ' + str(prune_error))
        return None


# Daz (as named by the DazToUnreal import) -> MetaHuman body skeleton.
# Follows the shipped ConvertToEpicSkeleton rename tables
# (DazToUnrealBlueprintUtils.cpp G8 + G9 sections) exactly where they have an
# entry — notably hip->spine_01 with the G9 spine chain shifted up one, and
# the G9 forearm twists SWAPPED (twist1->lowerarm_twist_02, engine comment
# 'The Lower Arm twists are swapped') — extended with bones those tables skip
# (metatarsals, hand anchors, pectorals, individual toes). Left-side and
# centerline bones only; right-side entries are generated by mirroring
# l_/lCamel to r_/rCamel. Bones without an entry fall back to their nearest
# mapped ancestor (face bones -> head, etc.). Deliberately, nothing maps to
# MetaHuman's RBF corrective/helper joints (wrist_inner, *_mcp, *_bulge, ...)
# — their conformed-pose transforms proved unreliable (10-17cm off), and
# vendor weights never reference them anyway.
DAZ_TO_METAHUMAN_BONE = {
    'root': 'root',
    'hip': 'spine_01', 'pelvis': 'pelvis',
    # Genesis 9
    'spine1': 'spine_02', 'spine2': 'spine_03', 'spine3': 'spine_04', 'spine4': 'spine_05',
    'neck1': 'neck_01', 'neck2': 'neck_02', 'head': 'head',
    'l_pectoral': 'spine_05',
    'l_shoulder': 'clavicle_l', 'l_upperarm': 'upperarm_l', 'l_forearm': 'lowerarm_l',
    'l_hand': 'hand_l', 'l_hand_anchor': 'hand_l',
    'l_upperarmtwist1': 'upperarm_twist_01_l', 'l_upperarmtwist2': 'upperarm_twist_02_l',
    'l_forearmtwist1': 'lowerarm_twist_02_l', 'l_forearmtwist2': 'lowerarm_twist_01_l',
    'l_thumb1': 'thumb_01_l', 'l_thumb2': 'thumb_02_l', 'l_thumb3': 'thumb_03_l',
    'l_indexmetacarpal': 'index_metacarpal_l',
    'l_index1': 'index_01_l', 'l_index2': 'index_02_l', 'l_index3': 'index_03_l',
    'l_midmetacarpal': 'middle_metacarpal_l',
    'l_mid1': 'middle_01_l', 'l_mid2': 'middle_02_l', 'l_mid3': 'middle_03_l',
    'l_ringmetacarpal': 'ring_metacarpal_l',
    'l_ring1': 'ring_01_l', 'l_ring2': 'ring_02_l', 'l_ring3': 'ring_03_l',
    'l_pinkymetacarpal': 'pinky_metacarpal_l',
    'l_pinky1': 'pinky_01_l', 'l_pinky2': 'pinky_02_l', 'l_pinky3': 'pinky_03_l',
    'l_thigh': 'thigh_l', 'l_shin': 'calf_l', 'l_foot': 'foot_l',
    'l_metatarsal': 'ball_l', 'l_toes': 'ball_l',
    'l_thightwist1': 'thigh_twist_01_l', 'l_thightwist2': 'thigh_twist_02_l',
    'l_bigtoe1': 'bigtoe_01_l', 'l_bigtoe2': 'bigtoe_02_l',
    'l_indextoe1': 'indextoe_01_l', 'l_indextoe2': 'indextoe_02_l',
    'l_midtoe1': 'middletoe_01_l', 'l_midtoe2': 'middletoe_02_l',
    'l_ringtoe1': 'ringtoe_01_l', 'l_ringtoe2': 'ringtoe_02_l',
    'l_pinkytoe1': 'littletoe_01_l', 'l_pinkytoe2': 'littletoe_02_l',
    # Genesis 8 / Genesis 3
    'abdomenLower': 'spine_02', 'abdomenUpper': 'spine_03',
    'chestLower': 'spine_04', 'chestUpper': 'spine_05',
    'neckLower': 'neck_01', 'neckUpper': 'neck_02',
    'lPectoral': 'spine_05',
    'lCollar': 'clavicle_l', 'lShldrBend': 'upperarm_l', 'lShldrTwist': 'upperarm_twist_01_l',
    'lForearmBend': 'lowerarm_l', 'lForearmTwist': 'lowerarm_twist_02_l',
    'lHand': 'hand_l',
    'lThumb1': 'thumb_01_l', 'lThumb2': 'thumb_02_l', 'lThumb3': 'thumb_03_l',
    'lCarpal1': 'index_metacarpal_l',
    'lIndex1': 'index_01_l', 'lIndex2': 'index_02_l', 'lIndex3': 'index_03_l',
    'lCarpal2': 'middle_metacarpal_l',
    'lMid1': 'middle_01_l', 'lMid2': 'middle_02_l', 'lMid3': 'middle_03_l',
    'lCarpal3': 'ring_metacarpal_l',
    'lRing1': 'ring_01_l', 'lRing2': 'ring_02_l', 'lRing3': 'ring_03_l',
    'lCarpal4': 'pinky_metacarpal_l',
    'lPinky1': 'pinky_01_l', 'lPinky2': 'pinky_02_l', 'lPinky3': 'pinky_03_l',
    'lThighBend': 'thigh_l', 'lThighTwist': 'thigh_twist_01_l',
    'lShin': 'calf_l', 'lFoot': 'foot_l', 'lMetatarsals': 'ball_l', 'lToe': 'ball_l',
    'lHeel': 'foot_l',
}
for _daz_name, _mh_name in list(DAZ_TO_METAHUMAN_BONE.items()):
    if not _mh_name.endswith('_l'):
        continue
    if _daz_name.startswith('l_'):
        DAZ_TO_METAHUMAN_BONE['r_' + _daz_name[2:]] = _mh_name[:-2] + '_r'
    elif _daz_name.startswith('l') and len(_daz_name) > 1 and _daz_name[1].isupper():
        DAZ_TO_METAHUMAN_BONE['r' + _daz_name[1:]] = _mh_name[:-2] + '_r'

# Bones whose aim delta would measure JOINT-PLACEMENT CONVENTION differences
# rather than pose differences: the torso chain (MetaHuman spine segmentation
# and pelvis/hip placement diverge from Daz — Spike A measured hip width +26%,
# spine segments +/-12-15% — while both skeletons' A poses genuinely agree the
# torso is upright, so the correct delta there is ~identity) and the toes
# (ball-joint placement noise over ~2 cm levers; the foot aim carries them).
# Keyed on the MetaHuman target name so all Daz generations are covered.
# Symptom when these aimed: hip picked up a 46 deg delta that twisted the
# whole body and stepped one leg forward. Clavicles and thighs joined the
# set 2026-07-26 on DiagnoseTorsoOffset evidence — same convention class
# (clavicle length ~-77% vs Daz, hip width +26%), subtler symptom: their
# aim noise pitched the torso garments forward off the body (shirt fit the
# neck exactly but its chest back edge sat 0.8 cm inside the body with 1.2
# cm spare at the front; bikini back edge ~3 cm forward of authored, worse
# at the waist band than the pelvis — rotations hinged at the collar and
# thigh joints, while the body itself measured within ~1-2 cm of the Daz
# figure). The shin/foot still aim independently in component space, so
# leg pose handling below the thigh is unaffected.
NO_AIM_METAHUMAN_TARGETS = frozenset({
    'pelvis', 'spine_01', 'spine_02', 'spine_03', 'spine_04', 'spine_05',
    'neck_01', 'neck_02', 'head', 'ball_l', 'ball_r',
    'clavicle_l', 'clavicle_r', 'thigh_l', 'thigh_r',
})

# Position-snap anchors: bones whose re-posed position is REPLACED by the
# MetaHuman joint instead of the parent-carry. Only the forearm and hand
# qualify — the arm is the one long chain where carry drift accumulates
# visibly (the gloves floated a wrist-width off the hand bones, 2026-07-25)
# AND the elbow/wrist pivots are anatomically pinned the same way on both
# rigs, so the snap corrects drift rather than injecting convention
# differences. The snap-to-carry correction JUMP lands on whatever geometry
# blends a snapped bone with a carried neighbor, so the snap chain must
# start where that seam is bare skin: starting it at the UPPERARM put the
# jump on the clavicle/deltoid sleeve and smeared a bulge into the shirt's
# upper arm (2026-07-26); starting at the LOWERARM puts it at the elbow,
# between the sleeve hem and the glove cuff, where nothing renders it.
# Everything else carries, each for a measured reason (2026-07-25
# snap-everything run): fingers — see FINGER_TARGET_PREFIXES; clavicles —
# placement is convention (~-77% length vs Daz); thighs/legs — hip-region
# conventions differ (Spike A: hip width +26%), and the torso rides the
# thigh-socket base translation below instead.
SNAP_METAHUMAN_TARGETS = frozenset({
    'lowerarm_l', 'lowerarm_r', 'hand_l', 'hand_r',
})

# Finger targets get a third treatment: neither snapped (the re-estimated
# joint noise ~2.5 cm rivals their 2-3 cm segments — snapping squashed the
# glove fingers) nor parent-carried (carry reproduces the Daz hand's
# internal fan through aim deltas, and on a skin-tight glove those few
# degrees of direction error interpenetrate the body fingers — both
# observed 2026-07-25/26). Instead each finger joint is laid from its
# parent's re-posed position ALONG the MetaHuman joint-to-joint direction
# at the VENDOR bind length: the chain tracks the body finger exactly and
# the glove finger simply extends slightly past the tip, which is what a
# glove does. Per-segment noise cannot accumulate past a few mm over 3-4
# tiny segments below the exactly-snapped hand.
FINGER_TARGET_PREFIXES = ('thumb_', 'index_', 'middle_', 'ring_', 'pinky_')


def _aim_allowed(metahuman_target):
    return metahuman_target not in NO_AIM_METAHUMAN_TARGETS and 'toe' not in metahuman_target


def _all_bones_info(dynamic_mesh):
    """[FGeometryScriptBoneInfo] for the mesh's bone attributes."""
    result = unreal.GeometryScript_BoneWeights.get_all_bones_info(dynamic_mesh)
    if isinstance(result, tuple):
        for entry in result:
            if isinstance(entry, unreal.Array) or isinstance(entry, list):
                return list(entry)
        # some bindings return (mesh, array) where array is a wrapped struct list
        for entry in result:
            if entry is not None and not isinstance(entry, unreal.DynamicMesh):
                return list(entry)
    raise RuntimeError('could not read bone info from the dynamic mesh')


# Small pure-python vector/quaternion kit for the pose retarget — the unreal
# struct method bindings vary across versions, plain tuples don't.
def _vec_add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def _vec_sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def _vec_scale(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def _vec_dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def _vec_cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def _vec_length(a): return math.sqrt(_vec_dot(a, a))


def _vec_normalized(a):
    length = _vec_length(a)
    return _vec_scale(a, 1.0 / length) if length > 1e-12 else (0.0, 0.0, 0.0)


def _quat_mul(a, b):
    """Hamilton product: (a*b) rotates by b first, then a. (x, y, z, w)."""
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def _quat_rotate(q, v):
    qv = (q[0], q[1], q[2])
    t = _vec_scale(_vec_cross(qv, v), 2.0)
    return _vec_add(_vec_add(v, _vec_scale(t, q[3])), _vec_cross(qv, t))


def _quat_from_axis_angle(axis, angle):
    half = 0.5 * angle
    sine = math.sin(half)
    return (axis[0] * sine, axis[1] * sine, axis[2] * sine, math.cos(half))


def _quat_between(from_direction, to_direction):
    """Minimal rotation taking one unit direction onto another (no twist)."""
    d = _vec_dot(from_direction, to_direction)
    if d > 1.0 - 1e-9:
        return (0.0, 0.0, 0.0, 1.0)
    if d < -1.0 + 1e-9:
        axis = _vec_cross(from_direction, (1.0, 0.0, 0.0))
        if _vec_length(axis) < 1e-6:
            axis = _vec_cross(from_direction, (0.0, 1.0, 0.0))
        return _quat_from_axis_angle(_vec_normalized(axis), math.pi)
    axis = _vec_cross(from_direction, to_direction)
    w = 1.0 + d
    scale = 1.0 / math.sqrt(_vec_dot(axis, axis) + w * w)
    return (axis[0] * scale, axis[1] * scale, axis[2] * scale, w * scale)


def _signed_twist_angle(axis, from_direction, to_direction):
    """Signed angle about `axis` taking from_direction's perpendicular
    component onto to_direction's (0 when either is parallel to the axis)."""
    from_perpendicular = _vec_sub(from_direction, _vec_scale(axis, _vec_dot(from_direction, axis)))
    to_perpendicular = _vec_sub(to_direction, _vec_scale(axis, _vec_dot(to_direction, axis)))
    if _vec_length(from_perpendicular) < 1e-6 or _vec_length(to_perpendicular) < 1e-6:
        return 0.0
    return math.atan2(_vec_dot(_vec_cross(from_perpendicular, to_perpendicular), axis),
                      _vec_dot(from_perpendicular, to_perpendicular))


def _retarget_daz_pose_to_metahuman(daz_bones, metahuman_bones, debug=None, extras=None):
    """Retarget the MetaHuman A pose onto the DAZ skeleton in component
    space: rotations by direction alignment, positions by snapping mapped
    bones onto the MetaHuman joints. Every Daz bone with a (first-claimant)
    MetaHuman counterpart gets the minimal world-space rotation delta taking
    its bind child direction onto the corresponding MetaHuman bone direction,
    plus a twist about that aim axis when a second, sufficiently
    perpendicular mapped child pins the roll (palm plane from the finger
    spread, pelvis left/right from the thighs) or, failing that, when a
    hinge/bend plane pins it (elbows and knees bend about one axis — if the
    hinge axis is misaligned the limb twists to match, see the roll block
    below). Aiming each bone independently in component space is deliberate:
    copying LOCAL rotations would compound position error down a chain of
    different-length bones.

    Positions, four treatments (see the SNAP_METAHUMAN_TARGETS,
    FINGER_TARGET_PREFIXES and base-offset comments for the per-region
    evidence): the whole skeleton first inherits a base translation measured
    at the thigh sockets (averaged left+right, applied at the root carry) so
    torso garments follow the body's commit-time pelvis shift; the
    forearm/hand anchors snap exactly onto their MetaHuman joint; finger
    joints are laid from their parent along the MetaHuman joint-to-joint
    DIRECTION at the VENDOR bind length; everything else carries its
    position from its parent at the bind-local offset rotated by the
    parent's delta. This split is the survivor of the measured failure
    modes of 2026-07-25/26: pure carry accumulated per-segment length
    mismatch from the hip out until the gloves floated a wrist-width off
    the hand bones; snapping EVERY aim-allowed bone fixed the gloves'
    placement but squashed their fingers (re-estimated joint noise ~2.5 cm
    vs 2-3 cm segments); carrying the fingers from the snapped hand kept
    vendor lengths but reproduced the Daz hand's internal fan,
    interpenetrating the skin-tight glove; snapping from the UPPERARM up
    smeared the sleeve at the carried clavicle; and the torso garments sat
    at their authored Daz position while the body shifted underneath. (The
    ORIGINAL pre-step-6.5 snap variant crumpled everything — it snapped onto
    the stranded FK hand chain, 11-20 cm off.) Nothing here reads the
    MetaHuman helper/corrective joints (absent from the map) or any
    conform-exported pose — only A-pose reference transforms, which are
    trustworthy.

    Returns (bind_positions, deltas, new_positions, stats); a vertex re-poses
    as new_positions[i] + deltas[i].rotate(vertex - bind_positions[i])."""
    identity = (0.0, 0.0, 0.0, 1.0)
    count = len(daz_bones)
    names = [str(info.name) for info in daz_bones]
    parents = [info.parent_index for info in daz_bones]
    bind_positions = []
    for info in daz_bones:
        translation = info.world_transform.translation
        bind_positions.append((translation.x, translation.y, translation.z))

    metahuman_count = len(metahuman_bones)
    metahuman_names = [str(info.name) for info in metahuman_bones]
    metahuman_position = {}
    metahuman_parent = {}
    for index, info in enumerate(metahuman_bones):
        translation = info.world_transform.translation
        metahuman_position[metahuman_names[index]] = (translation.x, translation.y, translation.z)
        parent_index = info.parent_index
        metahuman_parent[metahuman_names[index]] = \
            metahuman_names[parent_index] if 0 <= parent_index < metahuman_count else None

    def _descendant_depth(ancestor, name):
        depth, current = 0, name
        while current is not None:
            if current == ancestor:
                return depth
            current = metahuman_parent.get(current)
            depth += 1
        return None

    # First Daz bone (parent-first order) claims each MetaHuman bone;
    # duplicate mappings (l_toes after l_metatarsal -> ball_l, lHeel after
    # lFoot -> foot_l, pectorals after spine4 -> spine_05) stay unmapped and
    # ride along.
    mapped = [None] * count
    claimed = set()
    for index in range(count):
        target = DAZ_TO_METAHUMAN_BONE.get(names[index])
        if target is not None and target in metahuman_position and target not in claimed:
            mapped[index] = target
            claimed.add(target)

    # Torso base translation: RETIRED 2026-07-26 after one run. The theory
    # was that the commit shifts the body's pelvis under the
    # identity-carried torso garments and the thigh-socket deltas would
    # measure that shift — but they measured only 2.1 cm against a visibly
    # ~5+ cm garment offset, and applying it moved the garments the WRONG
    # way. Conclusion: the torso offset is not a joint displacement; it
    # lives in pelvis/spine ROTATION or surface/shape differences that
    # joint positions cannot see (DiagnoseTorsoOffset.py measures the
    # surfaces directly). Kept as a logged diagnostic, applied nowhere.
    base_offset = (0.0, 0.0, 0.0)
    thigh_deltas = [_vec_sub(metahuman_position[mapped[i]], bind_positions[i])
                    for i in range(count) if mapped[i] in ('thigh_l', 'thigh_r')]
    if thigh_deltas:
        total = (0.0, 0.0, 0.0)
        for delta in thigh_deltas:
            total = _vec_add(total, delta)
        base_offset = _vec_scale(total, 1.0 / len(thigh_deltas))

    children = [[] for _ in range(count)]
    for index in range(count):
        parent = parents[index]
        if parent >= index:
            raise RuntimeError('bone table is not parent-first at ' + names[index])
        if parent >= 0:
            children[parent].append(index)

    def _aim_candidates(bone):
        """Mapped chain continuations of `bone`, best first: (depth on the
        MetaHuman side, -bind offset length, bind dir, target dir, child)."""
        found = []
        for child in children[bone]:
            if mapped[child] is None or mapped[child] == mapped[bone]:
                continue
            depth = _descendant_depth(mapped[bone], mapped[child])
            if depth is None:
                continue
            bind_offset = _vec_sub(bind_positions[child], bind_positions[bone])
            target_offset = _vec_sub(metahuman_position[mapped[child]],
                                     metahuman_position[mapped[bone]])
            if _vec_length(bind_offset) < 0.05 or _vec_length(target_offset) < 0.05:
                continue
            found.append((depth, -_vec_length(bind_offset),
                          _vec_normalized(bind_offset), _vec_normalized(target_offset), child))
        found.sort(key=lambda entry: (entry[0], entry[1]))
        return found

    deltas = [identity] * count
    new_positions = [None] * count
    stats = {'mapped': 0, 'snapped': 0, 'chained': 0, 'max_move': 0.0, 'max_rotation': 0.0,
             'base_move': _vec_length(base_offset)}
    for index in range(count):
        parent = parents[index]
        parent_delta = deltas[parent] if parent >= 0 else identity
        if parent >= 0:
            position = _vec_add(new_positions[parent],
                                _quat_rotate(parent_delta,
                                             _vec_sub(bind_positions[index], bind_positions[parent])))
        else:
            position = bind_positions[index]

        delta = None
        if mapped[index] is not None and _aim_allowed(mapped[index]):
            # Aim child: prefer the true chain continuation (nearest MetaHuman
            # descendant), tie-break on the longest bind offset. Target
            # directions are measured joint-to-joint on the MetaHuman skeleton
            # — only the DIRECTION is taken from it, never a position.
            candidates = _aim_candidates(index)
            # The thumb is a side branch, not a chain continuation — with the
            # longest-offset tie-break the HAND aimed along its thumb (whose
            # base sits further from the wrist than the metacarpal joints),
            # rotating the whole palm to match the thumb direction. Ignore
            # thumb targets whenever any other continuation exists; the thumb
            # chain itself (thumb1 -> thumb2) keeps them.
            non_thumb = [entry for entry in candidates
                         if not str(mapped[entry[4]]).startswith('thumb')]
            if non_thumb:
                candidates = non_thumb
            if candidates:
                _, _, aim_bind, aim_target, aim_child = candidates[0]
                # FOOT: aim at the child JOINT from the carried ankle instead
                # of along the MetaHuman joint-to-joint direction. The ankle
                # joint is placed by convention ~1.2 cm higher on the
                # MetaHuman than the Daz rig over the same flat-on-the-ground
                # foot (XiuLin 2026-08-13), so the joint-to-joint line dips
                # ~10 deg steeper than the actual skin pose and the aim
                # pitched footwear plantar-down off the foot bones (boot toe
                # caps drooped, skin toes poked out the instep). Pointing the
                # bind direction at the MetaHuman ball joint FROM the foot's
                # carried position cancels that placement offset AND the
                # accumulated carry drift: the boot's toe end lands on the
                # ball joint where the body's toes actually are (measured:
                # foot delta 11.2 deg -> ~1.6 deg, toe joints 3.3-4.1 cm off
                # -> under 1 cm).
                if mapped[index] in ('foot_l', 'foot_r'):
                    to_child_joint = _vec_sub(metahuman_position[mapped[aim_child]], position)
                    if _vec_length(to_child_joint) > 0.05:
                        aim_target = _vec_normalized(to_child_joint)
                delta = _quat_between(aim_bind, aim_target)
                roll_source = 'inherited'
                roll_twist_degrees = 0.0
                best_roll = None
                for _, _, second_bind, second_target, _child in candidates[1:]:
                    perpendicular = _vec_length(_vec_cross(second_target, aim_target))
                    if perpendicular > 0.2 and (best_roll is None or perpendicular > best_roll[0]):
                        best_roll = (perpendicular, second_bind, second_target)
                if best_roll is not None:
                    twist = _signed_twist_angle(aim_target, _quat_rotate(delta, best_roll[1]), best_roll[2])
                    delta = _quat_mul(_quat_from_axis_angle(aim_target, twist), delta)
                    roll_source = 'sibling'
                    roll_twist_degrees = math.degrees(twist)
                else:
                    # Hinge / bend-plane roll pin: joints like the elbow and
                    # knee bend about ONE axis, so matching the chain
                    # DIRECTION is not enough — if the hinge axis is
                    # misaligned the limb must TWIST about the aim axis to
                    # match. The hinge normal on each skeleton is the cross
                    # product of consecutive segment directions; it exists
                    # whenever the joint carries even a slight bend (riggers
                    # leave one exactly so IK knows the hinge). Try the plane
                    # below this bone first (bone x child chain, e.g.
                    # upperarm x forearm = elbow), then the plane above
                    # (parent x bone — pins the forearm to the same elbow
                    # plane, and the thigh to the pelvis-leg plane). Guard
                    # against near-straight chains (normals too short) and
                    # opposite-sign bends (a ~180 deg twist is convention
                    # noise, not a real roll).
                    #
                    # The FOOT is excluded: its hinge normals are built from
                    # the ball->toe joint directions, which carry the same
                    # placement-convention noise the foot's aim override
                    # above cancels — measured as a mirrored +/-8.2 deg heel
                    # roll on flat A-pose feet that genuinely agree (XiuLin
                    # 2026-08-13). Both rigs plant the foot flat in A pose,
                    # so the inherited roll is already correct.
                    planes = () if mapped[index] in ('foot_l', 'foot_r') else ('child', 'parent')
                    for plane in planes:
                        if plane == 'child':
                            deeper = _aim_candidates(aim_child)
                            if not deeper:
                                continue
                            pair = (aim_bind, deeper[0][2], aim_target, deeper[0][3])
                        else:
                            parent_bone = parents[index]
                            if parent_bone < 0 or mapped[parent_bone] is None:
                                continue
                            above = _aim_candidates(parent_bone)
                            if not above:
                                continue
                            pair = (above[0][2], aim_bind, above[0][3], aim_target)
                        bind_normal = _vec_cross(pair[0], pair[1])
                        target_normal = _vec_cross(pair[2], pair[3])
                        # A hinge normal is only trustworthy over a real bend:
                        # 0.15 =~ a 9 deg bend. The elbow/wrist/knee/ankle all
                        # bend well past that in both skeletons; the
                        # near-straight finger chains fall under it (observed:
                        # the pinky metacarpal picked up a ~40 deg noise twist
                        # from its almost-straight chain). Same reasoning for
                        # the 60 deg cap — a real roll correction is ~15-25
                        # deg (elbow), anything larger is an opposite-sign or
                        # degenerate bend.
                        if _vec_length(bind_normal) < 0.15 or _vec_length(target_normal) < 0.15:
                            continue
                        twist = _signed_twist_angle(aim_target,
                                                    _quat_rotate(delta, _vec_normalized(bind_normal)),
                                                    _vec_normalized(target_normal))
                        if abs(twist) > math.radians(60.0):
                            continue
                        delta = _quat_mul(_quat_from_axis_angle(aim_target, twist), delta)
                        roll_source = 'hinge-' + plane
                        roll_twist_degrees = math.degrees(twist)
                        break
                if debug is not None:
                    debug[index] = {'via': names[aim_child], 'bind_dir': aim_bind,
                                    'target_dir': aim_target, 'roll': roll_source,
                                    'twist_deg': roll_twist_degrees}
        if delta is None:
            delta = parent_delta

        if mapped[index] in SNAP_METAHUMAN_TARGETS:
            position = metahuman_position[mapped[index]]
            stats['snapped'] += 1
        elif (mapped[index] is not None and mapped[index].startswith(FINGER_TARGET_PREFIXES)
              and parent >= 0 and mapped[parent] is not None and mapped[parent] != mapped[index]):
            span = _vec_sub(metahuman_position[mapped[index]], metahuman_position[mapped[parent]])
            span_length = _vec_length(span)
            bind_length = _vec_length(_vec_sub(bind_positions[index], bind_positions[parent]))
            if span_length > 1e-3:
                position = _vec_add(new_positions[parent],
                                    _vec_scale(span, bind_length / span_length))
                stats['chained'] += 1

        deltas[index] = delta
        new_positions[index] = position
        if mapped[index] is not None:
            stats['mapped'] += 1
            stats['max_move'] = max(stats['max_move'],
                                    _vec_length(_vec_sub(position, bind_positions[index])))
            stats['max_rotation'] = max(stats['max_rotation'],
                                        math.degrees(2.0 * math.acos(min(1.0, abs(delta[3])))))
    if extras is not None:
        extras['mapped'] = mapped
        extras['metahuman_position'] = metahuman_position
    return bind_positions, deltas, new_positions, stats


def _remap_daz_weights_to_metahuman(cloth_mesh, apose_bones_mesh, label):
    """Move a clothing mesh from its authored Daz pose onto the A-posed
    MetaHuman skeleton, keeping the vendor-authored weights:

    1. Capture every vertex's weights against the current (Daz) bone table.
    2. Re-pose the geometry into the MetaHuman A pose by LBS through the Daz
       skeleton ITSELF (_retarget_daz_pose_to_metahuman) — the deforming
       skeleton is the one the garment was fitted to, so the deformation
       cannot crumple; only the per-bone targets come from the MetaHuman.
    3. Replace the bone attributes with the A-posed MetaHuman skeleton
       (apose_bones_mesh = the final exported body) and rewrite the weights
       with name-mapped indices; unmapped Daz bones (face rig etc.) walk up
       the Daz parent chain to the nearest mapped ancestor.

    Geometry, bind skeleton and body then all AGREE at rest. No spatial
    correspondence is ever used, so the conform's hand-pose mismatch (which
    broke closest-point transfer at the fingers) and the exported conformed
    skeleton's garbage helper-joint transforms (which crumpled the
    conformed-pose bind) never enter the math."""
    daz_bones = _all_bones_info(cloth_mesh)
    daz_names = [str(info.name) for info in daz_bones]
    daz_parents = [info.parent_index for info in daz_bones]

    get_weights = _named_call(unreal.GeometryScript_BoneWeights, ('get_vertex_bone_weights',))
    set_weights = _named_call(unreal.GeometryScript_BoneWeights, ('set_vertex_bone_weights',))
    num_vertices = _first_if_tuple(_named_call(
        unreal.GeometryScript_MeshQueries, ('get_num_vertex_i_ds', 'get_num_vertex_ids'))(cloth_mesh))
    if isinstance(num_vertices, unreal.DynamicMesh):  # signature variant: count is an out param
        raise RuntimeError('could not read the vertex count')

    captured = []
    for vertex_id in range(num_vertices):
        result = get_weights(cloth_mesh, vertex_id)
        weight_list, has_weights = None, False
        if isinstance(result, tuple):
            for entry in result:
                if isinstance(entry, bool):
                    has_weights = entry
                elif not isinstance(entry, unreal.DynamicMesh) and entry is not None:
                    weight_list = entry
        captured.append([(entry.bone_index, entry.weight) for entry in weight_list]
                        if (has_weights and weight_list is not None) else [])

    # Re-pose the geometry into the MetaHuman A pose before touching the
    # skeleton, weighting each vertex's rigid per-bone motion by its vendor
    # weights.
    metahuman_bones = _all_bones_info(apose_bones_mesh)
    bind_positions, deltas, bone_positions, stats = _retarget_daz_pose_to_metahuman(daz_bones, metahuman_bones)
    if stats['mapped'] == 0:
        raise RuntimeError('no Daz bones map onto the MetaHuman skeleton')
    position_list = _read_all_vertex_positions(cloth_mesh)
    if position_list is None:
        raise RuntimeError('could not read the clothing vertex positions')
    to_vectors = _named_call(unreal.GeometryScript_List, ('convert_vector_list_to_array',))
    positions = to_vectors(position_list)
    if len(positions) != num_vertices:
        raise RuntimeError('vertex position list (%d) does not match the vertex count (%d)'
                           % (len(positions), num_vertices))
    bone_count = len(daz_bones)
    reposed = []
    for vertex_id in range(num_vertices):
        position = positions[vertex_id]
        vertex = (position.x, position.y, position.z)
        accumulated = (0.0, 0.0, 0.0)
        total_weight = 0.0
        for bone_index, weight in captured[vertex_id]:
            if not (0 <= bone_index < bone_count) or weight <= 0.0:
                continue
            moved = _vec_add(bone_positions[bone_index],
                             _quat_rotate(deltas[bone_index],
                                          _vec_sub(vertex, bind_positions[bone_index])))
            accumulated = _vec_add(accumulated, _vec_scale(moved, weight))
            total_weight += weight
        if total_weight > 1e-6:
            vertex = _vec_scale(accumulated, 1.0 / total_weight)
        reposed.append(unreal.Vector(vertex[0], vertex[1], vertex[2]))
    try:
        to_list = _named_call(unreal.GeometryScript_List, ('convert_array_to_vector_list',))
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
        _named_call_any(('GeometryScript_MeshEdits', 'GeometryScript_MeshBasicEdit', 'GeometryScript_BasicEdits'),
                        ('set_all_mesh_vertex_positions',))(cloth_mesh, vector_list)
    except Exception:
        set_position = _named_call_any(
            ('GeometryScript_MeshEdits', 'GeometryScript_MeshBasicEdit', 'GeometryScript_BasicEdits'),
            ('set_vertex_position',))
        for vertex_id in range(num_vertices):
            set_position(cloth_mesh, vertex_id, reposed[vertex_id])
    # Rotated surfaces (~40 deg at the fingers) need refreshed normals;
    # cosmetic, so best-effort.
    try:
        recompute = _named_call_any(('GeometryScript_Normals', 'GeometryScript_MeshNormals'),
                                    ('recompute_normals',))
        _first_if_tuple(recompute(cloth_mesh, unreal.GeometryScriptCalculateNormalsOptions()))
    except Exception as normals_error:
        unreal.log_warning("ConvertToMetaHuman.py: clothing '%s': normals not recomputed after the re-pose (%s)"
                           % (label, normals_error))
    unreal.log("ConvertToMetaHuman.py: clothing '%s': re-posed %d vertices into the MetaHuman A pose "
               '(%d/%d Daz bones aimed, %d snapped onto the MetaHuman joints, %d finger joints laid along '
               'the MetaHuman chain at vendor lengths, torso base offset %.1f cm, max joint move %.1f cm, '
               'max rotation %.1f deg)'
               % (label, num_vertices, stats['mapped'], len(daz_bones),
                  stats['snapped'], stats['chained'], stats['base_move'], stats['max_move'],
                  stats['max_rotation']))

    # Replace the bone attributes with the A-posed MetaHuman skeleton, then
    # build daz-bone-index -> metahuman-bone-index via the name map + parent
    # walk-up.
    _named_call_any(('GeometryScript_BoneWeights', 'GeometryScript_MeshBoneWeights', 'GeometryScript_SkinWeights'),
                    ('copy_bones_from_mesh',))(apose_bones_mesh, cloth_mesh)
    metahuman_index = {str(info.name): index for index, info in enumerate(_all_bones_info(cloth_mesh))}
    fallback_index = metahuman_index.get('pelvis', 0)
    resolved = []
    unmapped = set()
    for bone_index, bone_name in enumerate(daz_names):
        walk_index, target = bone_index, None
        while walk_index >= 0:
            candidate = DAZ_TO_METAHUMAN_BONE.get(daz_names[walk_index])
            if candidate is not None and candidate in metahuman_index:
                target = candidate
                break
            walk_index = daz_parents[walk_index]
        if target is None:
            unmapped.add(bone_name)
        resolved.append(metahuman_index[target] if target is not None else fallback_index)
    if unmapped:
        unreal.log_warning("ConvertToMetaHuman.py: clothing '%s': %d Daz bone(s) had no MetaHuman ancestor "
                           '(weights sent to pelvis): %s' % (label, len(unmapped), ', '.join(sorted(unmapped)[:8])))

    weighted_vertices = 0
    for vertex_id in range(num_vertices):
        merged = {}
        for bone_index, weight in captured[vertex_id]:
            target_index = resolved[bone_index] if 0 <= bone_index < len(resolved) else fallback_index
            merged[target_index] = merged.get(target_index, 0.0) + weight
        if not merged:
            merged[fallback_index] = 1.0
        bone_weights = []
        for target_index, weight in merged.items():
            entry = unreal.GeometryScriptBoneWeight()
            entry.bone_index = target_index
            entry.weight = weight
            bone_weights.append(entry)
        set_weights(cloth_mesh, vertex_id, bone_weights)
        weighted_vertices += 1
    unreal.log("ConvertToMetaHuman.py: clothing '%s': remapped vendor weights on %d vertices (%d Daz bones -> "
               '%d MetaHuman bones)' % (label, weighted_vertices, len(daz_names), len(metahuman_index)))


def _dyn_vertex_count(dynamic_mesh):
    count = _first_if_tuple(_named_call(
        unreal.GeometryScript_MeshQueries, ('get_num_vertex_i_ds', 'get_num_vertex_ids'))(dynamic_mesh))
    if isinstance(count, unreal.DynamicMesh):
        raise RuntimeError('could not read the vertex count')
    return count


def _read_vertex_bone_weight_lists(dynamic_mesh, vertex_count):
    """[(bone_index, weight), ...] per vertex from the mesh's bone attributes."""
    get_weights = _named_call(unreal.GeometryScript_BoneWeights, ('get_vertex_bone_weights',))
    weights = []
    for vertex_id in range(vertex_count):
        result = get_weights(dynamic_mesh, vertex_id)
        weight_list, has_weights = None, False
        if isinstance(result, tuple):
            for entry in result:
                if isinstance(entry, bool):
                    has_weights = entry
                elif not isinstance(entry, unreal.DynamicMesh) and entry is not None:
                    weight_list = entry
        weights.append([(entry.bone_index, entry.weight) for entry in weight_list]
                       if (has_weights and weight_list is not None) else [])
    return weights


def _foot_bone_indices(bones_info, side):
    """Bone-table indices of the MetaHuman foot chain for one side."""
    found = set()
    for index, info in enumerate(bones_info):
        name = str(info.name)
        if name in ('foot_' + side, 'ball_' + side) or ('toe' in name and name.endswith('_' + side)):
            found.add(index)
    return found


_WRAP_SPATIAL_OWNERS = ('GeometryScript_MeshSpatial', 'GeometryScript_SpatialQuery', 'GeometryScript_Spatial')


# Bake-only EXTREMITY fit (feet + fingers). The garment-safe retarget lets
# positions PARENT-CARRY, so the re-posed skin sits within ~1-4cm of the
# MetaHuman body rather than ON it — good enough for garments, but the
# likeness bake's nearest-point correspondence shears over that residual
# misalignment where the surface is small and detailed (foot/instep smears,
# knuckles slightly off — 2026-08-08). The BAKE source is disposable — it is
# built, projected from, and thrown away, and the garment wrap keeps its own
# un-fitted copy — so at the extremities the "snap everything" treatment that
# was rejected for garments is exactly right: each mapped foot/ball/toe/
# finger/metacarpal bone SNAPS onto its MetaHuman joint, AIMS along the
# segment to its (also snapped) child, and stretches its skin by the
# MetaHuman/Daz segment-length ratio. Squished or stretched skin still
# carries its own texture, and with every joint pinned, per-segment errors
# have nothing to compound down.
#
# EXTREMITIES ONLY — the whole-skeleton version of this fit was tried
# 2026-08-08 and made the glutes/thighs WORSE: pelvis/thigh/spine joint
# placement is convention-different (hip width +26%, spine segmentation
# ±12-15%), so snapping those joints does not align the torso skin, it
# DEFORMS it — glute skin sheared between differently-snapped joints into
# folds the correspondence cannot untangle. The torso already sits within
# ~1-2cm and bakes acceptably through the reach; extremity segments are
# small, anatomically pinned the same way on both rigs, and sit below an
# exactly-snapped anchor (hand) or a short carry chain (foot), so snapping
# them corrects drift without injecting convention differences. The HAND
# itself is excluded: the standard retarget already snaps it and pins its
# palm roll from the finger spread — a plain minimal-rotation aim here would
# lose that roll pin.
_BAKE_FIT_TARGETS = frozenset({'foot_l', 'foot_r', 'ball_l', 'ball_r'})
_BAKE_FIT_TARGET_PREFIXES = ('thumb_', 'index_', 'middle_', 'ring_', 'pinky_')


def _bake_fit_target(metahuman_target):
    return metahuman_target is not None and (
        metahuman_target in _BAKE_FIT_TARGETS
        or 'toe' in metahuman_target
        or metahuman_target.startswith(_BAKE_FIT_TARGET_PREFIXES))


def _bake_fit_overrides(daz_bones, bind_positions, deltas, new_positions, mapped, metahuman_position):
    """Per-bone override transforms forcing the extremity subtrees onto the
    MetaHuman joints (see the comment above): snapped position, segment-aimed
    rotation and a uniform per-bone SCALE (MetaHuman/Daz segment length, so
    the skin between joints stretches to fit — a vertex re-poses as
    fit_pos[i] + fit_delta[i].rotate(fit_scale[i] * (v - bind[i]))). Bones
    outside the fit set keep the standard retarget transforms at scale 1.
    Unmapped riders inside a fitted subtree (heel, the duplicate toes->ball
    claim) carry from their nearest FITTED ancestor at its scale. Leaf/tip
    bones with no mapped continuation inherit the parent's aim and scale.
    Aim children prefer non-thumb targets, then the longest bind segment.
    Returns (fit_deltas, fit_positions, fit_scales, fitted_count,
    max_correction_cm)."""
    count = len(daz_bones)
    parents = [info.parent_index for info in daz_bones]
    children = [[] for _ in range(count)]
    for index in range(count):
        if parents[index] >= 0:
            children[parents[index]].append(index)
    fit_deltas = list(deltas)
    fit_positions = list(new_positions)
    fit_scales = [1.0] * count
    in_subtree = [False] * count
    fitted_count = 0
    max_correction = 0.0
    for index in range(count):
        parent = parents[index]
        target = mapped[index]
        if _bake_fit_target(target) and target in metahuman_position:
            in_subtree[index] = True
            fit_positions[index] = metahuman_position[target]
            aim = None
            candidates = []
            for child in children[index]:
                child_target = mapped[child]
                if child_target is None or child_target == target or child_target not in metahuman_position:
                    continue
                bind_offset = _vec_sub(bind_positions[child], bind_positions[index])
                target_offset = _vec_sub(metahuman_position[child_target], metahuman_position[target])
                bind_length = _vec_length(bind_offset)
                target_length = _vec_length(target_offset)
                if bind_length < 0.05 or target_length < 0.05:
                    continue
                candidates.append((child_target.startswith('thumb_'), -bind_length,
                                   bind_offset, target_offset, bind_length, target_length))
            if candidates:
                candidates.sort(key=lambda entry: (entry[0], entry[1]))
                _, _, bind_offset, target_offset, bind_length, target_length = candidates[0]
                aim = (_quat_between(_vec_normalized(bind_offset), _vec_normalized(target_offset)),
                       max(0.3, min(3.0, target_length / bind_length)))
            if aim is None and parent >= 0 and in_subtree[parent]:
                aim = (fit_deltas[parent], fit_scales[parent])
            if aim is None:
                aim = (deltas[index], 1.0)
            fit_deltas[index], fit_scales[index] = aim
            fitted_count += 1
            max_correction = max(max_correction,
                                 _vec_length(_vec_sub(fit_positions[index], new_positions[index])))
        elif parent >= 0 and in_subtree[parent]:
            in_subtree[index] = True
            fit_deltas[index] = fit_deltas[parent]
            fit_scales[index] = fit_scales[parent]
            fit_positions[index] = _vec_add(
                fit_positions[parent],
                _quat_rotate(fit_deltas[parent],
                             _vec_scale(_vec_sub(bind_positions[index], bind_positions[parent]),
                                        fit_scales[parent])))
    return fit_deltas, fit_positions, fit_scales, fitted_count, max_correction


def _build_reposed_skin_dyn(merged_mesh, wearable_slot_names, apose_bones_mesh):
    """The Daz figure's SKIN (wearable + non-skin slots dropped) re-posed into
    the MetaHuman A pose through its OWN skeleton — the exact machinery the
    garments go through. Built once and shared by the garment wrap (after
    their re-pose the garments hug this surface BY CONSTRUCTION) and the body
    likeness bake (as an A-pose-aligned projection source: the conform's posed
    export never reproduces the Daz HAND pose, so the posed-transplant bake
    target sits cm off at the fingers/toes and texels grab nail plates and
    neighbouring fingers — flat nail-cream fingertips). Positions only;
    material ids and UVs are untouched.

    Returns (dynamic_mesh, vertex_count, bake_dynamic_mesh): the first mesh is
    the wrap/shared skin, the third is the BAKE-ONLY variant with the whole
    mapped skeleton force-fitted onto the MetaHuman joints (_bake_fit_overrides;
    same object as the first when the fit found nothing to move)."""
    def _keep_source_slot(slot_name):
        return _is_daz_skin_slot(slot_name) and slot_name not in wearable_slot_names

    source_dyn = _prune_dynamic_mesh_to_slots(
        _copy_skeletal_mesh_to_dynamic_mesh(merged_mesh), merged_mesh, _keep_source_slot)
    compact = _named_call_any(('GeometryScript_MeshRepair', 'GeometryScript_MeshRepairFunctions'),
                              ('compact_mesh',))
    _first_if_tuple(compact(source_dyn))

    # Re-pose the skin through the Daz skeleton — same math as the garments.
    daz_bones = _all_bones_info(source_dyn)
    metahuman_bones = _all_bones_info(apose_bones_mesh)
    retarget_extras = {}
    bind_positions, deltas, bone_positions, stats = _retarget_daz_pose_to_metahuman(
        daz_bones, metahuman_bones, extras=retarget_extras)
    if stats['mapped'] == 0:
        raise RuntimeError('no Daz bones map onto the MetaHuman skeleton')
    fit_deltas, fit_positions, fit_scales, fit_count, fit_correction = _bake_fit_overrides(
        daz_bones, bind_positions, deltas, bone_positions,
        retarget_extras.get('mapped') or [None] * len(daz_bones),
        retarget_extras.get('metahuman_position') or {})
    get_weights = _named_call(unreal.GeometryScript_BoneWeights, ('get_vertex_bone_weights',))
    num_vertices = _first_if_tuple(_named_call(
        unreal.GeometryScript_MeshQueries, ('get_num_vertex_i_ds', 'get_num_vertex_ids'))(source_dyn))
    if isinstance(num_vertices, unreal.DynamicMesh):
        raise RuntimeError('could not read the skin vertex count')
    positions = _named_call(unreal.GeometryScript_List, ('convert_vector_list_to_array',))(
        _read_all_vertex_positions(source_dyn))
    bone_count = len(daz_bones)
    reposed = []
    fitted = []
    for vertex_id in range(num_vertices):
        result = get_weights(source_dyn, vertex_id)
        weight_list, has_weights = None, False
        if isinstance(result, tuple):
            for entry in result:
                if isinstance(entry, bool):
                    has_weights = entry
                elif not isinstance(entry, unreal.DynamicMesh) and entry is not None:
                    weight_list = entry
        position = positions[vertex_id]
        vertex = (position.x, position.y, position.z)
        accumulated, total_weight = (0.0, 0.0, 0.0), 0.0
        fit_accumulated = (0.0, 0.0, 0.0)
        if has_weights and weight_list is not None:
            for entry in weight_list:
                bone_index, weight = entry.bone_index, entry.weight
                if not (0 <= bone_index < bone_count) or weight <= 0.0:
                    continue
                offset = _vec_sub(vertex, bind_positions[bone_index])
                moved = _vec_add(bone_positions[bone_index],
                                 _quat_rotate(deltas[bone_index], offset))
                accumulated = _vec_add(accumulated, _vec_scale(moved, weight))
                if fit_count:
                    fit_moved = _vec_add(fit_positions[bone_index],
                                         _quat_rotate(fit_deltas[bone_index],
                                                      _vec_scale(offset, fit_scales[bone_index])))
                    fit_accumulated = _vec_add(fit_accumulated, _vec_scale(fit_moved, weight))
                total_weight += weight
        fit_vertex = vertex
        if total_weight > 1e-6:
            vertex = _vec_scale(accumulated, 1.0 / total_weight)
            fit_vertex = _vec_scale(fit_accumulated, 1.0 / total_weight) if fit_count else vertex
        reposed.append(unreal.Vector(vertex[0], vertex[1], vertex[2]))
        if fit_count:
            fitted.append(unreal.Vector(fit_vertex[0], fit_vertex[1], fit_vertex[2]))

    def _apply_positions(dynamic_mesh, position_array):
        to_list = _named_call(unreal.GeometryScript_List, ('convert_array_to_vector_list',))
        list_result = to_list(position_array)
        vector_list = None
        for entry in (list_result if isinstance(list_result, tuple) else (list_result,)):
            if isinstance(entry, unreal.GeometryScriptVectorList):
                vector_list = entry
        if vector_list is None:
            raise RuntimeError('convert_array_to_vector_list returned no list')
        _named_call_any(('GeometryScript_MeshEdits', 'GeometryScript_MeshBasicEdit', 'GeometryScript_BasicEdits'),
                        ('set_all_mesh_vertex_positions',))(dynamic_mesh, vector_list)

    _apply_positions(source_dyn, reposed)

    # Bake variant: same deterministic construction (so the vertex order
    # matches the fitted position list), fitted foot positions applied.
    bake_dyn = source_dyn
    if fit_count:
        try:
            bake_dyn = _prune_dynamic_mesh_to_slots(
                _copy_skeletal_mesh_to_dynamic_mesh(merged_mesh), merged_mesh, _keep_source_slot)
            _first_if_tuple(compact(bake_dyn))
            bake_vertices = _first_if_tuple(_named_call(
                unreal.GeometryScript_MeshQueries, ('get_num_vertex_i_ds', 'get_num_vertex_ids'))(bake_dyn))
            if bake_vertices != num_vertices:
                raise RuntimeError('bake copy has %s vertices, expected %d' % (bake_vertices, num_vertices))
            _apply_positions(bake_dyn, fitted)
            unreal.log('ConvertToMetaHuman.py: bake skeleton fit — %d bones snapped onto the '
                       'MetaHuman joints (largest joint correction %.1fcm)' % (fit_count, fit_correction))
        except Exception as fit_error:
            unreal.log_warning('ConvertToMetaHuman.py: bake skeleton fit failed (%s) — baking from the '
                               'shared re-posed skin' % fit_error)
            bake_dyn = source_dyn
    return source_dyn, num_vertices, bake_dyn


def _build_clothing_wrap_context(merged_mesh, wearable_slot_names, final_body_mesh, final_head_mesh,
                                 apose_bones_mesh, reposed_skin=None):
    """Everything _wrap_clothing_to_final_body needs, built once per convert.

    SOURCE: the re-posed Daz skin (reposed_skin when the bake already built
    it — the LBS pass over the full figure is the expensive part — otherwise
    built here). TARGET: the final committed body + head skin, the surface
    the garments should hug. The head matters: the body export is HEADLESS,
    and without it collar/strap vertices that bound to Daz neck/head skin
    snap to the neck-opening rim."""
    if reposed_skin is None:
        reposed_skin = _build_reposed_skin_dyn(merged_mesh, wearable_slot_names, apose_bones_mesh)
    source_dyn, num_vertices = reposed_skin[0], reposed_skin[1]

    # Final body + head skin as one target surface.
    target_dyn = _prune_dynamic_mesh_to_slots(
        _copy_skeletal_mesh_to_dynamic_mesh(final_body_mesh), final_body_mesh,
        lambda slot: slot.lower().startswith('body'))
    if final_head_mesh is not None:
        head_dyn = _prune_dynamic_mesh_to_slots(
            _copy_skeletal_mesh_to_dynamic_mesh(final_head_mesh), final_head_mesh,
            lambda slot: slot.lower().startswith('head'))
        _named_call_any(('GeometryScript_MeshEdits', 'GeometryScript_MeshBasicEdit', 'GeometryScript_BasicEdits'),
                        ('append_mesh',))(target_dyn, head_dyn, unreal.Transform())
    else:
        unreal.log_warning('ConvertToMetaHuman.py: garment wrap target has NO head mesh — collar/strap '
                           'vertices near the neck will distort toward the body neck rim')

    build_bvh = _named_call_any(_WRAP_SPATIAL_OWNERS, ('build_bvh_for_mesh',))

    def _bvh_for(dynamic_mesh):
        result = build_bvh(dynamic_mesh)
        for entry in (result if isinstance(result, tuple) else (result,)):
            if isinstance(entry, unreal.GeometryScriptDynamicMeshBVH):
                return entry
        raise RuntimeError('build_bvh_for_mesh returned no BVH')

    context = {
        'source_dyn': source_dyn, 'target_dyn': target_dyn,
        'source_bvh': _bvh_for(source_dyn), 'target_bvh': _bvh_for(target_dyn),
        'source_normals': {}, 'target_normals': {},
        'find_nearest': _named_call_any(_WRAP_SPATIAL_OWNERS, ('find_nearest_point_on_mesh',)),
        'triangle_positions': _named_call(unreal.GeometryScript_MeshQueries, ('get_triangle_positions',)),
        'query_options': unreal.GeometryScriptSpatialQueryOptions(),
    }
    unreal.log('ConvertToMetaHuman.py: garment wrap ready — source skin %d verts re-posed, target %d tris%s'
               % (num_vertices, target_dyn.get_triangle_count(),
                  ' (incl. head)' if final_head_mesh is not None else ''))
    return context


def _wrap_clothing_to_final_body(cloth_mesh, context, label):
    """Surface transport for one (already re-posed) garment dynamic mesh:
    move each vertex by the local difference between the re-posed Daz skin
    and the final body+head, preserving the authored garment-to-skin offset
    along the normal. A correspondence is REJECTED when the source->target
    jump exceeds WRAP_MAX_CORRESPONDENCE or the target surface faces away
    from the source surface (closest-point grabbing the WRONG side — the
    opposite inner thigh under a crotch panel tears the garment). Rejected
    vertices are filled from garment-topology neighbors (a mid-garment vertex
    left behind IS a tear), then WRAP_SMOOTH_ITERATIONS relaxation passes
    blend discontinuities (bust curvature) instead of bunching."""
    find_nearest = context['find_nearest']
    query_options = context['query_options']
    triangle_positions = context['triangle_positions']
    source_dyn, target_dyn = context['source_dyn'], context['target_dyn']
    source_bvh, target_bvh = context['source_bvh'], context['target_bvh']

    def _nearest(dynamic_mesh, bvh, point):
        result = find_nearest(dynamic_mesh, bvh, unreal.Vector(point[0], point[1], point[2]), query_options)
        for entry in (result if isinstance(result, tuple) else (result,)):
            if isinstance(entry, unreal.GeometryScriptTrianglePoint):
                if not getattr(entry, 'valid', getattr(entry, 'b_valid', True)):
                    return None
                return entry
        return None

    def _normal(dynamic_mesh, triangle_id, cache):
        normal = cache.get(triangle_id)
        if normal is None:
            result = triangle_positions(dynamic_mesh, triangle_id)
            corners = [entry for entry in (result if isinstance(result, tuple) else (result,))
                       if isinstance(entry, unreal.Vector)]
            if len(corners) < 3:
                return None
            a = (corners[0].x, corners[0].y, corners[0].z)
            b = (corners[1].x, corners[1].y, corners[1].z)
            c = (corners[2].x, corners[2].y, corners[2].z)
            normal = _vec_normalized(_vec_cross(_vec_sub(b, a), _vec_sub(c, a)))
            cache[triangle_id] = normal
        return normal

    positions = _named_call(unreal.GeometryScript_List, ('convert_vector_list_to_array',))(
        _read_all_vertex_positions(cloth_mesh))
    vertex_count = len(positions)

    displacements = [None] * vertex_count
    skipped, capped, flipped = 0, 0, 0
    max_correspondence = 0.0
    for vertex_id in range(vertex_count):
        position = positions[vertex_id]
        vertex = (position.x, position.y, position.z)
        source_hit = _nearest(source_dyn, source_bvh, vertex)
        source_normal = (None if source_hit is None
                         else _normal(source_dyn, source_hit.triangle_id, context['source_normals']))
        if source_normal is None:
            skipped += 1
            continue
        source_point = (source_hit.position.x, source_hit.position.y, source_hit.position.z)
        target_hit = _nearest(target_dyn, target_bvh, source_point)
        target_normal = (None if target_hit is None
                         else _normal(target_dyn, target_hit.triangle_id, context['target_normals']))
        if target_normal is None:
            skipped += 1
            continue
        target_point = (target_hit.position.x, target_hit.position.y, target_hit.position.z)
        correspondence = _vec_length(_vec_sub(target_point, source_point))
        max_correspondence = max(max_correspondence, correspondence)
        if correspondence > WRAP_MAX_CORRESPONDENCE:
            capped += 1
        elif _vec_dot(source_normal, target_normal) < WRAP_MIN_NORMAL_DOT:
            flipped += 1
        else:
            offset = _vec_dot(_vec_sub(vertex, source_point), source_normal)
            new_vertex = _vec_add(target_point, _vec_scale(target_normal, offset))
            displacements[vertex_id] = _vec_sub(new_vertex, vertex)

    triangles = _read_all_triangles(cloth_mesh)
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
                    total = _vec_add(total, displacements[other])
                    count += 1
            if count:
                filled_this_round[vertex_id] = _vec_scale(total, 1.0 / count)
            else:
                remaining.append(vertex_id)
        if not filled_this_round:
            break
        for vertex_id, value in filled_this_round.items():
            displacements[vertex_id] = value
        unresolved = remaining
    for vertex_id in unresolved:
        displacements[vertex_id] = (0.0, 0.0, 0.0)  # island with no resolved vertex — stays put

    # RIGID FOOTWEAR REGION: a boot/shoe is a rigid shell standing off the
    # foot with an air gap (toe box, heel block, sole), not cloth hugging
    # skin. The per-vertex wrap dents that shell — neighbouring dome
    # vertices resolve against DIFFERENT toes/foot patches and inherit their
    # differing local deltas, crushing the smooth authored toe box onto the
    # toes until they poke through (XiuLin boots 2026-08-13; Hana sneakers
    # flattened the same way). Fix: per side, average the wrapped
    # displacements over the foot-weighted vertices into ONE rigid step and
    # blend each vertex toward it by its foot weight — the shell translates
    # as a unit onto the final foot, the shaft keeps the cloth wrap, and the
    # skinning weights make the transition seamless.
    try:
        if any((position.z < 30.0) for position in positions):
            bones = _all_bones_info(cloth_mesh)
            side_bones = {side: _foot_bone_indices(bones, side) for side in ('l', 'r')}
            weight_lists = _read_vertex_bone_weight_lists(cloth_mesh, vertex_count)
            foot_fraction = {'l': [0.0] * vertex_count, 'r': [0.0] * vertex_count}
            any_foot = False
            for vertex_id, vertex_weights in enumerate(weight_lists):
                total_weight = 0.0
                per_side = {'l': 0.0, 'r': 0.0}
                for bone_index, weight in vertex_weights:
                    if weight <= 0.0:
                        continue
                    total_weight += weight
                    for side in ('l', 'r'):
                        if bone_index in side_bones[side]:
                            per_side[side] += weight
                if total_weight > 1e-6:
                    for side in ('l', 'r'):
                        fraction = per_side[side] / total_weight
                        if fraction > 1e-3:
                            foot_fraction[side][vertex_id] = fraction
                            any_foot = True
            if any_foot:
                for side in ('l', 'r'):
                    rigid_total, rigid_weight = (0.0, 0.0, 0.0), 0.0
                    for vertex_id in range(vertex_count):
                        fraction = foot_fraction[side][vertex_id]
                        if fraction > 0.5:
                            rigid_total = _vec_add(rigid_total,
                                                   _vec_scale(displacements[vertex_id], fraction))
                            rigid_weight += fraction
                    if rigid_weight <= 1e-6:
                        continue
                    rigid_step = _vec_scale(rigid_total, 1.0 / rigid_weight)
                    blended = 0
                    for vertex_id in range(vertex_count):
                        fraction = foot_fraction[side][vertex_id]
                        if fraction <= 1e-3:
                            continue
                        displacements[vertex_id] = _vec_add(
                            _vec_scale(displacements[vertex_id], 1.0 - fraction),
                            _vec_scale(rigid_step, fraction))
                        blended += 1
                    unreal.log("ConvertToMetaHuman.py: clothing '%s': rigid footwear region (%s) — "
                               '%d vertices blended toward a %.2f cm rigid step'
                               % (label, side, blended, _vec_length(rigid_step)))
    except Exception as rigid_error:
        unreal.log_warning("ConvertToMetaHuman.py: clothing '%s': rigid footwear pass failed "
                           '(per-vertex wrap kept): %s' % (label, rigid_error))

    for _ in range(max(0, WRAP_SMOOTH_ITERATIONS)):
        smoothed = list(displacements)
        for vertex_id in range(vertex_count):
            if not neighbors[vertex_id]:
                continue
            total = (0.0, 0.0, 0.0)
            for other in neighbors[vertex_id]:
                total = _vec_add(total, displacements[other])
            average = _vec_scale(total, 1.0 / len(neighbors[vertex_id]))
            smoothed[vertex_id] = _vec_add(_vec_scale(displacements[vertex_id], 0.5),
                                           _vec_scale(average, 0.5))
        displacements = smoothed

    moved = []
    max_move, total_move = 0.0, 0.0
    for vertex_id in range(vertex_count):
        position = positions[vertex_id]
        displacement = displacements[vertex_id]
        step = _vec_length(displacement)
        max_move = max(max_move, step)
        total_move += step
        moved.append(unreal.Vector(position.x + displacement[0],
                                   position.y + displacement[1],
                                   position.z + displacement[2]))
    to_list = _named_call(unreal.GeometryScript_List, ('convert_array_to_vector_list',))
    list_result = to_list(moved)
    vector_list = None
    for entry in (list_result if isinstance(list_result, tuple) else (list_result,)):
        if isinstance(entry, unreal.GeometryScriptVectorList):
            vector_list = entry
    if vector_list is None:
        raise RuntimeError('convert_array_to_vector_list returned no list')
    _named_call_any(('GeometryScript_MeshEdits', 'GeometryScript_MeshBasicEdit', 'GeometryScript_BasicEdits'),
                    ('set_all_mesh_vertex_positions',))(cloth_mesh, vector_list)
    try:
        recompute = _named_call_any(('GeometryScript_Normals', 'GeometryScript_MeshNormals'),
                                    ('recompute_normals',))
        _first_if_tuple(recompute(cloth_mesh, unreal.GeometryScriptCalculateNormalsOptions()))
    except Exception as normals_error:
        unreal.log_warning("ConvertToMetaHuman.py: clothing '%s': normals not recomputed after the wrap (%s)"
                           % (label, normals_error))
    resolved = vertex_count - skipped - capped - flipped
    unreal.log("ConvertToMetaHuman.py: clothing '%s': wrapped onto the final body — %d/%d vertices resolved "
               'directly (%d capped, %d wrong-side, %d no-hit; %d filled in %d rounds), mean move %.2f cm, '
               'max %.2f cm, max correspondence %.2f cm'
               % (label, resolved, vertex_count, capped, flipped, skipped, unresolved_start, fill_rounds,
                  (total_move / vertex_count) if vertex_count else 0.0, max_move, max_correspondence))


def _split_clothing_item(merged_mesh, item, body_weights_mesh, body_mesh, clothing_package_path,
                         bind_bones_mesh=None, apose_bones_mesh=None, wrap_context=None):
    """One clothing wearable -> its own skeletal mesh asset under
    clothing_package_path, carrying its Daz materials.

    Preferred path: re-pose the vendor geometry from its authored Daz pose
    into the MetaHuman A pose through its OWN Daz skeleton, then bind at rest
    against apose_bones_mesh (the final A-posed body) with the vendor weights
    name-swapped onto the MetaHuman bones — geometry, bind skeleton and body
    all agree at rest, and animation deforms the garment from a correct rest
    shape (_remap_daz_weights_to_metahuman).

    Fallback: nearest-surface weight transfer from body_weights_mesh (the
    POSED pre-commit body export — it lies on the Daz-posed clothing) with
    the bind skeleton from bind_bones_mesh (the conformed-pose live-body
    duplicate) when available. Known-degraded: the conform does not reproduce
    the Daz hand pose and the exported helper-joint transforms are unreliable
    — this is the combination that crumpled the gloves. Last resort keeps the
    Daz skeleton + original weights. Returns the asset or None."""
    label = item['label']
    try:
        # Section-ordered, so these double as dynamic-mesh material ids below.
        merged_slots = _lod_material_slots(merged_mesh)
        kept_indices = [material_id for material_id, material in enumerate(merged_slots)
                        if str(material.material_slot_name) in item['slots']]
        if not kept_indices:
            unreal.log_warning("ConvertToMetaHuman.py: clothing '%s' owns no material sections on the import — skipped"
                               % label)
            return None
        cloth_mesh = _prune_dynamic_mesh_to_slots(
            _copy_skeletal_mesh_to_dynamic_mesh(merged_mesh), merged_mesh,
            lambda slot: slot in item['slots'])
        compact = _named_call_any(('GeometryScript_MeshRepair', 'GeometryScript_MeshRepairFunctions'),
                                  ('compact_mesh',))
        _first_if_tuple(compact(cloth_mesh))
        # Compaction keeps the original (sparse) material ids; the new asset
        # derives its sections and slot count from them, so remap to 0..n-1.
        remap_materials = _named_call(unreal.GeometryScript_Materials,
                                      ('remap_material_i_ds', 'remap_material_ids'))
        for new_id, old_id in enumerate(kept_indices):
            if new_id != old_id:
                remap_materials(cloth_mesh, old_id, new_id)

        skeleton = None
        bone_owners = ('GeometryScript_BoneWeights', 'GeometryScript_MeshBoneWeights', 'GeometryScript_SkinWeights')
        try:
            try:
                # Preferred: no spatial correspondence and no conform-exported
                # pose anywhere — re-pose the geometry into the A pose through
                # the garment's own Daz skeleton, keep the vendor weights,
                # bind against the A-posed MetaHuman skeleton.
                if apose_bones_mesh is None:
                    raise RuntimeError('A-posed body bones unavailable')
                _remap_daz_weights_to_metahuman(cloth_mesh, apose_bones_mesh, label)
                # Surface transport BEFORE asset creation: geometry is final
                # when the asset is built, weights are never rewritten after.
                if wrap_context is not None:
                    try:
                        _wrap_clothing_to_final_body(cloth_mesh, wrap_context, label)
                    except Exception as wrap_error:
                        unreal.log_warning("ConvertToMetaHuman.py: clothing '%s': surface wrap failed — "
                                           'garment keeps its re-posed position (expect the body-delta '
                                           'offset): %s' % (label, wrap_error))
            except Exception as remap_error:
                unreal.log_warning("ConvertToMetaHuman.py: clothing '%s': A-pose re-pose + vendor weight remap "
                                   'failed (%s) — falling back to closest-point transfer' % (label, remap_error))
                bone_source = bind_bones_mesh if bind_bones_mesh is not None else body_weights_mesh
                _named_call_any(bone_owners, ('copy_bones_from_mesh',))(bone_source, cloth_mesh)
                transfer_options = unreal.GeometryScriptTransferBoneWeightsOptions()
                try:
                    transfer_options.transfer_method = _enum_value(
                        unreal.TransferBoneWeightsMethod, ('CLOSEST_POINT_ON_SURFACE',))
                except Exception:
                    pass  # default transfer method
                # The transfer's default OutputTargetMeshBones=SourceBones REPLACES
                # the target's bone attributes with the weight source's — silently
                # undoing the bind skeleton chosen above. TargetBones keeps our
                # bones and reindexes the transferred weights against them BY NAME.
                try:
                    transfer_options.output_target_mesh_bones = _enum_value(
                        unreal.OutputTargetMeshBones, ('TARGET_BONES',))
                except Exception:
                    unreal.log_warning("ConvertToMetaHuman.py: clothing '%s': OutputTargetMeshBones unavailable — "
                                       'the weight source mesh dictates the bind skeleton' % label)
                transfer = _named_call_any(bone_owners, ('transfer_bone_weights_from_mesh',))
                _first_if_tuple(transfer(body_weights_mesh, cloth_mesh, transfer_options))
            skeleton = body_mesh.skeleton
        except Exception as reskin_error:
            unreal.log_warning("ConvertToMetaHuman.py: clothing '%s' re-skin to the MetaHuman skeleton failed "
                               '(keeping the Daz skinning): %s' % (label, reskin_error))
            skeleton = merged_mesh.skeleton

        asset_path = clothing_package_path + '/SK_' + _sanitize_daz_name(label)
        # No overwrite option on the skeletal-mesh asset util — replace like the
        # rest of the pipeline replaces its outputs on reconvert.
        if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
            _close_asset_editors(asset_path)
            if not _safe_delete_asset(asset_path, "clothing '%s'" % label):
                unreal.log_warning("ConvertToMetaHuman.py: clothing '%s': could not replace %s — skipped"
                                   % (label, asset_path))
                return None
        create_asset = _named_call(unreal.GeometryScript_NewAssetUtils,
                                   ('create_new_skeletal_mesh_asset_from_mesh',))
        create_options = unreal.GeometryScriptCreateNewSkeletalMeshAssetOptions()
        # Build the asset's reference skeleton from the dynamic mesh's bone
        # attributes rather than the USkeleton rest pose: on the preferred
        # path these are the final body's A pose (== the rest pose, harmless
        # but exact), on the transfer fallback the conformed pose the
        # Daz-pose geometry was bound against.
        try:
            create_options.set_editor_property('use_mesh_bone_proportions', True)
        except Exception:
            unreal.log_warning("ConvertToMetaHuman.py: clothing '%s': use_mesh_bone_proportions unavailable "
                               '— binding against the skeleton rest pose' % label)
        created = _first_if_tuple(create_asset(
            cloth_mesh, skeleton, asset_path, create_options))
        if created is None:
            unreal.log_warning("ConvertToMetaHuman.py: clothing '%s': asset creation failed at %s" % (label, asset_path))
            return None
        try:
            materials = []
            for old_id in kept_indices:
                source_material = merged_slots[old_id]
                skeletal_material = unreal.SkeletalMaterial()
                skeletal_material.set_editor_property('material_interface',
                                                      source_material.material_interface)
                skeletal_material.set_editor_property('material_slot_name',
                                                      source_material.material_slot_name)
                materials.append(skeletal_material)
            created.set_editor_property('materials', materials)
        except Exception as material_error:
            unreal.log_warning("ConvertToMetaHuman.py: clothing '%s': material assignment failed: %s"
                               % (label, material_error))
        unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=True)
        unreal.log("ConvertToMetaHuman.py: clothing '%s' -> %s%s"
                   % (label, asset_path, '' if skeleton is body_mesh.skeleton else ' (Daz skeleton)'))
        return created
    except Exception as split_error:
        unreal.log_warning("ConvertToMetaHuman.py: clothing '%s' split failed: %s" % (label, split_error))
        return None


def _named_call_any(owner_names, name_variants):
    for owner_name in owner_names:
        owner = getattr(unreal, owner_name, None)
        if owner is None:
            continue
        for name in name_variants:
            func = getattr(owner, name, None)
            if func is not None:
                return func
    raise AttributeError('none of %s have any of %s' % (owner_names, name_variants))


def _transplant_posed_positions(final_dynamic_mesh, posed_skeletal_mesh):
    """Copy the pre-commit posed export's vertex positions onto the final
    mesh's dynamic copy (both exports duplicate the same DNA mesh, so vertex
    order matches when the counts do). The result is a bake target with the
    FINAL mesh's UV atlas but the POSED geometry, sitting ~mm from the Daz
    source everywhere. Returns True on success, False to fall back to the
    A-posed bake. Must run BEFORE any pruning — triangle deletion may discard
    orphaned vertices and desynchronize the vertex order."""
    try:
        posed_dynamic_mesh = _copy_skeletal_mesh_to_dynamic_mesh(posed_skeletal_mesh)
        get_num_vertices = _named_call(unreal.GeometryScript_MeshQueries,
                                       ('get_num_vertex_i_ds', 'get_num_vertex_ids'))
        final_count = _first_if_tuple(get_num_vertices(final_dynamic_mesh))
        posed_count = _first_if_tuple(get_num_vertices(posed_dynamic_mesh))
        if final_count != posed_count:
            unreal.log_warning('ConvertToMetaHuman.py: posed/final vertex counts differ (%s vs %s)'
                               % (posed_count, final_count))
            return False
        # Same vertex count is NOT enough: the pre-commit BODY export matched the
        # final count on Victoria97 yet transplanted scrambled (mostly-black
        # bake), and its UV atlas already proved it structurally different.
        # Sampled triangle index-triples catch a vertex-order mismatch.
        get_num_triangles = _named_call(unreal.GeometryScript_MeshQueries,
                                        ('get_num_triangle_i_ds', 'get_num_triangle_ids'))
        final_tri_count = _first_if_tuple(get_num_triangles(final_dynamic_mesh))
        posed_tri_count = _first_if_tuple(get_num_triangles(posed_dynamic_mesh))
        if final_tri_count != posed_tri_count:
            unreal.log_warning('ConvertToMetaHuman.py: posed/final triangle counts differ (%s vs %s)'
                               % (posed_tri_count, final_tri_count))
            return False
        get_triangle = _named_call(unreal.GeometryScript_MeshQueries,
                                   ('get_triangle_indexes', 'get_triangle_indices', 'get_triangle'))

        def _triangle_triple(mesh, triangle_id):
            result = get_triangle(mesh, triangle_id)
            items = result if isinstance(result, tuple) else (result,)
            for item in items:
                if isinstance(item, unreal.IntVector):
                    return (item.x, item.y, item.z)
            return None

        sample_count = 24
        for sample in range(sample_count):
            triangle_id = (final_tri_count - 1) * sample // max(1, sample_count - 1)
            if _triangle_triple(final_dynamic_mesh, triangle_id) != _triangle_triple(posed_dynamic_mesh, triangle_id):
                unreal.log_warning('ConvertToMetaHuman.py: posed/final vertex order differs (triangle %d) '
                                   '— baking against the A-posed mesh instead' % triangle_id)
                return False
        # Signature: GetAllVertexPositions(TargetMesh, PositionList&, bSkipGaps,
        # bHasVertexIDGaps&) — python wants skip_gaps as arg 2. skip_gaps=False
        # keeps the list indexed by vertex id, matching SetAllMeshVertexPositions.
        get_positions = _named_call(unreal.GeometryScript_MeshQueries, ('get_all_vertex_positions',))
        try:
            positions_result = get_positions(posed_dynamic_mesh, False)
        except TypeError:
            positions_result = get_positions(posed_dynamic_mesh)
        position_list = None
        has_gaps = False
        if isinstance(positions_result, tuple):
            for item in positions_result:
                if isinstance(item, unreal.GeometryScriptVectorList):
                    position_list = item
                elif isinstance(item, bool):
                    has_gaps = has_gaps or item
        elif isinstance(positions_result, unreal.GeometryScriptVectorList):
            position_list = positions_result
        if position_list is None:
            unreal.log_warning('ConvertToMetaHuman.py: could not read posed vertex positions')
            return False
        if has_gaps:
            unreal.log('ConvertToMetaHuman.py: posed mesh reports vertex id gaps (same-exporter structure assumed)')
        set_positions = _named_call_any(
            ('GeometryScript_MeshEdits', 'GeometryScript_MeshBasicEdit', 'GeometryScript_BasicEdits'),
            ('set_all_mesh_vertex_positions',))
        set_positions(final_dynamic_mesh, position_list)
        unreal.log('ConvertToMetaHuman.py: pose-aligned bake target (%d vertices transplanted)' % final_count)
        return True
    except Exception as transplant_error:
        unreal.log_warning('ConvertToMetaHuman.py: posed-position transplant failed: ' + str(transplant_error))
        return False


def _prepare_bake_target_mesh(target_skeletal_mesh, keep_slot, posed_skeletal_mesh=None):
    """Dynamic-mesh bake target for one MetaHuman mesh: LOD0 copy, optionally
    pose-aligned by transplanting the posed export's vertex positions (must
    happen before pruning — deletion may discard orphaned vertices and desync
    the order), pruned to the keep_slot sections, and its UV island shifted
    into [0,1]. The MetaHuman BODY atlas lives in UDIM tile [1,2] (the head is
    in [0,1]) and the baker only rasterizes [0,1]; the materials sample with
    wrapping, so textures baked in the shifted layout apply unchanged.
    Returns (dynamic_mesh, pose_aligned)."""
    target_dynamic_mesh = _copy_skeletal_mesh_to_dynamic_mesh(target_skeletal_mesh)
    pose_aligned = (posed_skeletal_mesh is not None
                    and _transplant_posed_positions(target_dynamic_mesh, posed_skeletal_mesh))
    target_dynamic_mesh = _prune_dynamic_mesh_to_slots(
        target_dynamic_mesh, target_skeletal_mesh, keep_slot)

    box_result = unreal.GeometryScript_MeshQueries.get_uv_set_bounding_box(target_dynamic_mesh, 0)
    box = box_result[0] if isinstance(box_result, tuple) else box_result
    target_tile = (math.floor(box.min.x), math.floor(box.min.y))
    if target_tile != (0, 0):
        _translate_mesh_uvs(target_dynamic_mesh, unreal.Vector2D(-target_tile[0], -target_tile[1]),
                            unreal.GeometryScriptMeshSelection())
    return target_dynamic_mesh, pose_aligned


def _run_texture_bake(target_dynamic_mesh, source_dynamic_mesh, bake_types, asset_path,
                      projection_distance, resolution_names=BAKE_RESOLUTION_NAMES):
    """Mesh-to-mesh texture bake. Returns the persisted texture asset when
    asset_path is set; with asset_path=None returns the transient baked
    texture (it carries source data). None on failure."""
    bake_options = unreal.GeometryScriptBakeTextureOptions()
    bake_options.resolution = _enum_value(unreal.GeometryScriptBakeResolution, resolution_names)
    bake_options.samples_per_pixel = _enum_value(unreal.GeometryScriptBakeSamplesPerPixel, BAKE_SAMPLES_NAMES)
    bake_options.projection_distance = projection_distance
    try:
        bake_options.gutter_size = BAKE_GUTTER_SIZE
    except Exception:
        pass  # property name varies across 5.8 builds; the 4px default still bakes

    baked = unreal.GeometryScript_Bake.bake_texture(
        target_dynamic_mesh, unreal.Transform(),
        unreal.GeometryScriptBakeTargetMeshOptions(),
        source_dynamic_mesh, unreal.Transform(),
        unreal.GeometryScriptBakeSourceMeshOptions(),
        bake_types,
        bake_options)
    if not baked or baked[0] is None:
        unreal.log_warning('ConvertToMetaHuman.py: bake produced no texture for ' + (asset_path or '<transient>'))
        return None
    if not asset_path:
        return baked[0]
    return _persist_baked_texture(baked[0], asset_path)


def _persist_baked_texture(baked_texture, asset_path):
    """Persist a transient baked texture (source data only) as an asset at
    asset_path, dodging the broken overwrite branch. Returns the persisted
    texture or None."""
    # CreateNewTexture2DAsset's overwrite path rebuilds the existing asset via
    # NewObject-over-the-loaded-object (FTexture2DBuilder::InitializeAndReplace-
    # ExistingTexture); on 5.8 that leaves the source bulkdata claiming a payload
    # it can't produce, and every save of the package then fails the
    # EditorBulkData TryPayloadValidationForSaving ensure ('invalid payload...
    # needs to be reverted/recreated'). Delete the stale asset first so the
    # create always takes the clean transient->package move; callers re-point
    # the MICs at the returned texture afterwards.
    #
    # Deleting alone is NOT always enough (2026-07-28, Victoria9Overalls
    # reconvert): delete_asset reports success when ObjectTools'
    # DeleteSingleObject strips RF_Standalone|RF_Public — BEFORE the garbage
    # collect actually purges the object. An old texture something still
    # references (force delete's reference replacement misses subobject
    # referencers, per its own source comment) survives the GC in place, still
    # named at the target path, and CreateNewTexture2DAsset's StaticFindObject
    # then routes into the broken overwrite branch anyway. So after the
    # delete, shove any survivor out of the way by renaming it into the
    # transient package (it is a flagless in-memory husk by then); if even
    # that fails, bake to a sibling name instead — anything but the overwrite
    # branch. Sibling litter from a degraded run gets deleted here next time.
    # One candidate is cleared at a time. Sweeping all three up front deleted the
    # _R/_R2 siblings on every healthy run, including any the user had authored
    # under those names, even though the first candidate was the one about to be
    # used.
    candidate_paths = (asset_path, asset_path + '_R', asset_path + '_R2')
    save_path = None
    for candidate in candidate_paths:
        candidate_name = candidate.rsplit('/', 1)[-1]
        deleted_cleanly = True
        if unreal.EditorAssetLibrary.does_asset_exist(candidate):
            deleted_cleanly = _safe_delete_asset(candidate, 'stale baked texture')
        try:
            survivor = unreal.find_object(None, candidate + '.' + candidate_name)
        except Exception:
            survivor = None
        if survivor is None:
            save_path = candidate
            break
        # Only a delete that REPORTED SUCCESS leaves the flagless husk the rename
        # below assumes. A delete that failed means the object is a live asset
        # (still referenced, or the user cancelled the confirmation), and renaming
        # that into the transient package would detach a real asset from its
        # package and null out everything pointing at it. Leave it alone and take
        # the next name instead.
        if not deleted_cleanly:
            unreal.log_warning('ConvertToMetaHuman.py: %s is a live asset that would not delete — '
                               'leaving it untouched and baking to the next name' % candidate)
            continue
        unreal.log_warning('ConvertToMetaHuman.py: a stale in-memory texture still occupies '
                           + candidate + ' after the delete')
        try:
            transient_package = unreal.find_object(None, '/Engine/Transient')
            if transient_package is not None and survivor.rename(
                    '%s_stale_%d' % (candidate_name, id(survivor)), transient_package):
                unreal.log('ConvertToMetaHuman.py: renamed the stale texture out of ' + candidate)
                save_path = candidate
                break
        except Exception as rename_error:
            unreal.log_warning('ConvertToMetaHuman.py: stale texture rename failed at %s: %s'
                               % (candidate, rename_error))
    if save_path is None:
        save_path = asset_path
        unreal.log_warning('ConvertToMetaHuman.py: every candidate path for ' + asset_path
                           + ' is occupied — creating over the stale object; the save will likely fail')

    persisted = _first_if_tuple(
        unreal.GeometryScript_NewAssetUtils.create_new_texture2d_asset(
            baked_texture, save_path,
            unreal.GeometryScriptCreateNewTexture2DAssetOptions(overwrite_if_exists=True)))
    if persisted:
        if not unreal.EditorAssetLibrary.save_asset(save_path, only_if_is_dirty=True):
            unreal.log_warning('ConvertToMetaHuman.py: baked texture failed to save — ' + save_path)
    return persisted


def _bake_daz_basecolor(daz_dynamic_mesh, daz_textures, target_skeletal_mesh, keep_slot, asset_path,
                        fallback_projection_distance, aligned_projection_distance, posed_skeletal_mesh=None,
                        facing_filtered=False):
    """Bake the Daz diffuse set onto one MetaHuman mesh's UV layout and persist
    the result as a texture asset. Returns the persistent UTexture2D or None.
    When the posed pre-commit export is available and topology-compatible, the
    bake runs pose-aligned at aligned_projection_distance; otherwise against
    the A-posed final mesh at fallback_projection_distance. With
    facing_filtered=True (the body) the projection runs through the C++
    facing-rejecting baker instead of the engine's — see BAKE_MIN_FACING_DOT."""
    target_dynamic_mesh, pose_aligned = _prepare_bake_target_mesh(
        target_skeletal_mesh, keep_slot, posed_skeletal_mesh)
    projection_distance = aligned_projection_distance if pose_aligned else fallback_projection_distance
    if facing_filtered and hasattr(unreal.DazToUnrealBlueprintUtils, 'bake_basecolor_facing_filtered'):
        baked = unreal.DazToUnrealBlueprintUtils.bake_basecolor_facing_filtered(
            target_dynamic_mesh, daz_dynamic_mesh, daz_textures,
            BAKE_RESOLUTION, projection_distance, BAKE_PREFER_RAY_DISTANCE,
            BAKE_MIN_FACING_DOT, BAKE_GUTTER_SIZE)
        if baked is not None:
            return _persist_baked_texture(baked, asset_path) if asset_path else baked
        unreal.log_warning('ConvertToMetaHuman.py: facing-filtered bake failed for '
                           + (asset_path or '<transient>') + ' — falling back to the engine baker')
    elif facing_filtered:
        unreal.log_warning('ConvertToMetaHuman.py: BakeBasecolorFacingFiltered missing (plugin needs a '
                           'rebuild) — body bake falls back to the engine baker')
    return _run_texture_bake(
        target_dynamic_mesh, daz_dynamic_mesh,
        [unreal.GeometryScript_Bake.make_bake_type_multi_texture(daz_textures, 0)],
        asset_path, projection_distance)


def _build_phantom_foot_cover(body_mesh, sides):
    """The body's own foot region (foot/ball/toe-weighted triangles), offset
    outward along its vertex normals by PHANTOM_FOOT_COVER_OFFSET — appended
    to the clothing-coverage source as phantom footwear so the whole foot
    reads as covered even where the skin locally protrudes through the real
    shell. Returns a transient DynamicMesh or None when no triangles qualify."""
    body_dyn = _prune_dynamic_mesh_to_slots(
        _copy_skeletal_mesh_to_dynamic_mesh(body_mesh), body_mesh,
        lambda slot: slot.lower().startswith('body'))
    vertex_count = _dyn_vertex_count(body_dyn)
    bones = _all_bones_info(body_dyn)
    covered_bones = set()
    for side in sides:
        covered_bones |= _foot_bone_indices(bones, side)
    if not covered_bones:
        return None
    weight_lists = _read_vertex_bone_weight_lists(body_dyn, vertex_count)
    fractions = []
    for vertex_weights in weight_lists:
        total = sum(weight for _, weight in vertex_weights if weight > 0.0)
        foot = sum(weight for bone_index, weight in vertex_weights
                   if weight > 0.0 and bone_index in covered_bones)
        fractions.append(foot / total if total > 1e-6 else 0.0)

    positions = _named_call(unreal.GeometryScript_List, ('convert_vector_list_to_array',))(
        _read_all_vertex_positions(body_dyn))
    triangles = _read_all_triangles(body_dyn)
    if triangles is None or len(positions) != vertex_count:
        raise RuntimeError('could not read the body geometry for the phantom foot cover')

    # Area-weighted vertex normals + the qualifying triangle list in one pass.
    normals = [(0.0, 0.0, 0.0)] * vertex_count
    kept = []
    for triangle in triangles:
        corners = (triangle.x, triangle.y, triangle.z)
        if any(corner < 0 or corner >= vertex_count for corner in corners):
            continue
        a, b, c = (positions[corner] for corner in corners)
        face = _vec_cross(_vec_sub((b.x, b.y, b.z), (a.x, a.y, a.z)),
                          _vec_sub((c.x, c.y, c.z), (a.x, a.y, a.z)))
        for corner in corners:
            normals[corner] = _vec_add(normals[corner], face)
        if all(fractions[corner] > PHANTOM_FOOT_COVER_MIN_WEIGHT for corner in corners):
            kept.append(corners)
    if not kept:
        return None

    remap = {}
    buffer_vertices = []
    buffer_triangles = []
    for corners in kept:
        new_corners = []
        for corner in corners:
            new_index = remap.get(corner)
            if new_index is None:
                new_index = len(buffer_vertices)
                remap[corner] = new_index
                position = positions[corner]
                normal = _vec_normalized(normals[corner])
                buffer_vertices.append(unreal.Vector(
                    position.x + normal[0] * PHANTOM_FOOT_COVER_OFFSET,
                    position.y + normal[1] * PHANTOM_FOOT_COVER_OFFSET,
                    position.z + normal[2] * PHANTOM_FOOT_COVER_OFFSET))
            new_corners.append(new_index)
        buffer_triangles.append(unreal.IntVector(new_corners[0], new_corners[1], new_corners[2]))
    buffers = unreal.GeometryScriptSimpleMeshBuffers()
    buffers.set_editor_property('vertices', buffer_vertices)
    buffers.set_editor_property('triangles', buffer_triangles)
    phantom = unreal.new_object(type=unreal.DynamicMesh)
    append_buffers = _named_call_any(
        ('GeometryScript_MeshBasicEdit', 'GeometryScript_MeshEdits', 'GeometryScript_BasicEdits'),
        ('append_buffers_to_mesh',))
    _first_if_tuple(append_buffers(phantom, buffers))
    if phantom.get_triangle_count() == 0:
        raise RuntimeError('phantom foot cover appended no triangles')
    return phantom


def _bake_clothing_coverage(body_mesh, coverage_items):
    """Gap-aware clothing coverage in the final body's UV layout: per-texel
    garment-to-skin gap distance where opaque clothing overlays the skin
    (facing-filtered raycasts — see the COVERAGE_* constants), 255 where bare.
    Feeds _create_hide_mask / CreateHideMaskFromGapCoverage.

    coverage_items: [(clothing_skeletal_mesh, keep_slot)] with keep_slot
    already excluding transparent surfaces (see-through garments must not cull
    the skin under them). The split clothing assets are bound in the conformed
    Daz pose while the body's reference pose is the MetaHuman A pose, so both
    are spawned as temporary actors and each clothing component is leader-posed
    to the body — the leader sits in its A-pose reference pose and the follower
    re-skins the clothing into it — before its geometry is read. If leader-pose
    evaluation doesn't take, the read falls back to the clothing's own bind
    pose, which is close to the A pose (the garments were split from
    body-fitted geometry). Returns the TRANSIENT baked coverage texture (it
    carries source data, which is all the mask builder reads), or None."""
    if not coverage_items:
        return None
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if actor_subsystem is None:
        raise RuntimeError('EditorActorSubsystem unavailable')
    spawned_actors = []
    try:
        origin = unreal.Vector(0.0, 0.0, 0.0)
        body_actor = actor_subsystem.spawn_actor_from_object(body_mesh, origin, transient=True)
        if body_actor is None:
            raise RuntimeError('could not spawn the coverage body actor')
        spawned_actors.append(body_actor)
        body_component = body_actor.skeletal_mesh_component

        copy_from_component = _named_call_any(
            ('GeometryScript_SceneUtils', 'GeometryScript_SceneUtilityFunctions'),
            ('copy_mesh_from_component',))
        append_mesh = _named_call_any(
            ('GeometryScript_MeshEdits', 'GeometryScript_MeshBasicEdit', 'GeometryScript_BasicEdits'),
            ('append_mesh',))

        # The editor-default SourceModel read poses the mesh description from the
        # component's own ComponentSpaceTransforms, which a leader-posed follower
        # never owns — CopyMeshFromComponent fails every item with 'Unable to
        # pose the source mesh' and returns an EMPTY (not null) mesh, so the
        # coverage would silently bake black. The RenderData read goes through
        # GetCPUSkinnedVertices, which resolves LeaderPoseComponent explicitly.
        copy_options = unreal.GeometryScriptCopyMeshFromComponentOptions()
        copy_options.requested_lod = unreal.GeometryScriptMeshReadLOD(
            lod_type=unreal.GeometryScriptLODType.RENDER_DATA, lod_index=0)

        source_mesh = None
        for clothing_mesh, keep_slot in coverage_items:
            item_actor = actor_subsystem.spawn_actor_from_object(clothing_mesh, origin, transient=True)
            if item_actor is None:
                unreal.log_warning('ConvertToMetaHuman.py: could not spawn a coverage actor for '
                                   + clothing_mesh.get_name())
                continue
            spawned_actors.append(item_actor)
            item_component = item_actor.skeletal_mesh_component
            item_component.set_leader_pose_component(body_component, True)
            refresh = getattr(item_component, 'refresh_bone_transforms', None)
            if callable(refresh):
                try:
                    refresh()
                except Exception:
                    pass

            item_dynamic_mesh = _first_if_tuple(copy_from_component(
                item_component, unreal.new_object(type=unreal.DynamicMesh),
                copy_options, True))
            if item_dynamic_mesh is not None and item_dynamic_mesh.get_triangle_count() == 0:
                unreal.log_warning('ConvertToMetaHuman.py: leader-posed coverage read was empty for '
                                   + clothing_mesh.get_name() + ' — falling back to its bind pose')
                item_dynamic_mesh = _copy_skeletal_mesh_to_dynamic_mesh(clothing_mesh)
            if item_dynamic_mesh is None or item_dynamic_mesh.get_triangle_count() == 0:
                unreal.log_warning('ConvertToMetaHuman.py: could not read coverage geometry from '
                                   + clothing_mesh.get_name())
                continue
            unreal.log('ConvertToMetaHuman.py: coverage source %s — %d tris' % (
                clothing_mesh.get_name(), item_dynamic_mesh.get_triangle_count()))
            _prune_dynamic_mesh_to_slots(item_dynamic_mesh, clothing_mesh, keep_slot)
            if source_mesh is None:
                source_mesh = item_dynamic_mesh
            else:
                append_mesh(source_mesh, item_dynamic_mesh, unreal.Transform())
        if source_mesh is None:
            return None

        # Footwear -> phantom foot cover (see the PHANTOM_FOOT_COVER_*
        # constants). Detection is deliberately two-stage: cheap bounds
        # check first, then real foot-bone weights on the candidates only.
        try:
            phantom_sides = set()
            for clothing_mesh, _keep_slot in coverage_items:
                try:
                    bounds = clothing_mesh.get_bounds()
                    if bounds.origin.z - bounds.box_extent.z > FOOTWEAR_BOUNDS_MIN_Z:
                        continue
                except Exception:
                    continue
                item_dyn = _copy_skeletal_mesh_to_dynamic_mesh(clothing_mesh)
                item_bones = _all_bones_info(item_dyn)
                item_sides = {side: _foot_bone_indices(item_bones, side) for side in ('l', 'r')}
                side_counts = {'l': 0, 'r': 0}
                for vertex_weights in _read_vertex_bone_weight_lists(item_dyn, _dyn_vertex_count(item_dyn)):
                    total = sum(weight for _, weight in vertex_weights if weight > 0.0)
                    if total <= 1e-6:
                        continue
                    for side in ('l', 'r'):
                        per_side = sum(weight for bone_index, weight in vertex_weights
                                       if weight > 0.0 and bone_index in item_sides[side])
                        if per_side / total > PHANTOM_FOOT_COVER_MIN_WEIGHT:
                            side_counts[side] += 1
                item_is_footwear = False
                for side in ('l', 'r'):
                    if side_counts[side] >= 25:
                        phantom_sides.add(side)
                        item_is_footwear = True
                if item_is_footwear:
                    unreal.log('ConvertToMetaHuman.py: coverage — %s detected as footwear (foot-weighted '
                               'verts l=%d r=%d)' % (clothing_mesh.get_name(), side_counts['l'], side_counts['r']))
            if phantom_sides:
                phantom = _build_phantom_foot_cover(body_mesh, sorted(phantom_sides))
                if phantom is not None:
                    unreal.log('ConvertToMetaHuman.py: coverage — phantom foot cover appended '
                               '(%s, %d tris at %.1f cm stand-off)'
                               % ('+'.join(sorted(phantom_sides)), phantom.get_triangle_count(),
                                  PHANTOM_FOOT_COVER_OFFSET))
                    append_mesh(source_mesh, phantom, unreal.Transform())
        except Exception as phantom_error:
            unreal.log_warning('ConvertToMetaHuman.py: phantom foot cover failed (coverage keeps the real '
                               'clothing only): ' + str(phantom_error))

        if not hasattr(unreal.DazToUnrealBlueprintUtils, 'bake_clothing_gap_coverage'):
            raise RuntimeError('bake_clothing_gap_coverage is missing — rebuild the DazToUnreal plugin')
        target_dynamic_mesh, _ = _prepare_bake_target_mesh(
            body_mesh, lambda slot: slot.startswith('body'))
        return unreal.DazToUnrealBlueprintUtils.bake_clothing_gap_coverage(
            target_dynamic_mesh, source_mesh, COVERAGE_RESOLUTION,
            COVERAGE_PROJECTION_DISTANCE, COVERAGE_MIN_FACING_DOT, COVERAGE_INWARD_OFFSET)
    finally:
        for actor in spawned_actors:
            try:
                actor_subsystem.destroy_actor(actor)
            except Exception:
                pass


def _create_hide_mask(coverage_texture, hide_mask_path):
    """Gap-aware hide mask from the coverage bake. Single call site for the
    policy constants so RemaskBodyHider picks up tweaks automatically."""
    if not hasattr(unreal.DazToUnrealBlueprintUtils, 'create_hide_mask_from_gap_coverage'):
        raise RuntimeError('create_hide_mask_from_gap_coverage is missing — rebuild the DazToUnreal plugin')
    return unreal.DazToUnrealBlueprintUtils.create_hide_mask_from_gap_coverage(
        coverage_texture, hide_mask_path, COVERAGE_PROJECTION_DISTANCE,
        HIDE_MASK_ERODE_PIXELS, HIDE_MASK_GAP_ERODE_PIXELS_PER_CM, HIDE_MASK_BLUR_PIXELS)


def _apply_baked_basecolor(skeletal_mesh, slot_matches, baked_texture, enable_texture_override):
    """Point the persisted skin MICs (created by apply_meta_human_skin_materials)
    at the baked texture. Handles the VT material variants.

    enable_texture_override: parts of M_skin_unified_UI replace the sampled
    Basecolor with parametric content — the BODY branch (static switch
    IsBody=true) renders albedo as a flat rbias/gbias/bbias tone and ignores
    the texture entirely, and the HEAD's chest bib (the _Head mesh extends
    over the clavicles) uses the stock Color_CHEST overlay + underwear straps,
    which shows as a tone seam against the baked body. The designed escape
    hatch is the 'Use Texture Override' scalar (MF_skin_UI_overrides_input):
    at 1 the shader lerps albedo+normal to the real Basecolor/Normal samples.
    The face region already samples Basecolor natively, so enabling it on the
    head only changes the chest bib."""
    applied = False
    for material in skeletal_mesh.materials:
        if not slot_matches(str(material.material_slot_name)):
            continue
        instance = material.material_interface
        if not isinstance(instance, unreal.MaterialInstanceConstant):
            continue

        # Override whichever basecolor parameter this MIC carries from the
        # generated MID ("Basecolor" or the virtual-texture variant).
        parameter_names = [str(texture_param.parameter_info.name)
                           for texture_param in instance.get_editor_property('texture_parameter_values')]
        basecolor_names = [name for name in parameter_names if name in ('Basecolor', 'Basecolor VT')] or ['Basecolor']
        for parameter_name in basecolor_names:
            if parameter_name.endswith('VT'):
                baked_texture.set_editor_property('virtual_texture_streaming', True)
            unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
                instance, parameter_name, baked_texture)

        if enable_texture_override:
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                instance, 'Use Texture Override', 1.0)
            # The stock underwear overlay has no Daz equivalent — match the source.
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(instance, 'Show Top Underwear', 0.0)
            # 'Show Top Underwear' is the ONLY toggle Epic ships — the BOTTOM
            # briefs blend unconditionally through the Underwear_Mask texture
            # (grey shorts painted over the baked skin), so blank the mask.
            flat_black_mask = unreal.load_object(
                name='/MetaHumanCharacter/Lookdev_UHM/Common/Textures/Placeholders/T_Flat_Black_M', outer=None)
            if flat_black_mask is not None:
                unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
                    instance, 'Underwear_Mask', flat_black_mask)
            else:
                unreal.log_warning('ConvertToMetaHuman.py: T_Flat_Black_M placeholder not found — '
                                   'the stock bottom underwear stays painted on the body')

        unreal.MaterialEditingLibrary.update_material_instance(instance)
        applied = True
    return applied


parser = argparse.ArgumentParser(description='Conform a fresh MetaHuman body to an imported Daz body mesh.')
parser.add_argument('--skeletalMesh', help='Imported Daz body skeletal mesh (package path) to conform to.')
parser.add_argument('--dtuFile', help='DTU file for the import (wearable metadata; future shape/keypoint hints).')
args = parser.parse_args()

# Wearable metadata: which material slots on the merged import belong to
# clothing/hair follower figures. Empty on old DTUs — everything then behaves
# exactly as before.
clothing_wearables, wearable_exclude_slots = _load_dtu_wearables(args.dtuFile)
if wearable_exclude_slots:
    unreal.log('ConvertToMetaHuman.py: DTU wearables — %d clothing item(s), %d excluded material slot(s)'
               % (len(clothing_wearables), len(wearable_exclude_slots)))

# `/Game/Path/Asset.Asset` -> name "Asset", package path "/Game/Path".
# Split on the LAST path segment first, so a bare package path (no `.Object`
# suffix) and a folder name containing a dot both resolve the way they read.
# Taking `.split('.')[-1]` off the whole string instead turned `/Game/X/Hero`
# into the name `/Game/X/Hero` and built nonsense paths from it.
target_mesh_path = args.skeletalMesh.strip().strip('"').strip("'")
package_path = target_mesh_path.rsplit('/', 1)[0]
asset_name = target_mesh_path.rsplit('/', 1)[-1].split('.')[0]
if not asset_name or not package_path.startswith('/Game/'):
    # '/Game' itself is refused on purpose: the convert deletes and force-saves
    # inside this folder, so accepting the content root would aim both at the
    # whole project. A character one folder down is all that is needed.
    raise RuntimeError(
        'ConvertToMetaHuman: --skeletalMesh must name an asset in a subfolder of /Game '
        '(e.g. /Game/Characters/Hero.Hero), got: ' + str(args.skeletalMesh))

# Confine every delete in this run to the converted mesh's own folder.
_set_pipeline_root(package_path)

# GC-crash guard: a (re)convert rewrites assets all over the package; any open
# asset editor (skeletal-mesh preview especially) still holding the old
# objects crashes the editor in garbage collection.
_close_asset_editors(package_path + '/')
character_asset_name = asset_name + '_MHC'
character_asset_path = package_path + '/' + character_asset_name

metahuman_subsystem = unreal.get_editor_subsystem(unreal.MetaHumanCharacterEditorSubsystem)
if metahuman_subsystem is None:
    raise RuntimeError('MetaHumanCharacterEditorSubsystem unavailable — is the MetaHumanCharacter plugin enabled?')

# 1. Load the Daz body (the conform target).
target_mesh = unreal.load_object(name=target_mesh_path, outer=None)
if target_mesh is None:
    raise RuntimeError('ConvertToMetaHuman.py: failed to load target mesh: ' + str(target_mesh_path))

# 2. Extract the target topology up front so we fail before creating an asset
#    if the mesh can't be read.
body_vertices, body_indices = _unwrap_conform_mesh_data(
    metahuman_subsystem.get_mesh_data_for_conforming(target_mesh))
if len(body_vertices) == 0 or len(body_indices) == 0:
    raise RuntimeError('ConvertToMetaHuman.py: no conformable mesh data on ' + str(target_mesh_path))
# Clothing/hair must not shape the body — fit to the figure, not the clothed
# silhouette. Falls back to the full mesh if the pruned read can't be trusted.
if wearable_exclude_slots:
    pruned_geometry = _conform_geometry_excluding_slots(target_mesh, wearable_exclude_slots, body_vertices)
    if pruned_geometry is not None:
        unreal.log('ConvertToMetaHuman.py: conform target pruned of wearables — %d of %d verts kept'
                   % (len(pruned_geometry[0]), len(body_vertices)))
        body_vertices, body_indices = pruned_geometry
unreal.log('ConvertToMetaHuman.py: target %d verts, %d tris' % (len(body_vertices), len(body_indices) // 3))

# 3. Create (or reuse) the MetaHumanCharacter asset.
character = unreal.load_object(name=character_asset_path, outer=None)
created_character = False
if character is None:
    character = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name=character_asset_name,
        package_path=package_path,
        asset_class=unreal.MetaHumanCharacter,
        factory=unreal.new_object(type=unreal.MetaHumanCharacterFactoryNew))
    created_character = True
if character is None:
    raise RuntimeError('ConvertToMetaHuman.py: failed to create MetaHumanCharacter at ' + character_asset_path)

if not metahuman_subsystem.try_add_object_to_edit(character):
    # Only roll back an asset this run created; never delete a pre-existing one.
    if created_character and not _safe_delete_asset(character_asset_path, 'orphaned character'):
        unreal.log_warning('ConvertToMetaHuman.py: failed to delete orphaned asset ' + character_asset_path)
    raise RuntimeError('ConvertToMetaHuman.py: unable to edit ' + character_asset_path + ' — already open for edit?')

conform_succeeded = False
try:
    # 4. Assemble a COMBINED conform: the Daz export is one mesh with the head
    #    attached, which is exactly what the 'combined' pipeline expects — it fits
    #    the face identity model AND the body model in a single local solve. For
    #    Combined, the WHOLE mesh goes into the body_vertices slots (engine tool
    #    does the same, MetaHumanCharacterEditorMeshImportTool.cpp:875); the
    #    head_vertices slots are only for the separate-parts HeadOnly/HeadAndBody
    #    modes. NOTE: no facial contour tracking data is provided here — the
    #    interactive tool auto-tracks 2D landmarks via a scene capture + the local
    #    face tracker, which is tool-private machinery. The solve runs without it;
    #    if face likeness is poor, feed 3D key_point_targets instead (Daz knows
    #    its facial landmarks) rather than replicating image tracking.
    conform_params = unreal.ConformTargetParams()
    conform_params.conform_target_mesh.target_parts_type = unreal.TargetPartsType.COMBINED
    conform_params.conform_target_mesh.body_vertices = body_vertices
    conform_params.conform_target_mesh.body_vertex_indices = body_indices
    conform_params.estimate_body_joints_from_mesh = True
    conform_params.auto_solve = True
    conform_params.body_conform_solve_settings.pipeline_name = 'combined'

    # Combined conform state is keyed under the combined_mesh slot
    # (engine tool: TargetMeshKey.CombinedMesh when not using separate parts).
    target_mesh_key = unreal.MetaHumanCharacterTargetMeshKey()
    target_mesh_key.combined_mesh = target_mesh

    unreal.log('ConvertToMetaHuman.py: running local combined (face+body) conform...')
    if not metahuman_subsystem.conform_to_target_meshes(character, target_mesh_key, conform_params):
        raise RuntimeError('ConvertToMetaHuman.py: conform_to_target_meshes failed')

    # 5. Export the posed DNA BEFORE flipping live state back to A pose — it reads
    #    the conformed posed state stored under target_mesh_key. Downstream tools
    #    (material baking, skeletal-mesh assembly) consume this DNA.
    posed_dna_export = unreal.MetaHumanPosedDNAExportParams()
    posed_dna_export.target_mesh_key = target_mesh_key
    posed_dna_export.project_path = package_path
    posed_dna_export.overwrite_existing_assets = True
    unreal.MetaHumanCharacterExportBlueprintLibrary.export_posed_dna(character, posed_dna_export)

    # 5.5 Posed geometry export — the GEOMETRY DONOR for the pose-aligned
    #     likeness bake in step 7.5. Taken NOW, while the edit session still
    #     holds the conformed Daz pose (it is unrecoverable after the commit in
    #     step 6). Not used as a bake target directly: its BODY UV atlas
    #     differs from the final exported meshes, so step 7.5 transplants these
    #     vertex positions onto the final meshes' dynamic copies instead.
    #     Non-fatal — without it the bake falls back to the A-posed meshes.
    bake_temp_path = package_path + '/MHBakeTemp'
    try:
        posed_geometry_params = unreal.MetaHumanGeometryExportParams()
        posed_geometry_params.project_path = bake_temp_path
        posed_geometry_params.head_skeletal_mesh = True
        posed_geometry_params.body_skeletal_mesh = True
        posed_geometry_params.full_body_skeletal_mesh = False
        posed_geometry_params.overwrite_existing_assets = True
        unreal.MetaHumanCharacterExportBlueprintLibrary.export_geometry(character, posed_geometry_params)
    except Exception as posed_export_error:
        unreal.log_warning('ConvertToMetaHuman.py: posed geometry export failed (bake will use the A-posed meshes): '
                           + str(posed_export_error))

    # 5.6 Conformed-pose BIND skeleton for the clothing split. ExportGeometry
    #     products carry the STANDARD REST-POSE reference skeleton even when
    #     their geometry is posed (verified on Victoria9TargetMH: the split
    #     clothing's ref skeleton matched the A-pose body bone-for-bone), so
    #     binding clothing against step 5.5's export gave an A-pose bind and
    #     the Daz-pose geometry rendered un-re-posed. The live edit-session
    #     body mesh is DNA-evaluated — duplicating it NOW (pre-commit)
    #     captures the conformed-pose skeleton. Non-fatal.
    posed_bind_body = None
    try:
        posed_bind_body = unreal.DazToUnrealBlueprintUtils.extract_meta_human_body_mesh(
            character, bake_temp_path + '/' + character_asset_name + '_PosedBindBody')
        if posed_bind_body is None:
            unreal.log_warning('ConvertToMetaHuman.py: conformed-pose body extraction returned nothing — '
                               'clothing will bind against the rest pose')
    except Exception as bind_extract_error:
        unreal.log_warning('ConvertToMetaHuman.py: conformed-pose body extraction failed — clothing will '
                           'bind against the rest pose: ' + str(bind_extract_error))

    # 6. Re-evaluate the body in the MetaHuman A pose and propagate to the face so
    #    the saved asset is canonical (the interactive tool does this on shutdown).
    metahuman_subsystem.commit_posed_state_as_a_pose(character, target_mesh_key)

    # 6.5 Re-estimate the body joints on the COMMITTED A-pose geometry. The
    #     step-4 conform estimated joints from the DAZ-POSED target; the commit
    #     above then re-posed the geometry to the MetaHuman A pose and the FK
    #     joints never followed — measured on NewTest3: hand_l 11cm and finger
    #     joints 15-20cm outside the skin they dominate (exactly where the Daz
    #     and MetaHuman poses genuinely differ — the hands), while the mcp/pip
    #     helper joints sat 0.1-0.3cm ON the committed knuckles. Fix: a second
    #     conform pass whose target is the character's own combined A-posed
    #     mesh — the shape solve is a no-op (target == own shape) but
    #     estimate_body_joints_from_mesh now sees the A pose, landing the FK
    #     chain inside the hands (worst hand-chain bone 2.5cm from its skin
    #     after, vs 11-20 before). The combined utility mesh is created here,
    #     earlier than its step-8 slot, to serve as the target; step 8
    #     recreates it so the saved utility reflects the refit skeleton.
    #     Non-fatal: on failure the skeleton keeps the Daz-pose hand joints.
    try:
        refit_target = unreal.DazToUnrealBlueprintUtils.create_meta_human_combined_mesh(
            character, package_path + '/' + character_asset_name + '_MHCombined')
        if refit_target is None:
            raise RuntimeError('combined mesh creation returned nothing')
        refit_vertices, refit_indices = _unwrap_conform_mesh_data(
            metahuman_subsystem.get_mesh_data_for_conforming(refit_target))
        refit_params = unreal.ConformTargetParams()
        refit_params.conform_target_mesh.target_parts_type = unreal.TargetPartsType.COMBINED
        refit_params.conform_target_mesh.body_vertices = refit_vertices
        refit_params.conform_target_mesh.body_vertex_indices = refit_indices
        refit_params.estimate_body_joints_from_mesh = True
        refit_params.auto_solve = True
        refit_params.body_conform_solve_settings.pipeline_name = 'combined'
        refit_key = unreal.MetaHumanCharacterTargetMeshKey()
        refit_key.combined_mesh = refit_target
        if not metahuman_subsystem.conform_to_target_meshes(character, refit_key, refit_params):
            raise RuntimeError('conform_to_target_meshes failed on the A-pose target')
        metahuman_subsystem.commit_posed_state_as_a_pose(character, refit_key)
        unreal.log('ConvertToMetaHuman.py: body joints re-estimated on the committed A pose')
    except Exception as refit_error:
        unreal.log_warning('ConvertToMetaHuman.py: A-pose joint re-estimate failed (the skeleton keeps '
                           'the Daz-pose hand joints): ' + str(refit_error))

    # 7. Export the head + body skeletal meshes (the renderable two-mesh character,
    #    same structure as real MetaHumans). Engine path: duplicates the edit-session
    #    face/body meshes, applies persistent topology materials, PostEditChange +
    #    finish compilation, and syncs the content browser. Creates
    #    <character>_Head and <character>_Body under package_path.
    geometry_params = unreal.MetaHumanGeometryExportParams()
    geometry_params.project_path = package_path
    geometry_params.head_skeletal_mesh = True
    geometry_params.body_skeletal_mesh = True
    geometry_params.full_body_skeletal_mesh = False
    geometry_params.overwrite_existing_assets = True
    unreal.MetaHumanCharacterExportBlueprintLibrary.export_geometry(character, geometry_params)
    unreal.log('ConvertToMetaHuman.py: exported %s_Head and %s_Body skeletal meshes to %s'
               % (character_asset_name, character_asset_name, package_path))

    # 7.5 Likeness texture bake (stage (b)): bake the Daz diffuse maps onto the
    #     final meshes' UV layout, pose-aligned when possible — the bake target
    #     combines the final mesh's UVs with the step-5.5 posed export's vertex
    #     positions, so the Daz and MetaHuman surfaces sit ~mm apart everywhere
    #     (this is what keeps eye/nostril/lip texels off the Daz socket and
    #     mouth interiors, and hands/armpits/elbows clean). Falls back to the
    #     A-posed meshes at looser distances on topology mismatch. Non-fatal —
    #     the character falls back to the synthesized skin from step 10.
    baked_head_texture = None
    baked_body_texture = None
    posed_head = None
    posed_body = None
    reposed_skin = None
    try:
        final_head = unreal.load_object(name=package_path + '/' + character_asset_name + '_Head', outer=None)
        final_body = unreal.load_object(name=package_path + '/' + character_asset_name + '_Body', outer=None)
        if final_head is None or final_body is None:
            raise RuntimeError('exported head/body meshes not found under ' + package_path)
        posed_head = unreal.load_object(name=bake_temp_path + '/' + character_asset_name + '_Head', outer=None)
        posed_body = unreal.load_object(name=bake_temp_path + '/' + character_asset_name + '_Body', outer=None)
        if posed_head is None or posed_body is None:
            unreal.log_warning('ConvertToMetaHuman.py: posed export missing — baking against the A-posed meshes')

        # One source texture per Daz material index (MultiTexture samples by the
        # source triangle's material ID); non-skin sections are pruned from the
        # source mesh so lash/brow cards and mouth interiors can't pollute the
        # projection, and their array slots are simply never sampled. Wearable
        # sections (clothing/hair from the DTU) are pruned for the same reason:
        # over covered skin the nearest surface is the garment, and its albedo
        # would bake onto the body.
        def _keep_daz_source_slot(slot_name):
            return _is_daz_skin_slot(slot_name) and slot_name not in wearable_exclude_slots

        # Indexed by MATERIAL ID (section), not by slot: MultiTexture samples
        # this array with the source triangle's material id, so a slot-ordered
        # array would hand every section its neighbour's texture on a
        # reimported asset (see _lod_material_slots).
        source_slots = _lod_material_slots(target_mesh)
        daz_textures = [_material_diffuse_texture(material.material_interface)
                        for material in source_slots]
        if not any(texture is not None and _keep_daz_source_slot(str(material.material_slot_name))
                   for material, texture in zip(source_slots, daz_textures)):
            raise RuntimeError('no Daz skin diffuse textures found to bake')
        daz_dynamic_mesh = _normalize_udim_tiles(
            _prune_dynamic_mesh_to_slots(
                _copy_skeletal_mesh_to_dynamic_mesh(target_mesh), target_mesh, _keep_daz_source_slot),
            len(source_slots))

        baked_texture_path = package_path + '/Skin/BakedTextures'
        baked_head_texture = _bake_daz_basecolor(
            daz_dynamic_mesh, daz_textures, final_head,
            lambda slot: slot.startswith('head'),
            baked_texture_path + '/T_' + asset_name + '_Head_BaseColor_Baked',
            BAKE_PROJECTION_DISTANCE_HEAD, BAKE_PROJECTION_DISTANCE_ALIGNED_HEAD, posed_head)
        # BODY bake source: the re-posed Daz skin, against the plain A-posed
        # final body (no posed transplant). The posed export's HAND pose never
        # matches the Daz source (the conform doesn't reproduce it), so the
        # transplant path sits cm off at the fingers/toes and texels grab nail
        # plates and neighbouring fingers — flat nail-cream fingertips/toes
        # (LauraforGenesis9_1, 2026-08-05). The re-pose runs the garment
        # retarget (finger handling verified against glove fingers), so source
        # and target agree everywhere at the tight aligned reach. The same
        # re-posed skin feeds the garment wrap context below.
        try:
            reposed_skin = _build_reposed_skin_dyn(
                target_mesh, wearable_exclude_slots,
                _copy_skeletal_mesh_to_dynamic_mesh(final_body))
        except Exception as repose_error:
            unreal.log_warning('ConvertToMetaHuman.py: skin re-pose failed (%s) — body bake falls back '
                               'to the posed-transplant target' % repose_error)
        if reposed_skin is not None:
            unreal.log('ConvertToMetaHuman.py: body bake source = re-posed Daz skin (A-pose aligned, '
                       'skeleton-fitted)')
            # reposed_skin[2] is the bake-only foot-fitted variant; the wrap
            # keeps reposed_skin[0], which the UDIM shift here never touches.
            baked_body_texture = _bake_daz_basecolor(
                _normalize_udim_tiles(reposed_skin[2], len(source_slots)), daz_textures, final_body,
                lambda slot: slot.startswith('body'),
                baked_texture_path + '/T_' + asset_name + '_Body_BaseColor_Baked',
                BAKE_PROJECTION_DISTANCE_ALIGNED_BODY, BAKE_PROJECTION_DISTANCE_ALIGNED_BODY, None,
                facing_filtered=True)
        else:
            baked_body_texture = _bake_daz_basecolor(
                daz_dynamic_mesh, daz_textures, final_body,
                lambda slot: slot.startswith('body'),
                baked_texture_path + '/T_' + asset_name + '_Body_BaseColor_Baked',
                BAKE_PROJECTION_DISTANCE_BODY, BAKE_PROJECTION_DISTANCE_ALIGNED_BODY, posed_body,
                facing_filtered=True)
        unreal.log('ConvertToMetaHuman.py: likeness bake — head %s, body %s'
                   % (bool(baked_head_texture), bool(baked_body_texture)))
    except Exception as bake_error:
        unreal.log_warning('ConvertToMetaHuman.py: likeness texture bake skipped: ' + str(bake_error))

    # 7.6 Clothing wearables (Spike B phase 1): split each clothing item out of
    #     the merged import into its own skeletal mesh asset under
    #     <package>/Clothing. Preferred path: re-pose the vendor geometry from
    #     its authored Daz pose into the MetaHuman A pose through its OWN Daz
    #     skeleton (direction-aligned retarget of the A pose through the bone
    #     map — each Daz bone snaps to its MetaHuman joint and aims where the
    #     MetaHuman bone aims, so different bone lengths never compound down a
    #     chain and the conform's unreliable helper-joint transforms never
    #     enter), then bind at rest against the A-posed MetaHuman skeleton
    #     with the vendor weights name-swapped. Geometry, bind pose and body
    #     all agree at rest; animation deforms from a correct rest shape.
    #     Fallback: closest-point weight transfer from the posed pre-commit
    #     export (the previous, known-degraded approach). Non-fatal per item.
    clothing_assets = []
    clothing_asset_pairs = []
    if clothing_wearables:
        try:
            reskin_body = unreal.load_object(name=package_path + '/' + character_asset_name + '_Body', outer=None)
            if reskin_body is None:
                raise RuntimeError('exported body mesh not found for the clothing re-skin')
            transfer_source = posed_body
            if transfer_source is None:
                unreal.log_warning('ConvertToMetaHuman.py: posed body export unavailable — clothing binds against '
                                   'the A-posed body and will sit offset by the pose difference')
                transfer_source = reskin_body
            body_weights_mesh = _copy_skeletal_mesh_to_dynamic_mesh(transfer_source)

            # A-posed final body: the preferred re-pose path's bind-bone
            # source (and the probe reference below).
            final_body_dyn = _copy_skeletal_mesh_to_dynamic_mesh(reskin_body)

            # Bind-skeleton source for the transfer FALLBACK: the step-5.6
            # conformed-pose duplicate. Probe a bone against the A-posed final
            # body — a real conformed skeleton must differ; zero delta means
            # it captured the rest pose again.
            bind_bones_mesh = None
            if posed_bind_body is not None:
                bind_bones_mesh = _copy_skeletal_mesh_to_dynamic_mesh(posed_bind_body)
                max_probe_delta = 0.0
                for probe_bone in ('upperarm_l', 'thigh_l', 'hand_l', 'index_01_l'):
                    probe_deltas = []
                    for probe_mesh in (bind_bones_mesh, final_body_dyn):
                        info_result = unreal.GeometryScript_BoneWeights.get_bone_info(probe_mesh, probe_bone)
                        bone_info = next((entry for entry in info_result
                                          if isinstance(entry, unreal.GeometryScriptBoneInfo)), None) \
                            if isinstance(info_result, tuple) else None
                        probe_deltas.append(bone_info.local_transform if bone_info else None)
                    if probe_deltas[0] is not None and probe_deltas[1] is not None:
                        rot_a, rot_b = probe_deltas[0].rotation, probe_deltas[1].rotation
                        dot = abs(rot_a.x * rot_b.x + rot_a.y * rot_b.y + rot_a.z * rot_b.z + rot_a.w * rot_b.w)
                        max_probe_delta = max(max_probe_delta, math.degrees(2.0 * math.acos(min(1.0, dot))))
                if max_probe_delta < 0.1:
                    unreal.log_warning('ConvertToMetaHuman.py: extracted body skeleton matches the rest pose '
                                       '(max probe delta %.3f deg) — conformed bind NOT captured; the transfer '
                                       'fallback (if used) would bind against the rest pose' % max_probe_delta)
                    bind_bones_mesh = None
                else:
                    unreal.log('ConvertToMetaHuman.py: conformed bind skeleton captured '
                               '(max probe delta %.2f deg vs A pose)' % max_probe_delta)

            # Surface-transport wrap context, built once: after their re-pose
            # the garments hug the re-posed Daz skin; the wrap moves them onto
            # the final committed body+head surface BEFORE each asset is
            # created (an in-place rewrite after binding corrupted animation
            # weights — garments tore under animation while the A pose looked
            # perfect, since bind-pose rendering ignores weights entirely).
            wrap_context = None
            try:
                head_for_wrap = unreal.load_object(name=package_path + '/' + character_asset_name + '_Head',
                                                   outer=None)
                wrap_context = _build_clothing_wrap_context(
                    target_mesh, wearable_exclude_slots, reskin_body, head_for_wrap, final_body_dyn,
                    reposed_skin=reposed_skin)
            except Exception as wrap_context_error:
                unreal.log_warning('ConvertToMetaHuman.py: garment wrap unavailable — garments keep their '
                                   're-posed positions (expect the body-delta offset): '
                                   + str(wrap_context_error))

            for item in clothing_wearables:
                item_asset = _split_clothing_item(target_mesh, item, body_weights_mesh, transfer_source,
                                                  package_path + '/Clothing', bind_bones_mesh, final_body_dyn,
                                                  wrap_context)
                if item_asset is not None:
                    clothing_asset_pairs.append((item, item_asset))
            clothing_assets = [pair_asset for _, pair_asset in clothing_asset_pairs]
            unreal.log('ConvertToMetaHuman.py: clothing split — %d of %d item(s) created under %s/Clothing'
                       % (len(clothing_assets), len(clothing_wearables), package_path))
            # The wrap context and dynamic-mesh copies anchor a huge wrapper
            # web — drop them and flush while their targets are still alive.
            del wrap_context, body_weights_mesh, final_body_dyn, bind_bones_mesh
            reposed_skin = None
            _flush_python_wrappers('clothing split')
        except Exception as clothing_error:
            unreal.log_warning('ConvertToMetaHuman.py: clothing split skipped: ' + str(clothing_error))

    # The posed exports (bake geometry donor + clothing transfer source) are no
    # longer needed past this point. Delete the three assets steps 5.5/5.6 put
    # here BY NAME. delete_directory is recursive and took whatever else shared
    # the folder name, and MHBakeTemp sits in the user's own character folder.
    for temp_suffix in ('_Head', '_Body', '_PosedBindBody'):
        _safe_delete_asset(bake_temp_path + '/' + character_asset_name + temp_suffix,
                           'posed export')
    # Drop the folder only once it is genuinely empty, so anything unexpected in
    # there survives and is visible rather than being swept up silently.
    try:
        if unreal.EditorAssetLibrary.does_directory_exist(bake_temp_path):
            leftovers = unreal.EditorAssetLibrary.list_assets(bake_temp_path, recursive=True)
            if not leftovers:
                unreal.EditorAssetLibrary.delete_directory(bake_temp_path)
            else:
                unreal.log_warning('ConvertToMetaHuman.py: leaving %s in place — %d unexpected asset(s) inside'
                                   % (bake_temp_path, len(leftovers)))
    except Exception as temp_cleanup_error:
        unreal.log_warning('ConvertToMetaHuman.py: posed-export cleanup skipped: ' + str(temp_cleanup_error))

    # 8. Combined face+body mesh — single-section outfit-fitting / DCC-export utility
    #    (Epic's export applies a clay material to this; it carries body measurements
    #    as ChaosOutfitAssetBodyUserData). NOT the renderable character. Non-fatal.
    combined_mesh_asset_path = package_path + '/' + asset_name + '_MHCombined'
    combined_mesh = unreal.DazToUnrealBlueprintUtils.create_meta_human_combined_mesh(character, combined_mesh_asset_path)
    if combined_mesh is None:
        unreal.log_warning('ConvertToMetaHuman.py: combined face+body mesh creation failed (see log for details)')
    else:
        unreal.EditorAssetLibrary.save_asset(combined_mesh_asset_path, only_if_is_dirty=True)
        unreal.log('ConvertToMetaHuman.py: combined utility mesh — saved ' + combined_mesh_asset_path)

    # 9. Assemble the placeable character blueprint from the exported meshes.
    #    Duplicates the actor template configured in plugin settings (components
    #    named Face and Body, leader-posed like real MetaHumans) and assigns the
    #    meshes. Non-fatal: the meshes are usable without the blueprint.
    head_mesh_path = package_path + '/' + character_asset_name + '_Head'
    body_mesh_path = package_path + '/' + character_asset_name + '_Body'
    head_mesh = unreal.load_object(name=head_mesh_path, outer=None)
    body_mesh = unreal.load_object(name=body_mesh_path, outer=None)
    if head_mesh is None or body_mesh is None:
        unreal.log_warning('ConvertToMetaHuman.py: exported head/body meshes not found (%s, %s) — skipping blueprint assembly'
                           % (head_mesh_path, body_mesh_path))
    else:
        blueprint_path = package_path + '/BP_' + asset_name + '_MH'
        character_blueprint = unreal.DazToUnrealBlueprintUtils.assemble_meta_human_character_blueprint(
            head_mesh, body_mesh, blueprint_path)
        if character_blueprint is None:
            unreal.log_warning('ConvertToMetaHuman.py: character blueprint assembly failed (see log for details)')
        else:
            unreal.EditorAssetLibrary.save_asset(blueprint_path, only_if_is_dirty=True)
            unreal.log('ConvertToMetaHuman.py: character blueprint — saved ' + blueprint_path)

            # 9.5 Sync the split clothing onto the blueprint: one Clothing_*
            #     skeletal mesh component per asset, child of Body (the
            #     template's construction script leader-poses Body children,
            #     so they follow all body animation with no extra wiring).
            #     Runs even with no clothing so components from a previous
            #     outfit are removed on reconvert. Non-fatal.
            try:
                if unreal.DazToUnrealBlueprintUtils.attach_clothing_to_character_blueprint(
                        character_blueprint, clothing_assets):
                    unreal.EditorAssetLibrary.save_asset(blueprint_path, only_if_is_dirty=True)
                    if clothing_assets:
                        unreal.log('ConvertToMetaHuman.py: attached %d clothing component(s) to %s'
                                   % (len(clothing_assets), blueprint_path))
            except Exception as attach_error:
                unreal.log_warning('ConvertToMetaHuman.py: clothing attachment failed '
                                   '(is the DazToUnreal plugin up to date?): ' + str(attach_error))

        # 10. Skin: estimate tone from the Daz diffuse, synthesize MetaHuman skin
        #     textures locally, persist them, and assign to the exported meshes.
        #     Non-fatal — the character just stays on topology materials.
        skin_export_path = package_path + '/Skin'
        if unreal.DazToUnrealBlueprintUtils.apply_meta_human_skin_materials(
                character, head_mesh, body_mesh, target_mesh, skin_export_path):
            unreal.EditorAssetLibrary.save_asset(head_mesh_path, only_if_is_dirty=True)
            unreal.EditorAssetLibrary.save_asset(body_mesh_path, only_if_is_dirty=True)
            unreal.EditorAssetLibrary.save_directory(skin_export_path, only_if_is_dirty=True)
            unreal.log('ConvertToMetaHuman.py: skin materials applied — textures/MICs under ' + skin_export_path)
        else:
            unreal.log_warning('ConvertToMetaHuman.py: skin material application failed (see log for details)')

        # 11. Likeness: point the persisted skin MICs at the textures baked in
        #     step 7.5. Basecolor only for v1 — normals/cavity stay synthesized
        #     (MetaHuman detail is high quality), and with no face rig yet the
        #     animated wrinkle maps never engage, so leaving them synthesized is
        #     harmless. Both MICs get 'Use Texture Override' = 1: the body
        #     shader branch otherwise ignores the Basecolor texture entirely
        #     (albedo is a parametric bias/gain tone), and the head's chest bib
        #     otherwise renders the stock Color_CHEST overlay + underwear
        #     straps, a visible tone seam against the baked body.
        if baked_head_texture and _apply_baked_basecolor(
                head_mesh, lambda slot: slot.startswith('head'), baked_head_texture, True):
            unreal.EditorAssetLibrary.save_asset(head_mesh_path, only_if_is_dirty=True)
            unreal.log('ConvertToMetaHuman.py: baked head basecolor applied — ' + baked_head_texture.get_path_name())
        if baked_body_texture and _apply_baked_basecolor(
                body_mesh, lambda slot: slot.startswith('body'), baked_body_texture, True):
            unreal.EditorAssetLibrary.save_asset(body_mesh_path, only_if_is_dirty=True)
            unreal.log('ConvertToMetaHuman.py: baked body basecolor applied — ' + baked_body_texture.get_path_name())
        if baked_head_texture or baked_body_texture:
            unreal.EditorAssetLibrary.save_directory(skin_export_path, only_if_is_dirty=True)

        # 12. Poke-through culling preview (non-destructive): bake where opaque
        #     clothing covers the body, turn it into a hide mask, and drive
        #     Epic's HideMask* params on the body MICs — the covered skin stops
        #     rendering but the mesh is untouched, so outfit changes stay
        #     possible. 'Bake MetaHuman Body Culling' on the blueprint context
        #     menu later makes the cookable culled copy from the stored source
        #     data. A reconvert always resets to the unbaked preview state:
        #     stale culled meshes and masks from a previous outfit go first.
        #     Non-fatal.
        try:
            culled_mesh_path = body_mesh_path + '_Culled'
            _safe_delete_asset(culled_mesh_path, 'stale culled body')
            hide_mask_path = package_path + '/Skin/BakedTextures/T_' + asset_name + '_BodyHideMask'

            transparent_labels = []
            coverage_items = []
            for item, clothing_asset in clothing_asset_pairs:
                item_transparent_slots = item.get('transparent_slots') or set()
                if item['slots'] and item['slots'] <= item_transparent_slots:
                    transparent_labels.append(item['label'])
                    continue
                coverage_items.append((clothing_asset,
                                       lambda slot, excluded=item_transparent_slots: slot not in excluded))
            if transparent_labels:
                unreal.log('ConvertToMetaHuman.py: transparent clothing excluded from body culling: '
                           + ', '.join(transparent_labels))

            hide_mask = None
            coverage_texture = _bake_clothing_coverage(body_mesh, coverage_items)
            if coverage_texture is not None:
                hide_mask = _create_hide_mask(coverage_texture, hide_mask_path)
            if hide_mask:
                unreal.DazToUnrealBlueprintUtils.set_body_hide_mask_material_params(
                    body_mesh, hide_mask, HIDE_MASK_MAX_CULL_VALUE, HIDE_MASK_MIN_KEEP_VALUE,
                    HIDE_MASK_MAX_SHRINK_DISTANCE)
                unreal.EditorAssetLibrary.save_asset(hide_mask_path, only_if_is_dirty=True)
                unreal.log('ConvertToMetaHuman.py: body hide mask applied — ' + hide_mask_path)
            else:
                _safe_delete_asset(hide_mask_path, 'unused hide mask')
                unreal.DazToUnrealBlueprintUtils.set_body_hide_mask_material_params(
                    body_mesh, None, 0.0, 0.0, 0.0)

            # The reconvert exported a fresh _Body (previous user data is gone
            # with the old mesh), so always write the source data back — the
            # bake/restore context actions read it.
            unreal.DazToUnrealBlueprintUtils.set_meta_human_body_source_data(
                body_mesh, body_mesh, character_asset_path, hide_mask, transparent_labels,
                HIDE_MASK_MAX_CULL_VALUE, HIDE_MASK_MIN_KEEP_VALUE, HIDE_MASK_MAX_SHRINK_DISTANCE)
            unreal.EditorAssetLibrary.save_asset(body_mesh_path, only_if_is_dirty=True)
            unreal.EditorAssetLibrary.save_directory(skin_export_path, only_if_is_dirty=True)
        except Exception as culling_error:
            unreal.log_warning('ConvertToMetaHuman.py: body culling preview skipped: ' + str(culling_error))

    unreal.EditorAssetLibrary.save_asset(character_asset_path, only_if_is_dirty=True)
    unreal.log('ConvertToMetaHuman.py: conform complete — saved ' + character_asset_path)
    conform_succeeded = True

finally:
    if metahuman_subsystem.is_object_added_for_editing(character):
        metahuman_subsystem.remove_object_to_edit(character)
    if not conform_succeeded and created_character:
        if not _safe_delete_asset(character_asset_path, 'orphaned character'):
            unreal.log_warning('ConvertToMetaHuman.py: failed to delete orphaned asset ' + character_asset_path)

_flush_python_wrappers('conversion')

# Crash insurance: repeated GC crashes have eaten unsaved textures and
# material instances after otherwise-successful converts — flush everything
# the conversion produced or touched.
try:
    unreal.EditorAssetLibrary.save_directory(package_path, only_if_is_dirty=True, recursive=True)
    unreal.log('ConvertToMetaHuman.py: save sweep complete — ' + package_path)
except Exception as save_sweep_error:
    unreal.log_warning('ConvertToMetaHuman.py: save sweep failed: ' + str(save_sweep_error))

unreal.log('ConvertToMetaHuman.py: done.')
