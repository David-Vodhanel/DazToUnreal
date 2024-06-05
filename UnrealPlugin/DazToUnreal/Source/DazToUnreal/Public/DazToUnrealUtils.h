#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "DazToUnrealEnums.h"

class FDazToUnrealUtils
{
public:
	static FString SanitizeName(FString OriginalName);
	static bool MakeDirectoryAndCheck(FString& Directory);
	static bool IsModelFacingX(UObject* MeshObject);
	static FString GetDTUPathForModel(FSoftObjectPath MeshObjectPath);
	static FSoftObjectPath GetSkeletonForImport(const DazToUnrealImportData& DazImportData);

private:
	static bool IsSkeletonUsed(FSoftObjectPath SkeletonPath);
};