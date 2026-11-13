#include "EpicodeSandboxClock.h"

#include "Dom/JsonValue.h"

void FEpicodeSandboxSimulationClock::Configure(int32 InTickRate, int64 InStartTick)
{
	TickRate = FMath::Clamp(InTickRate, 1, 240);
	FixedDeltaSeconds = 1.0 / static_cast<double>(TickRate);
	Reset(InStartTick);
}

void FEpicodeSandboxSimulationClock::Reset(int64 InStartTick)
{
	Accumulator = 0.0;
	CurrentTick = FMath::Max<int64>(0, InStartTick);
	NextCommandSequence = 1;
	ProcessedCount = 0;
	Pending.Reset();
	LastTickCommands.Reset();
}

int64 FEpicodeSandboxSimulationClock::Enqueue(EEpicodeSandboxCommandCategory InCategory, const TSharedPtr<FJsonObject>& InPayload, int64 InTick)
{
	FEpicodeSandboxQueuedCommand& Command = Pending.AddDefaulted_GetRef();
	Command.Sequence = NextCommandSequence++;
	Command.Tick = InTick > 0 ? InTick : CurrentTick + 1;
	Command.Category = InCategory;
	Command.Payload = InPayload.IsValid() ? InPayload : MakeShared<FJsonObject>();
	return Command.Sequence;
}

int32 FEpicodeSandboxSimulationClock::Advance(double InDeltaSeconds, int32 InMaxTicks, TFunctionRef<void(int64, const TArray<FEpicodeSandboxQueuedCommand>&)> InOnTick)
{
	if (InDeltaSeconds <= 0.0 || InMaxTicks <= 0)
	{
		return 0;
	}
	Accumulator += InDeltaSeconds;
	int32 Ticks = 0;
	while (Accumulator + KINDA_SMALL_NUMBER >= FixedDeltaSeconds && Ticks < InMaxTicks)
	{
		Accumulator -= FixedDeltaSeconds;
		if (Accumulator < 0.0)
		{
			Accumulator = 0.0;
		}
		++CurrentTick;
		ProcessDueCommands(CurrentTick);
		++ProcessedCount;
		++Ticks;
		InOnTick(CurrentTick, LastTickCommands);
	}
	return Ticks;
}

int32 FEpicodeSandboxSimulationClock::Advance(double InDeltaSeconds, int32 InMaxTicks)
{
	return Advance(InDeltaSeconds, InMaxTicks, [](int64, const TArray<FEpicodeSandboxQueuedCommand>&) {});
}

TArray<FEpicodeSandboxQueuedCommand> FEpicodeSandboxSimulationClock::PeekPendingSorted() const
{
	TArray<FEpicodeSandboxQueuedCommand> Sorted = Pending;
	Sorted.Sort([](const FEpicodeSandboxQueuedCommand& A, const FEpicodeSandboxQueuedCommand& B)
	{
		if (A.Tick != B.Tick)
		{
			return A.Tick < B.Tick;
		}
		const int32 OrderA = GetSandboxCommandSortOrder(A.Category);
		const int32 OrderB = GetSandboxCommandSortOrder(B.Category);
		if (OrderA != OrderB)
		{
			return OrderA < OrderB;
		}
		return A.Sequence < B.Sequence;
	});
	return Sorted;
}

void FEpicodeSandboxSimulationClock::ProcessDueCommands(int64 InTick)
{
	TArray<FEpicodeSandboxQueuedCommand> Due;
	TArray<FEpicodeSandboxQueuedCommand> Remaining;
	for (const FEpicodeSandboxQueuedCommand& Command : Pending)
	{
		if (Command.Tick <= InTick)
		{
			Due.Add(Command);
		}
		else
		{
			Remaining.Add(Command);
		}
	}
	Due.Sort([](const FEpicodeSandboxQueuedCommand& A, const FEpicodeSandboxQueuedCommand& B)
	{
		const int32 OrderA = GetSandboxCommandSortOrder(A.Category);
		const int32 OrderB = GetSandboxCommandSortOrder(B.Category);
		if (OrderA != OrderB)
		{
			return OrderA < OrderB;
		}
		return A.Sequence < B.Sequence;
	});
	LastTickCommands = MoveTemp(Due);
	Pending = MoveTemp(Remaining);
}
