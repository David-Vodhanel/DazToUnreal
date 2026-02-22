#include "DazToUnrealRetarget.h"

#include "Engine/SkeletalMesh.h"

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 3
#include "Rig/IKRigDefinition.h"
#include "RigEditor/IKRigDefinitionFactory.h"
#include "RigEditor/IKRigController.h"
#include "Retargeter/IKRetargeter.h"
#include "RetargetEditor/IKRetargetFactory.h"
#include "RetargetEditor/IKRetargeterController.h"
#elif ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION == 2
#include "IKRigDefinition.h"
#endif

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 2
#include "AssetToolsModule.h"
#endif

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 7
#include "RigEditor/IKRigAutoCharacterizer.h"
#include "RigEditor/IKRigAutoFBIK.h"
#include "Retargeter/RetargetOps/PelvisMotionOp.h"
#include "Retargeter/RetargetOps/IKChainsOp.h"
#include "Retargeter/RetargetOps/FKChainsOp.h"
#include "Retargeter/RetargetOps/RunIKRigOp.h"
#include "Rig/Solvers/IKRigFullBodyIK.h"
#endif

#include "DazToUnrealUtils.h"
#include "EditorFramework/AssetImportData.h"

EDazRetargetCharacterType FDazToUnrealRetarget::GetCharacterTypeFromMesh(USkeletalMesh* SkeletalMesh)
{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 7
	if (SkeletalMesh == nullptr)
	{
		return EDazRetargetCharacterType::Unknown;
	}

	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();

	// Lambda to check if all specified bones exist in the skeleton
	auto HasAllBones = [&RefSkeleton](const TArray<FName>& BoneNames) -> bool
	{
		for (const FName& BoneName : BoneNames)
		{
			if (RefSkeleton.FindBoneIndex(BoneName) == INDEX_NONE)
			{
				return false;
			}
		}
		return true;
	};

	// Order matters: more specific checks first
	if (HasAllBones({FName("Hips"), FName("HeadTop_End")}))
	{
		return EDazRetargetCharacterType::Mixamo;
	}
	if (HasAllBones({FName("hip"), FName("abdomenLower"), FName("abdomenUpper"), FName("chestUpper"), FName("lHeel")}))
	{
		return EDazRetargetCharacterType::Genesis3;
	}
	if (HasAllBones({FName("hip"), FName("abdomenLower"), FName("abdomenUpper"), FName("chestUpper")}))
	{
		return EDazRetargetCharacterType::Genesis8;
	}
	if (HasAllBones({FName("hip"), FName("pelvis"), FName("spine1"), FName("spine4")}))
	{
		return EDazRetargetCharacterType::Genesis9;
	}
#endif
	return EDazRetargetCharacterType::Unknown;
}

FName FDazToUnrealRetarget::GetPelvisBoneForMesh(USkeletalMesh* SkeletalMesh)
{
	EDazRetargetCharacterType CharacterType = GetCharacterTypeFromMesh(SkeletalMesh);

	switch (CharacterType)
	{
	case EDazRetargetCharacterType::Genesis3:
	case EDazRetargetCharacterType::Genesis8:
	case EDazRetargetCharacterType::Genesis9:
		return FName("hip");
	case EDazRetargetCharacterType::Mixamo:
		return FName("Hips");
	default:
		return FName("pelvis");
	}
}

UIKRigDefinition* FDazToUnrealRetarget::CreateIKRigForSkeletalMesh(USkeletalMesh* SkeletalMesh)
{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 7
	if (SkeletalMesh == nullptr)
	{
		return nullptr;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	FString PackagePath = FPaths::GetPath(SkeletalMesh->GetOutermost()->GetPathName());
	FString AssetName = FPaths::GetBaseFilename(SkeletalMesh->GetOutermost()->GetPathName()) + TEXT("_IKRig");
	UIKRigDefinitionFactory* RigDefinitionFactory = NewObject<UIKRigDefinitionFactory>();
	UObject* NewAsset = AssetToolsModule.Get().CreateAsset(*AssetName, *PackagePath, nullptr, RigDefinitionFactory);

	UIKRigDefinition* RigDefinition = Cast<UIKRigDefinition>(NewAsset);
	if (RigDefinition == nullptr)
	{
		return nullptr;
	}

	EDazRetargetCharacterType CharacterType = GetCharacterTypeFromMesh(SkeletalMesh);

	FRetargetDefinition RetargetDefinition;
	//TArray<FBoneSettingsForIK> AllBoneSettings;

	switch (CharacterType)
	{
	case EDazRetargetCharacterType::Genesis3:
	case EDazRetargetCharacterType::Genesis8:
		RetargetDefinition.RootBone = FName("hip");

		RetargetDefinition.AddBoneChain(FName("Spine"), FName("abdomenLower"), FName("chestUpper"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("Head"), FName("head"), FName("head"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("Neck"), FName("neckLower"), FName("neckUpper"), NAME_None);

		RetargetDefinition.AddBoneChain(FName("LeftArm"), FName("lShldrBend"), FName("lHand"), FName("lHand_Goal"));
		RetargetDefinition.AddBoneChain(FName("RightArm"), FName("rShldrBend"), FName("rHand"), FName("rHand_Goal"));

		RetargetDefinition.AddBoneChain(FName("LeftClavicle"), FName("lCollar"), FName("lCollar"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("RightClavicle"), FName("rCollar"), FName("rCollar"), NAME_None);

		RetargetDefinition.AddBoneChain(FName("LeftLeg"), FName("lThighBend"), FName("lToe"), FName("lToe_Goal"));
		RetargetDefinition.AddBoneChain(FName("RightLeg"), FName("rThighBend"), FName("rToe"), FName("rToe_Goal"));

		RetargetDefinition.AddBoneChain(FName("LeftPinky"), FName("lPinky1"), FName("lPinky3"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("RightPinky"), FName("rPinky1"), FName("rPinky3"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("LeftRing"), FName("lRing1"), FName("lRing3"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("RightRing"), FName("rRing1"), FName("rRing3"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("LeftMiddle"), FName("lMid1"), FName("lMid3"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("RightMiddle"), FName("rMid1"), FName("rMid3"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("LeftIndex"), FName("lIndex1"), FName("lIndex3"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("RightIndex"), FName("rIndex1"), FName("rIndex3"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("LeftThumb"), FName("lThumb1"), FName("lThumb3"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("RightThumb"), FName("rThumb1"), FName("rThumb3"), NAME_None);

		RetargetDefinition.AddBoneChain(FName("HandRootIK"), FName("ik_hand_root"), FName("ik_hand_root"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("RightHandIK"), FName("ik_hand_r"), FName("ik_hand_r"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("HandGunIK"), FName("ik_hand_gun"), FName("ik_hand_gun"), NAME_None);

		RetargetDefinition.AddBoneChain(FName("FootRootIK"), FName("ik_foot_root"), FName("ik_foot_root"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("LeftFootIK"), FName("ik_foot_l"), FName("ik_foot_l"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("RightFootIK"), FName("ik_foot_r"), FName("ik_foot_r"), NAME_None);

		RetargetDefinition.AddBoneChain(FName("Root"), FName("root"), FName("root"), NAME_None);
		break;

	case EDazRetargetCharacterType::Genesis9:
		RetargetDefinition.RootBone = FName("hip");

		RetargetDefinition.AddBoneChain(FName("Spine"), FName("spine1"), FName("spine4"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("Head"), FName("head"), FName("head"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("Neck"), FName("neck1"), FName("neck2"), NAME_None);

		RetargetDefinition.AddBoneChain(FName("LeftArm"), FName("l_upperarm"), FName("l_hand"), FName("l_hand_Goal"));
		RetargetDefinition.AddBoneChain(FName("RightArm"), FName("r_upperarm"), FName("r_hand"), FName("r_hand_Goal"));

		RetargetDefinition.AddBoneChain(FName("LeftClavicle"), FName("l_shoulder"), FName("l_shoulder"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("RightClavicle"), FName("r_shoulder"), FName("r_shoulder"), NAME_None);

		RetargetDefinition.AddBoneChain(FName("LeftLeg"), FName("l_thigh"), FName("l_toes"), FName("l_toes_Goal"));
		RetargetDefinition.AddBoneChain(FName("RightLeg"), FName("r_thigh"), FName("r_toes"), FName("r_toes_Goal"));

		RetargetDefinition.AddBoneChain(FName("LeftPinky"), FName("l_pinky1"), FName("l_pinky3"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("RightPinky"), FName("r_pinky1"), FName("r_pinky3"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("LeftRing"), FName("l_ring1"), FName("l_ring3"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("RightRing"), FName("r_ring1"), FName("r_ring3"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("LeftMiddle"), FName("l_mid1"), FName("l_mid3"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("RightMiddle"), FName("r_mid1"), FName("r_mid3"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("LeftIndex"), FName("l_index1"), FName("l_index3"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("RightIndex"), FName("r_index1"), FName("r_index3"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("LeftThumb"), FName("l_thumb1"), FName("l_thumb3"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("RightThumb"), FName("r_thumb1"), FName("r_thumb3"), NAME_None);

		RetargetDefinition.AddBoneChain(FName("HandRootIK"), FName("ik_hand_root"), FName("ik_hand_root"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("RightHandIK"), FName("ik_hand_r"), FName("ik_hand_r"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("HandGunIK"), FName("ik_hand_gun"), FName("ik_hand_gun"), NAME_None);

		RetargetDefinition.AddBoneChain(FName("FootRootIK"), FName("ik_foot_root"), FName("ik_foot_root"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("LeftFootIK"), FName("ik_foot_l"), FName("ik_foot_l"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("RightFootIK"), FName("ik_foot_r"), FName("ik_foot_r"), NAME_None);

		RetargetDefinition.AddBoneChain(FName("Root"), FName("root"), FName("root"), NAME_None);

		break;

	case EDazRetargetCharacterType::Mixamo:
		RetargetDefinition.RootBone = FName("Hips");

		RetargetDefinition.AddBoneChain(FName("Spine"), FName("Spine"), FName("Spine2"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("Head"), FName("Head"), FName("Head"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("Neck"), FName("Neck"), FName("Neck"), NAME_None);

		RetargetDefinition.AddBoneChain(FName("LeftArm"), FName("LeftArm"), FName("LeftHand"), FName("LeftHand_Goal"));
		RetargetDefinition.AddBoneChain(FName("RightArm"), FName("RightArm"), FName("RightHand"), FName("RightHand_Goal"));

		RetargetDefinition.AddBoneChain(FName("LeftClavicle"), FName("LeftShoulder"), FName("LeftShoulder"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("RightClavicle"), FName("RightShoulder"), FName("RightShoulder"), NAME_None);

		RetargetDefinition.AddBoneChain(FName("LeftLeg"), FName("LeftUpLeg"), FName("LeftToeBase"), FName("LeftToeBase_Goal"));
		RetargetDefinition.AddBoneChain(FName("RightLeg"), FName("RightUpLeg"), FName("RightToeBase"), FName("RightToeBase_Goal"));

		RetargetDefinition.AddBoneChain(FName("LeftPinky"), FName("LeftHandPinky1"), FName("LeftHandPinky3"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("RightPinky"), FName("RightHandPinky1"), FName("RightHandPinky3"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("LeftRing"), FName("LeftHandRing1"), FName("LeftHandRing3"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("RightRing"), FName("RightHandRing1"), FName("RightHandRing3"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("LeftMiddle"), FName("LeftHandMiddle1"), FName("LeftHandMiddle3"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("RightMiddle"), FName("RightHandMiddle1"), FName("RightHandMiddle3"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("LeftIndex"), FName("LeftHandIndex1"), FName("LeftHandIndex3"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("RightIndex"), FName("RightHandIndex1"), FName("RightHandIndex3"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("LeftThumb"), FName("LeftHandThumb1"), FName("LeftHandThumb3"), NAME_None);
		RetargetDefinition.AddBoneChain(FName("RightThumb"), FName("RightHandThumb1"), FName("RightHandThumb3"), NAME_None);
		break;

	default:
		break;
	}

	const UIKRigController* Controller = UIKRigController::GetController(RigDefinition);
	Controller->SetSkeletalMesh(SkeletalMesh);
	if (CharacterType == EDazRetargetCharacterType::Unknown)
	{
		FAutoCharacterizeResults CharacterizeResults;
		Controller->AutoGenerateRetargetDefinition(CharacterizeResults);
		Controller->SetRetargetDefinition(CharacterizeResults.AutoRetargetDefinition.RetargetDefinition);
		FAutoFBIKResults AutoFBIKResults;
		Controller->AutoGenerateFBIK(AutoFBIKResults);
	}
	else
	{
		FAutoFBIKResults AutoFBIKResults;
		Controller->SetRetargetDefinition(RetargetDefinition);
		FDazToUnrealRetarget::CreateFBIKSetup(*Controller, RetargetDefinition);
	}

	return RigDefinition;
#else
	return nullptr;
#endif
}

UIKRetargeter* FDazToUnrealRetarget::CreateIKRetargeter(UIKRigDefinition* SourceIKRig, UIKRigDefinition* TargetIKRig)
{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 7
	if (SourceIKRig == nullptr || TargetIKRig == nullptr)
	{
		return nullptr;
	}

	// Get the skeletal meshes from the IK rigs
	const UIKRigController* SourceIKRigController = UIKRigController::GetController(SourceIKRig);
	const UIKRigController* TargetIKRigController = UIKRigController::GetController(TargetIKRig);
	USkeletalMesh* SourceMesh = SourceIKRigController->GetSkeletalMesh();
	USkeletalMesh* TargetMesh = TargetIKRigController->GetSkeletalMesh();

	// Derive asset name: Source_to_Target_IKRetargeter
	FString SourceRigName = FPaths::GetBaseFilename(SourceIKRig->GetOutermost()->GetPathName()).Replace(TEXT("_IKRig"), TEXT(""));
	FString TargetRigName = FPaths::GetBaseFilename(TargetIKRig->GetOutermost()->GetPathName()).Replace(TEXT("_IKRig"), TEXT(""));
	FString PackagePath = FPaths::GetPath(TargetIKRig->GetOutermost()->GetPathName());
	FString AssetName = SourceRigName + TEXT("_to_") + TargetRigName + TEXT("_IKRetargeter");

	// Check if it already exists
	FString FullAssetPath = PackagePath + TEXT("/") + AssetName;
	UIKRetargeter* Retargeter = Cast<UIKRetargeter>(StaticLoadObject(UIKRetargeter::StaticClass(), nullptr, *FullAssetPath));

	// If not, create a new one
	if (Retargeter == nullptr)
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		UIKRetargetFactory* RetargetFactory = NewObject<UIKRetargetFactory>();
		UObject* NewAsset = AssetToolsModule.Get().CreateAsset(*AssetName, *PackagePath, nullptr, RetargetFactory);
		Retargeter = Cast<UIKRetargeter>(NewAsset);
	}

	if (Retargeter == nullptr)
	{
		return nullptr;
	}

	// Get the retargeter controller
	UIKRetargeterController* RetargeterController = UIKRetargeterController::GetController(Retargeter);

	// Assign source and target IK rigs
	RetargeterController->SetIKRig(ERetargetSourceOrTarget::Source, SourceIKRig);
	RetargeterController->SetIKRig(ERetargetSourceOrTarget::Target, TargetIKRig);

	// Auto-map chains by exact name match
	RetargeterController->AutoMapChains(EAutoMapChainType::Exact, true);

	// Check if the source and target have different facing directions
	bool bSourceForceFrontX = FDazToUnrealUtils::IsModelFacingX(SourceMesh);
	bool bTargetForceFrontX = FDazToUnrealUtils::IsModelFacingX(TargetMesh);

	// Rotate the root if the target faces right but source does not
	if (!bSourceForceFrontX && bTargetForceFrontX)
	{
		FRotator RotationOffset(0.0f, 90.0f, 0.0f);
		RetargeterController->SetRotationOffsetForRetargetPoseBone(FName("root"), RotationOffset.Quaternion(), ERetargetSourceOrTarget::Target);
	}

	// Setup the Root chain translation mode to Absolute
	FTargetChainSettings RootChainSettings = RetargeterController->GetRetargetChainSettings(FName("Root"));
	RootChainSettings.FK.TranslationMode = ERetargetTranslationMode::Absolute;
	RetargeterController->SetRetargetChainSettings(FName("Root"), RootChainSettings);

	// Set root settings - blend to source fixes animation hitches
	FTargetRootSettings RootSettings = RetargeterController->GetRootSettings();
	RootSettings.BlendToSource = 1.0f;
	RetargeterController->SetRootSettings(RootSettings);

	// Get the pelvis motion op (it's typically added by default)
	if (FIKRetargetPelvisMotionOp* PelvisOp = RetargeterController->GetFirstRetargetOpOfType<FIKRetargetPelvisMotionOp>())
	{
		// Set the source pelvis bone
		PelvisOp->Settings.SourcePelvisBone = GetPelvisBoneForMesh(SourceMesh);

		// Set the target pelvis bone
		PelvisOp->Settings.TargetPelvisBone = GetPelvisBoneForMesh(TargetMesh);
	}

	// Map the FKChains
	if (FIKRetargetFKChainsOp* FKChainsOp = RetargeterController->GetFirstRetargetOpOfType<FIKRetargetFKChainsOp>())
	{
		FKChainsOp->GetChainMapping()->ReinitializeWithIKRigs(SourceIKRig, TargetIKRig);
		FKChainsOp->Settings.IKRigAsset = TargetIKRig;
		FKChainsOp->GetChainMapping()->AutoMapChains(EAutoMapChainType::Exact, true);
	}

	// Remove the auto added Retarget IK Goals Op (not sure why it's broken). 
	// There's something wrong with the memory on it, so going to avoid accessing it by assuming it's the only child op
	for (int32 OpIndex = 0; OpIndex < RetargeterController->GetNumRetargetOps(); OpIndex++)
	{
		if (RetargeterController->GetParentOpIndex(OpIndex) != INDEX_NONE)
		{
			RetargeterController->RemoveRetargetOp(OpIndex);
			break;
		}
	}

	if (FIKRetargetRunIKRigOp* IKChainsOp = RetargeterController->GetFirstRetargetOpOfType<FIKRetargetRunIKRigOp>())
	{
		IKChainsOp->GetChainMapping()->ReinitializeWithIKRigs(SourceIKRig, TargetIKRig);
		IKChainsOp->Settings.IKRigAsset = TargetIKRig;
		IKChainsOp->GetChainMapping()->AutoMapChains(EAutoMapChainType::Exact, true);
		IKChainsOp->SetEnabled(false);

		const int32 RunIKIndex = RetargeterController->GetIndexOfRetargetOp(IKChainsOp);
		const FName RunIKOpName = RetargeterController->GetOpName(RunIKIndex);
		RetargeterController->AddRetargetOp(FIKRetargetIKChainsOp::StaticStruct(), RunIKOpName);

		if (FIKRetargetIKChainsOp* IKGoalsOp = RetargeterController->GetFirstRetargetOpOfType<FIKRetargetIKChainsOp>())
		{
			for (auto& ChainSettings : IKGoalsOp->Settings.ChainsToRetarget)
			{
				ChainSettings.BlendToSource = 1.0f;
			}
		}
	}

	// Auto align the bones
	RetargeterController->AutoAlignAllBones(ERetargetSourceOrTarget::Target, ERetargetAutoAlignMethod::ChainToChain);

	return Retargeter;
#else
	return nullptr;
#endif
}

// This is a slightly modified partial duplicate of FAutoFBIKCreator::CreateFBIKSetup which isn't accessible
void FDazToUnrealRetarget::CreateFBIKSetup(const UIKRigController& IKRigController, const FRetargetDefinition& RetargetDefinition)
{
	// ensure we have a mesh to operate on
	USkeletalMesh* Mesh = IKRigController.GetSkeletalMesh();
	if (!Mesh)
	{
		return;
	}

	// create all the goals in the template
	TArray<FName> GoalNames;
	const TArray<FBoneChain>& ExpectedChains = RetargetDefinition.BoneChains;
	for (const FBoneChain& ExpectedChain : ExpectedChains)
	{
		if (ExpectedChain.IKGoalName == NAME_None)
		{
			continue;
		}

		const FBoneChain* Chain = IKRigController.GetRetargetChainByName(ExpectedChain.ChainName);
		FName GoalName = (Chain && Chain->IKGoalName != NAME_None) ? Chain->IKGoalName : ExpectedChain.IKGoalName;
		const UIKRigEffectorGoal* ChainGoal = IKRigController.GetGoal(GoalName);
		if (!ChainGoal)
		{
			// create new goal
			GoalName = IKRigController.AddNewGoal(GoalName, ExpectedChain.EndBone.BoneName);
			if (Chain)
			{
				IKRigController.SetRetargetChainGoal(Chain->ChainName, GoalName);
			}
		}

		GoalNames.Add(GoalName);
	}

	// create IK solver and attach all the goals to it
	const int32 SolverIndex = IKRigController.AddSolver(FIKRigFullBodyIKSolver::StaticStruct());
	for (const FName& GoalName : GoalNames)
	{
		IKRigController.ConnectGoalToSolver(GoalName, SolverIndex);
	}

	// set the root of the solver
	const bool bSetRoot = IKRigController.SetStartBone(RetargetDefinition.RootBone, SolverIndex);
	if (!bSetRoot)
	{
		//Results.Outcome = EAutoFBIKResult::MissingRootBone;
		return;
	}

	// update solver settings for retargeting
	FIKRigFullBodyIKSolver* Solver = static_cast<FIKRigFullBodyIKSolver*>(IKRigController.GetSolverAtIndex(SolverIndex));
	// set the root behavior to "free", allows pelvis motion only when needed to reach goals
	Solver->Settings.RootBehavior = EPBIKRootBehavior::Free;
	// removing pull chain alpha on all goals "calms" the motion down, especially when retargeting arms
	Solver->Settings.GlobalPullChainAlpha = 0.0f;
}