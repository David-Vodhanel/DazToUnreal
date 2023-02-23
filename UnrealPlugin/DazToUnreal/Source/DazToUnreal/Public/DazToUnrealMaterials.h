#pragma once

#include "CoreMinimal.h"
#include "DazToUnrealEnums.h"

class UMaterialInstanceConstant;
class UMaterialInterface;
class USubsurfaceProfile;
class FJsonValue;

DECLARE_LOG_CATEGORY_EXTERN(LogDazToUnrealMaterial, Log, All);

// struct for holding material override settings
struct FDUFTextureProperty
{
	FString Name;
	FString Type;
	FString Value;
	FString ObjectName;
	FString ShaderName;
	FString MaterialAssetName;

	inline bool operator==(const FDUFTextureProperty& rhs) const
	{
		return Name == rhs.Name && Type == rhs.Type && Value == rhs.Value && ObjectName == rhs.ObjectName;
	}
};

class FDazToUnrealMaterials
{
public:
	static FSoftObjectPath GetBaseMaterial(FString MaterialName, TArray<FDUFTextureProperty > MaterialProperties);
	static UMaterialInstanceConstant* CreateMaterial(const FString CharacterMaterialFolder, const FString CharacterTexturesFolder, FString& MaterialName, TMap<FString, TArray<FDUFTextureProperty>> MaterialProperties, const DazCharacterType CharacterType, UMaterialInterface* ParentMaterial, USubsurfaceProfile* SubsurfaceProfile);
	static void CorrectDazShaders(FString MaterialName, TMap<FString, TArray<FDUFTextureProperty>>& MaterialProperties);
	static void SetMaterialProperty(const FString& MaterialName, const FString& PropertyName, const FString& PropertyType, const FString& PropertyValue, TMap<FString, TArray<FDUFTextureProperty>>& MaterialProperties);
	static bool HasMaterialProperty(const FString& PropertyName, const TArray<FDUFTextureProperty> &MaterialProperties);
	static FString GetMaterialProperty(const FString& PropertyName, const TArray<FDUFTextureProperty>& MaterialProperties);

	static FSoftObjectPath GetMostCommonBaseMaterial(TArray<FString> MaterialNames, TMap<FString, TArray<FDUFTextureProperty>> MaterialProperties);
	static TArray<FDUFTextureProperty> GetMostCommonProperties(TArray<FString> MaterialNames, TMap<FString, TArray<FDUFTextureProperty>> MaterialProperties);
	static FDUFTextureProperty GetMostCommonProperty(FString PropertyName, TArray<FString> MaterialNames, TMap<FString, TArray<FDUFTextureProperty>> MaterialProperties);

	static FSoftObjectPath GetBaseMaterialForShader(FString ShaderName);
	static FSoftObjectPath GetSkinMaterialForShader(FString ShaderName);

	static USubsurfaceProfile* CreateSubsurfaceBaseProfileForCharacter(const FString CharacterMaterialFolder, TMap<FString, TArray<FDUFTextureProperty>>& MaterialProperties);
	static USubsurfaceProfile* CreateSubsurfaceProfileForMaterial(const FString MaterialName, const FString CharacterMaterialFolder, const TArray<FDUFTextureProperty > MaterialProperties);
	static bool SubsurfaceProfilesAreIdentical(USubsurfaceProfile* A, USubsurfaceProfile* B);
	static bool SubsurfaceProfilesWouldBeIdentical(USubsurfaceProfile* ExistingSubsurfaceProfile, const TArray<FDUFTextureProperty > MaterialProperties);

	// Returns a map of material to the material it's a duplicate of.
	static TMap<TSharedPtr<FJsonValue>, TSharedPtr<FJsonValue>> FindDuplicateMaterials(TArray<TSharedPtr<FJsonValue>> MaterialList);

	// Get the friendly material object name from the fbx name
	static FString GetFriendlyObjectName(FString FbxObjectName, TMap<FString, TArray<FDUFTextureProperty>> MaterialProperties);
};