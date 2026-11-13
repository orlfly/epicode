#include "EpicodeSandboxSession.h"

#include "EpicodeProductionLibrary.h"
#include "EpicodeSandboxJson.h"
#include "EpicodeSandboxPersistence.h"
#include "EpicodeSandboxProjection.h"
#include "EpicodeSandboxSettings.h"
#include "EpicodeTypes.h"

namespace
{
	EEpicodeSandboxEntityKind MapEntityKind(const FEpicodeEntityRecord& InEntity, const FEpicodeProductionDocument& InProduction)
	{
		switch (InEntity.Type)
		{
		case EEpicodeEntityType::Character:
		{
			const FEpicodeProductionCastMember* Member = InProduction.FindCastByEntity(InEntity.Id.ToString(EGuidFormats::DigitsWithHyphens));
			if (Member && Member->ParticipantKind.Equals(TEXT("npc"), ESearchCase::IgnoreCase))
			{
				return EEpicodeSandboxEntityKind::Npc;
			}
			return EEpicodeSandboxEntityKind::Character;
		}
		case EEpicodeEntityType::Prop: return EEpicodeSandboxEntityKind::Prop;
		case EEpicodeEntityType::Light: return EEpicodeSandboxEntityKind::Light;
		case EEpicodeEntityType::Camera: return EEpicodeSandboxEntityKind::Camera;
		case EEpicodeEntityType::Group:
		default: return EEpicodeSandboxEntityKind::Trigger;
		}
	}

	FString GetArchetypeId(const FEpicodeEntityRecord& InEntity)
	{
		if (InEntity.Data.IsValid())
		{
			FString Value;
			if (InEntity.Data->TryGetStringField(TEXT("archetypeId"), Value) && !Value.IsEmpty())
			{
				return Value;
			}
			if (InEntity.Data->TryGetStringField(TEXT("assetPath"), Value) && !Value.IsEmpty())
			{
				return Value;
			}
			if (InEntity.Data->TryGetStringField(TEXT("mesh"), Value) && !Value.IsEmpty())
			{
				return Value;
			}
		}
		return InEntity.GetTypeName();
	}
}

bool FEpicodeSandboxSession::Load(const FEpicodeProductionDocument& InProduction, const FEpicodeSceneDocument& InScene, const FEpicodeSandboxSessionConfig& InConfig, FString& OutError)
{
	FString ValidationError;
	if (!InProduction.Validate(ValidationError))
	{
		Error = ValidationError;
		State = EEpicodeSandboxSessionState::Failed;
		OutError = Error;
		return false;
	}
	if (!InScene.Validate(ValidationError))
	{
		Error = FString::Printf(TEXT("SCENE_INVALID: %s"), *ValidationError);
		State = EEpicodeSandboxSessionState::Failed;
		OutError = Error;
		return false;
	}
	if (!InProduction.SceneId.Equals(InScene.SceneId, ESearchCase::CaseSensitive))
	{
		Error = FString::Printf(TEXT("SCENE_MISMATCH: production references %s but scene is %s"), *InProduction.SceneId, *InScene.SceneId);
		State = EEpicodeSandboxSessionState::Failed;
		OutError = Error;
		return false;
	}
	for (const FEpicodeProductionCastMember& Member : InProduction.Cast)
	{
		const FEpicodeEntityRecord* Entity = InScene.FindEntity(Member.EntityId);
		if (!Entity)
		{
			Error = FString::Printf(TEXT("PRODUCTION_REFERENCE_NOT_FOUND: cast slot %s references %s"), *Member.Slot, *Member.EntityId);
			State = EEpicodeSandboxSessionState::Failed;
			OutError = Error;
			return false;
		}
		if (Entity->Type != EEpicodeEntityType::Character)
		{
			Error = FString::Printf(TEXT("CAST_ENTITY_TYPE_INVALID: cast slot %s references %s"), *Member.Slot, *Member.EntityId);
			State = EEpicodeSandboxSessionState::Failed;
			OutError = Error;
			return false;
		}
	}

	Production = InProduction;
	Scene = InScene;
	Config = InConfig;
	if (!BuildWorld(OutError))
	{
		Error = OutError;
		State = EEpicodeSandboxSessionState::Failed;
		return false;
	}
	State = EEpicodeSandboxSessionState::Ready;
	Error.Reset();
	return true;
}

bool FEpicodeSandboxSession::LoadLegacy(const FEpicodeSceneDocument& InScene, const FEpicodeSandboxSessionConfig& InConfig, FString& OutError)
{
	if (!InScene.Validate(OutError))
	{
		Error = FString::Printf(TEXT("SCENE_INVALID: %s"), *OutError);
		State = EEpicodeSandboxSessionState::Failed;
		return false;
	}
	FEpicodeProductionDocument Legacy = FEpicodeProductionLegacy::MakeDefaultView(InScene);
	return Load(Legacy, InScene, InConfig, OutError);
}

bool FEpicodeSandboxSession::BuildWorld(FString& OutError)
{
	FEpicodeSandboxSessionConfig EffectiveConfig = Config;
	if (EffectiveConfig.SessionId.IsEmpty())
	{
		EffectiveConfig.SessionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	}
	if (EffectiveConfig.TickRate <= 0)
	{
		const UEpicodeSandboxSettings* Settings = UEpicodeSandboxSettings::Get();
		EffectiveConfig.TickRate = Settings ? Settings->FixedTickRate : 30;
	}
	if (EffectiveConfig.Seed == 0)
	{
		EffectiveConfig.Seed = static_cast<int32>(FCrc::StrCrc32(*EffectiveConfig.SessionId));
	}
	Config = EffectiveConfig;

	FEpicodeSandboxWorld NewWorld = FEpicodeSandboxWorld::Create(Config.SessionId, Production.ProductionId, Scene.SceneId, Config.Seed);
	NewWorld.Tick = 0;
	NewWorld.Narrative = FEpicodeSandboxNarrativeState();
	if (Production.Narrative.IsValid())
	{
		if (!FEpicodeSandboxNarrativeState::FromJson(Production.Narrative, NewWorld.Narrative, OutError))
		{
			return false;
		}
		FString EntryBeatId;
		if (Production.Narrative->TryGetStringField(TEXT("entryBeatId"), EntryBeatId) && !EntryBeatId.IsEmpty())
		{
			NewWorld.Narrative.CurrentBeatId = EntryBeatId;
		}
	}

	for (const FEpicodeEntityRecord& Entity : Scene.Entities)
	{
		if (Entity.Type == EEpicodeEntityType::Group)
		{
			continue;
		}
		FEpicodeSandboxEntity SandboxEntity;
		SandboxEntity.Id = Entity.Id.ToString(EGuidFormats::DigitsWithHyphens);
		SandboxEntity.Kind = MapEntityKind(Entity, Production);
		SandboxEntity.ArchetypeId = GetArchetypeId(Entity);
		SandboxEntity.Location = Entity.Transform.Location;
		SandboxEntity.Rotation = Entity.Transform.Rotation;
		SandboxEntity.bVisible = Entity.bVisible;
		SandboxEntity.Data = Entity.Data.IsValid() ? FEpicodeSandboxJson::CloneObject(Entity.Data) : MakeShared<FJsonObject>();
		if (!NewWorld.AddEntity(SandboxEntity, OutError))
		{
			return false;
		}
	}

	for (const FEpicodeProductionCastMember& Member : Production.Cast)
	{
		FEpicodeSandboxParticipant Participant;
		Participant.Id = Member.Slot;
		if (!ParseSandboxParticipantKind(Member.ParticipantKind, Participant.Kind))
		{
			OutError = FString::Printf(TEXT("CAST_PARTICIPANT_INVALID: %s"), *Member.ParticipantKind);
			return false;
		}
		Participant.CharacterEntityId = Member.EntityId;
		Participant.bConnected = true;
		if (!NewWorld.AddParticipant(Participant, OutError))
		{
			return false;
		}
	}

	if (!NewWorld.Validate(OutError))
	{
		return false;
	}

	World = NewWorld;
	Clock.Configure(Config.TickRate, 0);
	RandomStreams.Initialize(Config.Seed);
	Events.Reset(1);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("productionId"), Production.ProductionId);
	Payload->SetStringField(TEXT("sceneId"), Scene.SceneId);
	Payload->SetNumberField(TEXT("seed"), Config.Seed);
	Payload->SetNumberField(TEXT("entities"), World.Entities.Num());
	Payload->SetNumberField(TEXT("participants"), World.Participants.Num());
	Events.Append(0, TEXT("sandbox.loaded"), TEXT("system"), Payload);
	return true;
}

bool FEpicodeSandboxSession::Reset(FString& OutError)
{
	if (State == EEpicodeSandboxSessionState::Uninitialized || Production.ProductionId.IsEmpty())
	{
		OutError = TEXT("SESSION_NOT_LOADED: sandbox session has no production");
		return false;
	}
	if (State == EEpicodeSandboxSessionState::Running)
	{
		OutError = TEXT("SESSION_RUNNING: stop the sandbox session before reset");
		return false;
	}
	if (!BuildWorld(OutError))
	{
		Error = OutError;
		State = EEpicodeSandboxSessionState::Failed;
		return false;
	}
	Events.Append(0, TEXT("sandbox.reset"), TEXT("system"));
	State = EEpicodeSandboxSessionState::Ready;
	Error.Reset();
	return true;
}

bool FEpicodeSandboxSession::Start(FString& OutError)
{
	if (State == EEpicodeSandboxSessionState::Closed)
	{
		OutError = TEXT("SESSION_CLOSED: sandbox session is closed");
		return false;
	}
	if (State == EEpicodeSandboxSessionState::Uninitialized || State == EEpicodeSandboxSessionState::Failed)
	{
		OutError = TEXT("SESSION_NOT_READY: sandbox session has not loaded");
		return false;
	}
	if (State == EEpicodeSandboxSessionState::Running)
	{
		return true;
	}
	State = EEpicodeSandboxSessionState::Running;
	Events.Append(World.Tick, TEXT("sandbox.started"), TEXT("system"));
	return true;
}

bool FEpicodeSandboxSession::Stop(FString& OutError)
{
	if (State == EEpicodeSandboxSessionState::Closed)
	{
		OutError = TEXT("SESSION_CLOSED: sandbox session is closed");
		return false;
	}
	if (State != EEpicodeSandboxSessionState::Running)
	{
		return true;
	}
	State = EEpicodeSandboxSessionState::Ready;
	Events.Append(World.Tick, TEXT("sandbox.stopped"), TEXT("system"));
	return true;
}

bool FEpicodeSandboxSession::Close(FString& OutError)
{
	if (State == EEpicodeSandboxSessionState::Closed)
	{
		return true;
	}
	if (State == EEpicodeSandboxSessionState::Running)
	{
		State = EEpicodeSandboxSessionState::Ready;
	}
	Events.Append(World.Tick, TEXT("sandbox.closed"), TEXT("system"));
	State = EEpicodeSandboxSessionState::Closed;
	OutError.Reset();
	return true;
}

int32 FEpicodeSandboxSession::Tick(double InDeltaSeconds)
{
	if (State != EEpicodeSandboxSessionState::Running || InDeltaSeconds <= 0.0)
	{
		return 0;
	}
	const int32 Ticks = Clock.Advance(InDeltaSeconds, FMath::Max(1, Config.MaxTicksPerAdvance), [this](int64 InTick, const TArray<FEpicodeSandboxQueuedCommand>& InCommands)
	{
		World.Tick = InTick;
	});
	return Ticks;
}

bool FEpicodeSandboxSession::SaveSnapshot(const FString& InSessionRoot, FString& OutError) const
{
	return FEpicodeSandboxPersistence::Save(World, Events, InSessionRoot, OutError);
}

bool FEpicodeSandboxSession::RestoreSnapshot(const FString& InSessionRoot, FString& OutError)
{
	FEpicodeSandboxWorld RestoredWorld;
	FEpicodeSandboxEventStream RestoredEvents;
	if (!FEpicodeSandboxPersistence::Load(InSessionRoot, RestoredWorld, RestoredEvents, OutError))
	{
		return false;
	}
	World = RestoredWorld;
	Events = RestoredEvents;
	Clock.Configure(FMath::Max(1, Config.TickRate), World.Tick);
	RandomStreams.Initialize(World.Seed);
	State = EEpicodeSandboxSessionState::Ready;
	Error.Reset();
	return true;
}

FString FEpicodeSandboxSession::GetDigest() const
{
	return FEpicodeSandboxDigest::ComputeCombinedDigest(World, Events.GetEvents());
}
