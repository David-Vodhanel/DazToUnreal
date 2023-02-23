#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Common/UdpSocketBuilder.h"
#include "Containers/Ticker.h"
#include "Widgets/SWidget.h"
#include "Framework/Commands/UICommandList.h"

#include "DazToUnrealEnums.h"

DECLARE_LOG_CATEGORY_EXTERN(LogDazToUnreal, Log, All);

class FToolBarBuilder;
class FMenuBuilder;
struct FDUFTextureProperty;
//class FbxNode;


enum DazAssetType
{
	SkeletalMesh,
	StaticMesh,
	Animation,
	Environment,
	Pose,
	MLDeformer
};

struct TextureLookupInfo
{
	FString sSourceFullPath;
	bool bIsCutOut;
};

class FDazToUnrealModule : public IModuleInterface
{
public:
	static int BatchConversionMode;
	static FString BatchConversionDestPath;
	static TMap<FString, FString> AssetIDLookup;
	TMap<FString, TextureLookupInfo> m_sourceTextureLookupTable;
	TMap<FString, TextureLookupInfo> m_targetTextureLookupTable;

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
	static inline FDazToUnrealModule& Get()
	{
		return FModuleManager::LoadModuleChecked< FDazToUnrealModule >("DazToUnreal");
	}

	/** This function will be bound to Command (by default it will bring up plugin window) */
	//void PluginButtonClicked();

	//** Runs the installer to install the Daz Studio plugin*/
	void InstallDazStudioPlugin();

	//** Copies the skeleton assets and redirects any references to them*/
	void InstallSkeletonAssetsToProject();

	//** Copies the material assets and redirects and references to them*/
	void InstallMaterialAssetsToProject();
	
	/** Function to start the import process*/
	UObject* ImportFromDaz(TSharedPtr<FJsonObject> JsonObject, const FString& FileName);

private:
	
	void AddToolbarExtension(FToolBarBuilder& Builder);
	void AddMenuExtension(FMenuBuilder& Builder);
	TSharedRef<SWidget> MakeDazToUnrealToolbarMenu(TSharedPtr<FUICommandList> DazToUnrealCommandList);

	//TSharedRef<class SDockTab> OnSpawnPluginTab(const class FSpawnTabArgs& SpawnTabArgs);

private:
	TSharedPtr<class FUICommandList> PluginCommands;

	// Network listener functions
private:
	/** Starts the UDP listener for receiving messages from Daz3D*/
	void StartupUDPListener();

	/** Stops the listener*/
	void ShutdownUDPListener();

	/** Socket for the listener*/
	FSocket* ServerSocket = nullptr;

	/** Delegate for the tick function.  Incomming messages are checked on tick*/
	FTickerDelegate TickDelegate;

	/** DelegateHandle for the tick function*/
#if ENGINE_MAJOR_VERSION > 4
	FTSTicker::FDelegateHandle TickDelegateHandle;
#else
	FDelegateHandle TickDelegateHandle;
#endif

	/** Tick function for handling incomming messages from Daz3D*/
	bool Tick(float DeltaTime);

	//void GetWeights(FbxNode *SceneNode, TMap<int, TArray<int>>& VertexPolygons, TMap<int, double>& ClusterWeights, FVector TargetPosition, int SearchFromVertex, TArray<int>& TouchedPolygons, TArray<int>& TouchedVertices, double& WeightsOut, double& DistancesOut, int32 Depth);

private:

	/** Imports the textures for the model*/
	bool ImportTextureAssets(TArray<FString>& SourcePaths, FString& ImportLocation);

	/** Imports the modified FBX file*/
	UObject* ImportFBXAsset(const FString& SourcePath, const FString& ImportLocation, const DazAssetType& AssetType, const DazCharacterType& CharacterType, const FString& CharacterTypeName, const bool bSetPostProcessAnimation);

	/** Function for creating the Material Instances for the model*/
	//bool CreateMaterials(const FString CharacterMaterialFolder, const FString CharacterTexturesFolder, const TArray<FString>& MaterialNames, TMap<FString, TArray<FDUFTextureProperty>> MaterialProperties, const DazCharacterType CharacterType);

	/** Set material properties that will be set on the Material Instances*/
	void SetMaterialProperty(const FString& MaterialName, const FString& PropertyName, const FString& PropertyType, const FString& PropertyValue, TMap<FString, TArray<FDUFTextureProperty>>& MaterialProperties);
	FString GetSubSurfaceAlphaTexture(const DazCharacterType CharacterType, const FString& MaterialName);

	/** Converts a hex string to a Linear color.*/
	FLinearColor FromHex(const FString& HexString);
};