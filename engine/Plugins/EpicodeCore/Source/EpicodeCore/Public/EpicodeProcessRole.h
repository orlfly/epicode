#pragma once

#include "CoreMinimal.h"

/** Resolves the externally visible role of an Epicode process. */
class EPICODECORE_API FEpicodeProcessRole
{
public:
	static FString Resolve(const FString& InCommandLine, bool bIsEditor);
	static bool IsKnown(const FString& InRole);
};
