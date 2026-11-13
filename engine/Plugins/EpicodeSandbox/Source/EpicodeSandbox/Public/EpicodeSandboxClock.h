#pragma once

#include "CoreMinimal.h"
#include "EpicodeSandboxTypes.h"

class EPICODESANDBOX_API FEpicodeSandboxSimulationClock
{
public:
	void Configure(int32 InTickRate, int64 InStartTick = 0);
	void Reset(int64 InStartTick = 0);

	int64 Enqueue(EEpicodeSandboxCommandCategory InCategory, const TSharedPtr<FJsonObject>& InPayload = nullptr, int64 InTick = 0);
	int32 Advance(double InDeltaSeconds, int32 InMaxTicks, TFunctionRef<void(int64, const TArray<FEpicodeSandboxQueuedCommand>&)> InOnTick);
	int32 Advance(double InDeltaSeconds, int32 InMaxTicks = 8);

	int32 GetTickRate() const { return TickRate; }
	double GetFixedDeltaSeconds() const { return FixedDeltaSeconds; }
	int64 GetCurrentTick() const { return CurrentTick; }
	int32 GetPendingCount() const { return Pending.Num(); }
	int32 GetProcessedCount() const { return ProcessedCount; }
	const TArray<FEpicodeSandboxQueuedCommand>& GetLastTickCommands() const { return LastTickCommands; }
	TArray<FEpicodeSandboxQueuedCommand> PeekPendingSorted() const;

private:
	void ProcessDueCommands(int64 InTick);

	int32 TickRate = 30;
	double FixedDeltaSeconds = 1.0 / 30.0;
	double Accumulator = 0.0;
	int64 CurrentTick = 0;
	int64 NextCommandSequence = 1;
	int32 ProcessedCount = 0;
	TArray<FEpicodeSandboxQueuedCommand> Pending;
	TArray<FEpicodeSandboxQueuedCommand> LastTickCommands;
};
