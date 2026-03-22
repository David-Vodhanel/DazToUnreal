#pragma once

#include "CoreMinimal.h"

class UPoseAsset;
class UAnimSequence;

class FDazToUnrealPoses
{
public:
	static UPoseAsset* CreatePoseAsset(UAnimSequence* SourceAnimation, TArray<FString> PoseNames);
};