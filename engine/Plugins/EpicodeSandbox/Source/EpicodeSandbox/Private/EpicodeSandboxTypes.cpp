#include "EpicodeSandboxTypes.h"

#include "Dom/JsonValue.h"
#include "EpicodeSandboxJson.h"

namespace
{
	bool TryReadVector(const TSharedPtr<FJsonObject>& InObject, const TCHAR* InField, FVector& OutValue)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!InObject->TryGetArrayField(InField, Values) || !Values || Values->Num() != 3)
		{
			return false;
		}
		OutValue = FVector((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
		return true;
	}

	bool TryReadRotator(const TSharedPtr<FJsonObject>& InObject, const TCHAR* InField, FRotator& OutValue)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!InObject->TryGetArrayField(InField, Values) || !Values || Values->Num() != 3)
		{
			return false;
		}
		OutValue = FRotator((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
		return true;
	}

	TSharedPtr<FJsonObject> MakeTransformJson(const FVector& InLocation, const FRotator& InRotation)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("location"), {MakeShared<FJsonValueNumber>(InLocation.X), MakeShared<FJsonValueNumber>(InLocation.Y), MakeShared<FJsonValueNumber>(InLocation.Z)});
		Result->SetArrayField(TEXT("rotation"), {MakeShared<FJsonValueNumber>(InRotation.Pitch), MakeShared<FJsonValueNumber>(InRotation.Yaw), MakeShared<FJsonValueNumber>(InRotation.Roll)});
		return Result;
	}

	bool IsKnownEntityField(const FString& InKey)
	{
		return InKey == TEXT("id") || InKey == TEXT("kind") || InKey == TEXT("archetypeId")
			|| InKey == TEXT("location") || InKey == TEXT("rotation") || InKey == TEXT("visible")
			|| InKey == TEXT("ownerId") || InKey == TEXT("inventory") || InKey == TEXT("data");
	}
}

FString LexToString(EEpicodeSandboxEntityKind InKind)
{
	switch (InKind)
	{
	case EEpicodeSandboxEntityKind::Character: return TEXT("character");
	case EEpicodeSandboxEntityKind::Npc: return TEXT("npc");
	case EEpicodeSandboxEntityKind::Prop: return TEXT("prop");
	case EEpicodeSandboxEntityKind::Item: return TEXT("item");
	case EEpicodeSandboxEntityKind::Build: return TEXT("build");
	case EEpicodeSandboxEntityKind::Resource: return TEXT("resource");
	case EEpicodeSandboxEntityKind::Trigger: return TEXT("trigger");
	case EEpicodeSandboxEntityKind::Light: return TEXT("light");
	case EEpicodeSandboxEntityKind::Camera: return TEXT("camera");
	default: return TEXT("prop");
	}
}

bool ParseSandboxEntityKind(const FString& InValue, EEpicodeSandboxEntityKind& OutKind)
{
	const FString Value = InValue.ToLower();
	if (Value == TEXT("character")) { OutKind = EEpicodeSandboxEntityKind::Character; return true; }
	if (Value == TEXT("npc")) { OutKind = EEpicodeSandboxEntityKind::Npc; return true; }
	if (Value == TEXT("prop")) { OutKind = EEpicodeSandboxEntityKind::Prop; return true; }
	if (Value == TEXT("item")) { OutKind = EEpicodeSandboxEntityKind::Item; return true; }
	if (Value == TEXT("build")) { OutKind = EEpicodeSandboxEntityKind::Build; return true; }
	if (Value == TEXT("resource")) { OutKind = EEpicodeSandboxEntityKind::Resource; return true; }
	if (Value == TEXT("trigger")) { OutKind = EEpicodeSandboxEntityKind::Trigger; return true; }
	if (Value == TEXT("light")) { OutKind = EEpicodeSandboxEntityKind::Light; return true; }
	if (Value == TEXT("camera")) { OutKind = EEpicodeSandboxEntityKind::Camera; return true; }
	return false;
}

FString LexToString(EEpicodeSandboxParticipantKind InKind)
{
	switch (InKind)
	{
	case EEpicodeSandboxParticipantKind::Performer: return TEXT("performer");
	case EEpicodeSandboxParticipantKind::Agent: return TEXT("agent");
	case EEpicodeSandboxParticipantKind::Viewer: return TEXT("viewer");
	case EEpicodeSandboxParticipantKind::Director: return TEXT("director");
	case EEpicodeSandboxParticipantKind::Npc: return TEXT("npc");
	case EEpicodeSandboxParticipantKind::Owner: return TEXT("owner");
	default: return TEXT("performer");
	}
}

bool ParseSandboxParticipantKind(const FString& InValue, EEpicodeSandboxParticipantKind& OutKind)
{
	const FString Value = InValue.ToLower();
	if (Value == TEXT("performer")) { OutKind = EEpicodeSandboxParticipantKind::Performer; return true; }
	if (Value == TEXT("agent")) { OutKind = EEpicodeSandboxParticipantKind::Agent; return true; }
	if (Value == TEXT("viewer")) { OutKind = EEpicodeSandboxParticipantKind::Viewer; return true; }
	if (Value == TEXT("director")) { OutKind = EEpicodeSandboxParticipantKind::Director; return true; }
	if (Value == TEXT("npc")) { OutKind = EEpicodeSandboxParticipantKind::Npc; return true; }
	if (Value == TEXT("owner")) { OutKind = EEpicodeSandboxParticipantKind::Owner; return true; }
	return false;
}

FString LexToString(EEpicodeSandboxCommandCategory InCategory)
{
	switch (InCategory)
	{
	case EEpicodeSandboxCommandCategory::Input: return TEXT("input");
	case EEpicodeSandboxCommandCategory::Rule: return TEXT("rule");
	case EEpicodeSandboxCommandCategory::Projection: return TEXT("projection");
	case EEpicodeSandboxCommandCategory::System: return TEXT("system");
	default: return TEXT("input");
	}
}

int32 GetSandboxCommandSortOrder(EEpicodeSandboxCommandCategory InCategory)
{
	switch (InCategory)
	{
	case EEpicodeSandboxCommandCategory::Input: return 0;
	case EEpicodeSandboxCommandCategory::Rule: return 1;
	case EEpicodeSandboxCommandCategory::Projection: return 2;
	case EEpicodeSandboxCommandCategory::System: return 3;
	default: return 4;
	}
}

FEpicodeSandboxValue FEpicodeSandboxValue::MakeBool(bool InValue)
{
	FEpicodeSandboxValue Result;
	Result.Type = EEpicodeSandboxValueType::Boolean;
	Result.BoolValue = InValue;
	return Result;
}

FEpicodeSandboxValue FEpicodeSandboxValue::MakeInt(int64 InValue)
{
	FEpicodeSandboxValue Result;
	Result.Type = EEpicodeSandboxValueType::Integer;
	Result.IntValue = InValue;
	return Result;
}

FEpicodeSandboxValue FEpicodeSandboxValue::MakeNumber(double InValue)
{
	FEpicodeSandboxValue Result;
	Result.Type = EEpicodeSandboxValueType::Number;
	Result.NumberValue = InValue;
	return Result;
}

FEpicodeSandboxValue FEpicodeSandboxValue::MakeString(const FString& InValue)
{
	FEpicodeSandboxValue Result;
	Result.Type = EEpicodeSandboxValueType::String;
	Result.StringValue = InValue;
	return Result;
}

TSharedPtr<FJsonValue> FEpicodeSandboxValue::ToJson() const
{
	switch (Type)
	{
	case EEpicodeSandboxValueType::Boolean: return MakeShared<FJsonValueBoolean>(BoolValue);
	case EEpicodeSandboxValueType::Integer: return MakeShared<FJsonValueNumber>(static_cast<double>(IntValue));
	case EEpicodeSandboxValueType::Number: return MakeShared<FJsonValueNumber>(NumberValue);
	case EEpicodeSandboxValueType::String: return MakeShared<FJsonValueString>(StringValue);
	default: return MakeShared<FJsonValueNull>();
	}
}

bool FEpicodeSandboxValue::FromJson(const TSharedPtr<FJsonValue>& InJson, FEpicodeSandboxValue& OutValue, FString& OutError)
{
	if (!InJson.IsValid())
	{
		OutError = TEXT("sandbox value is null");
		return false;
	}
	switch (InJson->Type)
	{
	case EJson::Boolean:
		OutValue = MakeBool(InJson->AsBool());
		return true;
	case EJson::Number:
	{
		const double Number = InJson->AsNumber();
		if (FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number)))
		{
			OutValue = MakeInt(static_cast<int64>(FMath::RoundToDouble(Number)));
		}
		else
		{
			OutValue = MakeNumber(Number);
		}
		return true;
	}
	case EJson::String:
		OutValue = MakeString(InJson->AsString());
		return true;
	default:
		OutError = TEXT("sandbox value must be a boolean, number or string");
		return false;
	}
}

FString FEpicodeSandboxValue::ToCanonicalString() const
{
	switch (Type)
	{
	case EEpicodeSandboxValueType::Boolean: return FString::Printf(TEXT("b:%s"), BoolValue ? TEXT("true") : TEXT("false"));
	case EEpicodeSandboxValueType::Integer: return FString::Printf(TEXT("i:%lld"), static_cast<long long>(IntValue));
	case EEpicodeSandboxValueType::Number: return FString::Printf(TEXT("n:%.17g"), NumberValue);
	case EEpicodeSandboxValueType::String: return TEXT("s:") + FEpicodeSandboxJson::EscapeString(StringValue);
	default: return TEXT("null");
	}
}

FString FEpicodeSandboxValue::ToDisplayString() const
{
	switch (Type)
	{
	case EEpicodeSandboxValueType::Boolean: return BoolValue ? TEXT("true") : TEXT("false");
	case EEpicodeSandboxValueType::Integer: return FString::Printf(TEXT("%lld"), static_cast<long long>(IntValue));
	case EEpicodeSandboxValueType::Number: return FString::Printf(TEXT("%.17g"), NumberValue);
	case EEpicodeSandboxValueType::String: return StringValue;
	default: return FString();
	}
}

bool FEpicodeSandboxValue::AsBool() const
{
	switch (Type)
	{
	case EEpicodeSandboxValueType::Boolean: return BoolValue;
	case EEpicodeSandboxValueType::Integer: return IntValue != 0;
	case EEpicodeSandboxValueType::Number: return NumberValue != 0.0;
	case EEpicodeSandboxValueType::String: return StringValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) || StringValue == TEXT("1");
	default: return false;
	}
}

int64 FEpicodeSandboxValue::AsInt() const
{
	switch (Type)
	{
	case EEpicodeSandboxValueType::Boolean: return BoolValue ? 1 : 0;
	case EEpicodeSandboxValueType::Integer: return IntValue;
	case EEpicodeSandboxValueType::Number: return static_cast<int64>(NumberValue);
	case EEpicodeSandboxValueType::String: return FCString::Atoi64(*StringValue);
	default: return 0;
	}
}

double FEpicodeSandboxValue::AsNumber() const
{
	switch (Type)
	{
	case EEpicodeSandboxValueType::Boolean: return BoolValue ? 1.0 : 0.0;
	case EEpicodeSandboxValueType::Integer: return static_cast<double>(IntValue);
	case EEpicodeSandboxValueType::Number: return NumberValue;
	case EEpicodeSandboxValueType::String: return FCString::Atof(*StringValue);
	default: return 0.0;
	}
}

TSharedPtr<FJsonObject> FEpicodeSandboxEntity::ToJson() const
{
	TSharedPtr<FJsonObject> Result = UnknownFields.IsValid() ? FEpicodeSandboxJson::CloneObject(UnknownFields) : MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("id"), Id);
	Result->SetStringField(TEXT("kind"), LexToString(Kind));
	Result->SetStringField(TEXT("archetypeId"), ArchetypeId);
	Result->SetObjectField(TEXT("transform"), MakeTransformJson(Location, Rotation));
	Result->SetBoolField(TEXT("visible"), bVisible);
	Result->SetStringField(TEXT("ownerId"), OwnerId);
	TArray<FString> ItemKeys;
	for (const TPair<FString, int32>& Pair : Inventory)
	{
		ItemKeys.Add(Pair.Key);
	}
	ItemKeys.Sort();
	TSharedPtr<FJsonObject> InventoryJson = MakeShared<FJsonObject>();
	for (const FString& ItemKey : ItemKeys)
	{
		InventoryJson->SetNumberField(ItemKey, Inventory.FindRef(ItemKey));
	}
	Result->SetObjectField(TEXT("inventory"), InventoryJson);
	Result->SetObjectField(TEXT("data"), Data.IsValid() ? FEpicodeSandboxJson::CloneObject(Data) : MakeShared<FJsonObject>());
	return Result;
}

bool FEpicodeSandboxEntity::FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeSandboxEntity& OutEntity, FString& OutError)
{
	if (!InJson.IsValid())
	{
		OutError = TEXT("sandbox entity must be an object");
		return false;
	}
	OutEntity = FEpicodeSandboxEntity();
	if (!InJson->TryGetStringField(TEXT("id"), OutEntity.Id) || OutEntity.Id.IsEmpty())
	{
		OutError = TEXT("sandbox entity id is required");
		return false;
	}
	FString Kind;
	if (!InJson->TryGetStringField(TEXT("kind"), Kind) || !ParseSandboxEntityKind(Kind, OutEntity.Kind))
	{
		OutError = TEXT("sandbox entity kind is invalid");
		return false;
	}
	InJson->TryGetStringField(TEXT("archetypeId"), OutEntity.ArchetypeId);
	const TSharedPtr<FJsonObject>* Transform = nullptr;
	if (InJson->TryGetObjectField(TEXT("transform"), Transform) && Transform)
	{
		TryReadVector(*Transform, TEXT("location"), OutEntity.Location);
		TryReadRotator(*Transform, TEXT("rotation"), OutEntity.Rotation);
	}
	InJson->TryGetBoolField(TEXT("visible"), OutEntity.bVisible);
	InJson->TryGetStringField(TEXT("ownerId"), OutEntity.OwnerId);
	const TSharedPtr<FJsonObject>* Inventory = nullptr;
	if (InJson->TryGetObjectField(TEXT("inventory"), Inventory) && Inventory)
	{
		for (const auto& Pair : (*Inventory)->Values)
		{
			OutEntity.Inventory.Add(FString(*Pair.Key), static_cast<int32>(Pair.Value->AsNumber()));
		}
	}
	const TSharedPtr<FJsonObject>* Data = nullptr;
	if (InJson->TryGetObjectField(TEXT("data"), Data) && Data)
	{
		OutEntity.Data = FEpicodeSandboxJson::CloneObject(*Data);
	}
	for (const auto& Pair : InJson->Values)
	{
		const FString Key(*Pair.Key);
		if (!IsKnownEntityField(Key))
		{
			if (!OutEntity.UnknownFields.IsValid())
			{
				OutEntity.UnknownFields = MakeShared<FJsonObject>();
			}
			OutEntity.UnknownFields->SetField(Key, FEpicodeSandboxJson::CloneValue(Pair.Value));
		}
	}
	return true;
}

FString FEpicodeSandboxEntity::ToCanonicalString() const
{
	TArray<FString> ItemKeys;
	for (const TPair<FString, int32>& Pair : Inventory)
	{
		ItemKeys.Add(Pair.Key);
	}
	ItemKeys.Sort();
	TArray<FString> Items;
	for (const FString& ItemKey : ItemKeys)
	{
		Items.Add(FEpicodeSandboxJson::EscapeString(ItemKey) + TEXT(":") + FString::FromInt(Inventory.FindRef(ItemKey)));
	}
	return FString::Printf(TEXT("%s|%s|%s|%.6f,%.6f,%.6f|%.6f,%.6f,%.6f|%d|%s|%s|%s"),
		*Id,
		*LexToString(Kind),
		*ArchetypeId,
		Location.X, Location.Y, Location.Z,
		Rotation.Pitch, Rotation.Yaw, Rotation.Roll,
		bVisible ? 1 : 0,
		*OwnerId,
		*FString::Join(Items, TEXT(",")),
		*FEpicodeSandboxJson::CanonicalString(Data));
}

int32 FEpicodeSandboxEntity::GetItemCount(const FString& InItemId) const
{
	const int32* Count = Inventory.Find(InItemId);
	return Count ? *Count : 0;
}

void FEpicodeSandboxEntity::SetItemCount(const FString& InItemId, int32 InCount)
{
	if (InCount <= 0)
	{
		Inventory.Remove(InItemId);
	}
	else
	{
		Inventory.Add(InItemId, InCount);
	}
}

bool FEpicodeSandboxEntity::AddItem(const FString& InItemId, int32 InDelta, FString& OutError)
{
	const int64 NewCount = static_cast<int64>(GetItemCount(InItemId)) + InDelta;
	if (NewCount < 0)
	{
		OutError = FString::Printf(TEXT("INVENTORY_NEGATIVE: %s would become %lld"), *InItemId, static_cast<long long>(NewCount));
		return false;
	}
	SetItemCount(InItemId, static_cast<int32>(NewCount));
	return true;
}

TSharedPtr<FJsonObject> FEpicodeSandboxParticipant::ToJson() const
{
	TSharedPtr<FJsonObject> Result = UnknownFields.IsValid() ? FEpicodeSandboxJson::CloneObject(UnknownFields) : MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("id"), Id);
	Result->SetStringField(TEXT("kind"), LexToString(Kind));
	Result->SetStringField(TEXT("characterEntityId"), CharacterEntityId);
	Result->SetBoolField(TEXT("connected"), bConnected);
	Result->SetBoolField(TEXT("takeover"), bTakeover);
	Result->SetStringField(TEXT("controllerId"), ConnectedControllerId);
	return Result;
}

bool FEpicodeSandboxParticipant::FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeSandboxParticipant& OutParticipant, FString& OutError)
{
	if (!InJson.IsValid())
	{
		OutError = TEXT("sandbox participant must be an object");
		return false;
	}
	OutParticipant = FEpicodeSandboxParticipant();
	if (!InJson->TryGetStringField(TEXT("id"), OutParticipant.Id) || OutParticipant.Id.IsEmpty())
	{
		OutError = TEXT("sandbox participant id is required");
		return false;
	}
	FString Kind;
	if (!InJson->TryGetStringField(TEXT("kind"), Kind) || !ParseSandboxParticipantKind(Kind, OutParticipant.Kind))
	{
		OutError = TEXT("sandbox participant kind is invalid");
		return false;
	}
	InJson->TryGetStringField(TEXT("characterEntityId"), OutParticipant.CharacterEntityId);
	InJson->TryGetBoolField(TEXT("connected"), OutParticipant.bConnected);
	InJson->TryGetBoolField(TEXT("takeover"), OutParticipant.bTakeover);
	InJson->TryGetStringField(TEXT("controllerId"), OutParticipant.ConnectedControllerId);
	for (const auto& Pair : InJson->Values)
	{
		const FString Key(*Pair.Key);
		if (Key != TEXT("id") && Key != TEXT("kind") && Key != TEXT("characterEntityId")
			&& Key != TEXT("connected") && Key != TEXT("takeover") && Key != TEXT("controllerId"))
		{
			if (!OutParticipant.UnknownFields.IsValid())
			{
				OutParticipant.UnknownFields = MakeShared<FJsonObject>();
			}
			OutParticipant.UnknownFields->SetField(Key, FEpicodeSandboxJson::CloneValue(Pair.Value));
		}
	}
	return true;
}

FString FEpicodeSandboxParticipant::ToCanonicalString() const
{
	return FString::Printf(TEXT("%s|%s|%s|%d|%d|%s"), *Id, *LexToString(Kind), *CharacterEntityId, bConnected ? 1 : 0, bTakeover ? 1 : 0, *ConnectedControllerId);
}

TSharedPtr<FJsonObject> FEpicodeSandboxNarrativeState::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("currentBeatId"), CurrentBeatId);
	Result->SetStringField(TEXT("currentObjectiveId"), CurrentObjectiveId);
	Result->SetStringField(TEXT("note"), Note);
	TArray<FString> VariableKeys;
	for (const TPair<FString, FEpicodeSandboxValue>& Pair : Variables)
	{
		VariableKeys.Add(Pair.Key);
	}
	VariableKeys.Sort();
	TSharedPtr<FJsonObject> VariablesJson = MakeShared<FJsonObject>();
	for (const FString& Key : VariableKeys)
	{
		VariablesJson->SetField(Key, Variables.FindRef(Key).ToJson());
	}
	Result->SetObjectField(TEXT("variables"), VariablesJson);

	TArray<FString> ObjectiveKeys;
	for (const TPair<FString, bool>& Pair : Objectives)
	{
		ObjectiveKeys.Add(Pair.Key);
	}
	ObjectiveKeys.Sort();
	TSharedPtr<FJsonObject> ObjectivesJson = MakeShared<FJsonObject>();
	for (const FString& Key : ObjectiveKeys)
	{
		ObjectivesJson->SetBoolField(Key, Objectives.FindRef(Key));
	}
	Result->SetObjectField(TEXT("objectives"), ObjectivesJson);
	TArray<TSharedPtr<FJsonValue>> EventValues;
	for (const FString& Event : CompletedEvents)
	{
		EventValues.Add(MakeShared<FJsonValueString>(Event));
	}
	Result->SetArrayField(TEXT("completedEvents"), EventValues);
	return Result;
}

bool FEpicodeSandboxNarrativeState::FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeSandboxNarrativeState& OutState, FString& OutError)
{
	if (!InJson.IsValid())
	{
		OutError = TEXT("sandbox narrative must be an object");
		return false;
	}
	OutState = FEpicodeSandboxNarrativeState();
	InJson->TryGetStringField(TEXT("currentBeatId"), OutState.CurrentBeatId);
	InJson->TryGetStringField(TEXT("currentObjectiveId"), OutState.CurrentObjectiveId);
	InJson->TryGetStringField(TEXT("note"), OutState.Note);
	const TSharedPtr<FJsonObject>* Variables = nullptr;
	if (InJson->TryGetObjectField(TEXT("variables"), Variables) && Variables)
	{
		for (const auto& Pair : (*Variables)->Values)
		{
			FEpicodeSandboxValue Value;
			FString ValueError;
			if (!FEpicodeSandboxValue::FromJson(Pair.Value, Value, ValueError))
			{
				OutError = FString::Printf(TEXT("variable %s: %s"), *FString(*Pair.Key), *ValueError);
				return false;
			}
			OutState.Variables.Add(FString(*Pair.Key), Value);
		}
	}
	const TSharedPtr<FJsonObject>* Objectives = nullptr;
	if (InJson->TryGetObjectField(TEXT("objectives"), Objectives) && Objectives)
	{
		for (const auto& Pair : (*Objectives)->Values)
		{
			OutState.Objectives.Add(FString(*Pair.Key), Pair.Value->AsBool());
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* CompletedEvents = nullptr;
	if (InJson->TryGetArrayField(TEXT("completedEvents"), CompletedEvents) && CompletedEvents)
	{
		for (const TSharedPtr<FJsonValue>& Event : *CompletedEvents)
		{
			OutState.CompletedEvents.Add(Event->AsString());
		}
	}
	return true;
}

FString FEpicodeSandboxNarrativeState::ToCanonicalString() const
{
	TArray<FString> VariableKeys;
	for (const TPair<FString, FEpicodeSandboxValue>& Pair : Variables)
	{
		VariableKeys.Add(Pair.Key);
	}
	VariableKeys.Sort();
	TArray<FString> VariableParts;
	for (const FString& Key : VariableKeys)
	{
		VariableParts.Add(FEpicodeSandboxJson::EscapeString(Key) + TEXT(":") + Variables.FindRef(Key).ToCanonicalString());
	}

	TArray<FString> ObjectiveKeys;
	for (const TPair<FString, bool>& Pair : Objectives)
	{
		ObjectiveKeys.Add(Pair.Key);
	}
	ObjectiveKeys.Sort();
	TArray<FString> ObjectiveParts;
	for (const FString& Key : ObjectiveKeys)
	{
		ObjectiveParts.Add(FEpicodeSandboxJson::EscapeString(Key) + TEXT(":") + (Objectives.FindRef(Key) ? TEXT("1") : TEXT("0")));
	}

	TArray<FString> Events = CompletedEvents;
	Events.Sort();
	return FString::Printf(TEXT("%s|%s|%s|{%s}|{%s}|%s"),
		*CurrentBeatId,
		*CurrentObjectiveId,
		*Note,
		*FString::Join(VariableParts, TEXT(",")),
		*FString::Join(ObjectiveParts, TEXT(",")),
		*FString::Join(Events, TEXT(",")));
}

const FEpicodeSandboxValue* FEpicodeSandboxNarrativeState::FindVariable(const FString& InName) const
{
	return Variables.Find(InName);
}

void FEpicodeSandboxNarrativeState::SetVariable(const FString& InName, const FEpicodeSandboxValue& InValue)
{
	Variables.Add(InName, InValue);
}

TSharedPtr<FJsonObject> FEpicodeSandboxEvent::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("sequence"), static_cast<double>(Sequence));
	Result->SetNumberField(TEXT("tick"), static_cast<double>(Tick));
	Result->SetStringField(TEXT("type"), Type);
	Result->SetStringField(TEXT("actorId"), ActorId);
	Result->SetStringField(TEXT("targetId"), TargetId);
	Result->SetObjectField(TEXT("payload"), Payload.IsValid() ? FEpicodeSandboxJson::CloneObject(Payload) : MakeShared<FJsonObject>());
	return Result;
}

bool FEpicodeSandboxEvent::FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeSandboxEvent& OutEvent, FString& OutError)
{
	if (!InJson.IsValid())
	{
		OutError = TEXT("sandbox event must be an object");
		return false;
	}
	OutEvent = FEpicodeSandboxEvent();
	InJson->TryGetNumberField(TEXT("sequence"), OutEvent.Sequence);
	InJson->TryGetNumberField(TEXT("tick"), OutEvent.Tick);
	if (!InJson->TryGetStringField(TEXT("type"), OutEvent.Type) || OutEvent.Type.IsEmpty())
	{
		OutError = TEXT("sandbox event type is required");
		return false;
	}
	InJson->TryGetStringField(TEXT("actorId"), OutEvent.ActorId);
	InJson->TryGetStringField(TEXT("targetId"), OutEvent.TargetId);
	const TSharedPtr<FJsonObject>* Payload = nullptr;
	if (InJson->TryGetObjectField(TEXT("payload"), Payload) && Payload)
	{
		OutEvent.Payload = FEpicodeSandboxJson::CloneObject(*Payload);
	}
	return true;
}

FString FEpicodeSandboxEvent::ToCanonicalString() const
{
	return FString::Printf(TEXT("%lld|%lld|%s|%s|%s|%s"),
		static_cast<long long>(Sequence),
		static_cast<long long>(Tick),
		*Type,
		*ActorId,
		*TargetId,
		*FEpicodeSandboxJson::CanonicalString(Payload));
}

TSharedPtr<FJsonObject> FEpicodeSandboxProjectionCommand::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("operation"), Operation);
	Result->SetStringField(TEXT("entityId"), EntityId);
	Result->SetStringField(TEXT("kind"), LexToString(Kind));
	Result->SetStringField(TEXT("archetypeId"), ArchetypeId);
	Result->SetObjectField(TEXT("transform"), MakeTransformJson(Location, Rotation));
	Result->SetObjectField(TEXT("data"), Data.IsValid() ? FEpicodeSandboxJson::CloneObject(Data) : MakeShared<FJsonObject>());
	return Result;
}
