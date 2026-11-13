#pragma once

#include "CoreMinimal.h"
#include "EpicodeSandboxWorld.h"

class EPICODESANDBOX_API FEpicodeSandboxPersistence
{
public:
	static bool Save(const FEpicodeSandboxWorld& InWorld, const FEpicodeSandboxEventStream& InEvents, const FString& InSessionRoot, FString& OutError);
	static bool Load(const FString& InSessionRoot, FEpicodeSandboxWorld& OutWorld, FEpicodeSandboxEventStream& OutEvents, FString& OutError);
	static FString GetSnapshotPath(const FString& InSessionRoot);
};
