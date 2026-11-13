#pragma once

#include "CoreMinimal.h"
#include "EpicodeReplayCheckpoint.h"
#include "EpicodeReplayJournal.h"
#include "EpicodeSandboxTypes.h"

enum class EEpicodeReplayStepResult : uint8
{
	Running,
	Completed,
	Failed,
	Cancelled
};

struct EPICODESANDBOX_API FEpicodeReplayDiff
{
	int64 Sequence = 0;
	FString EntityId;
	FString Field;
	FString Expected;
	FString Actual;

	TSharedPtr<FJsonObject> ToJson() const;
};

struct EPICODESANDBOX_API FEpicodeReplayResimOptions
{
	FString ExpectedContentDigest;
	bool bAllowInexact = false;
};

struct EPICODESANDBOX_API FEpicodeReplayResimResult
{
	bool bSuccess = false;
	bool bExact = false;
	bool bCancelled = false;
	bool bInexact = false;
	int64 TargetSequence = 0;
	int64 AppliedEvents = 0;
	FString Digest;
	FString Error;
	TArray<FEpicodeReplayDiff> Diffs;
	FEpicodeSandboxWorld World;
	FEpicodeSandboxRandomStreams Random;

	TSharedPtr<FJsonObject> ToJson() const;
};

class EPICODESANDBOX_API FEpicodeReplayState
{
public:
	static bool ApplyEvent(FEpicodeSandboxWorld& InOutWorld, const FEpicodeSandboxEvent& InEvent, FString& OutError);
	static void ComputeDiff(const FEpicodeSandboxWorld& InExpected, const FEpicodeSandboxWorld& InActual, int64 InSequence, TArray<FEpicodeReplayDiff>& OutDiffs);
	static bool CanReplay(EEpicodeSandboxParticipantKind InKind, FString& OutError);
};

class EPICODESANDBOX_API FEpicodeReplayResimRunner
{
public:
	bool Begin(const FString& InRoot, int64 InTargetSequence, const FEpicodeReplayResimOptions& InOptions, FString& OutError);
	EEpicodeReplayStepResult Step(int32 InMaxEvents, FString& OutError);
	void Cancel() { bCancelRequested = true; }
	bool IsRunning() const { return bBegun && !bFinished; }
	const FEpicodeReplayResimResult& GetResult() const { return Result; }
	const FEpicodeSandboxWorld& GetWorld() const { return World; }
	const FEpicodeSandboxRandomStreams& GetRandom() const { return Random; }
	int64 GetTargetSequence() const { return TargetSequence; }
	int64 GetNextSequence() const { return NextIndex < Journal.GetEntries().Num() ? Journal.GetEntries()[NextIndex].Event.Sequence : TargetSequence + 1; }

private:
	EEpicodeReplayStepResult Complete(FString& OutError);
	EEpicodeReplayStepResult Fail(const FString& InError, FString& OutError);

	FEpicodeReplayJournal Journal;
	FEpicodeReplayCheckpoint Checkpoint;
	FString Root;
	FEpicodeSandboxWorld World;
	FEpicodeSandboxRandomStreams Random;
	FEpicodeReplayResimOptions Options;
	FEpicodeReplayResimResult Result;
	int64 TargetSequence = 0;
	int32 NextIndex = 0;
	bool bBegun = false;
	bool bFinished = false;
	bool bCancelRequested = false;
	bool bVersionMismatch = false;
};

class EPICODESANDBOX_API FEpicodeReplayResim
{
public:
	static bool Run(const FString& InRoot, int64 InTargetSequence, const FEpicodeReplayResimOptions& InOptions, FEpicodeReplayResimResult& OutResult, FString& OutError);
};

struct EPICODESANDBOX_API FEpicodeReplayGhostPage
{
	int64 StartSequence = 0;
	int64 TargetSequence = 0;
	TArray<FEpicodeReplayJournalEntry> Entries;
};

class EPICODESANDBOX_API FEpicodeReplayGhost
{
public:
	bool Open(const FString& InRoot, FString& OutError);
	bool GetPage(int64 InStartSequence, int32 InCount, FEpicodeReplayGhostPage& OutPage, FString& OutError) const;
	bool Seek(int64 InSequence, FEpicodeReplayResimResult& OutResult, FString& OutError) const;
	bool IsReadOnly() const { return true; }
	int64 GetFirstSequence() const { return Journal.GetFirstSequence(); }
	int64 GetLastSequence() const { return Journal.GetLastSequence(); }
	const FString& GetRoot() const { return Root; }

private:
	FString Root;
	FEpicodeReplayJournal Journal;
};

struct EPICODESANDBOX_API FEpicodeReplayBranchInfo
{
	FString BranchId;
	FString ParentSessionId;
	int64 ForkSequence = 0;
	FString CreatedBy;
	FString ContentDigest;
	FString Root;
	FString Checksum;

	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeReplayBranchInfo& OutInfo, FString& OutError);
	FString ComputeChecksum() const;
};

class EPICODESANDBOX_API FEpicodeReplayBranch
{
public:
	static bool Create(
		const FString& InParentRoot,
		const FString& InBranchRoot,
		const FString& InBranchId,
		int64 InForkSequence,
		const FString& InCreatedBy,
		EEpicodeSandboxParticipantKind InActorKind,
		FString& OutError);
	static FString GetBranchMetadataPath(const FString& InRoot);
};
