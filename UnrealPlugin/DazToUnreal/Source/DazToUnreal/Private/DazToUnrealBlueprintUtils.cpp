#include "DazToUnrealBlueprintUtils.h"
#include "DazMetaHumanSourceData.h"
#include "DazToUnrealSettings.h"
#include "ReferenceSkeleton.h"
#include "Animation/Skeleton.h"
#include "AssetNotifications.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION == 2
#include "IKRigDefinition.h"
#endif

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION > 2
#include "Rig/IKRigDefinition.h"
#endif

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION > 3
#include "SkeletonModifier.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsAssetUtils.h"
#endif

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 8
#include <atomic>
#include "Algo/Accumulate.h"
#include "Async/ParallelFor.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "UDynamicMesh.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "GroomAsset.h"
#include "GroomTextureBuilder.h"
#include "ImageCore.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCharacterGeneratedAssets.h"
#include "MetaHumanCollection.h"
#include "MetaHumanGeometryRemoval.h"
#include "MetaHumanInstance.h"
#include "MeshQueries.h"
#include "Misc/PackageName.h"
#include "SubobjectDataSubsystem.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#endif

DEFINE_LOG_CATEGORY(LogDazToUnrealBlueprintUtils);

#define LOCTEXT_NAMESPACE "FDazToUnrealModule"

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
		// Check the plugin settings first
		const UDazToUnrealSettings* Settings = GetDefault<UDazToUnrealSettings>();
		if (Settings && !Settings->EpicSkeletonMesh.IsNull())
		{
			TargetEpicSkeleton = Cast<USkeletalMesh>(Settings->EpicSkeletonMesh.TryLoad());
		}
	}

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

	if (!TargetEpicSkeleton)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Error, TEXT("Convert to Epic Skeleton Used, but Quinn not found"));
		FNotificationInfo Info(LOCTEXT("SkeletalMeshHasNoSkeleton", "Convert to Epic Skeleton requires project to have the UE5 mannequin.  Start from a template project that includes the mannequins."));
		Info.bUseSuccessFailIcons = true;
		Info.Image = FAppStyle::GetBrush(TEXT("MessageLog.Error"));
		Info.bFireAndForget = true;
		Info.bUseThrobber = true;
		Info.FadeOutDuration = 2.f;
		Info.ExpireDuration = 8.f;;
		TSharedPtr<SNotificationItem> NotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);
		if (NotificationPtr)
		{
			NotificationPtr->SetCompletionState(SNotificationItem::CS_Fail);
		}
		return;
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

	// G3/G8/G8.1 Renaming ****************************************************************
	// Rename Spine
	Modifier->RenameBone("hip", "spine_01");
	Modifier->RenameBone("abdomenLower", "spine_02");
	Modifier->RenameBone("abdomenUpper", "spine_03");
	Modifier->RenameBone("chestLower", "spine_04");
	Modifier->RenameBone("chestUpper", "spine_05");

	// Rename Neck
	Modifier->RenameBone("neckLower", "neck_01");
	Modifier->RenameBone("neckUpper", "neck_02");

	// Rename Legs
	Modifier->RenameBone("lThighBend", "thigh_l");
	Modifier->RenameBone("lShin", "calf_l");
	Modifier->RenameBone("lFoot", "foot_l");
	Modifier->RenameBone("lToe", "ball_l");

	Modifier->RenameBone("rThighBend", "thigh_r");
	Modifier->RenameBone("rShin", "calf_r");
	Modifier->RenameBone("rFoot", "foot_r");
	Modifier->RenameBone("rToe", "ball_r");

	// Leg Twists
	Modifier->RenameBone("lThighTwist", "thigh_twist_01_l");

	Modifier->RenameBone("rThighTwist", "thigh_twist_01_r");

	// Arm Twists
	Modifier->RenameBone("lShldrTwist", "upperarm_twist_01_l");

	Modifier->RenameBone("rShldrTwist", "upperarm_twist_01_r");

	// The Lower Arm twists are swapped
	Modifier->RenameBone("lForearmTwist", "lowerarm_twist_02_l");

	Modifier->RenameBone("rForearmTwist", "lowerarm_twist_02_r");

	// Rename Arms
	Modifier->RenameBone("lCollar", "clavicle_l");
	Modifier->RenameBone("lShldrBend", "upperarm_l");
	Modifier->RenameBone("lForearmBend", "lowerarm_l");
	Modifier->RenameBone("lHand", "hand_l");

	Modifier->RenameBone("rCollar", "clavicle_r");
	Modifier->RenameBone("rShldrBend", "upperarm_r");
	Modifier->RenameBone("rForearmBend", "lowerarm_r");
	Modifier->RenameBone("rHand", "hand_r");

	// Rename Fingers
	Modifier->RenameBone("rCarpal1", "index_metacarpal_r");
	Modifier->RenameBone("rIndex1", "index_01_r");
	Modifier->RenameBone("rIndex2", "index_02_r");
	Modifier->RenameBone("rIndex3", "index_03_r");

	Modifier->RenameBone("rCarpal2", "middle_metacarpal_r");
	Modifier->RenameBone("rMid1", "middle_01_r");
	Modifier->RenameBone("rMid2", "middle_02_r");
	Modifier->RenameBone("rMid3", "middle_03_r");
						  
	Modifier->RenameBone("rCarpal4", "pinky_metacarpal_r");
	Modifier->RenameBone("rPinky1", "pinky_01_r");
	Modifier->RenameBone("rPinky2", "pinky_02_r");
	Modifier->RenameBone("rPinky3", "pinky_03_r");
						  
	Modifier->RenameBone("rCarpal3", "ring_metacarpal_r");
	Modifier->RenameBone("rRing1", "ring_01_r");
	Modifier->RenameBone("rRing2", "ring_02_r");
	Modifier->RenameBone("rRing3", "ring_03_r");
						  
	Modifier->RenameBone("rThumb1", "thumb_01_r");
	Modifier->RenameBone("rThumb2", "thumb_02_r");
	Modifier->RenameBone("rThumb3", "thumb_03_r");

	Modifier->RenameBone("lCarpal1", "index_metacarpal_l");
	Modifier->RenameBone("lIndex1", "index_01_l");
	Modifier->RenameBone("lIndex2", "index_02_l");
	Modifier->RenameBone("lIndex3", "index_03_l");

	Modifier->RenameBone("lCarpal2", "middle_metacarpal_l");
	Modifier->RenameBone("lMid1", "middle_01_l");
	Modifier->RenameBone("lMid2", "middle_02_l");
	Modifier->RenameBone("lMid3", "middle_03_l");

	Modifier->RenameBone("lCarpal4", "pinky_metacarpal_l");
	Modifier->RenameBone("lPinky1", "pinky_01_l");
	Modifier->RenameBone("lPinky2", "pinky_02_l");
	Modifier->RenameBone("lPinky3", "pinky_03_l");

	Modifier->RenameBone("lCarpal3", "ring_metacarpal_l");
	Modifier->RenameBone("lRing1", "ring_01_l");
	Modifier->RenameBone("lRing2", "ring_02_l");
	Modifier->RenameBone("lRing3", "ring_03_l");

	Modifier->RenameBone("lThumb1", "thumb_01_l");
	Modifier->RenameBone("lThumb2", "thumb_02_l");
	Modifier->RenameBone("lThumb3", "thumb_03_l");

	// G9 Renaming ****************************************************************
	// Rename Spine
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

	AlignBone(Modifier, FName("clavicle_l"), FName("upperarm_l"), FVector(0.0f, 1.0f, 1.0f));
	AlignBone(Modifier, FName("upperarm_l"), FName("lowerarm_l"), FVector(0.0f, 1.0f, 1.0f));

	// Hand twice on purpose. 
	AlignBone(Modifier, FName("lowerarm_l"), FName("hand_l"), FVector(0.0f, 1.0f, 1.0f));
	AlignBone(Modifier, FName("lowerarm_l"), FName("hand_l"), FVector(0.0f, 1.0f, 1.0f));

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

	AlignBone(Modifier, FName("clavicle_r"), FName("upperarm_r"), FVector(0.0f, 1.0f, 1.0f));
	AlignBone(Modifier, FName("upperarm_r"), FName("lowerarm_r"), FVector(0.0f, 1.0f, 1.0f));

	// Hand twice on purpose
	AlignBone(Modifier, FName("lowerarm_r"), FName("hand_r"), FVector(0.0f, 1.0f, 1.0f));
	AlignBone(Modifier, FName("lowerarm_r"), FName("hand_r"), FVector(0.0f, 1.0f, 1.0f));

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
	FText CreationErrorMessage;
	bool bSuccess = FPhysicsAssetUtils::CreateFromSkeletalMesh(SkeletalMesh->GetPhysicsAsset(), SkeletalMesh, FPhysAssetCreateParams(), CreationErrorMessage);
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
	FVector ChildRelativeLocation = Modifier->GetBoneTransform(Child, false).GetLocation();
	FRotator NewRotation = FRotator(0.0f);
	if (FMath::IsNearlyEqual(FMath::Abs(AlignmentAxis.Z), 1.0f))
	{
		double Rotation = FMath::Atan(ChildRelativeLocation.Y / ChildRelativeLocation.X);
		Rotation = FMath::RadiansToDegrees(Rotation);
		NewRotation.Yaw = Rotation * AlignmentAxis.Z;
	}

	if (FMath::IsNearlyEqual(FMath::Abs(AlignmentAxis.Y), 1.0f))
	{
		double Rotation = FMath::Atan(ChildRelativeLocation.Z / ChildRelativeLocation.X);
		Rotation = FMath::RadiansToDegrees(Rotation);
		NewRotation.Pitch = Rotation * AlignmentAxis.Y;
	}
	AdditiveBoneOrientation(Modifier, Parent, FQuat(FRotator(NewRotation.Pitch * AlignmentAxis.Y, NewRotation.Yaw * AlignmentAxis.Z, 0.0f)));
}

void UDazToUnrealBlueprintUtils::FixBoneOffset(class USkeletonModifier* Modifier, FName Parent, FName BoneToFix, FVector ForwardAxis)
{
	FTransform ParentTransform = Modifier->GetBoneTransform(Parent);
	FTransform BoneToFixTransform = Modifier->GetBoneTransform(BoneToFix);
	float Length = BoneToFixTransform.GetLocation().Length();
	BoneToFixTransform.SetLocation(ForwardAxis * Length);
	Modifier->SetBoneTransform(BoneToFix, BoneToFixTransform, true);
}
#endif

USkeletalMesh* UDazToUnrealBlueprintUtils::ExtractMetaHumanBodyMesh(UObject* MetaHumanCharacter, const FString& AssetPathAndName)
{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 8
	UMetaHumanCharacter* Character = Cast<UMetaHumanCharacter>(MetaHumanCharacter);
	if (!Character)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("ExtractMetaHumanBodyMesh: not a MetaHumanCharacter: %s"), *GetNameSafe(MetaHumanCharacter));
		return nullptr;
	}

	UMetaHumanCharacterEditorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UMetaHumanCharacterEditorSubsystem>() : nullptr;
	if (!Subsystem)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("ExtractMetaHumanBodyMesh: MetaHumanCharacterEditorSubsystem unavailable"));
		return nullptr;
	}

	// The live body mesh only exists while the character is open for edit.
	bool bAddedForEdit = false;
	if (!Subsystem->IsObjectAddedForEditing(Character))
	{
		if (!Subsystem->TryAddObjectToEdit(Character))
		{
			UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("ExtractMetaHumanBodyMesh: unable to open %s for edit"), *Character->GetName());
			return nullptr;
		}
		bAddedForEdit = true;
	}

	USkeletalMesh* Result = nullptr;
	if (const TSharedRef<FMetaHumanCharacterEditorData>* CharacterData = Subsystem->GetMetaHumanCharacterEditorData(Character))
	{
		if (USkeletalMesh* BodyMesh = (*CharacterData)->BodyMesh)
		{
			// AssetPathAndName may be /Game/Path/Name or /Game/Path/Name.Name
			FString PackageName = AssetPathAndName;
			int32 DotIndex = INDEX_NONE;
			if (PackageName.FindChar(TEXT('.'), DotIndex))
			{
				PackageName.LeftInline(DotIndex);
			}
			FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);

			UPackage* Package = CreatePackage(*PackageName);
			Result = DuplicateObject<USkeletalMesh>(BodyMesh, Package, *AssetName);
			if (Result)
			{
				Result->SetFlags(RF_Public | RF_Standalone);
				FAssetRegistryModule::AssetCreated(Result);
				Result->MarkPackageDirty();

				// The duplicated mesh keeps its references; anything still pointing at the
				// transient edit session won't survive a save. Surface it instead of failing.
				if (USkeleton* Skeleton = Result->GetSkeleton())
				{
					if (Skeleton->GetOutermost() == GetTransientPackage())
					{
						UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("ExtractMetaHumanBodyMesh: %s references a transient skeleton %s — it will not survive a save"), *Result->GetName(), *Skeleton->GetName());
					}
				}
				else
				{
					UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("ExtractMetaHumanBodyMesh: %s has no skeleton"), *Result->GetName());
				}
			}
		}
		else
		{
			UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("ExtractMetaHumanBodyMesh: no body mesh in the edit session for %s"), *Character->GetName());
		}
	}
	else
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("ExtractMetaHumanBodyMesh: no editor data for %s"), *Character->GetName());
	}

	if (bAddedForEdit)
	{
		Subsystem->RemoveObjectToEdit(Character);
	}
	return Result;
#else
	UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("ExtractMetaHumanBodyMesh requires Unreal Engine 5.8 or later"));
	return nullptr;
#endif
}

USkeletalMesh* UDazToUnrealBlueprintUtils::CreateMetaHumanCombinedMesh(UObject* MetaHumanCharacter, const FString& AssetPathAndName)
{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 8
	UMetaHumanCharacter* Character = Cast<UMetaHumanCharacter>(MetaHumanCharacter);
	if (!Character)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("CreateMetaHumanCombinedMesh: not a MetaHumanCharacter: %s"), *GetNameSafe(MetaHumanCharacter));
		return nullptr;
	}

	UMetaHumanCharacterEditorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UMetaHumanCharacterEditorSubsystem>() : nullptr;
	if (!Subsystem)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("CreateMetaHumanCombinedMesh: MetaHumanCharacterEditorSubsystem unavailable"));
		return nullptr;
	}

	// CreateCombinedFaceAndBodyMesh check()s that the character is open for edit — the
	// editor would crash if we called it without an edit session, so guarantee one.
	bool bAddedForEdit = false;
	if (!Subsystem->IsObjectAddedForEditing(Character))
	{
		if (!Subsystem->TryAddObjectToEdit(Character))
		{
			UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("CreateMetaHumanCombinedMesh: unable to open %s for edit"), *Character->GetName());
			return nullptr;
		}
		bAddedForEdit = true;
	}

	// NOTE: the merged mesh is a single-section outfit-fitting / DCC-export utility, not a
	// renderable character (Epic's own geometry export applies a clay material to it; real
	// MetaHumans keep face and body as separate skeletal meshes). It carries body
	// measurements as ChaosOutfitAssetBodyUserData for clothing fit.
	USkeletalMesh* Result = Subsystem->CreateCombinedFaceAndBodyMesh(Character, AssetPathAndName, /*bOverwriteExisting =*/ true);
	if (Result)
	{
		Result->MarkPackageDirty();
	}
	else
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("CreateMetaHumanCombinedMesh: merge failed for %s"), *Character->GetName());
	}

	if (bAddedForEdit)
	{
		Subsystem->RemoveObjectToEdit(Character);
	}
	return Result;
#else
	UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("CreateMetaHumanCombinedMesh requires Unreal Engine 5.8 or later"));
	return nullptr;
#endif
}

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 8
// Assign a skeletal mesh to a component template and propagate to any archetype instances.
// Mirrors UE::MetaHuman::Private::SetSkeletalMesh in MetaHumanDefaultEditorPipelineLegacy.cpp.
static bool SetComponentTemplateSkeletalMesh(UActorComponent* InComponent, USkeletalMesh* InSkelMesh)
{
	USkeletalMeshComponent* SkelMeshComponent = Cast<USkeletalMeshComponent>(InComponent);
	if (!SkelMeshComponent)
	{
		return false;
	}

	FProperty* SkelMeshProperty = USkeletalMeshComponent::StaticClass()->FindPropertyByName(SkelMeshComponent->GetSkeletalMeshAssetPropertyNameChecked());
	check(SkelMeshProperty);
	FPropertyChangedEvent SkelMeshChangedEvent{ SkelMeshProperty, EPropertyChangeType::ValueSet };

	SkelMeshComponent->SetSkeletalMeshAsset(InSkelMesh);
	// Empty the override materials since PostEditChangeProperty will recreate the slots
	SkelMeshComponent->OverrideMaterials.Empty();
	SkelMeshComponent->PostEditChangeProperty(SkelMeshChangedEvent);

	TArray<UObject*> Instances;
	SkelMeshComponent->GetArchetypeInstances(Instances);
	for (UObject* Instance : Instances)
	{
		if (USkeletalMeshComponent* SkelMeshCompInstance = Cast<USkeletalMeshComponent>(Instance))
		{
			SkelMeshCompInstance->SetSkeletalMeshAsset(InSkelMesh);
			SkelMeshCompInstance->OverrideMaterials.Empty();
			SkelMeshCompInstance->PostEditChangeProperty(SkelMeshChangedEvent);
		}
	}
	return true;
}
#endif

UObject* UDazToUnrealBlueprintUtils::AssembleMetaHumanCharacterBlueprint(USkeletalMesh* FaceMesh, USkeletalMesh* BodyMesh, const FString& BlueprintPathAndName)
{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 8
	if (!FaceMesh || !BodyMesh)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("AssembleMetaHumanCharacterBlueprint: face and body meshes are both required"));
		return nullptr;
	}

	const UDazToUnrealSettings* CachedSettings = GetDefault<UDazToUnrealSettings>();
	UBlueprint* TemplateBlueprint = Cast<UBlueprint>(CachedSettings->MetaHumanActorTemplate.TryLoad());
	if (!TemplateBlueprint)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("AssembleMetaHumanCharacterBlueprint: unable to load actor template %s"), *CachedSettings->MetaHumanActorTemplate.ToString());
		return nullptr;
	}

	// BlueprintPathAndName may be /Game/Path/Name or /Game/Path/Name.Name
	FString PackageName = BlueprintPathAndName;
	int32 DotIndex = INDEX_NONE;
	if (PackageName.FindChar(TEXT('.'), DotIndex))
	{
		PackageName.LeftInline(DotIndex);
	}
	const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);

	// Reuse an existing generated blueprint (just update the meshes) so re-imports stay idempotent.
	UBlueprint* GeneratedBP = LoadObject<UBlueprint>(nullptr, *(PackageName + TEXT(".") + AssetName), nullptr, LOAD_NoWarn);
	if (!GeneratedBP)
	{
		UPackage* Package = CreatePackage(*PackageName);
		GeneratedBP = Cast<UBlueprint>(DuplicateObject<UObject>(TemplateBlueprint, Package, *AssetName));
		if (!GeneratedBP)
		{
			UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("AssembleMetaHumanCharacterBlueprint: failed to duplicate template %s"), *TemplateBlueprint->GetName());
			return nullptr;
		}
		GeneratedBP->SetFlags(RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(GeneratedBP);
	}

	AActor* ActorCDO = GeneratedBP->GeneratedClass ? GeneratedBP->GeneratedClass->GetDefaultObject<AActor>() : nullptr;
	if (!ActorCDO)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("AssembleMetaHumanCharacterBlueprint: %s has no generated class"), *GeneratedBP->GetName());
		return nullptr;
	}

	// Find the Face/Body components by name and assign the meshes (mirrors the MetaHuman
	// pipeline's UpdateActorBlueprint, which drives the same templates).
	USubobjectDataSubsystem* SubobjectDataSubsystem = USubobjectDataSubsystem::Get();
	TArray<FSubobjectDataHandle> SubobjectDataHandles;
	SubobjectDataSubsystem->GatherSubobjectData(ActorCDO, SubobjectDataHandles);
	SubobjectDataHandles = TSet<FSubobjectDataHandle>(SubobjectDataHandles).Array();

	bool bFaceSet = false;
	bool bBodySet = false;
	for (const FSubobjectDataHandle& Handle : SubobjectDataHandles)
	{
		if (UActorComponent* ActorComponent = const_cast<UActorComponent*>(Handle.GetData()->GetObjectForBlueprint<UActorComponent>(GeneratedBP)))
		{
			FString ComponentName = ActorComponent->GetName();
			ComponentName.RemoveFromEnd(UActorComponent::ComponentTemplateNameSuffix);

			if (ComponentName == TEXT("Face"))
			{
				bFaceSet = SetComponentTemplateSkeletalMesh(ActorComponent, FaceMesh);
			}
			else if (ComponentName == TEXT("Body"))
			{
				bBodySet = SetComponentTemplateSkeletalMesh(ActorComponent, BodyMesh);
			}
		}
	}

	if (!bFaceSet || !bBodySet)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("AssembleMetaHumanCharacterBlueprint: template %s is missing a %s skeletal mesh component; the blueprint may be incomplete"),
			*TemplateBlueprint->GetName(), !bFaceSet ? TEXT("Face") : TEXT("Body"));
	}

	FKismetEditorUtilities::CompileBlueprint(GeneratedBP);
	GeneratedBP->MarkPackageDirty();
	return GeneratedBP;
#else
	UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("AssembleMetaHumanCharacterBlueprint requires Unreal Engine 5.8 or later"));
	return nullptr;
#endif
}

bool UDazToUnrealBlueprintUtils::AttachClothingToCharacterBlueprint(UObject* CharacterBlueprint, const TArray<USkeletalMesh*>& ClothingMeshes)
{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 8
	UBlueprint* GeneratedBP = Cast<UBlueprint>(CharacterBlueprint);
	AActor* ActorCDO = (GeneratedBP && GeneratedBP->GeneratedClass) ? GeneratedBP->GeneratedClass->GetDefaultObject<AActor>() : nullptr;
	if (!ActorCDO)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("AttachClothingToCharacterBlueprint: not a compiled actor blueprint: %s"), *GetNameSafe(CharacterBlueprint));
		return false;
	}

	// Component name per mesh: Clothing_<asset name minus SK_>. The clothing components are
	// children of Body — the template's construction script leader-poses every skeletal mesh
	// child of Body (except Face), so they follow body animation with no extra wiring.
	TMap<FString, USkeletalMesh*> DesiredComponents;
	for (USkeletalMesh* ClothingMesh : ClothingMeshes)
	{
		if (ClothingMesh)
		{
			FString MeshName = ClothingMesh->GetName();
			MeshName.RemoveFromStart(TEXT("SK_"));
			DesiredComponents.Add(TEXT("Clothing_") + MeshName, ClothingMesh);
		}
	}

	USubobjectDataSubsystem* SubobjectDataSubsystem = USubobjectDataSubsystem::Get();
	TArray<FSubobjectDataHandle> SubobjectDataHandles;
	SubobjectDataSubsystem->GatherSubobjectData(ActorCDO, SubobjectDataHandles);
	SubobjectDataHandles = TSet<FSubobjectDataHandle>(SubobjectDataHandles).Array();

	FSubobjectDataHandle RootHandle;
	FSubobjectDataHandle BodyHandle;
	TMap<FString, TPair<FSubobjectDataHandle, UActorComponent*>> ExistingClothingComponents;
	for (const FSubobjectDataHandle& Handle : SubobjectDataHandles)
	{
		const FSubobjectData* Data = Handle.GetData();
		if (!Data)
		{
			continue;
		}
		if (Data->IsActor())
		{
			RootHandle = Handle;
			continue;
		}
		if (UActorComponent* ActorComponent = const_cast<UActorComponent*>(Data->GetObjectForBlueprint<UActorComponent>(GeneratedBP)))
		{
			FString ComponentName = ActorComponent->GetName();
			ComponentName.RemoveFromEnd(UActorComponent::ComponentTemplateNameSuffix);
			if (ComponentName == TEXT("Body"))
			{
				BodyHandle = Handle;
			}
			else if (ComponentName.StartsWith(TEXT("Clothing_")))
			{
				ExistingClothingComponents.Add(ComponentName, TPair<FSubobjectDataHandle, UActorComponent*>(Handle, ActorComponent));
			}
		}
	}
	if (!BodyHandle.IsValid())
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("AttachClothingToCharacterBlueprint: %s has no Body component"), *GeneratedBP->GetName());
		return false;
	}

	for (const TPair<FString, USkeletalMesh*>& Desired : DesiredComponents)
	{
		if (TPair<FSubobjectDataHandle, UActorComponent*>* Existing = ExistingClothingComponents.Find(Desired.Key))
		{
			SetComponentTemplateSkeletalMesh(Existing->Value, Desired.Value);
			ExistingClothingComponents.Remove(Desired.Key);
			continue;
		}

		FAddNewSubobjectParams AddParams;
		AddParams.ParentHandle = BodyHandle;
		AddParams.NewClass = USkeletalMeshComponent::StaticClass();
		AddParams.BlueprintContext = GeneratedBP;
		FText FailReason;
		FSubobjectDataHandle NewHandle = SubobjectDataSubsystem->AddNewSubobject(AddParams, FailReason);
		if (!NewHandle.IsValid())
		{
			UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("AttachClothingToCharacterBlueprint: failed to add component for %s: %s"),
				*Desired.Value->GetName(), *FailReason.ToString());
			continue;
		}
		SubobjectDataSubsystem->RenameSubobject(NewHandle, FText::FromString(Desired.Key));
		if (const FSubobjectData* NewData = NewHandle.GetData())
		{
			if (UActorComponent* NewComponent = const_cast<UActorComponent*>(NewData->GetObjectForBlueprint<UActorComponent>(GeneratedBP)))
			{
				SetComponentTemplateSkeletalMesh(NewComponent, Desired.Value);
			}
		}
	}

	// Whatever is left no longer matches a clothing mesh — the outfit changed in Daz.
	if (ExistingClothingComponents.Num() > 0 && RootHandle.IsValid())
	{
		TArray<FSubobjectDataHandle> StaleHandles;
		for (const TPair<FString, TPair<FSubobjectDataHandle, UActorComponent*>>& Stale : ExistingClothingComponents)
		{
			UE_LOG(LogDazToUnrealBlueprintUtils, Log, TEXT("AttachClothingToCharacterBlueprint: removing stale component %s"), *Stale.Key);
			StaleHandles.Add(Stale.Value.Key);
		}
		SubobjectDataSubsystem->DeleteSubobjects(RootHandle, StaleHandles, GeneratedBP);
	}

	FKismetEditorUtilities::CompileBlueprint(GeneratedBP);
	GeneratedBP->MarkPackageDirty();
	return true;
#else
	UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("AttachClothingToCharacterBlueprint requires Unreal Engine 5.8 or later"));
	return false;
#endif
}

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 8
// Average color of a texture's top mip in sRGB space (EstimateSkinTone expects sRGB).
static bool GetAverageTextureColor(UTexture2D* InTexture, FLinearColor& OutColor)
{
	if (!InTexture || !InTexture->Source.IsValid())
	{
		return false;
	}

	FImage MipImage;
	if (!InTexture->Source.GetMipImage(MipImage, 0))
	{
		return false;
	}

	FImage Converted;
	MipImage.CopyTo(Converted, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
	const TArrayView64<FColor> Colors = Converted.AsBGRA8();
	if (Colors.Num() == 0)
	{
		return false;
	}

	// Stride the pixels — an approximate average is plenty for tone estimation.
	const int64 Stride = FMath::Max<int64>(1, Colors.Num() / 65536);
	double SumR = 0.0, SumG = 0.0, SumB = 0.0;
	int64 SampleCount = 0;
	for (int64 Index = 0; Index < Colors.Num(); Index += Stride)
	{
		SumR += Colors[Index].R;
		SumG += Colors[Index].G;
		SumB += Colors[Index].B;
		SampleCount++;
	}

	OutColor = FLinearColor(SumR / SampleCount / 255.0, SumG / SampleCount / 255.0, SumB / SampleCount / 255.0);
	return true;
}

// Find the Daz character's skin diffuse texture, preferring face/head material slots.
static UTexture2D* FindDazSkinDiffuseTexture(USkeletalMesh* InDazMesh)
{
	if (!InDazMesh)
	{
		return nullptr;
	}

	UTexture2D* FallbackDiffuse = nullptr;
	for (const FSkeletalMaterial& Material : InDazMesh->GetMaterials())
	{
		UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(Material.MaterialInterface);
		if (!MaterialInstance)
		{
			continue;
		}

		const FString SlotName = Material.MaterialSlotName.ToString();
		const bool bIsFaceSlot = SlotName.Contains(TEXT("Face")) || SlotName.Contains(TEXT("Head"));

		for (const FTextureParameterValue& TextureParam : MaterialInstance->TextureParameterValues)
		{
			if (TextureParam.ParameterInfo.Name.ToString().Contains(TEXT("Diffuse")))
			{
				if (UTexture2D* Texture = Cast<UTexture2D>(TextureParam.ParameterValue))
				{
					if (bIsFaceSlot)
					{
						return Texture;
					}
					if (!FallbackDiffuse)
					{
						FallbackDiffuse = Texture;
					}
				}
			}
		}
	}
	return FallbackDiffuse;
}

// Persist a (typically transient) material instance as a MIC asset, remapping any transient
// textures to their persisted duplicates. Returns the source unchanged if it is already a
// saved asset. Mirrors the persist step of the engine's ExportMaterials.
static UMaterialInterface* PersistMaterialInstance(UMaterialInterface* InSource, const FString& InPackagePath, const FString& InAssetName, const TMap<UTexture2D*, UTexture2D*>& InTextureRemap)
{
	if (!InSource)
	{
		return nullptr;
	}

	// Already a real asset (e.g. a topology or plugin-content material) — use as-is.
	if (InSource->GetPackage() != GetTransientPackage() && !InSource->HasAnyFlags(RF_Transient))
	{
		return InSource;
	}

	UMaterialInstance* SourceInstance = Cast<UMaterialInstance>(InSource);
	if (!SourceInstance || !SourceInstance->Parent)
	{
		return nullptr;
	}

	UPackage* NewPackage = CreatePackage(*InPackagePath);
	UMaterialInstanceConstant* NewMaterial = NewObject<UMaterialInstanceConstant>(NewPackage, FName(*InAssetName));
	NewMaterial->SetParentEditorOnly(SourceInstance->Parent);
	NewMaterial->ScalarParameterValues = SourceInstance->ScalarParameterValues;
	NewMaterial->VectorParameterValues = SourceInstance->VectorParameterValues;
	NewMaterial->TextureParameterValues = SourceInstance->TextureParameterValues;

	for (FTextureParameterValue& TextureParam : NewMaterial->TextureParameterValues)
	{
		if (UTexture2D* const* PersistentTexture = InTextureRemap.Find(Cast<UTexture2D>(TextureParam.ParameterValue)))
		{
			TextureParam.ParameterValue = *PersistentTexture;
		}
	}

	NewMaterial->SetFlags(RF_Public | RF_Standalone);
	FAssetRegistryModule::AssetCreated(NewMaterial);
	NewMaterial->PostEditChange();
	NewMaterial->MarkPackageDirty();
	return NewMaterial;
}

// Copy the generated mesh's materials onto matching slots of the exported target mesh.
static void AssignGeneratedMaterials(USkeletalMesh* InGeneratedMesh, USkeletalMesh* InTargetMesh, const FString& InExportPath, const TMap<UTexture2D*, UTexture2D*>& InTextureRemap)
{
	if (!InGeneratedMesh || !InTargetMesh)
	{
		return;
	}

	TArray<FSkeletalMaterial> TargetMaterials = InTargetMesh->GetMaterials();
	bool bChanged = false;
	for (const FSkeletalMaterial& SourceMaterial : InGeneratedMesh->GetMaterials())
	{
		for (FSkeletalMaterial& TargetMaterial : TargetMaterials)
		{
			if (TargetMaterial.MaterialSlotName == SourceMaterial.MaterialSlotName)
			{
				const FString AssetName = FString::Printf(TEXT("MI_%s_%s"), *InTargetMesh->GetName(), *SourceMaterial.MaterialSlotName.ToString());
				if (UMaterialInterface* Persisted = PersistMaterialInstance(SourceMaterial.MaterialInterface, InExportPath / TEXT("Materials") / AssetName, AssetName, InTextureRemap))
				{
					TargetMaterial.MaterialInterface = Persisted;
					bChanged = true;
				}
				break;
			}
		}
	}

	if (bChanged)
	{
		InTargetMesh->SetMaterials(TargetMaterials);
		InTargetMesh->PostEditChange();
		InTargetMesh->MarkPackageDirty();
	}
}
#endif

bool UDazToUnrealBlueprintUtils::ApplyMetaHumanSkinMaterials(UObject* MetaHumanCharacter, USkeletalMesh* HeadMesh, USkeletalMesh* BodyMesh, USkeletalMesh* DazSourceMesh, const FString& TextureExportPath)
{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 8
	UMetaHumanCharacter* Character = Cast<UMetaHumanCharacter>(MetaHumanCharacter);
	if (!Character || (!HeadMesh && !BodyMesh))
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("ApplyMetaHumanSkinMaterials: character and at least one mesh are required"));
		return false;
	}

	UMetaHumanCharacterEditorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UMetaHumanCharacterEditorSubsystem>() : nullptr;
	if (!Subsystem)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("ApplyMetaHumanSkinMaterials: MetaHumanCharacterEditorSubsystem unavailable"));
		return false;
	}

	bool bAddedForEdit = false;
	if (!Subsystem->IsObjectAddedForEditing(Character))
	{
		if (!Subsystem->TryAddObjectToEdit(Character))
		{
			UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("ApplyMetaHumanSkinMaterials: unable to open %s for edit"), *Character->GetName());
			return false;
		}
		bAddedForEdit = true;
	}

	bool bSucceeded = false;
	{
		// The character was created headlessly and is still in the "Topology" preview
		// material mode — texture synthesis only produces textures with source data in
		// Editable ("Skin") mode (see the TODO above the source-data ensure in
		// UMetaHumanCharacterEditorSubsystem::TryGenerateCharacterAssets).
		Subsystem->UpdateCharacterPreviewMaterial(Character, EMetaHumanCharacterSkinPreviewMaterial::Editable);

		// Estimate the skin tone from the Daz diffuse and apply it so the synthesized
		// textures match the source character. All local (texture synthesis model ships
		// with the plugin); skipped gracefully if synthesis is unavailable.
		FLinearColor DazSkinTone;
		if (Subsystem->IsTextureSynthesisEnabled() && GetAverageTextureColor(FindDazSkinDiffuseTexture(DazSourceMesh), DazSkinTone))
		{
			const FVector2f SkinToneUV = Subsystem->EstimateSkinTone(DazSkinTone, 0);
			Character->SkinSettings.Skin.U = SkinToneUV.X;
			Character->SkinSettings.Skin.V = SkinToneUV.Y;
			UE_LOG(LogDazToUnrealBlueprintUtils, Log, TEXT("ApplyMetaHumanSkinMaterials: estimated skin tone (%g, %g) -> UV (%g, %g)"), DazSkinTone.R, DazSkinTone.G, SkinToneUV.X, SkinToneUV.Y);
		}
		else
		{
			UE_LOG(LogDazToUnrealBlueprintUtils, Log, TEXT("ApplyMetaHumanSkinMaterials: skin tone estimation skipped (no synthesis model or no Daz diffuse found)"));
		}

		// Commit (not just apply): CommitSkinSettings synthesizes the face AND body
		// textures for the current settings and stores them on the character (it calls
		// StoreSynthesizedTextures internally), which is what gives the
		// generated-assets textures valid source data.
		Subsystem->CommitSkinSettings(Character, Character->SkinSettings);

		// Generate the character's meshes/materials/textures (local, transient).
		FMetaHumanCharacterGeneratedAssets GeneratedAssets;
		if (Subsystem->TryGenerateCharacterAssets(Character, GetTransientPackage(), GeneratedAssets))
		{
			// Persist the synthesized textures and remap material references to them.
			TMap<UTexture2D*, UTexture2D*> TextureRemap;
			auto PersistTextures = [&TextureRemap, &TextureExportPath](const auto& InTextureMap, const FString& InSubFolder)
			{
				for (const auto& TexturePair : InTextureMap)
				{
					if (UTexture2D* SourceTexture = TexturePair.Value.Get())
					{
						const FString TextureName = SourceTexture->GetFName().GetPlainNameString();
						const FString PackagePath = TextureExportPath / InSubFolder / TextureName;
						UPackage* NewPackage = CreatePackage(*PackagePath);
						if (UTexture2D* PersistentTexture = DuplicateObject(SourceTexture, NewPackage, FName(*TextureName)))
						{
							PersistentTexture->SetFlags(RF_Public | RF_Standalone);
							FAssetRegistryModule::AssetCreated(PersistentTexture);
							PersistentTexture->MarkPackageDirty();
							TextureRemap.Add(SourceTexture, PersistentTexture);
						}
					}
				}
			};
			PersistTextures(GeneratedAssets.SynthesizedFaceTextures, TEXT("FaceTextures"));
			PersistTextures(GeneratedAssets.BodyTextures, TEXT("BodyTextures"));

			AssignGeneratedMaterials(GeneratedAssets.FaceMesh, HeadMesh, TextureExportPath, TextureRemap);
			AssignGeneratedMaterials(GeneratedAssets.BodyMesh, BodyMesh, TextureExportPath, TextureRemap);
			bSucceeded = true;
		}
		else
		{
			UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("ApplyMetaHumanSkinMaterials: TryGenerateCharacterAssets failed for %s"), *Character->GetName());
		}
	}

	if (bAddedForEdit)
	{
		Subsystem->RemoveObjectToEdit(Character);
	}
	return bSucceeded;
#else
	UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("ApplyMetaHumanSkinMaterials requires Unreal Engine 5.8 or later"));
	return false;
#endif
}

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 8
// The conversion source data lives in the editor-only user data array (cook-stripped).
// GetAssetUserDataOfClass searches the runtime AND editor-only arrays.
static UDazMetaHumanSourceData* FindMetaHumanSourceData(USkeletalMesh* InMesh)
{
	return InMesh ? Cast<UDazMetaHumanSourceData>(InMesh->GetAssetUserDataOfClass(UDazMetaHumanSourceData::StaticClass())) : nullptr;
}

// Append to the mesh's AssetUserDataEditorOnly array. There is no public mutator for it
// (the GetAssetUserDataEditorOnly accessor is protected and AddAssetUserData only feeds
// the cooked runtime array — where our editor-module class must never end up), so go
// through the reflected property.
static bool AddEditorOnlyAssetUserData(USkeletalMesh* InMesh, UAssetUserData* InUserData)
{
	FArrayProperty* ArrayProperty = FindFProperty<FArrayProperty>(USkeletalMesh::StaticClass(), TEXT("AssetUserDataEditorOnly"));
	FObjectProperty* InnerProperty = ArrayProperty ? CastField<FObjectProperty>(ArrayProperty->Inner) : nullptr;
	if (!InnerProperty)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("AddEditorOnlyAssetUserData: AssetUserDataEditorOnly property not found on USkeletalMesh"));
		return false;
	}
	FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(InMesh));
	const int32 Index = ArrayHelper.AddValue();
	InnerProperty->SetObjectPropertyValue(ArrayHelper.GetRawPtr(Index), InUserData);
	return true;
}

// Find the Body skeletal mesh component template on a blueprint produced by
// AssembleMetaHumanCharacterBlueprint.
static USkeletalMeshComponent* FindBodyComponentTemplate(UBlueprint* GeneratedBP)
{
	AActor* ActorCDO = (GeneratedBP && GeneratedBP->GeneratedClass) ? GeneratedBP->GeneratedClass->GetDefaultObject<AActor>() : nullptr;
	if (!ActorCDO)
	{
		return nullptr;
	}

	USubobjectDataSubsystem* SubobjectDataSubsystem = USubobjectDataSubsystem::Get();
	TArray<FSubobjectDataHandle> SubobjectDataHandles;
	SubobjectDataSubsystem->GatherSubobjectData(ActorCDO, SubobjectDataHandles);
	for (const FSubobjectDataHandle& Handle : SubobjectDataHandles)
	{
		const FSubobjectData* Data = Handle.GetData();
		if (!Data || Data->IsActor())
		{
			continue;
		}
		if (UActorComponent* ActorComponent = const_cast<UActorComponent*>(Data->GetObjectForBlueprint<UActorComponent>(GeneratedBP)))
		{
			FString ComponentName = ActorComponent->GetName();
			ComponentName.RemoveFromEnd(UActorComponent::ComponentTemplateNameSuffix);
			if (ComponentName == TEXT("Body"))
			{
				return Cast<USkeletalMeshComponent>(ActorComponent);
			}
		}
	}
	return nullptr;
}
#endif

bool UDazToUnrealBlueprintUtils::SetMetaHumanBodySourceData(USkeletalMesh* BodyMesh, USkeletalMesh* OriginalBodyMesh, const FString& MetaHumanCharacterPath, UTexture2D* HideMaskTexture, const TArray<FString>& TransparentClothingItems, float MaxCullValue, float MinKeepValue, float MaxShrinkDistance)
{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 8
	if (!BodyMesh)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("SetMetaHumanBodySourceData: body mesh is required"));
		return false;
	}

	UDazMetaHumanSourceData* SourceData = FindMetaHumanSourceData(BodyMesh);
	if (!SourceData)
	{
		SourceData = NewObject<UDazMetaHumanSourceData>(BodyMesh);
		if (!AddEditorOnlyAssetUserData(BodyMesh, SourceData))
		{
			return false;
		}
	}

	SourceData->OriginalBodyMesh = OriginalBodyMesh ? OriginalBodyMesh : BodyMesh;
	SourceData->CulledBodyMesh = nullptr;
	SourceData->MetaHumanCharacter = FSoftObjectPath(MetaHumanCharacterPath);
	SourceData->HideMaskTexture = HideMaskTexture;
	SourceData->TransparentClothingItems = TransparentClothingItems;
	SourceData->HideMaskMaxCullValue = MaxCullValue;
	SourceData->HideMaskMinKeepValue = MinKeepValue;
	SourceData->HideMaskMaxShrinkDistance = MaxShrinkDistance;
	SourceData->bCullingBaked = false;

	BodyMesh->MarkPackageDirty();
	return true;
#else
	UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("SetMetaHumanBodySourceData requires Unreal Engine 5.8 or later"));
	return false;
#endif
}

UTexture2D* UDazToUnrealBlueprintUtils::CreateHideMaskFromCoverage(UTexture2D* CoverageTexture, const FString& AssetPathAndName, int32 ErodePixels, int32 BlurPixels)
{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 8
	if (!CoverageTexture || !CoverageTexture->Source.IsValid())
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("CreateHideMaskFromCoverage: coverage texture has no source data"));
		return nullptr;
	}

	FImage MipImage;
	if (!CoverageTexture->Source.GetMipImage(MipImage, 0))
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("CreateHideMaskFromCoverage: unable to read coverage image"));
		return nullptr;
	}

	// Same-gamma conversion: the coverage bake is effectively binary (white where clothing
	// projected, black elsewhere), so no gamma correction should shift the threshold.
	FImage Converted;
	MipImage.CopyTo(Converted, ERawImageFormat::BGRA8, MipImage.GetGammaSpace());
	const int32 Width = Converted.SizeX;
	const int32 Height = Converted.SizeY;
	const TArrayView64<FColor> Pixels = Converted.AsBGRA8();
	if (Width <= 0 || Height <= 0 || Pixels.Num() != int64(Width) * Height)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("CreateHideMaskFromCoverage: unexpected coverage image layout"));
		return nullptr;
	}

	// Threshold: covered where any channel is bright (the bake writes white on coverage).
	const int32 NumPixels = Width * Height;
	TArray<uint8> Covered;
	Covered.SetNumUninitialized(NumPixels);
	for (int32 Index = 0; Index < NumPixels; Index++)
	{
		const FColor& Color = Pixels[Index];
		Covered[Index] = (FMath::Max3(Color.R, Color.G, Color.B) > 127) ? 1 : 0;
	}

	// Erode the covered region (separable min filter) — a safety margin so geometry near
	// clothing borders survives; over-culling is the failure mode that shows skin gaps.
	auto ErodeAxis = [Width, Height](const TArray<uint8>& InValues, TArray<uint8>& OutValues, int32 Radius, bool bHorizontal)
	{
		for (int32 Y = 0; Y < Height; Y++)
		{
			for (int32 X = 0; X < Width; X++)
			{
				uint8 Value = 1;
				for (int32 Offset = -Radius; Offset <= Radius && Value; Offset++)
				{
					const int32 SampleX = bHorizontal ? FMath::Clamp(X + Offset, 0, Width - 1) : X;
					const int32 SampleY = bHorizontal ? Y : FMath::Clamp(Y + Offset, 0, Height - 1);
					Value &= InValues[SampleY * Width + SampleX];
				}
				OutValues[Y * Width + X] = Value;
			}
		}
	};
	if (ErodePixels > 0)
	{
		TArray<uint8> Scratch;
		Scratch.SetNumUninitialized(NumPixels);
		ErodeAxis(Covered, Scratch, ErodePixels, true);
		ErodeAxis(Scratch, Covered, ErodePixels, false);
	}

	// Invert into hide-mask semantics (0 = covered/hidden, 255 = visible)...
	TArray<uint8> Mask;
	Mask.SetNumUninitialized(NumPixels);
	for (int32 Index = 0; Index < NumPixels; Index++)
	{
		Mask[Index] = Covered[Index] ? 0 : 255;
	}

	// ...and blur to create the gradient band between MaxCullValue and MinKeepValue that
	// drives vertex shrinking at clothing borders (separable box blur).
	auto BlurAxis = [Width, Height](const TArray<uint8>& InValues, TArray<uint8>& OutValues, int32 Radius, bool bHorizontal)
	{
		const int32 WindowSize = Radius * 2 + 1;
		for (int32 Y = 0; Y < Height; Y++)
		{
			for (int32 X = 0; X < Width; X++)
			{
				uint32 Sum = 0;
				for (int32 Offset = -Radius; Offset <= Radius; Offset++)
				{
					const int32 SampleX = bHorizontal ? FMath::Clamp(X + Offset, 0, Width - 1) : X;
					const int32 SampleY = bHorizontal ? Y : FMath::Clamp(Y + Offset, 0, Height - 1);
					Sum += InValues[SampleY * Width + SampleX];
				}
				OutValues[Y * Width + X] = uint8(Sum / WindowSize);
			}
		}
	};
	if (BlurPixels > 0)
	{
		TArray<uint8> Scratch;
		Scratch.SetNumUninitialized(NumPixels);
		BlurAxis(Mask, Scratch, BlurPixels, true);
		BlurAxis(Scratch, Mask, BlurPixels, false);
	}

	TArray<FColor> MaskPixels;
	MaskPixels.SetNumUninitialized(NumPixels);
	for (int32 Index = 0; Index < NumPixels; Index++)
	{
		const uint8 Value = Mask[Index];
		MaskPixels[Index] = FColor(Value, Value, Value, 255);
	}

	// AssetPathAndName may be /Game/Path/Name or /Game/Path/Name.Name
	FString PackageName = AssetPathAndName;
	int32 DotIndex = INDEX_NONE;
	if (PackageName.FindChar(TEXT('.'), DotIndex))
	{
		PackageName.LeftInline(DotIndex);
	}
	const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);

	UPackage* Package = CreatePackage(*PackageName);
	UTexture2D* MaskTexture = FindObject<UTexture2D>(Package, *AssetName);
	const bool bExisted = MaskTexture != nullptr;
	if (!MaskTexture)
	{
		MaskTexture = NewObject<UTexture2D>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	}

	MaskTexture->Source.Init(Width, Height, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(MaskPixels.GetData()));
	MaskTexture->SRGB = false;
	MaskTexture->CompressionSettings = TC_Masks;
	// The cull threshold must be stable at every draw distance — mip averaging along the
	// covered/visible border would flicker geometry in and out.
	MaskTexture->MipGenSettings = TMGS_NoMipmaps;
	MaskTexture->UpdateResource();
	MaskTexture->PostEditChange();
	if (!bExisted)
	{
		FAssetRegistryModule::AssetCreated(MaskTexture);
	}
	MaskTexture->MarkPackageDirty();
	return MaskTexture;
#else
	UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("CreateHideMaskFromCoverage requires Unreal Engine 5.8 or later"));
	return nullptr;
#endif
}

UTexture2D* UDazToUnrealBlueprintUtils::BakeClothingGapCoverage(UDynamicMesh* BodyBakeMesh, UDynamicMesh* ClothingMesh, int32 Resolution, float MaxGapDistance, float MinFacingDot, float InwardOffset)
{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 8
	if (!BodyBakeMesh || !ClothingMesh || MaxGapDistance <= 0.0f)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("BakeClothingGapCoverage: body mesh, clothing mesh and a positive reach are required"));
		return nullptr;
	}
	Resolution = FMath::Clamp(Resolution, 64, 8192);

	UE::Geometry::FDynamicMesh3 BodyMesh3;
	BodyBakeMesh->ProcessMesh([&BodyMesh3](const UE::Geometry::FDynamicMesh3& Mesh) { BodyMesh3 = Mesh; });
	UE::Geometry::FDynamicMesh3 ClothMesh3;
	ClothingMesh->ProcessMesh([&ClothMesh3](const UE::Geometry::FDynamicMesh3& Mesh) { ClothMesh3 = Mesh; });
	if (ClothMesh3.TriangleCount() == 0)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("BakeClothingGapCoverage: clothing mesh has no triangles"));
		return nullptr;
	}
	const UE::Geometry::FDynamicMeshUVOverlay* UVOverlay =
		BodyMesh3.HasAttributes() ? BodyMesh3.Attributes()->PrimaryUV() : nullptr;
	if (!UVOverlay)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("BakeClothingGapCoverage: body mesh has no UV layer"));
		return nullptr;
	}

	// Rasterize the body's UV charts: per texel, the 3D surface point + face normal.
	// Texel (X, Y) samples UV ((X+0.5)/Res, (Y+0.5)/Res) — the same convention the
	// material uses to sample the finished mask, so no V flip anywhere.
	const int32 NumPixels = Resolution * Resolution;
	TArray<FVector3f> TexelPositions;
	TexelPositions.SetNumZeroed(NumPixels);
	TArray<FVector3f> TexelNormals;
	TexelNormals.SetNumZeroed(NumPixels);
	TArray<uint8> TexelValid;
	TexelValid.SetNumZeroed(NumPixels);

	for (const int32 TriangleID : BodyMesh3.TriangleIndicesItr())
	{
		if (!UVOverlay->IsSetTriangle(TriangleID))
		{
			continue;
		}
		const UE::Geometry::FIndex3i UVTriangle = UVOverlay->GetTriangle(TriangleID);
		const FVector2f UV0 = UVOverlay->GetElement(UVTriangle.A);
		const FVector2f UV1 = UVOverlay->GetElement(UVTriangle.B);
		const FVector2f UV2 = UVOverlay->GetElement(UVTriangle.C);
		const UE::Geometry::FIndex3i Triangle = BodyMesh3.GetTriangle(TriangleID);
		const FVector3d P0 = BodyMesh3.GetVertex(Triangle.A);
		const FVector3d P1 = BodyMesh3.GetVertex(Triangle.B);
		const FVector3d P2 = BodyMesh3.GetVertex(Triangle.C);
		const FVector3d FaceNormal = BodyMesh3.GetTriNormal(TriangleID);
		if (FaceNormal.IsNearlyZero())
		{
			continue;
		}

		// Pixel-space UV corners; texel centers sit at integer+0.5.
		const double Ax = double(UV0.X) * Resolution, Ay = double(UV0.Y) * Resolution;
		const double Bx = double(UV1.X) * Resolution, By = double(UV1.Y) * Resolution;
		const double Cx = double(UV2.X) * Resolution, Cy = double(UV2.Y) * Resolution;
		const double Denominator = (Bx - Ax) * (Cy - Ay) - (By - Ay) * (Cx - Ax);
		if (FMath::Abs(Denominator) < UE_DOUBLE_SMALL_NUMBER)
		{
			continue;
		}
		const int32 MinX = FMath::Clamp(FMath::FloorToInt32(FMath::Min3(Ax, Bx, Cx) - 0.5), 0, Resolution - 1);
		const int32 MaxX = FMath::Clamp(FMath::CeilToInt32(FMath::Max3(Ax, Bx, Cx) + 0.5), 0, Resolution - 1);
		const int32 MinY = FMath::Clamp(FMath::FloorToInt32(FMath::Min3(Ay, By, Cy) - 0.5), 0, Resolution - 1);
		const int32 MaxY = FMath::Clamp(FMath::CeilToInt32(FMath::Max3(Ay, By, Cy) + 0.5), 0, Resolution - 1);
		for (int32 Y = MinY; Y <= MaxY; Y++)
		{
			for (int32 X = MinX; X <= MaxX; X++)
			{
				const double Px = X + 0.5, Py = Y + 0.5;
				const double W1 = ((Px - Ax) * (Cy - Ay) - (Py - Ay) * (Cx - Ax)) / Denominator;
				const double W2 = ((Bx - Ax) * (Py - Ay) - (By - Ay) * (Px - Ax)) / Denominator;
				const double W0 = 1.0 - W1 - W2;
				const double Tolerance = -1e-4;
				if (W0 < Tolerance || W1 < Tolerance || W2 < Tolerance)
				{
					continue;
				}
				const FVector3d Position = P0 * W0 + P1 * W1 + P2 * W2;
				const int32 Index = Y * Resolution + X;
				TexelPositions[Index] = FVector3f(Position);
				TexelNormals[Index] = FVector3f(FaceNormal);
				TexelValid[Index] = 1;
			}
		}
	}

	// Raycast every valid texel along its normal against the clothing, starting
	// InwardOffset inside the skin. Grazing hits (garment surface nearly parallel
	// to the ray) are skipped and the ray continues — up to a few hops.
	UE::Geometry::FDynamicMeshAABBTree3 ClothBVH(&ClothMesh3, true);
	TArray<uint8> EncodedGap;
	EncodedGap.Init(255, NumPixels);
	std::atomic<int32> CoveredCount(0);
	ParallelFor(Resolution, [&](int32 Y)
	{
		UE::Geometry::IMeshSpatial::FQueryOptions QueryOptions;
		for (int32 X = 0; X < Resolution; X++)
		{
			const int32 Index = Y * Resolution + X;
			if (!TexelValid[Index])
			{
				continue;
			}
			const FVector3d Normal(TexelNormals[Index]);
			FVector3d Origin = FVector3d(TexelPositions[Index]) - Normal * double(InwardOffset);
			double Traveled = 0.0;
			double Remaining = double(InwardOffset) + double(MaxGapDistance);
			for (int32 Hop = 0; Hop < 8 && Remaining > 0.0; Hop++)
			{
				QueryOptions.MaxDistance = Remaining;
				double HitT = 0.0;
				int32 HitTriangleID = -1;
				if (!ClothBVH.FindNearestHitTriangle(FRay3d(Origin, Normal), HitT, HitTriangleID, QueryOptions))
				{
					break;
				}
				const FVector3d ClothNormal = ClothMesh3.GetTriNormal(HitTriangleID);
				if (FMath::Abs(ClothNormal.Dot(Normal)) >= double(MinFacingDot))
				{
					const double Gap = FMath::Max(0.0, Traveled + HitT - double(InwardOffset));
					EncodedGap[Index] = uint8(FMath::Clamp(FMath::RoundToInt32(Gap / double(MaxGapDistance) * 254.0), 0, 254));
					CoveredCount++;
					break;
				}
				const double Step = HitT + 0.01;
				Origin += Normal * Step;
				Traveled += Step;
				Remaining -= Step;
			}
		}
	});

	TArray<FColor> OutPixels;
	OutPixels.SetNumUninitialized(NumPixels);
	for (int32 Index = 0; Index < NumPixels; Index++)
	{
		const uint8 Value = EncodedGap[Index];
		OutPixels[Index] = FColor(Value, Value, Value, 255);
	}
	UTexture2D* Result = NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient);
	Result->Source.Init(Resolution, Resolution, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(OutPixels.GetData()));
	Result->SRGB = false;
	Result->CompressionSettings = TC_Masks;
	Result->MipGenSettings = TMGS_NoMipmaps;
	Result->UpdateResource();
	UE_LOG(LogDazToUnrealBlueprintUtils, Log, TEXT("BakeClothingGapCoverage: %d covered texels (%d rasterized) at %dpx, reach %.1f, facing dot %.2f"),
		CoveredCount.load(), int32(Algo::Accumulate(TexelValid, 0)), Resolution, MaxGapDistance, MinFacingDot);
	return Result;
#else
	UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("BakeClothingGapCoverage requires Unreal Engine 5.8 or later"));
	return nullptr;
#endif
}

// The mesh-to-mesh correspondence below is a modified version of Epic's texture bake
// correspondence in the GeometryProcessing plugin — original code:
//   Engine/Plugins/Runtime/GeometryProcessing/Source/DynamicMesh/Private/Sampling/MeshBakerCommon.cpp
//     (UE::Geometry::GetDetailMeshTrianglePoint_Raycast — the ray strategy)
//   Engine/Plugins/Runtime/GeometryProcessing/Source/DynamicMesh/Private/Sampling/MeshMapBaker.cpp
//     (FMeshMapBaker's correspondence sample evaluation, reached via GeometryScript BakeTexture)
// Three deliberate changes for the aligned Daz->MetaHuman likeness bake:
//   1. FACING REJECTION: only source triangles whose normal agrees with the texel normal
//      (dot >= MinFacingDot) can be sampled. Epic's version accepts the first hit of any
//      orientation — their own TODO in GetDetailMeshTrianglePoint_Raycast reads "should we
//      check normals here? inverse normal should probably not be considered valid" — which
//      is what let finger-side texels sample the neighbouring finger and fingertip texels
//      sample nail plates.
//   2. ADAPTIVE ray-vs-nearest arbitration: both queries run and the along-normal ray
//      hit wins whenever it lands within max(3x the nearest-point distance,
//      PreferRayDistance). Epic tries an inward ray cast from ProjectionDistance OUTSIDE
//      the surface first; between close parallel surfaces (adjacent fingers, ~5mm apart)
//      that start point already sits past the neighbouring finger, and the neighbour's
//      far side both intercepts the ray and PASSES a facing test — our rays start AT the
//      texel position, and the acceptance limit scales with how well the meshes align
//      locally: on a snap-fitted finger the own-skin nearest point is ~1-2mm, so the
//      limit stays under the neighbour's far wall (a finger thickness+ away); where
//      nearest-point-only failed, the ray now wins instead — a nail texel's
//      laterally-nearest facing-compatible surface is the fingertip skin ~1mm beside the
//      plate while the plate sits mm ALONG the normal (nails erased, 2026-08-09), and a
//      laterally-drifted span (mid-calf, source 1-4cm off — parent-carried, not fitted)
//      collapses many texels onto the same source ridge under nearest-point (smeared
//      dash bands) while the ray gives the clean shrink-wrap projection.
//   3. FOOTPRINT-FILTERED SAMPLING: the source-vs-target texel density mismatch (a Daz
//      UDIM tile spends 4096px on one limb, the MetaHuman body atlas ~a quarter of that)
//      means one bilinear tap per texel point-samples a higher-frequency image — the skin
//      pore/detail grid aliases into regular moire stripe bands (calves/back,
//      2026-08-09). Each texel instead estimates its source-UV footprint from its UV
//      neighbours and samples a software mip chain trilinearly, like a GPU sampler would.
UTexture2D* UDazToUnrealBlueprintUtils::BakeBasecolorFacingFiltered(UDynamicMesh* TargetBakeMesh, UDynamicMesh* SourceMesh,
	const TArray<UTexture2D*>& MaterialIDTextures, int32 Resolution, float ProjectionDistance, float PreferRayDistance, float MinFacingDot, int32 GutterSize)
{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 8
	if (!TargetBakeMesh || !SourceMesh || ProjectionDistance <= 0.0f)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("BakeBasecolorFacingFiltered: target mesh, source mesh and a positive reach are required"));
		return nullptr;
	}
	Resolution = FMath::Clamp(Resolution, 64, 8192);
	GutterSize = FMath::Clamp(GutterSize, 0, 256);

	UE::Geometry::FDynamicMesh3 TargetMesh3;
	TargetBakeMesh->ProcessMesh([&TargetMesh3](const UE::Geometry::FDynamicMesh3& Mesh) { TargetMesh3 = Mesh; });
	UE::Geometry::FDynamicMesh3 SourceMesh3;
	SourceMesh->ProcessMesh([&SourceMesh3](const UE::Geometry::FDynamicMesh3& Mesh) { SourceMesh3 = Mesh; });
	if (SourceMesh3.TriangleCount() == 0)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("BakeBasecolorFacingFiltered: source mesh has no triangles"));
		return nullptr;
	}
	const UE::Geometry::FDynamicMeshUVOverlay* TargetUVOverlay =
		TargetMesh3.HasAttributes() ? TargetMesh3.Attributes()->PrimaryUV() : nullptr;
	const UE::Geometry::FDynamicMeshUVOverlay* SourceUVOverlay =
		SourceMesh3.HasAttributes() ? SourceMesh3.Attributes()->PrimaryUV() : nullptr;
	if (!TargetUVOverlay || !SourceUVOverlay)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("BakeBasecolorFacingFiltered: both meshes need a UV layer"));
		return nullptr;
	}
	const UE::Geometry::FDynamicMeshMaterialAttribute* SourceMaterialIDs =
		(SourceMesh3.HasAttributes() && SourceMesh3.Attributes()->HasMaterialID())
			? SourceMesh3.Attributes()->GetMaterialID() : nullptr;

	// Decode each material's diffuse into BGRA8/sRGB once; null or unreadable entries
	// stay invalid and their hits become neighbour-filled misses rather than black.
	TArray<FImage> SourceImages;
	SourceImages.SetNum(MaterialIDTextures.Num());
	TArray<uint8> SourceImageValid;
	SourceImageValid.SetNumZeroed(MaterialIDTextures.Num());
	for (int32 ImageIndex = 0; ImageIndex < MaterialIDTextures.Num(); ImageIndex++)
	{
		UTexture2D* Texture = MaterialIDTextures[ImageIndex];
		FImage MipImage;
		if (Texture && Texture->Source.IsValid() && Texture->Source.GetMipImage(MipImage, 0))
		{
			MipImage.CopyTo(SourceImages[ImageIndex], ERawImageFormat::BGRA8, EGammaSpace::sRGB);
			SourceImageValid[ImageIndex] = SourceImages[ImageIndex].AsBGRA8().Num() > 0 ? 1 : 0;
		}
	}

	// Software mip chains for the footprint-filtered sampling (design note 3). Level N
	// halves level N-1, stopping once the image bottoms out or at MaxSourceMipLevel
	// (32px footprints — far beyond any honest density mismatch; larger UV steps are
	// seam crossings and the sampler rejects them instead of chasing deeper mips).
	constexpr int32 MaxSourceMipLevel = 5;
	TArray<TArray<FImage>> SourceMipChains;
	SourceMipChains.SetNum(MaterialIDTextures.Num());
	for (int32 ImageIndex = 0; ImageIndex < MaterialIDTextures.Num(); ImageIndex++)
	{
		if (!SourceImageValid[ImageIndex])
		{
			continue;
		}
		SourceMipChains[ImageIndex].Reserve(MaxSourceMipLevel);
		const FImage* PreviousLevel = &SourceImages[ImageIndex];
		for (int32 Level = 1; Level <= MaxSourceMipLevel; Level++)
		{
			const int32 MipWidth = FMath::Max(1, SourceImages[ImageIndex].SizeX >> Level);
			const int32 MipHeight = FMath::Max(1, SourceImages[ImageIndex].SizeY >> Level);
			if (MipWidth == PreviousLevel->SizeX && MipHeight == PreviousLevel->SizeY)
			{
				break;
			}
			FImage& MipImage = SourceMipChains[ImageIndex].AddDefaulted_GetRef();
			FImageCore::ResizeImageAllocDest(*PreviousLevel, MipImage, MipWidth, MipHeight,
				ERawImageFormat::BGRA8, EGammaSpace::sRGB);
			PreviousLevel = &MipImage;
		}
	}

	// Rasterize the target's UV charts: per texel, the 3D surface point + SMOOTH
	// (vertex-interpolated) normal — flat face normals flip across concave creases
	// (underbutt, thigh folds) and the correspondence direction jitters with them,
	// banding the crease; the authored normal overlay varies continuously. Falls
	// back to the face normal when the overlay is missing/unset. Texel (X, Y)
	// samples UV ((X+0.5)/Res, (Y+0.5)/Res) — the same convention the material
	// uses to sample the finished texture, so no V flip anywhere (this block
	// otherwise matches BakeClothingGapCoverage).
	const UE::Geometry::FDynamicMeshNormalOverlay* TargetNormalOverlay =
		TargetMesh3.HasAttributes() ? TargetMesh3.Attributes()->PrimaryNormals() : nullptr;
	const int32 NumPixels = Resolution * Resolution;
	TArray<FVector3f> TexelPositions;
	TexelPositions.SetNumZeroed(NumPixels);
	TArray<FVector3f> TexelNormals;
	TexelNormals.SetNumZeroed(NumPixels);
	// 0 = background, 1 = rasterized but no valid correspondence, 2 = baked
	TArray<uint8> TexelState;
	TexelState.SetNumZeroed(NumPixels);

	for (const int32 TriangleID : TargetMesh3.TriangleIndicesItr())
	{
		if (!TargetUVOverlay->IsSetTriangle(TriangleID))
		{
			continue;
		}
		const UE::Geometry::FIndex3i UVTriangle = TargetUVOverlay->GetTriangle(TriangleID);
		const FVector2f UV0 = TargetUVOverlay->GetElement(UVTriangle.A);
		const FVector2f UV1 = TargetUVOverlay->GetElement(UVTriangle.B);
		const FVector2f UV2 = TargetUVOverlay->GetElement(UVTriangle.C);
		const UE::Geometry::FIndex3i Triangle = TargetMesh3.GetTriangle(TriangleID);
		const FVector3d P0 = TargetMesh3.GetVertex(Triangle.A);
		const FVector3d P1 = TargetMesh3.GetVertex(Triangle.B);
		const FVector3d P2 = TargetMesh3.GetVertex(Triangle.C);
		const FVector3d FaceNormal = TargetMesh3.GetTriNormal(TriangleID);
		if (FaceNormal.IsNearlyZero())
		{
			continue;
		}
		FVector3f N0 = FVector3f(FaceNormal), N1 = N0, N2 = N0;
		if (TargetNormalOverlay != nullptr && TargetNormalOverlay->IsSetTriangle(TriangleID))
		{
			const UE::Geometry::FIndex3i NormalTriangle = TargetNormalOverlay->GetTriangle(TriangleID);
			N0 = TargetNormalOverlay->GetElement(NormalTriangle.A);
			N1 = TargetNormalOverlay->GetElement(NormalTriangle.B);
			N2 = TargetNormalOverlay->GetElement(NormalTriangle.C);
		}

		const double Ax = double(UV0.X) * Resolution, Ay = double(UV0.Y) * Resolution;
		const double Bx = double(UV1.X) * Resolution, By = double(UV1.Y) * Resolution;
		const double Cx = double(UV2.X) * Resolution, Cy = double(UV2.Y) * Resolution;
		const double Denominator = (Bx - Ax) * (Cy - Ay) - (By - Ay) * (Cx - Ax);
		if (FMath::Abs(Denominator) < UE_DOUBLE_SMALL_NUMBER)
		{
			continue;
		}
		const int32 MinX = FMath::Clamp(FMath::FloorToInt32(FMath::Min3(Ax, Bx, Cx) - 0.5), 0, Resolution - 1);
		const int32 MaxX = FMath::Clamp(FMath::CeilToInt32(FMath::Max3(Ax, Bx, Cx) + 0.5), 0, Resolution - 1);
		const int32 MinY = FMath::Clamp(FMath::FloorToInt32(FMath::Min3(Ay, By, Cy) - 0.5), 0, Resolution - 1);
		const int32 MaxY = FMath::Clamp(FMath::CeilToInt32(FMath::Max3(Ay, By, Cy) + 0.5), 0, Resolution - 1);
		for (int32 Y = MinY; Y <= MaxY; Y++)
		{
			for (int32 X = MinX; X <= MaxX; X++)
			{
				const double Px = X + 0.5, Py = Y + 0.5;
				const double W1 = ((Px - Ax) * (Cy - Ay) - (Py - Ay) * (Cx - Ax)) / Denominator;
				const double W2 = ((Bx - Ax) * (Py - Ay) - (By - Ay) * (Px - Ax)) / Denominator;
				const double W0 = 1.0 - W1 - W2;
				const double Tolerance = -1e-4;
				if (W0 < Tolerance || W1 < Tolerance || W2 < Tolerance)
				{
					continue;
				}
				const FVector3d Position = P0 * W0 + P1 * W1 + P2 * W2;
				const int32 Index = Y * Resolution + X;
				FVector3f SmoothNormal = N0 * float(W0) + N1 * float(W1) + N2 * float(W2);
				if (!SmoothNormal.Normalize())
				{
					SmoothNormal = FVector3f(FaceNormal);
				}
				TexelPositions[Index] = FVector3f(Position);
				TexelNormals[Index] = SmoothNormal;
				TexelState[Index] = 1;
			}
		}
	}

	const auto SampleBilinear = [](const FImage& Image, float U, float V) -> FColor
	{
		// The engine baker's source sampler clamps UVs to [0,1] (see the UDIM
		// normalization notes python-side); match it.
		const TArrayView64<const FColor> Pixels = Image.AsBGRA8();
		const int64 Width = Image.SizeX, Height = Image.SizeY;
		const float PixelX = FMath::Clamp(U, 0.0f, 1.0f) * float(Width) - 0.5f;
		const float PixelY = FMath::Clamp(V, 0.0f, 1.0f) * float(Height) - 0.5f;
		const int64 X0 = FMath::Clamp(int64(FMath::FloorToInt32(PixelX)), int64(0), Width - 1);
		const int64 Y0 = FMath::Clamp(int64(FMath::FloorToInt32(PixelY)), int64(0), Height - 1);
		const int64 X1 = FMath::Min(X0 + 1, Width - 1);
		const int64 Y1 = FMath::Min(Y0 + 1, Height - 1);
		const float FracX = FMath::Clamp(PixelX - float(X0), 0.0f, 1.0f);
		const float FracY = FMath::Clamp(PixelY - float(Y0), 0.0f, 1.0f);
		const FColor C00 = Pixels[Y0 * Width + X0], C10 = Pixels[Y0 * Width + X1];
		const FColor C01 = Pixels[Y1 * Width + X0], C11 = Pixels[Y1 * Width + X1];
		const auto LerpChannel = [FracX, FracY](uint8 V00, uint8 V10, uint8 V01, uint8 V11) -> uint8
		{
			const float Top = FMath::Lerp(float(V00), float(V10), FracX);
			const float Bottom = FMath::Lerp(float(V01), float(V11), FracX);
			return uint8(FMath::Clamp(FMath::RoundToInt32(FMath::Lerp(Top, Bottom, FracY)), 0, 255));
		};
		return FColor(LerpChannel(C00.R, C10.R, C01.R, C11.R), LerpChannel(C00.G, C10.G, C01.G, C11.G),
			LerpChannel(C00.B, C10.B, C01.B, C11.B), 255);
	};

	TArray<FColor> OutPixels;
	OutPixels.Init(FColor(0, 0, 0, 255), NumPixels);
	TArray<FVector2f> TexelSourceUVs;
	TexelSourceUVs.SetNumZeroed(NumPixels);
	TArray<int32> TexelMaterials;
	TexelMaterials.Init(-1, NumPixels);
	UE::Geometry::FDynamicMeshAABBTree3 SourceBVH(&SourceMesh3, true);
	std::atomic<int32> BakedCount(0), MissCount(0);
	ParallelFor(Resolution, [&](int32 Y)
	{
		// The filter lambda captures this by reference so one FQueryOptions serves the
		// whole row without per-texel TFunction allocations.
		FVector3d TexelNormal = FVector3d::UnitZ();
		const double MinFacingDotD = double(MinFacingDot);
		UE::Geometry::IMeshSpatial::FQueryOptions QueryOptions(double(ProjectionDistance),
			[&SourceMesh3, &TexelNormal, MinFacingDotD](int32 TriangleID)
			{
				return SourceMesh3.GetTriNormal(TriangleID).Dot(TexelNormal) >= MinFacingDotD;
			});
		// Facing-filtered rays out/in along the texel normal (Epic's forward/backward
		// rays, minus the outside-in start), nearer hit wins.
		const auto FacingRayHit = [&SourceBVH](const FVector3d& Origin, const FVector3d& Direction,
			UE::Geometry::IMeshSpatial::FQueryOptions& Options, FVector3d& OutBaryCoords,
			double& OutHitDistance) -> int32
		{
			double ForwardT = 0.0, BackwardT = 0.0;
			int32 ForwardTID = -1, BackwardTID = -1;
			FVector3d ForwardBary = FVector3d::Zero(), BackwardBary = FVector3d::Zero();
			const bool bForwardHit = SourceBVH.FindNearestHitTriangle(
				FRay3d(Origin, Direction), ForwardT, ForwardTID, ForwardBary, Options);
			const bool bBackwardHit = SourceBVH.FindNearestHitTriangle(
				FRay3d(Origin, -Direction), BackwardT, BackwardTID, BackwardBary, Options);
			if (bForwardHit && (!bBackwardHit || ForwardT <= BackwardT))
			{
				OutBaryCoords = ForwardBary;
				OutHitDistance = ForwardT;
				return ForwardTID;
			}
			if (bBackwardHit)
			{
				OutBaryCoords = BackwardBary;
				OutHitDistance = BackwardT;
				return BackwardTID;
			}
			return -1;
		};
		for (int32 X = 0; X < Resolution; X++)
		{
			const int32 Index = Y * Resolution + X;
			if (TexelState[Index] == 0)
			{
				continue;
			}
			const FVector3d Position(TexelPositions[Index]);
			TexelNormal = FVector3d(TexelNormals[Index]);

			// Adaptive arbitration (strategy comment above the function): the along-normal
			// ray wins whenever it lands within max(RayAcceptFactor * the nearest-point
			// distance, PreferRayDistance) — plate-on-skin detail (nails) and laterally
			// drifted spans (mid-calf) both need the ALONG-NORMAL correspondence, while a
			// neighbouring finger's far wall (several times farther than the own-skin
			// nearest point) stays rejected. PreferRayDistance <= 0 restores plain
			// nearest-point-first. Invalid triangle IDs are -1.
			constexpr double RayAcceptFactor = 3.0;
			int32 HitTriangleID = -1;
			FVector3d BaryCoords = FVector3d::Zero();
			double NearestDistSqr = 0.0;
			const int32 NearestTriangleID = SourceBVH.FindNearestTriangle(Position, NearestDistSqr, QueryOptions);
			double RayHitDistance = 0.0;
			FVector3d RayBaryCoords = FVector3d::Zero();
			const int32 RayTriangleID = (PreferRayDistance > 0.0f || NearestTriangleID < 0)
				? FacingRayHit(Position, TexelNormal, QueryOptions, RayBaryCoords, RayHitDistance) : -1;
			const double RayAcceptLimit = FMath::Max(
				RayAcceptFactor * FMath::Sqrt(FMath::Max(NearestDistSqr, 0.0)), double(PreferRayDistance));
			if (RayTriangleID >= 0
				&& (NearestTriangleID < 0 || (PreferRayDistance > 0.0f && RayHitDistance <= RayAcceptLimit)))
			{
				HitTriangleID = RayTriangleID;
				BaryCoords = RayBaryCoords;
			}
			else if (NearestTriangleID >= 0)
			{
				HitTriangleID = NearestTriangleID;
				BaryCoords = UE::Geometry::TMeshQueries<UE::Geometry::FDynamicMesh3>::TriangleDistance(
					SourceMesh3, HitTriangleID, Position).TriangleBaryCoords;
			}

			const int32 MaterialIndex = (HitTriangleID >= 0 && SourceMaterialIDs)
				? SourceMaterialIDs->GetValue(HitTriangleID) : 0;
			if (HitTriangleID < 0
				|| !SourceImageValid.IsValidIndex(MaterialIndex) || !SourceImageValid[MaterialIndex]
				|| !SourceUVOverlay->IsSetTriangle(HitTriangleID))
			{
				MissCount++;
				continue;
			}
			const UE::Geometry::FIndex3i SourceUVTriangle = SourceUVOverlay->GetTriangle(HitTriangleID);
			TexelSourceUVs[Index] =
				SourceUVOverlay->GetElement(SourceUVTriangle.A) * float(BaryCoords.X)
				+ SourceUVOverlay->GetElement(SourceUVTriangle.B) * float(BaryCoords.Y)
				+ SourceUVOverlay->GetElement(SourceUVTriangle.C) * float(BaryCoords.Z);
			TexelMaterials[Index] = MaterialIndex;
			TexelState[Index] = 2;
			BakedCount++;
		}
	});

	// Sampling pass (design note 3): a texel's source-UV footprint is the largest
	// source-pixel step to a 4-neighbour with the same source material; steps beyond
	// SeamRejectPx are UV-seam/UDIM-tile crossings, not minification, and are ignored.
	// log2(footprint) picks the software mip level, sampled trilinearly. Footprints
	// under a source pixel (magnification) stay on the full-resolution image, which is
	// the pre-filtering behaviour.
	ParallelFor(Resolution, [&](int32 Y)
	{
		constexpr float SeamRejectPx = 64.0f;
		const int32 NeighborOffsets[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
		for (int32 X = 0; X < Resolution; X++)
		{
			const int32 Index = Y * Resolution + X;
			if (TexelState[Index] != 2)
			{
				continue;
			}
			const int32 MaterialIndex = TexelMaterials[Index];
			const FImage& BaseImage = SourceImages[MaterialIndex];
			const FVector2f SourceUV = TexelSourceUVs[Index];
			float FootprintPx = 0.0f;
			for (const auto& Offset : NeighborOffsets)
			{
				const int32 NX = X + Offset[0], NY = Y + Offset[1];
				if (NX < 0 || NX >= Resolution || NY < 0 || NY >= Resolution)
				{
					continue;
				}
				const int32 NeighborIndex = NY * Resolution + NX;
				if (TexelState[NeighborIndex] != 2 || TexelMaterials[NeighborIndex] != MaterialIndex)
				{
					continue;
				}
				const FVector2f Step = TexelSourceUVs[NeighborIndex] - SourceUV;
				const float StepPx = FVector2f(Step.X * BaseImage.SizeX, Step.Y * BaseImage.SizeY).Length();
				if (StepPx <= SeamRejectPx)
				{
					FootprintPx = FMath::Max(FootprintPx, StepPx);
				}
			}
			const TArray<FImage>& MipChain = SourceMipChains[MaterialIndex];
			const float MipLevel = FMath::Clamp(FootprintPx > 1.0f ? FMath::Log2(FootprintPx) : 0.0f,
				0.0f, float(MipChain.Num()));
			const int32 LevelLow = FMath::FloorToInt32(MipLevel);
			const float LevelFrac = MipLevel - float(LevelLow);
			const FImage& ImageLow = LevelLow == 0 ? BaseImage : MipChain[LevelLow - 1];
			const FColor ColorLow = SampleBilinear(ImageLow, SourceUV.X, SourceUV.Y);
			if (LevelLow >= MipChain.Num() || LevelFrac < UE_KINDA_SMALL_NUMBER)
			{
				OutPixels[Index] = ColorLow;
				continue;
			}
			const FColor ColorHigh = SampleBilinear(MipChain[LevelLow], SourceUV.X, SourceUV.Y);
			const auto LerpChannel = [LevelFrac](uint8 Low, uint8 High) -> uint8
			{
				return uint8(FMath::Clamp(FMath::RoundToInt32(FMath::Lerp(float(Low), float(High), LevelFrac)), 0, 255));
			};
			OutPixels[Index] = FColor(LerpChannel(ColorLow.R, ColorHigh.R),
				LerpChannel(ColorLow.G, ColorHigh.G), LerpChannel(ColorLow.B, ColorHigh.B), 255);
		}
	});

	// Fill: one multi-source BFS from every baked texel. In-chart misses (state 1) fill
	// at any depth — a miss becomes a smear of adjacent skin, never black — while
	// background texels fill only within GutterSize of a chart (the mip-safe dilation
	// ring the engine baker calls the gutter). Each filled texel takes the AVERAGE of
	// its already-final neighbours, with same-wave fills quarantined at state 3 until
	// the wave ends — copying a single neighbour extrudes the chart edge outward and
	// reads as repeating streak bands in any large miss region (fresh-Victoria crotch,
	// inner thighs and instep, 2026-08-08); the averaged front diffuses into a smooth
	// gradient instead.
	int32 FilledMissCount = 0, GutterCount = 0;
	TArray<uint8> FilledMask;
	FilledMask.SetNumZeroed(NumPixels);
	{
		TArray<int32> Wave;
		Wave.Reserve(NumPixels / 4);
		for (int32 Index = 0; Index < NumPixels; Index++)
		{
			if (TexelState[Index] == 2)
			{
				Wave.Add(Index);
			}
		}
		TArray<int32> NextWave;
		for (int32 Depth = 1; Wave.Num() > 0; Depth++)
		{
			NextWave.Reset();
			for (const int32 Index : Wave)
			{
				const int32 X = Index % Resolution;
				const int32 Y = Index / Resolution;
				for (int32 DY = -1; DY <= 1; DY++)
				{
					for (int32 DX = -1; DX <= 1; DX++)
					{
						const int32 NX = X + DX, NY = Y + DY;
						if ((DX == 0 && DY == 0) || NX < 0 || NX >= Resolution || NY < 0 || NY >= Resolution)
						{
							continue;
						}
						const int32 NeighborIndex = NY * Resolution + NX;
						if (TexelState[NeighborIndex] >= 2)
						{
							continue;
						}
						const bool bInChartMiss = TexelState[NeighborIndex] == 1;
						if (!bInChartMiss && Depth > GutterSize)
						{
							continue;
						}
						int32 SumR = 0, SumG = 0, SumB = 0, Taps = 0;
						for (int32 TapDY = -1; TapDY <= 1; TapDY++)
						{
							for (int32 TapDX = -1; TapDX <= 1; TapDX++)
							{
								const int32 TapX = NX + TapDX, TapY = NY + TapDY;
								if (TapX < 0 || TapX >= Resolution || TapY < 0 || TapY >= Resolution)
								{
									continue;
								}
								const int32 TapIndex = TapY * Resolution + TapX;
								if (TexelState[TapIndex] == 2)
								{
									const FColor& Tap = OutPixels[TapIndex];
									SumR += Tap.R; SumG += Tap.G; SumB += Tap.B;
									Taps++;
								}
							}
						}
						if (Taps == 0)
						{
							continue;	// unreachable: Index itself neighbours NeighborIndex at state 2
						}
						OutPixels[NeighborIndex] = FColor(uint8(SumR / Taps), uint8(SumG / Taps), uint8(SumB / Taps), 255);
						TexelState[NeighborIndex] = 3;
						FilledMask[NeighborIndex] = 1;
						(bInChartMiss ? FilledMissCount : GutterCount)++;
						NextWave.Add(NeighborIndex);
					}
				}
			}
			for (const int32 Index : NextWave)
			{
				TexelState[Index] = 2;
			}
			Swap(Wave, NextWave);
		}
	}

	// Post-smooth the filled regions only (baked texels never move): a few 3x3 box
	// passes over fill texels, sampling colored (state 2) texels, to soften the
	// residual wavefront structure the diffusion fill leaves in wide regions.
	if (FilledMissCount + GutterCount > 0)
	{
		const int32 FillBlurPasses = 4;
		TArray<FColor> BlurSource;
		for (int32 Pass = 0; Pass < FillBlurPasses; Pass++)
		{
			BlurSource = OutPixels;
			ParallelFor(Resolution, [&](int32 Y)
			{
				for (int32 X = 0; X < Resolution; X++)
				{
					const int32 Index = Y * Resolution + X;
					if (!FilledMask[Index])
					{
						continue;
					}
					int32 SumR = 0, SumG = 0, SumB = 0, Taps = 0;
					for (int32 TapDY = -1; TapDY <= 1; TapDY++)
					{
						for (int32 TapDX = -1; TapDX <= 1; TapDX++)
						{
							const int32 TapX = X + TapDX, TapY = Y + TapDY;
							if (TapX < 0 || TapX >= Resolution || TapY < 0 || TapY >= Resolution)
							{
								continue;
							}
							const int32 TapIndex = TapY * Resolution + TapX;
							if (TexelState[TapIndex] == 2)
							{
								const FColor& Tap = BlurSource[TapIndex];
								SumR += Tap.R; SumG += Tap.G; SumB += Tap.B;
								Taps++;
							}
						}
					}
					if (Taps > 0)
					{
						OutPixels[Index] = FColor(uint8(SumR / Taps), uint8(SumG / Taps), uint8(SumB / Taps), 255);
					}
				}
			});
		}
	}
	int32 UnfilledCount = 0;
	for (int32 Index = 0; Index < NumPixels; Index++)
	{
		UnfilledCount += TexelState[Index] == 1 ? 1 : 0;
	}
	if (UnfilledCount > 0)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("BakeBasecolorFacingFiltered: %d rasterized texels stayed black (chart with no valid correspondence at all)"), UnfilledCount);
	}

	UTexture2D* Result = NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient);
	Result->Source.Init(Resolution, Resolution, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(OutPixels.GetData()));
	Result->SRGB = true;
	Result->CompressionSettings = TC_Default;
	Result->UpdateResource();
	UE_LOG(LogDazToUnrealBlueprintUtils, Log, TEXT("BakeBasecolorFacingFiltered: %d baked, %d misses (%d neighbour-filled), %d gutter px at %dpx, reach %.1f, plate rays %.1f, facing dot %.2f"),
		BakedCount.load(), MissCount.load(), FilledMissCount, GutterCount, Resolution, ProjectionDistance, PreferRayDistance, MinFacingDot);
	return Result;
#else
	UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("BakeBasecolorFacingFiltered requires Unreal Engine 5.8 or later"));
	return nullptr;
#endif
}

UTexture2D* UDazToUnrealBlueprintUtils::CreateHideMaskFromGapCoverage(UTexture2D* GapCoverageTexture, const FString& AssetPathAndName, float MaxGapDistance, int32 ErodePixels, float GapErodePixelsPerCm, int32 BlurPixels)
{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 8
	if (!GapCoverageTexture || !GapCoverageTexture->Source.IsValid())
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("CreateHideMaskFromGapCoverage: coverage texture has no source data"));
		return nullptr;
	}

	FImage MipImage;
	if (!GapCoverageTexture->Source.GetMipImage(MipImage, 0))
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("CreateHideMaskFromGapCoverage: unable to read coverage image"));
		return nullptr;
	}
	FImage Converted;
	MipImage.CopyTo(Converted, ERawImageFormat::BGRA8, MipImage.GetGammaSpace());
	const int32 Width = Converted.SizeX;
	const int32 Height = Converted.SizeY;
	const TArrayView64<FColor> Pixels = Converted.AsBGRA8();
	if (Width <= 0 || Height <= 0 || Pixels.Num() != int64(Width) * Height)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("CreateHideMaskFromGapCoverage: unexpected coverage image layout"));
		return nullptr;
	}

	// Decode: 255 = bare skin, 0..254 = covered with gap 0..MaxGapDistance.
	const int32 NumPixels = Width * Height;
	TArray<uint8> GapValues;
	GapValues.SetNumUninitialized(NumPixels);
	for (int32 Index = 0; Index < NumPixels; Index++)
	{
		GapValues[Index] = Pixels[Index].R;
	}

	// Chamfer distance (px) from every covered texel to the nearest bare texel —
	// "how deep inside the covered region am I". Two-pass 1 / sqrt(2) transform.
	TArray<float> BorderDistance;
	BorderDistance.SetNumUninitialized(NumPixels);
	for (int32 Index = 0; Index < NumPixels; Index++)
	{
		BorderDistance[Index] = (GapValues[Index] == 255) ? 0.0f : 1e9f;
	}
	const float Diagonal = 1.41421356f;
	for (int32 Y = 0; Y < Height; Y++)
	{
		for (int32 X = 0; X < Width; X++)
		{
			const int32 Index = Y * Width + X;
			float Value = BorderDistance[Index];
			if (X > 0) { Value = FMath::Min(Value, BorderDistance[Index - 1] + 1.0f); }
			if (Y > 0) { Value = FMath::Min(Value, BorderDistance[Index - Width] + 1.0f); }
			if (X > 0 && Y > 0) { Value = FMath::Min(Value, BorderDistance[Index - Width - 1] + Diagonal); }
			if (X < Width - 1 && Y > 0) { Value = FMath::Min(Value, BorderDistance[Index - Width + 1] + Diagonal); }
			BorderDistance[Index] = Value;
		}
	}
	for (int32 Y = Height - 1; Y >= 0; Y--)
	{
		for (int32 X = Width - 1; X >= 0; X--)
		{
			const int32 Index = Y * Width + X;
			float Value = BorderDistance[Index];
			if (X < Width - 1) { Value = FMath::Min(Value, BorderDistance[Index + 1] + 1.0f); }
			if (Y < Height - 1) { Value = FMath::Min(Value, BorderDistance[Index + Width] + 1.0f); }
			if (X < Width - 1 && Y < Height - 1) { Value = FMath::Min(Value, BorderDistance[Index + Width + 1] + Diagonal); }
			if (X > 0 && Y < Height - 1) { Value = FMath::Min(Value, BorderDistance[Index + Width - 1] + Diagonal); }
			BorderDistance[Index] = Value;
		}
	}

	// Cull only deeper than the gap-scaled margin; the margin plays the role the
	// fixed erode played in CreateHideMaskFromCoverage, but widens with the local
	// garment-to-skin gap so openings keep live skin along their sightlines.
	TArray<uint8> Mask;
	Mask.SetNumUninitialized(NumPixels);
	int32 CulledCount = 0, CoveredCount = 0;
	for (int32 Index = 0; Index < NumPixels; Index++)
	{
		uint8 Value = 255;
		if (GapValues[Index] != 255)
		{
			CoveredCount++;
			const float GapCm = float(GapValues[Index]) / 254.0f * MaxGapDistance;
			const float MarginPixels = float(ErodePixels) + GapErodePixelsPerCm * GapCm;
			if (BorderDistance[Index] > MarginPixels)
			{
				Value = 0;
				CulledCount++;
			}
		}
		Mask[Index] = Value;
	}

	// Blur to create the gradient band between MaxCullValue and MinKeepValue that
	// drives vertex shrinking at clothing borders (separable box blur).
	if (BlurPixels > 0)
	{
		auto BlurAxis = [Width, Height](const TArray<uint8>& InValues, TArray<uint8>& OutValues, int32 Radius, bool bHorizontal)
		{
			const int32 WindowSize = Radius * 2 + 1;
			for (int32 Y = 0; Y < Height; Y++)
			{
				for (int32 X = 0; X < Width; X++)
				{
					uint32 Sum = 0;
					for (int32 Offset = -Radius; Offset <= Radius; Offset++)
					{
						const int32 SampleX = bHorizontal ? FMath::Clamp(X + Offset, 0, Width - 1) : X;
						const int32 SampleY = bHorizontal ? Y : FMath::Clamp(Y + Offset, 0, Height - 1);
						Sum += InValues[SampleY * Width + SampleX];
					}
					OutValues[Y * Width + X] = uint8(Sum / WindowSize);
				}
			}
		};
		TArray<uint8> Scratch;
		Scratch.SetNumUninitialized(NumPixels);
		BlurAxis(Mask, Scratch, BlurPixels, true);
		BlurAxis(Scratch, Mask, BlurPixels, false);
	}

	TArray<FColor> MaskPixels;
	MaskPixels.SetNumUninitialized(NumPixels);
	for (int32 Index = 0; Index < NumPixels; Index++)
	{
		const uint8 Value = Mask[Index];
		MaskPixels[Index] = FColor(Value, Value, Value, 255);
	}

	// AssetPathAndName may be /Game/Path/Name or /Game/Path/Name.Name
	FString PackageName = AssetPathAndName;
	int32 DotIndex = INDEX_NONE;
	if (PackageName.FindChar(TEXT('.'), DotIndex))
	{
		PackageName.LeftInline(DotIndex);
	}
	const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);

	UPackage* Package = CreatePackage(*PackageName);
	UTexture2D* MaskTexture = FindObject<UTexture2D>(Package, *AssetName);
	const bool bExisted = MaskTexture != nullptr;
	if (!MaskTexture)
	{
		MaskTexture = NewObject<UTexture2D>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	}

	MaskTexture->Source.Init(Width, Height, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(MaskPixels.GetData()));
	MaskTexture->SRGB = false;
	MaskTexture->CompressionSettings = TC_Masks;
	// The cull threshold must be stable at every draw distance — mip averaging along the
	// covered/visible border would flicker geometry in and out.
	MaskTexture->MipGenSettings = TMGS_NoMipmaps;
	MaskTexture->UpdateResource();
	MaskTexture->PostEditChange();
	if (!bExisted)
	{
		FAssetRegistryModule::AssetCreated(MaskTexture);
	}
	MaskTexture->MarkPackageDirty();
	UE_LOG(LogDazToUnrealBlueprintUtils, Log, TEXT("CreateHideMaskFromGapCoverage: culled %d of %d covered texels (erode %d px + %.1f px/cm)"),
		CulledCount, CoveredCount, ErodePixels, GapErodePixelsPerCm);
	return MaskTexture;
#else
	UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("CreateHideMaskFromGapCoverage requires Unreal Engine 5.8 or later"));
	return nullptr;
#endif
}

UTexture2D* UDazToUnrealBlueprintUtils::CreateGroomFollicleMask(const TArray<UObject*>& GroomAssets, const TArray<int32>& Channels, const FString& AssetPathAndName, int32 Resolution, int32 RootRadius)
{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 8
	TArray<FFollicleInfo> FollicleInfos;
	for (int32 Index = 0; Index < GroomAssets.Num(); Index++)
	{
		UGroomAsset* Groom = Cast<UGroomAsset>(GroomAssets[Index]);
		if (!Groom)
		{
			UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("CreateGroomFollicleMask: entry %d is not a GroomAsset (%s) - skipping"),
				Index, *GetNameSafe(GroomAssets[Index]));
			continue;
		}
		FFollicleInfo& Info = FollicleInfos.AddDefaulted_GetRef();
		Info.GroomAsset = Groom;
		const int32 Channel = Channels.IsValidIndex(Index) ? Channels[Index] : 0;
		Info.Channel = static_cast<FFollicleInfo::EChannel>(FMath::Clamp(Channel, 0, 3));
		Info.KernelSizeInPixels = uint32(FMath::Max(2, RootRadius));
	}
	if (FollicleInfos.Num() == 0)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("CreateGroomFollicleMask: no valid grooms"));
		return nullptr;
	}

	// The follicle rasterizer requires a square power-of-two texture
	Resolution = FMath::Clamp(int32(FMath::RoundUpToPowerOfTwo(uint32(FMath::Max(Resolution, 256)))), 256, 8192);

	// AssetPathAndName may be /Game/Path/Name or /Game/Path/Name.Name
	FString PackageName = AssetPathAndName;
	int32 DotIndex = INDEX_NONE;
	if (PackageName.FindChar(TEXT('.'), DotIndex))
	{
		PackageName.LeftInline(DotIndex);
	}
	const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);

	// Reuse-in-place is safe here: AllocateFollicleTextureResources re-initializes the
	// source bulkdata from scratch, so no stale-payload claims survive (unlike the
	// overwrite branch the BaseColor bakes hit)
	UPackage* Package = CreatePackage(*PackageName);
	UTexture2D* FollicleMap = FindObject<UTexture2D>(Package, *AssetName);
	const bool bExisted = FollicleMap != nullptr;
	if (!FollicleMap)
	{
		FollicleMap = NewObject<UTexture2D>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	}

	// Mirror the official MetaHuman assembly flow (MetaHumanDefaultEditorPipelineBase):
	// allocate, blur the lower mips, then Pre/PostEditChange around the CPU root splat
	const int32 MipCount = FMath::FloorLog2(Resolution) + 1;
	FGroomTextureBuilder::AllocateFollicleTextureResources(FollicleMap, FIntPoint(Resolution), MipCount);
	FollicleMap->MipGenSettings = TMGS_Blur5;
	{
		FollicleMap->PreEditChange(nullptr);
		FGroomTextureBuilder::BuildFollicleTexture(FollicleInfos, FollicleMap, false);
		FollicleMap->PostEditChange();
	}

	if (!bExisted)
	{
		FAssetRegistryModule::AssetCreated(FollicleMap);
	}
	FollicleMap->MarkPackageDirty();
	UE_LOG(LogDazToUnrealBlueprintUtils, Log, TEXT("CreateGroomFollicleMask: built %dx%d follicle mask from %d groom(s) at %s"),
		Resolution, Resolution, FollicleInfos.Num(), *PackageName);
	return FollicleMap;
#else
	UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("CreateGroomFollicleMask requires Unreal Engine 5.8 or later"));
	return nullptr;
#endif
}

UTexture2D* UDazToUnrealBlueprintUtils::BakeFollicleMaskIntoTexture(UTexture2D* BaseColorTexture, UTexture2D* FollicleMaskTexture, const FString& AssetPathAndName, FLinearColor HairMultiply, FLinearColor EyebrowsMultiply, FLinearColor BeardMultiply, float Strength, float BroadCoverageGain)
{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 8
	if (!BaseColorTexture || !BaseColorTexture->Source.IsValid() || !FollicleMaskTexture || !FollicleMaskTexture->Source.IsValid())
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("BakeFollicleMaskIntoTexture: missing source data on input textures"));
		return nullptr;
	}

	FImage BaseMip;
	FImage MaskMip;
	if (!BaseColorTexture->Source.GetMipImage(BaseMip, 0) || !FollicleMaskTexture->Source.GetMipImage(MaskMip, 0))
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("BakeFollicleMaskIntoTexture: unable to read input images"));
		return nullptr;
	}

	FImage Base;
	BaseMip.CopyTo(Base, ERawImageFormat::RGBA32F, EGammaSpace::Linear);
	FImage Mask;
	MaskMip.CopyTo(Mask, ERawImageFormat::BGRA8, EGammaSpace::Linear);

	const int64 Width = Base.SizeX;
	const int64 Height = Base.SizeY;
	const TArrayView64<FLinearColor> BasePixels = Base.AsRGBA32F();
	const TArrayView64<FColor> MaskPixels = Mask.AsBGRA8();
	if (BasePixels.Num() == 0 || MaskPixels.Num() == 0)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("BakeFollicleMaskIntoTexture: empty input images"));
		return nullptr;
	}

	Strength = FMath::Clamp(Strength, 0.0f, 1.0f);
	BroadCoverageGain = FMath::Max(BroadCoverageGain, 0.0f);

	// Mip 0 of the mask is hard per-root splats; between roots it is pure black, so a
	// direct read only ever produces sparse stubble dots. Rebuild the broad coverage
	// the shader path gets from the blurred lower mips: block-average the dots into a
	// coarse grid, blur it, and use max(dot, gained grid) as the darkening alpha.
	const int32 GridSize = 256;
	TArray<FVector3f> Grid;
	Grid.AddZeroed(GridSize * GridSize);
	{
		TArray<int32> CellCounts;
		CellCounts.AddZeroed(GridSize * GridSize);
		for (int64 MaskY = 0; MaskY < Mask.SizeY; MaskY++)
		{
			const int64 CellY = MaskY * GridSize / Mask.SizeY;
			for (int64 MaskX = 0; MaskX < Mask.SizeX; MaskX++)
			{
				const FColor& MaskColor = MaskPixels[MaskY * Mask.SizeX + MaskX];
				const int64 Cell = CellY * GridSize + MaskX * GridSize / Mask.SizeX;
				Grid[Cell] += FVector3f(MaskColor.R, MaskColor.G, MaskColor.B) / 255.0f;
				CellCounts[Cell]++;
			}
		}
		for (int32 Cell = 0; Cell < Grid.Num(); Cell++)
		{
			Grid[Cell] /= FMath::Max(CellCounts[Cell], 1);
		}
		// Two 3x3 box passes soften cell edges and feather coverage a little past the
		// outermost roots (hairline, part line)
		for (int32 Pass = 0; Pass < 2; Pass++)
		{
			TArray<FVector3f> Blurred;
			Blurred.AddZeroed(GridSize * GridSize);
			for (int32 CellY = 0; CellY < GridSize; CellY++)
			{
				for (int32 CellX = 0; CellX < GridSize; CellX++)
				{
					FVector3f Sum = FVector3f::ZeroVector;
					int32 Taps = 0;
					for (int32 DY = -1; DY <= 1; DY++)
					{
						for (int32 DX = -1; DX <= 1; DX++)
						{
							const int32 TapX = CellX + DX;
							const int32 TapY = CellY + DY;
							if (TapX >= 0 && TapX < GridSize && TapY >= 0 && TapY < GridSize)
							{
								Sum += Grid[TapY * GridSize + TapX];
								Taps++;
							}
						}
					}
					Blurred[CellY * GridSize + CellX] = Sum / Taps;
				}
			}
			Grid = MoveTemp(Blurred);
		}
	}
	auto SampleGrid = [&Grid, GridSize](float U, float V) -> FVector3f
	{
		const float GX = FMath::Clamp(U * GridSize - 0.5f, 0.0f, GridSize - 1.0f);
		const float GY = FMath::Clamp(V * GridSize - 0.5f, 0.0f, GridSize - 1.0f);
		const int32 X0 = FMath::FloorToInt32(GX);
		const int32 Y0 = FMath::FloorToInt32(GY);
		const int32 X1 = FMath::Min(X0 + 1, GridSize - 1);
		const int32 Y1 = FMath::Min(Y0 + 1, GridSize - 1);
		const float FX = GX - X0;
		const float FY = GY - Y0;
		return FMath::Lerp(
			FMath::Lerp(Grid[Y0 * GridSize + X0], Grid[Y0 * GridSize + X1], FX),
			FMath::Lerp(Grid[Y1 * GridSize + X0], Grid[Y1 * GridSize + X1], FX), FY);
	};

	ParallelFor(int32(Height), [&](int32 Y)
	{
		const int64 MaskY = int64(Y) * Mask.SizeY / Height;
		const float V = (float(Y) + 0.5f) / float(Height);
		for (int64 X = 0; X < Width; X++)
		{
			const int64 MaskX = X * Mask.SizeX / Width;
			const FColor& MaskColor = MaskPixels[MaskY * Mask.SizeX + MaskX];
			const FVector3f Broad = SampleGrid((float(X) + 0.5f) / float(Width), V) * BroadCoverageGain;
			if (MaskColor.R == 0 && MaskColor.G == 0 && MaskColor.B == 0 && Broad.IsNearlyZero())
			{
				continue;
			}
			FLinearColor& Pixel = BasePixels[int64(Y) * Width + X];
			const float HairAlpha = FMath::Max(float(MaskColor.R) / 255.0f, FMath::Min(Broad.X, 1.0f)) * Strength;
			const float BrowsAlpha = FMath::Max(float(MaskColor.G) / 255.0f, FMath::Min(Broad.Y, 1.0f)) * Strength;
			const float BeardAlpha = FMath::Max(float(MaskColor.B) / 255.0f, FMath::Min(Broad.Z, 1.0f)) * Strength;
			Pixel.R *= FMath::Lerp(1.0f, HairMultiply.R, HairAlpha) * FMath::Lerp(1.0f, EyebrowsMultiply.R, BrowsAlpha) * FMath::Lerp(1.0f, BeardMultiply.R, BeardAlpha);
			Pixel.G *= FMath::Lerp(1.0f, HairMultiply.G, HairAlpha) * FMath::Lerp(1.0f, EyebrowsMultiply.G, BrowsAlpha) * FMath::Lerp(1.0f, BeardMultiply.G, BeardAlpha);
			Pixel.B *= FMath::Lerp(1.0f, HairMultiply.B, HairAlpha) * FMath::Lerp(1.0f, EyebrowsMultiply.B, BrowsAlpha) * FMath::Lerp(1.0f, BeardMultiply.B, BeardAlpha);
		}
	});

	// Back to the storage format the rest of the pipeline expects for baked basecolor
	FImage Result;
	Base.CopyTo(Result, ERawImageFormat::BGRA8, BaseColorTexture->SRGB ? EGammaSpace::sRGB : EGammaSpace::Linear);

	// AssetPathAndName may be /Game/Path/Name or /Game/Path/Name.Name
	FString PackageName = AssetPathAndName;
	int32 DotIndex = INDEX_NONE;
	if (PackageName.FindChar(TEXT('.'), DotIndex))
	{
		PackageName.LeftInline(DotIndex);
	}
	const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);

	// Reuse-in-place is safe: Source.Init fully re-initializes the bulkdata
	UPackage* Package = CreatePackage(*PackageName);
	UTexture2D* DerivedTexture = FindObject<UTexture2D>(Package, *AssetName);
	const bool bExisted = DerivedTexture != nullptr;
	if (!DerivedTexture)
	{
		DerivedTexture = NewObject<UTexture2D>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	}

	DerivedTexture->PreEditChange(nullptr);
	DerivedTexture->Source.Init(int32(Width), int32(Height), 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(Result.AsBGRA8().GetData()));
	DerivedTexture->SRGB = BaseColorTexture->SRGB;
	DerivedTexture->CompressionSettings = BaseColorTexture->CompressionSettings;
	DerivedTexture->MipGenSettings = BaseColorTexture->MipGenSettings;
	DerivedTexture->LODGroup = BaseColorTexture->LODGroup;
	DerivedTexture->UpdateResource();
	DerivedTexture->PostEditChange();
	if (!bExisted)
	{
		FAssetRegistryModule::AssetCreated(DerivedTexture);
	}
	DerivedTexture->MarkPackageDirty();
	UE_LOG(LogDazToUnrealBlueprintUtils, Log, TEXT("BakeFollicleMaskIntoTexture: %s + %s -> %s (%dx%d, strength %.2f, broad gain %.1f)"),
		*BaseColorTexture->GetName(), *FollicleMaskTexture->GetName(), *PackageName, int32(Width), int32(Height), Strength, BroadCoverageGain);
	return DerivedTexture;
#else
	UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("BakeFollicleMaskIntoTexture requires Unreal Engine 5.8 or later"));
	return nullptr;
#endif
}

FString UDazToUnrealBlueprintUtils::GetMetaHumanInstanceParametersJson(UObject* CharacterOrCollectionOrInstance)
{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 8
	const UMetaHumanInstance* Instance = Cast<UMetaHumanInstance>(CharacterOrCollectionOrInstance);
	if (!Instance)
	{
		const UMetaHumanCollection* Collection = Cast<UMetaHumanCollection>(CharacterOrCollectionOrInstance);
		if (!Collection)
		{
			if (UMetaHumanCharacter* Character = Cast<UMetaHumanCharacter>(CharacterOrCollectionOrInstance))
			{
				Collection = Character->GetInternalCollection();
			}
		}
		if (Collection)
		{
			Instance = Collection->GetDefaultInstance();
		}
	}
	if (!Instance)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("GetMetaHumanInstanceParametersJson: %s is not a MetaHuman character, collection or instance"),
			*GetNameSafe(CharacterOrCollectionOrInstance));
		return TEXT("{}");
	}

	auto EscapeJson = [](const FString& In)
	{
		FString Out = In;
		Out.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Out.ReplaceInline(TEXT("\""), TEXT("\\\""));
		Out.ReplaceInline(TEXT("\n"), TEXT(" "));
		Out.ReplaceInline(TEXT("\r"), TEXT(" "));
		return Out;
	};

	TArray<FString> Entries;
	for (const TPair<FMetaHumanPaletteItemPath, FInstancedPropertyBag>& Pair : Instance->GetOverriddenInstanceParameters())
	{
		const UPropertyBag* BagStruct = Pair.Value.GetPropertyBagStruct();
		if (!BagStruct)
		{
			continue;
		}
		TArray<FString> Params;
		for (const FPropertyBagPropertyDesc& Desc : BagStruct->GetPropertyDescs())
		{
			const FString Key = FString::Printf(TEXT("\"%s\":"), *EscapeJson(Desc.Name.ToString()));
			switch (Desc.ValueType)
			{
			case EPropertyBagPropertyType::Bool:
				if (TValueOrError<bool, EPropertyBagResult> Value = Pair.Value.GetValueBool(Desc.Name); Value.HasValue())
				{
					Params.Add(Key + (Value.GetValue() ? TEXT("true") : TEXT("false")));
				}
				break;
			case EPropertyBagPropertyType::Int32:
				if (TValueOrError<int32, EPropertyBagResult> Value = Pair.Value.GetValueInt32(Desc.Name); Value.HasValue())
				{
					Params.Add(Key + FString::Printf(TEXT("%d"), Value.GetValue()));
				}
				break;
			case EPropertyBagPropertyType::Float:
				if (TValueOrError<float, EPropertyBagResult> Value = Pair.Value.GetValueFloat(Desc.Name); Value.HasValue())
				{
					Params.Add(Key + FString::Printf(TEXT("%g"), Value.GetValue()));
				}
				break;
			case EPropertyBagPropertyType::Double:
				if (TValueOrError<double, EPropertyBagResult> Value = Pair.Value.GetValueDouble(Desc.Name); Value.HasValue())
				{
					Params.Add(Key + FString::Printf(TEXT("%g"), Value.GetValue()));
				}
				break;
			case EPropertyBagPropertyType::Struct:
				if (TValueOrError<FStructView, EPropertyBagResult> Value = Pair.Value.GetValueStruct(Desc.Name);
					Value.HasValue() && Value.GetValue().GetScriptStruct() == TBaseStructure<FLinearColor>::Get())
				{
					const FLinearColor& Color = Value.GetValue().Get<FLinearColor>();
					Params.Add(Key + FString::Printf(TEXT("[%g,%g,%g,%g]"), Color.R, Color.G, Color.B, Color.A));
				}
				break;
			default:
				break;
			}
		}
		Entries.Add(FString::Printf(TEXT("{\"item\":\"%s\",\"params\":{%s}}"),
			*EscapeJson(Pair.Key.ToDebugString()), *FString::Join(Params, TEXT(","))));
	}
	UE_LOG(LogDazToUnrealBlueprintUtils, Log, TEXT("GetMetaHumanInstanceParametersJson: %d overridden parameter bag(s) on %s"),
		Entries.Num(), *Instance->GetPathName());
	return FString::Printf(TEXT("{\"entries\":[%s]}"), *FString::Join(Entries, TEXT(",")));
#else
	UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("GetMetaHumanInstanceParametersJson requires Unreal Engine 5.8 or later"));
	return TEXT("{}");
#endif
}

bool UDazToUnrealBlueprintUtils::SetBodyHideMaskMaterialParams(USkeletalMesh* BodyMesh, UTexture2D* HideMaskTexture, float MaxCullValue, float MinKeepValue, float MaxShrinkDistance)
{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 8
	if (!BodyMesh)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("SetBodyHideMaskMaterialParams: body mesh is required"));
		return false;
	}

	UTexture2D* Texture = HideMaskTexture;
	if (!Texture)
	{
		// Clear mode: flat white plus thresholds no pixel value can cross, so the mask
		// path is a no-op even if the white texture fails to load.
		Texture = LoadObject<UTexture2D>(nullptr, TEXT("/MetaHumanCharacter/Lookdev_UHM/Common/Textures/Placeholders/T_Flat_White_C.T_Flat_White_C"));
		MaxCullValue = -1.0f;
		MinKeepValue = 0.0f;
		MaxShrinkDistance = 0.0f;
	}

	int32 NumUpdated = 0;
	for (const FSkeletalMaterial& Material : BodyMesh->GetMaterials())
	{
		if (!Material.MaterialSlotName.ToString().StartsWith(TEXT("body"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		UMaterialInstanceConstant* MaterialInstance = Cast<UMaterialInstanceConstant>(Material.MaterialInterface);
		if (!MaterialInstance)
		{
			UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("SetBodyHideMaskMaterialParams: slot %s is not a material instance"), *Material.MaterialSlotName.ToString());
			continue;
		}

		if (Texture)
		{
			MaterialInstance->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("HideMaskTexture")), Texture);
		}
		MaterialInstance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("HideMaskMaxCullValue")), MaxCullValue);
		MaterialInstance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("HideMaskMinKeepValue")), MinKeepValue);
		MaterialInstance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("HideMaskMaxShrinkDistance")), MaxShrinkDistance);
		MaterialInstance->PostEditChange();
		MaterialInstance->MarkPackageDirty();
		NumUpdated++;
	}

	if (NumUpdated == 0)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("SetBodyHideMaskMaterialParams: no body material slots found on %s"), *BodyMesh->GetName());
	}
	return NumUpdated > 0;
#else
	UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("SetBodyHideMaskMaterialParams requires Unreal Engine 5.8 or later"));
	return false;
#endif
}

bool UDazToUnrealBlueprintUtils::BakeMetaHumanBodyCulling(UObject* CharacterBlueprint)
{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 8
	UBlueprint* GeneratedBP = Cast<UBlueprint>(CharacterBlueprint);
	USkeletalMeshComponent* BodyComponent = FindBodyComponentTemplate(GeneratedBP);
	if (!BodyComponent)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("BakeMetaHumanBodyCulling: %s has no Body skeletal mesh component"), *GetNameSafe(CharacterBlueprint));
		return false;
	}

	USkeletalMesh* CurrentMesh = BodyComponent->GetSkeletalMeshAsset();
	UDazMetaHumanSourceData* CurrentData = FindMetaHumanSourceData(CurrentMesh);
	if (!CurrentData)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("BakeMetaHumanBodyCulling: %s has no Daz MetaHuman source data — reconvert the character first"), *GetNameSafe(CurrentMesh));
		return false;
	}

	// Always bake from the pristine original so re-baking after an outfit change is idempotent.
	USkeletalMesh* OriginalMesh = CurrentData->OriginalBodyMesh.LoadSynchronous();
	if (!OriginalMesh)
	{
		OriginalMesh = CurrentMesh;
	}
	UDazMetaHumanSourceData* SourceData = FindMetaHumanSourceData(OriginalMesh);
	if (!SourceData)
	{
		SourceData = CurrentData;
	}

	UTexture2D* MaskTexture = SourceData->HideMaskTexture.LoadSynchronous();
	if (!MaskTexture)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("BakeMetaHumanBodyCulling: %s has no hide mask (no opaque clothing coverage) — nothing to bake"), *OriginalMesh->GetName());
		return false;
	}

	UE::MetaHuman::GeometryRemoval::FHiddenFaceMapTexture MapTexture;
	MapTexture.Texture = MaskTexture;
	MapTexture.Settings.MaxCullValue = SourceData->HideMaskMaxCullValue;
	MapTexture.Settings.MinKeepValue = SourceData->HideMaskMinKeepValue;
	MapTexture.Settings.MaxShrinkDistance = SourceData->HideMaskMaxShrinkDistance;

	TArray<UE::MetaHuman::GeometryRemoval::FHiddenFaceMapImage> MapImages;
	FText FailureReason;
	if (!UE::MetaHuman::GeometryRemoval::TryConvertHiddenFaceMapTexturesToImages(MakeArrayView(&MapTexture, 1), MapImages, FailureReason) || MapImages.Num() == 0)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("BakeMetaHumanBodyCulling: unable to read hide mask %s: %s"), *MaskTexture->GetName(), *FailureReason.ToString());
		return false;
	}
	MapImages[0].DebugName = MaskTexture->GetName();

	// Only cull sections belonging to body materials. An empty slot list would mean
	// "process everything" to RemoveAndShrinkGeometry, so bail instead if none match.
	TArray<FName> BodySlots;
	for (const FSkeletalMaterial& Material : OriginalMesh->GetMaterials())
	{
		if (Material.MaterialSlotName.ToString().StartsWith(TEXT("body"), ESearchCase::IgnoreCase))
		{
			BodySlots.Add(Material.MaterialSlotName);
		}
	}
	if (BodySlots.Num() == 0)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("BakeMetaHumanBodyCulling: no body material slots found on %s"), *OriginalMesh->GetName());
		return false;
	}

	// Duplicate the original to <name>_Culled, replacing any previous bake.
	const FString CulledPackageName = OriginalMesh->GetOutermost()->GetName() + TEXT("_Culled");
	const FString CulledAssetName = FPackageName::GetLongPackageAssetName(CulledPackageName);
	UPackage* CulledPackage = CreatePackage(*CulledPackageName);
	const bool bExisted = FindObject<UObject>(CulledPackage, *CulledAssetName) != nullptr;
	if (bExisted)
	{
		if (UObject* Existing = FindObject<UObject>(CulledPackage, *CulledAssetName))
		{
			Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
		}
	}
	USkeletalMesh* CulledMesh = DuplicateObject<USkeletalMesh>(OriginalMesh, CulledPackage, *CulledAssetName);
	if (!CulledMesh)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("BakeMetaHumanBodyCulling: failed to duplicate %s"), *OriginalMesh->GetName());
		return false;
	}
	CulledMesh->SetFlags(RF_Public | RF_Standalone);
	if (!bExisted)
	{
		FAssetRegistryModule::AssetCreated(CulledMesh);
	}

	{
		FScopedSkeletalMeshPostEditChange ScopedPostEditChange(CulledMesh);
		const int32 NumLODs = CulledMesh->GetLODNum();
		for (int32 LODIndex = 0; LODIndex < NumLODs; LODIndex++)
		{
			if (!UE::MetaHuman::GeometryRemoval::RemoveAndShrinkGeometry(CulledMesh, LODIndex, MapImages[0], BodySlots))
			{
				UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("BakeMetaHumanBodyCulling: geometry removal failed on %s LOD %d"), *CulledMesh->GetName(), LODIndex);
				return false;
			}
		}
	}
	CulledMesh->MarkPackageDirty();

	SetComponentTemplateSkeletalMesh(BodyComponent, CulledMesh);

	// The geometry is gone — leaving the preview params set would double-cull the border
	// shrink band and cook the mask texture for nothing. Restore reapplies them.
	SetBodyHideMaskMaterialParams(CulledMesh, nullptr);

	SourceData->CulledBodyMesh = CulledMesh;
	SourceData->bCullingBaked = true;
	OriginalMesh->MarkPackageDirty();

	// The duplicate carried a copy of the source data — keep it pointing at the truth.
	if (UDazMetaHumanSourceData* CulledData = FindMetaHumanSourceData(CulledMesh))
	{
		CulledData->OriginalBodyMesh = OriginalMesh;
		CulledData->CulledBodyMesh = CulledMesh;
		CulledData->bCullingBaked = true;
	}

	FKismetEditorUtilities::CompileBlueprint(GeneratedBP);
	GeneratedBP->MarkPackageDirty();
	UE_LOG(LogDazToUnrealBlueprintUtils, Log, TEXT("BakeMetaHumanBodyCulling: %s now uses %s"), *GeneratedBP->GetName(), *CulledMesh->GetPathName());
	return true;
#else
	UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("BakeMetaHumanBodyCulling requires Unreal Engine 5.8 or later"));
	return false;
#endif
}

bool UDazToUnrealBlueprintUtils::RestoreMetaHumanOriginalBody(UObject* CharacterBlueprint)
{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 8
	UBlueprint* GeneratedBP = Cast<UBlueprint>(CharacterBlueprint);
	USkeletalMeshComponent* BodyComponent = FindBodyComponentTemplate(GeneratedBP);
	if (!BodyComponent)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("RestoreMetaHumanOriginalBody: %s has no Body skeletal mesh component"), *GetNameSafe(CharacterBlueprint));
		return false;
	}

	USkeletalMesh* CurrentMesh = BodyComponent->GetSkeletalMeshAsset();
	UDazMetaHumanSourceData* CurrentData = FindMetaHumanSourceData(CurrentMesh);
	if (!CurrentData)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("RestoreMetaHumanOriginalBody: %s has no Daz MetaHuman source data"), *GetNameSafe(CurrentMesh));
		return false;
	}

	USkeletalMesh* OriginalMesh = CurrentData->OriginalBodyMesh.LoadSynchronous();
	if (!OriginalMesh)
	{
		UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("RestoreMetaHumanOriginalBody: original body mesh reference on %s is broken"), *GetNameSafe(CurrentMesh));
		return false;
	}

	if (CurrentMesh != OriginalMesh)
	{
		SetComponentTemplateSkeletalMesh(BodyComponent, OriginalMesh);
	}

	// Reapply the non-destructive preview from the stored settings.
	UDazMetaHumanSourceData* SourceData = FindMetaHumanSourceData(OriginalMesh);
	if (!SourceData)
	{
		SourceData = CurrentData;
	}
	if (UTexture2D* MaskTexture = SourceData->HideMaskTexture.LoadSynchronous())
	{
		SetBodyHideMaskMaterialParams(OriginalMesh, MaskTexture, SourceData->HideMaskMaxCullValue, SourceData->HideMaskMinKeepValue, SourceData->HideMaskMaxShrinkDistance);
	}
	else
	{
		SetBodyHideMaskMaterialParams(OriginalMesh, nullptr);
	}

	SourceData->bCullingBaked = false;
	CurrentData->bCullingBaked = false;
	OriginalMesh->MarkPackageDirty();

	if (USkeletalMesh* CulledMesh = SourceData->CulledBodyMesh.LoadSynchronous())
	{
		// Left on disk deliberately — unreferenced, so it no longer cooks; the next bake overwrites it.
		UE_LOG(LogDazToUnrealBlueprintUtils, Log, TEXT("RestoreMetaHumanOriginalBody: culled mesh kept at %s"), *CulledMesh->GetPathName());
	}

	FKismetEditorUtilities::CompileBlueprint(GeneratedBP);
	GeneratedBP->MarkPackageDirty();
	UE_LOG(LogDazToUnrealBlueprintUtils, Log, TEXT("RestoreMetaHumanOriginalBody: %s now uses %s"), *GeneratedBP->GetName(), *OriginalMesh->GetPathName());
	return true;
#else
	UE_LOG(LogDazToUnrealBlueprintUtils, Warning, TEXT("RestoreMetaHumanOriginalBody requires Unreal Engine 5.8 or later"));
	return false;
#endif
}

#undef LOCTEXT_NAMESPACE