#include "EpicodeProductionDocument.h"

#include "Dom/JsonValue.h"

namespace
{
	TSharedPtr<FJsonValue> CloneValue(const TSharedPtr<FJsonValue>& InValue)
	{
		if (!InValue.IsValid())
		{
			return MakeShared<FJsonValueNull>();
		}
		switch (InValue->Type)
		{
		case EJson::Object:
		{
			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			for (const auto& Pair : InValue->AsObject()->Values)
			{
				Object->SetField(FString(*Pair.Key), CloneValue(Pair.Value));
			}
			return MakeShared<FJsonValueObject>(Object);
		}
		case EJson::Array:
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const TSharedPtr<FJsonValue>& Value : InValue->AsArray())
			{
				Values.Add(CloneValue(Value));
			}
			return MakeShared<FJsonValueArray>(Values);
		}
		case EJson::String:
			return MakeShared<FJsonValueString>(InValue->AsString());
		case EJson::Boolean:
			return MakeShared<FJsonValueBoolean>(InValue->AsBool());
		case EJson::Number:
			return MakeShared<FJsonValueNumber>(InValue->AsNumber());
		case EJson::Null:
		default:
			return MakeShared<FJsonValueNull>();
		}
	}

	TSharedPtr<FJsonObject> CloneObject(const TSharedPtr<FJsonObject>& InObject)
	{
		if (!InObject.IsValid())
		{
			return MakeShared<FJsonObject>();
		}
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		for (const auto& Pair : InObject->Values)
		{
			Result->SetField(FString(*Pair.Key), CloneValue(Pair.Value));
		}
		return Result;
	}

	bool IsKnownTopLevelField(const FString& InKey)
	{
		return InKey == TEXT("schemaVersion")
			|| InKey == TEXT("productionId")
			|| InKey == TEXT("name")
			|| InKey == TEXT("sceneId")
			|| InKey == TEXT("cast")
			|| InKey == TEXT("worldRules")
			|| InKey == TEXT("narrative")
			|| InKey == TEXT("cameraPresets")
			|| InKey == TEXT("extensions");
	}

	bool IsValidParticipantKind(const FString& InKind)
	{
		const FString Kind = InKind.ToLower();
		return Kind == TEXT("performer") || Kind == TEXT("agent") || Kind == TEXT("viewer") || Kind == TEXT("npc");
	}

	bool TryGetObjectArray(const TSharedPtr<FJsonObject>& InObject, const FString& InField, TArray<TSharedPtr<FJsonObject>>& OutValues, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!InObject->TryGetArrayField(InField, Values) || !Values)
		{
			return true;
		}
		for (int32 Index = 0; Index < Values->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject> Value = (*Values)[Index]->AsObject();
			if (!Value.IsValid())
			{
				OutError = FString::Printf(TEXT("%s[%d] must be an object"), *InField, Index);
				return false;
			}
			OutValues.Add(Value);
		}
		return true;
	}
}

TSharedPtr<FJsonObject> FEpicodeProductionCastMember::ToJson() const
{
	TSharedPtr<FJsonObject> Result = Extra.IsValid() ? CloneObject(Extra) : MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("slot"), Slot);
	Result->SetStringField(TEXT("entityId"), EntityId);
	Result->SetStringField(TEXT("participantKind"), ParticipantKind);
	return Result;
}

bool FEpicodeProductionCastMember::FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeProductionCastMember& OutMember, FString& OutError)
{
	if (!InJson.IsValid())
	{
		OutError = TEXT("cast member must be an object");
		return false;
	}
	OutMember = FEpicodeProductionCastMember();
	if (!InJson->TryGetStringField(TEXT("slot"), OutMember.Slot) || OutMember.Slot.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("cast member slot is required");
		return false;
	}
	if (!InJson->TryGetStringField(TEXT("entityId"), OutMember.EntityId) || OutMember.EntityId.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("cast member entityId is required");
		return false;
	}
	InJson->TryGetStringField(TEXT("participantKind"), OutMember.ParticipantKind);
	if (OutMember.ParticipantKind.IsEmpty())
	{
		OutMember.ParticipantKind = TEXT("performer");
	}
	if (!IsValidParticipantKind(OutMember.ParticipantKind))
	{
		OutError = FString::Printf(TEXT("unsupported participantKind %s"), *OutMember.ParticipantKind);
		return false;
	}
	for (const auto& Pair : InJson->Values)
	{
		const FString Key(*Pair.Key);
		if (Key != TEXT("slot") && Key != TEXT("entityId") && Key != TEXT("participantKind"))
		{
			if (!OutMember.Extra.IsValid())
			{
				OutMember.Extra = MakeShared<FJsonObject>();
			}
			OutMember.Extra->SetField(Key, CloneValue(Pair.Value));
		}
	}
	return true;
}

TSharedPtr<FJsonObject> FEpicodeProductionCameraPreset::ToJson() const
{
	TSharedPtr<FJsonObject> Result = Extra.IsValid() ? CloneObject(Extra) : MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("id"), Id);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("cameraId"), CameraId);
	Result->SetObjectField(TEXT("settings"), Settings.IsValid() ? CloneObject(Settings) : MakeShared<FJsonObject>());
	return Result;
}

bool FEpicodeProductionCameraPreset::FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeProductionCameraPreset& OutPreset, FString& OutError)
{
	if (!InJson.IsValid())
	{
		OutError = TEXT("camera preset must be an object");
		return false;
	}
	OutPreset = FEpicodeProductionCameraPreset();
	if (!InJson->TryGetStringField(TEXT("id"), OutPreset.Id) || OutPreset.Id.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("camera preset id is required");
		return false;
	}
	InJson->TryGetStringField(TEXT("name"), OutPreset.Name);
	if (!InJson->TryGetStringField(TEXT("cameraId"), OutPreset.CameraId) || OutPreset.CameraId.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("camera preset cameraId is required");
		return false;
	}
	const TSharedPtr<FJsonObject>* Settings = nullptr;
	if (InJson->TryGetObjectField(TEXT("settings"), Settings) && Settings)
	{
		OutPreset.Settings = CloneObject(*Settings);
	}
	for (const auto& Pair : InJson->Values)
	{
		const FString Key(*Pair.Key);
		if (Key != TEXT("id") && Key != TEXT("name") && Key != TEXT("cameraId") && Key != TEXT("settings"))
		{
			if (!OutPreset.Extra.IsValid())
			{
				OutPreset.Extra = MakeShared<FJsonObject>();
			}
			OutPreset.Extra->SetField(Key, CloneValue(Pair.Value));
		}
	}
	return true;
}

bool FEpicodeProductionMigration::Migrate(TSharedPtr<FJsonObject>& InOutJson, int32 FromVersion, int32 ToVersion, FString& OutError)
{
	if (!InOutJson.IsValid())
	{
		OutError = TEXT("production document must be an object");
		return false;
	}
	if (FromVersion < 0 || ToVersion < FromVersion || ToVersion > FEpicodeProductionDocument::CurrentSchemaVersion)
	{
		OutError = FString::Printf(TEXT("Cannot migrate production schema from %d to %d."), FromVersion, ToVersion);
		return false;
	}

	int32 Version = FromVersion;
	if (Version < 1)
	{
		if (!InOutJson->HasField(TEXT("productionId")))
		{
			InOutJson->SetStringField(TEXT("productionId"), FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens));
		}
		if (!InOutJson->HasField(TEXT("name")))
		{
			InOutJson->SetStringField(TEXT("name"), TEXT("Untitled Production"));
		}
		if (!InOutJson->HasField(TEXT("sceneId")))
		{
			InOutJson->SetStringField(TEXT("sceneId"), FString());
		}
		if (!InOutJson->HasField(TEXT("cast")))
		{
			InOutJson->SetArrayField(TEXT("cast"), {});
		}
		if (!InOutJson->HasField(TEXT("worldRules")) || !InOutJson->HasTypedField<EJson::Object>(TEXT("worldRules")))
		{
			InOutJson->SetObjectField(TEXT("worldRules"), FEpicodeProductionDocument::MakeDefaultWorldRules());
		}
		if (!InOutJson->HasField(TEXT("narrative")) || !InOutJson->HasTypedField<EJson::Object>(TEXT("narrative")))
		{
			InOutJson->SetObjectField(TEXT("narrative"), FEpicodeProductionDocument::MakeDefaultNarrative());
		}
		if (!InOutJson->HasField(TEXT("cameraPresets")))
		{
			InOutJson->SetArrayField(TEXT("cameraPresets"), {});
		}
		if (!InOutJson->HasField(TEXT("extensions")) || !InOutJson->HasTypedField<EJson::Object>(TEXT("extensions")))
		{
			InOutJson->SetObjectField(TEXT("extensions"), MakeShared<FJsonObject>());
		}
		InOutJson->SetNumberField(TEXT("schemaVersion"), 1);
		Version = 1;
	}

	if (Version != ToVersion)
	{
		OutError = FString::Printf(TEXT("No migration path from production schema %d to %d."), Version, ToVersion);
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FEpicodeProductionDocument::MakeDefaultWorldRules()
{
	TSharedPtr<FJsonObject> Rules = MakeShared<FJsonObject>();
	Rules->SetArrayField(TEXT("interactionPoints"), {});
	Rules->SetArrayField(TEXT("zones"), {});
	Rules->SetArrayField(TEXT("spawners"), {});
	Rules->SetArrayField(TEXT("spawnTables"), {});
	Rules->SetArrayField(TEXT("resources"), {});
	Rules->SetArrayField(TEXT("npcProfiles"), {});
	Rules->SetArrayField(TEXT("recipes"), {});
	TSharedPtr<FJsonObject> Budgets = MakeShared<FJsonObject>();
	Budgets->SetNumberField(TEXT("maxEntities"), 1000);
	Budgets->SetNumberField(TEXT("maxEventsPerSecond"), 1000);
	Budgets->SetNumberField(TEXT("maxSpawnPerSecond"), 50);
	Rules->SetObjectField(TEXT("budgets"), Budgets);
	return Rules;
}

TSharedPtr<FJsonObject> FEpicodeProductionDocument::MakeDefaultNarrative()
{
	TSharedPtr<FJsonObject> Narrative = MakeShared<FJsonObject>();
	Narrative->SetStringField(TEXT("entryBeatId"), FString());
	Narrative->SetObjectField(TEXT("variables"), MakeShared<FJsonObject>());
	Narrative->SetArrayField(TEXT("beats"), {});
	Narrative->SetArrayField(TEXT("cues"), {});
	return Narrative;
}

FEpicodeProductionDocument FEpicodeProductionDocument::CreateDefault(const FString& InSceneId, const FString& InName)
{
	FEpicodeProductionDocument Document;
	Document.ProductionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	Document.Name = InName;
	Document.SceneId = InSceneId;
	Document.WorldRules = MakeDefaultWorldRules();
	Document.Narrative = MakeDefaultNarrative();
	Document.Extensions = MakeShared<FJsonObject>();
	return Document;
}

TSharedPtr<FJsonObject> FEpicodeProductionDocument::ToJson() const
{
	TSharedPtr<FJsonObject> Result = UnknownFields.IsValid() ? CloneObject(UnknownFields) : MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("schemaVersion"), CurrentSchemaVersion);
	Result->SetStringField(TEXT("productionId"), ProductionId);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("sceneId"), SceneId);

	TArray<TSharedPtr<FJsonValue>> CastValues;
	for (const FEpicodeProductionCastMember& Member : Cast)
	{
		CastValues.Add(MakeShared<FJsonValueObject>(Member.ToJson()));
	}
	Result->SetArrayField(TEXT("cast"), CastValues);
	Result->SetObjectField(TEXT("worldRules"), WorldRules.IsValid() ? CloneObject(WorldRules) : MakeDefaultWorldRules());
	Result->SetObjectField(TEXT("narrative"), Narrative.IsValid() ? CloneObject(Narrative) : MakeDefaultNarrative());

	TArray<TSharedPtr<FJsonValue>> CameraValues;
	for (const FEpicodeProductionCameraPreset& Preset : CameraPresets)
	{
		CameraValues.Add(MakeShared<FJsonValueObject>(Preset.ToJson()));
	}
	Result->SetArrayField(TEXT("cameraPresets"), CameraValues);
	Result->SetObjectField(TEXT("extensions"), Extensions.IsValid() ? CloneObject(Extensions) : MakeShared<FJsonObject>());
	return Result;
}

bool FEpicodeProductionDocument::FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeProductionDocument& OutDocument, FString& OutError)
{
	if (!InJson.IsValid())
	{
		OutError = TEXT("PRODUCTION_DOCUMENT_INVALID: production document must be an object");
		return false;
	}

	TSharedPtr<FJsonObject> Working = CloneObject(InJson);
	int32 Version = 0;
	Working->TryGetNumberField(TEXT("schemaVersion"), Version);
	if (Version > CurrentSchemaVersion)
	{
		OutError = FString::Printf(TEXT("PRODUCTION_VERSION_UNSUPPORTED: schema %d is newer than supported %d"), Version, CurrentSchemaVersion);
		return false;
	}
	const bool bUpgrade = Version < CurrentSchemaVersion;
	if (bUpgrade && !FEpicodeProductionMigration::Migrate(Working, Version, CurrentSchemaVersion, OutError))
	{
		return false;
	}

	OutDocument = FEpicodeProductionDocument();
	OutDocument.SchemaVersion = CurrentSchemaVersion;
	OutDocument.bPendingUpgrade = bUpgrade;
	Working->TryGetStringField(TEXT("productionId"), OutDocument.ProductionId);
	if (OutDocument.ProductionId.IsEmpty())
	{
		OutDocument.ProductionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	}
	Working->TryGetStringField(TEXT("name"), OutDocument.Name);
	if (OutDocument.Name.IsEmpty())
	{
		OutDocument.Name = TEXT("Untitled Production");
	}
	Working->TryGetStringField(TEXT("sceneId"), OutDocument.SceneId);

	TArray<TSharedPtr<FJsonObject>> CastValues;
	if (!TryGetObjectArray(Working, TEXT("cast"), CastValues, OutError))
	{
		OutError = TEXT("INVALID_CAST: ") + OutError;
		return false;
	}
	for (int32 Index = 0; Index < CastValues.Num(); ++Index)
	{
		FEpicodeProductionCastMember Member;
		FString MemberError;
		if (!FEpicodeProductionCastMember::FromJson(CastValues[Index], Member, MemberError))
		{
			OutError = FString::Printf(TEXT("INVALID_CAST: cast[%d]: %s"), Index, *MemberError);
			return false;
		}
		OutDocument.Cast.Add(Member);
	}

	const TSharedPtr<FJsonObject>* Rules = nullptr;
	if (Working->TryGetObjectField(TEXT("worldRules"), Rules) && Rules)
	{
		OutDocument.WorldRules = CloneObject(*Rules);
	}
	else
	{
		OutDocument.WorldRules = MakeDefaultWorldRules();
	}
	const TSharedPtr<FJsonObject>* Narrative = nullptr;
	if (Working->TryGetObjectField(TEXT("narrative"), Narrative) && Narrative)
	{
		OutDocument.Narrative = CloneObject(*Narrative);
	}
	else
	{
		OutDocument.Narrative = MakeDefaultNarrative();
	}

	TArray<TSharedPtr<FJsonObject>> CameraValues;
	if (!TryGetObjectArray(Working, TEXT("cameraPresets"), CameraValues, OutError))
	{
		OutError = TEXT("INVALID_CAMERA_PRESET: ") + OutError;
		return false;
	}
	for (int32 Index = 0; Index < CameraValues.Num(); ++Index)
	{
		FEpicodeProductionCameraPreset Preset;
		FString PresetError;
		if (!FEpicodeProductionCameraPreset::FromJson(CameraValues[Index], Preset, PresetError))
		{
			OutError = FString::Printf(TEXT("INVALID_CAMERA_PRESET: cameraPresets[%d]: %s"), Index, *PresetError);
			return false;
		}
		OutDocument.CameraPresets.Add(Preset);
	}

	const TSharedPtr<FJsonObject>* Extensions = nullptr;
	if (Working->TryGetObjectField(TEXT("extensions"), Extensions) && Extensions)
	{
		OutDocument.Extensions = CloneObject(*Extensions);
	}
	else
	{
		OutDocument.Extensions = MakeShared<FJsonObject>();
	}

	for (const auto& Pair : Working->Values)
	{
		const FString Key(*Pair.Key);
		if (!IsKnownTopLevelField(Key))
		{
			if (!OutDocument.UnknownFields.IsValid())
			{
				OutDocument.UnknownFields = MakeShared<FJsonObject>();
			}
			OutDocument.UnknownFields->SetField(Key, CloneValue(Pair.Value));
		}
	}

	if (!OutDocument.Validate(OutError))
	{
		return false;
	}
	return true;
}

bool FEpicodeProductionDocument::Validate(FString& OutError) const
{
	if (SchemaVersion < 1 || SchemaVersion > CurrentSchemaVersion)
	{
		OutError = FString::Printf(TEXT("PRODUCTION_SCHEMA_INVALID: schemaVersion %d"), SchemaVersion);
		return false;
	}
	if (ProductionId.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("PRODUCTION_ID_REQUIRED: productionId is required");
		return false;
	}
	if (Name.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("PRODUCTION_NAME_REQUIRED: name is required");
		return false;
	}
	if (SceneId.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("PRODUCTION_SCENE_REQUIRED: sceneId is required");
		return false;
	}
	if (!WorldRules.IsValid())
	{
		OutError = TEXT("PRODUCTION_RULES_INVALID: worldRules must be an object");
		return false;
	}
	if (!Narrative.IsValid())
	{
		OutError = TEXT("PRODUCTION_NARRATIVE_INVALID: narrative must be an object");
		return false;
	}

	TSet<FString> Slots;
	TSet<FString> CastEntities;
	for (int32 Index = 0; Index < Cast.Num(); ++Index)
	{
		const FEpicodeProductionCastMember& Member = Cast[Index];
		const FString Slot = Member.Slot.TrimStartAndEnd();
		if (Slot.IsEmpty())
		{
			OutError = FString::Printf(TEXT("CAST_SLOT_REQUIRED: cast[%d].slot is empty"), Index);
			return false;
		}
		if (Slots.Contains(Slot.ToLower()))
		{
			OutError = FString::Printf(TEXT("DUPLICATE_CAST_SLOT: cast[%d].slot (%s)"), Index, *Slot);
			return false;
		}
		Slots.Add(Slot.ToLower());
		const FString EntityId = Member.EntityId.TrimStartAndEnd();
		if (EntityId.IsEmpty())
		{
			OutError = FString::Printf(TEXT("CAST_ENTITY_REQUIRED: cast[%d].entityId is empty"), Index);
			return false;
		}
		if (CastEntities.Contains(EntityId.ToLower()))
		{
			OutError = FString::Printf(TEXT("DUPLICATE_CAST_ENTITY: cast[%d].entityId (%s)"), Index, *EntityId);
			return false;
		}
		CastEntities.Add(EntityId.ToLower());
		if (!IsValidParticipantKind(Member.ParticipantKind))
		{
			OutError = FString::Printf(TEXT("CAST_PARTICIPANT_INVALID: cast[%d].participantKind (%s)"), Index, *Member.ParticipantKind);
			return false;
		}
	}

	TSet<FString> PresetIds;
	for (int32 Index = 0; Index < CameraPresets.Num(); ++Index)
	{
		const FEpicodeProductionCameraPreset& Preset = CameraPresets[Index];
		const FString Id = Preset.Id.TrimStartAndEnd();
		if (Id.IsEmpty())
		{
			OutError = FString::Printf(TEXT("CAMERA_PRESET_ID_REQUIRED: cameraPresets[%d].id is empty"), Index);
			return false;
		}
		if (PresetIds.Contains(Id.ToLower()))
		{
			OutError = FString::Printf(TEXT("DUPLICATE_CAMERA_PRESET: cameraPresets[%d].id (%s)"), Index, *Id);
			return false;
		}
		PresetIds.Add(Id.ToLower());
		if (Preset.CameraId.TrimStartAndEnd().IsEmpty())
		{
			OutError = FString::Printf(TEXT("CAMERA_PRESET_CAMERA_REQUIRED: cameraPresets[%d].cameraId is empty"), Index);
			return false;
		}
	}
	return true;
}

const FEpicodeProductionCastMember* FEpicodeProductionDocument::FindCastBySlot(const FString& InSlot) const
{
	for (const FEpicodeProductionCastMember& Member : Cast)
	{
		if (Member.Slot.Equals(InSlot, ESearchCase::IgnoreCase))
		{
			return &Member;
		}
	}
	return nullptr;
}

FEpicodeProductionCastMember* FEpicodeProductionDocument::FindCastBySlot(const FString& InSlot)
{
	for (FEpicodeProductionCastMember& Member : Cast)
	{
		if (Member.Slot.Equals(InSlot, ESearchCase::IgnoreCase))
		{
			return &Member;
		}
	}
	return nullptr;
}

const FEpicodeProductionCastMember* FEpicodeProductionDocument::FindCastByEntity(const FString& InEntityId) const
{
	for (const FEpicodeProductionCastMember& Member : Cast)
	{
		if (Member.EntityId.Equals(InEntityId, ESearchCase::IgnoreCase))
		{
			return &Member;
		}
	}
	return nullptr;
}
