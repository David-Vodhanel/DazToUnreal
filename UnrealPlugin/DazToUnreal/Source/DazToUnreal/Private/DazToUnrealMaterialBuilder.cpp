#include "DazToUnrealMaterialBuilder.h"
#include "DazToUnrealMaterials.h"
#include "DazToUnrealSettings.h"

// Material editing API (requires MaterialEditor module in Build.cs)
#include "MaterialEditingLibrary.h"

// Material types
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionClamp.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialFunction.h"
#include "Engine/Texture.h"

// Asset creation
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/MaterialFactoryNew.h"

// Package / asset infrastructure
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/MetaData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"

// Console command
#include "HAL/IConsoleManager.h"


static const TCHAR* BUILDER_VERSION_KEY = TEXT("DazMaterialBuilderVersion");

// Console command: Daz.RebuildBaseMaterials
static FAutoConsoleCommand GDazRebuildBaseMaterialsCmd(
	TEXT("Daz.RebuildBaseMaterials"),
	TEXT("Force-rebuild all DazToUnreal generated base materials (ignores version stamp)."),
	FConsoleCommandDelegate::CreateStatic(&FDazToUnrealMaterialBuilder::ForceRebuildMaterials)
);

// Default engine textures for parameters that may not be set by every character
static const TCHAR* TEX_WHITE       = TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture");
static const TCHAR* TEX_FLAT_NORMAL = TEXT("/Engine/EngineMaterials/FlatNormal.FlatNormal");

// Engine material functions
static const TCHAR* FUNC_BLEND_NORMALS  = TEXT("/Engine/Functions/Engine_MaterialFunctions02/Utility/BlendAngleCorrectedNormals.BlendAngleCorrectedNormals");
static const TCHAR* FUNC_FLATTEN_NORMAL = TEXT("/Engine/Functions/Engine_MaterialFunctions01/Texturing/FlattenNormal.FlattenNormal");

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

void FDazToUnrealMaterialBuilder::BuildOutdatedMaterials()
{
	const UDazToUnrealSettings* Settings = GetDefault<UDazToUnrealSettings>();
	if (!Settings->bUseGeneratedBaseMaterials)
	{
		return;
	}

	const FString DestFolder = Settings->GeneratedMaterialsFolder.Path.IsEmpty()
		? FString(TEXT("/Game/DazToUnreal/Common"))
		: Settings->GeneratedMaterialsFolder.Path;

	BuildBasePBRSkinMaterial(DestFolder);
}

void FDazToUnrealMaterialBuilder::ForceRebuildMaterials()
{
	UE_LOG(LogDazToUnrealMaterial, Display, TEXT("[DazMaterialBuilder] Force-rebuilding all base materials..."));

	const UDazToUnrealSettings* Settings = GetDefault<UDazToUnrealSettings>();
	const FString DestFolder = Settings->GeneratedMaterialsFolder.Path.IsEmpty()
		? FString(TEXT("/Game/DazToUnreal/Common"))
		: Settings->GeneratedMaterialsFolder.Path;

	BuildBasePBRSkinMaterial(DestFolder, /*bForce=*/ true);

	UE_LOG(LogDazToUnrealMaterial, Display, TEXT("[DazMaterialBuilder] Force-rebuild complete."));
}

// ---------------------------------------------------------------------------
// Version stamping (editor-only UPackage metadata)
// ---------------------------------------------------------------------------

bool FDazToUnrealMaterialBuilder::IsCurrentVersion(UMaterial* Material)
{
	if (!IsValid(Material)) return false;
	UPackage* Package = Material->GetPackage();
	if (!Package) return false;
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 6
	FMetaData& Meta = Package->GetMetaData();
	const FString Stored = Meta.GetValue(Material, BUILDER_VERSION_KEY);
#else
	UMetaData* Meta = Package->GetMetaData();
	if (!Meta) return false;
	const FString Stored = Meta->GetValue(Material, BUILDER_VERSION_KEY);
#endif
	return !Stored.IsEmpty() && FCString::Atoi(*Stored) >= MATERIAL_BUILDER_VERSION;
}

void FDazToUnrealMaterialBuilder::StampVersion(UMaterial* Material)
{
	if (!IsValid(Material)) return;
	UPackage* Package = Material->GetPackage();
	if (!Package) return;
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 6
	FMetaData& Meta = Package->GetMetaData();
	Meta.SetValue(Material, BUILDER_VERSION_KEY, *FString::FromInt(MATERIAL_BUILDER_VERSION));
#else
	UMetaData* Meta = Package->GetMetaData();
	if (!Meta) return;
	Meta->SetValue(Material, BUILDER_VERSION_KEY, *FString::FromInt(MATERIAL_BUILDER_VERSION));
#endif
}

// ---------------------------------------------------------------------------
// Asset management
// ---------------------------------------------------------------------------

UMaterial* FDazToUnrealMaterialBuilder::GetOrCreateMaterial(
	const FString& PackagePath, const FString& AssetName)
{
	const FString FullObjectPath = PackagePath / AssetName + TEXT(".") + AssetName;

	// Try to load an existing asset first
	UMaterial* Existing = LoadObject<UMaterial>(nullptr, *FullObjectPath);
	if (Existing)
	{
		return Existing;
	}

	// Not found – create a brand-new material asset via AssetTools
	FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");
	UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
	return Cast<UMaterial>(
		AssetToolsModule.Get().CreateAsset(AssetName, PackagePath, UMaterial::StaticClass(), Factory));
}

void FDazToUnrealMaterialBuilder::ClearMaterialExpressions(UMaterial* Material)
{
	if (!IsValid(Material)) return;
	UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);
}

void FDazToUnrealMaterialBuilder::SaveAndNotify(UMaterial* Material)
{
	if (!IsValid(Material)) return;

	Material->MarkPackageDirty();

	UPackage* Package = Material->GetPackage();
	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		Package->GetName(), FPackageName::GetAssetPackageExtension());

#if ENGINE_MAJOR_VERSION >= 5
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
#else
	UPackage::SavePackage(Package, nullptr, RF_Public | RF_Standalone, *PackageFileName);
#endif

	// Notify the asset registry so the new asset appears in the Content Browser
	FAssetRegistryModule& Registry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	Registry.Get().AssetCreated(Material);
}

// ---------------------------------------------------------------------------
// Node creation helpers
// ---------------------------------------------------------------------------

UMaterialExpressionTextureSampleParameter2D* FDazToUnrealMaterialBuilder::AddTexParam(
	UMaterial* Material, const FName& Name, const FName& Group, int32 X, int32 Y,
	const TCHAR* DefaultTexturePath)
{
	auto* Expr = Cast<UMaterialExpressionTextureSampleParameter2D>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureSampleParameter2D::StaticClass(), X, Y));
	if (Expr)
	{
		Expr->ParameterName = Name;
		Expr->Group = Group;
		if (DefaultTexturePath)
		{
			UTexture* DefaultTex = LoadObject<UTexture>(nullptr, DefaultTexturePath);
			if (DefaultTex)
			{
				Expr->Texture = DefaultTex;
			}
		}
	}
	return Expr;
}

UMaterialExpressionScalarParameter* FDazToUnrealMaterialBuilder::AddScalarParam(
	UMaterial* Material, const FName& Name, const FName& Group, float Default, int32 X, int32 Y)
{
	auto* Expr = Cast<UMaterialExpressionScalarParameter>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionScalarParameter::StaticClass(), X, Y));
	if (Expr)
	{
		Expr->ParameterName = Name;
		Expr->Group = Group;
		Expr->DefaultValue = Default;
	}
	return Expr;
}

UMaterialExpressionVectorParameter* FDazToUnrealMaterialBuilder::AddVectorParam(
	UMaterial* Material, const FName& Name, const FName& Group, const FLinearColor& Default, int32 X, int32 Y)
{
	auto* Expr = Cast<UMaterialExpressionVectorParameter>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionVectorParameter::StaticClass(), X, Y));
	if (Expr)
	{
		Expr->ParameterName = Name;
		Expr->Group = Group;
		Expr->DefaultValue = Default;
	}
	return Expr;
}

UMaterialExpressionMultiply* FDazToUnrealMaterialBuilder::AddMultiply(UMaterial* Material, int32 X, int32 Y)
{
	return Cast<UMaterialExpressionMultiply>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionMultiply::StaticClass(), X, Y));
}

UMaterialExpressionLinearInterpolate* FDazToUnrealMaterialBuilder::AddLerp(UMaterial* Material, int32 X, int32 Y)
{
	return Cast<UMaterialExpressionLinearInterpolate>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionLinearInterpolate::StaticClass(), X, Y));
}

UMaterialExpressionClamp* FDazToUnrealMaterialBuilder::AddClamp(
	UMaterial* Material, float MinVal, float MaxVal, int32 X, int32 Y)
{
	auto* Expr = Cast<UMaterialExpressionClamp>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionClamp::StaticClass(), X, Y));
	if (Expr)
	{
		Expr->ClampMode = CMODE_Clamp;
		Expr->MinDefault = MinVal;
		Expr->MaxDefault = MaxVal;
	}
	return Expr;
}

UMaterialExpressionConstant* FDazToUnrealMaterialBuilder::AddConstant(UMaterial* Material, float Value, int32 X, int32 Y)
{
	auto* Expr = Cast<UMaterialExpressionConstant>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant::StaticClass(), X, Y));
	if (Expr)
	{
		Expr->R = Value;
	}
	return Expr;
}

UMaterialExpressionTextureCoordinate* FDazToUnrealMaterialBuilder::AddTexCoord(UMaterial* Material, int32 X, int32 Y)
{
	return Cast<UMaterialExpressionTextureCoordinate>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureCoordinate::StaticClass(), X, Y));
}

UMaterialExpressionMaterialFunctionCall* FDazToUnrealMaterialBuilder::AddFunctionCall(
	UMaterial* Material, const TCHAR* FunctionPath, int32 X, int32 Y)
{
	auto* Expr = Cast<UMaterialExpressionMaterialFunctionCall>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionMaterialFunctionCall::StaticClass(), X, Y));
	if (Expr)
	{
		UMaterialFunction* Func = LoadObject<UMaterialFunction>(nullptr, FunctionPath);
		if (Func)
		{
			Expr->SetMaterialFunction(Func);
		}
		else
		{
			UE_LOG(LogDazToUnrealMaterial, Warning,
				TEXT("[DazMaterialBuilder] Failed to load material function: %s"), FunctionPath);
		}
	}
	return Expr;
}

UMaterialExpressionAppendVector* FDazToUnrealMaterialBuilder::AddAppendVector(UMaterial* Material, int32 X, int32 Y)
{
	return Cast<UMaterialExpressionAppendVector>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionAppendVector::StaticClass(), X, Y));
}

UMaterialExpressionOneMinus* FDazToUnrealMaterialBuilder::AddOneMinus(UMaterial* Material, int32 X, int32 Y)
{
	return Cast<UMaterialExpressionOneMinus>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionOneMinus::StaticClass(), X, Y));
}

UMaterialExpressionStaticSwitchParameter* FDazToUnrealMaterialBuilder::AddStaticSwitch(
	UMaterial* Material, const FName& Name, const FName& Group, bool bDefault, int32 X, int32 Y)
{
	auto* Expr = Cast<UMaterialExpressionStaticSwitchParameter>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionStaticSwitchParameter::StaticClass(), X, Y));
	if (Expr)
	{
		Expr->ParameterName = Name;
		Expr->Group = Group;
		Expr->DefaultValue = bDefault;
	}
	return Expr;
}

// ---------------------------------------------------------------------------
// BasePBRSkinMaterial builder
//
// Graph layout (X grows rightward toward the material result node):
//
//   Col 0  x=-2600   far-left params (tiling, unused)
//   Col A  x=-2200   secondary params / first ops
//   Col B  x=-1800   texture / vector parameters
//   Col C  x=-1400   scalar params / function calls
//   Col D  x=-1000   operations (FlattenNormal, multiply)
//   Col E  x=-600    second operation tier
//   Col F  x=-200    blend / final node before output
//   Col G  x= 200    static switch → material output
//
// Row layout (Y):
//   BaseColor  y = -800 … -200
//   Normal     y =  100 … 1100
//   Roughness  y = 1200 … 1500  (note: shifted down for detail normal)
//   Metallic   y = 1600 … 1700
//   Specular   y = 1800 … 2100
//   Opacity    y = 2200 … 2600
//   Unused     y = 2700 … 4000
// ---------------------------------------------------------------------------

void FDazToUnrealMaterialBuilder::BuildBasePBRSkinMaterial(const FString& DestinationFolder, bool bForce)
{
	const FString AssetName = TEXT("BasePBRSkinMaterial");

	UMaterial* Material = GetOrCreateMaterial(DestinationFolder, AssetName);
	if (!Material)
	{
		UE_LOG(LogDazToUnrealMaterial, Error,
			TEXT("[DazMaterialBuilder] Failed to get/create %s/%s"),
			*DestinationFolder, *AssetName);
		return;
	}

	if (!bForce && IsCurrentVersion(Material))
	{
		UE_LOG(LogDazToUnrealMaterial, Log,
			TEXT("[DazMaterialBuilder] %s/%s is already v%d, skipping"),
			*DestinationFolder, *AssetName, MATERIAL_BUILDER_VERSION);
		return;
	}

	// Remove any existing expression nodes (handles the "outdated" rebuild case)
	ClearMaterialExpressions(Material);

	// ---- Material-level settings ----
	// ShadingModel is private in UE5 – must use the public setter
	Material->SetShadingModel(MSM_SubsurfaceProfile);
	Material->BlendMode = BLEND_Opaque;

	// ---- Parameter groups (shown in Material Instance editor) ----
	const FName GrpDiffuse  (TEXT("01 - Diffuse"));
	const FName GrpNormal   (TEXT("02 - Normal"));
	const FName GrpRoughness(TEXT("03 - Roughness"));
	const FName GrpSpecular (TEXT("04 - Specular"));
	const FName GrpSSS      (TEXT("05 - Subsurface"));
	const FName GrpCavity   (TEXT("06 - Cavity"));

	// =========================================================
	//  BASE COLOR
	//  DiffuseTex × DiffuseVec  × Lerp(1, NormalMap.B, CavityStrength)
	// =========================================================
	auto* DiffuseTex     = AddTexParam   (Material, TEXT("Diffuse Color Texture"),   GrpDiffuse,  -1800, -800, TEX_WHITE);
	auto* DiffuseVec     = AddVectorParam(Material, TEXT("Diffuse Color"),   GrpDiffuse,
	                                       FLinearColor::White,              -1800, -600);
	auto* CavityStrength = AddScalarParam(Material, TEXT("Cavity Strength"), GrpCavity,
	                                       0.5f,                             -1400, -350);
	auto* ConstOne       = AddConstant   (Material, 1.0f,                    -1400, -200);

	// DiffuseTex.RGB × DiffuseVec.RGB
	auto* MulDiffuse = AddMultiply(Material, -1000, -700);
	UMaterialEditingLibrary::ConnectMaterialExpressions(DiffuseTex, TEXT("RGB"), MulDiffuse, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(DiffuseVec, TEXT("RGB"), MulDiffuse, TEXT("B"));

	// NormalMap texture — shared by BaseColor (cavity) and Normal output
	auto* NormalTex = AddTexParam(Material, TEXT("Normal Map Texture"), GrpNormal, -1800, 200, TEX_FLAT_NORMAL);
	if (NormalTex) NormalTex->SamplerType = SAMPLERTYPE_Normal;

	// Lerp(1.0, NormalMap.B, CavityStrength)  →  cavity darkening multiplier
	auto* LerpCavity = AddLerp(Material, -1000, -300);
	UMaterialEditingLibrary::ConnectMaterialExpressions(ConstOne,      TEXT(""),       LerpCavity, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(NormalTex,     TEXT("B"),      LerpCavity, TEXT("B"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(CavityStrength, TEXT(""),      LerpCavity, TEXT("Alpha"));

	// MulDiffuse × LerpCavity → BaseColor
	auto* MulBaseColor = AddMultiply(Material, -600, -500);
	UMaterialEditingLibrary::ConnectMaterialExpressions(MulDiffuse,  TEXT(""), MulBaseColor, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(LerpCavity,  TEXT(""), MulBaseColor, TEXT("B"));
	UMaterialEditingLibrary::ConnectMaterialProperty(MulBaseColor, TEXT(""), MP_BaseColor);

	// =========================================================
	//  NORMAL  (matches Daz PBRSkinParameters graph)
	//
	//  Base normal path:
	//    NormalMapTex → FlattenNormal(Normal, 1-NormalMapStrength)
	//
	//  Detail normal path:
	//    TexCoord × AppendVector(HTiles, VTiles) → DetailNormalTex UVs
	//    DetailNormalTex → FlattenNormal(Normal, 1-DetailNormalMap)
	//    → FlattenNormal(stage1, 1-(DetailWeightTex*DetailWeight))
	//
	//  Blend:
	//    BlendAngleCorrectedNormals(processedBase, processedDetail)
	//
	//  StaticSwitch "Detail Enable" (default off):
	//    True = blended, False = base only → MP_Normal
	// =========================================================

	// Unused param kept for DazToUnreal property binding
	AddScalarParam(Material, TEXT("Bump Strength"), GrpNormal, 1.0f, -2600, 100);

	// -- Base normal strength via FlattenNormal --
	auto* NormalMapStrength = AddScalarParam(Material, TEXT("Normal Map"),
		GrpNormal, 1.0f, -2200, 200);
	auto* OneMinusNMS = AddOneMinus(Material, -1800, 250);
	UMaterialEditingLibrary::ConnectMaterialExpressions(NormalMapStrength, TEXT(""), OneMinusNMS, TEXT(""));

	auto* FlattenBase = AddFunctionCall(Material, FUNC_FLATTEN_NORMAL, -1400, 200);
	if (FlattenBase && FlattenBase->GetInput(0) && FlattenBase->GetInput(1))
	{
		FlattenBase->GetInput(0)->Connect(0, NormalTex);   // Normal (V3)
		FlattenBase->GetInput(1)->Connect(0, OneMinusNMS); // Flatness (S)
	}

	// -- Detail UV tiling: TexCoord × AppendVector(HTiles, VTiles) --
	auto* DetailHTiles = AddScalarParam(Material, TEXT("Detail Horizontal Tiles"),
		GrpNormal, 0.5f, -2600, 500);
	auto* DetailVTiles = AddScalarParam(Material, TEXT("Detail Vertical Tiles"),
		GrpNormal, 0.5f, -2600, 600);
	auto* AppendTiles = AddAppendVector(Material, -2200, 550);
	UMaterialEditingLibrary::ConnectMaterialExpressions(DetailHTiles, TEXT(""), AppendTiles, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(DetailVTiles, TEXT(""), AppendTiles, TEXT("B"));

	auto* TexCoord = AddTexCoord(Material, -2600, 700);
	auto* MulUV = AddMultiply(Material, -2200, 700);
	UMaterialEditingLibrary::ConnectMaterialExpressions(TexCoord,     TEXT(""), MulUV, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(AppendTiles,  TEXT(""), MulUV, TEXT("B"));

	// -- Detail normal texture sampled with tiled UVs --
	auto* DetailNormalTex = AddTexParam(Material, TEXT("Detail Normal Map Texture"),
		GrpNormal, -1800, 550, TEX_FLAT_NORMAL);
	if (DetailNormalTex) DetailNormalTex->SamplerType = SAMPLERTYPE_Normal;
	UMaterialEditingLibrary::ConnectMaterialExpressions(MulUV, TEXT(""), DetailNormalTex, TEXT("UVs"));

	// -- Detail strength: FlattenNormal(DetailTex, 1 - DetailNormalMap) --
	auto* DetailNormalMapStr = AddScalarParam(Material, TEXT("Detail Normal Map"),
		GrpNormal, 0.5f, -1800, 750);
	auto* OneMinusDNM = AddOneMinus(Material, -1400, 750);
	UMaterialEditingLibrary::ConnectMaterialExpressions(DetailNormalMapStr, TEXT(""), OneMinusDNM, TEXT(""));

	auto* FlattenDetail1 = AddFunctionCall(Material, FUNC_FLATTEN_NORMAL, -1000, 600);
	if (FlattenDetail1 && FlattenDetail1->GetInput(0) && FlattenDetail1->GetInput(1))
	{
		FlattenDetail1->GetInput(0)->Connect(0, DetailNormalTex); // Normal (V3)
		FlattenDetail1->GetInput(1)->Connect(0, OneMinusDNM);    // Flatness (S)
	}

	// -- Detail weight: FlattenNormal(stage1, 1 - (WeightTex × Weight)) --
	auto* DetailWeightTex = AddTexParam(Material, TEXT("Detail Weight Texture"),
		GrpNormal, -1800, 900, TEX_WHITE);
	auto* DetailWeight = AddScalarParam(Material, TEXT("Detail Weight"),
		GrpNormal, 1.0f, -1800, 1050);

	auto* MulWeight = AddMultiply(Material, -1400, 950);
	UMaterialEditingLibrary::ConnectMaterialExpressions(DetailWeightTex, TEXT("RGB"), MulWeight, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(DetailWeight,    TEXT(""),    MulWeight, TEXT("B"));

	auto* OneMinusWeight = AddOneMinus(Material, -1000, 950);
	UMaterialEditingLibrary::ConnectMaterialExpressions(MulWeight, TEXT(""), OneMinusWeight, TEXT(""));

	auto* FlattenDetail2 = AddFunctionCall(Material, FUNC_FLATTEN_NORMAL, -600, 750);
	if (FlattenDetail2 && FlattenDetail2->GetInput(0) && FlattenDetail2->GetInput(1))
	{
		FlattenDetail2->GetInput(0)->Connect(0, FlattenDetail1); // stage1 result
		FlattenDetail2->GetInput(1)->Connect(0, OneMinusWeight); // weight flatness
	}

	// -- BlendAngleCorrectedNormals(processedBase, processedDetail) --
	auto* BlendNormals = AddFunctionCall(Material, FUNC_BLEND_NORMALS, -200, 500);
	if (BlendNormals && BlendNormals->GetInput(0) && BlendNormals->GetInput(1))
	{
		BlendNormals->GetInput(0)->Connect(0, FlattenBase);    // BaseNormal
		BlendNormals->GetInput(1)->Connect(0, FlattenDetail2); // AdditionalNormal
	}

	// -- StaticSwitch "Detail Enable": True=blend, False=base only --
	auto* DetailSwitch = AddStaticSwitch(Material, TEXT("Detail Enable"),
		GrpNormal, false, 200, 500);
	UMaterialEditingLibrary::ConnectMaterialExpressions(BlendNormals, TEXT(""), DetailSwitch, TEXT("True"));
	if (FlattenBase)
	{
		UMaterialEditingLibrary::ConnectMaterialExpressions(FlattenBase, TEXT(""), DetailSwitch, TEXT("False"));
	}
	UMaterialEditingLibrary::ConnectMaterialProperty(DetailSwitch, TEXT(""), MP_Normal);

	// =========================================================
	//  ROUGHNESS
	//  Clamp(RoughnessTex.R × RoughnessScalar, 0.35, 0.75)
	//  Daz roughness values often exceed 0.8 (too dry/matte). Clamping bakes
	//  the quality fix directly into the parent material graph.
	// =========================================================
	auto* RoughnessTex  = AddTexParam   (Material, TEXT("Specular Lobe 1 Roughness Texture"), GrpRoughness,
	                                      -1800, 1200, TEX_WHITE);
	auto* RoughnessMult = AddScalarParam(Material, TEXT("Specular Lobe 1 Roughness"), GrpRoughness,
	                                      1.0f, -1400, 1350);

	auto* MulRoughness = AddMultiply(Material, -1000, 1275);
	UMaterialEditingLibrary::ConnectMaterialExpressions(RoughnessTex,  TEXT("R"), MulRoughness, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(RoughnessMult, TEXT(""),  MulRoughness, TEXT("B"));

	auto* ClampRoughness = AddClamp(Material, 0.35f, 0.75f, -600, 1275);
	UMaterialEditingLibrary::ConnectMaterialExpressions(MulRoughness, TEXT(""), ClampRoughness, TEXT(""));
	UMaterialEditingLibrary::ConnectMaterialProperty(ClampRoughness, TEXT(""), MP_Roughness);

	// =========================================================
	//  METALLIC  (PBRSkin is always non-metallic; exposed for completeness)
	// =========================================================
	auto* MetallicParam = AddScalarParam(Material, TEXT("Metallic Weight"), GrpSpecular,
	                                      0.0f, -800, 1550);
	UMaterialEditingLibrary::ConnectMaterialProperty(MetallicParam, TEXT(""), MP_Metallic);

	// =========================================================
	//  SPECULAR
	//  DualLobeSpecularWeight × DualLobeSpecularReflectivity
	// =========================================================
	auto* DualLobeWeight  = AddScalarParam(Material, TEXT("Dual Lobe Specular Weight"),
	                                        GrpSpecular, 1.0f,  -1400, 1750);
	auto* DualLobeReflect = AddScalarParam(Material, TEXT("Dual Lobe Specular Reflectivity"),
	                                        GrpSpecular, 0.25f, -1400, 1900);

	auto* MulSpecular = AddMultiply(Material, -1000, 1825);
	UMaterialEditingLibrary::ConnectMaterialExpressions(DualLobeWeight,  TEXT(""), MulSpecular, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(DualLobeReflect, TEXT(""), MulSpecular, TEXT("B"));
	UMaterialEditingLibrary::ConnectMaterialProperty(MulSpecular, TEXT(""), MP_Specular);

	// =========================================================
	//  OPACITY  (SubsurfaceProfile mode uses Opacity as the SSS weight)
	//  SubsurfaceAlphaTex.R × DiffuseSubsurfaceColorWeight × TranslucencyWeight
	// =========================================================
	auto* SubsurfAlphaTex = AddTexParam   (Material, TEXT("Subsurface Alpha Texture"),
	                                        GrpSSS, -1800, 2150, TEX_WHITE);
	auto* SSColorWeight   = AddScalarParam(Material, TEXT("Diffuse Subsurface Color Weight"),
	                                        GrpSSS, 0.5f,  -1400, 2300);
	auto* TransWeight     = AddScalarParam(Material, TEXT("Translucency Weight"),
	                                        GrpSSS, 0.85f, -1400, 2450);

	auto* MulOp1 = AddMultiply(Material, -1000, 2225);
	UMaterialEditingLibrary::ConnectMaterialExpressions(SubsurfAlphaTex, TEXT("R"), MulOp1, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(SSColorWeight,   TEXT(""),  MulOp1, TEXT("B"));

	auto* MulOp2 = AddMultiply(Material, -600, 2350);
	UMaterialEditingLibrary::ConnectMaterialExpressions(MulOp1,      TEXT(""), MulOp2, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(TransWeight, TEXT(""), MulOp2, TEXT("B"));
	UMaterialEditingLibrary::ConnectMaterialProperty(MulOp2, TEXT(""), MP_Opacity);

	// =========================================================
	//  UNUSED-BUT-EXPOSED PARAMETERS
	//  These are not wired to outputs in the parent material but must exist
	//  so that DazToUnrealMaterials.cpp can set them on material instances
	//  via SetScalarParameterValueEditorOnly / SetVectorParameterValueEditorOnly.
	// =========================================================

	// SSS colors (read by CreateSubsurfaceProfileForMaterial; stored on USubsurfaceProfile)
	AddVectorParam(Material, TEXT("SSS Color"),
		GrpSSS, FLinearColor(0.976f, 0.694f, 0.761f, 1.f), -400, 2700);
	AddVectorParam(Material, TEXT("Transmitted Color"),
		GrpSSS, FLinearColor(0.976f, 0.482f, 0.353f, 1.f), -400, 2850);
	AddVectorParam(Material, TEXT("Translucency Color"),
		GrpSSS, FLinearColor::White,                        -400, 3000);

	// Dual-lobe specular extras
	AddScalarParam(Material, TEXT("Dual Lobe Specular Roughness Mult"), GrpSpecular, 1.0f,  -400, 3150);
	AddScalarParam(Material, TEXT("Specular Lobe 2 Roughness Mult"),    GrpSpecular, 0.55f, -400, 3300);
	AddScalarParam(Material, TEXT("Dual Lobe Specular Ratio"),          GrpSpecular, 0.15f, -400, 3450);

	// Diffuse extras
	AddScalarParam(Material, TEXT("Diffuse Roughness"), GrpDiffuse, 0.0f, -400, 3600);

	// Texture variants of color params (DazToUnreal sets these as textures on instances)
	AddTexParam(Material, TEXT("Translucency Color Texture"), GrpSSS, -400, 3750, TEX_WHITE);

	// ---- Stamp version, compile, save ----
	StampVersion(Material);
	UMaterialEditingLibrary::RecompileMaterial(Material);
	SaveAndNotify(Material);

	UE_LOG(LogDazToUnrealMaterial, Log,
		TEXT("[DazMaterialBuilder] Built %s/%s (v%d)"),
		*DestinationFolder, *AssetName, MATERIAL_BUILDER_VERSION);
}
