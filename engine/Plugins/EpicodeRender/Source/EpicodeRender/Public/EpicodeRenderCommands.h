#pragma once

#include "CoreMinimal.h"

class FEpicodeCommandRegistry;

class EPICODERENDER_API FEpicodeRenderCommands
{
public:
	static void Register(FEpicodeCommandRegistry& Registry);
};
