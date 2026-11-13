#pragma once

#include "CoreMinimal.h"

class FEpicodeCommandRegistry;

class FEpicodeSandboxCommands
{
public:
	static void Register(FEpicodeCommandRegistry& InRegistry);
};
