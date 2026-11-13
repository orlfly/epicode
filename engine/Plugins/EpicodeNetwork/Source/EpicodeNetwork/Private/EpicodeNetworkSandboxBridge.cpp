#include "EpicodeNetworkSandboxBridge.h"

#include "EpicodeSandboxJson.h"
#include "EpicodeSandboxSettings.h"

namespace
{
	FString SandboxStateToString(EEpicodeSandboxSessionState InState)
	{
		switch (InState)
		{
		case EEpicodeSandboxSessionState::Uninitialized: return TEXT("uninitialized");
		case EEpicodeSandboxSessionState::Ready: return TEXT("ready");
		case EEpicodeSandboxSessionState::Running: return TEXT("running");
		case EEpicodeSandboxSessionState::Closed: return TEXT("closed");
		case EEpicodeSandboxSessionState::Failed: return TEXT("failed");
		default: return TEXT("unknown");
		}
	}

}

bool FEpicodeNetworkSandboxBridge::Create(const FEpicodeProductionDocument& InProduction, const FEpicodeSceneDocument& InScene, const FEpicodeSandboxSessionConfig& InConfig, FString& OutError)
{
	if (!UEpicodeSandboxSettings::Get()->bEnableSandboxDirector)
	{
		OutError = TEXT("SANDBOX_DISABLED: sandbox director is disabled by settings.");
		return false;
	}
	if (!Session.Load(InProduction, InScene, InConfig, OutError))
	{
		return false;
	}
	bLegacy = false;
	return FinishCreate(InConfig, OutError);
}

bool FEpicodeNetworkSandboxBridge::CreateLegacy(const FEpicodeSceneDocument& InScene, const FEpicodeSandboxSessionConfig& InConfig, FString& OutError)
{
	if (!UEpicodeSandboxSettings::Get()->bEnableSandboxDirector)
	{
		OutError = TEXT("SANDBOX_DISABLED: sandbox director is disabled by settings.");
		return false;
	}
	if (!Session.LoadLegacy(InScene, InConfig, OutError))
	{
		return false;
	}
	bLegacy = true;
	for (const FString& ParticipantId : Session.GetWorld().GetSortedParticipantIds())
	{
		if (FEpicodeSandboxParticipant* Participant = Session.GetMutableWorld().FindParticipant(ParticipantId))
		{
			Participant->bConnected = false;
		}
	}
	return FinishCreate(InConfig, OutError);
}

bool FEpicodeNetworkSandboxBridge::FinishCreate(const FEpicodeSandboxSessionConfig& InConfig, FString& OutError)
{
	Config = InConfig;
	bCreated = true;
	if (!Session.Start(OutError))
	{
		bCreated = false;
		return false;
	}
	ProjectedEntityIds.Reset();
	for (const FEpicodeSandboxProjectionCommand& Command : FEpicodeSandboxProjection::Build(Session.GetWorld()))
	{
		ProjectedEntityIds.Add(Command.EntityId);
	}
	LastProjectedSequence = Session.GetEvents().GetLastSequence();
	return true;
}

bool FEpicodeNetworkSandboxBridge::Start(FString& OutError)
{
	if (!bCreated)
	{
		OutError = TEXT("SANDBOX_NOT_CREATED: create the bridge before starting.");
		return false;
	}
	return Session.Start(OutError);
}

bool FEpicodeNetworkSandboxBridge::Close(FString& OutError)
{
	if (!bCreated)
	{
		OutError = TEXT("SANDBOX_NOT_CREATED: bridge is not active.");
		return false;
	}
	const bool bResult = Session.Close(OutError);
	bCreated = false;
	return bResult;
}

void FEpicodeNetworkSandboxBridge::Tick(double InDeltaSeconds)
{
	if (bCreated)
	{
		Session.Tick(InDeltaSeconds);
	}
}

bool FEpicodeNetworkSandboxBridge::BindPlayer(const FString& InPlayerId, EEpicodeSandboxParticipantKind InKind, const FString& InCharacterId, FString& OutError)
{
	if (InPlayerId.IsEmpty())
	{
		OutError = TEXT("INVALID_PARAMS: playerId is required.");
		return false;
	}
	FEpicodeSandboxWorld& World = Session.GetMutableWorld();
	if (!InCharacterId.IsEmpty() && !World.FindEntity(InCharacterId))
	{
		OutError = FString::Printf(TEXT("CHARACTER_NOT_FOUND: %s"), *InCharacterId);
		return false;
	}
	if (!InCharacterId.IsEmpty())
	{
		for (const FString& ParticipantId : World.GetSortedParticipantIds())
		{
			const FEpicodeSandboxParticipant& Other = World.Participants.FindChecked(ParticipantId);
			if (ParticipantId != InPlayerId && Other.bConnected && Other.CharacterEntityId == InCharacterId)
			{
				OutError = FString::Printf(TEXT("CHARACTER_SLOT_TAKEN: %s is already controlled by %s"), *InCharacterId, *ParticipantId);
				return false;
			}
		}
	}
	FEpicodeSandboxParticipant* Existing = World.FindParticipant(InPlayerId);
	if (Existing)
	{
		Existing->Kind = InKind;
		Existing->CharacterEntityId = InCharacterId;
		Existing->bConnected = true;
		Existing->bTakeover = false;
		return true;
	}
	FEpicodeSandboxParticipant Participant;
	Participant.Id = InPlayerId;
	Participant.Kind = InKind;
	Participant.CharacterEntityId = InCharacterId;
	Participant.bConnected = true;
	return World.AddParticipant(Participant, OutError);
}

bool FEpicodeNetworkSandboxBridge::SetPlayerConnected(const FString& InPlayerId, bool bInConnected, FString& OutError)
{
	FEpicodeSandboxParticipant* Participant = Session.GetMutableWorld().FindParticipant(InPlayerId);
	if (!Participant)
	{
		OutError = FString::Printf(TEXT("PARTICIPANT_NOT_FOUND: %s"), *InPlayerId);
		return false;
	}
	Participant->bConnected = bInConnected;
	if (!bInConnected)
	{
		Participant->bTakeover = false;
	}
	return true;
}

bool FEpicodeNetworkSandboxBridge::AssignCharacter(const FString& InPlayerId, const FString& InCharacterId, FString& OutError)
{
	FEpicodeSandboxWorld& World = Session.GetMutableWorld();
	FEpicodeSandboxParticipant* Participant = World.FindParticipant(InPlayerId);
	if (!Participant)
	{
		OutError = FString::Printf(TEXT("PARTICIPANT_NOT_FOUND: %s"), *InPlayerId);
		return false;
	}
	if (!InCharacterId.IsEmpty() && !World.FindEntity(InCharacterId))
	{
		OutError = FString::Printf(TEXT("CHARACTER_NOT_FOUND: %s"), *InCharacterId);
		return false;
	}
	if (!InCharacterId.IsEmpty())
	{
		for (const FString& ParticipantId : World.GetSortedParticipantIds())
		{
			const FEpicodeSandboxParticipant& Other = World.Participants.FindChecked(ParticipantId);
			if (ParticipantId != InPlayerId && Other.bConnected && Other.CharacterEntityId == InCharacterId)
			{
				OutError = FString::Printf(TEXT("CHARACTER_SLOT_TAKEN: %s is already controlled by %s"), *InCharacterId, *ParticipantId);
				return false;
			}
		}
	}
	Participant->CharacterEntityId = InCharacterId;
	return true;
}

bool FEpicodeNetworkSandboxBridge::RecordPlayerTransform(const FString& InPlayerId, const FVector& InLocation, const FRotator& InRotation, FString& OutError)
{
	FEpicodeSandboxWorld& World = Session.GetMutableWorld();
	FEpicodeSandboxParticipant* Participant = World.FindParticipant(InPlayerId);
	if (!Participant || Participant->CharacterEntityId.IsEmpty())
	{
		OutError = FString::Printf(TEXT("PARTICIPANT_NOT_FOUND: %s has no character"), *InPlayerId);
		return false;
	}
	FEpicodeSandboxEntity* Entity = World.FindEntity(Participant->CharacterEntityId);
	if (!Entity)
	{
		OutError = FString::Printf(TEXT("CHARACTER_NOT_FOUND: %s"), *Participant->CharacterEntityId);
		return false;
	}
	Entity->Location = InLocation;
	Entity->Rotation = InRotation;
	FEpicodeSandboxEvent& Event = Session.GetMutableEvents().Append(World.Tick, TEXT("sandbox.playerMoved"), InPlayerId, nullptr, Entity->Id);
	Event.Payload = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Changes;
	TSharedPtr<FJsonObject> Location = MakeShared<FJsonObject>();
	Location->SetStringField(TEXT("op"), TEXT("entity.location"));
	Location->SetStringField(TEXT("id"), Entity->Id);
	Location->SetArrayField(TEXT("location"), {
		MakeShared<FJsonValueNumber>(InLocation.X),
		MakeShared<FJsonValueNumber>(InLocation.Y),
		MakeShared<FJsonValueNumber>(InLocation.Z)
	});
	Changes.Add(MakeShared<FJsonValueObject>(Location));
	TSharedPtr<FJsonObject> Rotation = MakeShared<FJsonObject>();
	Rotation->SetStringField(TEXT("op"), TEXT("entity.rotation"));
	Rotation->SetStringField(TEXT("id"), Entity->Id);
	Rotation->SetArrayField(TEXT("rotation"), {
		MakeShared<FJsonValueNumber>(InRotation.Pitch),
		MakeShared<FJsonValueNumber>(InRotation.Yaw),
		MakeShared<FJsonValueNumber>(InRotation.Roll)
	});
	Changes.Add(MakeShared<FJsonValueObject>(Rotation));
	Event.Payload->SetArrayField(TEXT("changes"), Changes);
	return true;
}

bool FEpicodeNetworkSandboxBridge::CanSubmit(const FString& InPlayerId, EEpicodeSessionAction InAction, FString& OutError) const
{
	const FEpicodeSandboxParticipant* Participant = Session.GetWorld().FindParticipant(InPlayerId);
	if (!Participant)
	{
		OutError = FString::Printf(TEXT("PARTICIPANT_NOT_FOUND: %s"), *InPlayerId);
		return false;
	}
	if (Participant->Kind == EEpicodeSandboxParticipantKind::Viewer)
	{
		OutError = TEXT("VIEWER_READ_ONLY: viewer cannot modify the sandbox");
		return false;
	}
	switch (InAction)
	{
	case EEpicodeSessionAction::WorldInteract:
	case EEpicodeSessionAction::ControlOwnCharacter:
	case EEpicodeSessionAction::ViewPrivateInventory:
		return true;
	case EEpicodeSessionAction::ManageSession:
	case EEpicodeSessionAction::ControlScene:
		if (Participant->Kind == EEpicodeSandboxParticipantKind::Owner || Participant->Kind == EEpicodeSandboxParticipantKind::Director)
		{
			return true;
		}
		OutError = TEXT("FORBIDDEN: sandbox management requires owner or director");
		return false;
	case EEpicodeSessionAction::QuerySession:
		return true;
	default:
		OutError = TEXT("FORBIDDEN: unsupported sandbox action");
		return false;
	}
}

TArray<FEpicodeSandboxProjectionCommand> FEpicodeNetworkSandboxBridge::BuildProjection()
{
	TArray<FEpicodeSandboxProjectionCommand> Commands = FEpicodeSandboxProjection::Build(Session.GetWorld());
	TSet<FString> CurrentIds;
	for (const FEpicodeSandboxProjectionCommand& Command : Commands)
	{
		CurrentIds.Add(Command.EntityId);
	}
	for (const FString& PreviousId : ProjectedEntityIds)
	{
		if (!CurrentIds.Contains(PreviousId))
		{
			FEpicodeSandboxProjectionCommand& Remove = Commands.AddDefaulted_GetRef();
			Remove.Operation = TEXT("remove");
			Remove.EntityId = PreviousId;
		}
	}
	ProjectedEntityIds = CurrentIds;
	LastProjectedSequence = Session.GetEvents().GetLastSequence();
	return Commands;
}

TArray<FEpicodeNetworkEvent> FEpicodeNetworkSandboxBridge::CollectEventsSince(int64 InSequence) const
{
	TArray<FEpicodeNetworkEvent> Result;
	for (const FEpicodeSandboxEvent& Event : Session.GetEvents().GetEvents())
	{
		if (Event.Sequence <= InSequence)
		{
			continue;
		}
		FEpicodeNetworkEvent& Out = Result.AddDefaulted_GetRef();
		Out.Sequence = Event.Sequence;
		Out.Timestamp = static_cast<double>(Event.Tick);
		Out.Type = Event.Type;
		Out.PlayerId = Event.ActorId;
		Out.EntityId = Event.TargetId;
		Out.Payload = Event.Payload.IsValid() ? FEpicodeSandboxJson::CloneObject(Event.Payload) : MakeShared<FJsonObject>();
	}
	return Result;
}

TSharedPtr<FJsonObject> FEpicodeNetworkSandboxBridge::GetReplicationSnapshot() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("state"), SandboxStateToString(Session.GetState()));
	Result->SetStringField(TEXT("sessionId"), Session.GetConfig().SessionId);
	Result->SetStringField(TEXT("digest"), GetDigest());
	Result->SetNumberField(TEXT("eventSequence"), static_cast<double>(GetEventSequence()));
	Result->SetNumberField(TEXT("seed"), Session.GetConfig().Seed);
	Result->SetBoolField(TEXT("replayable"), IsReplayable());
	Result->SetBoolField(TEXT("legacy"), bLegacy);
	TArray<TSharedPtr<FJsonValue>> Events;
	for (const FEpicodeNetworkEvent& Event : CollectEventsSince(0))
	{
		Events.Add(MakeShared<FJsonValueObject>(Event.ToJson()));
	}
	Result->SetArrayField(TEXT("events"), Events);
	return Result;
}

bool FEpicodeNetworkSandboxBridge::IsActive() const
{
	return bCreated && (Session.GetState() == EEpicodeSandboxSessionState::Running || Session.GetState() == EEpicodeSandboxSessionState::Ready);
}

EEpicodeSandboxSessionState FEpicodeNetworkSandboxBridge::GetState() const
{
	return Session.GetState();
}

FString FEpicodeNetworkSandboxBridge::GetDigest() const
{
	return Session.GetDigest();
}

int64 FEpicodeNetworkSandboxBridge::GetEventSequence() const
{
	return Session.GetEvents().GetLastSequence();
}
