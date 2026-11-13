#include "EpicodeReplayEngine.h"

#include "EpicodeSandboxJson.h"
#include "EpicodeSandboxPersistence.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	bool ReadVector(const TSharedPtr<FJsonObject>& InObject, const FString& InField, FVector& OutVector)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!InObject->TryGetArrayField(InField, Values) || !Values || Values->Num() != 3)
		{
			return false;
		}
		OutVector = FVector((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
		return true;
	}

	TSharedPtr<FJsonObject> EnsureData(FEpicodeSandboxEntity& InEntity)
	{
		if (!InEntity.Data.IsValid())
		{
			InEntity.Data = MakeShared<FJsonObject>();
		}
		return InEntity.Data;
	}

	bool ValueDiffers(const TSharedPtr<FJsonValue>& InExpected, const TSharedPtr<FJsonValue>& InActual)
	{
		return FEpicodeSandboxJson::CanonicalString(InExpected) != FEpicodeSandboxJson::CanonicalString(InActual);
	}

	bool FindObjectDiff(
		const TSharedPtr<FJsonValue>& InExpected,
		const TSharedPtr<FJsonValue>& InActual,
		const FString& InPrefix,
		FString& OutField,
		FString& OutExpected,
		FString& OutActual)
	{
		if (!InExpected.IsValid() || !InActual.IsValid() || InExpected->Type != InActual->Type)
		{
			OutField = InPrefix;
			OutExpected = InExpected.IsValid() ? FEpicodeSandboxJson::CanonicalString(InExpected) : TEXT("<missing>");
			OutActual = InActual.IsValid() ? FEpicodeSandboxJson::CanonicalString(InActual) : TEXT("<missing>");
			return true;
		}
		if (InExpected->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> ExpectedObject = InExpected->AsObject();
			const TSharedPtr<FJsonObject> ActualObject = InActual->AsObject();
			TSet<FString> Keys;
			for (const auto& Pair : ExpectedObject->Values)
			{
				Keys.Add(FString(*Pair.Key));
			}
			for (const auto& Pair : ActualObject->Values)
			{
				Keys.Add(FString(*Pair.Key));
			}
			TArray<FString> SortedKeys = Keys.Array();
			SortedKeys.Sort();
			for (const FString& Key : SortedKeys)
			{
				const TSharedPtr<FJsonValue> ExpectedPtr = ExpectedObject->HasField(Key) ? ExpectedObject->TryGetField(Key) : nullptr;
				const TSharedPtr<FJsonValue> ActualPtr = ActualObject->HasField(Key) ? ActualObject->TryGetField(Key) : nullptr;
				const FString ChildPrefix = InPrefix.IsEmpty() ? Key : InPrefix + TEXT(".") + Key;
				if (FindObjectDiff(ExpectedPtr, ActualPtr, ChildPrefix, OutField, OutExpected, OutActual))
				{
					return true;
				}
			}
			return false;
		}
		if (InExpected->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& ExpectedArray = InExpected->AsArray();
			const TArray<TSharedPtr<FJsonValue>>& ActualArray = InActual->AsArray();
			const int32 Count = FMath::Max(ExpectedArray.Num(), ActualArray.Num());
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const TSharedPtr<FJsonValue> ExpectedPtr = ExpectedArray.IsValidIndex(Index) ? ExpectedArray[Index] : nullptr;
				const TSharedPtr<FJsonValue> ActualPtr = ActualArray.IsValidIndex(Index) ? ActualArray[Index] : nullptr;
				const FString ChildPrefix = FString::Printf(TEXT("%s[%d]"), *InPrefix, Index);
				if (FindObjectDiff(ExpectedPtr, ActualPtr, ChildPrefix, OutField, OutExpected, OutActual))
				{
					return true;
				}
			}
			return false;
		}
		if (ValueDiffers(InExpected, InActual))
		{
			OutField = InPrefix;
			OutExpected = FEpicodeSandboxJson::CanonicalString(InExpected);
			OutActual = FEpicodeSandboxJson::CanonicalString(InActual);
			return true;
		}
		return false;
	}

	bool LoadJsonFile(const FString& InPath, TSharedPtr<FJsonObject>& OutJson)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *InPath))
		{
			return false;
		}
		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Text);
		return FJsonSerializer::Deserialize(Reader, OutJson) && OutJson.IsValid();
	}
}

TSharedPtr<FJsonObject> FEpicodeReplayDiff::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("sequence"), static_cast<double>(Sequence));
	Result->SetStringField(TEXT("entityId"), EntityId);
	Result->SetStringField(TEXT("field"), Field);
	Result->SetStringField(TEXT("expected"), Expected);
	Result->SetStringField(TEXT("actual"), Actual);
	return Result;
}

TSharedPtr<FJsonObject> FEpicodeReplayResimResult::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bSuccess);
	Result->SetBoolField(TEXT("exact"), bExact);
	Result->SetBoolField(TEXT("cancelled"), bCancelled);
	Result->SetBoolField(TEXT("inexact"), bInexact);
	Result->SetNumberField(TEXT("targetSequence"), static_cast<double>(TargetSequence));
	Result->SetNumberField(TEXT("appliedEvents"), static_cast<double>(AppliedEvents));
	Result->SetStringField(TEXT("digest"), Digest);
	Result->SetStringField(TEXT("error"), Error);
	TArray<TSharedPtr<FJsonValue>> DiffValues;
	for (const FEpicodeReplayDiff& Diff : Diffs)
	{
		DiffValues.Add(MakeShared<FJsonValueObject>(Diff.ToJson()));
	}
	Result->SetArrayField(TEXT("diffs"), DiffValues);
	return Result;
}

bool FEpicodeReplayState::CanReplay(EEpicodeSandboxParticipantKind InKind, FString& OutError)
{
	if (InKind == EEpicodeSandboxParticipantKind::Owner || InKind == EEpicodeSandboxParticipantKind::Director)
	{
		return true;
	}
	OutError = TEXT("FORBIDDEN: replay requires owner or director");
	return false;
}

bool FEpicodeReplayState::ApplyEvent(FEpicodeSandboxWorld& InOutWorld, const FEpicodeSandboxEvent& InEvent, FString& OutError)
{
	if (InEvent.Sequence <= 0 || InEvent.Type.IsEmpty())
	{
		OutError = TEXT("REPLAY_EVENT_INVALID: missing sequence or type");
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Changes = nullptr;
	if (InEvent.Payload.IsValid() && InEvent.Payload->TryGetArrayField(TEXT("changes"), Changes) && Changes)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Changes)
		{
			const TSharedPtr<FJsonObject>* ChangePtr = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(ChangePtr) || !ChangePtr)
			{
				OutError = TEXT("REPLAY_EVENT_INVALID: malformed change");
				return false;
			}
			const TSharedPtr<FJsonObject>& Change = *ChangePtr;
			FString Op;
			Change->TryGetStringField(TEXT("op"), Op);
			if (Op == TEXT("narrative.variable"))
			{
				FString Name;
				const TSharedPtr<FJsonValue> ValuePtr = Change->TryGetField(TEXT("value"));
				FEpicodeSandboxValue Parsed;
				if (!Change->TryGetStringField(TEXT("name"), Name) || !ValuePtr
					|| !FEpicodeSandboxValue::FromJson(ValuePtr, Parsed, OutError))
				{
					OutError = TEXT("REPLAY_EVENT_INVALID: narrative variable change");
					return false;
				}
				InOutWorld.Narrative.SetVariable(Name, Parsed);
				continue;
			}
			if (Op == TEXT("narrative.beat"))
			{
				FString BeatId;
				if (!Change->TryGetStringField(TEXT("value"), BeatId))
				{
					OutError = TEXT("REPLAY_EVENT_INVALID: narrative beat change");
					return false;
				}
				InOutWorld.Narrative.CurrentBeatId = BeatId;
				continue;
			}
			if (Op == TEXT("narrative.objective"))
			{
				FString ObjectiveId;
				bool bCompleted = true;
				if (!Change->TryGetStringField(TEXT("id"), ObjectiveId) || !Change->TryGetBoolField(TEXT("value"), bCompleted))
				{
					OutError = TEXT("REPLAY_EVENT_INVALID: narrative objective change");
					return false;
				}
				InOutWorld.Narrative.Objectives.Add(ObjectiveId, bCompleted);
				continue;
			}
			if (Op == TEXT("narrative.objectiveCurrent"))
			{
				FString ObjectiveId;
				if (!Change->TryGetStringField(TEXT("value"), ObjectiveId))
				{
					OutError = TEXT("REPLAY_EVENT_INVALID: narrative current objective change");
					return false;
				}
				InOutWorld.Narrative.CurrentObjectiveId = ObjectiveId;
				continue;
			}
			if (Op == TEXT("entity.add"))
			{
				const TSharedPtr<FJsonObject>* EntityJson = nullptr;
				FEpicodeSandboxEntity Entity;
				if (!Change->TryGetObjectField(TEXT("entity"), EntityJson) || !EntityJson
					|| !FEpicodeSandboxEntity::FromJson(*EntityJson, Entity, OutError)
					|| !InOutWorld.AddEntity(Entity, OutError))
				{
					OutError = TEXT("REPLAY_EVENT_INVALID: entity add");
					return false;
				}
				continue;
			}
			if (Op == TEXT("entity.remove"))
			{
				FString Id;
				if (!Change->TryGetStringField(TEXT("id"), Id) || !InOutWorld.RemoveEntity(Id, OutError))
				{
					OutError = TEXT("REPLAY_EVENT_INVALID: entity remove");
					return false;
				}
				continue;
			}
			FString Id;
			if (!Change->TryGetStringField(TEXT("id"), Id))
			{
				OutError = TEXT("REPLAY_EVENT_INVALID: change has no entity id");
				return false;
			}
			FEpicodeSandboxEntity* Entity = InOutWorld.FindEntity(Id);
			if (!Entity)
			{
				OutError = FString::Printf(TEXT("REPLAY_EVENT_INVALID: entity %s not found"), *Id);
				return false;
			}
			if (Op == TEXT("entity.location"))
			{
				if (!ReadVector(Change, TEXT("location"), Entity->Location))
				{
					OutError = TEXT("REPLAY_EVENT_INVALID: entity location");
					return false;
				}
				continue;
			}
			if (Op == TEXT("entity.rotation"))
			{
				FVector Rotation;
				if (!ReadVector(Change, TEXT("rotation"), Rotation))
				{
					OutError = TEXT("REPLAY_EVENT_INVALID: entity rotation");
					return false;
				}
				Entity->Rotation = FRotator(Rotation.X, Rotation.Y, Rotation.Z);
				continue;
			}
			if (Op == TEXT("entity.inventory"))
			{
				FString ItemId;
				int32 Count = 0;
				if (!Change->TryGetStringField(TEXT("itemId"), ItemId) || !Change->TryGetNumberField(TEXT("count"), Count))
				{
					OutError = TEXT("REPLAY_EVENT_INVALID: entity inventory");
					return false;
				}
				Entity->SetItemCount(ItemId, Count);
				continue;
			}
			if (Op == TEXT("entity.visible"))
			{
				bool bVisible = true;
				if (!Change->TryGetBoolField(TEXT("value"), bVisible))
				{
					OutError = TEXT("REPLAY_EVENT_INVALID: entity visible");
					return false;
				}
				Entity->bVisible = bVisible;
				continue;
			}
			if (Op == TEXT("entity.owner"))
			{
				FString OwnerId;
				Change->TryGetStringField(TEXT("value"), OwnerId);
				Entity->OwnerId = OwnerId;
				continue;
			}
			if (Op == TEXT("entity.dataString"))
			{
				FString Key;
				FString DataValue;
				if (!Change->TryGetStringField(TEXT("key"), Key) || !Change->TryGetStringField(TEXT("value"), DataValue))
				{
					OutError = TEXT("REPLAY_EVENT_INVALID: entity data string");
					return false;
				}
				EnsureData(*Entity)->SetStringField(Key, DataValue);
				continue;
			}
			if (Op == TEXT("entity.dataInt"))
			{
				FString Key;
				int32 DataValue = 0;
				if (!Change->TryGetStringField(TEXT("key"), Key) || !Change->TryGetNumberField(TEXT("value"), DataValue))
				{
					OutError = TEXT("REPLAY_EVENT_INVALID: entity data int");
					return false;
				}
				EnsureData(*Entity)->SetNumberField(Key, DataValue);
				continue;
			}
			if (Op == TEXT("entity.dataRemove"))
			{
				FString Key;
				if (!Change->TryGetStringField(TEXT("key"), Key))
				{
					OutError = TEXT("REPLAY_EVENT_INVALID: entity data remove");
					return false;
				}
				EnsureData(*Entity)->RemoveField(Key);
				continue;
			}
			if (Op == TEXT("participant.takeover"))
			{
				FString ParticipantId;
				bool bTakeover = true;
				if (!Change->TryGetStringField(TEXT("participantId"), ParticipantId) || !Change->TryGetBoolField(TEXT("value"), bTakeover))
				{
					OutError = TEXT("REPLAY_EVENT_INVALID: participant takeover");
					return false;
				}
				FEpicodeSandboxParticipant* Participant = InOutWorld.FindParticipant(ParticipantId);
				if (!Participant)
				{
					OutError = TEXT("REPLAY_EVENT_INVALID: participant not found");
					return false;
				}
				Participant->bTakeover = bTakeover;
				Participant->bConnected = !bTakeover;
				continue;
			}
			OutError = FString::Printf(TEXT("REPLAY_EVENT_UNSUPPORTED: %s"), *Op);
			return false;
		}
	}
	InOutWorld.Tick = FMath::Max(InOutWorld.Tick, InEvent.Tick);
	return true;
}

void FEpicodeReplayState::ComputeDiff(const FEpicodeSandboxWorld& InExpected, const FEpicodeSandboxWorld& InActual, int64 InSequence, TArray<FEpicodeReplayDiff>& OutDiffs)
{
	TSet<FString> EntityIds;
	for (const auto& Pair : InExpected.Entities)
	{
		EntityIds.Add(Pair.Key);
	}
	for (const auto& Pair : InActual.Entities)
	{
		EntityIds.Add(Pair.Key);
	}
	TArray<FString> SortedIds = EntityIds.Array();
	SortedIds.Sort();
	for (const FString& EntityId : SortedIds)
	{
		const FEpicodeSandboxEntity* Expected = InExpected.FindEntity(EntityId);
		const FEpicodeSandboxEntity* Actual = InActual.FindEntity(EntityId);
		if (!Expected || !Actual)
		{
			FEpicodeReplayDiff Diff;
			Diff.Sequence = InSequence;
			Diff.EntityId = EntityId;
			Diff.Field = TEXT("entity");
			Diff.Expected = Expected ? TEXT("present") : TEXT("missing");
			Diff.Actual = Actual ? TEXT("present") : TEXT("missing");
			OutDiffs.Add(Diff);
			return;
		}
		FString Field;
		FString ExpectedText;
		FString ActualText;
		if (FindObjectDiff(MakeShared<FJsonValueObject>(Expected->ToJson()), MakeShared<FJsonValueObject>(Actual->ToJson()), FString(), Field, ExpectedText, ActualText))
		{
			FEpicodeReplayDiff Diff;
			Diff.Sequence = InSequence;
			Diff.EntityId = EntityId;
			Diff.Field = Field;
			Diff.Expected = ExpectedText;
			Diff.Actual = ActualText;
			OutDiffs.Add(Diff);
			return;
		}
	}
	FString Field;
	FString ExpectedText;
	FString ActualText;
	if (FindObjectDiff(MakeShared<FJsonValueObject>(InExpected.Narrative.ToJson()), MakeShared<FJsonValueObject>(InActual.Narrative.ToJson()), TEXT("narrative"), Field, ExpectedText, ActualText))
	{
		FEpicodeReplayDiff Diff;
		Diff.Sequence = InSequence;
		Diff.Field = Field;
		Diff.Expected = ExpectedText;
		Diff.Actual = ActualText;
		OutDiffs.Add(Diff);
	}
}

bool FEpicodeReplayResimRunner::Begin(const FString& InRoot, int64 InTargetSequence, const FEpicodeReplayResimOptions& InOptions, FString& OutError)
{
	Journal = FEpicodeReplayJournal();
	Root = InRoot;
	Checkpoint = FEpicodeReplayCheckpoint();
	World = FEpicodeSandboxWorld();
	Random = FEpicodeSandboxRandomStreams();
	Result = FEpicodeReplayResimResult();
	Options = InOptions;
	TargetSequence = InTargetSequence;
	NextIndex = 0;
	bBegun = false;
	bFinished = false;
	bCancelRequested = false;
	bVersionMismatch = false;
	Result.TargetSequence = InTargetSequence;

	if (!Journal.Open(InRoot, OutError))
	{
		return false;
	}
	if (InTargetSequence > Journal.GetLastSequence())
	{
		OutError = FString::Printf(TEXT("REPLAY_TARGET_UNAVAILABLE: target %lld, last %lld"), InTargetSequence, Journal.GetLastSequence());
		return false;
	}
	if (!Options.ExpectedContentDigest.IsEmpty() && Journal.GetHeader().ContentDigest != Options.ExpectedContentDigest)
	{
		if (!Options.bAllowInexact)
		{
			OutError = TEXT("REPLAY_VERSION_MISMATCH: content digest differs");
			return false;
		}
		bVersionMismatch = true;
		Result.bInexact = true;
	}
	if (!FEpicodeReplayCheckpoints::LoadNearest(InRoot, InTargetSequence, Checkpoint, OutError))
	{
		if (InTargetSequence < Journal.GetFirstSequence())
		{
			OutError = TEXT("REPLAY_HISTORY_UNAVAILABLE: target is outside retained history");
		}
		return false;
	}
	if (InTargetSequence < Journal.GetFirstSequence() && Checkpoint.Sequence != InTargetSequence)
	{
		OutError = TEXT("REPLAY_HISTORY_UNAVAILABLE: target is outside retained history");
		return false;
	}
	if (!Checkpoint.ContentDigest.IsEmpty() && !Options.ExpectedContentDigest.IsEmpty() && Checkpoint.ContentDigest != Options.ExpectedContentDigest)
	{
		if (!Options.bAllowInexact)
		{
			OutError = TEXT("REPLAY_VERSION_MISMATCH: checkpoint content digest differs");
			return false;
		}
		bVersionMismatch = true;
		Result.bInexact = true;
	}
	World = Checkpoint.World;
	Random = Checkpoint.Random;
	while (NextIndex < Journal.GetEntries().Num() && Journal.GetEntries()[NextIndex].Event.Sequence <= Checkpoint.Sequence)
	{
		++NextIndex;
	}
	bBegun = true;
	if (TargetSequence == Checkpoint.Sequence)
	{
		return true;
	}
	return true;
}

EEpicodeReplayStepResult FEpicodeReplayResimRunner::Fail(const FString& InError, FString& OutError)
{
	Result.Error = InError;
	Result.bSuccess = false;
	bFinished = true;
	OutError = InError;
	return EEpicodeReplayStepResult::Failed;
}

EEpicodeReplayStepResult FEpicodeReplayResimRunner::Complete(FString& OutError)
{
	Result.World = World;
	Result.Random = Random;
	Result.Digest = FEpicodeSandboxDigest::ComputeWorldDigest(World);
	if (bVersionMismatch)
	{
		Result.bInexact = true;
	}
	Result.bExact = !bVersionMismatch;
	Result.bSuccess = true;
	bFinished = true;
	OutError.Reset();
	return EEpicodeReplayStepResult::Completed;
}

EEpicodeReplayStepResult FEpicodeReplayResimRunner::Step(int32 InMaxEvents, FString& OutError)
{
	OutError.Reset();
	if (!bBegun || bFinished)
	{
		return bFinished ? EEpicodeReplayStepResult::Completed : Fail(TEXT("REPLAY_NOT_STARTED: runner is not started"), OutError);
	}
	if (bCancelRequested)
	{
		Result.bCancelled = true;
		Result.bSuccess = false;
		Result.Error = TEXT("REPLAY_CANCELLED");
		bFinished = true;
		OutError = Result.Error;
		return EEpicodeReplayStepResult::Cancelled;
	}
	int32 Applied = 0;
	while (Applied < FMath::Max(1, InMaxEvents))
	{
		if (bCancelRequested)
		{
			Result.bCancelled = true;
			Result.bSuccess = false;
			Result.Error = TEXT("REPLAY_CANCELLED");
			bFinished = true;
			OutError = Result.Error;
			return EEpicodeReplayStepResult::Cancelled;
		}
		if (NextIndex >= Journal.GetEntries().Num() || Journal.GetEntries()[NextIndex].Event.Sequence > TargetSequence)
		{
			break;
		}
		const FEpicodeReplayJournalEntry& Entry = Journal.GetEntries()[NextIndex];
		if (!FEpicodeReplayState::ApplyEvent(World, Entry.Event, OutError))
		{
			return Fail(OutError, OutError);
		}
		++NextIndex;
		++Applied;
		++Result.AppliedEvents;
	}
	if (NextIndex >= Journal.GetEntries().Num() || Journal.GetEntries()[NextIndex].Event.Sequence > TargetSequence)
	{
		const FString ActualDigest = FEpicodeSandboxDigest::ComputeWorldDigest(World);
		FString RecordedDigest;
		if (!Checkpoint.ContentDigest.IsEmpty())
		{
			RecordedDigest = FEpicodeSandboxDigest::ComputeWorldDigest(Checkpoint.World);
		}
		if (TargetSequence > Checkpoint.Sequence)
		{
			for (const FEpicodeReplayJournalEntry& Entry : Journal.GetEntries())
			{
				if (Entry.Event.Sequence == TargetSequence)
				{
					RecordedDigest = Entry.WorldDigest;
					break;
				}
			}
		}
		if (!RecordedDigest.IsEmpty() && RecordedDigest != ActualDigest)
		{
			Result.Digest = ActualDigest;
			FEpicodeReplayDiff Diff;
			Diff.Sequence = TargetSequence;
			Diff.Field = TEXT("digest");
			Diff.Expected = RecordedDigest;
			Diff.Actual = ActualDigest;
			FEpicodeReplayCheckpoint TargetCheckpoint;
			FString CheckpointError;
			if (FEpicodeReplayCheckpoints::LoadAt(Root, TargetSequence, TargetCheckpoint, CheckpointError))
			{
				FEpicodeReplayState::ComputeDiff(TargetCheckpoint.World, World, TargetSequence, Result.Diffs);
			}
			if (Result.Diffs.Num() == 0)
			{
				Result.Diffs.Add(Diff);
			}
			return Fail(TEXT("REPLAY_DIVERGED: state digest differs at target"), OutError);
		}
		return Complete(OutError);
	}
	return EEpicodeReplayStepResult::Running;
}

bool FEpicodeReplayResim::Run(const FString& InRoot, int64 InTargetSequence, const FEpicodeReplayResimOptions& InOptions, FEpicodeReplayResimResult& OutResult, FString& OutError)
{
	FEpicodeReplayResimRunner Runner;
	if (!Runner.Begin(InRoot, InTargetSequence, InOptions, OutError))
	{
		OutResult = Runner.GetResult();
		OutResult.Error = OutError;
		return false;
	}
	EEpicodeReplayStepResult StepResult = EEpicodeReplayStepResult::Running;
	while (StepResult == EEpicodeReplayStepResult::Running)
	{
		StepResult = Runner.Step(1000000, OutError);
	}
	OutResult = Runner.GetResult();
	if (StepResult == EEpicodeReplayStepResult::Completed)
	{
		return true;
	}
	if (OutError.IsEmpty())
	{
		OutError = OutResult.Error;
	}
	return false;
}

bool FEpicodeReplayGhost::Open(const FString& InRoot, FString& OutError)
{
	Root = InRoot;
	return Journal.Open(InRoot, OutError);
}

bool FEpicodeReplayGhost::GetPage(int64 InStartSequence, int32 InCount, FEpicodeReplayGhostPage& OutPage, FString& OutError) const
{
	OutPage = FEpicodeReplayGhostPage();
	OutPage.StartSequence = InStartSequence;
	OutPage.TargetSequence = InStartSequence + FMath::Max(0, InCount) - 1;
	if (InStartSequence < Journal.GetFirstSequence())
	{
		OutError = FString::Printf(TEXT("REPLAY_HISTORY_UNAVAILABLE: earliest is %lld"), Journal.GetFirstSequence());
		return false;
	}
	for (const FEpicodeReplayJournalEntry& Entry : Journal.GetEntries())
	{
		if (Entry.Event.Sequence >= InStartSequence && OutPage.Entries.Num() < InCount)
		{
			OutPage.Entries.Add(Entry);
		}
	}
	return true;
}

bool FEpicodeReplayGhost::Seek(int64 InSequence, FEpicodeReplayResimResult& OutResult, FString& OutError) const
{
	FEpicodeReplayResimOptions Options;
	return FEpicodeReplayResim::Run(Root, InSequence, Options, OutResult, OutError);
}

TSharedPtr<FJsonObject> FEpicodeReplayBranchInfo::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("branchId"), BranchId);
	Result->SetStringField(TEXT("parentSessionId"), ParentSessionId);
	Result->SetNumberField(TEXT("forkSequence"), static_cast<double>(ForkSequence));
	Result->SetStringField(TEXT("createdBy"), CreatedBy);
	Result->SetStringField(TEXT("contentDigest"), ContentDigest);
	Result->SetStringField(TEXT("checksum"), ComputeChecksum());
	return Result;
}

bool FEpicodeReplayBranchInfo::FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeReplayBranchInfo& OutInfo, FString& OutError)
{
	if (!InJson.IsValid())
	{
		OutError = TEXT("BRANCH_CORRUPT: missing metadata");
		return false;
	}
	double ForkSequence = 0.0;
	if (!InJson->TryGetStringField(TEXT("branchId"), OutInfo.BranchId)
		|| !InJson->TryGetStringField(TEXT("parentSessionId"), OutInfo.ParentSessionId)
		|| !InJson->TryGetNumberField(TEXT("forkSequence"), ForkSequence)
		|| !InJson->TryGetStringField(TEXT("createdBy"), OutInfo.CreatedBy)
		|| !InJson->TryGetStringField(TEXT("contentDigest"), OutInfo.ContentDigest))
	{
		OutError = TEXT("BRANCH_CORRUPT: incomplete metadata");
		return false;
	}
	OutInfo.ForkSequence = static_cast<int64>(ForkSequence);
	FString ActualChecksum;
	if (!InJson->TryGetStringField(TEXT("checksum"), ActualChecksum) || ActualChecksum != OutInfo.ComputeChecksum())
	{
		OutError = TEXT("BRANCH_CORRUPT: checksum mismatch");
		return false;
	}
	OutInfo.Checksum = ActualChecksum;
	return true;
}

FString FEpicodeReplayBranchInfo::ComputeChecksum() const
{
	TSharedPtr<FJsonObject> Canonical = MakeShared<FJsonObject>();
	Canonical->SetStringField(TEXT("branchId"), BranchId);
	Canonical->SetStringField(TEXT("parentSessionId"), ParentSessionId);
	Canonical->SetNumberField(TEXT("forkSequence"), static_cast<double>(ForkSequence));
	Canonical->SetStringField(TEXT("createdBy"), CreatedBy);
	Canonical->SetStringField(TEXT("contentDigest"), ContentDigest);
	return FEpicodeReplayChecksum::Sha1(FEpicodeSandboxJson::CanonicalString(Canonical));
}

FString FEpicodeReplayBranch::GetBranchMetadataPath(const FString& InRoot)
{
	return FPaths::Combine(InRoot, TEXT("branch.json"));
}

bool FEpicodeReplayBranch::Create(
	const FString& InParentRoot,
	const FString& InBranchRoot,
	const FString& InBranchId,
	int64 InForkSequence,
	const FString& InCreatedBy,
	EEpicodeSandboxParticipantKind InActorKind,
	FString& OutError)
{
	if (!FEpicodeReplayState::CanReplay(InActorKind, OutError))
	{
		return false;
	}
	if (InBranchId.IsEmpty() || InBranchRoot.IsEmpty() || InCreatedBy.IsEmpty())
	{
		OutError = TEXT("BRANCH_INVALID: missing branch id, root or creator");
		return false;
	}
	FEpicodeReplayJournal ParentJournal;
	if (!ParentJournal.Open(InParentRoot, OutError))
	{
		OutError = TEXT("BRANCH_POINT_UNAVAILABLE: ") + OutError;
		return false;
	}
	if (InForkSequence < ParentJournal.GetFirstSequence() || InForkSequence > ParentJournal.GetLastSequence())
	{
		OutError = TEXT("BRANCH_POINT_UNAVAILABLE: fork sequence is outside journal");
		return false;
	}
	if (IFileManager::Get().DirectoryExists(*InBranchRoot))
	{
		OutError = TEXT("BRANCH_EXISTS: branch directory already exists");
		return false;
	}
	FEpicodeReplayResimOptions Options;
	Options.ExpectedContentDigest = ParentJournal.GetHeader().ContentDigest;
	FEpicodeReplayResimResult ResimResult;
	FString ResimError;
	if (!FEpicodeReplayResim::Run(InParentRoot, InForkSequence, Options, ResimResult, ResimError))
	{
		OutError = TEXT("BRANCH_POINT_UNAVAILABLE: ") + ResimError;
		return false;
	}
	const FString TempRoot = InBranchRoot + TEXT(".tmp-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	if (!IFileManager::Get().MakeDirectory(*TempRoot, true))
	{
		OutError = TEXT("BRANCH_CREATE_FAILED: unable to create staging directory");
		return false;
	}
	FEpicodeReplayJournalHeader Header;
	Header.SessionId = InBranchId;
	Header.ContentDigest = ParentJournal.GetHeader().ContentDigest;
	Header.Seed = ParentJournal.GetHeader().Seed;
	Header.StartSequence = InForkSequence + 1;
	FEpicodeReplayJournal BranchJournal;
	if (!BranchJournal.Create(TempRoot, Header, OutError))
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		OutError = TEXT("BRANCH_CREATE_FAILED: ") + OutError;
		return false;
	}
	FEpicodeReplayCheckpoint Checkpoint;
	Checkpoint.Sequence = InForkSequence;
	Checkpoint.Tick = ResimResult.World.Tick;
	Checkpoint.TickRate = 30;
	Checkpoint.Seed = ParentJournal.GetHeader().Seed;
	Checkpoint.ContentDigest = ParentJournal.GetHeader().ContentDigest;
	Checkpoint.World = ResimResult.World;
	Checkpoint.Random = ResimResult.Random;
	if (!FEpicodeReplayCheckpoints::Save(TempRoot, Checkpoint, OutError))
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		OutError = TEXT("BRANCH_CREATE_FAILED: ") + OutError;
		return false;
	}
	FEpicodeSandboxEventStream EmptyEvents;
	EmptyEvents.Reset(InForkSequence + 1);
	if (!FEpicodeSandboxPersistence::Save(ResimResult.World, EmptyEvents, TempRoot, OutError))
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		OutError = TEXT("BRANCH_CREATE_FAILED: ") + OutError;
		return false;
	}
	FEpicodeReplayBranchInfo Info;
	Info.BranchId = InBranchId;
	Info.ParentSessionId = ParentJournal.GetHeader().SessionId;
	Info.ForkSequence = InForkSequence;
	Info.CreatedBy = InCreatedBy;
	Info.ContentDigest = ParentJournal.GetHeader().ContentDigest;
	Info.Checksum = Info.ComputeChecksum();
	const TSharedPtr<FJsonObject> Metadata = Info.ToJson();
	if (!FFileHelper::SaveStringToFile(FEpicodeSandboxJson::Serialize(Metadata, true), *GetBranchMetadataPath(TempRoot), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		OutError = TEXT("BRANCH_CREATE_FAILED: unable to write branch metadata");
		return false;
	}
	if (!IFileManager::Get().Move(*InBranchRoot, *TempRoot, true, true))
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		OutError = TEXT("BRANCH_CREATE_FAILED: unable to publish branch directory");
		return false;
	}
	return true;
}
