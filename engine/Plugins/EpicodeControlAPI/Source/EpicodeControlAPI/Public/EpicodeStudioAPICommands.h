#pragma once

#include "CoreMinimal.h"

class FEpicodeCommandRegistry;

class EPICODECONTROLAPI_API FEpicodeStudioAPICommands
{
public:
	static void Register(FEpicodeCommandRegistry& Registry);
};
