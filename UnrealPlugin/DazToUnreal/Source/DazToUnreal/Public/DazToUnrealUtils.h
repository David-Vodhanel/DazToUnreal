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

	// Copies the Daz-side plugin DLL(s) shipped in this plugin's Resources folder
	// into each installed Daz Studio's "plugins" directory, elevating only the
	// write-protected targets under a single UAC prompt. Reports the result via a
	// message dialog. Windows-only (other platforms show an explanatory dialog).
	static void InstallDazStudioPlugin();

private:
	static bool IsSkeletonUsed(FSoftObjectPath SkeletonPath);
};