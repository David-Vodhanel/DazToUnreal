#include "DazToUnrealPoses.h"
#include "DazToUnrealSettings.h"
#include "DazToUnrealTextures.h"
#include "DazToUnrealUtils.h"

#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "Animation/PoseAsset.h"

#include "Factories/PoseAssetFactory.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "PackageTools.h"
#include "Misc/EngineVersionComparison.h"

// Partially taken from UPoseAssetFactory::FactoryCreateNew
UPoseAsset* FDazToUnrealPoses::CreatePoseAsset(UAnimSequence* SourceAnimation, TArray<FString> PoseNames)
{
	if (SourceAnimation)
	{
		const UDazToUnrealSettings* CachedSettings = GetDefault<UDazToUnrealSettings>();
		
		// Make sure the path exists
		FString ContentDirectory = FPaths::ProjectContentDir();
		FString DAZPoseImportFolder = CachedSettings->PoseImportDirectory.Path.Replace(TEXT("/Game/"), *ContentDirectory);
		if (!FDazToUnrealUtils::MakeDirectoryAndCheck(DAZPoseImportFolder))
		{
			//log error
		}

		USkeleton* TargetSkeleton = SourceAnimation->GetSkeleton();

#if UE_VERSION_OLDER_THAN(5,3,0)
		TArray<FSmartName> InputPoseNames;
		if (PoseNames.Num() > 0)
		{
			for (int32 Index = 0; Index < PoseNames.Num(); ++Index)
			{
				FName PoseName = FName(*PoseNames[Index]);
				FSmartName NewName;
				if (TargetSkeleton->GetSmartNameByName(USkeleton::AnimCurveMappingName, PoseName, NewName) == false)
				{
					// if failed, add it
					TargetSkeleton->AddSmartNameAndModify(USkeleton::AnimCurveMappingName, PoseName, NewName);
				}

				// we want same names in multiple places
				InputPoseNames.AddUnique(NewName);
			}
		}
#else
		TArray<FName> InputPoseNames;
		for (int32 Index = 0; Index < PoseNames.Num(); ++Index)
		{
			FName PoseName = FName(*PoseNames[Index]);
			InputPoseNames.AddUnique(PoseName);
		}
#endif

		FString PackageName = UPackageTools::SanitizePackageName(*(CachedSettings->PoseImportDirectory.Path / FString(SourceAnimation->GetName())));
#if ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION < 26
		UPackage* Pkg = CreatePackage(nullptr, *PackageName);
#else
		UPackage* Pkg = CreatePackage(*PackageName);
#endif
		EObjectFlags Flags = RF_Public | RF_Standalone | RF_Transactional;

		UPoseAsset* PoseAsset = NewObject<UPoseAsset>(Pkg, FName(*SourceAnimation->GetName()), Flags);
		//PoseAsset->bAdditivePose = true;
		//PoseAsset->BasePoseIndex = 0;
		PoseAsset->CreatePoseFromAnimation(SourceAnimation, &InputPoseNames);
		PoseAsset->SetSkeleton(TargetSkeleton);
		PoseAsset->ConvertSpace(true, 0);

		return PoseAsset;
	}
	return nullptr;
	//return NewPose;

	//UPoseAssetFactory* Factory = NewObject<UPoseAssetFactory>();
	//Factory->TargetSkeleton = Skeleton;
	//Factory->PreviewSkeletalMesh = SkeletalMesh;

	//FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	//ContentBrowserModule.Get().CreateNewAsset(Name, FPackageName::GetLongPackagePath(PackageName), T::StaticClass(), Factory);

	/*UPoseAsset* PoseAsset = NewObject<UPoseAsset>(InParent, Class, Name, Flags);
	PoseAsset->CreatePoseFromAnimation(SourceAnimation, &InputPoseNames);
	PoseAsset->SetSkeleton(TargetSkeleton);*/
}