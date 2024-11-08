#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "DazToUnrealStyle.h"

class FDazToUnrealCommands : public TCommands<FDazToUnrealCommands>
{
public:

	FDazToUnrealCommands()
		: TCommands<FDazToUnrealCommands>(TEXT("DazToUnreal"), NSLOCTEXT("Contexts", "DazToUnreal", "DazToUnreal Plugin"), NAME_None, FDazToUnrealStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > OpenPluginWindow;
	TSharedPtr< FUICommandInfo > InstallDazStudioPlugin;
	TSharedPtr< FUICommandInfo > InstallSkeletonAssets;
	TSharedPtr< FUICommandInfo > InstallMaterialAssets;
	TSharedPtr< FUICommandInfo > CreateRetargeter;
	TSharedPtr< FUICommandInfo > ConvertToEpicSkeleton;
};