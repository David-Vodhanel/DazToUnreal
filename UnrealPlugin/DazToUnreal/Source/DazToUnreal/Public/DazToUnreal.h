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

struct TextureLookupInfo
{
	FString sSourceFullPath;
	bool bIsCutOut;
};


class FDazToUnrealModule : public IModuleInterface//, TSharedFromThis<FDazToUnrealModule>
{
public:
	static int BatchConversionMode;
	static FString BatchConversionDestPath;
	static TMap<FString, FString> AssetIDLookup;
	static TArray<UObject*> TextureListToDisableSRGB;
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

	// Section for IKRetargeter creation menus
private:

	// Create the menu for ik retargeter creation
	void AddCreateRetargeterMenu();

	// Create the per skeletal mesh menu
	void AddCreateRetargeterSubMenu(UToolMenu* Menu);

	// Create an IK Retargeter and IKRigs if needed for retargeting between two skeletal meshes
	void OnCreateRetargeterClicked(FSoftObjectPath SourceObjectPath, class USkeletalMesh* TargetSkeletalMesh);
	
	// Find a control rig for the skeletal mesh
	class UIKRigDefinition* FindIKRigForSkeletalMesh(class USkeletalMesh* SkeletalMesh);

private:

	// Create the menu for generating a Full Body IK Control Rig
	void AddCreateFullBodyIKControlRigMenu();

	// Create the Full Body IK Control Rig
	void OnCreateFullBodyIKControlRigClicked(FSoftObjectPath SourceObjectPath);

	// Create the menu for generating an IK based Control Rig
	void AddCreateIKLimbBasedControlRigMenu();

	// Create the Full Body IK Control Rig
	void OnCreateIKLimbBasedControlRigClicked(FSoftObjectPath SourceObjectPath);

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
	UObject* ImportFBXAsset(const DazToUnrealImportData& DazImportData);

	/** Function for creating the Material Instances for the model*/
	//bool CreateMaterials(const FString CharacterMaterialFolder, const FString CharacterTexturesFolder, const TArray<FString>& MaterialNames, TMap<FString, TArray<FDUFTextureProperty>> MaterialProperties, const DazCharacterType CharacterType);

	/** Set material properties that will be set on the Material Instances*/
	void SetMaterialProperty(const FString& MaterialName, const FString& PropertyName, const FString& PropertyType, const FString& PropertyValue, TMap<FString, TArray<FDUFTextureProperty>>& MaterialProperties);
	FString GetSubSurfaceAlphaTexture(const DazCharacterType CharacterType, const FString& MaterialName);

	/** Converts a hex string to a Linear color.*/
	FLinearColor FromHex(const FString& HexString);
};