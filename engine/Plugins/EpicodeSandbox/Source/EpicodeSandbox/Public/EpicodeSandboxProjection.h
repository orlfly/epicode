#pragma once

#include "CoreMinimal.h"
#include "EpicodeSandboxTypes.h"

struct FEpicodeSandboxWorld;

class EPICODESANDBOX_API FEpicodeSandboxProjection
{
public:
	static bool IsProjectable(EEpicodeSandboxEntityKind InKind);
	static TArray<FEpicodeSandboxProjectionCommand> Build(const FEpicodeSandboxWorld& InWorld);
};
