#include "DazToUnrealMorphs.h"
#include "DazJointControlledMorphAnimInstance.h"
#include "Serialization/JsonReader.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "EdGraphSchema_K2_Actions.h"
#include "AnimGraphNode_LinkedInputPose.h"
#if ENGINE_MAJOR_VERSION > 4
#include "AnimGraphNode_ControlRig.h"
#endif
#include "Kismet2/BlueprintEditorUtils.h"
#include "Animation/MorphTarget.h"
#include "LODUtilities.h"
#include "Math/DualQuat.h"

UAnimBlueprint* FDazToUnrealMorphs::CreateJointControlAnimation(TSharedPtr<FJsonObject> JsonObject, FString Folder, FString CharacterName, USkeleton* Skeleton, USkeletalMesh* Mesh)
{
	// Only only create the JCM Anim if the JointLinks object exists
	const TArray<TSharedPtr<FJsonValue>>* JointLinkList;// = JsonObject->GetArrayField(TEXT("JointLinks"));
	if (JsonObject->TryGetArrayField(TEXT("JointLinks"), JointLinkList))
	{
		const FString AssetName = CharacterName + TEXT("_JCMAnim");
		const FString PackageName = FPackageName::ObjectPathToPackageName(Folder) / AssetName;
		const FString ObjectName = FPackageName::ObjectPathToObjectName(PackageName);

		// Create the new blueprint next to the SkeletalMesh and get the AnimInstance from it.
#if ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION < 26
		UPackage* NewPackage = CreatePackage(nullptr, *PackageName);
#else
		UPackage* NewPackage = CreatePackage(*PackageName);
#endif
		UAnimBlueprint* AnimBlueprint = CreateBlueprint(NewPackage, FName(AssetName), Skeleton);
		UAnimInstance* AnimInstance = Cast<UAnimInstance>(AnimBlueprint->GetAnimBlueprintGeneratedClass()->ClassDefaultObject);


#if 0
#if ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION <= 26
		FLODUtilities::RegenerateLOD(Mesh, -1, true, true);
#else
		FLODUtilities::RegenerateLOD(Mesh, nullptr, -1, true, true);
#endif
#endif


		// Recompile the AnimBlueprint and return the updated AnimInstance
		FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBlueprint);
		FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
		
		return AnimBlueprint;
	}
	return nullptr;
}

// This is a stripped down version of UAnimBlueprintFactory::FactoryCreateNew
UAnimBlueprint* FDazToUnrealMorphs::CreateBlueprint(UObject* InParent, FName Name, USkeleton* Skeleton)
{
	// Create the Blueprint
	UClass* ClassToUse = UAnimInstance::StaticClass();
	UAnimBlueprint* NewBP = CastChecked<UAnimBlueprint>(FKismetEditorUtilities::CreateBlueprint(ClassToUse, InParent, Name, BPTYPE_Normal, UAnimBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), NAME_None));

	NewBP->TargetSkeleton = Skeleton;

	// Because the BP itself didn't have the skeleton set when the initial compile occured, it's not set on the generated classes either
	if (UAnimBlueprintGeneratedClass* TypedNewClass = Cast<UAnimBlueprintGeneratedClass>(NewBP->GeneratedClass))
	{
		TypedNewClass->TargetSkeleton = Skeleton;
	}
	if (UAnimBlueprintGeneratedClass* TypedNewClass_SKEL = Cast<UAnimBlueprintGeneratedClass>(NewBP->SkeletonGeneratedClass))
	{
		TypedNewClass_SKEL->TargetSkeleton = Skeleton;
	}

#if ENGINE_MAJOR_VERSION > 4
	// Need to add the Pose Input Node and connect it to the Output Node in the AnimGraph
	TArray<UEdGraph*> Graphs;
	NewBP->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph->GetFName() == UEdGraphSchema_K2::GN_AnimGraph)
		{
			UEdGraphNode* OutNode = nullptr;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				OutNode = Node;
			}
			// If the Blueprint isn't open in an Editor Window adding the UAnimGraphNode_LinkedInputPose node will crash.
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(NewBP);

			// Create the Input Node
			UAnimGraphNode_LinkedInputPose* const InputTemplate = NewObject<UAnimGraphNode_LinkedInputPose>();
			UEdGraphNode* const NewNode = FEdGraphSchemaAction_NewNode::SpawnNodeFromTemplate<UAnimGraphNode_LinkedInputPose>(Graph, InputTemplate, FVector2D(0.0f, 0.0f), false);

			// Create the Control Rig Node
			UAnimGraphNode_ControlRig* const ControlRigTemplate = NewObject<UAnimGraphNode_ControlRig>();
			UEdGraphNode* const ControlRigNode = FEdGraphSchemaAction_NewNode::SpawnNodeFromTemplate<UAnimGraphNode_ControlRig>(Graph, ControlRigTemplate, FVector2D(0.0f, 0.0f), false);

			if (NewNode && OutNode && ControlRigNode)
			{
				UEdGraphPin* NextPin = NewNode->FindPin(TEXT("Pose"));

				UEdGraphPin* ControlRigSourcePin = ControlRigNode->FindPin(TEXT("Source"));
				UEdGraphPin* ControlRigNextPin = ControlRigNode->FindPin(TEXT("Pose"));

				UEdGraphPin* OutPin = OutNode->FindPin(TEXT("Result"));

				NextPin->MakeLinkTo(ControlRigSourcePin);
				ControlRigNextPin->MakeLinkTo(OutPin);
			}

			// Don't need the editor open anymore, so close it now.
			// Note: Turns out I can't close it because part of the action happens on a tick
			//GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(NewBP);
		}
	}
#endif
	return NewBP;
}

bool FDazToUnrealMorphs::IsAutoJCMImport(TSharedPtr<FJsonObject> JsonObject)
{
	// UE4 doesn't have some of the functions needed for creating the control rig and blueprint automatically.
#if ENGINE_MAJOR_VERSION > 4
	const TArray<TSharedPtr<FJsonValue>>* JointLinkList;
	if (JsonObject->TryGetArrayField(TEXT("JointLinks"), JointLinkList))
	{
		return true;
	}
#endif
	return false;

}