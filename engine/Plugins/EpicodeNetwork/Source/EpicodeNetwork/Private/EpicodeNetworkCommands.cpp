#include "EpicodeNetworkCommands.h"

#include "EpicodeRuntimeMode.h"
#include "EpicodeCommand.h"
#include "EpicodeNetworkCharacter.h"
#include "EpicodeNetworkPlayerController.h"
#include "EpicodePerformanceTypes.h"
#include "EpicodeWorldState.h"
#include "EpicodeNetworkSession.h"
#include "EpicodeNetworkSubsystem.h"
#include "EpicodeNetworkTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

namespace
{
	FEpicodeCommandResult FailureFromSession(const FString& InError)
	{
		int32 ColonIndex = INDEX_NONE;
		if (InError.FindChar(TEXT(':'), ColonIndex))
		{
			const FString Code = InError.Left(ColonIndex).TrimStartAndEnd();
			const FString Message = InError.Mid(ColonIndex + 1).TrimStartAndEnd();
			return FEpicodeCommandResult::Failure(Code, Message, InError);
		}
		return FEpicodeCommandResult::Failure(TEXT("NETWORK_ERROR"), InError, InError);
	}

	FEpicodeCommandResult FailureFromParams(const FString& InError)
	{
		return FEpicodeCommandResult::Failure(TEXT("INVALID_PARAMS"), InError);
	}

	FEpicodeCommandResult GetSubsystem(UEpicodeNetworkSubsystem*& OutSubsystem)
	{
		OutSubsystem = UEpicodeNetworkSubsystem::Get();
		if (!OutSubsystem)
		{
			return FEpicodeCommandResult::Failure(TEXT("NETWORK_UNAVAILABLE"), TEXT("EpicodeNetwork subsystem is not available."));
		}
		return FEpicodeCommandResult::Success();
	}

	bool GetRequiredPlayerId(const TSharedPtr<FJsonObject>& Params, FString& OutPlayerId, FEpicodeCommandResult& OutFailure)
	{
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("playerId"), OutPlayerId, Error))
		{
			OutFailure = FailureFromParams(Error);
			return false;
		}
		return true;
	}

	UWorld* FindGameWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Game && Context.World())
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	AEpicodeNetworkCharacter* FindPlayerCharacter(const FString& InPlayerId, FEpicodeCommandResult& OutFailure)
	{
		UWorld* World = FindGameWorld();
		if (!World)
		{
			OutFailure = FEpicodeCommandResult::Failure(TEXT("WORLD_NOT_FOUND"), TEXT("No runtime game world is available."));
			return nullptr;
		}
		for (TActorIterator<AEpicodeNetworkCharacter> It(World); It; ++It)
		{
			if ((*It)->PlayerId == InPlayerId)
			{
				return *It;
			}
		}
		OutFailure = FEpicodeCommandResult::Failure(TEXT("CHARACTER_NOT_FOUND"),
			FString::Printf(TEXT("No possessed character for player '%s'."), *InPlayerId));
		return nullptr;
	}

	FEpicodeCommandResult SnapshotResult(UEpicodeNetworkSubsystem& Subsystem, const FString& InPlayerId)
	{
		FEpicodeNetworkSession* Session = Subsystem.GetSession();
		if (!Session)
		{
			return FEpicodeCommandResult::Failure(TEXT("SESSION_NOT_FOUND"), TEXT("Create a session first."));
		}
		if (!InPlayerId.IsEmpty())
		{
			FString Error;
			if (!Session->CanPerform(InPlayerId, EEpicodeSessionAction::QuerySession, Error))
			{
				return FailureFromSession(Error);
			}
		}
		TSharedPtr<FJsonObject> Snapshot = Session->ToJson();
		FEpicodeRuntimeMode::Resolve().ApplyToJson(Snapshot);
		const FEpicodeWorldState& WorldState = Subsystem.GetWorldState();
		TArray<TSharedPtr<FJsonValue>> Items;
		for (const FEpicodeItemRecord& Item : WorldState.GetItems())
		{
			Items.Add(MakeShared<FJsonValueObject>(Item.ToJson()));
		}
		Snapshot->SetArrayField(TEXT("items"), Items);
		TArray<TSharedPtr<FJsonValue>> Builds;
		for (const FEpicodeBuildRecord& Build : WorldState.GetBuilds())
		{
			Builds.Add(MakeShared<FJsonValueObject>(Build.ToJson()));
		}
		Snapshot->SetArrayField(TEXT("builds"), Builds);
		const FEpicodeNetworkSceneSummary& Summary = Subsystem.GetSceneSummary();
		if (Summary.Environment.IsValid())
		{
			Snapshot->SetObjectField(TEXT("environment"), Summary.Environment);
		}
		Snapshot->SetObjectField(TEXT("scene"), Summary.ToJson());
		return FEpicodeCommandResult::Success(Snapshot);
	}

	FEpicodeCommandResult MutateSession(UEpicodeNetworkSubsystem& Subsystem, bool bResult, const FString& InError)
	{
		if (!bResult)
		{
			return FailureFromSession(InError);
		}
		Subsystem.Sync();
		return SnapshotResult(Subsystem, FString());
	}

	bool ResolveParticipant(const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context, UEpicodeNetworkSubsystem& Subsystem, FString& OutPlayerId, FEpicodeCommandResult& OutFailure)
	{
		if (Context.ParticipantToken.IsEmpty())
		{
			FString RequestedId;
			EpicodeJson::GetOptionalString(Params, TEXT("playerId"), RequestedId);
			if (RequestedId.IsEmpty())
			{
				EpicodeJson::GetOptionalString(Params, TEXT("requesterId"), RequestedId);
			}
			OutPlayerId = RequestedId;
			return true;
		}
		FString Error;
		if (!Subsystem.ResolveAgentToken(Context.ParticipantToken, OutPlayerId, Error))
		{
			OutFailure = FailureFromSession(Error);
			return false;
		}
		FString RequestedId;
		EpicodeJson::GetOptionalString(Params, TEXT("playerId"), RequestedId);
		if (RequestedId.IsEmpty())
		{
			EpicodeJson::GetOptionalString(Params, TEXT("requesterId"), RequestedId);
		}
		if (!RequestedId.IsEmpty() && RequestedId != OutPlayerId)
		{
			OutFailure = FEpicodeCommandResult::Failure(TEXT("FORBIDDEN"), TEXT("The participant token does not match the requested player."));
			return false;
		}
		FEpicodeNetworkSession* Session = Subsystem.GetSession();
		if (Session && Context.ExpectedStateVersion >= 0.0
			&& Context.ExpectedStateVersion != static_cast<double>(Session->GetWorldVersion()))
		{
			OutFailure = FEpicodeCommandResult::Failure(TEXT("STALE_STATE"),
				FString::Printf(TEXT("expectedStateVersion %.0f does not match worldVersion %llu."),
					Context.ExpectedStateVersion, Session->GetWorldVersion()));
			return false;
		}
		if (!Subsystem.CheckAgentCommandRate(OutPlayerId, Error))
		{
			OutFailure = FailureFromSession(Error);
			return false;
		}
		return true;
	}

	bool GetRequiredTarget(const TSharedPtr<FJsonObject>& Params, const TCHAR* Field, FVector& OutTarget, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Params.IsValid() && Params->TryGetArrayField(Field, Values) && Values && Values->Num() == 3)
		{
			OutTarget = FVector((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
			return OutTarget.ContainsNaN() ? (OutError = TEXT("target must be finite"), false) : true;
		}
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (Params.IsValid() && Params->TryGetObjectField(Field, Object) && Object && Object->IsValid())
		{
			double X = 0.0;
			double Y = 0.0;
			double Z = 0.0;
			if ((*Object)->TryGetNumberField(TEXT("x"), X) && (*Object)->TryGetNumberField(TEXT("y"), Y) && (*Object)->TryGetNumberField(TEXT("z"), Z))
			{
				OutTarget = FVector(X, Y, Z);
				return OutTarget.ContainsNaN() ? (OutError = TEXT("target must be finite"), false) : true;
			}
		}
		OutError = FString::Printf(TEXT("%s must be [x,y,z] or {x,y,z}."), Field);
		return false;
	}
}

void FEpicodeNetworkCommands::Register(FEpicodeCommandRegistry& Registry)
{
	Registry.Register(TEXT("session.create"), TEXT("Create and start an authoritative production session."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString SceneId;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("sceneId"), SceneId, Error))
		{
			return FailureFromParams(Error);
		}
		FString PlayerId;
		FString DisplayName;
		EpicodeJson::GetOptionalString(Params, TEXT("playerId"), PlayerId);
		EpicodeJson::GetOptionalString(Params, TEXT("displayName"), DisplayName);
		double MaxPlayers = 0.0;
		double MaxViewers = 0.0;
		double GraceSeconds = -1.0;
		EpicodeJson::GetOptionalNumber(Params, TEXT("maxPlayers"), MaxPlayers, 0.0);
		EpicodeJson::GetOptionalNumber(Params, TEXT("maxViewers"), MaxViewers, 0.0);
		EpicodeJson::GetOptionalNumber(Params, TEXT("reconnectGraceSeconds"), GraceSeconds, -1.0);
		UEpicodeNetworkSubsystem* Subsystem = nullptr;
		FEpicodeCommandResult SubsystemResult = GetSubsystem(Subsystem);
		if (!SubsystemResult.bSuccess)
		{
			return SubsystemResult;
		}
		if (!Subsystem->CreateSession(SceneId, PlayerId, DisplayName, static_cast<int32>(MaxPlayers), GraceSeconds, Error, static_cast<int32>(MaxViewers)))
		{
			return FailureFromSession(Error);
		}
		return SnapshotResult(*Subsystem, FString());
	});

	Registry.Register(TEXT("session.start"), TEXT("Start a created session."), [](const TSharedPtr<FJsonObject>&, const FEpicodeCommandContext&)
	{
		UEpicodeNetworkSubsystem* Subsystem = nullptr;
		FEpicodeCommandResult SubsystemResult = GetSubsystem(Subsystem);
		if (!SubsystemResult.bSuccess)
		{
			return SubsystemResult;
		}
		FString Error;
		if (!Subsystem->StartSession(Error))
		{
			return FailureFromSession(Error);
		}
		return SnapshotResult(*Subsystem, FString());
	});

	Registry.Register(TEXT("session.join"), TEXT("Join an active session as a player."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString PlayerId;
		FString DisplayName;
		FString RoleText;
		FString KindText;
		EpicodeJson::GetOptionalString(Params, TEXT("playerId"), PlayerId);
		EpicodeJson::GetOptionalString(Params, TEXT("displayName"), DisplayName);
		EpicodeJson::GetOptionalString(Params, TEXT("role"), RoleText, TEXT("performer"));
		EpicodeJson::GetOptionalString(Params, TEXT("participantKind"), KindText, TEXT("performer"));
		EEpicodeSessionRole Role = EEpicodeSessionRole::Performer;
		if (!ParseSessionRole(RoleText, Role))
		{
			return FEpicodeCommandResult::Failure(TEXT("INVALID_PARAMS"), FString::Printf(TEXT("Unknown session role: %s"), *RoleText));
		}
		EEpicodeParticipantKind Kind = EEpicodeParticipantKind::Performer;
		if (!ParseParticipantKind(KindText, Kind))
		{
			return FEpicodeCommandResult::Failure(TEXT("INVALID_PARAMS"), FString::Printf(TEXT("Unknown participant kind: %s"), *KindText));
		}
		UEpicodeNetworkSubsystem* Subsystem = nullptr;
		FEpicodeCommandResult SubsystemResult = GetSubsystem(Subsystem);
		if (!SubsystemResult.bSuccess)
		{
			return SubsystemResult;
		}
		FString Error;
		FString JoinedPlayerId;
		if (!Subsystem->JoinSession(PlayerId, DisplayName, Role, JoinedPlayerId, Error, Kind))
		{
			return FailureFromSession(Error);
		}
		Subsystem->Sync();
		TSharedPtr<FJsonObject> Result = Subsystem->GetSession()->ToJson();
		Result->SetStringField(TEXT("playerId"), JoinedPlayerId);
		Result->SetStringField(TEXT("participantKind"), LexToString(Kind));
		Result->SetNumberField(TEXT("stateVersion"), static_cast<double>(Subsystem->GetSession()->GetWorldVersion()));
		if (Kind == EEpicodeParticipantKind::Agent)
		{
			Result->SetStringField(TEXT("participantToken"), Subsystem->IssueAgentToken(JoinedPlayerId));
		}
		return FEpicodeCommandResult::Success(Result);
	});

	Registry.Register(TEXT("session.leave"), TEXT("Leave the active session."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString PlayerId;
		FEpicodeCommandResult Failure;
		if (!GetRequiredPlayerId(Params, PlayerId, Failure))
		{
			return Failure;
		}
		UEpicodeNetworkSubsystem* Subsystem = nullptr;
		FEpicodeCommandResult SubsystemResult = GetSubsystem(Subsystem);
		if (!SubsystemResult.bSuccess)
		{
			return SubsystemResult;
		}
		FString Error;
		return MutateSession(*Subsystem, Subsystem->LeaveSession(PlayerId, Error), Error);
	});

	Registry.Register(TEXT("session.reconnect"), TEXT("Reconnect a player inside the grace period."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString PlayerId;
		FEpicodeCommandResult Failure;
		if (!GetRequiredPlayerId(Params, PlayerId, Failure))
		{
			return Failure;
		}
		UEpicodeNetworkSubsystem* Subsystem = nullptr;
		FEpicodeCommandResult SubsystemResult = GetSubsystem(Subsystem);
		if (!SubsystemResult.bSuccess)
		{
			return SubsystemResult;
		}
		FString Error;
		return MutateSession(*Subsystem, Subsystem->ReconnectSession(PlayerId, Error), Error);
	});

	Registry.Register(TEXT("session.status"), TEXT("Describe the current network session."), [](const TSharedPtr<FJsonObject>&, const FEpicodeCommandContext&)
	{
		UEpicodeNetworkSubsystem* Subsystem = nullptr;
		FEpicodeCommandResult SubsystemResult = GetSubsystem(Subsystem);
		if (!SubsystemResult.bSuccess)
		{
			return SubsystemResult;
		}
		return FEpicodeCommandResult::Success(Subsystem->GetStatusJson());
	}, false);

	Registry.Register(TEXT("session.snapshot"), TEXT("Return the authoritative session snapshot."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString PlayerId;
		EpicodeJson::GetOptionalString(Params, TEXT("playerId"), PlayerId);
		UEpicodeNetworkSubsystem* Subsystem = nullptr;
		FEpicodeCommandResult SubsystemResult = GetSubsystem(Subsystem);
		if (!SubsystemResult.bSuccess)
		{
			return SubsystemResult;
		}
		return SnapshotResult(*Subsystem, PlayerId);
	}, false);

	Registry.Register(TEXT("session.export"), TEXT("Write the session snapshot and event log."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString PlayerId;
		EpicodeJson::GetOptionalString(Params, TEXT("playerId"), PlayerId);
		UEpicodeNetworkSubsystem* Subsystem = nullptr;
		FEpicodeCommandResult SubsystemResult = GetSubsystem(Subsystem);
		if (!SubsystemResult.bSuccess)
		{
			return SubsystemResult;
		}
		FEpicodeNetworkSession* Session = Subsystem->GetSession();
		if (!Session)
		{
			return FEpicodeCommandResult::Failure(TEXT("SESSION_NOT_FOUND"), TEXT("Create a session first."));
		}
		if (!PlayerId.IsEmpty())
		{
			FString PermissionError;
			if (!Session->CanPerform(PlayerId, EEpicodeSessionAction::ManageSession, PermissionError))
			{
				return FailureFromSession(PermissionError);
			}
		}
		FString SessionPath;
		FString EventPath;
		FString Error;
		if (!Subsystem->ExportSession(SessionPath, EventPath, Error))
		{
			return FailureFromSession(Error);
		}
		TSharedPtr<FJsonObject> Result = Session->ToJson();
		Result->SetStringField(TEXT("sessionPath"), SessionPath);
		Result->SetStringField(TEXT("eventPath"), EventPath);
		return FEpicodeCommandResult::Success(Result);
	});

	Registry.Register(TEXT("session.end"), TEXT("End the active session."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString PlayerId;
		EpicodeJson::GetOptionalString(Params, TEXT("playerId"), PlayerId);
		UEpicodeNetworkSubsystem* Subsystem = nullptr;
		FEpicodeCommandResult SubsystemResult = GetSubsystem(Subsystem);
		if (!SubsystemResult.bSuccess)
		{
			return SubsystemResult;
		}
		FEpicodeNetworkSession* Session = Subsystem->GetSession();
		if (!Session)
		{
			return FEpicodeCommandResult::Failure(TEXT("SESSION_NOT_FOUND"), TEXT("Create a session first."));
		}
		if (!PlayerId.IsEmpty())
		{
			FString PermissionError;
			if (!Session->CanPerform(PlayerId, EEpicodeSessionAction::ManageSession, PermissionError))
			{
				return FailureFromSession(PermissionError);
			}
		}
		FString Error;
		return MutateSession(*Subsystem, Subsystem->EndSession(Error), Error);
	});

	Registry.Register(TEXT("session.kick"), TEXT("Kick a player from the session as the owner."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString RequesterId;
		FString TargetId;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("requesterId"), RequesterId, Error)
			|| !EpicodeJson::GetRequiredString(Params, TEXT("targetPlayerId"), TargetId, Error))
		{
			return FailureFromParams(Error);
		}
		UEpicodeNetworkSubsystem* Subsystem = nullptr;
		FEpicodeCommandResult SubsystemResult = GetSubsystem(Subsystem);
		if (!SubsystemResult.bSuccess)
		{
			return SubsystemResult;
		}
		return MutateSession(*Subsystem, Subsystem->KickPlayer(RequesterId, TargetId, Error), Error);
	});

	Registry.Register(TEXT("session.setRole"), TEXT("Change a player's role as the owner."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString RequesterId;
		FString TargetId;
		FString RoleText;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("requesterId"), RequesterId, Error)
			|| !EpicodeJson::GetRequiredString(Params, TEXT("targetPlayerId"), TargetId, Error)
			|| !EpicodeJson::GetRequiredString(Params, TEXT("role"), RoleText, Error))
		{
			return FailureFromParams(Error);
		}
		EEpicodeSessionRole Role = EEpicodeSessionRole::Performer;
		if (!ParseSessionRole(RoleText, Role))
		{
			return FEpicodeCommandResult::Failure(TEXT("INVALID_PARAMS"), FString::Printf(TEXT("Unknown session role: %s"), *RoleText));
		}
		UEpicodeNetworkSubsystem* Subsystem = nullptr;
		FEpicodeCommandResult SubsystemResult = GetSubsystem(Subsystem);
		if (!SubsystemResult.bSuccess)
		{
			return SubsystemResult;
		}
		return MutateSession(*Subsystem, Subsystem->SetPlayerRole(RequesterId, TargetId, Role, Error), Error);
	});

	Registry.Register(TEXT("session.assignCharacter"), TEXT("Assign a character entity to a player."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString RequesterId;
		FString TargetId;
		FString CharacterId;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("requesterId"), RequesterId, Error)
			|| !EpicodeJson::GetRequiredString(Params, TEXT("characterId"), CharacterId, Error))
		{
			return FailureFromParams(Error);
		}
		EpicodeJson::GetOptionalString(Params, TEXT("targetPlayerId"), TargetId, RequesterId);
		UEpicodeNetworkSubsystem* Subsystem = nullptr;
		FEpicodeCommandResult SubsystemResult = GetSubsystem(Subsystem);
		if (!SubsystemResult.bSuccess)
		{
			return SubsystemResult;
		}
		if (!Subsystem->AssignCharacter(RequesterId, TargetId, CharacterId, Error))
		{
			return FailureFromSession(Error);
		}
		const FEpicodeSessionPlayer* TargetPlayer = Subsystem->GetSession()->FindPlayer(TargetId);
		if (TargetPlayer && TargetPlayer->ParticipantKind == EEpicodeParticipantKind::Agent)
		{
			FString LeaseId;
			double ExpiresAt = 0.0;
			if (!Subsystem->AcquireAgentLease(TargetId, CharacterId, LeaseId, ExpiresAt, Error))
			{
				return FailureFromSession(Error);
			}
		}
		return MutateSession(*Subsystem, true, Error);
	});

	Registry.Register(TEXT("session.releaseCharacter"), TEXT("Release a player's character ownership."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString RequesterId;
		FString TargetId;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("requesterId"), RequesterId, Error))
		{
			return FailureFromParams(Error);
		}
		EpicodeJson::GetOptionalString(Params, TEXT("targetPlayerId"), TargetId, RequesterId);
		UEpicodeNetworkSubsystem* Subsystem = nullptr;
		FEpicodeCommandResult SubsystemResult = GetSubsystem(Subsystem);
		if (!SubsystemResult.bSuccess)
		{
			return SubsystemResult;
		}
		const FEpicodeSessionPlayer* TargetPlayer = Subsystem->GetSession()->FindPlayer(TargetId);
		const bool bAgent = TargetPlayer && TargetPlayer->ParticipantKind == EEpicodeParticipantKind::Agent;
		if (!Subsystem->ReleaseCharacter(RequesterId, TargetId, Error))
		{
			return FailureFromSession(Error);
		}
		if (bAgent && !Subsystem->ReleaseAgentLease(TargetId, Error))
		{
			return FailureFromSession(Error);
		}
		return MutateSession(*Subsystem, true, Error);
	});

	Registry.Register(TEXT("session.transferCharacter"), TEXT("Transfer character ownership between players."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString RequesterId;
		FString FromPlayerId;
		FString ToPlayerId;
		FString CharacterId;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("requesterId"), RequesterId, Error)
			|| !EpicodeJson::GetRequiredString(Params, TEXT("fromPlayerId"), FromPlayerId, Error)
			|| !EpicodeJson::GetRequiredString(Params, TEXT("toPlayerId"), ToPlayerId, Error)
			|| !EpicodeJson::GetRequiredString(Params, TEXT("characterId"), CharacterId, Error))
		{
			return FailureFromParams(Error);
		}
		UEpicodeNetworkSubsystem* Subsystem = nullptr;
		FEpicodeCommandResult SubsystemResult = GetSubsystem(Subsystem);
		if (!SubsystemResult.bSuccess)
		{
			return SubsystemResult;
		}
		return MutateSession(*Subsystem, Subsystem->TransferCharacter(RequesterId, FromPlayerId, ToPlayerId, CharacterId, Error), Error);
	});

	Registry.Register(TEXT("network.status"), TEXT("Describe the network module baseline."), [](const TSharedPtr<FJsonObject>&, const FEpicodeCommandContext&)
	{
		UEpicodeNetworkSubsystem* Subsystem = nullptr;
		FEpicodeCommandResult SubsystemResult = GetSubsystem(Subsystem);
		if (!SubsystemResult.bSuccess)
		{
			return SubsystemResult;
		}
		return FEpicodeCommandResult::Success(Subsystem->GetStatusJson());
	}, false);

	Registry.Register(TEXT("network.players"), TEXT("Describe connected runtime players, pawns and scene spawn mapping."), [](const TSharedPtr<FJsonObject>&, const FEpicodeCommandContext&)
	{
		UEpicodeNetworkSubsystem* Subsystem = nullptr;
		FEpicodeCommandResult SubsystemResult = GetSubsystem(Subsystem);
		if (!SubsystemResult.bSuccess)
		{
			return SubsystemResult;
		}

		UWorld* GameWorld = nullptr;
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::Game && Context.World())
				{
					GameWorld = Context.World();
					break;
				}
			}
		}
		if (!GameWorld)
		{
			return FEpicodeCommandResult::Failure(TEXT("WORLD_NOT_FOUND"), TEXT("No runtime game world is available."));
		}

		TArray<TSharedPtr<FJsonValue>> PlayerValues;
		for (FConstPlayerControllerIterator It = GameWorld->GetPlayerControllerIterator(); It; ++It)
		{
			const AEpicodeNetworkPlayerController* Controller = Cast<AEpicodeNetworkPlayerController>(It->Get());
			if (!Controller)
			{
				continue;
			}
			TSharedPtr<FJsonObject> Player = MakeShared<FJsonObject>();
			Player->SetStringField(TEXT("playerId"), Controller->GetSessionPlayerId());
			Player->SetStringField(TEXT("characterId"), Controller->GetSessionCharacterId());
			Player->SetStringField(TEXT("role"), Controller->GetSessionRole());

			const AEpicodeNetworkCharacter* Character = Cast<AEpicodeNetworkCharacter>(Controller->GetPawn());
			Player->SetBoolField(TEXT("hasPawn"), Character != nullptr);
			if (Character)
			{
				Player->SetStringField(TEXT("pawnClass"), Character->GetClass()->GetName());
				Player->SetStringField(TEXT("appearance"), Character->AppearanceMeshPath);
				Player->SetStringField(TEXT("location"), Character->GetActorLocation().ToString());
				Player->SetNumberField(TEXT("x"), Character->GetActorLocation().X);
				Player->SetNumberField(TEXT("y"), Character->GetActorLocation().Y);
				Player->SetNumberField(TEXT("z"), Character->GetActorLocation().Z);
			}
			for (const FEpicodeNetworkSceneCharacter& SceneCharacter : Subsystem->GetSceneSummary().Characters)
			{
				if (SceneCharacter.EntityId == Controller->GetSessionCharacterId())
				{
					Player->SetStringField(TEXT("sceneLocation"), SceneCharacter.Transform.Location.ToString());
					Player->SetNumberField(TEXT("sceneX"), SceneCharacter.Transform.Location.X);
					Player->SetNumberField(TEXT("sceneY"), SceneCharacter.Transform.Location.Y);
					Player->SetNumberField(TEXT("sceneZ"), SceneCharacter.Transform.Location.Z);
					Player->SetStringField(TEXT("sceneMesh"), SceneCharacter.MeshPath);
					break;
				}
			}
			PlayerValues.Add(MakeShared<FJsonValueObject>(Player));
		}

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("players"), PlayerValues);
		Result->SetNumberField(TEXT("count"), PlayerValues.Num());
		return FEpicodeCommandResult::Success(Result);
	}, false);

	Registry.Register(TEXT("session.heartbeat"), TEXT("Renew an agent character lease."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeNetworkSubsystem* Subsystem = nullptr;
		FEpicodeCommandResult SubsystemResult = GetSubsystem(Subsystem);
		if (!SubsystemResult.bSuccess)
		{
			return SubsystemResult;
		}
		FString PlayerId;
		FEpicodeCommandResult Failure;
		if (!ResolveParticipant(Params, Context, *Subsystem, PlayerId, Failure))
		{
			return Failure;
		}
		FString LeaseId;
		double ExpiresAt = 0.0;
		FString Error;
		if (!Subsystem->HeartbeatAgentLease(PlayerId, LeaseId, ExpiresAt, Error))
		{
			return FailureFromSession(Error);
		}
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("playerId"), PlayerId);
		Result->SetStringField(TEXT("leaseId"), LeaseId);
		Result->SetNumberField(TEXT("leaseExpiresAt"), ExpiresAt);
		Result->SetNumberField(TEXT("stateVersion"), static_cast<double>(Subsystem->GetSession()->GetWorldVersion()));
		return FEpicodeCommandResult::Success(Result);
	}, true, false, TEXT("agent"), true);

	Registry.Register(TEXT("character.moveTo"), TEXT("Move an agent-controlled character to a target."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeNetworkSubsystem* Subsystem = nullptr;
		FEpicodeCommandResult SubsystemResult = GetSubsystem(Subsystem);
		if (!SubsystemResult.bSuccess)
		{
			return SubsystemResult;
		}
		FString PlayerId;
		FEpicodeCommandResult Failure;
		if (!ResolveParticipant(Params, Context, *Subsystem, PlayerId, Failure))
		{
			return Failure;
		}
		FVector Target;
		FString Error;
		if (!GetRequiredTarget(Params, TEXT("target"), Target, Error))
		{
			return FailureFromParams(Error);
		}
		if (!Subsystem->MoveAgent(PlayerId, Target, Error))
		{
			return FailureFromSession(Error);
		}
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("playerId"), PlayerId);
		Result->SetStringField(TEXT("characterId"), Subsystem->GetSession()->FindPlayer(PlayerId)->CharacterId);
		Result->SetStringField(TEXT("target"), Target.ToString());
		Result->SetBoolField(TEXT("accepted"), true);
		Result->SetNumberField(TEXT("stateVersion"), static_cast<double>(Subsystem->GetSession()->GetWorldVersion()));
		return FEpicodeCommandResult::Success(Result);
	}, true, false, TEXT("agent"), true);

	Registry.Register(TEXT("character.lookAt"), TEXT("Aim an agent-controlled character at a target."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeNetworkSubsystem* Subsystem = nullptr;
		FEpicodeCommandResult SubsystemResult = GetSubsystem(Subsystem);
		if (!SubsystemResult.bSuccess)
		{
			return SubsystemResult;
		}
		FString PlayerId;
		FEpicodeCommandResult Failure;
		if (!ResolveParticipant(Params, Context, *Subsystem, PlayerId, Failure))
		{
			return Failure;
		}
		FVector Target;
		FString Error;
		if (!GetRequiredTarget(Params, TEXT("target"), Target, Error))
		{
			return FailureFromParams(Error);
		}
		if (!Subsystem->LookAgent(PlayerId, Target, Error))
		{
			return FailureFromSession(Error);
		}
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("playerId"), PlayerId);
		Result->SetStringField(TEXT("target"), Target.ToString());
		Result->SetNumberField(TEXT("stateVersion"), static_cast<double>(Subsystem->GetSession()->GetWorldVersion()));
		return FEpicodeCommandResult::Success(Result);
	}, true, false, TEXT("agent"), true);

	Registry.Register(TEXT("character.stop"), TEXT("Stop an agent-controlled character."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeNetworkSubsystem* Subsystem = nullptr;
		FEpicodeCommandResult SubsystemResult = GetSubsystem(Subsystem);
		if (!SubsystemResult.bSuccess)
		{
			return SubsystemResult;
		}
		FString PlayerId;
		FEpicodeCommandResult Failure;
		if (!ResolveParticipant(Params, Context, *Subsystem, PlayerId, Failure))
		{
			return Failure;
		}
		FString Error;
		if (!Subsystem->StopAgent(PlayerId, Error))
		{
			return FailureFromSession(Error);
		}
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("playerId"), PlayerId);
		Result->SetNumberField(TEXT("stateVersion"), static_cast<double>(Subsystem->GetSession()->GetWorldVersion()));
		return FEpicodeCommandResult::Success(Result);
	}, true, false, TEXT("agent"), true);

	Registry.Register(TEXT("character.state"), TEXT("Observe an agent-controlled character."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeNetworkSubsystem* Subsystem = nullptr;
		FEpicodeCommandResult SubsystemResult = GetSubsystem(Subsystem);
		if (!SubsystemResult.bSuccess)
		{
			return SubsystemResult;
		}
		FString PlayerId;
		FEpicodeCommandResult Failure;
		if (!ResolveParticipant(Params, Context, *Subsystem, PlayerId, Failure))
		{
			return Failure;
		}
		FString Error;
		TSharedPtr<FJsonObject> Result = Subsystem->GetAgentState(PlayerId, Error);
		if (!Result.IsValid())
		{
			return FailureFromSession(Error);
		}
		return FEpicodeCommandResult::Success(Result);
	}, false, false, TEXT("agent"), false);

	Registry.Register(TEXT("performance.action"), TEXT("Request a replicated performance action."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString PlayerId;
		FString Action;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("playerId"), PlayerId, Error) ||
			!EpicodeJson::GetRequiredString(Params, TEXT("action"), Action, Error))
		{
			return FailureFromParams(Error);
		}
		bool bInterrupt = false;
		EpicodeJson::GetOptionalBool(Params, TEXT("interrupt"), bInterrupt, false);
		double Duration = 1.0;
		EpicodeJson::GetOptionalNumber(Params, TEXT("duration"), Duration, 1.0);
		FEpicodeCommandResult Failure;
		AEpicodeNetworkCharacter* Character = FindPlayerCharacter(PlayerId, Failure);
		if (!Character)
		{
			return Failure;
		}
		if (!Character->RequestAction(Action, bInterrupt, Duration, Error))
		{
			return FEpicodeCommandResult::Failure(TEXT("ACTION_REJECTED"), Error);
		}
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("playerId"), PlayerId);
		Result->SetObjectField(TEXT("performance"), Character->PerformanceState.ToJson());
		return FEpicodeCommandResult::Success(Result);
	});

	Registry.Register(TEXT("performance.expression"), TEXT("Set a replicated emotion and expression intensity."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString PlayerId;
		FString Emotion;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("playerId"), PlayerId, Error) ||
			!EpicodeJson::GetRequiredString(Params, TEXT("emotion"), Emotion, Error))
		{
			return FailureFromParams(Error);
		}
		double Intensity = 0.0;
		EpicodeJson::GetOptionalNumber(Params, TEXT("intensity"), Intensity, 0.0);
		FEpicodeCommandResult Failure;
		AEpicodeNetworkCharacter* Character = FindPlayerCharacter(PlayerId, Failure);
		if (!Character)
		{
			return Failure;
		}
		if (!Character->SetExpression(Emotion, Intensity, Error))
		{
			return FEpicodeCommandResult::Failure(TEXT("EXPRESSION_REJECTED"), Error);
		}
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("playerId"), PlayerId);
		Result->SetObjectField(TEXT("performance"), Character->PerformanceState.ToJson());
		return FEpicodeCommandResult::Success(Result);
	});

	Registry.Register(TEXT("performance.voice"), TEXT("Set speaking state and viseme source with fallback."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString PlayerId;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("playerId"), PlayerId, Error))
		{
			return FailureFromParams(Error);
		}
		bool bSpeaking = true;
		EpicodeJson::GetOptionalBool(Params, TEXT("speaking"), bSpeaking, true);
		FString SourceText = TEXT("preset");
		EpicodeJson::GetOptionalString(Params, TEXT("source"), SourceText);
		EEpicodeVisemeSource Source = EEpicodeVisemeSource::Preset;
		if (!ParseVisemeSource(SourceText, Source))
		{
			return FEpicodeCommandResult::Failure(TEXT("INVALID_PARAMS"), FString::Printf(TEXT("Unknown viseme source '%s'."), *SourceText));
		}
		FEpicodeCommandResult Failure;
		AEpicodeNetworkCharacter* Character = FindPlayerCharacter(PlayerId, Failure);
		if (!Character)
		{
			return Failure;
		}
		FString Warning;
		if (!Character->SetVoice(bSpeaking, Source, Warning, Error))
		{
			return FEpicodeCommandResult::Failure(TEXT("VOICE_REJECTED"), Error);
		}
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("playerId"), PlayerId);
		Result->SetObjectField(TEXT("performance"), Character->PerformanceState.ToJson());
		if (!Warning.IsEmpty())
		{
			Result->SetStringField(TEXT("warning"), Warning);
		}
		return FEpicodeCommandResult::Success(Result);
	});

	Registry.Register(TEXT("performance.motionMatching"), TEXT("Enable Motion Matching when motion data is available."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString PlayerId;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("playerId"), PlayerId, Error))
		{
			return FailureFromParams(Error);
		}
		bool bEnabled = true;
		bool bDataAvailable = false;
		EpicodeJson::GetOptionalBool(Params, TEXT("enabled"), bEnabled, true);
		EpicodeJson::GetOptionalBool(Params, TEXT("motionDataAvailable"), bDataAvailable, false);
		FEpicodeCommandResult Failure;
		AEpicodeNetworkCharacter* Character = FindPlayerCharacter(PlayerId, Failure);
		if (!Character)
		{
			return Failure;
		}
		FString MotionError;
		Character->SetMotionMatching(bEnabled, bDataAvailable, MotionError);
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("playerId"), PlayerId);
		Result->SetObjectField(TEXT("performance"), Character->PerformanceState.ToJson());
		if (!MotionError.IsEmpty())
		{
			Result->SetStringField(TEXT("warning"), MotionError);
		}
		return FEpicodeCommandResult::Success(Result);
	});

	Registry.Register(TEXT("item.spawn"), TEXT("Spawn a server-authoritative runtime item."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString RequesterId;
		FString Type;
		FString Name;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("requesterId"), RequesterId, Error) ||
			!EpicodeJson::GetRequiredString(Params, TEXT("type"), Type, Error) ||
			!EpicodeJson::GetRequiredString(Params, TEXT("name"), Name, Error))
		{
			return FailureFromParams(Error);
		}
		UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
		FEpicodeNetworkSession* Session = Subsystem ? Subsystem->GetSession() : nullptr;
		if (!Session || !Session->CanPerform(RequesterId, EEpicodeSessionAction::ControlScene, Error))
		{
			return FailureFromSession(Error);
		}
		double X = 0.0, Y = 0.0, Z = 0.0, Durability = 1.0;
		double Quantity = 1.0;
		EpicodeJson::GetOptionalNumber(Params, TEXT("x"), X, 0.0);
		EpicodeJson::GetOptionalNumber(Params, TEXT("y"), Y, 0.0);
		EpicodeJson::GetOptionalNumber(Params, TEXT("z"), Z, 0.0);
		EpicodeJson::GetOptionalNumber(Params, TEXT("durability"), Durability, 1.0);
		EpicodeJson::GetOptionalNumber(Params, TEXT("quantity"), Quantity, 1.0);
		FString ItemId;
		if (!Subsystem->GetWorldState().SpawnItem(Type, Name, FVector(X, Y, Z), Durability, static_cast<int32>(Quantity), ItemId, Error))
		{
			return FailureFromSession(Error);
		}
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("itemId"), ItemId);
		Payload->SetStringField(TEXT("itemType"), Type);
		Subsystem->PublishWorldEvent(RequesterId, ItemId, TEXT("item.spawned"), Payload);
		return FEpicodeCommandResult::Success(Subsystem->GetWorldState().ToJson());
	});

	Registry.Register(TEXT("item.pickup"), TEXT("Pick up an item within interaction range."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString PlayerId;
		FString ItemId;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("playerId"), PlayerId, Error) ||
			!EpicodeJson::GetRequiredString(Params, TEXT("itemId"), ItemId, Error))
		{
			return FailureFromParams(Error);
		}
		double Distance = 0.0;
		double MaxDistance = 200.0;
		EpicodeJson::GetOptionalNumber(Params, TEXT("distance"), Distance, 0.0);
		EpicodeJson::GetOptionalNumber(Params, TEXT("maxDistance"), MaxDistance, 200.0);
		UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
		FEpicodeNetworkSession* Session = Subsystem ? Subsystem->GetSession() : nullptr;
		if (!Session || !Session->CanPerform(PlayerId, EEpicodeSessionAction::WorldInteract, Error))
		{
			return FailureFromSession(Error);
		}
		if (!Subsystem->GetWorldState().PickupItem(PlayerId, ItemId, Distance, MaxDistance, Error))
		{
			return FailureFromSession(Error);
		}
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("itemId"), ItemId);
		Subsystem->PublishWorldEvent(PlayerId, ItemId, TEXT("item.pickedUp"), Payload);
		return FEpicodeCommandResult::Success(Subsystem->GetWorldState().ToJson());
	});

	Registry.Register(TEXT("item.hold"), TEXT("Move a held item to a slot."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString PlayerId;
		FString ItemId;
		FString Slot;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("playerId"), PlayerId, Error) ||
			!EpicodeJson::GetRequiredString(Params, TEXT("itemId"), ItemId, Error))
		{
			return FailureFromParams(Error);
		}
		EpicodeJson::GetOptionalString(Params, TEXT("slot"), Slot);
		UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
		if (!Subsystem || !Subsystem->GetWorldState().HoldItem(PlayerId, ItemId, Slot, Error))
		{
			return FailureFromSession(Error);
		}
		return FEpicodeCommandResult::Success(Subsystem->GetWorldState().ToJson());
	});

	Registry.Register(TEXT("item.release"), TEXT("Release a held item without dropping it."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString PlayerId;
		FString ItemId;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("playerId"), PlayerId, Error) ||
			!EpicodeJson::GetRequiredString(Params, TEXT("itemId"), ItemId, Error))
		{
			return FailureFromParams(Error);
		}
		UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
		if (!Subsystem || !Subsystem->GetWorldState().ReleaseItem(PlayerId, ItemId, Error))
		{
			return FailureFromSession(Error);
		}
		return FEpicodeCommandResult::Success(Subsystem->GetWorldState().ToJson());
	});

	Registry.Register(TEXT("item.drop"), TEXT("Drop a held item onto the world."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString PlayerId;
		FString ItemId;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("playerId"), PlayerId, Error) ||
			!EpicodeJson::GetRequiredString(Params, TEXT("itemId"), ItemId, Error))
		{
			return FailureFromParams(Error);
		}
		UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
		if (!Subsystem || !Subsystem->GetWorldState().DropItem(PlayerId, ItemId, Error))
		{
			return FailureFromSession(Error);
		}
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("itemId"), ItemId);
		Subsystem->PublishWorldEvent(PlayerId, ItemId, TEXT("item.dropped"), Payload);
		return FEpicodeCommandResult::Success(Subsystem->GetWorldState().ToJson());
	});

	Registry.Register(TEXT("inventory.get"), TEXT("Read the private inventory of a player."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString RequesterId;
		FString PlayerId;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("requesterId"), RequesterId, Error) ||
			!EpicodeJson::GetRequiredString(Params, TEXT("playerId"), PlayerId, Error))
		{
			return FailureFromParams(Error);
		}
		UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
		FEpicodeInventoryRecord Inventory;
		if (!Subsystem || !Subsystem->GetWorldState().GetInventory(RequesterId, PlayerId, Inventory, Error))
		{
			return FailureFromSession(Error);
		}
		return FEpicodeCommandResult::Success(Inventory.ToJson());
	});

	Registry.Register(TEXT("craft.start"), TEXT("Start a craft job if materials are present."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString PlayerId;
		FString RecipeId;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("playerId"), PlayerId, Error) ||
			!EpicodeJson::GetRequiredString(Params, TEXT("recipeId"), RecipeId, Error))
		{
			return FailureFromParams(Error);
		}
		UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
		FEpicodeNetworkSession* Session = Subsystem ? Subsystem->GetSession() : nullptr;
		if (!Session || !Session->CanPerform(PlayerId, EEpicodeSessionAction::WorldInteract, Error))
		{
			return FailureFromSession(Error);
		}
		FString JobId;
		if (!Subsystem->GetWorldState().StartCraft(PlayerId, RecipeId, FPlatformTime::Seconds(), JobId, Error))
		{
			return FailureFromSession(Error);
		}
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("jobId"), JobId);
		Payload->SetStringField(TEXT("recipeId"), RecipeId);
		Subsystem->PublishWorldEvent(PlayerId, JobId, TEXT("craft.started"), Payload);
		return FEpicodeCommandResult::Success(Subsystem->GetWorldState().ToJson(FPlatformTime::Seconds()));
	});

	Registry.Register(TEXT("craft.status"), TEXT("Read craft progress."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString PlayerId;
		FString JobId;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("playerId"), PlayerId, Error) ||
			!EpicodeJson::GetRequiredString(Params, TEXT("jobId"), JobId, Error))
		{
			return FailureFromParams(Error);
		}
		UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
		TSharedPtr<FJsonObject> Status;
		if (!Subsystem || !Subsystem->GetWorldState().GetCraftStatus(PlayerId, JobId, FPlatformTime::Seconds(), Status, Error))
		{
			return FailureFromSession(Error);
		}
		return FEpicodeCommandResult::Success(Status);
	});

	Registry.Register(TEXT("craft.cancel"), TEXT("Cancel an active craft job."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString PlayerId;
		FString JobId;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("playerId"), PlayerId, Error) ||
			!EpicodeJson::GetRequiredString(Params, TEXT("jobId"), JobId, Error))
		{
			return FailureFromParams(Error);
		}
		UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
		if (!Subsystem || !Subsystem->GetWorldState().CancelCraft(PlayerId, JobId, Error))
		{
			return FailureFromSession(Error);
		}
		Subsystem->PublishWorldEvent(PlayerId, JobId, TEXT("craft.cancelled"));
		return FEpicodeCommandResult::Success(Subsystem->GetWorldState().ToJson(FPlatformTime::Seconds()));
	});

	Registry.Register(TEXT("build.confirm"), TEXT("Confirm a build after zone, distance and material checks."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString PlayerId;
		FString EntityType;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("playerId"), PlayerId, Error) ||
			!EpicodeJson::GetRequiredString(Params, TEXT("entityType"), EntityType, Error))
		{
			return FailureFromParams(Error);
		}
		double X = 0.0, Y = 0.0, Z = 0.0, BuilderX = 0.0, BuilderY = 0.0, BuilderZ = 0.0, MaxDistance = 500.0, Cost = 1.0;
		EpicodeJson::GetOptionalNumber(Params, TEXT("x"), X, 0.0);
		EpicodeJson::GetOptionalNumber(Params, TEXT("y"), Y, 0.0);
		EpicodeJson::GetOptionalNumber(Params, TEXT("z"), Z, 0.0);
		EpicodeJson::GetOptionalNumber(Params, TEXT("builderX"), BuilderX, 0.0);
		EpicodeJson::GetOptionalNumber(Params, TEXT("builderY"), BuilderY, 0.0);
		EpicodeJson::GetOptionalNumber(Params, TEXT("builderZ"), BuilderZ, 0.0);
		EpicodeJson::GetOptionalNumber(Params, TEXT("maxDistance"), MaxDistance, 500.0);
		EpicodeJson::GetOptionalNumber(Params, TEXT("materialCost"), Cost, 1.0);
		UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
		FEpicodeNetworkSession* Session = Subsystem ? Subsystem->GetSession() : nullptr;
		if (!Session || !Session->CanPerform(PlayerId, EEpicodeSessionAction::WorldInteract, Error))
		{
			return FailureFromSession(Error);
		}
		FString BuildId;
		if (!Subsystem->GetWorldState().ConfirmBuild(PlayerId, EntityType, FVector(BuilderX, BuilderY, BuilderZ),
			FVector(X, Y, Z), FRotator::ZeroRotator, static_cast<int32>(Cost), MaxDistance, BuildId, Error))
		{
			return FailureFromSession(Error);
		}
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("buildId"), BuildId);
		Payload->SetStringField(TEXT("entityType"), EntityType);
		Subsystem->PublishWorldEvent(PlayerId, BuildId, TEXT("build.confirmed"), Payload);
		return FEpicodeCommandResult::Success(Subsystem->GetWorldState().ToJson());
	});

	Registry.Register(TEXT("build.undo"), TEXT("Undo a build and refund its materials."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString PlayerId;
		FString BuildId;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("playerId"), PlayerId, Error) ||
			!EpicodeJson::GetRequiredString(Params, TEXT("buildId"), BuildId, Error))
		{
			return FailureFromParams(Error);
		}
		UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
		if (!Subsystem || !Subsystem->GetWorldState().UndoBuild(PlayerId, BuildId, Error))
		{
			return FailureFromSession(Error);
		}
		Subsystem->PublishWorldEvent(PlayerId, BuildId, TEXT("build.undone"));
		return FEpicodeCommandResult::Success(Subsystem->GetWorldState().ToJson());
	});

	Registry.Register(TEXT("world.status"), TEXT("Describe runtime items, inventories, crafts and builds."), [](const TSharedPtr<FJsonObject>&, const FEpicodeCommandContext&)
	{
		UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
		if (!Subsystem)
		{
			return FEpicodeCommandResult::Failure(TEXT("NETWORK_UNAVAILABLE"), TEXT("EpicodeNetwork subsystem is not available."));
		}
		return FEpicodeCommandResult::Success(Subsystem->GetWorldState().ToJson(FPlatformTime::Seconds()));
	}, false);
}
