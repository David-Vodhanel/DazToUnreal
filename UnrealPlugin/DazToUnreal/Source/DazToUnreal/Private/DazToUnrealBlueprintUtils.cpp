#include "DazToUnrealBlueprintUtils.h"
#include "ReferenceSkeleton.h"
#include "Animation/Skeleton.h"

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION == 2
#include "IKRigDefinition.h"
#endif

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION > 2
#include "Rig/IKRigDefinition.h"
#endif


UDazToUnrealBlueprintUtils::UDazToUnrealBlueprintUtils(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

TArray<FName> UDazToUnrealBlueprintUtils::GetBoneList(const UObject* IKRigObject)
{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 2
	if (const UIKRigDefinition* IKRig = Cast<UIKRigDefinition>(IKRigObject))
	{
		return IKRig->GetSkeleton().BoneNames;
	}
#endif
	return TArray<FName>();
}


FName UDazToUnrealBlueprintUtils::GetChildBone(const USkeleton* Skeleton, FName ParentBone)
{
	if (Skeleton)
	{
		int32 ParentBoneIndex = Skeleton->GetReferenceSkeleton().FindBoneIndex(ParentBone);
		if (ParentBoneIndex != INDEX_NONE)
		{
			TArray<int32> ChildBoneIndexes;
			Skeleton->GetChildBones(ParentBoneIndex, ChildBoneIndexes);
			for (int32 ChildBoneIndex : ChildBoneIndexes)
			{
				return Skeleton->GetReferenceSkeleton().GetBoneName(ChildBoneIndex);
			}
		}
	}
	return NAME_None;
}

FName UDazToUnrealBlueprintUtils::GetNextBone(const class USkeleton* Skeleton, FName StartBone, FName EndBone)
{
	if (Skeleton)
	{
		int32 ParentBoneIndex = Skeleton->GetReferenceSkeleton().FindBoneIndex(StartBone);
		if (ParentBoneIndex == INDEX_NONE) return NAME_None;

		TArray<int32> ChildBoneIndexes;
		Skeleton->GetChildBones(ParentBoneIndex, ChildBoneIndexes);
		for (int32 ChildBoneIndex : ChildBoneIndexes)
		{
			FName ChildBoneName = Skeleton->GetReferenceSkeleton().GetBoneName(ChildBoneIndex);
			if (ChildBoneName == EndBone) return EndBone;
			FName JointBoneName = GetNextBone(Skeleton, ChildBoneName, EndBone);
			if (JointBoneName != NAME_None) return ChildBoneName;
		}

	}
	return NAME_None;
}

FName UDazToUnrealBlueprintUtils::GetJointBone(const class USkeleton* Skeleton, FName StartBone, FName EndBone)
{
	if (Skeleton)
	{
		int32 ParentBoneIndex = Skeleton->GetReferenceSkeleton().FindBoneIndex(StartBone);
		if (ParentBoneIndex == INDEX_NONE) return NAME_None;

		TArray<int32> ChildBoneIndexes;
		Skeleton->GetChildBones(ParentBoneIndex, ChildBoneIndexes);
		for (int32 ChildBoneIndex : ChildBoneIndexes)
		{
			FName ChildBoneName = Skeleton->GetReferenceSkeleton().GetBoneName(ChildBoneIndex);
			if (ChildBoneName == EndBone) return StartBone;
			FName JointBoneName = GetJointBone(Skeleton, ChildBoneName, EndBone);
			if (JointBoneName != NAME_None) return JointBoneName;
		}
	
	}
	return NAME_None;
}