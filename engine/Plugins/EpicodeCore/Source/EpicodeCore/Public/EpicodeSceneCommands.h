#pragma once

#include "CoreMinimal.h"

class FEpicodeCommandRegistry;

class EPICODECORE_API FEpicodeSceneCommands
{
public:
	static void Register(FEpicodeCommandRegistry& Registry);
};
