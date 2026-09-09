#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DazToUnrealBlueprintUtils.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogDazToUnrealBlueprintUtils, Log, All);

UCLASS()
class UDazToUnrealBlueprintUtils : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

		// Get the list of bones in an IKRig
		UFUNCTION(BlueprintCallable, Category = "DazToUnrealUtils")
		static TArray<FName> GetBoneList(const UObject* IKRigObject);

		// Find the first child bone.  Used for IKRetargeter generation
		UFUNCTION(BlueprintCallable, Category = "DazToUnrealUtils")
		static FName GetChildBone(const class USkeleton* Skeleton, FName ParentBone);

		// Find the next bone in a chain.  Used for IKRetargeter generation
		UFUNCTION(BlueprintCallable, Category = "DazToUnrealUtils")
		static FName GetNextBone(const class USkeleton* Skeleton, FName ParentBone, FName EndBone);

		// Find the joint bone.  This is assumed to be the bone between two bones
		UFUNCTION(BlueprintCallable, Category = "DazToUnrealUtils")
		static FName GetJointBone(const class USkeleton* Skeleton, FName StartBone, FName EndBone);

		// Convert a Daz character to use the Epic Skeleton
		UFUNCTION(BlueprintCallable, Category = "DazToUnrealUtils")
		static void ConvertToEpicSkeleton(class USkeletalMesh* SkeletalMesh, class USkeletalMesh* TargetEpicSkeleton);

		// Duplicate the live body mesh of a MetaHumanCharacter open for edit into a standalone
		// skeletal mesh asset at AssetPathAndName (e.g. /Game/Path/Name). UE 5.8+ only; returns
		// nullptr on earlier engines or failure. Takes UObject to keep MetaHuman types out of this header.
		UFUNCTION(BlueprintCallable, Category = "DazToUnrealUtils")
		static class USkeletalMesh* ExtractMetaHumanBodyMesh(UObject* MetaHumanCharacter, const FString& AssetPathAndName);

		// Merge the MetaHumanCharacter's face and body into one skeletal mesh asset on the body
		// skeleton at AssetPathAndName (e.g. /Game/Path/Name). UE 5.8+ only; returns nullptr on
		// earlier engines or failure. Takes UObject to keep MetaHuman types out of this header.
		UFUNCTION(BlueprintCallable, Category = "DazToUnrealUtils")
		static class USkeletalMesh* CreateMetaHumanCombinedMesh(UObject* MetaHumanCharacter, const FString& AssetPathAndName);

		// Assemble a placeable character blueprint from the exported MetaHuman head + body meshes:
		// duplicates the actor template from settings (MetaHumanActorTemplate, needs components
		// named Face and Body) and assigns the meshes, mirroring the MetaHuman pipeline's
		// UpdateActorBlueprint. UE 5.8+ only; returns the UBlueprint or nullptr on failure.
		UFUNCTION(BlueprintCallable, Category = "DazToUnrealUtils")
		static UObject* AssembleMetaHumanCharacterBlueprint(class USkeletalMesh* FaceMesh, class USkeletalMesh* BodyMesh, const FString& BlueprintPathAndName);

		// Sync Clothing_* skeletal mesh components on a character blueprint produced by
		// AssembleMetaHumanCharacterBlueprint: one child component of Body per clothing mesh
		// (the template's construction script leader-poses Body children automatically), reusing
		// components on reconvert and removing Clothing_* components whose mesh is gone. UE 5.8+
		// only; returns false if the blueprint or its Body component can't be resolved.
		UFUNCTION(BlueprintCallable, Category = "DazToUnrealUtils")
		static bool AttachClothingToCharacterBlueprint(UObject* CharacterBlueprint, const TArray<class USkeletalMesh*>& ClothingMeshes);

		// Apply MetaHuman skin materials (with locally-synthesized textures) to the exported
		// head + body meshes. Estimates the skin tone from DazSourceMesh's face diffuse texture,
		// applies it to the character, generates the character's materials/textures, persists
		// them under TextureExportPath, and assigns them to matching material slots on the
		// meshes. All local — no cloud calls. UE 5.8+ only; returns false on failure.
		UFUNCTION(BlueprintCallable, Category = "DazToUnrealUtils")
		static bool ApplyMetaHumanSkinMaterials(UObject* MetaHumanCharacter, class USkeletalMesh* HeadMesh, class USkeletalMesh* BodyMesh, class USkeletalMesh* DazSourceMesh, const FString& TextureExportPath);

		// Store (find-or-add, full overwrite) UDazMetaHumanSourceData in BodyMesh's editor-only
		// asset user data — the non-destructive record the body-culling workflow reads. Stripped
		// automatically at cook. Returns false if BodyMesh is null.
		UFUNCTION(BlueprintCallable, Category = "DazToUnrealUtils")
		static bool SetMetaHumanBodySourceData(class USkeletalMesh* BodyMesh, class USkeletalMesh* OriginalBodyMesh, const FString& MetaHumanCharacterPath, class UTexture2D* HideMaskTexture, const TArray<FString>& TransparentClothingItems, float MaxCullValue = 0.1f, float MinKeepValue = 0.9f, float MaxShrinkDistance = 0.5f);

		// Turn a baked clothing-coverage texture (white = covered by clothing, black = bare) into
		// a hide-mask texture asset at AssetPathAndName (0 = covered/hidden, 255 = visible):
		// threshold, erode the covered region by ErodePixels (a safety margin so borderline
		// geometry survives), invert, then blur by BlurPixels to create the cull-to-keep
		// transition band that drives shrinking. Returns nullptr on failure.
		UFUNCTION(BlueprintCallable, Category = "DazToUnrealUtils")
		static class UTexture2D* CreateHideMaskFromCoverage(class UTexture2D* CoverageTexture, const FString& AssetPathAndName, int32 ErodePixels = 4, int32 BlurPixels = 4);

		// Gap-aware clothing coverage: rasterize the body's UV charts and raycast each texel
		// along the body normal (starting InwardOffset INSIDE the skin, to catch garments the
		// conform swallowed) against the merged clothing mesh. Writes per-texel GAP DISTANCE
		// (0..MaxGapDistance encoded 0..254; 255 = bare skin) and rejects grazing hits whose
		// |dot(clothing normal, body normal)| < MinFacingDot — cloth passing edge-on near skin
		// (inner thigh vs. a bikini crotch panel) no longer claims it. Returns a TRANSIENT
		// texture (source data only) for CreateHideMaskFromGapCoverage, or nullptr.
		UFUNCTION(BlueprintCallable, Category = "DazToUnrealUtils")
		static class UTexture2D* BakeClothingGapCoverage(class UDynamicMesh* BodyBakeMesh, class UDynamicMesh* ClothingMesh, int32 Resolution = 1024, float MaxGapDistance = 8.0f, float MinFacingDot = 0.3f, float InwardOffset = 2.0f);

		// Turn a gap coverage texture (BakeClothingGapCoverage) into a hide-mask texture asset
		// at AssetPathAndName (0 = hidden, 255 = visible). A covered texel culls only when its
		// chamfer distance (px) from the nearest bare texel exceeds a gap-scaled margin:
		// ErodePixels + GapErodePixelsPerCm * gap_cm — skin hugged by cloth culls to a slim
		// tuck border, skin under loose cloth (armhole/leg openings) keeps a fat band of live
		// geometry so sightlines into the opening never hit a hole. Blur then builds the
		// cull-to-keep transition band that drives vertex shrinking. Returns nullptr on failure.
		UFUNCTION(BlueprintCallable, Category = "DazToUnrealUtils")
		static class UTexture2D* CreateHideMaskFromGapCoverage(class UTexture2D* GapCoverageTexture, const FString& AssetPathAndName, float MaxGapDistance = 8.0f, int32 ErodePixels = 8, float GapErodePixelsPerCm = 4.0f, int32 BlurPixels = 4);

		// Bake the per-material-ID Daz diffuse set onto TargetBakeMesh's UV layout with a
		// FACING-FILTERED mesh-to-mesh correspondence (replaces the engine's
		// GeometryScript BakeTexture for the likeness bake). Per rasterized texel, both a
		// facing-filtered ray along +/- the smooth texel normal and the nearest
		// facing-compatible source point (dot >= MinFacingDot, within ProjectionDistance)
		// are queried, and the ray wins whenever its hit lands within max(3x the
		// nearest-point distance, PreferRayDistance): plate-on-skin detail (a nail plate
		// sits mm ABOVE fingertip skin) and laterally-drifted spans (mid-calf) are only
		// correct ALONG the normal, while a neighbouring finger's far wall — several
		// times farther than the own-skin nearest point — stays rejected.
		// PreferRayDistance <= 0 disables the ray preference (nearest-point-first).
		// Adjacent fingers/toes and nail plates face the wrong way and
		// are rejected instead of bleeding across the gap. Sampling is footprint-filtered:
		// each texel's source-UV gradient picks a level from a software mip chain — one
		// bilinear tap under the 2-4x source-vs-target texel density mismatch aliases the
		// skin pore/freckle grid into moire stripe bands (calves/back, 2026-08-09).
		// Texels with no valid correspondence are filled from their nearest baked
		// neighbour in UV space (never black), and the same fill dilates a GutterSize ring
		// past chart borders. Sources: SourceMesh triangles carry material IDs indexing
		// MaterialIDTextures (null entries are treated as misses and neighbour-filled);
		// source UVs must be pre-normalized into [0,1] per material (see
		// _normalize_udim_tiles). Returns a TRANSIENT sRGB texture for the caller to
		// persist, or nullptr. UE 5.8+ only.
		UFUNCTION(BlueprintCallable, Category = "DazToUnrealUtils")
		static class UTexture2D* BakeBasecolorFacingFiltered(class UDynamicMesh* TargetBakeMesh, class UDynamicMesh* SourceMesh, const TArray<class UTexture2D*>& MaterialIDTextures, int32 Resolution = 4096, float ProjectionDistance = 2.5f, float PreferRayDistance = 0.8f, float MinFacingDot = 0.5f, int32 GutterSize = 16);

		// Apply the hide-mask material params (HideMaskTexture/MaxCullValue/MinKeepValue/
		// MaxShrinkDistance) to every material instance on BodyMesh's body* slots — the
		// non-destructive editor/PIE preview of the culling. Pass a null texture to clear
		// (flat white + cull 0 = nothing hidden). Returns false if no body slot was found.
		UFUNCTION(BlueprintCallable, Category = "DazToUnrealUtils")
		static bool SetBodyHideMaskMaterialParams(class USkeletalMesh* BodyMesh, class UTexture2D* HideMaskTexture, float MaxCullValue = 0.1f, float MinKeepValue = 0.9f, float MaxShrinkDistance = 0.5f);

		// Bake the hide-mask culling into cookable geometry: duplicate the ORIGINAL body mesh to
		// <name>_Culled, remove/shrink covered geometry on every LOD's body* sections, repoint the
		// blueprint's Body component at the culled mesh, and clear the preview material params
		// (the geometry is gone — double-culling and cooking the mask would be wasted). Reads and
		// updates the UDazMetaHumanSourceData on the body mesh. Idempotent: always starts from the
		// original mesh. UE 5.8+ only; returns false on failure.
		UFUNCTION(BlueprintCallable, Category = "DazToUnrealUtils")
		static bool BakeMetaHumanBodyCulling(UObject* CharacterBlueprint);

		// Reverse BakeMetaHumanBodyCulling: repoint the blueprint's Body component at the original
		// body mesh and reapply the preview material params from the stored source data. The culled
		// mesh is left on disk (unreferenced, so it no longer cooks). UE 5.8+ only.
		UFUNCTION(BlueprintCallable, Category = "DazToUnrealUtils")
		static bool RestoreMetaHumanOriginalBody(UObject* CharacterBlueprint);

		// Build a follicle mask texture asset at AssetPathAndName from the given grooms — the
		// per-channel root-density map the MetaHuman head shader's 'Hair Mask' input reads to
		// darken scalp skin under hair (official assembly channel mapping: Hair=0/R,
		// Eyebrows=1/G, Beard+Mustache=2/B). GroomAssets and Channels are parallel arrays;
		// each strand root is splatted with a RootRadius-pixel kernel and lower mips are
		// blurred (TMGS_Blur5), matching the official MetaHuman build. Safe to re-run at the
		// same path: the source data is fully re-initialized. UE 5.8+ only; returns nullptr
		// on failure.
		UFUNCTION(BlueprintCallable, Category = "DazToUnrealUtils")
		static class UTexture2D* CreateGroomFollicleMask(const TArray<UObject*>& GroomAssets, const TArray<int32>& Channels, const FString& AssetPathAndName, int32 Resolution = 4096, int32 RootRadius = 8);

		// Fold follicle-mask scalp darkening into a baked basecolor texture, writing the result
		// to a NEW texture asset at AssetPathAndName (the input texture is never modified, so
		// re-running always starts clean). Needed because the head shader's 'Use Texture
		// Override' branch — which the Daz likeness bake uses — bypasses the material's own
		// 'Hair Mask' darkening; official MetaHuman assembly solves this the same way by baking
		// the follicle map into the face textures. Per texel: rgb *= lerp(1, ChannelMultiply,
		// mask_channel * Strength) for R=hair, G=eyebrows, B=beard. The mask's mip 0 holds
		// hard per-root dots, so the darkening alpha is max(dot, broad coverage), where broad
		// coverage is a blurred low-res average of the dots scaled by BroadCoverageGain —
		// this merges dense root areas into a continuous scalp shadow (the shader path gets
		// the same effect from the mask's blurred lower mips, which a CPU mip-0 read misses).
		// UE 5.8+ only; returns nullptr on failure.
		UFUNCTION(BlueprintCallable, Category = "DazToUnrealUtils")
		static class UTexture2D* BakeFollicleMaskIntoTexture(class UTexture2D* BaseColorTexture, class UTexture2D* FollicleMaskTexture, const FString& AssetPathAndName, FLinearColor HairMultiply, FLinearColor EyebrowsMultiply, FLinearColor BeardMultiply, float Strength = 1.0f, float BroadCoverageGain = 8.0f);

		// Dump the per-wardrobe-item overridden instance parameters of a MetaHuman character
		// (hair color: Melanin/Redness/DyeColor/ombre/regions/highlights, authored in the MHC
		// editor's Hair panel) as JSON:
		//   {"entries":[{"item":"<item path debug string>","params":{"Melanin":0.3,"DyeColor":[r,g,b,a],...}}]}
		// Only values the user actually changed exist in the bags — anything absent is at the
		// groom material's own default, which is also what the MHC preview shows. Accepts a
		// UMetaHumanCharacter, UMetaHumanCollection, or UMetaHumanInstance. The property bag
		// itself is not python-accessible, hence the JSON hop. UE 5.8+ only; returns "{}" on
		// failure.
		UFUNCTION(BlueprintCallable, Category = "DazToUnrealUtils")
		static FString GetMetaHumanInstanceParametersJson(UObject* CharacterOrCollectionOrInstance);

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION > 3
		static void SetBoneOrientation(class USkeletonModifier* Modifier, FName BoneName, FQuat Quat);
		static void AdditiveBoneOrientation(class USkeletonModifier* Modifier, FName BoneName, FQuat Quat);
		static void CopyBoneOrientation(class USkeletonModifier* Modifier, FName BoneNameToSet, FName BoneToCopy);

		static void SetBoneTransform(class USkeletalMesh* SkeletalMesh, FReferenceSkeletonModifier& RefSkelModifier, FName BoneName, FTransform NewTransform);

		static const FTransform GetGlobalTransform(const FReferenceSkeleton& RefSkeleton, const uint32 BoneIndex);

		static void AlignBone(class USkeletonModifier* Modifier, FName Parent, FName Child, FVector AlignmentAxis);

		static void FixBoneOffset(class USkeletonModifier* Modifier, FName Parent, FName BoneToFix, FVector ForwardAxis);

		static void UpdateReferencePose(class USkeletalMesh* SkeletalMesh, FName BoneName, FVector AdditiveRotation);
#endif
};