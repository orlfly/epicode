#pragma once

#include "CoreMinimal.h"
#include "EpicodeReplayEngine.h"

struct EPICODESANDBOX_API FEpicodeReplayRetentionPolicy
{
	int32 RetentionHours = 24;
	int64 MaxEvents = 0;
};

class EPICODESANDBOX_API FEpicodeReplayRetention
{
public:
	static bool Compact(
		const FString& InRoot,
		int64 InKeepFromSequence,
		EEpicodeSandboxParticipantKind InActorKind,
		FString& OutError);
	static bool ApplyPolicy(
		const FString& InRoot,
		const FEpicodeReplayRetentionPolicy& InPolicy,
		EEpicodeSandboxParticipantKind InActorKind,
		FString& OutError);
	static FString GetArchivePath(const FString& InRoot);
};
