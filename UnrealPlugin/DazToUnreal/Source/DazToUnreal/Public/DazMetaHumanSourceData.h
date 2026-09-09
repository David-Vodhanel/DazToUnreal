#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"
#include "DazMetaHumanSourceData.generated.h"

// Conversion source data stored in the editor-only AssetUserData array of a converted
// MetaHuman body mesh (GetAssetUserDataEditorOnly — stripped automatically at cook).
// Records what the body-culling workflow needs to stay non-destructive: the pristine
// original mesh, the baked culled copy, the clothing-coverage hide mask and its
// settings, and per-item transparency overrides.
UCLASS(DisplayName = "Daz MetaHuman Source Data")
class DAZTOUNREAL_API UDazMetaHumanSourceData : public UAssetUserData
{
	GENERATED_BODY()

public:
	// The pristine body mesh produced by the conversion. Culling bakes always start from
	// this mesh, so re-baking after an outfit change is idempotent.
	UPROPERTY(EditAnywhere, Category = "DazToUnreal")
	TSoftObjectPtr<class USkeletalMesh> OriginalBodyMesh;

	// The duplicate with covered geometry removed, produced by Bake MetaHuman Body Culling.
	UPROPERTY(EditAnywhere, Category = "DazToUnreal")
	TSoftObjectPtr<class USkeletalMesh> CulledBodyMesh;

	// The MetaHumanCharacter asset this body was exported from.
	UPROPERTY(EditAnywhere, Category = "DazToUnreal")
	FSoftObjectPath MetaHumanCharacter;

	// Clothing-coverage hide mask in body UV space (0 = covered/hidden, 255 = visible).
	// Drives both the material preview params and the bake's geometry removal.
	UPROPERTY(EditAnywhere, Category = "DazToUnreal")
	TSoftObjectPtr<class UTexture2D> HideMaskTexture;

	// Reserved for head bib/collar culling (not used yet).
	UPROPERTY(EditAnywhere, Category = "DazToUnreal")
	TSoftObjectPtr<class UTexture2D> HeadHideMaskTexture;

	// Clothing item labels excluded from the coverage bake (transparent garments must not
	// cull the body beneath them). Populated from the DTU opacity properties; edit here and
	// reconvert to override.
	UPROPERTY(EditAnywhere, Category = "DazToUnreal")
	TArray<FString> TransparentClothingItems;

	// Triangles fully covered by mask values below this are removed (see FHiddenFaceMapSettings).
	UPROPERTY(EditAnywhere, Category = "DazToUnreal")
	float HideMaskMaxCullValue = 0.1f;

	// Mask values above this leave the geometry untouched.
	UPROPERTY(EditAnywhere, Category = "DazToUnreal")
	float HideMaskMinKeepValue = 0.9f;

	// Vertices between the cull and keep thresholds shrink up to this distance (cm) along
	// their inverted normal when the culled mesh is baked.
	UPROPERTY(EditAnywhere, Category = "DazToUnreal")
	float HideMaskMaxShrinkDistance = 0.5f;

	// True while the owning blueprint's Body component points at CulledBodyMesh.
	UPROPERTY(EditAnywhere, Category = "DazToUnreal")
	bool bCullingBaked = false;
};
