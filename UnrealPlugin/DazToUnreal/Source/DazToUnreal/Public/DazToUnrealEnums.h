#pragma once

enum DazCharacterType
{
	Genesis1,
	Genesis3Male,
	Genesis3Female,
	Genesis8Male,
	Genesis8Female,
	Unknown
};

enum DazAssetType
{
	SkeletalMesh,
	StaticMesh,
	Animation,
	Environment,
	Pose,
	MLDeformer
};

struct DazToUnrealImportData
{
	FString SourcePath;
	FString ImportLocation;
	DazAssetType AssetType;
	DazCharacterType CharacterType;
	FString CharacterTypeName;
	bool bSetPostProcessAnimation = true;
	bool bCreateUniqueSkeleton = false;
	bool bFixTwistBones = false;
	bool bFaceCharacterRight = false;
};