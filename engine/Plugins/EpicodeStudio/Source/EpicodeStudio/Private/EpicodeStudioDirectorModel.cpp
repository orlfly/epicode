#include "EpicodeStudioDirectorModel.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	void AddLinesFromArray(const TArray<TSharedPtr<FJsonValue>>& InValues, TArray<FString>& OutLines)
	{
		for (const TSharedPtr<FJsonValue>& Value : InValues)
		{
			if (Value.IsValid() && Value->Type == EJson::String)
			{
				OutLines.Add(Value->AsString());
			}
		}
	}

	void AddBudgetLines(const TSharedPtr<FJsonObject>& InObject, TArray<FString>& OutLines)
	{
		if (!InObject.IsValid())
		{
			return;
		}
		for (const auto& Pair : InObject->Values)
		{
			if (!Pair.Value.IsValid())
			{
				continue;
			}
			const FString Key(*Pair.Key);
			if (Pair.Value->Type == EJson::Number)
			{
				OutLines.Add(FString::Printf(TEXT("%s: %.0f"), *Key, Pair.Value->AsNumber()));
			}
			else if (Pair.Value->Type == EJson::Boolean)
			{
				OutLines.Add(FString::Printf(TEXT("%s: %s"), *Key, Pair.Value->AsBool() ? TEXT("true") : TEXT("false")));
			}
			else if (Pair.Value->Type == EJson::String)
			{
				OutLines.Add(FString::Printf(TEXT("%s: %s"), *Key, *Pair.Value->AsString()));
			}
		}
		OutLines.Sort();
	}

	void ParseEvents(const TSharedPtr<FJsonObject>& InSnapshot, TArray<FEpicodeStudioEventRow>& OutEvents)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!InSnapshot.IsValid() || !InSnapshot->TryGetArrayField(TEXT("events"), Values) || !Values)
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			const TSharedPtr<FJsonObject> Event = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Event.IsValid())
			{
				continue;
			}
			FEpicodeStudioEventRow Row;
			double Sequence = 0.0;
			Event->TryGetNumberField(TEXT("sequence"), Sequence);
			Row.Sequence = static_cast<int64>(Sequence);
			Event->TryGetStringField(TEXT("type"), Row.Type);
			Event->TryGetStringField(TEXT("actorId"), Row.ActorId);
			Event->TryGetStringField(TEXT("summary"), Row.Summary);
			Event->TryGetStringField(TEXT("beatId"), Row.BeatId);
			if (Row.Summary.IsEmpty())
			{
				Row.Summary = Row.Type;
			}
			OutEvents.Add(Row);
		}
	}
}

void FEpicodeStudioDirectorModel::Reset()
{
	*this = FEpicodeStudioDirectorModel();
}

bool FEpicodeStudioDirectorModel::ApplySandboxSnapshot(const TSharedPtr<FJsonObject>& InSnapshot)
{
	if (!InSnapshot.IsValid())
	{
		LastError = TEXT("SANDBOX_SNAPSHOT_INVALID: missing snapshot");
		return false;
	}
	ProductionId.Reset();
	BudgetLines.Reset();
	ValidationWarnings.Reset();
	bSandboxActive = false;
	InSnapshot->TryGetStringField(TEXT("productionId"), ProductionId);
	InSnapshot->TryGetStringField(TEXT("state"), LastError);
	bSandboxActive = LastError == TEXT("running") || InSnapshot->GetBoolField(TEXT("active"));
	LastError.Reset();
	if (!InSnapshot->HasField(TEXT("productionId")) && !InSnapshot->HasField(TEXT("state")))
	{
		LastError = TEXT("SANDBOX_SNAPSHOT_INVALID: state and productionId are required");
		return false;
	}
	const TSharedPtr<FJsonObject>* Budget = nullptr;
	if (InSnapshot->TryGetObjectField(TEXT("budgets"), Budget) && Budget)
	{
		AddBudgetLines(*Budget, BudgetLines);
	}
	if (InSnapshot->TryGetObjectField(TEXT("budget"), Budget) && Budget)
	{
		AddBudgetLines(*Budget, BudgetLines);
	}
	const TArray<TSharedPtr<FJsonValue>>* Warnings = nullptr;
	if (InSnapshot->TryGetArrayField(TEXT("warnings"), Warnings) && Warnings)
	{
		AddLinesFromArray(*Warnings, ValidationWarnings);
	}
	const TSharedPtr<FJsonObject>* Rules = nullptr;
	if (InSnapshot->TryGetObjectField(TEXT("rules"), Rules) && Rules)
	{
		InSnapshot->TryGetStringField(TEXT("productionId"), ProductionId);
	}
	ParseEvents(InSnapshot, Events);
	return true;
}

bool FEpicodeStudioDirectorModel::ApplyDirectorSnapshot(const TSharedPtr<FJsonObject>& InSnapshot)
{
	if (!InSnapshot.IsValid())
	{
		LastError = TEXT("DIRECTOR_SNAPSHOT_INVALID: missing snapshot");
		return false;
	}
	InSnapshot->TryGetStringField(TEXT("beatId"), CurrentBeatId);
	InSnapshot->TryGetStringField(TEXT("objectiveId"), CurrentObjectiveId);
	const TArray<TSharedPtr<FJsonValue>>* CueValues = nullptr;
	CueLines.Reset();
	if (InSnapshot->TryGetArrayField(TEXT("cues"), CueValues) && CueValues)
	{
		AddLinesFromArray(*CueValues, CueLines);
	}
	const TArray<TSharedPtr<FJsonValue>>* Messages = nullptr;
	if (InSnapshot->TryGetArrayField(TEXT("messages"), Messages) && Messages)
	{
		CueLines.Reset();
		for (const TSharedPtr<FJsonValue>& Message : *Messages)
		{
			if (Message.IsValid())
			{
				CueLines.Add(Message->AsString());
			}
		}
	}
	const TSharedPtr<FJsonObject>* Narrative = nullptr;
	if (InSnapshot->TryGetObjectField(TEXT("narrative"), Narrative) && Narrative)
	{
		(*Narrative)->TryGetStringField(TEXT("currentBeatId"), CurrentBeatId);
		(*Narrative)->TryGetStringField(TEXT("currentObjectiveId"), CurrentObjectiveId);
	}
	if (InSnapshot->HasField(TEXT("paused")))
	{
		Mode = InSnapshot->GetBoolField(TEXT("paused")) ? TEXT("paused") : TEXT("live");
	}
	if (InSnapshot->HasField(TEXT("mode")))
	{
		FString NewMode;
		InSnapshot->TryGetStringField(TEXT("mode"), NewMode);
		if (!NewMode.IsEmpty())
		{
			Mode = NewMode;
		}
	}
	ParseEvents(InSnapshot, Events);
	LastError.Reset();
	return true;
}

void FEpicodeStudioDirectorModel::SetZoom(double InZoom)
{
	Zoom = FMath::Clamp(InZoom, 0.25, 8.0);
}

TArray<FEpicodeStudioEventRow> FEpicodeStudioDirectorModel::GetFilteredEvents() const
{
	if (EventFilter.IsEmpty())
	{
		return Events;
	}
	TArray<FEpicodeStudioEventRow> Result;
	for (const FEpicodeStudioEventRow& Event : Events)
	{
		if (Event.Type.Contains(EventFilter, ESearchCase::IgnoreCase)
			|| Event.ActorId.Contains(EventFilter, ESearchCase::IgnoreCase)
			|| Event.Summary.Contains(EventFilter, ESearchCase::IgnoreCase)
			|| Event.BeatId.Contains(EventFilter, ESearchCase::IgnoreCase))
		{
			Result.Add(Event);
		}
	}
	return Result;
}

bool FEpicodeStudioDirectorModel::SelectTake(const FString& InTakeId, int32 InVersion)
{
	if (InTakeId.IsEmpty() || InVersion < 1)
	{
		LastError = TEXT("TAKE_INVALID: take id and version are required");
		return false;
	}
	if (LatestTakeVersion > 0 && InVersion > LatestTakeVersion)
	{
		LastError = FString::Printf(TEXT("TAKE_NOT_FOUND: version %d"), InVersion);
		return false;
	}
	SelectedTakeId = InTakeId;
	SelectedTakeVersion = InVersion;
	LastError.Reset();
	return true;
}

bool FEpicodeStudioDirectorModel::SetMode(const FString& InMode)
{
	if (InMode != TEXT("live") && !CanControl())
	{
		LastError = TEXT("VIEWER_READ_ONLY: ghost and resim require director role");
		return false;
	}
	Mode = InMode;
	LastError.Reset();
	return true;
}

bool FEpicodeStudioDirectorModel::AcquireTakeover(const FString& InParticipantId, const FString& InDirectorId)
{
	if (!CanControl())
	{
		LastError = TEXT("VIEWER_READ_ONLY: takeover requires director role");
		return false;
	}
	if (InParticipantId.IsEmpty() || InDirectorId.IsEmpty())
	{
		LastError = TEXT("TAKEOVER_INVALID: participant and director are required");
		return false;
	}
	if (const FString* Existing = Takeovers.Find(InParticipantId))
	{
		if (*Existing != InDirectorId)
		{
			LastError = TEXT("TAKEOVER_CONFLICT: participant is controlled by another director");
			return false;
		}
	}
	Takeovers.Add(InParticipantId, InDirectorId);
	LastError.Reset();
	return true;
}

bool FEpicodeStudioDirectorModel::ReleaseTakeover(const FString& InParticipantId)
{
	Takeovers.Remove(InParticipantId);
	LastError.Reset();
	return true;
}

const FString& FEpicodeStudioDirectorModel::GetTakeoverDirector(const FString& InParticipantId) const
{
	static const FString Empty;
	const FString* Director = Takeovers.Find(InParticipantId);
	return Director ? *Director : Empty;
}
