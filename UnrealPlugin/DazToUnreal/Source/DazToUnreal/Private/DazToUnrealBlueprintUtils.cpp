#include "DazToUnrealBlueprintUtils.h"
#include "ReferenceSkeleton.h"
#include "Animation/Skeleton.h"
#include "AssetNotifications.h"
#include "AssetRegistry/AssetRegistryModule.h"

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION == 2
#include "IKRigDefinition.h"
#endif

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION > 2
#include "Rig/IKRigDefinition.h"
#endif

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION > 3
#include "SkeletonModifier.h"
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

void UDazToUnrealBlueprintUtils::ConvertToEpicSkeleton(USkeletalMesh* SkeletalMesh, USkeletalMesh* TargetEpicSkeleton)
{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION > 3
	if (!TargetEpicSkeleton)
	{
		// Find all SkeletalMeshes
		TArray<FAssetData> Assets;
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		AssetRegistry.GetAssetsByClass(USkeletalMesh::StaticClass()->GetClassPathName(), Assets);

		// Add a menu entry for each SkeletalMesh
		for (FAssetData Asset : Assets)
		{
			if (Asset.AssetName == "SKM_Quinn")
			{
				TargetEpicSkeleton = Cast<USkeletalMesh>(Asset.GetAsset());
			}
		}
	}

	USkeletonModifier* Modifier = NewObject<USkeletonModifier>();
	Modifier->SetSkeletalMesh(SkeletalMesh);

	// Reparent Pelvis area
	Modifier->ParentBone("pelvis", "root");
	Modifier->ParentBone("hip", "pelvis");
	//Modifier->ParentBone("l_thigh", "hip");
	//Modifier->ParentBone("r_thigh", "hip");
	//Modifier->ParentBone("spine1", "pelvis");
	//Modifier->RemoveBone("daz_pelvis", true);

	// Rename Spine
	//Modifier->RenameBone("pelvis", "spine_01");
	Modifier->RenameBone("hip", "spine_01");
	Modifier->RenameBone("spine1", "spine_02");
	Modifier->RenameBone("spine2", "spine_03");
	Modifier->RenameBone("spine3", "spine_04");
	Modifier->RenameBone("spine4", "spine_05");

	// Rename Neck
	Modifier->RenameBone("neck1", "neck_01");
	Modifier->RenameBone("neck2", "neck_02");

	// Rename Legs
	Modifier->RenameBone("l_thigh", "thigh_l");
	Modifier->RenameBone("l_shin", "calf_l");
	Modifier->RenameBone("l_foot", "foot_l");
	Modifier->RenameBone("l_toes", "ball_l");

	Modifier->RenameBone("r_thigh", "thigh_r");
	Modifier->RenameBone("r_shin", "calf_r");
	Modifier->RenameBone("r_foot", "foot_r");
	Modifier->RenameBone("r_toes", "ball_r");

	// Leg Twists
	Modifier->RenameBone("l_thightwist1", "thigh_twist_01_l");
	Modifier->RenameBone("l_thightwist2", "thigh_twist_02_l");

	Modifier->RenameBone("r_thightwist1", "thigh_twist_01_r");
	Modifier->RenameBone("r_thightwist2", "thigh_twist_02_r");

	// Arm Twists
	Modifier->RenameBone("l_upperarmtwist1", "upperarm_twist_01_l");
	Modifier->RenameBone("l_upperarmtwist2", "upperarm_twist_02_l");

	Modifier->RenameBone("r_upperarmtwist1", "upperarm_twist_01_r");
	Modifier->RenameBone("r_upperarmtwist2", "upperarm_twist_02_r");

	// The Lower Arm twists are swapped
	Modifier->RenameBone("l_forearmtwist1", "lowerarm_twist_02_l");
	Modifier->RenameBone("l_forearmtwist2", "lowerarm_twist_01_l");

	Modifier->RenameBone("r_forearmtwist1", "lowerarm_twist_02_r");
	Modifier->RenameBone("r_forearmtwist2", "lowerarm_twist_01_r");

	// Rename Arms
	Modifier->RenameBone("l_shoulder", "clavicle_l");
	Modifier->RenameBone("l_upperarm", "upperarm_l");
	Modifier->RenameBone("l_forearm", "lowerarm_l");
	Modifier->RenameBone("l_hand", "hand_l");

	Modifier->RenameBone("r_shoulder", "clavicle_r");
	Modifier->RenameBone("r_upperarm", "upperarm_r");
	Modifier->RenameBone("r_forearm", "lowerarm_r");
	Modifier->RenameBone("r_hand", "hand_r");

	// Rename Fingers
	Modifier->RenameBone("r_indexmetacarpal",	"index_metacarpal_r");
	Modifier->RenameBone("r_index1",			"index_01_r");
	Modifier->RenameBone("r_index2",			"index_02_r");
	Modifier->RenameBone("r_index3",			"index_03_r");

	Modifier->RenameBone("r_midmetacarpal",		"middle_metacarpal_r");
	Modifier->RenameBone("r_mid1",				"middle_01_r");
	Modifier->RenameBone("r_mid2",				"middle_02_r");
	Modifier->RenameBone("r_mid3",				"middle_03_r");

	Modifier->RenameBone("r_pinkymetacarpal",	"pinky_metacarpal_r");
	Modifier->RenameBone("r_pinky1",			"pinky_01_r");
	Modifier->RenameBone("r_pinky2",			"pinky_02_r");
	Modifier->RenameBone("r_pinky3",			"pinky_03_r");

	Modifier->RenameBone("r_ringmetacarpal",	"ring_metacarpal_r");
	Modifier->RenameBone("r_ring1",				"ring_01_r");
	Modifier->RenameBone("r_ring2",				"ring_02_r");
	Modifier->RenameBone("r_ring3",				"ring_03_r");

	Modifier->RenameBone("r_thumb1",			"thumb_01_r");
	Modifier->RenameBone("r_thumb2",			"thumb_02_r");
	Modifier->RenameBone("r_thumb3",			"thumb_03_r");

	Modifier->RenameBone("l_indexmetacarpal", "index_metacarpal_l");
	Modifier->RenameBone("l_index1", "index_01_l");
	Modifier->RenameBone("l_index2", "index_02_l");
	Modifier->RenameBone("l_index3", "index_03_l");

	Modifier->RenameBone("l_midmetacarpal", "middle_metacarpal_l");
	Modifier->RenameBone("l_mid1", "middle_01_l");
	Modifier->RenameBone("l_mid2", "middle_02_l");
	Modifier->RenameBone("l_mid3", "middle_03_l");

	Modifier->RenameBone("l_pinkymetacarpal", "pinky_metacarpal_l");
	Modifier->RenameBone("l_pinky1", "pinky_01_l");
	Modifier->RenameBone("l_pinky2", "pinky_02_l");
	Modifier->RenameBone("l_pinky3", "pinky_03_l");

	Modifier->RenameBone("l_ringmetacarpal", "ring_metacarpal_l");
	Modifier->RenameBone("l_ring1", "ring_01_l");
	Modifier->RenameBone("l_ring2", "ring_02_l");
	Modifier->RenameBone("l_ring3", "ring_03_l");
	
	Modifier->RenameBone("l_thumb1", "thumb_01_l");
	Modifier->RenameBone("l_thumb2", "thumb_02_l");
	Modifier->RenameBone("l_thumb3", "thumb_03_l");


	// Set Root Rotation
	Modifier->SetBoneTransform("root", FTransform::Identity, false);

	// spine_01 height
	//FTransform PelvisTransform = Modifier->GetBoneTransform("pelvis", true);
	//FTransform Spine01Transform = Modifier->GetBoneTransform("spine_01", true);
	//Spine01Transform.AddToTranslation(FVector(0.0f, 0.0f, 2.0f));
	//Modifier->SetBoneTransform("spine_01", Spine01Transform, false);

	//Rotation Order is Y, Z, X
	FQuat SpineRefRotation = FQuat(FRotator(90.0f, -90.0f, -90.0f));
	SetBoneOrientation(Modifier, "pelvis", SpineRefRotation);

	// Move pelvis down
	FTransform PelvisTransform = Modifier->GetBoneTransform("pelvis", true);
	FTransform LeftThighTransform = Modifier->GetBoneTransform("thigh_l", true);
	float PelvisHeightAdjustment = (PelvisTransform.GetTranslation().Z - LeftThighTransform.GetTranslation().Z) * 0.7f;

	FTransform PelvisRelativeTransform = Modifier->GetBoneTransform("pelvis", false);
	PelvisRelativeTransform.AddToTranslation(FVector(0.0f, 0.0f, -1.0f * PelvisHeightAdjustment));
	Modifier->SetBoneTransform("pelvis", PelvisRelativeTransform, false);

	// spine_01 height
	PelvisTransform = Modifier->GetBoneTransform("pelvis", true);
	FTransform Spine02Transform = Modifier->GetBoneTransform("spine_02", true);
	FVector RelativeLocation = FTransform::SubtractTranslations(Spine02Transform, PelvisTransform) * 0.5f;
	FTransform Spine01Transform = Modifier->GetBoneTransform("spine_01");
	Spine01Transform.SetTranslation(FVector(RelativeLocation.Z, RelativeLocation.Y, 0.0f));
	Modifier->SetBoneTransform("spine_01", Spine01Transform, false);

	SetBoneOrientation(Modifier, "spine_01", FQuat(FRotator(0.0f, 0.0f, 0.0f)));
	SetBoneOrientation(Modifier, "spine_02", FQuat(FRotator(0.0f, 0.0f, 0.0f)));
	SetBoneOrientation(Modifier, "spine_03", FQuat(FRotator(0.0f, 0.0f, 0.0f)));
	SetBoneOrientation(Modifier, "spine_04", FQuat(FRotator(0.0f, 0.0f, 0.0f)));
	SetBoneOrientation(Modifier, "spine_05", FQuat(FRotator(0.0f, 0.0f, 0.0f)));

	SetBoneOrientation(Modifier, "neck_01", FQuat(FRotator(0.0f, 0.0f, 0.0f)));
	SetBoneOrientation(Modifier, "neck_02", FQuat(FRotator(0.0f, 0.0f, 0.0f)));
	SetBoneOrientation(Modifier, "head", FQuat(FRotator(0.0f, 0.0f, 0.0f)));

	SetBoneOrientation(Modifier, "clavicle_l", FQuat(FRotator(-87.0f, -180.0f, 180.0f)));
	SetBoneOrientation(Modifier, "upperarm_l", FQuat(FRotator(0.0f, 0.0f, 0.0f)));
	SetBoneOrientation(Modifier, "lowerarm_l", FQuat(FRotator(0.0f, 0.0f, 0.0f)));

	SetBoneOrientation(Modifier, "clavicle_r", FQuat(FRotator(-87.0f, 0.0f, 180.0f)));
	SetBoneOrientation(Modifier, "upperarm_r", FQuat(FRotator(0.0f, 0.0f, 0.0f)));
	SetBoneOrientation(Modifier, "lowerarm_r", FQuat(FRotator(0.0f, 0.0f, 0.0f)));

	SetBoneOrientation(Modifier, "thigh_l", FQuat(FRotator(0.0f, 0.0f, 0.0f)));
	SetBoneOrientation(Modifier, "calf_l", FQuat(FRotator(0.0f, 0.0f, 0.0f)));
	SetBoneOrientation(Modifier, "foot_l", FQuat(FRotator(0.0f, 0.0f, 0.0f)));
	SetBoneOrientation(Modifier, "ball_l", FQuat(FRotator(0.0f, 90.0f, 0.0f)));

	SetBoneOrientation(Modifier, "thigh_r", FQuat(FRotator(0.0f, -180.0f, 0.0f)));
	SetBoneOrientation(Modifier, "calf_r", FQuat(FRotator(0.0f, 0.0f, 0.0f)));
	SetBoneOrientation(Modifier, "foot_r", FQuat(FRotator(0.0f, 0.0f, 0.0f)));
	SetBoneOrientation(Modifier, "ball_r", FQuat(FRotator(0.0f, 90.0f, 0.0f)));

	SetBoneOrientation(Modifier, "thigh_twist_01_l", FQuat(FRotator(0.0f, 0.0f, 0.0f)));
	SetBoneOrientation(Modifier, "thigh_twist_02_l", FQuat(FRotator(0.0f, 0.0f, 0.0f)));
	SetBoneOrientation(Modifier, "thigh_twist_01_r", FQuat(FRotator(0.0f, 0.0f, 0.0f)));
	SetBoneOrientation(Modifier, "thigh_twist_02_r", FQuat(FRotator(0.0f, 0.0f, 0.0f)));

	SetBoneOrientation(Modifier, "upperarm_twist_01_l", FQuat(FRotator(0.0f, 0.0f, 0.0f)));
	SetBoneOrientation(Modifier, "upperarm_twist_02_l", FQuat(FRotator(0.0f, 0.0f, 0.0f)));
	SetBoneOrientation(Modifier, "upperarm_twist_01_r", FQuat(FRotator(0.0f, 0.0f, 0.0f)));
	SetBoneOrientation(Modifier, "upperarm_twist_02_r", FQuat(FRotator(0.0f, 0.0f, 0.0f)));

	AlignBone(Modifier, FName("upperarm_twist_01_l"), FName("lowerarm_l"), FVector(0.0f, 1.0f, 0.0f));
	AlignBone(Modifier, FName("upperarm_twist_02_l"), FName("lowerarm_l"), FVector(0.0f, 1.0f, 0.0f));
	AlignBone(Modifier, FName("upperarm_twist_01_r"), FName("lowerarm_r"), FVector(0.0f, 1.0f, 0.0f));
	AlignBone(Modifier, FName("upperarm_twist_02_r"), FName("lowerarm_r"), FVector(0.0f, 1.0f, 0.0f));

	//AlignBone(Modifier, FName("pelvis"), FName("spine_01"));
	AlignBone(Modifier, FName("spine_04"), FName("spine_05"), FVector(0.0f, 0.0f, 1.0f));
	AlignBone(Modifier, FName("spine_03"), FName("spine_04"), FVector(0.0f, 0.0f, 1.0f));
	AlignBone(Modifier, FName("spine_02"), FName("spine_03"), FVector(0.0f, 0.0f, 1.0f));
	//AlignBone(Modifier, FName("spine_01"), FName("spine_02"), FVector(0.0f, 0.0f, 1.0f));
	
	AlignBone(Modifier, FName("spine_05"), FName("neck_01"), FVector(0.0f, 0.0f, 1.0f));
	AlignBone(Modifier, FName("neck_01"), FName("neck_02"), FVector(0.0f, 0.0f, 1.0f));

	AlignBone(Modifier, FName("lowerarm_l"), FName("hand_l"), FVector(0.0f, 1.0f, 1.0f));
	AlignBone(Modifier, FName("upperarm_l"), FName("lowerarm_l"), FVector(0.0f, 1.0f, 1.0f));
	AlignBone(Modifier, FName("clavicle_l"), FName("upperarm_l"), FVector(0.0f, 1.0f, 0.0f));

	AdditiveBoneOrientation(Modifier, "hand_l", FQuat(FRotator(0.0f, 0.0f, -180.0f)));

	AdditiveBoneOrientation(Modifier, "index_metacarpal_l", FQuat(FRotator(0.0f, 0.0f, -180.0f)));
	AdditiveBoneOrientation(Modifier, "index_01_l", FQuat(FRotator(0.0f, 0.0f, -180.0f)));
	AdditiveBoneOrientation(Modifier, "index_02_l", FQuat(FRotator(0.0f, 0.0f, -180.0f)));
	AdditiveBoneOrientation(Modifier, "index_03_l", FQuat(FRotator(0.0f, 0.0f, -180.0f)));

	AdditiveBoneOrientation(Modifier, "middle_metacarpal_l", FQuat(FRotator(0.0f, 0.0f, -180.0f)));
	AdditiveBoneOrientation(Modifier, "middle_01_l", FQuat(FRotator(0.0f, 0.0f, -180.0f)));
	AdditiveBoneOrientation(Modifier, "middle_02_l", FQuat(FRotator(0.0f, 0.0f, -180.0f)));
	AdditiveBoneOrientation(Modifier, "middle_03_l", FQuat(FRotator(0.0f, 0.0f, -180.0f)));

	AdditiveBoneOrientation(Modifier, "ring_metacarpal_l", FQuat(FRotator(0.0f, 0.0f, -180.0f)));
	AdditiveBoneOrientation(Modifier, "ring_01_l", FQuat(FRotator(0.0f, 0.0f, -180.0f)));
	AdditiveBoneOrientation(Modifier, "ring_02_l", FQuat(FRotator(0.0f, 0.0f, -180.0f)));
	AdditiveBoneOrientation(Modifier, "ring_03_l", FQuat(FRotator(0.0f, 0.0f, -180.0f)));

	AdditiveBoneOrientation(Modifier, "pinky_metacarpal_l", FQuat(FRotator(0.0f, 0.0f, -180.0f)));
	AdditiveBoneOrientation(Modifier, "pinky_01_l", FQuat(FRotator(0.0f, 0.0f, -180.0f)));
	AdditiveBoneOrientation(Modifier, "pinky_02_l", FQuat(FRotator(0.0f, 0.0f, -180.0f)));
	AdditiveBoneOrientation(Modifier, "pinky_03_l", FQuat(FRotator(0.0f, 0.0f, -180.0f)));

	AdditiveBoneOrientation(Modifier, "thumb_01_l", FQuat(FRotator(0.0f, 0.0f, -90.0f)));
	AdditiveBoneOrientation(Modifier, "thumb_02_l", FQuat(FRotator(0.0f, 0.0f, -90.0f)));
	AdditiveBoneOrientation(Modifier, "thumb_03_l", FQuat(FRotator(0.0f, 0.0f, -90.0f)));

	AlignBone(Modifier, FName("lowerarm_r"), FName("hand_r"), FVector(0.0f, 1.0f, 1.0f));
	AlignBone(Modifier, FName("upperarm_r"), FName("lowerarm_r"), FVector(0.0f, 1.0f, 1.0f));
	AlignBone(Modifier, FName("clavicle_r"), FName("upperarm_r"), FVector(0.0f, 1.0f, 0.0f));

	AlignBone(Modifier, FName("lowerarm_twist_02_r"), FName("hand_r"), FVector(0.0f, 1.0f, 1.0f));
	AlignBone(Modifier, FName("lowerarm_twist_01_r"), FName("hand_r"), FVector(0.0f, 1.0f, 1.0f));
	AdditiveBoneOrientation(Modifier, "lowerarm_twist_01_r", FQuat(FRotator(0.0f, 0.0f, 90.0f)));
	AdditiveBoneOrientation(Modifier, "lowerarm_twist_02_r", FQuat(FRotator(0.0f, 0.0f, 90.0f)));

	AlignBone(Modifier, FName("lowerarm_twist_02_l"), FName("hand_l"), FVector(0.0f, 1.0f, 1.0f));
	AlignBone(Modifier, FName("lowerarm_twist_01_l"), FName("hand_l"), FVector(0.0f, 1.0f, 1.0f));
	AdditiveBoneOrientation(Modifier, "lowerarm_twist_01_l", FQuat(FRotator(0.0f, 0.0f, -90.0f)));
	AdditiveBoneOrientation(Modifier, "lowerarm_twist_02_l", FQuat(FRotator(0.0f, 0.0f, -90.0f)));

	AdditiveBoneOrientation(Modifier, "thumb_01_r", FQuat(FRotator(0.0f, 0.0f, 90.0f)));
	AdditiveBoneOrientation(Modifier, "thumb_02_r", FQuat(FRotator(0.0f, 0.0f, 90.0f)));
	AdditiveBoneOrientation(Modifier, "thumb_03_r", FQuat(FRotator(0.0f, 0.0f, 90.0f)));
	
	AlignBone(Modifier, FName("calf_l"), FName("foot_l"), FVector(0.0f, 1.0f, 1.0f));
	AlignBone(Modifier, FName("thigh_l"), FName("calf_l"), FVector(0.0f, 1.0f, 1.0f));
	//AlignBone(Modifier, FName("foot_l"), FName("ball_l"), FVector(0.0f, 1.0f, 0.0f));

	AlignBone(Modifier, FName("calf_r"), FName("foot_r"), FVector(0.0f, -1.0f, 1.0f));
	AlignBone(Modifier, FName("thigh_r"), FName("calf_r"), FVector(0.0f, -1.0f, 1.0f));
	//AlignBone(Modifier, FName("foot_r"), FName("ball_r"), FVector(0.0f, 1.0f, 0.0f));


	// Zero Foot X
	FTransform LFootTransform = Modifier->GetBoneTransform("foot_l");
	FRotator LFootRotator = FEulerTransform(LFootTransform).GetRotation().Rotator();
	LFootRotator.Roll = 0.0f;
	LFootTransform.SetRotation(FQuat(LFootRotator));
	Modifier->SetBoneTransform("foot_l", LFootTransform, false);

	FTransform RFootTransform = Modifier->GetBoneTransform("foot_r");
	FRotator RFootRotator = FEulerTransform(RFootTransform).GetRotation().Rotator();
	RFootRotator.Roll = 0.0f;
	RFootTransform.SetRotation(FQuat(RFootRotator));
	Modifier->SetBoneTransform("foot_r", RFootTransform, false);


	Modifier->CommitSkeletonToSkeletalMesh();

	//UpdateReferencePose(SkeletalMesh, "lowerarm_l", FVector(0.0f, 0.0f, 37.0f));
	//UpdateReferencePose(SkeletalMesh, "lowerarm_r", FVector(0.0f, 0.0f, 37.0f));

	// Change the Skeleton
	USkeleton* TargetSkeleton = TargetEpicSkeleton->GetSkeleton();
	TargetSkeleton->MergeAllBonesToBoneTree(SkeletalMesh);
	SkeletalMesh->SetSkeleton(TargetSkeleton);

	// Update Retargetting
	int32 PelvisBoneIndex = TargetSkeleton->GetReferenceSkeleton().FindBoneIndex(FName(TEXT("pelvis")));
	if (PelvisBoneIndex != -1)
	{
		TargetSkeleton->SetBoneTranslationRetargetingMode(PelvisBoneIndex, EBoneTranslationRetargetingMode::Skeleton, true);
		TargetSkeleton->SetBoneTranslationRetargetingMode(PelvisBoneIndex, EBoneTranslationRetargetingMode::AnimationScaled, false);
	}

	int32 HeadBoneIndex = TargetSkeleton->GetReferenceSkeleton().FindBoneIndex(FName(TEXT("head")));
	if (HeadBoneIndex != -1)
	{
		TargetSkeleton->SetBoneTranslationRetargetingMode(HeadBoneIndex, EBoneTranslationRetargetingMode::AnimationRelative, true);
		TargetSkeleton->SetBoneTranslationRetargetingMode(HeadBoneIndex, EBoneTranslationRetargetingMode::Skeleton, false);
	}

	// Mark Dirty
	SkeletalMesh->MarkPackageDirty();
	FAssetNotifications::SkeletonNeedsToBeSaved(TargetSkeleton);

	// Disconnect the physics asset since it won't match now
	SkeletalMesh->SetPhysicsAsset(nullptr);
#endif
}

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION > 3
void UDazToUnrealBlueprintUtils::UpdateReferencePose(class USkeletalMesh* SkeletalMesh, FName BoneName, FVector AdditiveRotation)
{
	FReferenceSkeletonModifier RefSkelModifier(SkeletalMesh->GetRefSkeleton(), SkeletalMesh->GetSkeleton());
	const TArray<FMeshBoneInfo>& BoneInfos = RefSkelModifier.GetRefBoneInfo();
	const int32 RefBoneIndex = RefSkelModifier.GetReferenceSkeleton().FindBoneIndex(BoneName);
	TArray<FTransform> TargetSkeletonBoneTransforms = RefSkelModifier.GetReferenceSkeleton().GetRawRefBonePose();

	FTransform Transform = TargetSkeletonBoneTransforms[RefBoneIndex];
	Transform.SetRotation(FQuat(FRotator(AdditiveRotation.Y, AdditiveRotation.Z, AdditiveRotation.X)));

	RefSkelModifier.UpdateRefPoseTransform(RefBoneIndex, Transform);

}

void UDazToUnrealBlueprintUtils::SetBoneOrientation(class USkeletonModifier* Modifier, FName BoneName, FQuat Quat)
{
	FTransform Transform = Modifier->GetBoneTransform(BoneName);
	FRotator Rotator  = FEulerTransform(Transform).GetRotation().Rotator();
	FVector OriginalRotation = Transform.GetRotation().Euler();
	Transform.SetRotation(Quat);
	Modifier->SetBoneTransform(BoneName, Transform, false);
}

void UDazToUnrealBlueprintUtils::AdditiveBoneOrientation(class USkeletonModifier* Modifier, FName BoneName, FQuat Quat)
{
	FTransform Transform = Modifier->GetBoneTransform(BoneName);
	FRotator Rotator = FEulerTransform(Transform).GetRotation().Rotator();
	FVector OriginalRotation = Transform.GetRotation().Euler();
	Transform.ConcatenateRotation(Quat);
	Modifier->SetBoneTransform(BoneName, Transform, false);
}

void UDazToUnrealBlueprintUtils::CopyBoneOrientation(class USkeletonModifier* Modifier, FName BoneNameToSet, FName BoneToCopy)
{
	FTransform Transform = Modifier->GetBoneTransform(BoneToCopy, true);
	SetBoneOrientation(Modifier, BoneNameToSet, Transform.GetRotation());
}

void UDazToUnrealBlueprintUtils::SetBoneTransform(class USkeletalMesh* SkeletalMesh, FReferenceSkeletonModifier& RefSkelModifier, FName BoneName, FTransform NewTransform)
{
	{
		//FReferenceSkeletonModifier RefSkelModifier(SkeletalMesh->GetRefSkeleton(), SkeletalMesh->GetSkeleton());
		FReferenceSkeleton& ReferenceSkeleton = SkeletalMesh->GetRefSkeleton();

		int32 BoneIndex = RefSkelModifier.FindBoneIndex(FName("root"));

		TArray<int32> ChildrenToFix;
		TArray<FTransform> GlobalTransforms;
		// get children
		//for (int32 Index = 0; Index < SkeletalMesh->GetRefSkeleton().BoneIndices.Num(); Index++)
		{
			TArray<int32> Children;
			ReferenceSkeleton.GetRawDirectChildBones(BoneIndex, Children);
			for (int32 ChildIndex : Children)
			{
				//if (!BoneIndices.Contains(ChildIndex))
				{
					ChildrenToFix.Add(ChildIndex);
				}
			}
		}

		// sort them from highest index to lowest
		ChildrenToFix.Sort([](const int32 Index0, const int32 Index1) {return Index0 > Index1; });
		const int32 NumChildren = ChildrenToFix.Num();

		// compute global transforms (note that we could cache them for faster implementation) 
		GlobalTransforms.AddUninitialized(NumChildren);

		for (int32 Index = 0; Index < NumChildren; Index++)
		{
			GlobalTransforms[Index] = GetGlobalTransform(ReferenceSkeleton, ChildrenToFix[Index]);
		}

		FTransform Transform = FTransform::Identity;
		//int32 BoneIndex = RefSkelModifier.FindBoneIndex(FName("root"));
		RefSkelModifier.UpdateRefPoseTransform(BoneIndex, Transform);


		for (int32 Index = 0; Index < NumChildren; Index++)
		{
			const int32 ChildrenIndex = ChildrenToFix[Index];
			const int32 ParentIndex = ReferenceSkeleton.GetRawParentIndex(ChildrenIndex);
			const FTransform NewParentGlobal = GetGlobalTransform(ReferenceSkeleton, BoneIndex);
			FTransform NewLocal = GlobalTransforms[Index].GetRelativeTransform(NewParentGlobal);
			NewLocal.NormalizeRotation();
			RefSkelModifier.UpdateRefPoseTransform(ChildrenIndex, NewLocal);
		}
	}

	//SkeletalMesh->GetRefBasesInvMatrix().Reset();
	//SkeletalMesh->CalculateInvRefMatrices(); // Needs to be called after RefSkelModifier is destroyed
}

const FTransform UDazToUnrealBlueprintUtils::GetGlobalTransform(const FReferenceSkeleton& RefSkeleton, const uint32 BoneIndex)
{
	if (BoneIndex == INDEX_NONE) return FTransform::Identity;

	FTransform Transform = RefSkeleton.GetRawRefBonePose()[BoneIndex];
	
	const uint32 ParentIndex = RefSkeleton.GetRawParentIndex(BoneIndex);

	Transform *= GetGlobalTransform(RefSkeleton, ParentIndex);

	return Transform;
}

void UDazToUnrealBlueprintUtils::AlignBone(class USkeletonModifier* Modifier, FName Parent, FName Child, FVector AlignmentAxis)
{
	FTransform ParentTransform = Modifier->GetBoneTransform(Parent);
	FTransform ChildTransform = Modifier->GetBoneTransform(Child);
	
	FVector ChildRelativeLocation = ChildTransform.GetLocation();
	if (FMath::IsNearlyEqual(FMath::Abs(AlignmentAxis.Z), 1.0f))
	{
		double Rotation = FMath::Sin(ChildRelativeLocation.Y / ChildRelativeLocation.X);
		Rotation = FMath::RadiansToDegrees(Rotation);

		AdditiveBoneOrientation(Modifier, Parent, FQuat(FRotator(0.0f, Rotation * AlignmentAxis.Z, 0.0f)));
	}

	if (FMath::IsNearlyEqual(FMath::Abs(AlignmentAxis.Y), 1.0f))
	{
		double Rotation = FMath::Sin(ChildRelativeLocation.Z / ChildRelativeLocation.X);
		Rotation = FMath::RadiansToDegrees(Rotation);

		AdditiveBoneOrientation(Modifier, Parent, FQuat(FRotator(Rotation * AlignmentAxis.Y, 0.0f, 0.0f)));
	}
	//double Rotation = FVector::DotProduct(FVector(0,0,0), ChildTransform.GetLocation());

}
#endif