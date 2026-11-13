#pragma once

#include "CoreMinimal.h"

class FEpicodeCommandRegistry;

class EPICODEIMPORT_API FEpicodeImportCommands
{
public:
	static void Register(FEpicodeCommandRegistry& Registry);
};
