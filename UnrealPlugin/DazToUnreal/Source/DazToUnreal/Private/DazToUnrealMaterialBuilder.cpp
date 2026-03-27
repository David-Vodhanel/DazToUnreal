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
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
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
	BuildBaseIrayUberSkinMaterial(DestFolder);
}

void FDazToUnrealMaterialBuilder::ForceRebuildMaterials()
{
	UE_LOG(LogDazToUnrealMaterial, Display, TEXT("[DazMaterialBuilder] Force-rebuilding all base materials..."));

	const UDazToUnrealSettings* Settings = GetDefault<UDazToUnrealSettings>();
	const FString DestFolder = Settings->GeneratedMaterialsFolder.Path.IsEmpty()
		? FString(TEXT("/Game/DazToUnreal/Common"))
		: Settings->GeneratedMaterialsFolder.Path;

	BuildBasePBRSkinMaterial(DestFolder, /*bForce=*/ true);
	BuildBaseIrayUberSkinMaterial(DestFolder, /*bForce=*/ true);

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

UMaterialExpressionAdd* FDazToUnrealMaterialBuilder::AddAdd(UMaterial* Material, int32 X, int32 Y)
{
	return Cast<UMaterialExpressionAdd>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionAdd::StaticClass(), X, Y));
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

UMaterialExpressionDotProduct* FDazToUnrealMaterialBuilder::AddDotProduct(UMaterial* Material, int32 X, int32 Y)
{
	return Cast<UMaterialExpressionDotProduct>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionDotProduct::StaticClass(), X, Y));
}

UMaterialExpressionConstant3Vector* FDazToUnrealMaterialBuilder::AddConstant3Vector(
	UMaterial* Material, const FLinearColor& Value, int32 X, int32 Y)
{
	auto* Expr = Cast<UMaterialExpressionConstant3Vector>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant3Vector::StaticClass(), X, Y));
	if (Expr)
	{
		Expr->Constant = Value;
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
//   Col G  x= 200    static switch tier 1
//   Col H  x= 600    static switch tier 2 / final → material output
//
// Row layout (Y):
//   Makeup     y = -1200 … -950  (params for Makeup BaseColor overlay)
//   BaseColor  y = -800 … -200
//   Normal     y =  100 … 1100
//   Roughness  y = 1200 … 1550
//   Metallic   y = 1600 … 1700
//   Specular   y = 1800 … 2150
//   Opacity    y = 2200 … 2600
//   Unused     y = 2700 … 4200
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
	const FName GrpMakeup   (TEXT("07 - Makeup"));
	const FName GrpTopCoat  (TEXT("08 - Top Coat"));
	const FName GrpSpecOcc  (TEXT("09 - Specular Occlusion"));

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

	// MulDiffuse × LerpCavity → base color (before Makeup)
	auto* MulBaseColor = AddMultiply(Material, -600, -500);
	UMaterialEditingLibrary::ConnectMaterialExpressions(MulDiffuse,  TEXT(""), MulBaseColor, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(LerpCavity,  TEXT(""), MulBaseColor, TEXT("B"));

	// ---- Makeup overlay on BaseColor ----
	// Lerp(baseColor, makeupColor, makeupWeight), gated by "Makeup Enable" switch
	auto* MakeupTex      = AddTexParam   (Material, TEXT("Makeup Base Color Texture"), GrpMakeup, -1800, -1200, TEX_WHITE);
	auto* MakeupVec      = AddVectorParam(Material, TEXT("Makeup Base Color"), GrpMakeup,
	                                       FLinearColor::White, -1800, -1050);
	auto* MakeupWeight   = AddScalarParam(Material, TEXT("Makeup Weight"), GrpMakeup,
	                                       1.0f, -1400, -1100);

	auto* MulMakeupColor = AddMultiply(Material, -1000, -1125);
	UMaterialEditingLibrary::ConnectMaterialExpressions(MakeupTex, TEXT("RGB"), MulMakeupColor, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(MakeupVec, TEXT("RGB"), MulMakeupColor, TEXT("B"));

	auto* LerpMakeup = AddLerp(Material, -200, -600);
	UMaterialEditingLibrary::ConnectMaterialExpressions(MulBaseColor,   TEXT(""), LerpMakeup, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(MulMakeupColor, TEXT(""), LerpMakeup, TEXT("B"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(MakeupWeight,   TEXT(""), LerpMakeup, TEXT("Alpha"));

	auto* MakeupSwitchBC = AddStaticSwitch(Material, TEXT("Makeup Enable"), GrpMakeup, false, 200, -600);
	UMaterialEditingLibrary::ConnectMaterialExpressions(LerpMakeup,   TEXT(""), MakeupSwitchBC, TEXT("True"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(MulBaseColor, TEXT(""), MakeupSwitchBC, TEXT("False"));
	UMaterialEditingLibrary::ConnectMaterialProperty(MakeupSwitchBC, TEXT(""), MP_BaseColor);

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
	//  Lerp(RoughnessMin, RoughnessMax, RoughnessTex.R × RoughnessScalar)
	//  Remaps the 0-1 texture range into [Min, Max], preserving all variation.
	//  (Clamp would crush detail at the extremes.)
	// =========================================================
	auto* RoughnessTex  = AddTexParam   (Material, TEXT("Specular Lobe 1 Roughness Texture"), GrpRoughness,
	                                      -1800, 1200, TEX_WHITE);
	auto* RoughnessMult = AddScalarParam(Material, TEXT("Specular Lobe 1 Roughness"), GrpRoughness,
	                                      1.0f, -1400, 1350);

	auto* MulRoughness = AddMultiply(Material, -1000, 1275);
	UMaterialEditingLibrary::ConnectMaterialExpressions(RoughnessTex,  TEXT("R"), MulRoughness, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(RoughnessMult, TEXT(""),  MulRoughness, TEXT("B"));

	auto* RoughnessMin = AddScalarParam(Material, TEXT("Roughness Range Min"), GrpRoughness, 0.30f, -1000, 1150);
	auto* RoughnessMax = AddScalarParam(Material, TEXT("Roughness Range Max"), GrpRoughness, 0.60f, -1000, 1200);
	auto* RemapRoughness = AddLerp(Material, -600, 1275);
	UMaterialEditingLibrary::ConnectMaterialExpressions(RoughnessMin, TEXT(""), RemapRoughness, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(RoughnessMax, TEXT(""), RemapRoughness, TEXT("B"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(MulRoughness, TEXT(""), RemapRoughness, TEXT("Alpha"));

	// ---- Makeup roughness multiplier ----
	auto* MakeupRoughMult   = AddScalarParam(Material, TEXT("Makeup Roughness Mult"), GrpMakeup, 1.0f, -1400, 1450);
	auto* MulMakeupRough    = AddMultiply(Material, -200, 1300);
	UMaterialEditingLibrary::ConnectMaterialExpressions(RemapRoughness,  TEXT(""), MulMakeupRough, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(MakeupRoughMult, TEXT(""), MulMakeupRough, TEXT("B"));

	auto* MakeupSwitchRough = AddStaticSwitch(Material, TEXT("Makeup Enable"), GrpMakeup, false, 200, 1300);
	UMaterialEditingLibrary::ConnectMaterialExpressions(MulMakeupRough, TEXT(""), MakeupSwitchRough, TEXT("True"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(RemapRoughness, TEXT(""), MakeupSwitchRough, TEXT("False"));

	// ---- Top Coat roughness ----
	// Lerp toward TopCoatRoughness weighted by TopCoatWeight
	auto* TopCoatRoughness  = AddScalarParam(Material, TEXT("Top Coat Roughness"), GrpTopCoat, 0.67f, -1400, 1500);
	auto* TopCoatWeight     = AddScalarParam(Material, TEXT("Top Coat Weight"),    GrpTopCoat, 0.25f, -1400, 1550);

	auto* LerpTopCoatRough  = AddLerp(Material, 600, 1300);
	UMaterialEditingLibrary::ConnectMaterialExpressions(MakeupSwitchRough, TEXT(""), LerpTopCoatRough, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(TopCoatRoughness,  TEXT(""), LerpTopCoatRough, TEXT("B"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(TopCoatWeight,     TEXT(""), LerpTopCoatRough, TEXT("Alpha"));

	auto* TopCoatSwitchRough = AddStaticSwitch(Material, TEXT("Top Coat Enable"), GrpTopCoat, false, 1000, 1300);
	UMaterialEditingLibrary::ConnectMaterialExpressions(LerpTopCoatRough,  TEXT(""), TopCoatSwitchRough, TEXT("True"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(MakeupSwitchRough, TEXT(""), TopCoatSwitchRough, TEXT("False"));
	UMaterialEditingLibrary::ConnectMaterialProperty(TopCoatSwitchRough, TEXT(""), MP_Roughness);

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

	// ---- Specular Occlusion ----
	// Multiply AO texture into specular, gated by "Specular Occlusion Enable" switch
	auto* AOWeightTex   = AddTexParam(Material, TEXT("Ambient Occlusion Weight Texture"), GrpSpecOcc, -1800, 1950, TEX_WHITE);
	auto* MulSpecOcc    = AddMultiply(Material, -200, 1900);
	UMaterialEditingLibrary::ConnectMaterialExpressions(MulSpecular, TEXT(""), MulSpecOcc, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(AOWeightTex, TEXT("R"), MulSpecOcc, TEXT("B"));

	auto* SpecOccSwitch = AddStaticSwitch(Material, TEXT("Specular Occlusion Enable"), GrpSpecOcc, false, 200, 1900);
	UMaterialEditingLibrary::ConnectMaterialExpressions(MulSpecOcc,  TEXT(""), SpecOccSwitch, TEXT("True"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(MulSpecular, TEXT(""), SpecOccSwitch, TEXT("False"));

	// ---- Top Coat specular ----
	// Lerp toward TopCoatReflectivity weighted by TopCoatWeight
	auto* TopCoatReflectivity = AddScalarParam(Material, TEXT("Top Coat Reflectivity"), GrpTopCoat, 0.5f, -1400, 2100);

	auto* LerpTopCoatSpec = AddLerp(Material, 600, 1900);
	UMaterialEditingLibrary::ConnectMaterialExpressions(SpecOccSwitch,      TEXT(""), LerpTopCoatSpec, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(TopCoatReflectivity, TEXT(""), LerpTopCoatSpec, TEXT("B"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(TopCoatWeight,      TEXT(""), LerpTopCoatSpec, TEXT("Alpha"));

	auto* TopCoatSwitchSpec = AddStaticSwitch(Material, TEXT("Top Coat Enable"), GrpTopCoat, false, 1000, 1900);
	UMaterialEditingLibrary::ConnectMaterialExpressions(LerpTopCoatSpec, TEXT(""), TopCoatSwitchSpec, TEXT("True"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(SpecOccSwitch,   TEXT(""), TopCoatSwitchSpec, TEXT("False"));
	UMaterialEditingLibrary::ConnectMaterialProperty(TopCoatSwitchSpec, TEXT(""), MP_Specular);

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

	// Top Coat extras
	AddVectorParam(Material, TEXT("Top Coat Color"),       GrpTopCoat, FLinearColor::White, -400, 3900);
	AddScalarParam(Material, TEXT("Top Coat Bump Weight"), GrpTopCoat, 0.5f,                -400, 4050);

	// Specular Occlusion extras (note: Daz misspells "Facing" as "Occusion" in export)
	AddScalarParam(Material, TEXT("Specular Occlusion Facing"),  GrpSpecOcc, 0.6f, -400, 4200);
	AddScalarParam(Material, TEXT("Specular Occlusion Grazing"), GrpSpecOcc, 0.1f, -400, 4350);

	// Makeup extras
	AddScalarParam(Material, TEXT("Makeup Metallicity Enable"), GrpMakeup, 0.0f, -400, 4500);

	// ---- Stamp version, compile, save ----
	StampVersion(Material);
	UMaterialEditingLibrary::RecompileMaterial(Material);
	SaveAndNotify(Material);

	UE_LOG(LogDazToUnrealMaterial, Log,
		TEXT("[DazMaterialBuilder] Built %s/%s (v%d)"),
		*DestinationFolder, *AssetName, MATERIAL_BUILDER_VERSION);
}

// ---------------------------------------------------------------------------
// BaseIrayUberSkinMaterial builder
//
// Iray Uber is Daz's general-purpose physically-based shader. Gen 3/8
// characters (Victoria 7, Victoria 8, Rebekah, etc.) and some custom Gen 9
// characters use it for all skin surfaces.
//
// Key differences from PBRSkin:
//   - Different property names (Glossy system vs Dual Lobe Specular)
//   - Diffuse Weight + Diffuse Strength multipliers
//   - No detail normal tiling system
//   - Glossy Layered Weight (+ texture) for specular
//
// Graph layout follows the same column/row scheme as PBRSkin.
// ---------------------------------------------------------------------------

void FDazToUnrealMaterialBuilder::BuildBaseIrayUberSkinMaterial(const FString& DestinationFolder, bool bForce)
{
	const FString AssetName = TEXT("BaseIrayUberSkinMaterial");

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

	ClearMaterialExpressions(Material);

	Material->SetShadingModel(MSM_SubsurfaceProfile);
	Material->BlendMode = BLEND_Opaque;

	// ---- Parameter groups ----
	const FName GrpDiffuse   (TEXT("01 - Diffuse"));
	const FName GrpNormal    (TEXT("02 - Normal"));
	const FName GrpRoughness (TEXT("03 - Roughness"));
	const FName GrpSpecular  (TEXT("04 - Specular"));
	const FName GrpSSS       (TEXT("05 - Subsurface"));
	const FName GrpCavity    (TEXT("06 - Cavity"));
	const FName GrpGlossy    (TEXT("07 - Glossy"));

	// =========================================================
	//  BASE COLOR
	//  (DiffuseTex × DiffuseVec) × (DiffuseWeight × DiffuseStrength)
	//  × Lerp(1, NormalMap.B, CavityStrength)
	// =========================================================
	auto* DiffuseTex      = AddTexParam   (Material, TEXT("Diffuse Color Texture"), GrpDiffuse, -1800, -800, TEX_WHITE);
	auto* DiffuseVec      = AddVectorParam(Material, TEXT("Diffuse Color"), GrpDiffuse,
	                                        FLinearColor::White, -1800, -600);
	auto* DiffuseWeight   = AddScalarParam(Material, TEXT("Diffuse Weight"),   GrpDiffuse, 1.0f, -1800, -450);
	auto* DiffuseStrength = AddScalarParam(Material, TEXT("Diffuse Strength"), GrpDiffuse, 1.0f, -1800, -350);

	// DiffuseTex.RGB × DiffuseVec.RGB
	auto* MulDiffuse = AddMultiply(Material, -1000, -700);
	UMaterialEditingLibrary::ConnectMaterialExpressions(DiffuseTex, TEXT("RGB"), MulDiffuse, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(DiffuseVec, TEXT("RGB"), MulDiffuse, TEXT("B"));

	// DiffuseWeight × DiffuseStrength
	auto* MulDiffuseWS = AddMultiply(Material, -1000, -400);
	UMaterialEditingLibrary::ConnectMaterialExpressions(DiffuseWeight,   TEXT(""), MulDiffuseWS, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(DiffuseStrength, TEXT(""), MulDiffuseWS, TEXT("B"));

	// (DiffuseTex × DiffuseVec) × (Weight × Strength)
	auto* MulDiffuse2 = AddMultiply(Material, -600, -550);
	UMaterialEditingLibrary::ConnectMaterialExpressions(MulDiffuse,   TEXT(""), MulDiffuse2, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(MulDiffuseWS, TEXT(""), MulDiffuse2, TEXT("B"));

	// Cavity from normal map blue channel (same approach as PBRSkin)
	auto* NormalTex      = AddTexParam   (Material, TEXT("Normal Map Texture"), GrpNormal, -1800, 200, TEX_FLAT_NORMAL);
	if (NormalTex) NormalTex->SamplerType = SAMPLERTYPE_Normal;

	auto* CavityStrength = AddScalarParam(Material, TEXT("Cavity Strength"), GrpCavity, 0.5f, -1400, -250);
	auto* ConstOne       = AddConstant   (Material, 1.0f, -1400, -150);

	auto* LerpCavity = AddLerp(Material, -600, -250);
	UMaterialEditingLibrary::ConnectMaterialExpressions(ConstOne,       TEXT(""),  LerpCavity, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(NormalTex,      TEXT("B"), LerpCavity, TEXT("B"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(CavityStrength, TEXT(""),  LerpCavity, TEXT("Alpha"));

	// Final BaseColor = weighted diffuse × cavity
	auto* MulBaseColor = AddMultiply(Material, -200, -400);
	UMaterialEditingLibrary::ConnectMaterialExpressions(MulDiffuse2, TEXT(""), MulBaseColor, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(LerpCavity,  TEXT(""), MulBaseColor, TEXT("B"));
	UMaterialEditingLibrary::ConnectMaterialProperty(MulBaseColor, TEXT(""), MP_BaseColor);

	// =========================================================
	//  NORMAL  (simpler than PBRSkin — no detail normal system)
	//  FlattenNormal(NormalTex, 1 - NormalMapStrength) → MP_Normal
	// =========================================================
	AddScalarParam(Material, TEXT("Bump Strength"), GrpNormal, 0.0f, -2600, 100);

	auto* NormalMapStrength = AddScalarParam(Material, TEXT("Normal Map"), GrpNormal, 1.0f, -2200, 200);
	auto* OneMinusNMS       = AddOneMinus(Material, -1800, 250);
	UMaterialEditingLibrary::ConnectMaterialExpressions(NormalMapStrength, TEXT(""), OneMinusNMS, TEXT(""));

	auto* FlattenBase = AddFunctionCall(Material, FUNC_FLATTEN_NORMAL, -1400, 200);
	if (FlattenBase && FlattenBase->GetInput(0) && FlattenBase->GetInput(1))
	{
		FlattenBase->GetInput(0)->Connect(0, NormalTex);
		FlattenBase->GetInput(1)->Connect(0, OneMinusNMS);
	}
	UMaterialEditingLibrary::ConnectMaterialProperty(FlattenBase, TEXT(""), MP_Normal);

	// =========================================================
	//  SYSTEM SELECTOR + SPECULAR DETAIL TEXTURE
	//  GlossyLayeredWeight selects between Gen 3 (Glossy, =1) and Gen 8+ (Dual Lobe, =0).
	//  The specular textures from each system are repurposed as roughness detail —
	//  skin F0 is nearly uniform (~0.5), so per-pixel variation belongs in roughness.
	// =========================================================
	auto* GlossyWeightParam = AddScalarParam(Material, TEXT("Glossy Layered Weight"), GrpGlossy,
	                                           0.0f, -2600, 1450);

	// Specular detail textures (one per system, only the active one has real data)
	auto* GlossyWeightTex    = AddTexParam(Material, TEXT("Glossy Layered Weight Texture"), GrpGlossy,
	                                        -2600, 600, TEX_WHITE);
	auto* DualLobeReflectTex = AddTexParam(Material, TEXT("Dual Lobe Specular Reflectivity Texture"), GrpSpecular,
	                                        -2600, 750, TEX_WHITE);

	// Select the active system's texture: GlossyLayeredWeight=1 → GlossyTex, =0 → DualLobeTex
	auto* SelectSpecTex = AddLerp(Material, -2200, 675);
	UMaterialEditingLibrary::ConnectMaterialExpressions(DualLobeReflectTex, TEXT("R"), SelectSpecTex, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(GlossyWeightTex,    TEXT("R"), SelectSpecTex, TEXT("B"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(GlossyWeightParam,  TEXT(""),  SelectSpecTex, TEXT("Alpha"));

	// =========================================================
	//  ROUGHNESS
	//  Two roughness systems — use GlossyLayeredWeight to select.
	//  Then the selected specular texture modulates roughness:
	//    bright pixels (high specular) → lower roughness (smoother)
	//    dark pixels (low specular) → higher roughness (matte)
	//  Detail formula: roughness × Lerp(1.0, SpecDetailReduction, SpecTex)
	//  Finally remap into [Min, Max] to preserve variation.
	// =========================================================

	// Glossy roughness path (Gen 3)
	auto* GlossyRoughTex  = AddTexParam   (Material, TEXT("Glossy Roughness Texture"), GrpRoughness,
	                                         -2200, 900, TEX_WHITE);
	auto* GlossyRoughness = AddScalarParam(Material, TEXT("Glossy Roughness"), GrpRoughness,
	                                         0.5f, -2200, 1050);
	auto* MulGlossyRough = AddMultiply(Material, -1800, 975);
	UMaterialEditingLibrary::ConnectMaterialExpressions(GlossyRoughTex,  TEXT("R"), MulGlossyRough, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(GlossyRoughness, TEXT(""),  MulGlossyRough, TEXT("B"));

	// Dual Lobe roughness path (Gen 8+)
	auto* Lobe1RoughTex  = AddTexParam   (Material, TEXT("Specular Lobe 1 Roughness Texture"), GrpRoughness,
	                                        -2200, 1150, TEX_WHITE);
	auto* Lobe1Roughness = AddScalarParam(Material, TEXT("Specular Lobe 1 Roughness"), GrpRoughness,
	                                        0.0f, -2200, 1300);
	auto* MulLobe1Rough = AddMultiply(Material, -1800, 1225);
	UMaterialEditingLibrary::ConnectMaterialExpressions(Lobe1RoughTex,  TEXT("R"), MulLobe1Rough, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(Lobe1Roughness, TEXT(""),  MulLobe1Rough, TEXT("B"));

	// Select active roughness: GlossyLayeredWeight=1 → Glossy, =0 → DualLobe
	auto* SelectRoughness = AddLerp(Material, -1400, 1100);
	UMaterialEditingLibrary::ConnectMaterialExpressions(MulLobe1Rough,     TEXT(""), SelectRoughness, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(MulGlossyRough,    TEXT(""), SelectRoughness, TEXT("B"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(GlossyWeightParam, TEXT(""), SelectRoughness, TEXT("Alpha"));

	// Spec texture → centered roughness variation
	// Bright spec pixels = smoother, dark spec pixels = rougher, centered on scalar roughness.
	// Formula: scalarRough + (OneMinus(SpecTex) - 0.5) × SpecDetailRange
	// This preserves the scalar as the baseline and adds bidirectional variation from
	// the spec texture, preventing uniformly-bright textures from collapsing roughness.
	auto* InvertSpecTex = AddOneMinus(Material, -1800, 600);
	UMaterialEditingLibrary::ConnectMaterialExpressions(SelectSpecTex, TEXT(""), InvertSpecTex, TEXT(""));

	// Center: OneMinus(SpecTex) - 0.5 → range [-0.5, +0.5]
	// SpecTex=0.5 → 0 (neutral), SpecTex=1.0 → -0.5 (smoother), SpecTex=0.0 → +0.5 (rougher)
	auto* CenterOffset = AddConstant(Material, -0.5f, -1800, 650);
	auto* CenteredSpec = AddAdd(Material, -1600, 625);
	UMaterialEditingLibrary::ConnectMaterialExpressions(InvertSpecTex, TEXT(""), CenteredSpec, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(CenterOffset,  TEXT(""), CenteredSpec, TEXT("B"));

	// Scale the centered variation by SpecDetailRange
	auto* SpecDetailRange = AddScalarParam(Material, TEXT("Specular Detail Range"), GrpRoughness,
	                                        0.6f, -1600, 700);
	auto* ScaledVariation = AddMultiply(Material, -1400, 650);
	UMaterialEditingLibrary::ConnectMaterialExpressions(CenteredSpec,    TEXT(""), ScaledVariation, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(SpecDetailRange, TEXT(""), ScaledVariation, TEXT("B"));

	// Add variation to scalar roughness baseline
	auto* SpecRough = AddAdd(Material, -1200, 800);
	UMaterialEditingLibrary::ConnectMaterialExpressions(SelectRoughness, TEXT(""), SpecRough, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(ScaledVariation, TEXT(""), SpecRough, TEXT("B"));

	// =========================================================
	//  NORMAL-DERIVED ROUGHNESS
	//  Derive per-pixel roughness variation from normal map curvature.
	//  Dot(Normal.RGB, (0,0,1)) → 1 for flat, <1 for angled (pores/wrinkles).
	//  OneMinus gives "detail amount" (0=flat, >0=angled).
	//  Add scaled contribution to roughness alpha before remap.
	// =========================================================
	auto* FlatNormalVec = AddConstant3Vector(Material, FLinearColor(0.f, 0.f, 1.f), -1400, 350);
	auto* DotNormal = AddDotProduct(Material, -1000, 350);
	UMaterialEditingLibrary::ConnectMaterialExpressions(NormalTex,    TEXT("RGB"), DotNormal, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(FlatNormalVec, TEXT(""),   DotNormal, TEXT("B"));

	auto* OneMinusDot = AddOneMinus(Material, -800, 350);
	UMaterialEditingLibrary::ConnectMaterialExpressions(DotNormal, TEXT(""), OneMinusDot, TEXT(""));

	auto* NormalRoughStr = AddScalarParam(Material, TEXT("Normal Roughness Strength"), GrpRoughness,
	                                       0.3f, -1000, 450);
	auto* MulNormRough = AddMultiply(Material, -600, 400);
	UMaterialEditingLibrary::ConnectMaterialExpressions(OneMinusDot,   TEXT(""), MulNormRough, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(NormalRoughStr, TEXT(""), MulNormRough, TEXT("B"));

	// Add normal-derived roughness to spec-offset roughness
	auto* AddNormRough = AddAdd(Material, -400, 900);
	UMaterialEditingLibrary::ConnectMaterialExpressions(SpecRough, TEXT(""), AddNormRough, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(MulNormRough,   TEXT(""), AddNormRough, TEXT("B"));

	// Remap [0,1] → [Min, Max] to preserve texture variation
	auto* RoughnessMin = AddScalarParam(Material, TEXT("Roughness Range Min"), GrpRoughness, 0.30f, -400, 1050);
	auto* RoughnessMax = AddScalarParam(Material, TEXT("Roughness Range Max"), GrpRoughness, 0.60f, -400, 1100);
	auto* RemapRoughness = AddLerp(Material, -200, 1000);
	UMaterialEditingLibrary::ConnectMaterialExpressions(RoughnessMin,  TEXT(""), RemapRoughness, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(RoughnessMax,  TEXT(""), RemapRoughness, TEXT("B"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(AddNormRough,  TEXT(""), RemapRoughness, TEXT("Alpha"));
	UMaterialEditingLibrary::ConnectMaterialProperty(RemapRoughness, TEXT(""), MP_Roughness);

	// =========================================================
	//  METALLIC
	// =========================================================
	auto* MetallicParam = AddScalarParam(Material, TEXT("Metallic Weight"), GrpSpecular, 0.0f, -600, 2100);
	UMaterialEditingLibrary::ConnectMaterialProperty(MetallicParam, TEXT(""), MP_Metallic);

	// =========================================================
	//  SPECULAR
	//  Flat F0 — skin IOR doesn't vary per-pixel (~0.5 = 4% for skin).
	//  Single user-facing param with a name that won't collide with Daz import binding.
	//  Per-pixel detail is handled by the roughness channel above.
	// =========================================================
	auto* SkinSpecular = AddScalarParam(Material, TEXT("Skin Specular"), GrpSpecular,
	                                     0.5f, -1000, 1625);
	UMaterialEditingLibrary::ConnectMaterialProperty(SkinSpecular, TEXT(""), MP_Specular);

	// =========================================================
	//  OPACITY  (SubsurfaceProfile uses Opacity as SSS weight)
	//  TranslucencyWeight → MP_Opacity
	// =========================================================
	auto* TransWeight = AddScalarParam(Material, TEXT("Translucency Weight"), GrpSSS, 0.5f, -800, 2200);
	UMaterialEditingLibrary::ConnectMaterialProperty(TransWeight, TEXT(""), MP_Opacity);

	// =========================================================
	//  UNUSED-BUT-EXPOSED PARAMETERS
	//  Must exist for DazToUnreal property binding and
	//  SubsurfaceProfile creation.
	// =========================================================

	// SSS colors (read by CreateSubsurfaceProfileForMaterial)
	AddVectorParam(Material, TEXT("SSS Color"),
		GrpSSS, FLinearColor(0.976f, 0.694f, 0.761f, 1.f), -400, 1700);
	AddVectorParam(Material, TEXT("Transmitted Color"),
		GrpSSS, FLinearColor(0.976f, 0.482f, 0.353f, 1.f), -400, 1850);
	AddVectorParam(Material, TEXT("Translucency Color"),
		GrpSSS, FLinearColor::White, -400, 2000);
	AddTexParam(Material, TEXT("Translucency Color Texture"), GrpSSS, -400, 2150, TEX_WHITE);

	// SSS extras
	AddScalarParam(Material, TEXT("SSS Amount"),    GrpSSS, 0.5f, -400, 2300);
	AddScalarParam(Material, TEXT("SSS Direction"), GrpSSS, 0.0f, -400, 2450);
	AddScalarParam(Material, TEXT("Backscattering Weight"), GrpSSS, 0.0f, -400, 2600);
	AddVectorParam(Material, TEXT("Backscattering Color"),  GrpSSS, FLinearColor::White, -400, 2750);
	AddTexParam(Material, TEXT("Subsurface Alpha Texture"), GrpSSS, -400, 2900, TEX_WHITE);

	// Glossy extras (reflectivity here for import binding — not wired, F0 is "Skin Specular" above)
	AddScalarParam(Material, TEXT("Glossy Reflectivity"), GrpGlossy, 0.5f, -400, 3050);
	AddVectorParam(Material, TEXT("Glossy Specular"),     GrpGlossy, FLinearColor::White, -400, 3200);
	AddScalarParam(Material, TEXT("Glossy Anisotropy"),   GrpGlossy, 0.0f, -400, 3350);
	AddVectorParam(Material, TEXT("Glossy Color"),        GrpGlossy, FLinearColor::White, -400, 3500);

	// Diffuse extras
	AddScalarParam(Material, TEXT("Diffuse Roughness"), GrpDiffuse, 0.0f, -400, 3500);

	// Dual-lobe specular extras (for SubsurfaceProfile creation and import binding)
	// Not wired — F0 is "Skin Specular", textures feed roughness detail.
	AddScalarParam(Material, TEXT("Dual Lobe Specular Weight"),        GrpSpecular, 0.0f,  -400, 3600);
	AddScalarParam(Material, TEXT("Dual Lobe Specular Reflectivity"),  GrpSpecular, 0.5f,  -400, 3625);
	AddScalarParam(Material, TEXT("Dual Lobe Specular Ratio"),         GrpSpecular, 0.15f, -400, 3650);
	AddScalarParam(Material, TEXT("Specular Lobe 2 Roughness Mult"),   GrpSpecular, 0.55f, -400, 3800);
	AddScalarParam(Material, TEXT("Dual Lobe Specular Roughness Mult"), GrpSpecular, 1.0f, -400, 3950);

	// Refraction (0 for skin — present for property binding)
	AddScalarParam(Material, TEXT("Refraction Index"),  GrpSpecular, 1.4f, -400, 4400);
	AddScalarParam(Material, TEXT("Refraction Weight"), GrpSpecular, 0.0f, -400, 4550);

	// ---- Stamp version, compile, save ----
	StampVersion(Material);
	UMaterialEditingLibrary::RecompileMaterial(Material);
	SaveAndNotify(Material);

	UE_LOG(LogDazToUnrealMaterial, Log,
		TEXT("[DazMaterialBuilder] Built %s/%s (v%d)"),
		*DestinationFolder, *AssetName, MATERIAL_BUILDER_VERSION);
}
