#include "DazToUnrealUtils.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/Paths.h"
#include "UObject/SoftObjectPath.h"
#include "AssetRegistry/AssetRegistryModule.h"

#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"

#include "Engine/StaticMesh.h"
#include "EditorFramework/AssetImportData.h"
#include "Factories/FbxAssetImportData.h"

#include "DazToUnrealSettings.h"

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
		.Replace(TEXT(":"), TEXT("_"))
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
		if (UAssetImportData* AssetImportData = StaticMesh->AssetImportData)
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