#include "EpicodeDirector.h"

#include "EpicodeSandboxJson.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	bool ReadStringArray(const TSharedPtr<FJsonObject>& InJson, const FString& InField, TArray<FString>& OutValues, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!InJson->TryGetArrayField(InField, Values) || !Values)
		{
			return true;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			if (!Value.IsValid() || Value->Type != EJson::String)
			{
				OutError = FString::Printf(TEXT("DIRECTOR_JSON_INVALID: %s must be strings"), *InField);
				return false;
			}
			OutValues.Add(Value->AsString());
		}
		return true;
	}

	bool ReadConditions(const TSharedPtr<FJsonObject>& InJson, const FString& InField, TArray<FEpicodeSandboxCondition>& OutConditions, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!InJson->TryGetArrayField(InField, Values) || !Values)
		{
			return true;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			FEpicodeSandboxCondition Condition;
			if (!Value.IsValid() || !FEpicodeSandboxCondition::Parse(Value->AsString(), Condition, OutError))
			{
				return false;
			}
			OutConditions.Add(Condition);
		}
		return true;
	}

	TArray<TSharedPtr<FJsonValue>> ConditionJson(const TArray<FEpicodeSandboxCondition>& InConditions)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FEpicodeSandboxCondition& Condition : InConditions)
		{
			Values.Add(MakeShared<FJsonValueString>(Condition.ToExpression()));
		}
		return Values;
	}

	TSharedPtr<FJsonObject> MakeChange(const FString& InOp)
	{
		TSharedPtr<FJsonObject> Change = MakeShared<FJsonObject>();
		Change->SetStringField(TEXT("op"), InOp);
		return Change;
	}

	bool AllConditions(const TArray<FEpicodeSandboxCondition>& InConditions, const FEpicodeSandboxNarrativeState& InNarrative)
	{
		for (const FEpicodeSandboxCondition& Condition : InConditions)
		{
			if (!Condition.Evaluate(InNarrative))
			{
				return false;
			}
		}
		return true;
	}

	bool ParseCueTrigger(const FString& InValue, EEpicodeCueTrigger& OutTrigger)
	{
		const FString Value = InValue.ToLower();
		if (Value == TEXT("time")) { OutTrigger = EEpicodeCueTrigger::Time; return true; }
		if (Value == TEXT("event")) { OutTrigger = EEpicodeCueTrigger::Event; return true; }
		if (Value == TEXT("beat")) { OutTrigger = EEpicodeCueTrigger::Beat; return true; }
		if (Value == TEXT("condition")) { OutTrigger = EEpicodeCueTrigger::Condition; return true; }
		return false;
	}

	FString TriggerToString(EEpicodeCueTrigger InTrigger)
	{
		switch (InTrigger)
		{
		case EEpicodeCueTrigger::Time: return TEXT("time");
		case EEpicodeCueTrigger::Event: return TEXT("event");
		case EEpicodeCueTrigger::Beat: return TEXT("beat");
		case EEpicodeCueTrigger::Condition: return TEXT("condition");
		default: return TEXT("time");
		}
	}

	bool LoadJsonObject(const FString& InPath, TSharedPtr<FJsonObject>& OutJson)
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

bool FEpicodeDirectorCue::Parse(const TSharedPtr<FJsonObject>& InJson, FEpicodeDirectorCue& OutCue, FString& OutError)
{
	if (!InJson.IsValid())
	{
		OutError = TEXT("DIRECTOR_CUE_INVALID: missing cue");
		return false;
	}
	if (!InJson->TryGetStringField(TEXT("id"), OutCue.Id) || OutCue.Id.IsEmpty())
	{
		OutError = TEXT("DIRECTOR_CUE_INVALID: missing id");
		return false;
	}
	FString Trigger = TEXT("time");
	InJson->TryGetStringField(TEXT("trigger"), Trigger);
	if (!ParseCueTrigger(Trigger, OutCue.Trigger))
	{
		OutError = FString::Printf(TEXT("DIRECTOR_CUE_INVALID: unknown trigger %s"), *Trigger);
		return false;
	}
	InJson->TryGetNumberField(TEXT("time"), OutCue.Time);
	InJson->TryGetStringField(TEXT("event"), OutCue.EventType);
	InJson->TryGetStringField(TEXT("beatId"), OutCue.BeatId);
	InJson->TryGetStringField(TEXT("condition"), OutCue.ConditionText);
	InJson->TryGetBoolField(TEXT("once"), OutCue.bOnce);
	if (!OutCue.ConditionText.IsEmpty())
	{
		if (!FEpicodeSandboxCondition::Parse(OutCue.ConditionText, OutCue.Condition, OutError))
		{
			return false;
		}
		OutCue.bHasCondition = true;
	}
	if (!ReadStringArray(InJson, TEXT("actions"), OutCue.Actions, OutError))
	{
		return false;
	}
	if (OutCue.Trigger == EEpicodeCueTrigger::Time && OutCue.Time < 0.0)
	{
		OutError = TEXT("DIRECTOR_CUE_INVALID: negative time");
		return false;
	}
	if (OutCue.Trigger == EEpicodeCueTrigger::Event && OutCue.EventType.IsEmpty())
	{
		OutError = TEXT("DIRECTOR_CUE_INVALID: event trigger has no event");
		return false;
	}
	if (OutCue.Trigger == EEpicodeCueTrigger::Beat && OutCue.BeatId.IsEmpty())
	{
		OutError = TEXT("DIRECTOR_CUE_INVALID: beat trigger has no beat");
		return false;
	}
	if (OutCue.Trigger == EEpicodeCueTrigger::Condition && !OutCue.bHasCondition)
	{
		OutError = TEXT("DIRECTOR_CUE_INVALID: condition trigger has no condition");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FEpicodeDirectorCue::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("id"), Id);
	Result->SetStringField(TEXT("trigger"), TriggerToString(Trigger));
	Result->SetNumberField(TEXT("time"), Time);
	Result->SetStringField(TEXT("event"), EventType);
	Result->SetStringField(TEXT("beatId"), BeatId);
	Result->SetStringField(TEXT("condition"), ConditionText);
	Result->SetBoolField(TEXT("once"), bOnce);
	TArray<TSharedPtr<FJsonValue>> ActionValues;
	for (const FString& Action : Actions)
	{
		ActionValues.Add(MakeShared<FJsonValueString>(Action));
	}
	Result->SetArrayField(TEXT("actions"), ActionValues);
	return Result;
}

bool FEpicodeDirectorBeat::Parse(const TSharedPtr<FJsonObject>& InJson, FEpicodeDirectorBeat& OutBeat, FString& OutError)
{
	if (!InJson.IsValid() || !InJson->TryGetStringField(TEXT("id"), OutBeat.Id) || OutBeat.Id.IsEmpty())
	{
		OutError = TEXT("DIRECTOR_BEAT_INVALID: missing id");
		return false;
	}
	InJson->TryGetStringField(TEXT("name"), OutBeat.Name);
	if (!ReadConditions(InJson, TEXT("entryConditions"), OutBeat.EntryConditions, OutError)
		|| !ReadConditions(InJson, TEXT("exitConditions"), OutBeat.ExitConditions, OutError)
		|| !ReadStringArray(InJson, TEXT("objectives"), OutBeat.ObjectiveIds, OutError)
		|| !ReadStringArray(InJson, TEXT("cues"), OutBeat.CueIds, OutError)
		|| !ReadStringArray(InJson, TEXT("next"), OutBeat.NextBeatIds, OutError))
	{
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FEpicodeDirectorBeat::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("id"), Id);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetArrayField(TEXT("entryConditions"), ConditionJson(EntryConditions));
	Result->SetArrayField(TEXT("exitConditions"), ConditionJson(ExitConditions));
	TArray<TSharedPtr<FJsonValue>> ObjectiveValues;
	for (const FString& Objective : ObjectiveIds) ObjectiveValues.Add(MakeShared<FJsonValueString>(Objective));
	Result->SetArrayField(TEXT("objectives"), ObjectiveValues);
	TArray<TSharedPtr<FJsonValue>> CueValues;
	for (const FString& Cue : CueIds) CueValues.Add(MakeShared<FJsonValueString>(Cue));
	Result->SetArrayField(TEXT("cues"), CueValues);
	TArray<TSharedPtr<FJsonValue>> NextValues;
	for (const FString& Next : NextBeatIds) NextValues.Add(MakeShared<FJsonValueString>(Next));
	Result->SetArrayField(TEXT("next"), NextValues);
	return Result;
}

bool FEpicodeDirectorObjective::Parse(const TSharedPtr<FJsonObject>& InJson, FEpicodeDirectorObjective& OutObjective, FString& OutError)
{
	if (!InJson.IsValid() || !InJson->TryGetStringField(TEXT("id"), OutObjective.Id) || OutObjective.Id.IsEmpty())
	{
		OutError = TEXT("DIRECTOR_OBJECTIVE_INVALID: missing id");
		return false;
	}
	InJson->TryGetStringField(TEXT("description"), OutObjective.Description);
	InJson->TryGetStringField(TEXT("condition"), OutObjective.ConditionText);
	if (!OutObjective.ConditionText.IsEmpty())
	{
		if (!FEpicodeSandboxCondition::Parse(OutObjective.ConditionText, OutObjective.Condition, OutError))
		{
			return false;
		}
		OutObjective.bHasCondition = true;
	}
	return true;
}

TSharedPtr<FJsonObject> FEpicodeDirectorObjective::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("id"), Id);
	Result->SetStringField(TEXT("description"), Description);
	Result->SetStringField(TEXT("condition"), ConditionText);
	return Result;
}

bool FEpicodeBeatGraph::Parse(const TSharedPtr<FJsonObject>& InNarrative, FEpicodeBeatGraph& OutGraph, FString& OutError)
{
	if (!InNarrative.IsValid())
	{
		return true;
	}
	InNarrative->TryGetStringField(TEXT("entryBeatId"), OutGraph.EntryBeatId);
	const TSharedPtr<FJsonObject>* Variables = nullptr;
	if (InNarrative->TryGetObjectField(TEXT("variables"), Variables) && Variables)
	{
		for (const auto& Pair : (*Variables)->Values)
		{
			FEpicodeSandboxValue Value;
			if (!FEpicodeSandboxValue::FromJson(Pair.Value, Value, OutError))
			{
				OutError = FString::Printf(TEXT("DIRECTOR_VARIABLE_INVALID: %s"), *FString(*Pair.Key));
				return false;
			}
			OutGraph.Variables.Add(FString(*Pair.Key), Value);
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* Beats = nullptr;
	if (InNarrative->TryGetArrayField(TEXT("beats"), Beats) && Beats)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Beats)
		{
			const TSharedPtr<FJsonObject>* BeatJson = nullptr;
			FEpicodeDirectorBeat Beat;
			if (!Value.IsValid() || !Value->TryGetObject(BeatJson) || !BeatJson || !FEpicodeDirectorBeat::Parse(*BeatJson, Beat, OutError))
			{
				return false;
			}
			OutGraph.Beats.Add(Beat);
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* Objectives = nullptr;
	if (InNarrative->TryGetArrayField(TEXT("objectives"), Objectives) && Objectives)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Objectives)
		{
			const TSharedPtr<FJsonObject>* ObjectiveJson = nullptr;
			FEpicodeDirectorObjective Objective;
			if (!Value.IsValid() || !Value->TryGetObject(ObjectiveJson) || !ObjectiveJson || !FEpicodeDirectorObjective::Parse(*ObjectiveJson, Objective, OutError))
			{
				return false;
			}
			OutGraph.Objectives.Add(Objective);
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* Cues = nullptr;
	if (InNarrative->TryGetArrayField(TEXT("cues"), Cues) && Cues)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Cues)
		{
			const TSharedPtr<FJsonObject>* CueJson = nullptr;
			FEpicodeDirectorCue Cue;
			if (!Value.IsValid() || !Value->TryGetObject(CueJson) || !CueJson || !FEpicodeDirectorCue::Parse(*CueJson, Cue, OutError))
			{
				return false;
			}
			OutGraph.Cues.Add(Cue);
		}
	}
	return true;
}

bool FEpicodeBeatGraph::Validate(FString& OutError) const
{
	TSet<FString> BeatIds;
	for (const FEpicodeDirectorBeat& Beat : Beats)
	{
		if (Beat.Id.IsEmpty() || BeatIds.Contains(Beat.Id))
		{
			OutError = FString::Printf(TEXT("DIRECTOR_BEAT_DUPLICATE: %s"), *Beat.Id);
			return false;
		}
		BeatIds.Add(Beat.Id);
	}
	TSet<FString> ObjectiveIds;
	for (const FEpicodeDirectorObjective& Objective : Objectives)
	{
		if (Objective.Id.IsEmpty() || ObjectiveIds.Contains(Objective.Id))
		{
			OutError = FString::Printf(TEXT("DIRECTOR_OBJECTIVE_DUPLICATE: %s"), *Objective.Id);
			return false;
		}
		ObjectiveIds.Add(Objective.Id);
	}
	TSet<FString> CueIds;
	for (const FEpicodeDirectorCue& Cue : Cues)
	{
		if (Cue.Id.IsEmpty() || CueIds.Contains(Cue.Id))
		{
			OutError = FString::Printf(TEXT("DIRECTOR_CUE_DUPLICATE: %s"), *Cue.Id);
			return false;
		}
		CueIds.Add(Cue.Id);
	}
	if (!Beats.IsEmpty())
	{
		if (EntryBeatId.IsEmpty() || !BeatIds.Contains(EntryBeatId))
		{
			OutError = TEXT("DIRECTOR_ENTRY_MISSING: entry beat is not defined");
			return false;
		}
	}
	for (const FEpicodeDirectorBeat& Beat : Beats)
	{
		for (const FString& Next : Beat.NextBeatIds)
		{
			if (!BeatIds.Contains(Next))
			{
				OutError = FString::Printf(TEXT("DIRECTOR_BEAT_REFERENCE: %s -> %s"), *Beat.Id, *Next);
				return false;
			}
		}
		for (const FString& Objective : Beat.ObjectiveIds)
		{
			if (!ObjectiveIds.Contains(Objective))
			{
				OutError = FString::Printf(TEXT("DIRECTOR_OBJECTIVE_REFERENCE: %s -> %s"), *Beat.Id, *Objective);
				return false;
			}
		}
		for (const FString& Cue : Beat.CueIds)
		{
			if (!CueIds.Contains(Cue))
			{
				OutError = FString::Printf(TEXT("DIRECTOR_CUE_REFERENCE: %s -> %s"), *Beat.Id, *Cue);
				return false;
			}
		}
	}
	for (const FEpicodeDirectorCue& Cue : Cues)
	{
		if (Cue.Trigger == EEpicodeCueTrigger::Beat && !BeatIds.Contains(Cue.BeatId))
		{
			OutError = FString::Printf(TEXT("DIRECTOR_CUE_BEAT_REFERENCE: %s -> %s"), *Cue.Id, *Cue.BeatId);
			return false;
		}
	}
	TArray<const FEpicodeSandboxCondition*> Conditions;
	for (const FEpicodeDirectorBeat& Beat : Beats)
	{
		for (const FEpicodeSandboxCondition& Condition : Beat.EntryConditions) Conditions.Add(&Condition);
		for (const FEpicodeSandboxCondition& Condition : Beat.ExitConditions) Conditions.Add(&Condition);
	}
	for (const FEpicodeDirectorObjective& Objective : Objectives)
	{
		if (Objective.bHasCondition) Conditions.Add(&Objective.Condition);
	}
	for (const FEpicodeDirectorCue& Cue : Cues)
	{
		if (Cue.bHasCondition) Conditions.Add(&Cue.Condition);
	}
	for (const FEpicodeSandboxCondition* Condition : Conditions)
	{
		if (!Condition->bAlwaysTrue && !Condition->bAlwaysFalse && !Variables.Contains(Condition->Variable))
		{
			OutError = FString::Printf(TEXT("DIRECTOR_VARIABLE_UNDECLARED: %s"), *Condition->Variable);
			return false;
		}
	}
	if (!Beats.IsEmpty())
	{
		TSet<FString> Reachable;
		TArray<FString> Pending;
		Pending.Add(EntryBeatId);
		while (Pending.Num() > 0)
		{
			const FString Current = Pending.Pop();
			if (Reachable.Contains(Current))
			{
				continue;
			}
			Reachable.Add(Current);
			if (const FEpicodeDirectorBeat* Beat = FindBeat(Current))
			{
				for (const FString& Next : Beat->NextBeatIds)
				{
					Pending.Add(Next);
				}
			}
		}
		for (const FEpicodeDirectorBeat& Beat : Beats)
		{
			if (!Reachable.Contains(Beat.Id))
			{
				OutError = FString::Printf(TEXT("DIRECTOR_BEAT_UNREACHABLE: %s"), *Beat.Id);
				return false;
			}
		}
	}
	return true;
}

const FEpicodeDirectorBeat* FEpicodeBeatGraph::FindBeat(const FString& InId) const
{
	return Beats.FindByPredicate([&InId](const FEpicodeDirectorBeat& Beat) { return Beat.Id == InId; });
}

const FEpicodeDirectorObjective* FEpicodeBeatGraph::FindObjective(const FString& InId) const
{
	return Objectives.FindByPredicate([&InId](const FEpicodeDirectorObjective& Objective) { return Objective.Id == InId; });
}

const FEpicodeDirectorCue* FEpicodeBeatGraph::FindCue(const FString& InId) const
{
	return Cues.FindByPredicate([&InId](const FEpicodeDirectorCue& Cue) { return Cue.Id == InId; });
}

TArray<FString> FEpicodeBeatGraph::GetReachableBeats() const
{
	TArray<FString> Reachable;
	if (Beats.IsEmpty() || EntryBeatId.IsEmpty())
	{
		return Reachable;
	}
	TSet<FString> Visited;
	TArray<FString> Pending;
	Pending.Add(EntryBeatId);
	while (Pending.Num() > 0)
	{
		const FString Current = Pending.Pop();
		if (Visited.Contains(Current))
		{
			continue;
		}
		Visited.Add(Current);
		if (const FEpicodeDirectorBeat* Beat = FindBeat(Current))
		{
			for (const FString& Next : Beat->NextBeatIds)
			{
				Pending.Add(Next);
			}
		}
	}
	for (const FString& Id : Visited)
	{
		Reachable.Add(Id);
	}
	Reachable.Sort();
	return Reachable;
}

TSharedPtr<FJsonObject> FEpicodeDirectorTake::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("id"), Id);
	Result->SetNumberField(TEXT("version"), Version);
	Result->SetNumberField(TEXT("inSequence"), static_cast<double>(InSequence));
	Result->SetNumberField(TEXT("outSequence"), static_cast<double>(OutSequence));
	Result->SetNumberField(TEXT("inTick"), static_cast<double>(InTick));
	Result->SetNumberField(TEXT("outTick"), static_cast<double>(OutTick));
	Result->SetStringField(TEXT("beatId"), BeatId);
	Result->SetStringField(TEXT("resolutionPreset"), ResolutionPreset);
	Result->SetStringField(TEXT("contentDigest"), ContentDigest);
	Result->SetStringField(TEXT("note"), Note);
	TArray<TSharedPtr<FJsonValue>> CameraValues;
	for (const FString& CameraId : CameraIds) CameraValues.Add(MakeShared<FJsonValueString>(CameraId));
	Result->SetArrayField(TEXT("cameraIds"), CameraValues);
	TArray<TSharedPtr<FJsonValue>> PresetValues;
	for (const FString& PresetId : CameraPresetIds) PresetValues.Add(MakeShared<FJsonValueString>(PresetId));
	Result->SetArrayField(TEXT("cameraPresetIds"), PresetValues);
	Result->SetStringField(TEXT("checksum"), ComputeChecksum());
	return Result;
}

bool FEpicodeDirectorTake::FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeDirectorTake& OutTake, FString& OutError)
{
	if (!InJson.IsValid())
	{
		OutError = TEXT("TAKE_CORRUPT: missing take");
		return false;
	}
	double InSequence = 0.0;
	double OutSequence = 0.0;
	double InTick = 0.0;
	double OutTick = 0.0;
	if (!InJson->TryGetStringField(TEXT("id"), OutTake.Id)
		|| !InJson->TryGetNumberField(TEXT("version"), OutTake.Version)
		|| !InJson->TryGetNumberField(TEXT("inSequence"), InSequence)
		|| !InJson->TryGetNumberField(TEXT("outSequence"), OutSequence)
		|| !InJson->TryGetNumberField(TEXT("inTick"), InTick)
		|| !InJson->TryGetNumberField(TEXT("outTick"), OutTick)
		|| !InJson->TryGetStringField(TEXT("beatId"), OutTake.BeatId)
		|| !InJson->TryGetStringField(TEXT("resolutionPreset"), OutTake.ResolutionPreset)
		|| !InJson->TryGetStringField(TEXT("contentDigest"), OutTake.ContentDigest))
	{
		OutError = TEXT("TAKE_CORRUPT: incomplete take");
		return false;
	}
	OutTake.InSequence = static_cast<int64>(InSequence);
	OutTake.OutSequence = static_cast<int64>(OutSequence);
	OutTake.InTick = static_cast<int64>(InTick);
	OutTake.OutTick = static_cast<int64>(OutTick);
	InJson->TryGetStringField(TEXT("note"), OutTake.Note);
	OutTake.CameraIds.Reset();
	OutTake.CameraPresetIds.Reset();
	ReadStringArray(InJson, TEXT("cameraIds"), OutTake.CameraIds, OutError);
	ReadStringArray(InJson, TEXT("cameraPresetIds"), OutTake.CameraPresetIds, OutError);
	FString ActualChecksum;
	if (!InJson->TryGetStringField(TEXT("checksum"), ActualChecksum) || ActualChecksum != OutTake.ComputeChecksum())
	{
		OutError = TEXT("TAKE_CORRUPT: checksum mismatch");
		return false;
	}
	OutTake.Checksum = ActualChecksum;
	return true;
}

FString FEpicodeDirectorTake::ComputeChecksum() const
{
	TSharedPtr<FJsonObject> Canonical = MakeShared<FJsonObject>();
	Canonical->SetStringField(TEXT("id"), Id);
	Canonical->SetNumberField(TEXT("version"), Version);
	Canonical->SetNumberField(TEXT("inSequence"), static_cast<double>(InSequence));
	Canonical->SetNumberField(TEXT("outSequence"), static_cast<double>(OutSequence));
	Canonical->SetNumberField(TEXT("inTick"), static_cast<double>(InTick));
	Canonical->SetNumberField(TEXT("outTick"), static_cast<double>(OutTick));
	Canonical->SetStringField(TEXT("beatId"), BeatId);
	Canonical->SetStringField(TEXT("resolutionPreset"), ResolutionPreset);
	Canonical->SetStringField(TEXT("contentDigest"), ContentDigest);
	Canonical->SetStringField(TEXT("note"), Note);
	TArray<TSharedPtr<FJsonValue>> CameraValues;
	for (const FString& CameraId : CameraIds) CameraValues.Add(MakeShared<FJsonValueString>(CameraId));
	Canonical->SetArrayField(TEXT("cameraIds"), CameraValues);
	TArray<TSharedPtr<FJsonValue>> PresetValues;
	for (const FString& PresetId : CameraPresetIds) PresetValues.Add(MakeShared<FJsonValueString>(PresetId));
	Canonical->SetArrayField(TEXT("cameraPresetIds"), PresetValues);
	return FEpicodeReplayChecksum::Sha1(FEpicodeSandboxJson::CanonicalString(Canonical));
}

FString FEpicodeTakeLibrary::GetTakePath(const FString& InRoot, const FString& InTakeId, int32 InVersion)
{
	return FPaths::Combine(InRoot, TEXT("takes"), FString::Printf(TEXT("%s-v%d.json"), *InTakeId, InVersion));
}

bool FEpicodeTakeLibrary::Save(const FString& InRoot, FEpicodeDirectorTake& InOutTake, FString& OutError)
{
	if (InOutTake.Id.IsEmpty() || InOutTake.Version < 1 || InOutTake.InSequence >= InOutTake.OutSequence)
	{
		OutError = TEXT("TAKE_RANGE_INVALID: invalid take metadata");
		return false;
	}
	const FString Path = GetTakePath(InRoot, InOutTake.Id, InOutTake.Version);
	if (IFileManager::Get().FileExists(*Path))
	{
		OutError = TEXT("TAKE_VERSION_IMMUTABLE: take version already exists");
		return false;
	}
	if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true))
	{
		OutError = TEXT("TAKE_SAVE_FAILED: unable to create takes directory");
		return false;
	}
	InOutTake.Checksum = InOutTake.ComputeChecksum();
	const FString TempPath = Path + TEXT(".tmp");
	if (!FFileHelper::SaveStringToFile(FEpicodeSandboxJson::Serialize(InOutTake.ToJson(), true), *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		IFileManager::Get().Delete(*TempPath);
		OutError = TEXT("TAKE_SAVE_FAILED: unable to write take");
		return false;
	}
	if (!IFileManager::Get().Move(*Path, *TempPath, true, true))
	{
		IFileManager::Get().Delete(*TempPath);
		OutError = TEXT("TAKE_SAVE_FAILED: unable to publish take");
		return false;
	}
	return true;
}

bool FEpicodeTakeLibrary::Load(const FString& InRoot, const FString& InTakeId, int32 InVersion, FEpicodeDirectorTake& OutTake, FString& OutError)
{
	TSharedPtr<FJsonObject> Json;
	if (!LoadJsonObject(GetTakePath(InRoot, InTakeId, InVersion), Json))
	{
		OutError = TEXT("TAKE_NOT_FOUND: take version is missing");
		return false;
	}
	if (!FEpicodeDirectorTake::FromJson(Json, OutTake, OutError))
	{
		return false;
	}
	if (OutTake.Id != InTakeId || OutTake.Version != InVersion)
	{
		OutError = TEXT("TAKE_CORRUPT: metadata does not match path");
		return false;
	}
	return true;
}

TArray<int32> FEpicodeTakeLibrary::ListVersions(const FString& InRoot, const FString& InTakeId)
{
	TArray<int32> Versions;
	TArray<FString> Files;
	const FString Pattern = FString::Printf(TEXT("%s-v*.json"), *InTakeId);
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(InRoot, TEXT("takes"), Pattern), true, false);
	for (const FString& File : Files)
	{
		const FString Base = FPaths::GetBaseFilename(File);
		const int32 Marker = Base.Find(TEXT("-v"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (Marker != INDEX_NONE)
		{
			const FString Number = Base.RightChop(Marker + 2);
			if (Number.IsNumeric())
			{
				Versions.Add(FCString::Atoi(*Number));
			}
		}
	}
	Versions.Sort();
	return Versions;
}

bool FEpicodeTakeLibrary::LoadLatest(const FString& InRoot, const FString& InTakeId, FEpicodeDirectorTake& OutTake, FString& OutError)
{
	const TArray<int32> Versions = ListVersions(InRoot, InTakeId);
	if (Versions.Num() == 0)
	{
		OutError = TEXT("TAKE_NOT_FOUND: no take versions");
		return false;
	}
	return Load(InRoot, InTakeId, Versions.Last(), OutTake, OutError);
}

TSharedPtr<FJsonObject> FEpicodeTakeoverLease::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("participantId"), ParticipantId);
	Result->SetStringField(TEXT("directorId"), DirectorId);
	Result->SetStringField(TEXT("originalControllerId"), OriginalControllerId);
	Result->SetStringField(TEXT("reason"), Reason);
	Result->SetStringField(TEXT("endCondition"), EndCondition);
	Result->SetNumberField(TEXT("startedTick"), static_cast<double>(StartedTick));
	Result->SetBoolField(TEXT("active"), bActive);
	return Result;
}

bool FEpicodeDirectorController::Initialize(const FEpicodeBeatGraph& InGraph, FEpicodeSandboxWorld& InWorld, FEpicodeSandboxEventStream* InEvents, const TArray<FEpicodeProductionCameraPreset>& InCameraPresets, FString& OutError)
{
	World = &InWorld;
	Events = InEvents;
	Graph = InGraph;
	CameraPresets = InCameraPresets;
	if (!Graph.Validate(OutError))
	{
		return false;
	}
	Narrative = FEpicodeSandboxNarrativeState();
	for (const auto& Pair : Graph.Variables)
	{
		Narrative.SetVariable(Pair.Key, Pair.Value);
	}
	for (const FEpicodeDirectorObjective& Objective : Graph.Objectives)
	{
		Narrative.Objectives.Add(Objective.Id, false);
	}
	Narrative.CurrentBeatId = Graph.EntryBeatId;
	World->Narrative = Narrative;
	DirectorTime = 0.0;
	TimeScale = 1.0;
	bPaused = false;
	ExecutedCues.Reset();
	CueConditionLast.Reset();
	ActiveTakeovers.Reset();
	TakeoverLeases.Reset();
	Messages.Reset();
	CommandResults.Reset();
	CommandErrors.Reset();
	ViewMode = EEpicodeDirectorViewMode::Live;
	ViewWorld = *World;
	LastCueEventSequence = Events ? Events->GetLastSequence() : 0;
	if (!Graph.EntryBeatId.IsEmpty())
	{
		if (!EnterBeatInternal(Graph.EntryBeatId, TEXT("system"), 0, OutError))
		{
			return false;
		}
		LastCueEventSequence = Events ? Events->GetLastSequence() : 0;
	}
	return true;
}

bool FEpicodeDirectorController::Initialize(const FEpicodeProductionDocument& InProduction, FEpicodeSandboxWorld& InWorld, FEpicodeSandboxEventStream* InEvents, FString& OutError)
{
	FEpicodeBeatGraph NewGraph;
	if (!FEpicodeBeatGraph::Parse(InProduction.Narrative, NewGraph, OutError))
	{
		return false;
	}
	return Initialize(NewGraph, InWorld, InEvents, InProduction.CameraPresets, OutError);
}

bool FEpicodeDirectorController::AppendEvent(const FString& InType, const FString& InActorId, const TSharedPtr<FJsonObject>& InPayload, int64 InTick, FString& OutError)
{
	if (!Events)
	{
		return true;
	}
	Events->Append(InTick, InType, InActorId, InPayload);
	return true;
}

void FEpicodeDirectorController::SyncNarrative()
{
	if (World)
	{
		World->Narrative = Narrative;
	}
}

bool FEpicodeDirectorController::EnterBeatInternal(const FString& InBeatId, const FString& InActorId, int64 InTick, FString& OutError)
{
	const FEpicodeDirectorBeat* Beat = Graph.FindBeat(InBeatId);
	if (!Beat)
	{
		OutError = FString::Printf(TEXT("DIRECTOR_BEAT_NOT_FOUND: %s"), *InBeatId);
		return false;
	}
	const FString Previous = Narrative.CurrentBeatId;
	Narrative.CurrentBeatId = InBeatId;
	SyncNarrative();
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("beatId"), InBeatId);
	Payload->SetStringField(TEXT("previousBeatId"), Previous);
	TSharedPtr<FJsonObject> Change = MakeChange(TEXT("narrative.beat"));
	Change->SetStringField(TEXT("value"), InBeatId);
	Payload->SetArrayField(TEXT("changes"), {MakeShared<FJsonValueObject>(Change)});
	return AppendEvent(TEXT("narrative.beatEntered"), InActorId, Payload, InTick, OutError);
}

bool FEpicodeDirectorController::CompleteObjectiveInternal(const FString& InObjectiveId, int64 InTick, FString& OutError)
{
	Narrative.Objectives.Add(InObjectiveId, true);
	SyncNarrative();
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("objectiveId"), InObjectiveId);
	TSharedPtr<FJsonObject> Change = MakeChange(TEXT("narrative.objective"));
	Change->SetStringField(TEXT("id"), InObjectiveId);
	Change->SetBoolField(TEXT("value"), true);
	Payload->SetArrayField(TEXT("changes"), {MakeShared<FJsonValueObject>(Change)});
	return AppendEvent(TEXT("narrative.objectiveCompleted"), TEXT("system"), Payload, InTick, OutError);
}

int32 FEpicodeDirectorController::Tick(double InDeltaSeconds, int64 InTick)
{
	if (!World || bPaused)
	{
		return 0;
	}
	Narrative = World->Narrative;
	DirectorTime += FMath::Max(0.0, InDeltaSeconds) * TimeScale;
	int32 Actions = 0;
	for (const FEpicodeDirectorObjective& Objective : Graph.Objectives)
	{
		const bool bAlreadyCompleted = Narrative.Objectives.FindRef(Objective.Id);
		if (!bAlreadyCompleted && Objective.bHasCondition && Objective.Condition.Evaluate(Narrative))
		{
			FString Error;
			if (CompleteObjectiveInternal(Objective.Id, InTick, Error))
			{
				++Actions;
			}
		}
	}
	const FEpicodeDirectorBeat* Current = Graph.FindBeat(Narrative.CurrentBeatId);
	if (Current && Current->ExitConditions.Num() > 0 && AllConditions(Current->ExitConditions, Narrative))
	{
		for (const FString& NextId : Current->NextBeatIds)
		{
			const FEpicodeDirectorBeat* Next = Graph.FindBeat(NextId);
			if (Next && AllConditions(Next->EntryConditions, Narrative))
			{
				FString Error;
				if (EnterBeatInternal(NextId, TEXT("system"), InTick, Error))
				{
					++Actions;
				}
				break;
			}
		}
	}
	Actions += RunCues(InTick);
	return Actions;
}

int32 FEpicodeDirectorController::RunCues(int64 InTick)
{
	const int64 NowSequence = Events ? Events->GetLastSequence() : LastCueEventSequence;
	TArray<FString> EventTypes;
	if (Events)
	{
		for (const FEpicodeSandboxEvent& Event : Events->GetEvents())
		{
			if (Event.Sequence > LastCueEventSequence)
			{
				EventTypes.Add(Event.Type);
			}
		}
	}
	int32 Actions = 0;
	for (const FEpicodeDirectorCue& Cue : Graph.Cues)
	{
		if (Cue.bOnce && ExecutedCues.Contains(Cue.Id))
		{
			continue;
		}
		bool bTrigger = false;
		if (Cue.Trigger == EEpicodeCueTrigger::Time)
		{
			bTrigger = DirectorTime + KINDA_SMALL_NUMBER >= Cue.Time;
		}
		else if (Cue.Trigger == EEpicodeCueTrigger::Event)
		{
			bTrigger = EventTypes.Contains(Cue.EventType);
		}
		else if (Cue.Trigger == EEpicodeCueTrigger::Beat)
		{
			bTrigger = Narrative.CurrentBeatId == Cue.BeatId;
		}
		else if (Cue.Trigger == EEpicodeCueTrigger::Condition)
		{
			const bool bNow = Cue.Condition.Evaluate(Narrative);
			const bool bBefore = CueConditionLast.FindRef(Cue.Id);
			CueConditionLast.Add(Cue.Id, bNow);
			bTrigger = bNow && !bBefore;
			if (!bNow && !Cue.bOnce)
			{
				ExecutedCues.Remove(Cue.Id);
			}
		}
		if (!bTrigger)
		{
			continue;
		}
		FString FirstError;
		int32 Executed = 0;
		for (const FString& Action : Cue.Actions)
		{
			FString Error;
			if (ExecuteCueAction(Action, Cue, InTick, Error))
			{
				++Executed;
				++Actions;
			}
			else if (FirstError.IsEmpty())
			{
				FirstError = Error;
			}
		}
		const FString Key = FString::Printf(TEXT("cue:%s:%lld"), *Cue.Id, InTick);
		if (FirstError.IsEmpty())
		{
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("cueId"), Cue.Id);
			Payload->SetNumberField(TEXT("actions"), Executed);
			Payload->SetBoolField(TEXT("idempotent"), true);
			FString Error;
			AppendEvent(TEXT("director.cueExecuted"), TEXT("director"), Payload, InTick, Error);
		}
		else
		{
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("cueId"), Cue.Id);
			Payload->SetStringField(TEXT("reason"), FirstError);
			FString Error;
			AppendEvent(TEXT("director.cueFailed"), TEXT("director"), Payload, InTick, Error);
		}
		ExecutedCues.Add(Cue.Id);
	}
	LastCueEventSequence = NowSequence;
	return Actions;
}

bool FEpicodeDirectorController::ExecuteCueAction(const FString& InAction, const FEpicodeDirectorCue& InCue, int64 InTick, FString& OutError)
{
	FString Op;
	FString Value;
	if (!InAction.Split(TEXT(":"), &Op, &Value))
	{
		Op = InAction;
	}
	Op = Op.ToLower();
	if (Op == TEXT("camera"))
	{
		if (!IsCameraKnown(Value, Value))
		{
			OutError = FString::Printf(TEXT("DIRECTOR_CAMERA_NOT_FOUND: %s"), *Value);
			return false;
		}
		if (World && World->FindEntity(Value))
		{
			CameraId = Value;
			CameraPresetId.Reset();
		}
		else
		{
			CameraPresetId = Value;
			CameraId.Reset();
		}
		return true;
	}
	if (Op == TEXT("environment"))
	{
		Environment = MakeShared<FJsonObject>();
		Environment->SetStringField(TEXT("preset"), Value);
		return true;
	}
	if (Op == TEXT("spawn"))
	{
		TArray<FString> Parts;
		Value.ParseIntoArray(Parts, TEXT(":"), false);
		if (Parts.Num() < 1 || Parts[0].IsEmpty())
		{
			OutError = TEXT("DIRECTOR_CUE_ACTION_INVALID: spawn needs entity id");
			return false;
		}
		FEpicodeSandboxEntity Entity;
		Entity.Id = Parts[0];
		Entity.Kind = EEpicodeSandboxEntityKind::Prop;
		if (Parts.Num() > 1 && !ParseSandboxEntityKind(Parts[1], Entity.Kind))
		{
			OutError = FString::Printf(TEXT("DIRECTOR_CUE_ACTION_INVALID: unknown kind %s"), *Parts[1]);
			return false;
		}
		if (Parts.Num() > 2)
		{
			Entity.ArchetypeId = Parts[2];
		}
		if (!World || !World->AddEntity(Entity, OutError))
		{
			return false;
		}
		return true;
	}
	if (Op == TEXT("remove"))
	{
		if (!World || !World->RemoveEntity(Value, OutError))
		{
			return false;
		}
		return true;
	}
	if (Op == TEXT("message"))
	{
		Messages.Add(Value);
		return true;
	}
	if (Op == TEXT("pause"))
	{
		bPaused = true;
		return true;
	}
	if (Op == TEXT("slowmotion") || Op == TEXT("slowMotion"))
	{
		const double Scale = FCString::Atod(*Value);
		if (Scale <= 0.0)
		{
			OutError = TEXT("DIRECTOR_CUE_ACTION_INVALID: invalid time scale");
			return false;
		}
		TimeScale = FMath::Clamp(Scale, 0.1, 4.0);
		return true;
	}
	if (Op == TEXT("takeover"))
	{
		const FEpicodeSandboxParticipant* Participant = World ? World->FindParticipant(Value) : nullptr;
		if (!Participant)
		{
			OutError = FString::Printf(TEXT("TAKEOVER_TARGET_NOT_FOUND: %s"), *Value);
			return false;
		}
		if (ActiveTakeovers.Contains(Value))
		{
			OutError = FString::Printf(TEXT("DIRECTOR_TAKEOVER_CONFLICT: %s"), *Value);
			return false;
		}
		FEpicodeTakeoverLease Lease;
		Lease.ParticipantId = Value;
		Lease.DirectorId = TEXT("director");
		Lease.Reason = FString::Printf(TEXT("cue:%s"), *InCue.Id);
		Lease.StartedTick = InTick;
		Lease.bActive = true;
		ActiveTakeovers.Add(Value, Lease);
		TakeoverLeases.Add(Lease);
		FEpicodeSandboxParticipant* Target = World->FindParticipant(Value);
		Target->bTakeover = true;
		Target->bConnected = false;
		return true;
	}
	if (Op == TEXT("setobjective"))
	{
		if (!Graph.FindObjective(Value))
		{
			OutError = FString::Printf(TEXT("DIRECTOR_OBJECTIVE_NOT_FOUND: %s"), *Value);
			return false;
		}
		Narrative.CurrentObjectiveId = Value;
		SyncNarrative();
		return true;
	}
	if (Op == TEXT("enterbeat"))
	{
		return EnterBeatInternal(Value, TEXT("cue"), InTick, OutError);
	}
	OutError = FString::Printf(TEXT("DIRECTOR_CUE_ACTION_UNSUPPORTED: %s"), *InAction);
	return false;
}

bool FEpicodeDirectorController::IsCameraKnown(const FString& InCameraId, const FString& InPresetId) const
{
	if (!InCameraId.IsEmpty())
	{
		const FEpicodeSandboxEntity* Entity = World ? World->FindEntity(InCameraId) : nullptr;
		if (Entity && Entity->Kind == EEpicodeSandboxEntityKind::Camera)
		{
			return true;
		}
	}
	if (!InPresetId.IsEmpty())
	{
		for (const FEpicodeProductionCameraPreset& Preset : CameraPresets)
		{
			if (Preset.Id == InPresetId)
			{
				return true;
			}
		}
	}
	return false;
}

bool FEpicodeDirectorController::CanDirect(const FString& InActorId, FString& OutError) const
{
	if (!World)
	{
		OutError = TEXT("DIRECTOR_NOT_INITIALIZED: no world");
		return false;
	}
	const FEpicodeSandboxParticipant* Participant = World->FindParticipant(InActorId);
	if (!Participant)
	{
		OutError = FString::Printf(TEXT("PARTICIPANT_NOT_FOUND: %s"), *InActorId);
		return false;
	}
	if (Participant->Kind != EEpicodeSandboxParticipantKind::Owner && Participant->Kind != EEpicodeSandboxParticipantKind::Director)
	{
		OutError = TEXT("FORBIDDEN: director command requires owner or director");
		return false;
	}
	return true;
}

bool FEpicodeDirectorController::BeginCommand(const FString& InActorId, const FString& InKey, FEpicodeDirectorCommandResult& OutResult, bool bAllowNonLive)
{
	OutResult = FEpicodeDirectorCommandResult();
	if (InKey.IsEmpty())
	{
		OutResult.Error = TEXT("DIRECTOR_KEY_REQUIRED: idempotency key is required");
		return false;
	}
	if (const bool* Cached = CommandResults.Find(InKey))
	{
		OutResult.bSuccess = *Cached;
		OutResult.bDuplicate = true;
		OutResult.Error = CommandErrors.FindRef(InKey);
		return false;
	}
	if (!bAllowNonLive && ViewMode != EEpicodeDirectorViewMode::Live)
	{
		OutResult.Error = TEXT("REPLAY_READ_ONLY: ghost and resim views are read only");
		return false;
	}
	FString Error;
	if (!CanDirect(InActorId, Error))
	{
		OutResult.Error = Error;
		return false;
	}
	return true;
}

void FEpicodeDirectorController::EndCommand(const FString& InKey, bool bInSuccess, const FString& InError, FEpicodeDirectorCommandResult& OutResult)
{
	OutResult.bSuccess = bInSuccess;
	OutResult.Error = InError;
	CommandResults.Add(InKey, bInSuccess);
	CommandErrors.Add(InKey, InError);
}

FEpicodeDirectorCommandResult FEpicodeDirectorController::Pause(const FString& InActorId, const FString& InKey, int64 InTick)
{
	FEpicodeDirectorCommandResult Result;
	if (!BeginCommand(InActorId, InKey, Result)) return Result;
	bool bSuccess = true;
	FString Error;
	if (!bPaused)
	{
		bPaused = true;
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("key"), InKey);
		bSuccess = AppendEvent(TEXT("director.paused"), InActorId, Payload, InTick, Error);
	}
	EndCommand(InKey, bSuccess, Error, Result);
	return Result;
}

FEpicodeDirectorCommandResult FEpicodeDirectorController::Resume(const FString& InActorId, const FString& InKey, int64 InTick)
{
	FEpicodeDirectorCommandResult Result;
	if (!BeginCommand(InActorId, InKey, Result)) return Result;
	bool bSuccess = true;
	FString Error;
	if (bPaused)
	{
		bPaused = false;
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("key"), InKey);
		bSuccess = AppendEvent(TEXT("director.resumed"), InActorId, Payload, InTick, Error);
	}
	EndCommand(InKey, bSuccess, Error, Result);
	return Result;
}

FEpicodeDirectorCommandResult FEpicodeDirectorController::SetTimeScale(const FString& InActorId, const FString& InKey, double InScale, int64 InTick)
{
	FEpicodeDirectorCommandResult Result;
	if (!BeginCommand(InActorId, InKey, Result)) return Result;
	FString Error;
	if (InScale < 0.1 || InScale > 4.0)
	{
		Error = TEXT("DIRECTOR_TIMESCALE_INVALID: scale must be between 0.1 and 4.0");
		EndCommand(InKey, false, Error, Result);
		return Result;
	}
	TimeScale = InScale;
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetNumberField(TEXT("scale"), TimeScale);
	Payload->SetStringField(TEXT("key"), InKey);
	const bool bSuccess = AppendEvent(TEXT("director.timeScaleChanged"), InActorId, Payload, InTick, Error);
	EndCommand(InKey, bSuccess, Error, Result);
	return Result;
}

FEpicodeDirectorCommandResult FEpicodeDirectorController::SwitchCamera(const FString& InActorId, const FString& InKey, const FString& InCameraId, const FString& InPresetId, int64 InTick)
{
	FEpicodeDirectorCommandResult Result;
	if (!BeginCommand(InActorId, InKey, Result)) return Result;
	FString Error;
	if (InCameraId.IsEmpty() && InPresetId.IsEmpty())
	{
		Error = TEXT("DIRECTOR_CAMERA_REQUIRED: camera or preset is required");
		EndCommand(InKey, false, Error, Result);
		return Result;
	}
	if (!IsCameraKnown(InCameraId, InPresetId))
	{
		Error = FString::Printf(TEXT("DIRECTOR_CAMERA_NOT_FOUND: %s%s"), *InCameraId, *InPresetId);
		EndCommand(InKey, false, Error, Result);
		return Result;
	}
	CameraId = InCameraId;
	CameraPresetId = InPresetId;
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("cameraId"), CameraId);
	Payload->SetStringField(TEXT("presetId"), CameraPresetId);
	Payload->SetStringField(TEXT("key"), InKey);
	const bool bSuccess = AppendEvent(TEXT("director.cameraChanged"), InActorId, Payload, InTick, Error);
	EndCommand(InKey, bSuccess, Error, Result);
	return Result;
}

FEpicodeDirectorCommandResult FEpicodeDirectorController::SetEnvironment(const FString& InActorId, const FString& InKey, const TSharedPtr<FJsonObject>& InEnvironment, int64 InTick)
{
	FEpicodeDirectorCommandResult Result;
	if (!BeginCommand(InActorId, InKey, Result)) return Result;
	Environment = FEpicodeSandboxJson::CloneObject(InEnvironment);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetObjectField(TEXT("environment"), Environment);
	FString Error;
	const bool bSuccess = AppendEvent(TEXT("director.environmentChanged"), InActorId, Payload, InTick, Error);
	EndCommand(InKey, bSuccess, Error, Result);
	return Result;
}

FEpicodeDirectorCommandResult FEpicodeDirectorController::SpawnEntity(const FString& InActorId, const FString& InKey, const FEpicodeSandboxEntity& InEntity, int64 InTick)
{
	FEpicodeDirectorCommandResult Result;
	if (!BeginCommand(InActorId, InKey, Result)) return Result;
	FString Error;
	if (!World || !World->AddEntity(InEntity, Error))
	{
		EndCommand(InKey, false, Error, Result);
		return Result;
	}
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("entityId"), InEntity.Id);
	TSharedPtr<FJsonObject> Change = MakeChange(TEXT("entity.add"));
	Change->SetObjectField(TEXT("entity"), InEntity.ToJson());
	Payload->SetArrayField(TEXT("changes"), {MakeShared<FJsonValueObject>(Change)});
	const bool bSuccess = AppendEvent(TEXT("director.entitySpawned"), InActorId, Payload, InTick, Error);
	if (!bSuccess)
	{
		World->RemoveEntity(InEntity.Id, Error);
	}
	EndCommand(InKey, bSuccess, Error, Result);
	return Result;
}

FEpicodeDirectorCommandResult FEpicodeDirectorController::RemoveEntity(const FString& InActorId, const FString& InKey, const FString& InEntityId, int64 InTick)
{
	FEpicodeDirectorCommandResult Result;
	if (!BeginCommand(InActorId, InKey, Result)) return Result;
	FString Error;
	if (!World || !World->RemoveEntity(InEntityId, Error))
	{
		EndCommand(InKey, false, Error, Result);
		return Result;
	}
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("entityId"), InEntityId);
	TSharedPtr<FJsonObject> Change = MakeChange(TEXT("entity.remove"));
	Change->SetStringField(TEXT("id"), InEntityId);
	Payload->SetArrayField(TEXT("changes"), {MakeShared<FJsonValueObject>(Change)});
	const bool bSuccess = AppendEvent(TEXT("director.entityRemoved"), InActorId, Payload, InTick, Error);
	EndCommand(InKey, bSuccess, Error, Result);
	return Result;
}

FEpicodeDirectorCommandResult FEpicodeDirectorController::SetObjective(const FString& InActorId, const FString& InKey, const FString& InObjectiveId, int64 InTick)
{
	FEpicodeDirectorCommandResult Result;
	if (!BeginCommand(InActorId, InKey, Result)) return Result;
	FString Error;
	if (!Graph.FindObjective(InObjectiveId))
	{
		Error = FString::Printf(TEXT("DIRECTOR_OBJECTIVE_NOT_FOUND: %s"), *InObjectiveId);
		EndCommand(InKey, false, Error, Result);
		return Result;
	}
	Narrative.CurrentObjectiveId = InObjectiveId;
	Narrative.Objectives.FindOrAdd(InObjectiveId) = false;
	SyncNarrative();
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("objectiveId"), InObjectiveId);
	TSharedPtr<FJsonObject> Change = MakeChange(TEXT("narrative.objectiveCurrent"));
	Change->SetStringField(TEXT("value"), InObjectiveId);
	Payload->SetArrayField(TEXT("changes"), {MakeShared<FJsonValueObject>(Change)});
	const bool bSuccess = AppendEvent(TEXT("narrative.objectiveSet"), InActorId, Payload, InTick, Error);
	EndCommand(InKey, bSuccess, Error, Result);
	return Result;
}

FEpicodeDirectorCommandResult FEpicodeDirectorController::EnterBeat(const FString& InActorId, const FString& InKey, const FString& InBeatId, int64 InTick)
{
	FEpicodeDirectorCommandResult Result;
	if (!BeginCommand(InActorId, InKey, Result)) return Result;
	FString Error;
	const bool bSuccess = EnterBeatInternal(InBeatId, InActorId, InTick, Error);
	EndCommand(InKey, bSuccess, Error, Result);
	return Result;
}

FEpicodeDirectorCommandResult FEpicodeDirectorController::Message(const FString& InActorId, const FString& InKey, const FString& InText, int64 InTick)
{
	FEpicodeDirectorCommandResult Result;
	if (!BeginCommand(InActorId, InKey, Result)) return Result;
	if (InText.IsEmpty())
	{
		EndCommand(InKey, false, TEXT("DIRECTOR_MESSAGE_EMPTY: message is empty"), Result);
		return Result;
	}
	Messages.Add(InText);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("text"), InText);
	FString Error;
	const bool bSuccess = AppendEvent(TEXT("narrative.message"), InActorId, Payload, InTick, Error);
	EndCommand(InKey, bSuccess, Error, Result);
	return Result;
}

FEpicodeDirectorCommandResult FEpicodeDirectorController::AcquireTakeover(const FString& InActorId, const FString& InKey, const FString& InParticipantId, const FString& InReason, const FString& InEndCondition, int64 InTick)
{
	FEpicodeDirectorCommandResult Result;
	if (!BeginCommand(InActorId, InKey, Result)) return Result;
	if (!World)
	{
		EndCommand(InKey, false, TEXT("DIRECTOR_NOT_INITIALIZED: no world"), Result);
		return Result;
	}
	FEpicodeSandboxParticipant* Target = World->FindParticipant(InParticipantId);
	if (!Target)
	{
		EndCommand(InKey, false, FString::Printf(TEXT("TAKEOVER_TARGET_NOT_FOUND: %s"), *InParticipantId), Result);
		return Result;
	}
	if (Target->Kind != EEpicodeSandboxParticipantKind::Performer
		&& Target->Kind != EEpicodeSandboxParticipantKind::Agent
		&& Target->Kind != EEpicodeSandboxParticipantKind::Npc)
	{
		EndCommand(InKey, false, TEXT("TAKEOVER_TARGET_INVALID: only performer, agent or npc can be taken over"), Result);
		return Result;
	}
	if (const FEpicodeTakeoverLease* Existing = ActiveTakeovers.Find(InParticipantId))
	{
		if (Existing->DirectorId == InActorId)
		{
			Result.bSuccess = true;
			Result.bDuplicate = true;
			EndCommand(InKey, true, FString(), Result);
			return Result;
		}
		EndCommand(InKey, false, TEXT("DIRECTOR_TAKEOVER_CONFLICT: participant is already taken over"), Result);
		return Result;
	}
	FEpicodeTakeoverLease Lease;
	Lease.ParticipantId = InParticipantId;
	Lease.DirectorId = InActorId;
	Lease.OriginalControllerId = Target->ConnectedControllerId.IsEmpty() ? Target->Id : Target->ConnectedControllerId;
	Lease.Reason = InReason;
	Lease.EndCondition = InEndCondition;
	Lease.StartedTick = InTick;
	Lease.bActive = true;
	ActiveTakeovers.Add(InParticipantId, Lease);
	TakeoverLeases.Add(Lease);
	Target->bTakeover = true;
	Target->bConnected = false;
	Target->ConnectedControllerId = InActorId;
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("participantId"), InParticipantId);
	Payload->SetStringField(TEXT("directorId"), InActorId);
	Payload->SetStringField(TEXT("reason"), InReason);
	TSharedPtr<FJsonObject> Change = MakeChange(TEXT("participant.takeover"));
	Change->SetStringField(TEXT("participantId"), InParticipantId);
	Change->SetBoolField(TEXT("value"), true);
	Payload->SetArrayField(TEXT("changes"), {MakeShared<FJsonValueObject>(Change)});
	FString Error;
	const bool bSuccess = AppendEvent(TEXT("director.takeoverStarted"), InActorId, Payload, InTick, Error);
	EndCommand(InKey, bSuccess, Error, Result);
	return Result;
}

FEpicodeDirectorCommandResult FEpicodeDirectorController::ReleaseTakeover(const FString& InActorId, const FString& InKey, const FString& InParticipantId, int64 InTick)
{
	FEpicodeDirectorCommandResult Result;
	if (!BeginCommand(InActorId, InKey, Result)) return Result;
	FEpicodeTakeoverLease* Lease = ActiveTakeovers.Find(InParticipantId);
	if (!Lease)
	{
		EndCommand(InKey, false, FString::Printf(TEXT("TAKEOVER_NOT_FOUND: %s"), *InParticipantId), Result);
		return Result;
	}
	if (Lease->DirectorId != InActorId)
	{
		const FEpicodeSandboxParticipant* Actor = World ? World->FindParticipant(InActorId) : nullptr;
		if (!Actor || Actor->Kind != EEpicodeSandboxParticipantKind::Owner)
		{
			EndCommand(InKey, false, TEXT("FORBIDDEN: only the owning director or owner can release takeover"), Result);
			return Result;
		}
	}
	Lease->bActive = false;
	FEpicodeTakeoverLease Released = *Lease;
	ActiveTakeovers.Remove(InParticipantId);
	if (World)
	{
		if (FEpicodeSandboxParticipant* Target = World->FindParticipant(InParticipantId))
		{
			Target->bTakeover = false;
			Target->bConnected = true;
			Target->ConnectedControllerId = Released.OriginalControllerId;
		}
	}
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("participantId"), InParticipantId);
	Payload->SetStringField(TEXT("directorId"), Released.DirectorId);
	Payload->SetStringField(TEXT("originalControllerId"), Released.OriginalControllerId);
	Payload->SetStringField(TEXT("reason"), Released.Reason);
	TSharedPtr<FJsonObject> Change = MakeChange(TEXT("participant.takeover"));
	Change->SetStringField(TEXT("participantId"), InParticipantId);
	Change->SetBoolField(TEXT("value"), false);
	Payload->SetArrayField(TEXT("changes"), {MakeShared<FJsonValueObject>(Change)});
	FString Error;
	const bool bSuccess = AppendEvent(TEXT("director.takeoverEnded"), InActorId, Payload, InTick, Error);
	EndCommand(InKey, bSuccess, Error, Result);
	return Result;
}

bool FEpicodeDirectorController::ValidateTake(const FEpicodeDirectorTake& InTake, FString& OutError) const
{
	if (InTake.Id.IsEmpty())
	{
		OutError = TEXT("TAKE_ID_REQUIRED: take id is empty");
		return false;
	}
	if (InTake.InSequence >= InTake.OutSequence)
	{
		OutError = TEXT("TAKE_RANGE_INVALID: out point must be after in point");
		return false;
	}
	for (const FString& TakeCameraId : InTake.CameraIds)
	{
		const FEpicodeSandboxEntity* Entity = World ? World->FindEntity(TakeCameraId) : nullptr;
		if (!Entity || Entity->Kind != EEpicodeSandboxEntityKind::Camera)
		{
			OutError = FString::Printf(TEXT("TAKE_DEPENDENCY_MISSING: camera %s"), *TakeCameraId);
			return false;
		}
	}
	for (const FString& PresetId : InTake.CameraPresetIds)
	{
		bool bFound = false;
		for (const FEpicodeProductionCameraPreset& Preset : CameraPresets)
		{
			if (Preset.Id == PresetId)
			{
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			OutError = FString::Printf(TEXT("TAKE_DEPENDENCY_MISSING: preset %s"), *PresetId);
			return false;
		}
	}
	return true;
}

bool FEpicodeDirectorController::CreateTake(const FString& InActorId, const FString& InKey, const FString& InRoot, FEpicodeDirectorTake& InOutTake, FEpicodeDirectorCommandResult& OutResult, int64 InTick)
{
	OutResult = FEpicodeDirectorCommandResult();
	if (!BeginCommand(InActorId, InKey, OutResult)) return false;
	if (InOutTake.BeatId.IsEmpty())
	{
		InOutTake.BeatId = Narrative.CurrentBeatId;
	}
	if (InOutTake.ResolutionPreset.IsEmpty())
	{
		InOutTake.ResolutionPreset = TEXT("1080p");
	}
	InOutTake.ContentDigest = World ? FEpicodeSandboxDigest::ComputeWorldDigest(*World) : FString();
	InOutTake.Version = 1;
	FString Error;
	if (!ValidateTake(InOutTake, Error))
	{
		EndCommand(InKey, false, Error, OutResult);
		return false;
	}
	if (!FEpicodeTakeLibrary::Save(InRoot, InOutTake, Error))
	{
		EndCommand(InKey, false, Error, OutResult);
		return false;
	}
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("takeId"), InOutTake.Id);
	Payload->SetNumberField(TEXT("version"), InOutTake.Version);
	Payload->SetNumberField(TEXT("inSequence"), static_cast<double>(InOutTake.InSequence));
	Payload->SetNumberField(TEXT("outSequence"), static_cast<double>(InOutTake.OutSequence));
	AppendEvent(TEXT("director.takeCreated"), InActorId, Payload, InTick, Error);
	EndCommand(InKey, true, FString(), OutResult);
	return true;
}

bool FEpicodeDirectorController::ReviseTake(const FString& InActorId, const FString& InKey, const FString& InRoot, const FString& InTakeId, FEpicodeDirectorTake InDraft, FEpicodeDirectorTake& OutTake, FEpicodeDirectorCommandResult& OutResult, int64 InTick)
{
	OutResult = FEpicodeDirectorCommandResult();
	if (!BeginCommand(InActorId, InKey, OutResult)) return false;
	FEpicodeDirectorTake Latest;
	FString Error;
	if (!FEpicodeTakeLibrary::LoadLatest(InRoot, InTakeId, Latest, Error))
	{
		EndCommand(InKey, false, Error, OutResult);
		return false;
	}
	InDraft.Id = InTakeId;
	InDraft.Version = Latest.Version + 1;
	if (InDraft.BeatId.IsEmpty())
	{
		InDraft.BeatId = Narrative.CurrentBeatId;
	}
	if (InDraft.ResolutionPreset.IsEmpty())
	{
		InDraft.ResolutionPreset = Latest.ResolutionPreset;
	}
	InDraft.ContentDigest = World ? FEpicodeSandboxDigest::ComputeWorldDigest(*World) : FString();
	if (!ValidateTake(InDraft, Error) || !FEpicodeTakeLibrary::Save(InRoot, InDraft, Error))
	{
		EndCommand(InKey, false, Error, OutResult);
		return false;
	}
	OutTake = InDraft;
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("takeId"), OutTake.Id);
	Payload->SetNumberField(TEXT("version"), OutTake.Version);
	AppendEvent(TEXT("director.takeCreated"), InActorId, Payload, InTick, Error);
	EndCommand(InKey, true, FString(), OutResult);
	return true;
}

FEpicodeDirectorCommandResult FEpicodeDirectorController::SetView(const FString& InActorId, const FString& InKey, EEpicodeDirectorViewMode InMode, const FString& InReplayRoot, int64 InSequence)
{
	FEpicodeDirectorCommandResult Result;
	if (!BeginCommand(InActorId, InKey, Result, true)) return Result;
	if (InMode == EEpicodeDirectorViewMode::Live)
	{
		ViewMode = InMode;
		ReplayRoot.Reset();
		ViewSequence = 0;
		UpdateViewWorld();
		EndCommand(InKey, true, FString(), Result);
		return Result;
	}
	if (InReplayRoot.IsEmpty())
	{
		EndCommand(InKey, false, TEXT("REPLAY_HISTORY_UNAVAILABLE: replay root is empty"), Result);
		return Result;
	}
	FEpicodeReplayGhost Ghost;
	FEpicodeReplayResimResult ReplayResult;
	FString Error;
	if (!Ghost.Open(InReplayRoot, Error) || !Ghost.Seek(InSequence, ReplayResult, Error))
	{
		EndCommand(InKey, false, Error, Result);
		return Result;
	}
	ReplayRoot = InReplayRoot;
	ViewMode = InMode;
	ViewSequence = InSequence;
	ViewWorld = ReplayResult.World;
	EndCommand(InKey, true, FString(), Result);
	return Result;
}

void FEpicodeDirectorController::UpdateViewWorld()
{
	if (ViewMode == EEpicodeDirectorViewMode::Live && World)
	{
		ViewWorld = *World;
	}
}

const FEpicodeSandboxWorld& FEpicodeDirectorController::GetViewWorld() const
{
	return ViewMode == EEpicodeDirectorViewMode::Live && World ? *World : ViewWorld;
}

bool FEpicodeDirectorController::CreateBranch(const FString& InActorId, const FString& InKey, const FString& InParentRoot, const FString& InBranchRoot, const FString& InBranchId, int64 InForkSequence, FEpicodeDirectorCommandResult& OutResult)
{
	OutResult = FEpicodeDirectorCommandResult();
	if (!BeginCommand(InActorId, InKey, OutResult)) return false;
	const FEpicodeSandboxParticipant* Actor = World ? World->FindParticipant(InActorId) : nullptr;
	FString Error;
	if (!Actor || !FEpicodeReplayBranch::Create(InParentRoot, InBranchRoot, InBranchId, InForkSequence, InActorId, Actor->Kind, Error))
	{
		EndCommand(InKey, false, Error.IsEmpty() ? TEXT("BRANCH_CREATE_FAILED") : Error, OutResult);
		return false;
	}
	EndCommand(InKey, true, FString(), OutResult);
	return true;
}
