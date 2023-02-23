#include "DazToUnrealMaterials.h"
#include "DazToUnrealSettings.h"
#include "DazToUnrealTextures.h"

#include "Materials/MaterialInstanceConstant.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Factories/SubsurfaceProfileFactory.h"
#include "Engine/SubsurfaceProfile.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION > 0
#include "AssetRegistry/AssetRegistryModule.h"
#else
#include "AssetRegistryModule.h"
#endif
#include "AssetToolsModule.h"
#include "Dom/JsonObject.h"

DEFINE_LOG_CATEGORY(LogDazToUnrealMaterial);

FSoftObjectPath FDazToUnrealMaterials::GetBaseMaterialForShader(FString ShaderName)
{
	const UDazToUnrealSettings* CachedSettings = GetDefault<UDazToUnrealSettings>();
	FSoftObjectPath BaseMaterialAssetPath = CachedSettings->FindMaterial(ShaderName, EDazMaterialType::Base);
	return BaseMaterialAssetPath;
}
FSoftObjectPath FDazToUnrealMaterials::GetSkinMaterialForShader(FString ShaderName)
{
	const UDazToUnrealSettings* CachedSettings = GetDefault<UDazToUnrealSettings>();
	FSoftObjectPath BaseMaterialAssetPath = CachedSettings->FindMaterial(ShaderName, EDazMaterialType::Skin);
	return BaseMaterialAssetPath;
}

FSoftObjectPath FDazToUnrealMaterials::GetBaseMaterial(FString MaterialName, TArray<FDUFTextureProperty > MaterialProperties)
{
	const UDazToUnrealSettings* CachedSettings = GetDefault<UDazToUnrealSettings>();

	FString AssetType = "";
	FString ShaderName = "";
	FString Seperator;
	if ( CachedSettings->UseOriginalMaterialName)
	{
		Seperator = "";
	}
	else
	{
		Seperator = "_";
	}
	TArray<FDUFTextureProperty> Properties = MaterialProperties;
	for (FDUFTextureProperty Property : Properties)
	{
		if (Property.Name == TEXT("Asset Type"))
		{
			AssetType = Property.Value;
			ShaderName = Property.ShaderName;
		}
	}

	// Set the default material type
	FSoftObjectPath BaseMaterialAssetPath = CachedSettings->FindMaterial(ShaderName, EDazMaterialType::Base);

	if (AssetType == TEXT("Follower/Hair"))
	{
		BaseMaterialAssetPath = CachedSettings->FindMaterial(ShaderName, EDazMaterialType::Hair);
		if (MaterialName.EndsWith(Seperator + TEXT("scalp")))
		{
			BaseMaterialAssetPath = CachedSettings->FindMaterial(ShaderName, EDazMaterialType::Scalp);
		}
	}
	else if (AssetType == TEXT("Follower/Attachment/Head/Face/Eyelashes"))
	{
		if (MaterialName.Contains(Seperator + TEXT("EyeMoisture")))
		{
			BaseMaterialAssetPath = CachedSettings->FindMaterial(ShaderName, EDazMaterialType::EyeMoisture);
		}
		else
		{
			BaseMaterialAssetPath = CachedSettings->FindMaterial(ShaderName, EDazMaterialType::Alpha);
		}
	}
	else if (AssetType == TEXT("Follower/Attachment/Lower-Body/Hip/Front") &&
		MaterialName.Contains(Seperator + TEXT("Genitalia")))
	{
		BaseMaterialAssetPath = GetSkinMaterialForShader(ShaderName);
	}
	else if (AssetType == TEXT("Actor/Character"))
	{
		// Check for skin materials
		if (MaterialName.EndsWith(Seperator + TEXT("Face")) ||
			MaterialName.EndsWith(Seperator + TEXT("Head")) ||
			MaterialName.EndsWith(Seperator + TEXT("Lips")) ||
			MaterialName.EndsWith(Seperator + TEXT("Legs")) ||
			MaterialName.EndsWith(Seperator + TEXT("Hips")) ||
			MaterialName.EndsWith(Seperator + TEXT("Feet")) ||
			MaterialName.EndsWith(Seperator + TEXT("Torso")) ||
			MaterialName.EndsWith(Seperator + TEXT("Body")) ||
			MaterialName.EndsWith(Seperator + TEXT("Neck")) ||
			MaterialName.EndsWith(Seperator + TEXT("Shoulders")) ||
			MaterialName.EndsWith(Seperator + TEXT("Arms")) ||
			MaterialName.EndsWith(Seperator + TEXT("Forearms")) ||
			MaterialName.EndsWith(Seperator + TEXT("Hands")) ||
			MaterialName.EndsWith(Seperator + TEXT("EyeSocket")) ||
			MaterialName.EndsWith(Seperator + TEXT("Ears")) ||
			MaterialName.EndsWith(Seperator + TEXT("Fingernails")) ||
			MaterialName.EndsWith(Seperator + TEXT("Toenails")) ||
			MaterialName.EndsWith(Seperator + TEXT("Nipples")) ||
			MaterialName.EndsWith(Seperator + TEXT("Genitalia")))
		{
			BaseMaterialAssetPath = GetSkinMaterialForShader(ShaderName);
		}
		else if (MaterialName.Contains(Seperator + TEXT("EyeMoisture")))
		{
			BaseMaterialAssetPath = CachedSettings->FindMaterial(ShaderName, EDazMaterialType::EyeMoisture);
		}
		else if (MaterialName.Contains(Seperator + TEXT("EyeReflection")))
		{
			BaseMaterialAssetPath = CachedSettings->FindMaterial(ShaderName, EDazMaterialType::EyeMoisture);
		}
		else if (MaterialName.Contains(Seperator + TEXT("Tear")))
		{
			BaseMaterialAssetPath = CachedSettings->FindMaterial(ShaderName, EDazMaterialType::EyeMoisture);
		}
		else if (MaterialName.EndsWith(Seperator + TEXT("EyeLashes")))
		{
			BaseMaterialAssetPath = CachedSettings->FindMaterial(ShaderName, EDazMaterialType::Alpha);
		}
		else if (MaterialName.EndsWith(Seperator + TEXT("Eyelashes")))
		{
			BaseMaterialAssetPath = CachedSettings->FindMaterial(ShaderName, EDazMaterialType::Alpha);
		}
		else if (MaterialName.EndsWith(Seperator + TEXT("Eyelash")))
		{
			BaseMaterialAssetPath = CachedSettings->FindMaterial(ShaderName, EDazMaterialType::Alpha);
		}
		else if (MaterialName.EndsWith(Seperator + TEXT("cornea")))
		{
			BaseMaterialAssetPath = CachedSettings->FindMaterial(ShaderName, EDazMaterialType::Cornea);
		}
		/*else if (MaterialName.EndsWith(TEXT("_sclera")))
		{
			BaseMaterialAssetPath = CachedSettings->BaseMaterial;
		}
		else if (MaterialName.EndsWith(TEXT("_irises")))
		{
			BaseMaterialAssetPath = CachedSettings->BaseMaterial;
		}
		else if (MaterialName.EndsWith(TEXT("_pupils")))
		{
			BaseMaterialAssetPath = CachedSettings->BaseMaterial;
		}*/
		else
		{
			//BaseMaterialAssetPath = CachedSettings->BaseMaterial;

			for (FDUFTextureProperty Property : Properties)
			{
				if (Property.Name == TEXT("Cutout Opacity Texture"))
				{
					BaseMaterialAssetPath = CachedSettings->FindMaterial(ShaderName, EDazMaterialType::Alpha);
				}
			}

		}
	}
	else if (MaterialName.Contains(Seperator + TEXT("EyeMoisture")))
	{
		BaseMaterialAssetPath = CachedSettings->FindMaterial(ShaderName, EDazMaterialType::EyeMoisture);
	}
	else
	{
		//BaseMaterialAssetPath = CachedSettings->BaseMaterial;

		for (FDUFTextureProperty Property : Properties)
		{
			if (Property.Name == TEXT("Cutout Opacity Texture"))
			{
				BaseMaterialAssetPath = CachedSettings->FindMaterial(ShaderName, EDazMaterialType::Alpha);
			}
			if (Property.Name == TEXT("Opacity Strength") && Property.Value != TEXT("1"))
			{
				BaseMaterialAssetPath = CachedSettings->FindMaterial(ShaderName, EDazMaterialType::Alpha);
			}
		}

	}
	if (MaterialName.EndsWith(Seperator + TEXT("NoDraw")))
	{
		BaseMaterialAssetPath = CachedSettings->FindMaterial(ShaderName, EDazMaterialType::NoDraw);
	}

	return BaseMaterialAssetPath;
}

UMaterialInstanceConstant* FDazToUnrealMaterials::CreateMaterial(const FString CharacterMaterialFolder, const FString CharacterTexturesFolder, FString& MaterialName, TMap<FString, TArray<FDUFTextureProperty>> MaterialProperties, const DazCharacterType CharacterType, UMaterialInterface* ParentMaterial, USubsurfaceProfile* SubsurfaceProfile)
{
	const UDazToUnrealSettings* CachedSettings = GetDefault<UDazToUnrealSettings>();

	FSoftObjectPath BaseMaterialAssetPath = CachedSettings->FindMaterial(FString(TEXT("None")), EDazMaterialType::Base);
	// Prepare the material Properties
	if (MaterialProperties.Contains(MaterialName))
	{
		BaseMaterialAssetPath = GetBaseMaterial(MaterialName, MaterialProperties[MaterialName]);
	}

	FString ShaderName = "";
	FString AssetType = "";
	if (MaterialProperties.Contains(MaterialName))
	{
		TArray<FDUFTextureProperty> Properties = MaterialProperties[MaterialName];
		for (FDUFTextureProperty Property : Properties)
		{
			if (Property.Name == TEXT("Asset Type"))
			{
				AssetType = Property.Value;
				ShaderName = Property.ShaderName;
			}
		}
	}
	FString Seperator;
	if ( CachedSettings->UseOriginalMaterialName)
	{
		Seperator = "";
	}
	else
	{
		Seperator = "_";
	}
	if (AssetType == TEXT("Follower/Attachment/Head/Face/Eyelashes") ||
		AssetType == TEXT("Follower/Attachment/Head/Face/Eyes") ||
		AssetType == TEXT("Follower/Attachment/Head/Face/Eyes/Tear") ||
		AssetType == TEXT("Follower/Attachment/Head/Face/Tears"))
	{
		if (MaterialName.Contains(Seperator + TEXT("EyeMoisture")) || MaterialName.EndsWith(Seperator + TEXT("EyeReflection")))
		{
			SetMaterialProperty(MaterialName, TEXT("Metallic Weight"), TEXT("Double"), TEXT("1"), MaterialProperties);
			SetMaterialProperty(MaterialName, TEXT("Opacity Strength"), TEXT("Double"), FString::SanitizeFloat(CachedSettings->DefaultEyeMoistureOpacity), MaterialProperties);
			SetMaterialProperty(MaterialName, TEXT("Diffuse Color"), TEXT("Color"), TEXT("#bababa"), MaterialProperties);
			SetMaterialProperty(MaterialName, TEXT("Index of Refraction"), TEXT("Double"), TEXT("1.0"), MaterialProperties);
		}
		else if (MaterialName.EndsWith(Seperator + TEXT("Tear")) || MaterialName.EndsWith(Seperator + TEXT("Tears")))
		{
			SetMaterialProperty(MaterialName, TEXT("Metallic Weight"), TEXT("Double"), TEXT("1"), MaterialProperties);
			SetMaterialProperty(MaterialName, TEXT("Opacity Strength"), TEXT("Double"), FString::SanitizeFloat(CachedSettings->DefaultEyeMoistureOpacity), MaterialProperties);
			SetMaterialProperty(MaterialName, TEXT("Index of Refraction"), TEXT("Double"), TEXT("1.0"), MaterialProperties);
		}
		else
		{
			SetMaterialProperty(MaterialName, TEXT("Metallic Weight"), TEXT("Double"), TEXT("0"), MaterialProperties);
			SetMaterialProperty(MaterialName, TEXT("Glossy Layered Weight"), TEXT("Double"), TEXT("0"), MaterialProperties);
		}
	}
	else if (AssetType == TEXT("Follower/Attachment/Lower-Body/Hip/Front") &&
		MaterialName.Contains(Seperator + TEXT("Genitalia")))
	{
		SetMaterialProperty(MaterialName, TEXT("Subsurface Alpha Texture"), TEXT("Texture"), FDazToUnrealTextures::GetSubSurfaceAlphaTexture(CharacterType, MaterialName), MaterialProperties);
	}
	else if (AssetType == TEXT("Actor/Character"))
	{
		// Check for skin materials
		if (MaterialName.EndsWith(Seperator + TEXT("Face")) ||
			MaterialName.EndsWith(Seperator + TEXT("Head")) ||
			MaterialName.EndsWith(Seperator + TEXT("Lips")) ||
			MaterialName.EndsWith(Seperator + TEXT("Legs")) ||
			MaterialName.EndsWith(Seperator + TEXT("Torso")) ||
			MaterialName.EndsWith(Seperator + TEXT("Body")) ||
			MaterialName.EndsWith(Seperator + TEXT("Arms")) ||
			MaterialName.EndsWith(Seperator + TEXT("EyeSocket")) ||
			MaterialName.EndsWith(Seperator + TEXT("Ears")) ||
			MaterialName.EndsWith(Seperator + TEXT("Fingernails")) ||
			MaterialName.EndsWith(Seperator + TEXT("Toenails")) ||
			MaterialName.EndsWith(Seperator + TEXT("Genitalia")))
		{
			SetMaterialProperty(MaterialName, TEXT("Diffuse Subsurface Color Weight"), TEXT("Double"), FString::SanitizeFloat(CachedSettings->DefaultSkinDiffuseSubsurfaceColorWeight), MaterialProperties);
			SetMaterialProperty(MaterialName, TEXT("Subsurface Alpha Texture"), TEXT("Texture"), FDazToUnrealTextures::GetSubSurfaceAlphaTexture(CharacterType, MaterialName), MaterialProperties);
		}
		else if (MaterialName.Contains(Seperator + TEXT("EyeMoisture")))
		{
			SetMaterialProperty(MaterialName, TEXT("Metallic Weight"), TEXT("Double"), TEXT("1"), MaterialProperties);
			SetMaterialProperty(MaterialName, TEXT("Opacity Strength"), TEXT("Double"), FString::SanitizeFloat(CachedSettings->DefaultEyeMoistureOpacity), MaterialProperties);
			SetMaterialProperty(MaterialName, TEXT("Index of Refraction"), TEXT("Double"), TEXT("1.0"), MaterialProperties);
		}
		else if (MaterialName.EndsWith(Seperator + TEXT("EyeReflection")) || MaterialName.EndsWith(Seperator + TEXT("Tear")) || MaterialName.EndsWith(Seperator + TEXT("Tears")))
		{
			SetMaterialProperty(MaterialName, TEXT("Metallic Weight"), TEXT("Double"), TEXT("1"), MaterialProperties);
			SetMaterialProperty(MaterialName, TEXT("Opacity Strength"), TEXT("Double"), FString::SanitizeFloat(CachedSettings->DefaultEyeMoistureOpacity), MaterialProperties);
			SetMaterialProperty(MaterialName, TEXT("Index of Refraction"), TEXT("Double"), TEXT("1.0"), MaterialProperties);
		}
		else if (MaterialName.EndsWith(Seperator + TEXT("EyeLashes")) || MaterialName.EndsWith(Seperator + TEXT("Eyelashes")))
		{
			SetMaterialProperty(MaterialName, TEXT("Metallic Weight"), TEXT("Double"), TEXT("0"), MaterialProperties);
			SetMaterialProperty(MaterialName, TEXT("Glossy Layered Weight"), TEXT("Double"), TEXT("0"), MaterialProperties);
			SetMaterialProperty(MaterialName, TEXT("Index of Refraction"), TEXT("Double"), TEXT("1.0"), MaterialProperties);
		}
		else if (MaterialName.EndsWith(Seperator + TEXT("cornea")))
		{
			SetMaterialProperty(MaterialName, TEXT("Metallic Weight"), TEXT("Double"), TEXT("1"), MaterialProperties);
			SetMaterialProperty(MaterialName, TEXT("Opacity Strength"), TEXT("Double"), FString::SanitizeFloat(CachedSettings->DefaultEyeMoistureOpacity), MaterialProperties);
			SetMaterialProperty(MaterialName, TEXT("Index of Refraction"), TEXT("Double"), TEXT("1.0"), MaterialProperties);
		}
		else if (MaterialName.EndsWith(Seperator + TEXT("irises")))
		{
			SetMaterialProperty(MaterialName, TEXT("Pixel Depth Offset"), TEXT("Double"), TEXT("0.1"), MaterialProperties);
		}
		else if (MaterialName.EndsWith(Seperator + TEXT("pupils")))
		{
			SetMaterialProperty(MaterialName, TEXT("Pixel Depth Offset"), TEXT("Double"), TEXT("0.1"), MaterialProperties);
		}

	}
	else if (MaterialName.Contains(Seperator + TEXT("EyeMoisture")))
	{
		SetMaterialProperty(MaterialName, TEXT("Metallic Weight"), TEXT("Double"), TEXT("1"), MaterialProperties);
		SetMaterialProperty(MaterialName, TEXT("Opacity Strength"), TEXT("Double"), FString::SanitizeFloat(CachedSettings->DefaultEyeMoistureOpacity), MaterialProperties);
		SetMaterialProperty(MaterialName, TEXT("Index of Refraction"), TEXT("Double"), TEXT("1.0"), MaterialProperties);
	}

	CorrectDazShaders(MaterialName, MaterialProperties);

	// Create the Material Instance
	auto MaterialInstanceFactory = NewObject<UMaterialInstanceConstantFactoryNew>();
#if ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION < 26
	UPackage* Package = CreatePackage(nullptr, *(CharacterMaterialFolder / MaterialName));
#else
	UPackage* Package = CreatePackage(*(CharacterMaterialFolder / MaterialName));
#endif
	UMaterialInstanceConstant* UnrealMaterialConstant = (UMaterialInstanceConstant*)MaterialInstanceFactory->FactoryCreateNew(UMaterialInstanceConstant::StaticClass(), Package, *MaterialName, RF_Standalone | RF_Public, NULL, GWarn);


	if (UnrealMaterialConstant != NULL)
	{
		// Notify the asset registry
		FAssetRegistryModule::AssetCreated(UnrealMaterialConstant);

		// Set the dirty flag so this package will get saved later
		Package->SetDirtyFlag(true);

		UObject* BaseMaterial = BaseMaterialAssetPath.TryLoad();
		if (ParentMaterial && ParentMaterial->IsA(UMaterialInstanceConstant::StaticClass()) && Cast<UMaterialInstanceConstant>(ParentMaterial)->Parent == BaseMaterial)
		{
			UnrealMaterialConstant->SetParentEditorOnly(ParentMaterial);
		}
		else if (ParentMaterial && ParentMaterial->IsA(UMaterial::StaticClass()))
		{
			UnrealMaterialConstant->SetParentEditorOnly(ParentMaterial);
		}
		else
		{
			UnrealMaterialConstant->SetParentEditorOnly((UMaterial*)BaseMaterial);
		}

		if (SubsurfaceProfile)
		{
			if (!ParentMaterial || ParentMaterial->SubsurfaceProfile != SubsurfaceProfile)
			{
				UnrealMaterialConstant->bOverrideSubsurfaceProfile = 1;
				UnrealMaterialConstant->SubsurfaceProfile = SubsurfaceProfile;
			}
			else
			{
				UnrealMaterialConstant->bOverrideSubsurfaceProfile = 0;
			}
		}

		// Set the MaterialInstance properties
		if (MaterialProperties.Contains(MaterialName))
		{

			// Rename properties based on the settings
			TArray<FDUFTextureProperty> MaterialInstanceProperties;
			for (FDUFTextureProperty MaterialProperty : MaterialProperties[MaterialName])
			{
				if (CachedSettings->MaterialPropertyMapping.Contains(MaterialProperty.Name))
				{
					MaterialProperty.Name = CachedSettings->MaterialPropertyMapping[MaterialProperty.Name];
				}
				MaterialInstanceProperties.Add(MaterialProperty);
			}

			// Apply the properties
			for (FDUFTextureProperty MaterialProperty : MaterialInstanceProperties)
			{
				if (MaterialProperty.Type == TEXT("Texture"))
				{
					FSoftObjectPath TextureAssetPath(CharacterTexturesFolder / MaterialProperty.Value);
					UObject* TextureAsset = TextureAssetPath.TryLoad();
					if (TextureAsset == nullptr)
					{
						FSoftObjectPath TextureAssetPathFull(MaterialProperty.Value);
						TextureAsset = TextureAssetPathFull.TryLoad();
					}
					FMaterialParameterInfo ParameterInfo(*MaterialProperty.Name);
					UnrealMaterialConstant->SetTextureParameterValueEditorOnly(ParameterInfo, (UTexture*)TextureAsset);
				}
				if (MaterialProperty.Type == TEXT("Double"))
				{
					float Value = FCString::Atof(*MaterialProperty.Value);
					FMaterialParameterInfo ParameterInfo(*MaterialProperty.Name);
					UnrealMaterialConstant->SetScalarParameterValueEditorOnly(ParameterInfo, Value);
				}
				if (MaterialProperty.Type == TEXT("Color"))
				{
					//FLinearColor Value = FDazToUnrealModule::FromHex(MaterialProperty.Value);
					//FColor Color = Value.ToFColor(true);
					FColor Color = FColor::FromHex(MaterialProperty.Value);
					FMaterialParameterInfo ParameterInfo(*MaterialProperty.Name);
					UnrealMaterialConstant->SetVectorParameterValueEditorOnly(ParameterInfo, Color);
				}
				if (MaterialProperty.Type == TEXT("Switch"))
				{
					FStaticParameterSet StaticParameters;
					UnrealMaterialConstant->GetStaticParameterValues(StaticParameters);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION > 0
					TArray<FStaticSwitchParameter>& StaticSwitchParameters = StaticParameters.EditorOnly.StaticSwitchParameters;
#else
					TArray<FStaticSwitchParameter>& StaticSwitchParameters = StaticParameters.StaticSwitchParameters;
#endif
					
					for (int32 ParameterIdx = 0; ParameterIdx < StaticSwitchParameters.Num(); ParameterIdx++)
					{
						for (int32 SwitchParamIdx = 0; SwitchParamIdx < StaticSwitchParameters.Num(); SwitchParamIdx++)
						{
							FStaticSwitchParameter& StaticSwitchParam = StaticSwitchParameters[SwitchParamIdx];

							if (StaticSwitchParam.ParameterInfo.Name.ToString() == MaterialProperty.Name)
							{
								StaticSwitchParam.bOverride = true;
								StaticSwitchParam.Value = MaterialProperty.Value.ToLower() == TEXT("true");
							}
						}
					}
					UnrealMaterialConstant->UpdateStaticPermutation(StaticParameters);
				}
			}
		}
	}


	return UnrealMaterialConstant;
}

void FDazToUnrealMaterials::CorrectDazShaders(FString MaterialName, TMap<FString, TArray<FDUFTextureProperty>>& MaterialProperties)
{
	if (!MaterialProperties.Contains(MaterialName))
	{
		UE_LOG(LogTemp, Warning, TEXT("CorrectDazShaders(): ERROR: MaterialName not found in MaterialProperties: %s"), *MaterialName);
		return;
	}

	TArray<FDUFTextureProperty> Properties = MaterialProperties[MaterialName];
	if (Properties.Num() == 0)
	{
		// This material was likely remapped to a base material and removed
		UE_LOG(LogTemp, Warning, TEXT("CorrectDazShaders(): INFO: MaterialName has no properties: %s"), *MaterialName);
		return;
	}

	FString ShaderName = MaterialProperties[MaterialName][0].ShaderName;
	FString sMaterialAssetName = MaterialProperties[MaterialName][0].MaterialAssetName;

	////////////////////////////////////////////////////////
	// Shader Corrections for specific Daz-Shaders
	////////////////////////////////////////////////////////
	FString sCleanedName = MaterialName.Replace(TEXT("_"), TEXT(""));
	double fGlobalTransparencyCorrection = 10.0;
	if (ShaderName == TEXT("Iray Uber"))
	{
		double fIrayUberTransparencyCorrection = fGlobalTransparencyCorrection + 0.0;
		FString transparencyOffsetCorrection = FString::SanitizeFloat(fIrayUberTransparencyCorrection);

		// 2022-Feb-03 (Qasim B): Transparency Correction for Kent Hair
		if (
			(
				(sCleanedName.Contains(TEXT("KentHair")))
				|| (sMaterialAssetName.Contains(TEXT("CapriHair")))
				|| (sMaterialAssetName.Contains(TEXT("BronwynHair")))
				|| (sMaterialAssetName.Contains(TEXT("PonyKnots")))
				|| (sMaterialAssetName.Contains(TEXT("hair")))
			) 
			&& (!MaterialName.Contains(TEXT("_scalp")))
			&& (!MaterialName.Contains(TEXT("_cap")))
			)
		{
			SetMaterialProperty(MaterialName, TEXT("Transparency Offset"), TEXT("Double"), transparencyOffsetCorrection, MaterialProperties);
			//UE_LOG(LogTemp, Warning, TEXT("Iray Uber shader detected and fixed for material %s"), *MaterialName);
		}
		else if (MaterialName.Contains(TEXT("_scalp")) || MaterialName.Contains(TEXT("_cap")))
		{
			FString scalpFixString = TEXT("0.5");
			SetMaterialProperty(MaterialName, TEXT("Transparency Offset"), TEXT("Double"), scalpFixString, MaterialProperties);
			//UE_LOG(LogTemp, Warning, TEXT("Iray Uber shader detected and fixed for material %s"), *MaterialName);
		}

	}
	if (ShaderName == TEXT("omUberSurface"))
	{
		double fIrayUberTransparencyCorrection = 5.0;
		FString transparencyOffsetCorrection = FString::SanitizeFloat(fIrayUberTransparencyCorrection);

		// 2022-Feb-03 (Qasim B): Transparency Correction for Kent Hair
		if (
			(sMaterialAssetName.Contains(TEXT("hair")))
			&& (!MaterialName.Contains(TEXT("_scalp")))
			&& (!MaterialName.Contains(TEXT("_cap")))
			)
		{
			SetMaterialProperty(MaterialName, TEXT("Transparency Offset"), TEXT("Double"), transparencyOffsetCorrection, MaterialProperties);
			//UE_LOG(LogTemp, Warning, TEXT("Iray Uber shader detected and fixed for material %s"), *MaterialName);
		}

	}
	else if (ShaderName == TEXT("OOT Hairblending Hair"))
	{
		// 2022-Jan-31 (Qasim B): Transparency Correction for OOT Hairblending Hair
		double fOOTTransparencyCorrection = fGlobalTransparencyCorrection + 0.0;
		FString transparencyOffsetCorrection = FString::SanitizeFloat(fOOTTransparencyCorrection);
		SetMaterialProperty(MaterialName, TEXT("Transparency Offset"), TEXT("Double"), transparencyOffsetCorrection, MaterialProperties);
		//UE_LOG(LogTemp, Warning, TEXT("OOT Hairblending shader detected and fixed for material %s"), *MaterialName);

	}
	else if (ShaderName == TEXT("Littlefox Hair Shader"))
	{
		FString transparencyOffsetCorrection = FString::SanitizeFloat(fGlobalTransparencyCorrection);
		SetMaterialProperty(MaterialName, TEXT("Transparency Offset"), TEXT("Double"), transparencyOffsetCorrection, MaterialProperties);

		FString hexCorrection = "";
		bool _materialFound = false;
		// UE_LOG(LogTemp, Warning, TEXT("Executing For: %s"), *MaterialName);
		for (FDUFTextureProperty Property : MaterialProperties[MaterialName])
		{
			// UE_LOG(LogTemp, Warning, TEXT("M: %s P: %s"), *MaterialName, *Property.Name);
			if (Property.Name == TEXT("LLF-BaseColor"))
			{
				hexCorrection = Property.Value;
				SetMaterialProperty(MaterialName, TEXT("Diffuse Color"), TEXT("Color"), hexCorrection, MaterialProperties);
				_materialFound = true;
				break;
			}
		}

	}

	////////////////////////////////////////////////////////
	// Shader Corrections for specific Daz-Materials
	////////////////////////////////////////////////////////
	//
	// Place holder for Material-specific corections
	//

}

void FDazToUnrealMaterials::SetMaterialProperty(const FString& MaterialName, const FString& PropertyName, const FString& PropertyType, const FString& PropertyValue, TMap<FString, TArray<FDUFTextureProperty>>& MaterialProperties)
{
	if (!MaterialProperties.Contains(MaterialName))
	{
		MaterialProperties.Add(MaterialName, TArray<FDUFTextureProperty>());
	}
	TArray<FDUFTextureProperty>& Properties = MaterialProperties[MaterialName];
	for (FDUFTextureProperty& Property : Properties)
	{
		if (Property.Name == PropertyName)
		{
			Property.Value = PropertyValue;
			return;
		}
	}
	FDUFTextureProperty TextureProperty;
	TextureProperty.Name = PropertyName;
	TextureProperty.Type = PropertyType;
	TextureProperty.Value = PropertyValue;
	MaterialProperties[MaterialName].Add(TextureProperty);

}

FSoftObjectPath FDazToUnrealMaterials::GetMostCommonBaseMaterial(TArray<FString> MaterialNames, TMap<FString, TArray<FDUFTextureProperty>> MaterialProperties)
{
	TArray<FSoftObjectPath> BaseMaterials;
	for (FString MaterialName : MaterialNames)
	{
		BaseMaterials.Add(GetBaseMaterial(MaterialName, MaterialProperties[MaterialName]));
	}

	FSoftObjectPath MostCommonPath;
	int32 MostCommonCount = 0;
	for (FSoftObjectPath BaseMaterial : BaseMaterials)
	{
		int32 Count = 0;
		for (FSoftObjectPath BaseMaterialMatch : BaseMaterials)
		{
			if (BaseMaterialMatch == BaseMaterial)
			{
				Count++;
			}
		}
		if (Count > MostCommonCount)
		{
			MostCommonCount = Count;
			MostCommonPath = BaseMaterial;
		}
	}
	return MostCommonPath;
}

TArray<FDUFTextureProperty> FDazToUnrealMaterials::GetMostCommonProperties(TArray<FString> MaterialNames, TMap<FString, TArray<FDUFTextureProperty>> MaterialProperties)
{

	// Get a list of property names
	TArray<FString> PossibleProperties;
	for (FString MaterialName : MaterialNames)
	{
		for (FDUFTextureProperty Property : MaterialProperties[MaterialName])
		{
			if (Property.Name != TEXT("Asset Type"))
			{
				PossibleProperties.AddUnique(Property.Name);
			}
		}
	}

	TArray<FDUFTextureProperty> MostCommonProperties;
	for (FString PossibleProperty : PossibleProperties)
	{
		FDUFTextureProperty MostCommonProperty = GetMostCommonProperty(PossibleProperty, MaterialNames, MaterialProperties);
		if (MostCommonProperty.Name != TEXT(""))
		{
			MostCommonProperties.Add(MostCommonProperty);
		}
	}

	return MostCommonProperties;
}

FDUFTextureProperty FDazToUnrealMaterials::GetMostCommonProperty(FString PropertyName, TArray<FString> MaterialNames, TMap<FString, TArray<FDUFTextureProperty>> MaterialProperties)
{
	TArray<FDUFTextureProperty> PossibleProperties;

	// Only include properties that exist on all the child materials
	int32 PropertyCount = 0;
	for (FString MaterialName : MaterialNames)
	{
		// Gather all the options
		for (FDUFTextureProperty Property : MaterialProperties[MaterialName])
		{
			if (Property.Name == PropertyName)
			{
				PropertyCount++;
				PossibleProperties.Add(Property);
			}
		}
	}


	FDUFTextureProperty MostCommonProperty;
	int32 MostCommonCount = 0;
	if (PropertyCount == MaterialNames.Num())
	{
		for (FDUFTextureProperty PropertyToCount : PossibleProperties)
		{
			int32 Count = 0;
			for (FDUFTextureProperty PropertyToMatch : PossibleProperties)
			{
				if (PropertyToMatch == PropertyToCount)
				{
					Count++;
				}
			}
			if (Count > MostCommonCount)
			{
				MostCommonCount = Count;
				MostCommonProperty = PropertyToCount;
			}
		}
	}

	if (MostCommonCount <= 1)
	{
		MostCommonProperty.Name = TEXT("");
	}
	return MostCommonProperty;
}

bool FDazToUnrealMaterials::HasMaterialProperty(const FString& PropertyName, const  TArray<FDUFTextureProperty>& MaterialProperties)
{
	for (FDUFTextureProperty MaterialProperty : MaterialProperties)
	{
		if (MaterialProperty.Name == PropertyName)
		{
			return true;
		}
	}
	return false;
}
FString FDazToUnrealMaterials::GetMaterialProperty(const FString& PropertyName, const TArray<FDUFTextureProperty>& MaterialProperties)
{
	for (FDUFTextureProperty MaterialProperty : MaterialProperties)
	{
		if (MaterialProperty.Name == PropertyName)
		{
			return MaterialProperty.Value;
		}
	}
	return FString();
}

USubsurfaceProfile* FDazToUnrealMaterials::CreateSubsurfaceBaseProfileForCharacter(const FString CharacterMaterialFolder, TMap<FString, TArray<FDUFTextureProperty>>& MaterialProperties)
{
	const UDazToUnrealSettings* CachedSettings = GetDefault<UDazToUnrealSettings>();

	FString Seperator;
	if ( CachedSettings->UseOriginalMaterialName)
	{
		Seperator = "";
	}
	else
	{
		Seperator = "_";
	}

	// Find the torso material.
	for (TPair<FString, TArray<FDUFTextureProperty>> Pair : MaterialProperties)
	{
		FString AssetType;
		for (FDUFTextureProperty Property : Pair.Value)
		{
			if (Property.Name == TEXT("Asset Type"))
			{
				AssetType = Property.Value;
			}
		}

		if (AssetType == TEXT("Actor/Character"))
		{
			if (Pair.Key.EndsWith(Seperator + TEXT("Torso")) || Pair.Key.EndsWith(Seperator + TEXT("Body")))
			{
				return CreateSubsurfaceProfileForMaterial(Pair.Key, CharacterMaterialFolder, Pair.Value);
			}

		}
	}
	return nullptr;
}

USubsurfaceProfile* FDazToUnrealMaterials::CreateSubsurfaceProfileForMaterial(const FString MaterialName, const FString CharacterMaterialFolder, const TArray<FDUFTextureProperty > MaterialProperties)
{
	// Create the Material Instance
	//auto SubsurfaceProfileFactory = NewObject<USubsurfaceProfileFactory>();

	//Only create for the PBRSkin base material
	FString ShaderName;
	for (FDUFTextureProperty Property : MaterialProperties)
	{
		if (Property.Name == TEXT("Asset Type"))
		{
			ShaderName = Property.ShaderName;
		}
	}
	if (ShaderName != TEXT("PBRSkin"))
	{
		return nullptr;
	}

	FString SubsurfaceProfileName = MaterialName + TEXT("_Profile");
#if ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION < 26
	UPackage* Package = CreatePackage(nullptr, *(CharacterMaterialFolder / MaterialName));
#else
	UPackage* Package = CreatePackage(*(CharacterMaterialFolder / MaterialName));
#endif

	USubsurfaceProfile* SubsurfaceProfile = nullptr;
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION > 0
	FSoftObjectPath SubSurfaceProfilePath(*(CharacterMaterialFolder / SubsurfaceProfileName + TEXT(".") + SubsurfaceProfileName));
	FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(SubSurfaceProfilePath);
#else
	FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(*(CharacterMaterialFolder / SubsurfaceProfileName + TEXT(".") + SubsurfaceProfileName));
#endif
	UObject* Asset = FindObject<UObject>(nullptr, *(CharacterMaterialFolder / SubsurfaceProfileName + TEXT(".") + SubsurfaceProfileName));
	if (AssetData.IsValid())
	{
		SubsurfaceProfile = Cast<USubsurfaceProfile>(AssetData.GetAsset());
	}

	if (SubsurfaceProfile == nullptr)
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");
		SubsurfaceProfile = Cast<USubsurfaceProfile>(AssetToolsModule.Get().CreateAsset(SubsurfaceProfileName, FPackageName::GetLongPackagePath(*(CharacterMaterialFolder / MaterialName)), USubsurfaceProfile::StaticClass(), NULL));
	}
	if (HasMaterialProperty(TEXT("Specular Lobe 1 Roughness"), MaterialProperties))
	{
		SubsurfaceProfile->Settings.Roughness0 = FCString::Atof(*GetMaterialProperty(TEXT("Specular Lobe 1 Roughness"), MaterialProperties));
	}
	if (HasMaterialProperty(TEXT("Specular Lobe 2 Roughness Mult"), MaterialProperties))
	{
		SubsurfaceProfile->Settings.Roughness1 = FCString::Atof(*GetMaterialProperty(TEXT("Specular Lobe 2 Roughness Mult"), MaterialProperties));
	}
	if (HasMaterialProperty(TEXT("Dual Lobe Specular Ratio"), MaterialProperties))
	{
		SubsurfaceProfile->Settings.LobeMix = FCString::Atof(*GetMaterialProperty(TEXT("Dual Lobe Specular Ratio"), MaterialProperties));
	}
	if (HasMaterialProperty(TEXT("SSS Color"), MaterialProperties))
	{
		SubsurfaceProfile->Settings.SubsurfaceColor = FColor::FromHex(*GetMaterialProperty(TEXT("SSS Color"), MaterialProperties));
	}
	if (HasMaterialProperty(TEXT("SSS Color"), MaterialProperties))
	{
		SubsurfaceProfile->Settings.SubsurfaceColor = FColor::FromHex(*GetMaterialProperty(TEXT("SSS Color"), MaterialProperties));
	}
	if (HasMaterialProperty(TEXT("Transmitted Color"), MaterialProperties))
	{
		SubsurfaceProfile->Settings.FalloffColor = FColor::FromHex(*GetMaterialProperty(TEXT("Transmitted Color"), MaterialProperties));
	}
	return SubsurfaceProfile;
}

bool FDazToUnrealMaterials::SubsurfaceProfilesAreIdentical(USubsurfaceProfile* A, USubsurfaceProfile* B)
{
	if (A == nullptr || B == nullptr) return false;
	if (A->Settings.Roughness0 != B->Settings.Roughness0) return false;
	if (A->Settings.Roughness1 != B->Settings.Roughness1) return false;
	if (A->Settings.LobeMix != B->Settings.LobeMix) return false;
	if (A->Settings.SubsurfaceColor != B->Settings.SubsurfaceColor) return false;
	if (A->Settings.FalloffColor != B->Settings.FalloffColor) return false;
	return true;
}

bool FDazToUnrealMaterials::SubsurfaceProfilesWouldBeIdentical(USubsurfaceProfile* ExistingSubsurfaceProfile, const TArray<FDUFTextureProperty > MaterialProperties)
{
	if (ExistingSubsurfaceProfile == nullptr) return false;
	if (ExistingSubsurfaceProfile->Settings.Roughness0 != FCString::Atof(*GetMaterialProperty(TEXT("Specular Lobe 1 Roughness"), MaterialProperties))) return false;
	if (ExistingSubsurfaceProfile->Settings.Roughness1 != FCString::Atof(*GetMaterialProperty(TEXT("Specular Lobe 2 Roughness Mult"), MaterialProperties))) return false;
	if (ExistingSubsurfaceProfile->Settings.LobeMix != FCString::Atof(*GetMaterialProperty(TEXT("Dual Lobe Specular Ratio"), MaterialProperties))) return false;
	if (ExistingSubsurfaceProfile->Settings.SubsurfaceColor != FColor::FromHex(*GetMaterialProperty(TEXT("SSS Color"), MaterialProperties))) return false;
	if (ExistingSubsurfaceProfile->Settings.FalloffColor != FColor::FromHex(*GetMaterialProperty(TEXT("Transmitted Color"), MaterialProperties))) return false;
	return true;
}

// Returns a map of material to the material it's a duplicate of.
TMap<TSharedPtr<FJsonValue>, TSharedPtr<FJsonValue>> FDazToUnrealMaterials::FindDuplicateMaterials(TArray<TSharedPtr<FJsonValue>> MaterialList)
{
	TMap<TSharedPtr<FJsonValue>, TSharedPtr<FJsonValue>> Duplicates;
// JSon Duplicate functions were added in 5.0.  Disabling for UE4 so it can build.
#if ENGINE_MAJOR_VERSION >= 5
	for (int32 i = 0; i < MaterialList.Num(); i++)
	{
		TSharedPtr<FJsonObject> Material = MaterialList[i]->AsObject();
		FString MaterialName = Material->GetStringField(TEXT("Material Name"));
		TSharedPtr<FJsonObject> MaterialCopy = MakeShared<FJsonObject>();
		FJsonObject::Duplicate(Material, MaterialCopy);
		MaterialCopy->RemoveField(TEXT("Material Name"));

		for (int32 j = i + 1; j < MaterialList.Num(); j++)
		{
			TSharedPtr<FJsonObject> CompareMaterial = MaterialList[j]->AsObject();
			FString CompareMaterialName = CompareMaterial->GetStringField(TEXT("Material Name"));
			TSharedPtr<FJsonObject> CompareMaterialCopy = MakeShared<FJsonObject>();
			FJsonObject::Duplicate(CompareMaterial, CompareMaterialCopy);
			CompareMaterialCopy->RemoveField(TEXT("Material Name"));

			if (FJsonValueObject(MaterialCopy) == FJsonValueObject(CompareMaterialCopy) && !Duplicates.Contains(MaterialList[j]))
			{
				Duplicates.Add(MaterialList[j], MaterialList[i]);
				UE_LOG(LogDazToUnrealMaterial, Display, TEXT("Material %s is a duplicate of %s"), *CompareMaterialName, *MaterialName);
			}
		}
	}
#endif
	return Duplicates;
}

FString FDazToUnrealMaterials::GetFriendlyObjectName(FString FbxObjectName, TMap<FString, TArray<FDUFTextureProperty>> MaterialProperties)
{
	// Find the torso material.
	for (TPair<FString, TArray<FDUFTextureProperty>> Pair : MaterialProperties)
	{
		FString AssetType;
		for (FDUFTextureProperty Property : Pair.Value)
		{
			if (Property.MaterialAssetName == FbxObjectName)
			{
				return Property.ObjectName;
			}
		}
	}
	return FString();
}