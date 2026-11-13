#include "EpicodeSandboxWorld.h"

#include "Dom/JsonValue.h"
#include "EpicodeContentDigest.h"
#include "EpicodeSandboxJson.h"

namespace
{
	bool IsKnownWorldField(const FString& InKey)
	{
		return InKey == TEXT("worldId") || InKey == TEXT("sessionId") || InKey == TEXT("productionId")
			|| InKey == TEXT("sceneId") || InKey == TEXT("tick") || InKey == TEXT("seed")
			|| InKey == TEXT("entities") || InKey == TEXT("participants") || InKey == TEXT("narrative");
	}
}

FEpicodeSandboxWorld FEpicodeSandboxWorld::Create(const FString& InSessionId, const FString& InProductionId, const FString& InSceneId, int32 InSeed)
{
	FEpicodeSandboxWorld World;
	World.WorldId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	World.SessionId = InSessionId;
	World.ProductionId = InProductionId;
	World.SceneId = InSceneId;
	World.Seed = InSeed;
	World.UnknownFields = MakeShared<FJsonObject>();
	return World;
}

bool FEpicodeSandboxWorld::AddEntity(const FEpicodeSandboxEntity& InEntity, FString& OutError)
{
	if (InEntity.Id.IsEmpty())
	{
		OutError = TEXT("ENTITY_ID_REQUIRED: sandbox entity id is empty");
		return false;
	}
	if (Entities.Contains(InEntity.Id))
	{
		OutError = FString::Printf(TEXT("ENTITY_EXISTS: %s"), *InEntity.Id);
		return false;
	}
	Entities.Add(InEntity.Id, InEntity);
	return true;
}

bool FEpicodeSandboxWorld::RemoveEntity(const FString& InEntityId, FString& OutError)
{
	if (!Entities.Contains(InEntityId))
	{
		OutError = FString::Printf(TEXT("ENTITY_NOT_FOUND: %s"), *InEntityId);
		return false;
	}
	Entities.Remove(InEntityId);
	return true;
}

FEpicodeSandboxEntity* FEpicodeSandboxWorld::FindEntity(const FString& InEntityId)
{
	return Entities.Find(InEntityId);
}

const FEpicodeSandboxEntity* FEpicodeSandboxWorld::FindEntity(const FString& InEntityId) const
{
	return Entities.Find(InEntityId);
}

bool FEpicodeSandboxWorld::AddParticipant(const FEpicodeSandboxParticipant& InParticipant, FString& OutError)
{
	if (InParticipant.Id.IsEmpty())
	{
		OutError = TEXT("PARTICIPANT_ID_REQUIRED: sandbox participant id is empty");
		return false;
	}
	if (Participants.Contains(InParticipant.Id))
	{
		OutError = FString::Printf(TEXT("PARTICIPANT_EXISTS: %s"), *InParticipant.Id);
		return false;
	}
	Participants.Add(InParticipant.Id, InParticipant);
	return true;
}

FEpicodeSandboxParticipant* FEpicodeSandboxWorld::FindParticipant(const FString& InParticipantId)
{
	return Participants.Find(InParticipantId);
}

const FEpicodeSandboxParticipant* FEpicodeSandboxWorld::FindParticipant(const FString& InParticipantId) const
{
	return Participants.Find(InParticipantId);
}

TArray<FString> FEpicodeSandboxWorld::GetSortedEntityIds() const
{
	TArray<FString> Ids;
	Entities.GetKeys(Ids);
	Ids.Sort();
	return Ids;
}

TArray<FString> FEpicodeSandboxWorld::GetSortedParticipantIds() const
{
	TArray<FString> Ids;
	Participants.GetKeys(Ids);
	Ids.Sort();
	return Ids;
}

bool FEpicodeSandboxWorld::Validate(FString& OutError) const
{
	if (WorldId.IsEmpty() || SessionId.IsEmpty() || ProductionId.IsEmpty())
	{
		OutError = TEXT("WORLD_ID_REQUIRED: worldId, sessionId and productionId are required");
		return false;
	}
	for (const TPair<FString, FEpicodeSandboxEntity>& Pair : Entities)
	{
		if (Pair.Key != Pair.Value.Id)
		{
			OutError = FString::Printf(TEXT("ENTITY_KEY_MISMATCH: %s != %s"), *Pair.Key, *Pair.Value.Id);
			return false;
		}
	}
	for (const TPair<FString, FEpicodeSandboxParticipant>& Pair : Participants)
	{
		if (Pair.Key != Pair.Value.Id)
		{
			OutError = FString::Printf(TEXT("PARTICIPANT_KEY_MISMATCH: %s != %s"), *Pair.Key, *Pair.Value.Id);
			return false;
		}
		const bool bMayLackCharacter = Pair.Value.Kind == EEpicodeSandboxParticipantKind::Viewer
			|| Pair.Value.Kind == EEpicodeSandboxParticipantKind::Director
			|| Pair.Value.Kind == EEpicodeSandboxParticipantKind::Owner;
		if (!Pair.Value.CharacterEntityId.IsEmpty() && !Entities.Contains(Pair.Value.CharacterEntityId) && !bMayLackCharacter)
		{
			OutError = FString::Printf(TEXT("PARTICIPANT_ENTITY_NOT_FOUND: %s"), *Pair.Value.CharacterEntityId);
			return false;
		}
	}
	return true;
}

TSharedPtr<FJsonObject> FEpicodeSandboxWorld::ToJson() const
{
	TSharedPtr<FJsonObject> Result = UnknownFields.IsValid() ? FEpicodeSandboxJson::CloneObject(UnknownFields) : MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("worldId"), WorldId);
	Result->SetStringField(TEXT("sessionId"), SessionId);
	Result->SetStringField(TEXT("productionId"), ProductionId);
	Result->SetStringField(TEXT("sceneId"), SceneId);
	Result->SetNumberField(TEXT("tick"), static_cast<double>(Tick));
	Result->SetNumberField(TEXT("seed"), Seed);

	TArray<TSharedPtr<FJsonValue>> EntityValues;
	for (const FString& EntityId : GetSortedEntityIds())
	{
		EntityValues.Add(MakeShared<FJsonValueObject>(Entities.FindRef(EntityId).ToJson()));
	}
	Result->SetArrayField(TEXT("entities"), EntityValues);

	TArray<TSharedPtr<FJsonValue>> ParticipantValues;
	for (const FString& ParticipantId : GetSortedParticipantIds())
	{
		ParticipantValues.Add(MakeShared<FJsonValueObject>(Participants.FindRef(ParticipantId).ToJson()));
	}
	Result->SetArrayField(TEXT("participants"), ParticipantValues);
	Result->SetObjectField(TEXT("narrative"), Narrative.ToJson());
	return Result;
}

bool FEpicodeSandboxWorld::FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeSandboxWorld& OutWorld, FString& OutError)
{
	if (!InJson.IsValid())
	{
		OutError = TEXT("sandbox world must be an object");
		return false;
	}
	OutWorld = FEpicodeSandboxWorld();
	InJson->TryGetStringField(TEXT("worldId"), OutWorld.WorldId);
	if (OutWorld.WorldId.IsEmpty())
	{
		OutWorld.WorldId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	}
	InJson->TryGetStringField(TEXT("sessionId"), OutWorld.SessionId);
	InJson->TryGetStringField(TEXT("productionId"), OutWorld.ProductionId);
	InJson->TryGetStringField(TEXT("sceneId"), OutWorld.SceneId);
	InJson->TryGetNumberField(TEXT("tick"), OutWorld.Tick);
	InJson->TryGetNumberField(TEXT("seed"), OutWorld.Seed);

	const TArray<TSharedPtr<FJsonValue>>* Entities = nullptr;
	if (InJson->TryGetArrayField(TEXT("entities"), Entities) && Entities)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Entities)
		{
			FEpicodeSandboxEntity Entity;
			FString EntityError;
			if (!FEpicodeSandboxEntity::FromJson(Value->AsObject(), Entity, EntityError))
			{
				OutError = EntityError;
				return false;
			}
			if (!OutWorld.AddEntity(Entity, EntityError))
			{
				OutError = EntityError;
				return false;
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Participants = nullptr;
	if (InJson->TryGetArrayField(TEXT("participants"), Participants) && Participants)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Participants)
		{
			FEpicodeSandboxParticipant Participant;
			FString ParticipantError;
			if (!FEpicodeSandboxParticipant::FromJson(Value->AsObject(), Participant, ParticipantError))
			{
				OutError = ParticipantError;
				return false;
			}
			if (!OutWorld.AddParticipant(Participant, ParticipantError))
			{
				OutError = ParticipantError;
				return false;
			}
		}
	}

	const TSharedPtr<FJsonObject>* Narrative = nullptr;
	if (InJson->TryGetObjectField(TEXT("narrative"), Narrative) && Narrative)
	{
		if (!FEpicodeSandboxNarrativeState::FromJson(*Narrative, OutWorld.Narrative, OutError))
		{
			return false;
		}
	}

	for (const auto& Pair : InJson->Values)
	{
		const FString Key(*Pair.Key);
		if (!IsKnownWorldField(Key))
		{
			if (!OutWorld.UnknownFields.IsValid())
			{
				OutWorld.UnknownFields = MakeShared<FJsonObject>();
			}
			OutWorld.UnknownFields->SetField(Key, FEpicodeSandboxJson::CloneValue(Pair.Value));
		}
	}

	return OutWorld.Validate(OutError);
}

FString FEpicodeSandboxWorld::ToCanonicalString() const
{
	TArray<FString> EntitiesParts;
	for (const FString& EntityId : GetSortedEntityIds())
	{
		EntitiesParts.Add(Entities.FindRef(EntityId).ToCanonicalString());
	}
	TArray<FString> ParticipantParts;
	for (const FString& ParticipantId : GetSortedParticipantIds())
	{
		ParticipantParts.Add(Participants.FindRef(ParticipantId).ToCanonicalString());
	}
	return FString::Printf(TEXT("%s|%s|%s|%s|%lld|%d|%s|%s|%s"),
		*WorldId,
		*SessionId,
		*ProductionId,
		*SceneId,
		static_cast<long long>(Tick),
		Seed,
		*FString::Join(EntitiesParts, TEXT(";")),
		*FString::Join(ParticipantParts, TEXT(";")),
		*Narrative.ToCanonicalString());
}

void FEpicodeSandboxEventStream::Reset(int64 InStartSequence)
{
	Events.Reset();
	NextSequence = FMath::Max<int64>(1, InStartSequence);
}

FEpicodeSandboxEvent& FEpicodeSandboxEventStream::Append(int64 InTick, const FString& InType, const FString& InActorId, const TSharedPtr<FJsonObject>& InPayload, const FString& InTargetId)
{
	FEpicodeSandboxEvent& Event = Events.AddDefaulted_GetRef();
	Event.Sequence = NextSequence++;
	Event.Tick = InTick;
	Event.Type = InType;
	Event.ActorId = InActorId;
	Event.TargetId = InTargetId;
	Event.Payload = InPayload.IsValid() ? FEpicodeSandboxJson::CloneObject(InPayload) : MakeShared<FJsonObject>();
	return Event;
}

void FEpicodeSandboxEventStream::AppendExisting(const FEpicodeSandboxEvent& InEvent, FString& OutError)
{
	if (InEvent.Sequence != NextSequence)
	{
		OutError = FString::Printf(TEXT("EVENT_SEQUENCE_GAP: expected %lld, got %lld"), static_cast<long long>(NextSequence), static_cast<long long>(InEvent.Sequence));
		return;
	}
	Events.Add(InEvent);
	NextSequence = InEvent.Sequence + 1;
}

TSharedPtr<FJsonObject> FEpicodeSandboxEventStream::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("nextSequence"), static_cast<double>(NextSequence));
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FEpicodeSandboxEvent& Event : Events)
	{
		Values.Add(MakeShared<FJsonValueObject>(Event.ToJson()));
	}
	Result->SetArrayField(TEXT("events"), Values);
	return Result;
}

bool FEpicodeSandboxEventStream::FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeSandboxEventStream& OutStream, FString& OutError)
{
	if (!InJson.IsValid())
	{
		OutError = TEXT("sandbox event stream must be an object");
		return false;
	}
	OutStream.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!InJson->TryGetArrayField(TEXT("events"), Values) || !Values)
	{
		return true;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		FEpicodeSandboxEvent Event;
		if (!FEpicodeSandboxEvent::FromJson(Value->AsObject(), Event, OutError))
		{
			return false;
		}
		OutStream.AppendExisting(Event, OutError);
		if (!OutError.IsEmpty())
		{
			return false;
		}
	}
	return true;
}

FString FEpicodeSandboxDigest::ComputeWorldDigest(const FEpicodeSandboxWorld& InWorld)
{
	return FEpicodeContentDigest::HashString(InWorld.ToCanonicalString());
}

FString FEpicodeSandboxDigest::ComputeEventDigest(const TArray<FEpicodeSandboxEvent>& InEvents)
{
	FString Canonical;
	for (const FEpicodeSandboxEvent& Event : InEvents)
	{
		Canonical += Event.ToCanonicalString();
		Canonical += TEXT("\n");
	}
	return FEpicodeContentDigest::HashString(Canonical);
}

FString FEpicodeSandboxDigest::ComputeCombinedDigest(const FEpicodeSandboxWorld& InWorld, const TArray<FEpicodeSandboxEvent>& InEvents)
{
	return FEpicodeContentDigest::HashString(InWorld.ToCanonicalString() + TEXT("#") + FEpicodeSandboxDigest::ComputeEventDigest(InEvents));
}
