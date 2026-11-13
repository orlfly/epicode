#include "EpicodeSandboxGameplay.h"

#include "Dom/JsonValue.h"
#include "EpicodeSandboxJson.h"
#include "EpicodeSandboxSettings.h"

namespace
{
	bool HasTargetTag(const FEpicodeSandboxEntity& InEntity, const FString& InTag)
	{
		if (InTag.IsEmpty())
		{
			return true;
		}
		if (!InEntity.Data.IsValid())
		{
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Tags = nullptr;
		if (InEntity.Data->TryGetArrayField(TEXT("tags"), Tags) && Tags)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Tags)
			{
				if (Value->AsString().Equals(InTag, ESearchCase::IgnoreCase))
				{
					return true;
				}
			}
		}
		FString SingleTag;
		return InEntity.Data->TryGetStringField(TEXT("tag"), SingleTag) && SingleTag.Equals(InTag, ESearchCase::IgnoreCase);
	}
}

bool FEpicodeSandboxGameplay::Initialize(const FEpicodeSandboxRules& InRules, FEpicodeSandboxWorld& InWorld, FEpicodeSandboxRandomStreams& InRandom, FEpicodeSandboxEventStream& InEvents, int32 InTickRate, FString& OutError)
{
	Rules = InRules;
	World = &InWorld;
	Random = &InRandom;
	Events = &InEvents;
	TickRate = FMath::Clamp(InTickRate, 1, 240);
	LastBudgetTick = -1;
	EventsThisTick = 0;
	ResourceStates.Reset();
	SpawnerStates.Reset();
	InteractionCooldowns.Reset();
	FallbackNpcProfile = FEpicodeSandboxNpcProfile();
	FallbackNpcProfile.Behavior = TEXT("idle");
	FallbackNpcProfile.PerceptionRadius = 0.0;
	FallbackNpcProfile.Speed = 0.0;

	if (!Rules.Validate(World, OutError))
	{
		return false;
	}
	for (const FEpicodeSandboxResourceRule& Resource : Rules.Resources)
	{
		FEpicodeSandboxResourceRuntime& State = ResourceStates.Add(Resource.Id);
		State.RuleId = Resource.Id;
		State.Remaining = Resource.Capacity;
		State.NextRespawnTick = 0;
		if (FEpicodeSandboxEntity* Entity = World->FindEntity(Resource.EntityId))
		{
			if (!Entity->Data.IsValid())
			{
				Entity->Data = MakeShared<FJsonObject>();
			}
			Entity->Data->SetNumberField(TEXT("resourceRemaining"), State.Remaining);
		}
	}
	for (const FEpicodeSandboxSpawner& Spawner : Rules.Spawners)
	{
		FEpicodeSandboxSpawnerRuntime& State = SpawnerStates.Add(Spawner.Id);
		State.NextSpawnTick = 0;
		State.SpawnedCount = 0;
	}
	return true;
}

bool FEpicodeSandboxGameplay::ExecuteInteraction(const FString& InParticipantId, const FString& InEntityId, const FString& InVerb, FString& OutError)
{
	if (!World)
	{
		OutError = TEXT("GAMEPLAY_NOT_INITIALIZED: sandbox gameplay has no world");
		return false;
	}
	if (!CheckPermission(InParticipantId, TEXT("interact"), OutError))
	{
		return false;
	}
	FEpicodeSandboxEntity* Actor = GetParticipantEntity(InParticipantId, OutError);
	if (!Actor)
	{
		return false;
	}
	FEpicodeSandboxEntity* Target = World->FindEntity(InEntityId);
	if (!Target)
	{
		OutError = FString::Printf(TEXT("INTERACTION_NOT_FOUND: entity %s"), *InEntityId);
		return false;
	}
	const FEpicodeSandboxInteractionPoint* Interaction = Rules.FindInteractionByEntity(InEntityId);
	if (!Interaction)
	{
		OutError = FString::Printf(TEXT("INTERACTION_NOT_FOUND: entity %s has no interaction point"), *InEntityId);
		return false;
	}
	if (FVector::Dist(Actor->Location, Target->Location) > Interaction->MaxDistance)
	{
		OutError = FString::Printf(TEXT("INTERACTION_OUT_OF_RANGE: %s -> %s"), *InEntityId, *InParticipantId);
		return false;
	}
	bool bVerbAllowed = false;
	for (const FString& Verb : Interaction->Verbs)
	{
		if (Verb.Equals(InVerb, ESearchCase::IgnoreCase))
		{
			bVerbAllowed = true;
			break;
		}
	}
	if (!bVerbAllowed)
	{
		OutError = FString::Printf(TEXT("INTERACTION_NOT_ALLOWED: %s on %s"), *InVerb, *InEntityId);
		return false;
	}
	for (const FEpicodeSandboxCondition& Condition : Interaction->Conditions)
	{
		if (!Condition.Evaluate(World->Narrative))
		{
			OutError = FString::Printf(TEXT("INTERACTION_CONDITION_FAILED: %s"), *Interaction->Id);
			return false;
		}
	}

	const FString CooldownKey = InParticipantId + TEXT("|") + Interaction->Id;
	if (const int64* LastTick = InteractionCooldowns.Find(CooldownKey))
	{
		const int64 CooldownTicks = FMath::CeilToInt64(Interaction->CooldownSeconds * TickRate);
		if (CooldownTicks > 0 && World->Tick < *LastTick + CooldownTicks)
		{
			OutError = FString::Printf(TEXT("INTERACTION_COOLDOWN: %s"), *Interaction->Id);
			return false;
		}
	}

	FEpicodeSandboxWorld Backup = *World;
	TArray<TSharedPtr<FJsonValue>> Changes;
	for (const FEpicodeSandboxOutcome& Outcome : Interaction->Outcomes)
	{
		if (!ApplyOutcome(Outcome, *Actor, Changes, OutError))
		{
			*World = Backup;
			return false;
		}
	}
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("interactionId"), Interaction->Id);
	Payload->SetStringField(TEXT("entityId"), InEntityId);
	Payload->SetStringField(TEXT("verb"), InVerb);
	Payload->SetNumberField(TEXT("outcomes"), Interaction->Outcomes.Num());
	Payload->SetArrayField(TEXT("changes"), Changes);
	if (!AppendEvent(World->Tick, TEXT("sandbox.interaction"), InParticipantId, Payload, OutError))
	{
		*World = Backup;
		return false;
	}
	InteractionCooldowns.Add(CooldownKey, World->Tick);
	return true;
}

bool FEpicodeSandboxGameplay::ApplyOutcome(const FEpicodeSandboxOutcome& InOutcome, FEpicodeSandboxEntity& InActor, TArray<TSharedPtr<FJsonValue>>& OutChanges, FString& OutError)
{
	if (!World)
	{
		OutError = TEXT("GAMEPLAY_NOT_INITIALIZED: sandbox gameplay has no world");
		return false;
	}
	if (InOutcome.Action.Equals(TEXT("setVariable"), ESearchCase::IgnoreCase))
	{
		World->Narrative.SetVariable(InOutcome.Key, InOutcome.Value);
		TSharedPtr<FJsonObject> Change = MakeShared<FJsonObject>();
		Change->SetStringField(TEXT("op"), TEXT("narrative.variable"));
		Change->SetStringField(TEXT("name"), InOutcome.Key);
		Change->SetField(TEXT("value"), InOutcome.Value.ToJson());
		OutChanges.Add(MakeShared<FJsonValueObject>(Change));
		return true;
	}
	if (InOutcome.Action.Equals(TEXT("addItem"), ESearchCase::IgnoreCase))
	{
		if (!InActor.AddItem(InOutcome.Key, static_cast<int32>(InOutcome.Value.AsInt()), OutError))
		{
			return false;
		}
		TSharedPtr<FJsonObject> Change = MakeShared<FJsonObject>();
		Change->SetStringField(TEXT("op"), TEXT("entity.inventory"));
		Change->SetStringField(TEXT("id"), InActor.Id);
		Change->SetStringField(TEXT("itemId"), InOutcome.Key);
		Change->SetNumberField(TEXT("count"), InActor.GetItemCount(InOutcome.Key));
		OutChanges.Add(MakeShared<FJsonValueObject>(Change));
		return true;
	}
	if (InOutcome.Action.Equals(TEXT("consumeItem"), ESearchCase::IgnoreCase))
	{
		if (!InActor.AddItem(InOutcome.Key, -FMath::Abs(static_cast<int32>(InOutcome.Value.AsInt())), OutError))
		{
			return false;
		}
		TSharedPtr<FJsonObject> Change = MakeShared<FJsonObject>();
		Change->SetStringField(TEXT("op"), TEXT("entity.inventory"));
		Change->SetStringField(TEXT("id"), InActor.Id);
		Change->SetStringField(TEXT("itemId"), InOutcome.Key);
		Change->SetNumberField(TEXT("count"), InActor.GetItemCount(InOutcome.Key));
		OutChanges.Add(MakeShared<FJsonValueObject>(Change));
		return true;
	}
	if (InOutcome.Action.Equals(TEXT("setEntityVisible"), ESearchCase::IgnoreCase))
	{
		FEpicodeSandboxEntity* Entity = World->FindEntity(InOutcome.Key);
		if (!Entity)
		{
			OutError = FString::Printf(TEXT("ENTITY_NOT_FOUND: %s"), *InOutcome.Key);
			return false;
		}
		Entity->bVisible = InOutcome.Value.AsBool();
		TSharedPtr<FJsonObject> Change = MakeShared<FJsonObject>();
		Change->SetStringField(TEXT("op"), TEXT("entity.visible"));
		Change->SetStringField(TEXT("id"), Entity->Id);
		Change->SetBoolField(TEXT("value"), Entity->bVisible);
		OutChanges.Add(MakeShared<FJsonValueObject>(Change));
		return true;
	}
	if (InOutcome.Action.Equals(TEXT("setOwner"), ESearchCase::IgnoreCase))
	{
		FEpicodeSandboxEntity* Entity = World->FindEntity(InOutcome.Key);
		if (!Entity)
		{
			OutError = FString::Printf(TEXT("ENTITY_NOT_FOUND: %s"), *InOutcome.Key);
			return false;
		}
		Entity->OwnerId = InOutcome.Value.ToDisplayString();
		TSharedPtr<FJsonObject> Change = MakeShared<FJsonObject>();
		Change->SetStringField(TEXT("op"), TEXT("entity.owner"));
		Change->SetStringField(TEXT("id"), Entity->Id);
		Change->SetStringField(TEXT("value"), Entity->OwnerId);
		OutChanges.Add(MakeShared<FJsonValueObject>(Change));
		return true;
	}
	if (InOutcome.Action.Equals(TEXT("emit"), ESearchCase::IgnoreCase))
	{
		return true;
	}
	OutError = FString::Printf(TEXT("OUTCOME_UNKNOWN: %s"), *InOutcome.Action);
	return false;
}

int32 FEpicodeSandboxGameplay::Tick(int64 InTick, double InDeltaSeconds, FString& OutError)
{
	int32 Actions = 0;
	FString FirstError;
	FString Error;
	Actions += TickSpawners(InTick, Error);
	if (!Error.IsEmpty() && FirstError.IsEmpty())
	{
		FirstError = Error;
	}
	Error.Reset();
	Actions += TickNpcs(InTick, InDeltaSeconds, Error);
	if (!Error.IsEmpty() && FirstError.IsEmpty())
	{
		FirstError = Error;
	}
	Error.Reset();
	Actions += TickResources(InTick, Error);
	if (!Error.IsEmpty() && FirstError.IsEmpty())
	{
		FirstError = Error;
	}
	OutError = FirstError;
	return Actions;
}

int32 FEpicodeSandboxGameplay::TickSpawners(int64 InTick, FString& OutError)
{
	if (!World || !Random || !Events)
	{
		OutError = TEXT("GAMEPLAY_NOT_INITIALIZED: sandbox gameplay is not initialized");
		return 0;
	}
	int32 Spawned = 0;
	for (const FEpicodeSandboxSpawner& Spawner : Rules.Spawners)
	{
		FEpicodeSandboxSpawnerRuntime& Runtime = SpawnerStates.FindOrAdd(Spawner.Id);
		if (InTick < Runtime.NextSpawnTick)
		{
			continue;
		}
		const FEpicodeSandboxSpawnTable* Table = Rules.FindSpawnTable(Spawner.SpawnTableId);
		if (!Table)
		{
			OutError = FString::Printf(TEXT("SPAWN_TABLE_NOT_FOUND: %s"), *Spawner.SpawnTableId);
			continue;
		}
		if (CountSpawnerEntities(Spawner.Id) >= Spawner.MaxAlive)
		{
			OutError = FString::Printf(TEXT("SPAWN_BUDGET_EXCEEDED: spawner %s maxAlive"), *Spawner.Id);
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("spawnerId"), Spawner.Id);
			Payload->SetStringField(TEXT("reason"), TEXT("maxAlive"));
			AppendEvent(InTick, TEXT("sandbox.spawnBudgetExceeded"), TEXT("system"), Payload, OutError);
			Runtime.NextSpawnTick = InTick + FMath::Max<int64>(1, FMath::RoundToInt64(Spawner.IntervalSeconds * TickRate));
			continue;
		}
		const FEpicodeSandboxZone* Zone = Spawner.ZoneId.IsEmpty() ? nullptr : Rules.FindZone(Spawner.ZoneId);
		if (Zone && Zone->MaxEntities > 0 && CountZoneEntities(Zone->Id) >= Zone->MaxEntities)
		{
			OutError = FString::Printf(TEXT("SPAWN_BUDGET_EXCEEDED: zone %s"), *Zone->Id);
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("spawnerId"), Spawner.Id);
			Payload->SetStringField(TEXT("reason"), TEXT("zoneBudget"));
			AppendEvent(InTick, TEXT("sandbox.spawnBudgetExceeded"), TEXT("system"), Payload, OutError);
			Runtime.NextSpawnTick = InTick + FMath::Max<int64>(1, FMath::RoundToInt64(Spawner.IntervalSeconds * TickRate));
			continue;
		}
		if (!CanSpawn(1, OutError))
		{
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("spawnerId"), Spawner.Id);
			Payload->SetStringField(TEXT("reason"), TEXT("globalBudget"));
			AppendEvent(InTick, TEXT("sandbox.spawnBudgetExceeded"), TEXT("system"), Payload, OutError);
			Runtime.NextSpawnTick = InTick + FMath::Max<int64>(1, FMath::RoundToInt64(Spawner.IntervalSeconds * TickRate));
			continue;
		}
		const FEpicodeSandboxSpawnEntry* Entry = Rules.PickSpawnEntry(*Table, Random->GetFloat(TEXT("spawner:") + Spawner.Id, InTick));
		if (!Entry)
		{
			OutError = FString::Printf(TEXT("SPAWN_ENTRY_INVALID: %s"), *Spawner.Id);
			continue;
		}
		const FString EntityId = FString::Printf(TEXT("spawn:%s:%d"), *Spawner.Id, Runtime.SpawnedCount + 1);
		FEpicodeSandboxEntity Entity;
		Entity.Id = EntityId;
		Entity.Kind = Entry->Kind;
		Entity.ArchetypeId = Entry->ArchetypeId;
		Entity.bVisible = true;
		Entity.Data = MakeShared<FJsonObject>();
		Entity.Data->SetStringField(TEXT("spawnerId"), Spawner.Id);
		if (Zone)
		{
			Entity.Data->SetStringField(TEXT("zoneId"), Zone->Id);
			if (Zone->bHasBounds)
			{
				const double X = Random->GetFloat(TEXT("spawner:") + Spawner.Id + TEXT(":x"), InTick) * 2.0 - 1.0;
				const double Y = Random->GetFloat(TEXT("spawner:") + Spawner.Id + TEXT(":y"), InTick) * 2.0 - 1.0;
				const double Z = Random->GetFloat(TEXT("spawner:") + Spawner.Id + TEXT(":z"), InTick) * 2.0 - 1.0;
				Entity.Location = Zone->Center + FVector(X * Zone->Extent.X, Y * Zone->Extent.Y, Z * Zone->Extent.Z);
			}
		}
		Entity.Data->SetNumberField(TEXT("spawnedAtTick"), static_cast<double>(InTick));
		if (!World->AddEntity(Entity, OutError))
		{
			continue;
		}
		++Runtime.SpawnedCount;
		Runtime.NextSpawnTick = InTick + FMath::Max<int64>(1, FMath::RoundToInt64(Spawner.IntervalSeconds * TickRate));
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("spawnerId"), Spawner.Id);
		Payload->SetStringField(TEXT("entityId"), EntityId);
		Payload->SetStringField(TEXT("kind"), LexToString(Entity.Kind));
		Payload->SetStringField(TEXT("archetypeId"), Entity.ArchetypeId);
		TSharedPtr<FJsonObject> AddChange = MakeShared<FJsonObject>();
		AddChange->SetStringField(TEXT("op"), TEXT("entity.add"));
		AddChange->SetObjectField(TEXT("entity"), Entity.ToJson());
		Payload->SetArrayField(TEXT("changes"), {MakeShared<FJsonValueObject>(AddChange)});
		if (!AppendEvent(InTick, TEXT("sandbox.spawned"), TEXT("system"), Payload, OutError))
		{
			World->RemoveEntity(EntityId, OutError);
			continue;
		}
		++Spawned;
	}
	return Spawned;
}

int32 FEpicodeSandboxGameplay::TickNpcs(int64 InTick, double InDeltaSeconds, FString& OutError)
{
	if (!World)
	{
		OutError = TEXT("GAMEPLAY_NOT_INITIALIZED: sandbox gameplay has no world");
		return 0;
	}
	int32 Actions = 0;
	for (const FString& EntityId : World->GetSortedEntityIds())
	{
		FEpicodeSandboxEntity* Npc = World->FindEntity(EntityId);
		if (!Npc || Npc->Kind != EEpicodeSandboxEntityKind::Npc)
		{
			continue;
		}
		bool bTakenOver = false;
		for (const auto& ParticipantPair : World->Participants)
		{
			const FEpicodeSandboxParticipant& Participant = ParticipantPair.Value;
			if (Participant.bTakeover && Participant.CharacterEntityId == Npc->Id)
			{
				bTakenOver = true;
				break;
			}
		}
		if (bTakenOver)
		{
			continue;
		}
		const FEpicodeSandboxNpcProfile& Profile = ResolveNpcProfile(*Npc);
		const FEpicodeSandboxEntity* Target = nullptr;
		double BestDistance = TNumericLimits<double>::Max();
		for (const FString& CandidateId : World->GetSortedEntityIds())
		{
			if (CandidateId == EntityId)
			{
				continue;
			}
			const FEpicodeSandboxEntity* Candidate = World->FindEntity(CandidateId);
			if (!Candidate || Candidate->Kind != EEpicodeSandboxEntityKind::Character || !HasTargetTag(*Candidate, Profile.TargetTag))
			{
				continue;
			}
			const double Distance = FVector::Dist(Npc->Location, Candidate->Location);
			if (Distance <= Profile.PerceptionRadius && Distance < BestDistance)
			{
				BestDistance = Distance;
				Target = Candidate;
			}
		}

		if (!Npc->Data.IsValid())
		{
			Npc->Data = MakeShared<FJsonObject>();
		}
		FString PreviousTarget;
		Npc->Data->TryGetStringField(TEXT("npcTargetId"), PreviousTarget);
		if (Target)
		{
			Npc->Data->SetStringField(TEXT("npcTargetId"), Target->Id);
			Npc->Data->SetStringField(TEXT("npcState"), TEXT("chase"));
			if (MoveEntityToward(*Npc, Target->Location, Profile.Speed, InDeltaSeconds))
			{
				TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
				Payload->SetStringField(TEXT("npcId"), Npc->Id);
				Payload->SetStringField(TEXT("targetId"), Target->Id);
				Payload->SetStringField(TEXT("state"), TEXT("chase"));
				TArray<TSharedPtr<FJsonValue>> Changes;
				TSharedPtr<FJsonObject> LocationChange = MakeShared<FJsonObject>();
				LocationChange->SetStringField(TEXT("op"), TEXT("entity.location"));
				LocationChange->SetStringField(TEXT("id"), Npc->Id);
				LocationChange->SetArrayField(TEXT("location"), {
					MakeShared<FJsonValueNumber>(Npc->Location.X),
					MakeShared<FJsonValueNumber>(Npc->Location.Y),
					MakeShared<FJsonValueNumber>(Npc->Location.Z)
				});
				Changes.Add(MakeShared<FJsonValueObject>(LocationChange));
				TSharedPtr<FJsonObject> TargetChange = MakeShared<FJsonObject>();
				TargetChange->SetStringField(TEXT("op"), TEXT("entity.dataString"));
				TargetChange->SetStringField(TEXT("id"), Npc->Id);
				TargetChange->SetStringField(TEXT("key"), TEXT("npcTargetId"));
				TargetChange->SetStringField(TEXT("value"), Target->Id);
				Changes.Add(MakeShared<FJsonValueObject>(TargetChange));
				TSharedPtr<FJsonObject> StateChange = MakeShared<FJsonObject>();
				StateChange->SetStringField(TEXT("op"), TEXT("entity.dataString"));
				StateChange->SetStringField(TEXT("id"), Npc->Id);
				StateChange->SetStringField(TEXT("key"), TEXT("npcState"));
				StateChange->SetStringField(TEXT("value"), TEXT("chase"));
				Changes.Add(MakeShared<FJsonValueObject>(StateChange));
				Payload->SetArrayField(TEXT("changes"), Changes);
				if (!AppendEvent(InTick, TEXT("sandbox.npcMoved"), Npc->Id, Payload, OutError))
				{
					break;
				}
				++Actions;
			}
			continue;
		}

		if (!PreviousTarget.IsEmpty())
		{
			Npc->Data->RemoveField(TEXT("npcTargetId"));
			Npc->Data->SetStringField(TEXT("npcState"), TEXT("patrol"));
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("npcId"), Npc->Id);
			Payload->SetStringField(TEXT("previousTargetId"), PreviousTarget);
			TArray<TSharedPtr<FJsonValue>> Changes;
			TSharedPtr<FJsonObject> RemoveTarget = MakeShared<FJsonObject>();
			RemoveTarget->SetStringField(TEXT("op"), TEXT("entity.dataRemove"));
			RemoveTarget->SetStringField(TEXT("id"), Npc->Id);
			RemoveTarget->SetStringField(TEXT("key"), TEXT("npcTargetId"));
			Changes.Add(MakeShared<FJsonValueObject>(RemoveTarget));
			TSharedPtr<FJsonObject> StateChange = MakeShared<FJsonObject>();
			StateChange->SetStringField(TEXT("op"), TEXT("entity.dataString"));
			StateChange->SetStringField(TEXT("id"), Npc->Id);
			StateChange->SetStringField(TEXT("key"), TEXT("npcState"));
			StateChange->SetStringField(TEXT("value"), TEXT("patrol"));
			Changes.Add(MakeShared<FJsonValueObject>(StateChange));
			Payload->SetArrayField(TEXT("changes"), Changes);
			if (!AppendEvent(InTick, TEXT("sandbox.npcTargetLost"), Npc->Id, Payload, OutError))
			{
				break;
			}
			++Actions;
		}
		if (Profile.PatrolPoints.Num() > 0 && Profile.Speed > 0.0)
		{
			int32 PatrolIndex = 0;
			Npc->Data->TryGetNumberField(TEXT("npcPatrolIndex"), PatrolIndex);
			PatrolIndex = FMath::Clamp(PatrolIndex, 0, Profile.PatrolPoints.Num() - 1);
			const FVector TargetPoint = Profile.PatrolPoints[PatrolIndex];
			if (MoveEntityToward(*Npc, TargetPoint, Profile.Speed, InDeltaSeconds))
			{
				if (FVector::Dist(Npc->Location, TargetPoint) < 10.0)
				{
					Npc->Data->SetNumberField(TEXT("npcPatrolIndex"), (PatrolIndex + 1) % Profile.PatrolPoints.Num());
				}
				TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
				Payload->SetStringField(TEXT("npcId"), Npc->Id);
				Payload->SetStringField(TEXT("state"), TEXT("patrol"));
				TArray<TSharedPtr<FJsonValue>> Changes;
				TSharedPtr<FJsonObject> LocationChange = MakeShared<FJsonObject>();
				LocationChange->SetStringField(TEXT("op"), TEXT("entity.location"));
				LocationChange->SetStringField(TEXT("id"), Npc->Id);
				LocationChange->SetArrayField(TEXT("location"), {
					MakeShared<FJsonValueNumber>(Npc->Location.X),
					MakeShared<FJsonValueNumber>(Npc->Location.Y),
					MakeShared<FJsonValueNumber>(Npc->Location.Z)
				});
				Changes.Add(MakeShared<FJsonValueObject>(LocationChange));
				int32 PatrolIndexAfter = PatrolIndex;
				Npc->Data->TryGetNumberField(TEXT("npcPatrolIndex"), PatrolIndexAfter);
				TSharedPtr<FJsonObject> IndexChange = MakeShared<FJsonObject>();
				IndexChange->SetStringField(TEXT("op"), TEXT("entity.dataInt"));
				IndexChange->SetStringField(TEXT("id"), Npc->Id);
				IndexChange->SetStringField(TEXT("key"), TEXT("npcPatrolIndex"));
				IndexChange->SetNumberField(TEXT("value"), PatrolIndexAfter);
				Changes.Add(MakeShared<FJsonValueObject>(IndexChange));
				Payload->SetArrayField(TEXT("changes"), Changes);
				if (!AppendEvent(InTick, TEXT("sandbox.npcMoved"), Npc->Id, Payload, OutError))
				{
					break;
				}
				++Actions;
			}
		}
	}
	return Actions;
}

int32 FEpicodeSandboxGameplay::TickResources(int64 InTick, FString& OutError)
{
	if (!World)
	{
		OutError = TEXT("GAMEPLAY_NOT_INITIALIZED: sandbox gameplay has no world");
		return 0;
	}
	int32 Respawned = 0;
	for (const FEpicodeSandboxResourceRule& Resource : Rules.Resources)
	{
		FEpicodeSandboxResourceRuntime& State = ResourceStates.FindOrAdd(Resource.Id);
		State.RuleId = Resource.Id;
		if (State.Remaining <= 0 && InTick >= State.NextRespawnTick)
		{
			State.Remaining = Resource.Capacity;
			State.NextRespawnTick = 0;
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("resourceId"), Resource.Id);
			Payload->SetStringField(TEXT("entityId"), Resource.EntityId);
			Payload->SetNumberField(TEXT("remaining"), State.Remaining);
			if (FEpicodeSandboxEntity* Entity = World->FindEntity(Resource.EntityId))
			{
				if (!Entity->Data.IsValid())
				{
					Entity->Data = MakeShared<FJsonObject>();
				}
				Entity->Data->SetNumberField(TEXT("resourceRemaining"), State.Remaining);
				TSharedPtr<FJsonObject> Change = MakeShared<FJsonObject>();
				Change->SetStringField(TEXT("op"), TEXT("entity.dataInt"));
				Change->SetStringField(TEXT("id"), Resource.EntityId);
				Change->SetStringField(TEXT("key"), TEXT("resourceRemaining"));
				Change->SetNumberField(TEXT("value"), State.Remaining);
				Payload->SetArrayField(TEXT("changes"), {MakeShared<FJsonValueObject>(Change)});
			}
			if (!AppendEvent(InTick, TEXT("sandbox.resourceRespawned"), TEXT("system"), Payload, OutError))
			{
				break;
			}
			++Respawned;
		}
	}
	return Respawned;
}

bool FEpicodeSandboxGameplay::Collect(const FString& InParticipantId, const FString& InNodeEntityId, int32 InAmount, FString& OutError)
{
	if (!World)
	{
		OutError = TEXT("GAMEPLAY_NOT_INITIALIZED: sandbox gameplay has no world");
		return false;
	}
	if (InAmount <= 0)
	{
		OutError = TEXT("RESOURCE_AMOUNT_INVALID: amount must be positive");
		return false;
	}
	if (!CheckPermission(InParticipantId, TEXT("interact"), OutError))
	{
		return false;
	}
	FEpicodeSandboxEntity* Actor = GetParticipantEntity(InParticipantId, OutError);
	if (!Actor)
	{
		return false;
	}
	FEpicodeSandboxEntity* Node = World->FindEntity(InNodeEntityId);
	if (!Node)
	{
		OutError = FString::Printf(TEXT("RESOURCE_NOT_FOUND: entity %s"), *InNodeEntityId);
		return false;
	}
	const FEpicodeSandboxResourceRule* Rule = Rules.FindResourceByEntity(InNodeEntityId);
	if (!Rule)
	{
		OutError = FString::Printf(TEXT("RESOURCE_NOT_FOUND: %s"), *InNodeEntityId);
		return false;
	}
	if (FVector::Dist(Actor->Location, Node->Location) > 250.0)
	{
		OutError = FString::Printf(TEXT("RESOURCE_OUT_OF_RANGE: %s"), *InNodeEntityId);
		return false;
	}
	FEpicodeSandboxResourceRuntime& State = ResourceStates.FindOrAdd(Rule->Id);
	State.RuleId = Rule->Id;
	if (State.Remaining < InAmount)
	{
		OutError = FString::Printf(TEXT("RESOURCE_DEPLETED: %s remaining %d"), *Rule->Id, State.Remaining);
		return false;
	}
	const int32 PreviousCount = Actor->GetItemCount(Rule->ItemId);
	if (!Actor->AddItem(Rule->ItemId, InAmount, OutError))
	{
		return false;
	}
	State.Remaining -= InAmount;
	if (State.Remaining <= 0)
	{
		State.NextRespawnTick = World->Tick + FMath::Max<int64>(1, FMath::RoundToInt64(Rule->RespawnSeconds * TickRate));
	}
	if (!Node->Data.IsValid())
	{
		Node->Data = MakeShared<FJsonObject>();
	}
	Node->Data->SetNumberField(TEXT("resourceRemaining"), State.Remaining);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("resourceId"), Rule->Id);
	Payload->SetStringField(TEXT("itemId"), Rule->ItemId);
	Payload->SetNumberField(TEXT("amount"), InAmount);
	Payload->SetNumberField(TEXT("remaining"), State.Remaining);
	TArray<TSharedPtr<FJsonValue>> Changes;
	TSharedPtr<FJsonObject> NodeChange = MakeShared<FJsonObject>();
	NodeChange->SetStringField(TEXT("op"), TEXT("entity.dataInt"));
	NodeChange->SetStringField(TEXT("id"), Rule->EntityId);
	NodeChange->SetStringField(TEXT("key"), TEXT("resourceRemaining"));
	NodeChange->SetNumberField(TEXT("value"), State.Remaining);
	Changes.Add(MakeShared<FJsonValueObject>(NodeChange));
	TSharedPtr<FJsonObject> InventoryChange = MakeShared<FJsonObject>();
	InventoryChange->SetStringField(TEXT("op"), TEXT("entity.inventory"));
	InventoryChange->SetStringField(TEXT("id"), Actor->Id);
	InventoryChange->SetStringField(TEXT("itemId"), Rule->ItemId);
	InventoryChange->SetNumberField(TEXT("count"), Actor->GetItemCount(Rule->ItemId));
	Changes.Add(MakeShared<FJsonValueObject>(InventoryChange));
	Payload->SetArrayField(TEXT("changes"), Changes);
	if (!AppendEvent(World->Tick, TEXT("sandbox.resourceCollected"), InParticipantId, Payload, OutError))
	{
		State.Remaining += InAmount;
		Actor->SetItemCount(Rule->ItemId, PreviousCount);
		return false;
	}
	return true;
}

bool FEpicodeSandboxGameplay::ConsumeItem(const FString& InParticipantId, const FString& InItemId, int32 InAmount, FString& OutError)
{
	if (!World)
	{
		OutError = TEXT("GAMEPLAY_NOT_INITIALIZED: sandbox gameplay has no world");
		return false;
	}
	if (InAmount <= 0)
	{
		OutError = TEXT("RESOURCE_AMOUNT_INVALID: amount must be positive");
		return false;
	}
	if (!CheckPermission(InParticipantId, TEXT("interact"), OutError))
	{
		return false;
	}
	FEpicodeSandboxEntity* Actor = GetParticipantEntity(InParticipantId, OutError);
	if (!Actor)
	{
		return false;
	}
	const int32 PreviousCount = Actor->GetItemCount(InItemId);
	if (PreviousCount < InAmount)
	{
		OutError = FString::Printf(TEXT("INSUFFICIENT_RESOURCES: %s has %d, needs %d"), *InItemId, PreviousCount, InAmount);
		return false;
	}
	if (!Actor->AddItem(InItemId, -InAmount, OutError))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("itemId"), InItemId);
	Payload->SetNumberField(TEXT("amount"), InAmount);
	TSharedPtr<FJsonObject> Change = MakeShared<FJsonObject>();
	Change->SetStringField(TEXT("op"), TEXT("entity.inventory"));
	Change->SetStringField(TEXT("id"), Actor->Id);
	Change->SetStringField(TEXT("itemId"), InItemId);
	Change->SetNumberField(TEXT("count"), Actor->GetItemCount(InItemId));
	Payload->SetArrayField(TEXT("changes"), {MakeShared<FJsonValueObject>(Change)});
	if (!AppendEvent(World->Tick, TEXT("sandbox.itemConsumed"), InParticipantId, Payload, OutError))
	{
		Actor->SetItemCount(InItemId, PreviousCount);
		return false;
	}
	return true;
}

bool FEpicodeSandboxGameplay::Craft(const FString& InParticipantId, const FString& InRecipeId, FString& OutError)
{
	if (!World)
	{
		OutError = TEXT("GAMEPLAY_NOT_INITIALIZED: sandbox gameplay has no world");
		return false;
	}
	if (!CheckPermission(InParticipantId, TEXT("interact"), OutError))
	{
		return false;
	}
	FEpicodeSandboxEntity* Actor = GetParticipantEntity(InParticipantId, OutError);
	if (!Actor)
	{
		return false;
	}
	const FEpicodeSandboxRecipe* Recipe = Rules.FindRecipe(InRecipeId);
	if (!Recipe)
	{
		OutError = FString::Printf(TEXT("RECIPE_NOT_FOUND: %s"), *InRecipeId);
		return false;
	}
	for (const TPair<FString, int32>& Input : Recipe->Inputs)
	{
		if (Actor->GetItemCount(Input.Key) < Input.Value)
		{
			OutError = FString::Printf(TEXT("INSUFFICIENT_RESOURCES: %s needs %d of %s"), *InRecipeId, Input.Value, *Input.Key);
			return false;
		}
	}
	const TMap<FString, int32> Backup = Actor->Inventory;
	for (const TPair<FString, int32>& Input : Recipe->Inputs)
	{
		if (!Actor->AddItem(Input.Key, -Input.Value, OutError))
		{
			Actor->Inventory = Backup;
			return false;
		}
	}
	for (const TPair<FString, int32>& Output : Recipe->Outputs)
	{
		if (!Actor->AddItem(Output.Key, Output.Value, OutError))
		{
			Actor->Inventory = Backup;
			return false;
		}
	}
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("recipeId"), Recipe->Id);
	Payload->SetNumberField(TEXT("inputs"), Recipe->Inputs.Num());
	Payload->SetNumberField(TEXT("outputs"), Recipe->Outputs.Num());
	TArray<TSharedPtr<FJsonValue>> Changes;
	for (const TPair<FString, int32>& Item : Actor->Inventory)
	{
		TSharedPtr<FJsonObject> Change = MakeShared<FJsonObject>();
		Change->SetStringField(TEXT("op"), TEXT("entity.inventory"));
		Change->SetStringField(TEXT("id"), Actor->Id);
		Change->SetStringField(TEXT("itemId"), Item.Key);
		Change->SetNumberField(TEXT("count"), Item.Value);
		Changes.Add(MakeShared<FJsonValueObject>(Change));
	}
	Payload->SetArrayField(TEXT("changes"), Changes);
	if (!AppendEvent(World->Tick, TEXT("sandbox.crafted"), InParticipantId, Payload, OutError))
	{
		Actor->Inventory = Backup;
		return false;
	}
	return true;
}

bool FEpicodeSandboxGameplay::CanPerform(EEpicodeSandboxParticipantKind InKind, const FString& InAction)
{
	const FString Action = InAction.ToLower();
	if (Action == TEXT("observe") || Action == TEXT("read"))
	{
		return true;
	}
	if (InKind == EEpicodeSandboxParticipantKind::Viewer)
	{
		return false;
	}
	if (Action == TEXT("manageownership") || Action == TEXT("manageparticipants") || Action == TEXT("kick"))
	{
		return InKind == EEpicodeSandboxParticipantKind::Owner;
	}
	if (Action == TEXT("director") || Action == TEXT("spawn") || Action == TEXT("take"))
	{
		return InKind == EEpicodeSandboxParticipantKind::Director || InKind == EEpicodeSandboxParticipantKind::Owner;
	}
	if (Action == TEXT("interact"))
	{
		return InKind == EEpicodeSandboxParticipantKind::Performer
			|| InKind == EEpicodeSandboxParticipantKind::Agent
			|| InKind == EEpicodeSandboxParticipantKind::Npc
			|| InKind == EEpicodeSandboxParticipantKind::Director
			|| InKind == EEpicodeSandboxParticipantKind::Owner;
	}
	return true;
}

bool FEpicodeSandboxGameplay::CheckPermission(const FString& InParticipantId, const FString& InAction, FString& OutError) const
{
	if (!World)
	{
		OutError = TEXT("GAMEPLAY_NOT_INITIALIZED: sandbox gameplay has no world");
		return false;
	}
	const FEpicodeSandboxParticipant* Participant = World->FindParticipant(InParticipantId);
	if (!Participant)
	{
		OutError = FString::Printf(TEXT("PARTICIPANT_NOT_FOUND: %s"), *InParticipantId);
		return false;
	}
	if (!CanPerform(Participant->Kind, InAction))
	{
		OutError = Participant->Kind == EEpicodeSandboxParticipantKind::Viewer
			? FString(TEXT("VIEWER_READ_ONLY: viewer cannot modify the sandbox"))
			: FString::Printf(TEXT("FORBIDDEN: %s cannot %s"), *InParticipantId, *InAction);
		return false;
	}
	return true;
}

bool FEpicodeSandboxGameplay::CanSpawn(int32 InAdditional, FString& OutError) const
{
	if (!World)
	{
		OutError = TEXT("GAMEPLAY_NOT_INITIALIZED: sandbox gameplay has no world");
		return false;
	}
	if (World->Entities.Num() + FMath::Max(0, InAdditional) > Rules.Budget.MaxEntities)
	{
		OutError = FString::Printf(TEXT("SPAWN_BUDGET_EXCEEDED: %d entities, budget %d"), World->Entities.Num(), Rules.Budget.MaxEntities);
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FEpicodeSandboxGameplay::GetBudgetReport() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	if (!World)
	{
		return Result;
	}
	Result->SetNumberField(TEXT("entityCount"), World->Entities.Num());
	Result->SetNumberField(TEXT("maxEntities"), Rules.Budget.MaxEntities);
	Result->SetNumberField(TEXT("eventsThisTick"), EventsThisTick);
	Result->SetNumberField(TEXT("maxEventsPerSecond"), Rules.Budget.MaxEventsPerSecond);
	Result->SetNumberField(TEXT("maxSpawnPerSecond"), Rules.Budget.MaxSpawnPerSecond);
	Result->SetNumberField(TEXT("maxDirectorCues"), Rules.Budget.MaxDirectorCues);
	TArray<TSharedPtr<FJsonValue>> Spawners;
	for (const FEpicodeSandboxSpawner& Spawner : Rules.Spawners)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("id"), Spawner.Id);
		Entry->SetNumberField(TEXT("alive"), CountSpawnerEntities(Spawner.Id));
		Entry->SetNumberField(TEXT("maxAlive"), Spawner.MaxAlive);
		Spawners.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Result->SetArrayField(TEXT("spawners"), Spawners);
	return Result;
}

FEpicodeSandboxEntity* FEpicodeSandboxGameplay::GetParticipantEntity(const FString& InParticipantId, FString& OutError)
{
	const FEpicodeSandboxParticipant* Participant = World ? World->FindParticipant(InParticipantId) : nullptr;
	if (!Participant)
	{
		OutError = FString::Printf(TEXT("PARTICIPANT_NOT_FOUND: %s"), *InParticipantId);
		return nullptr;
	}
	FEpicodeSandboxEntity* Entity = World->FindEntity(Participant->CharacterEntityId);
	if (!Entity)
	{
		OutError = FString::Printf(TEXT("PARTICIPANT_ENTITY_NOT_FOUND: %s"), *Participant->CharacterEntityId);
		return nullptr;
	}
	return Entity;
}

const FEpicodeSandboxEntity* FEpicodeSandboxGameplay::GetParticipantEntity(const FString& InParticipantId, FString& OutError) const
{
	const FEpicodeSandboxParticipant* Participant = World ? World->FindParticipant(InParticipantId) : nullptr;
	if (!Participant)
	{
		OutError = FString::Printf(TEXT("PARTICIPANT_NOT_FOUND: %s"), *InParticipantId);
		return nullptr;
	}
	const FEpicodeSandboxEntity* Entity = World->FindEntity(Participant->CharacterEntityId);
	if (!Entity)
	{
		OutError = FString::Printf(TEXT("PARTICIPANT_ENTITY_NOT_FOUND: %s"), *Participant->CharacterEntityId);
		return nullptr;
	}
	return Entity;
}

bool FEpicodeSandboxGameplay::AppendEvent(int64 InTick, const FString& InType, const FString& InActorId, const TSharedPtr<FJsonObject>& InPayload, FString& OutError)
{
	if (!Events)
	{
		OutError = TEXT("GAMEPLAY_NOT_INITIALIZED: sandbox gameplay has no events");
		return false;
	}
	if (InTick != LastBudgetTick)
	{
		LastBudgetTick = InTick;
		EventsThisTick = 0;
	}
	const int32 MaxEventsPerTick = FMath::Max(1, Rules.Budget.MaxEventsPerSecond / FMath::Max(1, TickRate));
	if (EventsThisTick >= MaxEventsPerTick)
	{
		OutError = FString::Printf(TEXT("EVENT_BUDGET_EXCEEDED: %d events in tick %lld"), EventsThisTick, static_cast<long long>(InTick));
		return false;
	}
	Events->Append(InTick, InType, InActorId, InPayload);
	++EventsThisTick;
	return true;
}

int32 FEpicodeSandboxGameplay::CountSpawnerEntities(const FString& InSpawnerId) const
{
	if (!World)
	{
		return 0;
	}
	int32 Count = 0;
	for (const TPair<FString, FEpicodeSandboxEntity>& Pair : World->Entities)
	{
		FString Value;
		if (Pair.Value.Data.IsValid() && Pair.Value.Data->TryGetStringField(TEXT("spawnerId"), Value) && Value == InSpawnerId)
		{
			++Count;
		}
	}
	return Count;
}

int32 FEpicodeSandboxGameplay::CountZoneEntities(const FString& InZoneId) const
{
	if (!World)
	{
		return 0;
	}
	int32 Count = 0;
	for (const TPair<FString, FEpicodeSandboxEntity>& Pair : World->Entities)
	{
		FString Value;
		if (Pair.Value.Data.IsValid() && Pair.Value.Data->TryGetStringField(TEXT("zoneId"), Value) && Value == InZoneId)
		{
			++Count;
		}
	}
	return Count;
}

const FEpicodeSandboxNpcProfile& FEpicodeSandboxGameplay::ResolveNpcProfile(const FEpicodeSandboxEntity& InEntity) const
{
	FString ProfileId;
	if (InEntity.Data.IsValid())
	{
		InEntity.Data->TryGetStringField(TEXT("npcProfileId"), ProfileId);
	}
	if (!ProfileId.IsEmpty())
	{
		if (const FEpicodeSandboxNpcProfile* Profile = Rules.FindNpcProfile(ProfileId))
		{
			return *Profile;
		}
	}
	if (Rules.NpcProfiles.Num() > 0)
	{
		return Rules.NpcProfiles[0];
	}
	return FallbackNpcProfile;
}

bool FEpicodeSandboxGameplay::MoveEntityToward(FEpicodeSandboxEntity& InEntity, const FVector& InTarget, double InSpeed, double InDeltaSeconds)
{
	const FVector Delta = InTarget - InEntity.Location;
	const double Distance = Delta.Size();
	if (Distance <= KINDA_SMALL_NUMBER || InSpeed <= 0.0 || InDeltaSeconds <= 0.0)
	{
		return false;
	}
	const double Step = FMath::Min(Distance, InSpeed * InDeltaSeconds);
	InEntity.Location += Delta / Distance * Step;
	return true;
}
