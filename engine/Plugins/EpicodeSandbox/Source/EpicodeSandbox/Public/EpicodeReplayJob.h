#pragma once

#include "CoreMinimal.h"
#include "EpicodeReplayEngine.h"

enum class EEpicodeReplayJobState : uint8
{
	Queued,
	Running,
	Completed,
	Failed,
	Cancelled
};

struct EPICODESANDBOX_API FEpicodeReplayJobStatus
{
	FString Id;
	FString Root;
	FString RequestedBy;
	int64 TargetSequence = 0;
	EEpicodeReplayJobState State = EEpicodeReplayJobState::Queued;
	int32 AppliedEvents = 0;
	FString Error;
	FString Digest;
	TArray<FEpicodeReplayDiff> Diffs;

	TSharedPtr<FJsonObject> ToJson() const;
};

class EPICODESANDBOX_API FEpicodeReplayJobQueue
{
public:
	void Initialize(int32 InMaxConcurrent = 1);
	FString Submit(
		const FString& InRoot,
		int64 InTargetSequence,
		const FEpicodeReplayResimOptions& InOptions,
		const FString& InRequestedBy,
		EEpicodeSandboxParticipantKind InActorKind,
		FString& OutError);
	int32 StartQueued(FString& OutError);
	bool Step(const FString& InJobId, int32 InMaxEvents, FString& OutError);
	bool RunToCompletion(const FString& InJobId, FString& OutError);
	bool Cancel(const FString& InJobId, FString& OutError);
	void TickLive(double InDeltaSeconds) { LiveSeconds += FMath::Max(0.0, InDeltaSeconds); ++LiveTicks; }

	const TArray<FEpicodeReplayJobStatus>& GetJobs() const { return Jobs; }
	const FEpicodeReplayJobStatus* FindJob(const FString& InJobId) const;
	int32 GetRunningCount() const;
	double GetLiveSeconds() const { return LiveSeconds; }
	int32 GetLiveTicks() const { return LiveTicks; }
	int32 GetMaxConcurrent() const { return MaxConcurrent; }

private:
	FEpicodeReplayJobStatus* FindMutableJob(const FString& InJobId);
	void UpdateFromRunner(FEpicodeReplayJobStatus& InOutJob) const;

	TArray<FEpicodeReplayJobStatus> Jobs;
	TMap<FString, TUniquePtr<FEpicodeReplayResimRunner>> Runners;
	int32 MaxConcurrent = 1;
	double LiveSeconds = 0.0;
	int32 LiveTicks = 0;
};
