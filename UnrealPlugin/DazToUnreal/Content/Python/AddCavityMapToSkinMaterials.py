"""
AddCavityMapToSkinMaterials.py

Run this script ONCE in the Unreal Editor Python console to add a procedural
cavity effect to all DazToUnreal skin parent materials.

The cavity is derived from the normal map's Z (blue) channel:
  - Flat surfaces (high Z) → no darkening
  - Curved/creased surfaces (low Z) → darkened

Node chain added to each material:
  NormalMap.B --> ComponentMask(B)
                                  \
  Constant(1.0) --> Lerp.A         Lerp.B    (Lerp A=1, B=NormalB, Alpha=CavityStrength)
  ScalarParam("Cavity Strength", default=0.5) --> Lerp.Alpha
                                  /
  [existing BaseColor expr] --> Multiply.A
  Lerp result              --> Multiply.B
                                  |
                             BaseColor output

Usage:
  In UE editor: Edit > Execute Python Script > select this file
  Or paste into the Output Log Python console and press Enter.
"""

import unreal

SKIN_MATERIALS = [
    "/DazToUnreal/BasePBRSkinMaterial",
    "/DazToUnreal/BaseSSSSkinMaterial",
    "/DazToUnreal/IrayUberSkinMaterial",
    "/DazToUnreal/omUberSkinMaterial",
    "/DazToUnreal/AoASubsurfaceSkinMaterial",
]

CAVITY_STRENGTH_DEFAULT = 0.5
CAVITY_PARAM_NAME = "Cavity Strength"

# Node layout offsets relative to the existing BaseColor expression
LAYOUT = {
    "component_mask": (200, -300),
    "constant_one":   (200, -150),
    "scalar_param":   (200,  -50),
    "lerp":           (450, -200),
    "multiply":       (700, -100),
}


def find_normal_map_expression(expressions):
    """Return the first TextureSampleParameter2D whose parameter name contains 'Normal'."""
    for expr in expressions:
        if isinstance(expr, unreal.MaterialExpressionTextureSampleParameter2D):
            param_name = str(expr.get_editor_property("parameter_name")).lower()
            if "normal" in param_name:
                return expr
    return None


def find_existing_cavity_param(expressions):
    """Return True if a 'Cavity Strength' scalar parameter already exists."""
    for expr in expressions:
        if isinstance(expr, unreal.MaterialExpressionScalarParameter):
            param_name = str(expr.get_editor_property("parameter_name"))
            if param_name == CAVITY_PARAM_NAME:
                return True
    return False


def get_base_color_expression(material):
    """Return the expression node currently feeding the BaseColor input, or None."""
    base_color_input = material.get_editor_property("base_color")
    if base_color_input:
        return base_color_input.get_editor_property("expression")
    return None


def get_expression_position(expr):
    """Return (x, y) of an expression node."""
    return (
        expr.get_editor_property("material_expression_editor_x"),
        expr.get_editor_property("material_expression_editor_y"),
    )


def add_cavity_to_material(asset_path):
    material = unreal.load_asset(asset_path)
    if material is None:
        unreal.log_warning(f"[CavityMap] Could not load: {asset_path}")
        return False

    if not isinstance(material, unreal.Material):
        unreal.log_warning(f"[CavityMap] Not a Material: {asset_path}")
        return False

    expressions = list(material.get_editor_property("expressions"))

    # Skip if already patched
    if find_existing_cavity_param(expressions):
        unreal.log(f"[CavityMap] Already patched, skipping: {asset_path}")
        return True

    # Find the normal map texture parameter
    normal_expr = find_normal_map_expression(expressions)
    if normal_expr is None:
        unreal.log_warning(f"[CavityMap] No normal map expression found in: {asset_path}")
        return False

    # Find what is currently connected to BaseColor
    base_color_expr = get_base_color_expression(material)
    if base_color_expr is None:
        unreal.log_warning(f"[CavityMap] No BaseColor expression connected in: {asset_path}")
        return False

    # Determine layout anchor from the existing BaseColor expression
    anchor_x, anchor_y = get_expression_position(base_color_expr)

    mel = unreal.MaterialEditingLibrary

    # 1. ComponentMask — extract B channel from normal map
    mask = mel.create_material_expression(
        material,
        unreal.MaterialExpressionComponentMask,
        anchor_x + LAYOUT["component_mask"][0],
        anchor_y + LAYOUT["component_mask"][1],
    )
    mask.set_editor_property("r", False)
    mask.set_editor_property("g", False)
    mask.set_editor_property("b", True)
    mask.set_editor_property("a", False)

    # 2. Constant 1.0 — Lerp A input (no darkening at zero cavity)
    const_one = mel.create_material_expression(
        material,
        unreal.MaterialExpressionConstant,
        anchor_x + LAYOUT["constant_one"][0],
        anchor_y + LAYOUT["constant_one"][1],
    )
    const_one.set_editor_property("r", 1.0)

    # 3. ScalarParameter "Cavity Strength" — controls intensity
    scalar_param = mel.create_material_expression(
        material,
        unreal.MaterialExpressionScalarParameter,
        anchor_x + LAYOUT["scalar_param"][0],
        anchor_y + LAYOUT["scalar_param"][1],
    )
    scalar_param.set_editor_property("parameter_name", CAVITY_PARAM_NAME)
    scalar_param.set_editor_property("default_value", CAVITY_STRENGTH_DEFAULT)

    # 4. LinearInterpolate — Lerp(1.0, NormalB, CavityStrength)
    lerp = mel.create_material_expression(
        material,
        unreal.MaterialExpressionLinearInterpolate,
        anchor_x + LAYOUT["lerp"][0],
        anchor_y + LAYOUT["lerp"][1],
    )

    # 5. Multiply — BaseColor * LerpResult
    multiply = mel.create_material_expression(
        material,
        unreal.MaterialExpressionMultiply,
        anchor_x + LAYOUT["multiply"][0],
        anchor_y + LAYOUT["multiply"][1],
    )

    # Wire: NormalMap --> ComponentMask
    mel.connect_material_expressions(normal_expr, "RGB", mask, "")

    # Wire: Constant1 --> Lerp.A
    mel.connect_material_expressions(const_one, "", lerp, "A")

    # Wire: ComponentMask(B) --> Lerp.B
    mel.connect_material_expressions(mask, "", lerp, "B")

    # Wire: ScalarParam --> Lerp.Alpha
    mel.connect_material_expressions(scalar_param, "", lerp, "Alpha")

    # Wire: existing BaseColor expr --> Multiply.A
    mel.connect_material_expressions(base_color_expr, "", multiply, "A")

    # Wire: Lerp --> Multiply.B
    mel.connect_material_expressions(lerp, "", multiply, "B")

    # Wire: Multiply --> BaseColor output
    mel.connect_material_property(multiply, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # Recompile and save
    mel.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False)

    unreal.log(f"[CavityMap] Patched successfully: {asset_path}")
    return True


def main():
    unreal.log("[CavityMap] Starting cavity map addition to DazToUnreal skin materials...")
    success_count = 0
    for path in SKIN_MATERIALS:
        if add_cavity_to_material(path):
            success_count += 1
    unreal.log(f"[CavityMap] Done. {success_count}/{len(SKIN_MATERIALS)} materials patched.")


main()
