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

enum DazMaterialCombineType
{
	NoCombine,
	CombineIdentical,
	CombineAll
};

// Body-rig target selected at import time. Mutually exclusive: a character is
// converted to the Epic skeleton, to a MetaHuman, or left on its native Daz
// skeleton. Replaces the meaning of the legacy bConvertToEpicSkeleton bool over
// time (which is kept wired for back-compat with older Daz Studio exports).
enum class EDazSkeletonTarget
{
	None,
	EpicSkeleton,
	MetaHuman
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
	bool bConvertToEpicSkeleton = false;
	// Body-rig target. Resolved from the .dtu "SkeletonTarget" field when present,
	// otherwise inferred from bConvertToEpicSkeleton for back-compat.
	EDazSkeletonTarget SkeletonTarget = EDazSkeletonTarget::None;
	bool bFixTwistBones = false;
	bool bFaceCharacterRight = false;
	TMap<FName, FName> MaterialSlotNameToMaterialName;
};