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

	if (AssetType == TEXT("Follower/Hair") || AssetType == TEXT("Follower/Attachment/Head/Forehead/Eyebrows"))
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
	else if (AssetType == TEXT("Actor/Character") || AssetType == TEXT("Actor"))
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
			MaterialName.EndsWith(Seperator + TEXT("Genitalia")) ||
			MaterialName.EndsWith(Seperator + TEXT("Mouth Cavity")))
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
	else if (MaterialName.Contains(TEXT("eyebrow"), ESearchCase::IgnoreCase))
	{
		BaseMaterialAssetPath = CachedSettings->FindMaterial(ShaderName, EDazMaterialType::Hair);
	}
	else
	{
		//BaseMaterialAssetPath = CachedSettings->BaseMaterial;

		for (FDUFTextureProperty Property : Properties)
		{
			if (Property.Name == TEXT("Cutout Opacity Texture"))
			{
				BaseMaterialAssetPath = CachedSettings->FindMaterial(ShaderName, EDazMaterialType::Alpha);
				break;
			}
			else if (Property.Name == TEXT("Cutout Opacity") && Property.Value != TEXT("1"))
			{
				BaseMaterialAssetPath = CachedSettings->FindMaterial(ShaderName, EDazMaterialType::Alpha);
				break;
			}
			else if (Property.Name == TEXT("Opacity Strength") && Property.Value != TEXT("1"))
			{
				BaseMaterialAssetPath = CachedSettings->FindMaterial(ShaderName, EDazMaterialType::Alpha);
				break;
			}
			else if (Property.Name == TEXT("Refraction Weight") && Property.Value != TEXT("0"))
			{
				BaseMaterialAssetPath = CachedSettings->FindMaterial(ShaderName, EDazMaterialType::Alpha);
				break;
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

	// DB 2023-May-23: Fix for refraction weight & opacity strength interaction, part 1
	double RefractionWeight = 0.0;
	double OpacityStrength = 1.0;
	double CutoutOpacity = 1.0;
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
			// DB 2023-May-23: Fix for refraction weight & opacity strength interaction, part 2
			else if (Property.Name == TEXT("Refraction Weight"))
			{
				RefractionWeight = FCString::Atod(*Property.Value);
			}
			else if (Property.Name == TEXT("Opacity Strength"))
			{
				OpacityStrength = FCString::Atod(*Property.Value);
			}
			else if (Property.Name == TEXT("Cutout Opacity"))
			{
				CutoutOpacity = FCString::Atod(*Property.Value);
			}
		}
	}
	// DB 2023-May-23: Fix for refraction weight & opacity strength interaction, part 3
	if (RefractionWeight > 0.0 && OpacityStrength >= 1.0)
	{
		OpacityStrength = 1.0 - RefractionWeight;
		if (OpacityStrength <= 0.0)
		{
			OpacityStrength = 0.09;
		}
		SetMaterialProperty(MaterialName, TEXT("Opacity Strength"), TEXT("Double"), FString::SanitizeFloat(OpacityStrength), MaterialProperties);
	} else if (CutoutOpacity <= 1.0 && OpacityStrength >= 1.0)
	{
		SetMaterialProperty(MaterialName, TEXT("Opacity Strength"), TEXT("Double"), FString::SanitizeFloat(CutoutOpacity), MaterialProperties);
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
		SetMaterialProperty(MaterialName, TEXT("Cavity Strength"), TEXT("Double"), FString::SanitizeFloat(CachedSettings->DefaultCavityStrength), MaterialProperties);
	}
	else if (AssetType == TEXT("Actor/Character") || AssetType == TEXT("Actor"))
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
			MaterialName.EndsWith(Seperator + TEXT("Genitalia")) ||
			MaterialName.EndsWith(Seperator + TEXT("Mouth Cavity")))
		{
			SetMaterialProperty(MaterialName, TEXT("Diffuse Subsurface Color Weight"), TEXT("Double"), FString::SanitizeFloat(CachedSettings->DefaultSkinDiffuseSubsurfaceColorWeight), MaterialProperties);
			SetMaterialProperty(MaterialName, TEXT("Subsurface Alpha Texture"), TEXT("Texture"), FDazToUnrealTextures::GetSubSurfaceAlphaTexture(CharacterType, MaterialName), MaterialProperties);
			SetMaterialProperty(MaterialName, TEXT("Cavity Strength"), TEXT("Double"), FString::SanitizeFloat(CachedSettings->DefaultCavityStrength), MaterialProperties);
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
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION > 1
					TArray<FStaticSwitchParameter>& StaticSwitchParameters = StaticParameters.GetRuntime().StaticSwitchParameters;
#elif ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION > 0
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

	const UDazToUnrealSettings* CachedSettings = GetDefault<UDazToUnrealSettings>();
	FString ShaderName = MaterialProperties[MaterialName][0].ShaderName;
	FString sMaterialAssetName = MaterialProperties[MaterialName][0].MaterialAssetName;

	////////////////////////////////////////////////////////
	// Shader Corrections for specific Daz-Shaders
	////////////////////////////////////////////////////////
	FString sCleanedName = MaterialName.Replace(TEXT("_"), TEXT(""));
	double fGlobalTransparencyCorrection = 12.5;
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

	////////////////////////////////////////////////////////
	// PBRSkin Detail Normal Map Corrections
	//
	// Daz sends "Detail Enable" as Double (0/1) but our
	// BasePBRSkinMaterial uses a StaticSwitchParameter.
	// Convert it to Switch type so the import pipeline
	// can activate the detail normal path.
	//
	// Also handle "Detail Normal Map Mode":
	//   Mode 0 = reuse base Normal Map texture (tiled)
	//   Mode 1 = use separate Detail Normal Map texture
	////////////////////////////////////////////////////////
	if (ShaderName == TEXT("PBRSkin") && CachedSettings->bUseGeneratedBaseMaterials)
	{
		TArray<FDUFTextureProperty>& Props = MaterialProperties[MaterialName];

		// Handle Detail Normal Map Mode:
		//   Mode 0 = reuse base Normal Map texture (tiled)
		//   Mode 1 = use separate Detail Normal Map texture
		if (HasMaterialProperty(TEXT("Detail Normal Map Mode"), Props))
		{
			int32 Mode = FCString::Atoi(*GetMaterialProperty(TEXT("Detail Normal Map Mode"), Props));
			if (Mode == 0 && HasMaterialProperty(TEXT("Normal Map Texture"), Props))
			{
				FString BaseNormalTexture = GetMaterialProperty(TEXT("Normal Map Texture"), Props);
				SetMaterialProperty(MaterialName, TEXT("Detail Normal Map Texture"),
					TEXT("Texture"), BaseNormalTexture, MaterialProperties);
			}
		}

		// Enable "Detail Enable" static switch.
		// Daz sends this as Double (0/1) but our material uses a StaticSwitchParameter.
		// Also auto-enable when a Detail Normal Map Texture is present, even if
		// Detail Enable is missing or set to 0 — the texture's existence is the
		// strongest signal that detail normals should be active.
		bool bDetailEnabled = false;
		if (HasMaterialProperty(TEXT("Detail Enable"), Props))
		{
			bDetailEnabled = FCString::Atof(*GetMaterialProperty(TEXT("Detail Enable"), Props)) > 0.5;
		}
		if (!bDetailEnabled && HasMaterialProperty(TEXT("Detail Normal Map Texture"), Props))
		{
			bDetailEnabled = true;
		}
		SetMaterialProperty(MaterialName, TEXT("Detail Enable"), TEXT("Switch"),
			bDetailEnabled ? TEXT("true") : TEXT("false"), MaterialProperties);

		// Convert "Makeup Enable" from Double (0/1) to Switch (true/false)
		if (HasMaterialProperty(TEXT("Makeup Enable"), Props))
		{
			bool bEnabled = FCString::Atof(*GetMaterialProperty(TEXT("Makeup Enable"), Props)) > 0.5;
			SetMaterialProperty(MaterialName, TEXT("Makeup Enable"), TEXT("Switch"),
				bEnabled ? TEXT("true") : TEXT("false"), MaterialProperties);
		}

		// Convert "Top Coat Enable" from Double (0/1) to Switch (true/false)
		if (HasMaterialProperty(TEXT("Top Coat Enable"), Props))
		{
			bool bEnabled = FCString::Atof(*GetMaterialProperty(TEXT("Top Coat Enable"), Props)) > 0.5;
			SetMaterialProperty(MaterialName, TEXT("Top Coat Enable"), TEXT("Switch"),
				bEnabled ? TEXT("true") : TEXT("false"), MaterialProperties);
		}

		// Convert "Specular Occlusion Enable" from Double (0/1) to Switch (true/false)
		if (HasMaterialProperty(TEXT("Specular Occlusion Enable"), Props))
		{
			bool bEnabled = FCString::Atof(*GetMaterialProperty(TEXT("Specular Occlusion Enable"), Props)) > 0.5;
			SetMaterialProperty(MaterialName, TEXT("Specular Occlusion Enable"), TEXT("Switch"),
				bEnabled ? TEXT("true") : TEXT("false"), MaterialProperties);
		}

		// Auto-set Specular Detail Range based on whether a spec weight texture exists.
		// PBRSkin characters like Laura 9 have a "Dual Lobe Specular Weight Texture"
		// (e.g. Skin_SLW.jpg) that provides roughness variation. When present, set
		// range to 0.6 so the centered spec offset contributes to roughness.
		bool bHasSpecWeightTexture = HasMaterialProperty(TEXT("Dual Lobe Specular Weight Texture"), Props);
		SetMaterialProperty(MaterialName, TEXT("Specular Detail Range"),
			TEXT("Double"), bHasSpecWeightTexture ? TEXT("0.6") : TEXT("0.0"), MaterialProperties);

		// Set "bHasDetailRoughnessTexture" StaticSwitch based on whether a
		// "Detail Specular Roughness Mult Texture" exists in the CSV data.
		// Clara 8.1 ships Skin_MicroR.jpg; Victoria/Laura/Calypso do not.
		// When true: detail roughness texture is multiplied into base roughness.
		// When false: XY magnitude from detail normal map is used additively.
		bool bHasDetailRoughTex = HasMaterialProperty(TEXT("Detail Specular Roughness Mult Texture"), Props);
		SetMaterialProperty(MaterialName, TEXT("bHasDetailRoughnessTexture"), TEXT("Switch"),
			bHasDetailRoughTex ? TEXT("true") : TEXT("false"), MaterialProperties);

		// Set "bHasSpecularWeightTexture" StaticSwitch based on whether a
		// "Dual Lobe Specular Weight Texture" exists. Laura and Clara ship
		// texture maps (Skin_SLW.jpg, SkinSW.jpg); Victoria/Calypso use scalar only.
		// When true: specular = tex × scalar weight × reflectivity.
		// When false: specular = scalar weight × reflectivity.
		SetMaterialProperty(MaterialName, TEXT("bHasSpecularWeightTexture"), TEXT("Switch"),
			bHasSpecWeightTexture ? TEXT("true") : TEXT("false"), MaterialProperties);
	}

	////////////////////////////////////////////////////////
	// Iray Uber Roughness Corrections
	//
	// 1. Roughness Squared: Daz squares roughness values before GGX evaluation
	//    when this flag is set. We square the scalar here during import so the
	//    material graph receives the correct value.
	//
	// 2. Specular Detail Blend: When a specular texture exists (Dual Lobe or
	//    Glossy), set blend high so the inverted spec texture drives roughness
	//    variation. When no spec texture, set blend to 0 for flat scalar fallback.
	////////////////////////////////////////////////////////
	if (ShaderName == TEXT("Iray Uber") && CachedSettings->bUseGeneratedBaseMaterials)
	{
		TArray<FDUFTextureProperty>& Props = MaterialProperties[MaterialName];

		// Square roughness scalars when Roughness Squared is enabled
		bool bRoughnessSquared = false;
		if (HasMaterialProperty(TEXT("Roughness Squared"), Props))
		{
			bRoughnessSquared = FCString::Atof(*GetMaterialProperty(TEXT("Roughness Squared"), Props)) > 0.5;
		}
		if (bRoughnessSquared)
		{
			// Square Specular Lobe 1 Roughness
			if (HasMaterialProperty(TEXT("Specular Lobe 1 Roughness"), Props))
			{
				double Roughness = FCString::Atod(*GetMaterialProperty(TEXT("Specular Lobe 1 Roughness"), Props));
				Roughness = Roughness * Roughness;
				SetMaterialProperty(MaterialName, TEXT("Specular Lobe 1 Roughness"),
					TEXT("Double"), FString::SanitizeFloat(Roughness), MaterialProperties);
			}
			// Square Glossy Roughness
			if (HasMaterialProperty(TEXT("Glossy Roughness"), Props))
			{
				double Roughness = FCString::Atod(*GetMaterialProperty(TEXT("Glossy Roughness"), Props));
				Roughness = Roughness * Roughness;
				SetMaterialProperty(MaterialName, TEXT("Glossy Roughness"),
					TEXT("Double"), FString::SanitizeFloat(Roughness), MaterialProperties);
			}
		}

		// Auto-set Specular Detail Range based on whether a specular texture exists.
		// When a spec texture is present, it provides centered variation around the
		// scalar roughness (bright spec = smoother, dark = rougher). Without a texture,
		// set range to 0 so the scalar roughness is used directly.
		// Gen 8 (Rebekah):  texture on "Dual Lobe Specular Weight"
		// Gen 8+/9 (Kei):   texture on "Dual Lobe Specular Reflectivity"
		// Gen 3 (Vic 7):    texture on "Glossy Layered Weight"
		bool bHasSpecTexture = HasMaterialProperty(TEXT("Dual Lobe Specular Reflectivity Texture"), Props)
		                    || HasMaterialProperty(TEXT("Dual Lobe Specular Weight Texture"), Props)
		                    || HasMaterialProperty(TEXT("Glossy Layered Weight Texture"), Props);
		SetMaterialProperty(MaterialName, TEXT("Specular Detail Range"),
			TEXT("Double"), bHasSpecTexture ? TEXT("0.6") : TEXT("0.0"), MaterialProperties);

		// Set "bHasDetailNormalTexture" based on normal map texture presence.
		// When true, normal-derived roughness micro-detail is used (Dot curvature).
		// When false, falls through to bump map fallback if available.
		bool bHasNormalTex = HasMaterialProperty(TEXT("Normal Map Texture"), Props);
		SetMaterialProperty(MaterialName, TEXT("bHasDetailNormalTexture"), TEXT("Switch"),
			bHasNormalTex ? TEXT("true") : TEXT("false"), MaterialProperties);

		// Set "bHasBumpTexture" based on bump texture presence.
		// When true and no normal map exists, bump deviation from midpoint
		// drives roughness micro-detail (pore/wrinkle breakup).
		bool bHasBumpTex = HasMaterialProperty(TEXT("Bump Strength Texture"), Props);
		SetMaterialProperty(MaterialName, TEXT("bHasBumpTexture"), TEXT("Switch"),
			bHasBumpTex ? TEXT("true") : TEXT("false"), MaterialProperties);
	}

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
			Property.Type = PropertyType;
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

		if (AssetType == TEXT("Actor/Character") || AssetType == TEXT("Actor"))
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

	// Only create for skin shaders (PBRSkin and Iray Uber)
	FString ShaderName;
	for (FDUFTextureProperty Property : MaterialProperties)
	{
		if (Property.Name == TEXT("Asset Type"))
		{
			ShaderName = Property.ShaderName;
		}
	}
	if (ShaderName != TEXT("PBRSkin") && ShaderName != TEXT("Iray Uber"))
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
	// Dual specular lobe parameters.
	// Daz's Iray path-tracer roughness values don't map meaningfully to UE's
	// dual-lobe GGX system — the rendering models are too different. Use fixed
	// values close to UE's defaults (Roughness0=0.75, Roughness1=1.30, LobeMix=0.85).
	// Per-character roughness variation is handled by the material graph Roughness pin
	// (Lerp remap, specular detail textures, normal-derived roughness).
	// UE clamps: Roughness0/1 to [0.5, 2.0], LobeMix to [0.1, 0.9].
	SubsurfaceProfile->Settings.Roughness0 = 0.75f;
	SubsurfaceProfile->Settings.Roughness1 = 1.20f;
	SubsurfaceProfile->Settings.LobeMix = 0.85f;
	if (HasMaterialProperty(TEXT("SSS Color"), MaterialProperties))
	{
		SubsurfaceProfile->Settings.SubsurfaceColor = FColor::FromHex(*GetMaterialProperty(TEXT("SSS Color"), MaterialProperties));
	}
	if (HasMaterialProperty(TEXT("Transmitted Color"), MaterialProperties))
	{
		SubsurfaceProfile->Settings.FalloffColor = FColor::FromHex(*GetMaterialProperty(TEXT("Transmitted Color"), MaterialProperties));
	}
	// ScatterRadius must be > 0 for subsurface scattering to be visible in UE.
	// Daz's "Scattering Measurement Distance" (0.015 cm) uses a different SSS model
	// and is too small for UE's world-scale units. Use a fixed value that gives
	// natural skin translucency under typical UE lighting.
	SubsurfaceProfile->Settings.ScatterRadius = 1.2f;
	SubsurfaceProfile->Settings.IOR = 1.4f;
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
	// Roughness0, Roughness1, LobeMix are now fixed values — only SSS colors vary per material.
	if (ExistingSubsurfaceProfile->Settings.SubsurfaceColor != FColor::FromHex(*GetMaterialProperty(TEXT("SSS Color"), MaterialProperties))) return false;
	if (ExistingSubsurfaceProfile->Settings.FalloffColor != FColor::FromHex(*GetMaterialProperty(TEXT("Transmitted Color"), MaterialProperties))) return false;
	return true;
}

// Primary body part names that should be preferred when combining duplicate
// materials.  When two identical materials are found and one matches a primary
// name, it becomes the surviving "original" regardless of DTU ordering.
static bool IsPrimaryBodyPart(const FString& MaterialName)
{
	static const FString PrimaryParts[] = {
		TEXT("Head"), TEXT("Face"), TEXT("Body"), TEXT("Torso"),
		TEXT("Arms"), TEXT("Forearms"), TEXT("Hands"), TEXT("Legs"),
		TEXT("Feet"), TEXT("Hips"), TEXT("Neck"), TEXT("Shoulders"),
		TEXT("Ears"), TEXT("Lips"), TEXT("EyeSocket"),
	};
	for (const FString& Part : PrimaryParts)
	{
		if (MaterialName.EndsWith(Part)) return true;
	}
	return false;
}

// Returns a map of material to the material it's a duplicate of.
TMap<TSharedPtr<FJsonValue>, TSharedPtr<FJsonValue>> FDazToUnrealMaterials::FindDuplicateMaterials(TArray<TSharedPtr<FJsonValue>> MaterialList)
{
	TMap<TSharedPtr<FJsonValue>, TSharedPtr<FJsonValue>> Duplicates;
// JSon Duplicate functions were added in 5.0.  Disabling for UE4 so it can build.
#if ENGINE_MAJOR_VERSION >= 5
	for (int32 i = 0; i < MaterialList.Num(); i++)
	{
		if (Duplicates.Contains(MaterialList[i])) continue;

		TSharedPtr<FJsonObject> Material = MaterialList[i]->AsObject();
		FString MaterialName = Material->GetStringField(TEXT("Material Name"));
		TSharedPtr<FJsonObject> MaterialCopy = MakeShared<FJsonObject>();
		FJsonObject::Duplicate(Material, MaterialCopy);
		MaterialCopy->RemoveField(TEXT("Material Name"));

		for (int32 j = i + 1; j < MaterialList.Num(); j++)
		{
			if (Duplicates.Contains(MaterialList[j])) continue;

			TSharedPtr<FJsonObject> CompareMaterial = MaterialList[j]->AsObject();
			FString CompareMaterialName = CompareMaterial->GetStringField(TEXT("Material Name"));
			TSharedPtr<FJsonObject> CompareMaterialCopy = MakeShared<FJsonObject>();
			FJsonObject::Duplicate(CompareMaterial, CompareMaterialCopy);
			CompareMaterialCopy->RemoveField(TEXT("Material Name"));

			if (FJsonValueObject(MaterialCopy) == FJsonValueObject(CompareMaterialCopy))
			{
				// Prefer primary body part names as the surviving original.
				// e.g. "Head" survives over "Mouth Cavity" regardless of DTU order.
				bool bIPrimary = IsPrimaryBodyPart(MaterialName);
				bool bJPrimary = IsPrimaryBodyPart(CompareMaterialName);

				if (!bIPrimary && bJPrimary)
				{
					// j is the better name — make i the duplicate of j
					Duplicates.Add(MaterialList[i], MaterialList[j]);
					UE_LOG(LogDazToUnrealMaterial, Display, TEXT("Material %s is a duplicate of %s"), *MaterialName, *CompareMaterialName);
					break; // i is now a duplicate, stop comparing it
				}
				else
				{
					Duplicates.Add(MaterialList[j], MaterialList[i]);
					UE_LOG(LogDazToUnrealMaterial, Display, TEXT("Material %s is a duplicate of %s"), *CompareMaterialName, *MaterialName);
				}
			}
		}
	}
#endif
	return Duplicates;
}

// Returns a duplicate mapping of all materials to one material so there will only be one material slot.
TMap<TSharedPtr<FJsonValue>, TSharedPtr<FJsonValue>> FDazToUnrealMaterials::CombineToOneMaterial(TArray<TSharedPtr<FJsonValue>> MaterialList)
{
	TMap<TSharedPtr<FJsonValue>, TSharedPtr<FJsonValue>> Duplicates;
	for (int32 i = 1; i < MaterialList.Num(); i++)
	{
		Duplicates.Add(MaterialList[i], MaterialList[0]);
	}
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
