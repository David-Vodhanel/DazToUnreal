#include "DazToUnrealUtils.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/MessageDialog.h"
#include "Interfaces/IPluginManager.h"
#include "UObject/SoftObjectPath.h"
#include "AssetRegistry/AssetRegistryModule.h"

#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"

#include "Engine/StaticMesh.h"
#include "EditorFramework/AssetImportData.h"
#include "Factories/FbxAssetImportData.h"

#include "DazToUnrealSettings.h"

// Names produced here are concatenated into asset paths and into on-disk file
// names, so the separators matter for safety and not just for cosmetics: a
// DTU-supplied name carrying a slash would otherwise steer a write into a
// folder the caller never named. Backslash and colon were already covered, and
// '.' -> '_' is what stops a ".." segment forming; the forward slash and quote
// close the remaining gaps.
//
// Deliberately still a denylist rather than an allowlist: an allowlist would
// change the names of assets in projects that already imported characters, so
// re-importing would create duplicates instead of updating in place.
FString FDazToUnrealUtils::SanitizeName(FString OriginalName)
{
	return OriginalName.Replace(TEXT(" "), TEXT(""))
		.Replace(TEXT("("), TEXT("_"))
		.Replace(TEXT(")"), TEXT("_"))
		.Replace(TEXT("."), TEXT("_"))
		.Replace(TEXT("&"), TEXT("_"))
		.Replace(TEXT("!"), TEXT("_"))
		.Replace(TEXT("*"), TEXT("_"))
		.Replace(TEXT("<"), TEXT("_"))
		.Replace(TEXT(">"), TEXT("_"))
		.Replace(TEXT("?"), TEXT("_"))
		.Replace(TEXT("\\"), TEXT("_"))
		.Replace(TEXT("/"), TEXT("_"))
		.Replace(TEXT(":"), TEXT("_"))
		.Replace(TEXT("\""), TEXT("_"))
		.Replace(TEXT("|"), TEXT("_"))
		.Replace(TEXT("'"), TEXT("_"));
}

bool FDazToUnrealUtils::MakeDirectoryAndCheck(FString& Directory)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!FPaths::DirectoryExists(Directory))
	{
		PlatformFile.CreateDirectoryTree(*Directory);
		if (!FPaths::DirectoryExists(Directory))
		{
			UE_LOG(LogTemp, Warning, TEXT("DazToUnreal: ERROR: Unable to create directory tree: %s"), *Directory);
			return false;
		}
	}
	return true;
}

bool FDazToUnrealUtils::IsModelFacingX(UObject* MeshObject)
{
	if(USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(MeshObject))
	{
#if ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION < 27
		if (UAssetImportData* AssetImportData = SkeletalMesh->AssetImportData)
#else
		if (UAssetImportData* AssetImportData = SkeletalMesh->GetAssetImportData())
#endif
		{
			UFbxAssetImportData* FbxAssetImportData = Cast<UFbxAssetImportData>(AssetImportData);
			if (FbxAssetImportData != nullptr && FbxAssetImportData->bForceFrontXAxis)
			{
				return true;
			}
		}
	}
	if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(MeshObject))
	{
#if (ENGINE_MAJOR_VERSION > 5) || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
		if (UAssetImportData* AssetImportData = StaticMesh->GetAssetImportData())
#else
		if (UAssetImportData* AssetImportData = StaticMesh->AssetImportData)
#endif
		{
			UFbxAssetImportData* FbxAssetImportData = Cast<UFbxAssetImportData>(AssetImportData);
			if (FbxAssetImportData != nullptr && FbxAssetImportData->bForceFrontXAxis)
			{
				return true;
			}
		}
	}
	return false;
}

FString FDazToUnrealUtils::GetDTUPathForModel(FSoftObjectPath MeshObjectPath)
{
	if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(MeshObjectPath.TryLoad()))
	{
#if ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION < 27
		if (UAssetImportData* AssetImportData = SkeletalMesh->AssetImportData)
#else
		if (UAssetImportData* AssetImportData = SkeletalMesh->GetAssetImportData())
#endif
		{
			if (UFbxAssetImportData* FbxAssetImportData = Cast<UFbxAssetImportData>(AssetImportData))
			{
				for (FAssetImportInfo::FSourceFile SourceFile : FbxAssetImportData->GetSourceData().SourceFiles)
				{
					FString SourceFilePath = SourceFile.RelativeFilename;
					TArray<FString> LikelyPaths;
					LikelyPaths.Add(FPaths::ChangeExtension(SourceFilePath, TEXT("dtu")));
					LikelyPaths.Add(FPaths::GetPath(SourceFilePath) + TEXT("/../") + FPaths::ChangeExtension(FPaths::GetCleanFilename(SourceFilePath), TEXT("dtu")));
					for (FString PossiblePath : LikelyPaths)
					{
						if (FPaths::FileExists(PossiblePath))
						{
							return PossiblePath;
						}
					}
				}
			}
		}
	}

	return FString();
}

FSoftObjectPath FDazToUnrealUtils::GetSkeletonForImport(const DazToUnrealImportData& DazImportData)
{
	UDazToUnrealSettings* CachedSettings = GetMutableDefault<UDazToUnrealSettings>();

	USkeleton* Skeleton = nullptr;
	FSoftObjectPath SkeletonPath;
	if (!DazImportData.bCreateUniqueSkeleton)
	{
		if (DazImportData.bFixTwistBones)
		{
			// Some character types share a skeleton.  Get the mapped name.
			FString MappedSkeletonName = DazImportData.CharacterTypeName;
			if (CachedSettings->CharacterTypeMapping.Contains(DazImportData.CharacterTypeName))
			{
				MappedSkeletonName = CachedSettings->CharacterTypeMapping[DazImportData.CharacterTypeName];
			}

			if (CachedSettings->SkeletonsWithTwistFix.Contains(MappedSkeletonName))
			{
				Skeleton = (USkeleton*)CachedSettings->SkeletonsWithTwistFix[MappedSkeletonName].TryLoad();
				if (Skeleton)
				{
					SkeletonPath = CachedSettings->SkeletonsWithTwistFix[MappedSkeletonName];
				}
				else
				{
					CachedSettings->SkeletonsWithTwistFix.Remove(MappedSkeletonName);
				}
			}
		}
		else
		{
			// Some character types share a skeleton.  Get the mapped name.
			FString MappedSkeletonName = DazImportData.CharacterTypeName;
			if (CachedSettings->CharacterTypeMapping.Contains(DazImportData.CharacterTypeName))
			{
				MappedSkeletonName = CachedSettings->CharacterTypeMapping[DazImportData.CharacterTypeName];
			}

			// Look for an existing skeleton for the project.
			if (CachedSettings->OtherSkeletons.Contains(MappedSkeletonName))
			{
				Skeleton = (USkeleton*)CachedSettings->OtherSkeletons[MappedSkeletonName].TryLoad();
				if (Skeleton)
				{
					SkeletonPath = CachedSettings->OtherSkeletons[MappedSkeletonName];
				}
				else
				{
					CachedSettings->OtherSkeletons.Remove(MappedSkeletonName);
				}
			}
			else
			{
				// Check in the plugin for a skeleton (going away soon)
				if (DazImportData.CharacterType == DazCharacterType::Genesis1)
				{
					Skeleton = (USkeleton*)CachedSettings->Genesis1Skeleton.TryLoad();
					SkeletonPath = CachedSettings->Genesis1Skeleton;
				}
				if (DazImportData.CharacterType == DazCharacterType::Genesis3Male || DazImportData.CharacterType == DazCharacterType::Genesis3Female)
				{
					Skeleton = (USkeleton*)CachedSettings->Genesis3Skeleton.TryLoad();
					SkeletonPath = CachedSettings->Genesis3Skeleton;
				}
				if (DazImportData.CharacterType == DazCharacterType::Genesis8Male || DazImportData.CharacterType == DazCharacterType::Genesis8Female)
				{
					Skeleton = (USkeleton*)CachedSettings->Genesis8Skeleton.TryLoad();
					SkeletonPath = CachedSettings->Genesis8Skeleton;
				}
			}


			// Only return one of the plugin skeletons if it's already used in the project.
			// We're moving away from using skeletons that are included with the plugin
			if (Skeleton && SkeletonPath.ToString().StartsWith(TEXT("/DazToUnreal/")))
			{
				if (!IsSkeletonUsed(SkeletonPath))
				{
					Skeleton = nullptr;
					SkeletonPath.Reset();
				}
			}
		}
	}

	return SkeletonPath;
}

bool FDazToUnrealUtils::IsSkeletonUsed(FSoftObjectPath SkeletonPath)
{
#if ENGINE_MAJOR_VERSION > 4
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetIdentifier> Referencers;
	AssetRegistry.GetReferencers(FAssetIdentifier(SkeletonPath.GetLongPackageFName()), Referencers);
	for (const FAssetIdentifier& Identifier : Referencers)
	{
		TArray<FAssetData> Assets;
		AssetRegistry.GetAssetsByPackageName(Identifier.PackageName, Assets);

		for (const FAssetData& Asset : Assets)
		{
			if (Asset.IsInstanceOf(USkeletalMesh::StaticClass()))
			{
				return true;
			}
		}
	}
	return false;
#else
	return true;
#endif
}

// ---------------------------------------------------------------------------
// Daz Studio plugin install
//
// Copies the Daz-side plugin DLL that ships in this plugin's Resources folder
// into each installed Daz Studio's "plugins" directory. The copy is attempted
// without elevation first; only the targets whose plugins folder is
// write-protected are elevated, each by invoking the in-box, Microsoft-signed
// robocopy.exe directly, so no helper executable has to be shipped.
//
// No command interpreter is involved. The destination is derived from a
// registry value the unelevated user can write, so it is validated by
// IsSafeInstallPath before use and never passed through a shell that could read
// part of it as extra arguments.
//
// All registry reads happen here, in the unelevated editor process: the keys
// live under HKCU and an elevated process could resolve them against the admin
// account's hive instead of the real user's. Resolved destination paths are
// passed to the elevated step, which never touches the registry.
//
// Lives at the end of this translation unit, inside a PLATFORM_WINDOWS block,
// so the Windows.h includes it needs don't leak their API macros into the rest
// of this file.
// ---------------------------------------------------------------------------
#if PLATFORM_WINDOWS

#define LOCTEXT_NAMESPACE "FDazToUnrealUtils"

#include "Windows/WindowsPlatformMisc.h" // FWindowsPlatformMisc::QueryRegKey
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h> // CopyFileW + GetLastError, to tell ACCESS_DENIED from SHARING_VIOLATION

namespace
{
	// One Daz Studio version we know how to build a plugin for. The DLL name is
	// version-specific (DS6 only loads plugins named dsp_*.dll), so each version
	// maps to its own shipped binary rather than a single shared name.
	struct FKnownVersion
	{
		const TCHAR* Label;		// user-facing, e.g. "Daz Studio 4"
		const TCHAR* RegSubKey;	// under HKEY_CURRENT_USER
		const TCHAR* DllName;	// matching build shipped in Resources/
	};

	static const FKnownVersion KnownVersions[] =
	{
		{ TEXT("Daz Studio 4"), TEXT("Software\\DAZ\\Studio4"), TEXT("dzunrealbridge.dll") },
		{ TEXT("Daz Studio 6"), TEXT("Software\\DAZ\\Studio6"), TEXT("dsp_daztounreal.dll") },
	};

	struct FTarget
	{
		FString VersionLabel;
		FString DllName;
		FString SourceDir;	// Resources folder (where the source DLL lives)
		FString SourcePath;	// SourceDir\DllName
		FString PluginsDir;	// <install>\plugins
		FString DestPath;	// PluginsDir\DllName

		// Lets TArray::Contains() identify a target; DestPath is unique per target.
		bool operator==(const FTarget& Other) const { return DestPath == Other.DestPath; }
	};

	enum class EDazCopyResult
	{
		Done,			// copied (or already in sync) without elevation
		NeedsElevation,	// folder is write-protected
		DazRunning,		// existing DLL is loaded (file locked)
		Failed,			// anything else (e.g. source missing)
	};

	// "C:/Program Files/DAZ 3D/DAZStudio4" -> "C:\Program Files\DAZ 3D\DAZStudio4\plugins"
	static FString InstallPathToPluginsDir(const FString& InstallPath)
	{
		FString Path = InstallPath.Replace(TEXT("/"), TEXT("\\"));
		while (Path.EndsWith(TEXT("\\")))
		{
			Path = Path.LeftChop(1);
		}
		return Path + TEXT("\\plugins");
	}

	// The install path comes out of HKEY_CURRENT_USER, which the (unelevated)
	// user can write, and it ends up as an argument to a process we launch
	// ELEVATED. A quote would end the argument early and let the rest of the
	// value be read as further arguments - robocopy flags such as /MIR or
	// /PURGE would turn the single-file copy into a destructive mirror. Real
	// Windows paths contain none of these characters, so reject rather than
	// escape, and require something that actually looks like an absolute path.
	static bool IsSafeInstallPath(const FString& InstallPath)
	{
		if (InstallPath.IsEmpty() || InstallPath.Len() > 240)
		{
			return false;
		}

		// Must be a plain absolute local path: "C:\..." or "C:/...".
		if (InstallPath.Len() < 3 || !FChar::IsAlpha(InstallPath[0]) || InstallPath[1] != TEXT(':')
			|| (InstallPath[2] != TEXT('\\') && InstallPath[2] != TEXT('/')))
		{
			return false;
		}

		for (const TCHAR Character : InstallPath)
		{
			if (Character < 32 || FCString::Strchr(TEXT("\"'&|<>^%!`*?"), Character) != nullptr)
			{
				return false;
			}
		}
		return true;
	}

	// Full path to an in-box System32 binary (robocopy.exe / cmd.exe).
	static FString System32Exe(const TCHAR* ExeName)
	{
		FString WinDir = FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot"));
		if (WinDir.IsEmpty())
		{
			WinDir = TEXT("C:\\Windows");
		}
		return (WinDir + TEXT("\\System32\\") + ExeName).Replace(TEXT("/"), TEXT("\\"));
	}

	// robocopy takes a source DIR + dest DIR + filename filter, not a file path.
	// /R:1 /W:1 overrides the absurd defaults (1,000,000 retries / 30s wait) that
	// would otherwise hang ~forever on a locked DLL.
	static FString RobocopyArgs(const FTarget& T)
	{
		return FString::Printf(TEXT("\"%s\" \"%s\" \"%s\" /R:1 /W:1"), *T.SourceDir, *T.PluginsDir, *T.DllName);
	}

	// Resolve every installed Daz version that we have a matching build for.
	static TArray<FTarget> BuildTargets()
	{
		TArray<FTarget> Targets;

		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DazToUnreal"));
		if (!Plugin.IsValid())
		{
			return Targets;
		}

		FString ResourcesDir = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Resources"));
		ResourcesDir = ResourcesDir.Replace(TEXT("/"), TEXT("\\"));
		while (ResourcesDir.EndsWith(TEXT("\\")))
		{
			ResourcesDir = ResourcesDir.LeftChop(1);
		}

		for (const FKnownVersion& Known : KnownVersions)
		{
			// Read unelevated: these keys live under HKCU and an elevated process
			// could resolve them against the admin account's hive instead.
			FString InstallPath;
			if (!FWindowsPlatformMisc::QueryRegKey(HKEY_CURRENT_USER, Known.RegSubKey, TEXT("InstallPath-64"), InstallPath) || InstallPath.IsEmpty())
			{
				continue; // this version isn't installed
			}

			// Never hand an unvetted registry value to the elevated copy step.
			if (!IsSafeInstallPath(InstallPath))
			{
				UE_LOG(LogTemp, Warning,
					TEXT("DazToUnreal: ignoring %s - its registry install path is not a plain absolute path: %s"),
					Known.Label, *InstallPath);
				continue;
			}

			const FString SourcePath = ResourcesDir + TEXT("\\") + Known.DllName;
			if (!FPaths::FileExists(SourcePath))
			{
				continue; // no matching build shipped for this version
			}

			FTarget Target;
			Target.VersionLabel = Known.Label;
			Target.DllName = Known.DllName;
			Target.SourceDir = ResourcesDir;
			Target.SourcePath = SourcePath;
			Target.PluginsDir = InstallPathToPluginsDir(InstallPath);
			Target.DestPath = Target.PluginsDir + TEXT("\\") + Known.DllName;
			Targets.Add(Target);
		}

		return Targets;
	}

	static EDazCopyResult TryCopyUnelevated(const FTarget& Target)
	{
		if (CopyFileW(*Target.SourcePath, *Target.DestPath, FALSE))
		{
			return EDazCopyResult::Done;
		}

		switch (GetLastError())
		{
		case ERROR_ACCESS_DENIED:
			return EDazCopyResult::NeedsElevation;
		case ERROR_SHARING_VIOLATION: // existing DLL is loaded by a running Daz Studio
			return EDazCopyResult::DazRunning;
		default:
			return EDazCopyResult::Failed;
		}
	}

	// Truth source for whether a target ended up installed: the chained exit code
	// of robocopy/cmd is unreliable, so confirm the destination matches the source.
	static bool VerifyInstalled(const FTarget& Target)
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		const int64 SrcSize = PlatformFile.FileSize(*Target.SourcePath);
		const int64 DstSize = PlatformFile.FileSize(*Target.DestPath);
		return SrcSize >= 0 && DstSize == SrcSize;
	}
}

void FDazToUnrealUtils::InstallDazStudioPlugin()
{
	const TArray<FTarget> Targets = BuildTargets();
	if (Targets.Num() == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("DazInstall_NoTargets",
			"No installed Daz Studio version with a matching plugin build was found.\n\n"
			"Install Daz Studio first, then run this command again."));
		return;
	}

	TArray<FTarget> NeedElevation;
	TArray<FTarget> DazRunning;
	TArray<FTarget> Failed;

	for (const FTarget& Target : Targets)
	{
		switch (TryCopyUnelevated(Target))
		{
		case EDazCopyResult::Done:				break; // verified below
		case EDazCopyResult::NeedsElevation:	NeedElevation.Add(Target); break;
		case EDazCopyResult::DazRunning:		DazRunning.Add(Target); break;
		case EDazCopyResult::Failed:			Failed.Add(Target); break;
		}
	}

	// Elevate only the write-protected targets. ExecElevatedProcess runs the
	// binary with the "runas" verb, waits for it, and returns false if the
	// launch was declined or failed.
	//
	// Each target is copied by invoking robocopy.exe directly. An earlier version
	// chained several copies through "cmd.exe /c a & b" to keep a single UAC
	// prompt, but that put a registry-derived path through a command
	// interpreter. Launching robocopy per target means no shell parses these
	// arguments at all, at the cost of one prompt per Daz version that needs
	// elevation (only when several are installed into protected folders).
	bool bElevationLaunched = false;
	const FString RobocopyPath = System32Exe(TEXT("robocopy.exe"));
	for (const FTarget& Target : NeedElevation)
	{
		int32 ReturnCode = 0;
		if (FPlatformProcess::ExecElevatedProcess(*RobocopyPath, *RobocopyArgs(Target), &ReturnCode))
		{
			bElevationLaunched = true;
		}
		else
		{
			// Declined or failed to launch: further prompts would just repeat it.
			break;
		}
	}

	// Build the result report from re-verified destinations, not exit codes.
	TArray<FString> Installed;
	TArray<FString> NotInstalled;

	for (const FTarget& Target : Targets)
	{
		if (VerifyInstalled(Target))
		{
			Installed.Add(Target.VersionLabel);
		}
		else
		{
			FString Reason;
			if (DazRunning.Contains(Target))
			{
				Reason = FString::Printf(TEXT("%s is running - close it and try again"), *Target.VersionLabel);
			}
			else if (!bElevationLaunched && NeedElevation.Contains(Target))
			{
				Reason = FString::Printf(TEXT("administrator permission was declined - copy %s into %s manually"), *Target.DllName, *Target.PluginsDir);
			}
			else
			{
				Reason = FString::Printf(TEXT("could not write to %s - copy %s there manually"), *Target.PluginsDir, *Target.DllName);
			}
			NotInstalled.Add(FString::Printf(TEXT("%s: %s"), *Target.VersionLabel, *Reason));
		}
	}

	FString Message;
	if (Installed.Num() > 0)
	{
		Message += FString::Printf(TEXT("Installed for: %s\n"), *FString::Join(Installed, TEXT(", ")));
	}
	if (NotInstalled.Num() > 0)
	{
		if (!Message.IsEmpty())
		{
			Message += TEXT("\n");
		}
		Message += TEXT("Not installed:\n");
		for (const FString& Line : NotInstalled)
		{
			Message += FString::Printf(TEXT("  - %s\n"), *Line);
		}
	}

	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
}

#include "Windows/HideWindowsPlatformTypes.h"

#undef LOCTEXT_NAMESPACE

#else // !PLATFORM_WINDOWS

void FDazToUnrealUtils::InstallDazStudioPlugin()
{
	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(
		TEXT("Installing the Daz Studio plugin from here is only supported on Windows.")));
}

#endif // PLATFORM_WINDOWS