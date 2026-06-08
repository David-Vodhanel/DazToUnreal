#pragma once

#include "CoreMinimal.h"
#include "DazToUnrealEnums.h"


class FDazToUnrealPoses
{
public:
	static UPoseAsset* CreatePoseAsset(UAnimSequence* SourceAnimation, TArray<FString> PoseNames);
};