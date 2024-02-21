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
};