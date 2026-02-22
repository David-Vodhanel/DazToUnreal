#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UIKRigDefinition;
class UIKRetargeter;
class UIKRigController;
struct FRetargetDefinition;
struct FAutoFBIKResults;
struct FBoneSettingsForIK;

enum class EDazRetargetCharacterType : uint8
{
	Unknown,
	Genesis3,
	Genesis8,
	Genesis9,
	Mixamo
};

class FDazToUnrealRetarget
{
public:
	static EDazRetargetCharacterType GetCharacterTypeFromMesh(USkeletalMesh* SkeletalMesh);
	static FName GetPelvisBoneForMesh(USkeletalMesh* SkeletalMesh);
	static UIKRigDefinition* CreateIKRigForSkeletalMesh(USkeletalMesh* SkeletalMesh);
	static UIKRetargeter* CreateIKRetargeter(UIKRigDefinition* SourceIKRig, UIKRigDefinition* TargetIKRig);
	static void CreateFBIKSetup(const UIKRigController& IKRigController, const FRetargetDefinition& RetargetDefinition);
private:

};
