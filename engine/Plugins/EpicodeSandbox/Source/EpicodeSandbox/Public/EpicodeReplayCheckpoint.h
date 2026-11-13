#pragma once

#include "CoreMinimal.h"
#include "EpicodeSandboxRandom.h"
#include "EpicodeSandboxWorld.h"

struct EPICODESANDBOX_API FEpicodeReplayCheckpoint
{
	int32 SchemaVersion = 1;
	int64 Sequence = 0;
	int64 Tick = 0;
	int32 TickRate = 30;
	int32 Seed = 0;
	FString ContentDigest;
	FEpicodeSandboxWorld World;
	FEpicodeSandboxRandomStreams Random;
	FString Checksum;

	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeReplayCheckpoint& OutCheckpoint, FString& OutError);
	FString ComputeChecksum() const;
};

class EPICODESANDBOX_API FEpicodeReplayCheckpoints
{
public:
	static bool Save(const FString& InRoot, const FEpicodeReplayCheckpoint& InCheckpoint, FString& OutError);
	static bool LoadNearest(const FString& InRoot, int64 InTargetSequence, FEpicodeReplayCheckpoint& OutCheckpoint, FString& OutError);
	static bool LoadAt(const FString& InRoot, int64 InSequence, FEpicodeReplayCheckpoint& OutCheckpoint, FString& OutError);
	static TArray<int64> List(const FString& InRoot);
	static FString GetCheckpointDirectory(const FString& InRoot);
	static FString GetCheckpointPath(const FString& InRoot, int64 InSequence);
};
