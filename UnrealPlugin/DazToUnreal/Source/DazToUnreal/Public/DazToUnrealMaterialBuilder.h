#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UMaterialExpressionTextureSampleParameter2D;
class UMaterialExpressionScalarParameter;
class UMaterialExpressionVectorParameter;
class UMaterialExpressionMultiply;
class UMaterialExpressionLinearInterpolate;
class UMaterialExpressionClamp;
class UMaterialExpressionConstant;
class UMaterialExpressionTextureCoordinate;
class UMaterialExpressionMaterialFunctionCall;
class UMaterialExpressionAppendVector;
class UMaterialExpressionOneMinus;
class UMaterialExpressionStaticSwitchParameter;
class UMaterialExpressionAdd;
class UMaterialExpressionDotProduct;
class UMaterialExpressionConstant3Vector;
class UMaterialExpressionComponentMask;
class UMaterialExpressionSubtract;
class UMaterialExpressionAbs;

/**
 * Builds DazToUnreal base materials procedurally from C++ so they are always
 * version-native (no .uasset format compatibility issues between UE versions).
 *
 * Materials are written to the folder specified in UDazToUnrealSettings::GeneratedMaterialsFolder
 * (default: /DazToUnreal/Common/) on first editor startup, and regenerated whenever
 * MATERIAL_BUILDER_VERSION is incremented.
 */
class DAZTOUNREAL_API FDazToUnrealMaterialBuilder
{
public:
	/** Increment this to force regeneration of all managed materials on next editor launch. */
	static constexpr int32 MATERIAL_BUILDER_VERSION = 14;

	/** Called once the asset registry has finished its initial file scan. */
	static void BuildOutdatedMaterials();

	/** Force-rebuild all managed materials, ignoring the version stamp. */
	static void ForceRebuildMaterials();

private:
	// Version stamping (stored in UPackage metadata, not in material parameters)
	static bool IsCurrentVersion(UMaterial* Material);
	static void StampVersion(UMaterial* Material);

	// Asset management
	static UMaterial* GetOrCreateMaterial(const FString& PackagePath, const FString& AssetName);
	static void ClearMaterialExpressions(UMaterial* Material);
	static void SaveAndNotify(UMaterial* Material);

	// Per-material builders
	static void BuildBasePBRSkinMaterial(const FString& DestinationFolder, bool bForce = false);
	static void BuildBaseIrayUberSkinMaterial(const FString& DestinationFolder, bool bForce = false);

	// Node creation helpers
	static UMaterialExpressionTextureSampleParameter2D* AddTexParam(
		UMaterial* Material, const FName& Name, const FName& Group, int32 X, int32 Y,
		const TCHAR* DefaultTexturePath = nullptr);

	static UMaterialExpressionScalarParameter* AddScalarParam(
		UMaterial* Material, const FName& Name, const FName& Group, float Default, int32 X, int32 Y);

	static UMaterialExpressionVectorParameter* AddVectorParam(
		UMaterial* Material, const FName& Name, const FName& Group, const FLinearColor& Default, int32 X, int32 Y);

	static UMaterialExpressionMultiply* AddMultiply(UMaterial* Material, int32 X, int32 Y);
	static UMaterialExpressionLinearInterpolate* AddLerp(UMaterial* Material, int32 X, int32 Y);
	static UMaterialExpressionClamp* AddClamp(UMaterial* Material, float MinVal, float MaxVal, int32 X, int32 Y);
	static UMaterialExpressionConstant* AddConstant(UMaterial* Material, float Value, int32 X, int32 Y);
	static UMaterialExpressionTextureCoordinate* AddTexCoord(UMaterial* Material, int32 X, int32 Y);
	static UMaterialExpressionMaterialFunctionCall* AddFunctionCall(
		UMaterial* Material, const TCHAR* FunctionPath, int32 X, int32 Y);
	static UMaterialExpressionAppendVector* AddAppendVector(UMaterial* Material, int32 X, int32 Y);
	static UMaterialExpressionOneMinus* AddOneMinus(UMaterial* Material, int32 X, int32 Y);
	static UMaterialExpressionAdd* AddAdd(UMaterial* Material, int32 X, int32 Y);
	static UMaterialExpressionStaticSwitchParameter* AddStaticSwitch(
		UMaterial* Material, const FName& Name, const FName& Group, bool bDefault, int32 X, int32 Y);
	static UMaterialExpressionDotProduct* AddDotProduct(UMaterial* Material, int32 X, int32 Y);
	static UMaterialExpressionConstant3Vector* AddConstant3Vector(
		UMaterial* Material, const FLinearColor& Value, int32 X, int32 Y);
	static UMaterialExpressionComponentMask* AddComponentMask(
		UMaterial* Material, bool R, bool G, bool B, bool A, int32 X, int32 Y);
	static UMaterialExpressionSubtract* AddSubtract(UMaterial* Material, int32 X, int32 Y);
	static UMaterialExpressionAbs* AddAbs(UMaterial* Material, int32 X, int32 Y);
};
