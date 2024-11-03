#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DazToUnrealBlueprintUtils.generated.h"

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

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION > 3
		static void SetBoneOrientation(class USkeletonModifier* Modifier, FName BoneName, FQuat Quat);
		static void AdditiveBoneOrientation(class USkeletonModifier* Modifier, FName BoneName, FQuat Quat);
		static void CopyBoneOrientation(class USkeletonModifier* Modifier, FName BoneNameToSet, FName BoneToCopy);

		static void SetBoneTransform(class USkeletalMesh* SkeletalMesh, FReferenceSkeletonModifier& RefSkelModifier, FName BoneName, FTransform NewTransform);

		static const FTransform GetGlobalTransform(const FReferenceSkeleton& RefSkeleton, const uint32 BoneIndex);

		static void AlignBone(class USkeletonModifier* Modifier, FName Parent, FName Child, FVector AlignmentAxis);

		static void UpdateReferencePose(class USkeletalMesh* SkeletalMesh, FName BoneName, FVector AdditiveRotation);
#endif
};