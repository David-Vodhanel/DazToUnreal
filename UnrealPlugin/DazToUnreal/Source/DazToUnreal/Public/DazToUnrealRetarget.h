#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UIKRigDefinition;
class UIKRetargeter;

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

private:

};
