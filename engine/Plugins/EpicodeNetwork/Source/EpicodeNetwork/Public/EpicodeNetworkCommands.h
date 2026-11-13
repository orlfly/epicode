#pragma once

#include "CoreMinimal.h"

class FEpicodeCommandRegistry;

class EPICODENETWORK_API FEpicodeNetworkCommands
{
public:
	static void Register(FEpicodeCommandRegistry& Registry);
};
