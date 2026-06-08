#pragma once

#include "CoreMinimal.h"
#include "DazToUnrealEnums.h"

class FJsonObject;

class FDazToUnrealEnvironment
{
public:
	static void ImportEnvironment(TSharedPtr<FJsonObject> JsonObject);

private:
	
};