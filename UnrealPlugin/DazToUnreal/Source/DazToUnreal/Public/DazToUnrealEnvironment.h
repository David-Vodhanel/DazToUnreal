#pragma once

#include "CoreMinimal.h"

class FJsonObject;

class FDazToUnrealEnvironment
{
public:
	static void ImportEnvironment(TSharedPtr<FJsonObject> JsonObject);

private:
	
};