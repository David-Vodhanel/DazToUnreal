#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DazToUnrealMaterialPack.generated.h"

/** Permitted spline point types for SplineComponent. */
UENUM(BlueprintType)
enum class EDazMaterialType : uint8
{
	Base,
	Alpha,
	Masked,
	Skin,
	Hair,
	Scalp,
	EyeMoisture,
	Cornea,
	NoDraw
};

USTRUCT(BlueprintType)
struct FDazMaterialMappingInfo
{
	GENERATED_USTRUCT_BODY()

	/** Needs to match the name of the shader from Daz Studio.  Examples: PBRSkin, Iray Uber, Daz Studio Default */
	UPROPERTY(EditAnywhere, Category = "Material Mapping")
	FString DazShaderName;

	/** Type of the material */
	UPROPERTY(EditAnywhere, Category = "Material Mapping")
	EDazMaterialType MaterialType = EDazMaterialType::Base;

	/** Path to the material to use */
	UPROPERTY(EditAnywhere, Category = "Material Mapping", meta = (AllowedClasses = "Material"))
	FSoftObjectPath MaterialPath;

public:
	FDazMaterialMappingInfo();

};

UCLASS(BlueprintType)
class DAZTOUNREAL_API UDazToUnrealMaterialPack : public UDataAsset
{
	GENERATED_UCLASS_BODY()

public:
	/** The materials in this pack */
	UPROPERTY(EditAnywhere, Category = "Material Mapping")
	TArray<FDazMaterialMappingInfo> Materials;

	FSoftObjectPath FindMaterial(FString ShaderName, EDazMaterialType MaterialType);
};