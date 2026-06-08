#pragma once

#include "CoreMinimal.h"
#include "DazToUnrealEnums.h"

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION > 0

#endif

class FJsonObject;
class UMLDeformerAsset;
class UGeometryCache;

DECLARE_LOG_CATEGORY_EXTERN(LogDazToUnrealMLDeformer, Log, All);

struct FDazToUnrealMLDeformerParams
{
	TSharedPtr<FJsonObject> JsonImportData;
	FString AssetName;
	UAnimSequence* AnimationAsset;
	UObject* OutAsset = nullptr;
	DazToUnrealImportData ImportData;
	UGeometryCache* GeometryCacheAsset;
};

class FDazToUnrealMLDeformer
{
public:
	static void ImportMLDeformerAssets(FDazToUnrealMLDeformerParams& DazToUnrealMLDeformerParams);
	static void CreateMLDeformer(FDazToUnrealMLDeformerParams& DazToUnrealMLDeformerParams);
	static void ModelPropertyChange(FPropertyChangedEvent& PropertyChangeEvent, UMLDeformerAsset* DeformerAsset);
};