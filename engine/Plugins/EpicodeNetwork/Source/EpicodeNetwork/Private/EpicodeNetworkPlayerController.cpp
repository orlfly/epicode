#include "EpicodeNetworkPlayerController.h"

#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "EpicodeNetworkCharacter.h"
#include "EpicodeNetworkModule.h"
#include "EpicodeNetworkSession.h"
#include "EpicodeNetworkSettings.h"
#include "EpicodeNetworkSubsystem.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerState.h"
#include "Misc/CommandLine.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

AEpicodeNetworkPlayerController::AEpicodeNetworkPlayerController()
{
	bReplicates = true;
}

void AEpicodeNetworkPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		StartSmokeMovement();
		if (FParse::Param(FCommandLine::Get(), TEXT("EpicodeSmokePerformance")))
		{
			GetWorldTimerManager().SetTimer(SmokePerformanceTimer, this, &AEpicodeNetworkPlayerController::RunSmokePerformance, 3.0f, false);
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("EpicodeSmokeWorld")))
		{
			GetWorldTimerManager().SetTimer(SmokeWorldTimer, this, &AEpicodeNetworkPlayerController::RunSmokeWorld, 4.0f, false);
		}
	}
	if (!IsLocalController())
	{
		return;
	}

	FString DisplayName;
	if (!FParse::Value(FCommandLine::Get(), TEXT("EpicodePlayerName="), DisplayName) || DisplayName.IsEmpty())
	{
		DisplayName = PlayerState ? PlayerState->GetPlayerName() : GetName();
	}
	if (DisplayName.IsEmpty())
	{
		DisplayName = TEXT("Player");
	}
	ServerRegisterPlayer(DisplayName);
}

void AEpicodeNetworkPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsLocalController() || !IsSessionRegistered() || !GetWorld())
	{
		return;
	}
	DriftReportAccumulator += DeltaSeconds;
	if (DriftReportAccumulator < 1.0f)
	{
		return;
	}
	DriftReportAccumulator = 0.0f;
	for (TActorIterator<AEpicodeNetworkCharacter> It(GetWorld()); It; ++It)
	{
		AEpicodeNetworkCharacter* Character = *It;
		if (Character == GetPawn() || !Character || Character->CharacterId.IsEmpty())
		{
			continue;
		}
		ServerReportRemoteDrift(Character->CharacterId, Character->GetActorLocation());
	}
}

void AEpicodeNetworkPlayerController::ServerReportRemoteDrift_Implementation(const FString& InCharacterId, const FVector& InLocation)
{
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}
	for (TActorIterator<AEpicodeNetworkCharacter> It(GetWorld()); It; ++It)
	{
		AEpicodeNetworkCharacter* Character = *It;
		if (!Character || Character->CharacterId != InCharacterId)
		{
			continue;
		}
		const double Distance = FVector::Dist(Character->GetActorLocation(), InLocation);
		UE_LOG(LogEpicodeNetwork, Log, TEXT("Remote drift player='%s' character='%s' distance=%.2f"),
			*SessionPlayerId, *InCharacterId, Distance);
		break;
	}
}

void AEpicodeNetworkPlayerController::StartSmokeMovement()
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("EpicodeSmokeMove")) || SmokeMoveSteps > 0)
	{
		return;
	}
	SmokeMoveSteps = 1;
	GetWorldTimerManager().SetTimer(SmokeMoveTimer, this, &AEpicodeNetworkPlayerController::StepSmokeMovement, 1.0f, true, 2.0f);
}

void AEpicodeNetworkPlayerController::StepSmokeMovement()
{
	if (SmokeMoveSteps > 6)
	{
		GetWorldTimerManager().ClearTimer(SmokeMoveTimer);
		return;
	}
	if (APawn* ControlledPawn = GetPawn())
	{
		const double Direction = (SmokeMoveSteps % 2 == 0) ? -1.0 : 1.0;
		ControlledPawn->SetActorLocation(ControlledPawn->GetActorLocation() + FVector(0.0, 120.0 * Direction, 0.0), false, nullptr, ETeleportType::TeleportPhysics);
		UE_LOG(LogEpicodeNetwork, Log, TEXT("Smoke movement player='%s' step=%d location=%s"),
			*SessionPlayerId, SmokeMoveSteps, *ControlledPawn->GetActorLocation().ToString());
	}
	++SmokeMoveSteps;
}

void AEpicodeNetworkPlayerController::RunSmokePerformance()
{
	AEpicodeNetworkCharacter* Character = Cast<AEpicodeNetworkCharacter>(GetPawn());
	if (!Character)
	{
		return;
	}
	FString Error;
	Character->RequestAction(TEXT("wave"), false, 2.0, Error);
	Character->SetExpression(TEXT("happy"), 0.8, Error);
	FString Warning;
	Character->SetVoice(true, EEpicodeVisemeSource::LiveLink, Warning, Error);
	Character->ApplyLocalMicroExpression(0.25);
	Character->ApplyLocalIKCorrections(0.5, 0.5, 0.25);
	UE_LOG(LogEpicodeNetwork, Log,
		TEXT("Performance smoke player='%s' action='%s' emotion='%s' intensity=%.2f viseme='%s' warning='%s'"),
		*SessionPlayerId,
		*Character->PerformanceState.ActionTag,
		*Character->PerformanceState.Emotion,
		Character->PerformanceState.ExpressionIntensity,
		*LexToString(Character->PerformanceState.VisemeSource),
		*Warning);
}

void AEpicodeNetworkPlayerController::RunSmokeWorld()
{
	UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
	if (!Subsystem || SessionPlayerId.IsEmpty())
	{
		return;
	}
	FEpicodeWorldState& World = Subsystem->GetWorldState();
	World.GrantMaterials(SessionPlayerId, 5);
	FString ItemId;
	FString Error;
	World.SpawnItem(TEXT("scrap"), TEXT("Smoke Scrap"), FVector(0.0, 0.0, 0.0), 1.0, 1, ItemId, Error);
	TSharedPtr<FJsonObject> SpawnPayload = MakeShared<FJsonObject>();
	SpawnPayload->SetStringField(TEXT("itemId"), ItemId);
	Subsystem->PublishWorldEvent(SessionPlayerId, ItemId, TEXT("item.spawned"), SpawnPayload);

	World.PickupItem(SessionPlayerId, ItemId, 0.0, 200.0, Error);
	Subsystem->PublishWorldEvent(SessionPlayerId, ItemId, TEXT("item.pickedUp"), SpawnPayload);

	FString JobId;
	World.StartCraft(SessionPlayerId, TEXT("craft.tool"), FPlatformTime::Seconds(), JobId, Error);
	TSharedPtr<FJsonObject> CraftPayload = MakeShared<FJsonObject>();
	CraftPayload->SetStringField(TEXT("jobId"), JobId);
	Subsystem->PublishWorldEvent(SessionPlayerId, JobId, TEXT("craft.started"), CraftPayload);

	FString BuildId;
	const double BuildX = SessionPlayerId.IsEmpty() ? 100.0 : 100.0 + static_cast<double>(SessionPlayerId[0] % 5) * 50.0;
	World.ConfirmBuild(SessionPlayerId, TEXT("wall"), FVector(0.0, 0.0, 0.0), FVector(BuildX, 0.0, 0.0),
		FRotator::ZeroRotator, 1, 500.0, BuildId, Error);
	TSharedPtr<FJsonObject> BuildPayload = MakeShared<FJsonObject>();
	BuildPayload->SetStringField(TEXT("buildId"), BuildId);
	Subsystem->PublishWorldEvent(SessionPlayerId, BuildId, TEXT("build.confirmed"), BuildPayload);

	UE_LOG(LogEpicodeNetwork, Log,
		TEXT("World smoke player='%s' item='%s' craft='%s' build='%s' error='%s'"),
		*SessionPlayerId, *ItemId, *JobId, *BuildId, *Error);
}

void AEpicodeNetworkPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AEpicodeNetworkPlayerController, SessionPlayerId);
	DOREPLIFETIME(AEpicodeNetworkPlayerController, SessionCharacterId);
	DOREPLIFETIME(AEpicodeNetworkPlayerController, SessionRole);
}

FString AEpicodeNetworkPlayerController::MakeSafePlayerId(const FString& InDisplayName)
{
	FString Safe = InDisplayName.TrimStartAndEnd();
	Safe.ReplaceInline(TEXT(" "), TEXT("-"));
	Safe.ReplaceInline(TEXT("/"), TEXT("-"));
	Safe.ReplaceInline(TEXT("\\"), TEXT("-"));
	Safe.ReplaceInline(TEXT(":"), TEXT("-"));
	if (Safe.IsEmpty())
	{
		Safe = TEXT("Player");
	}
	return Safe.Left(64);
}

void AEpicodeNetworkPlayerController::ServerRegisterPlayer_Implementation(const FString& DisplayName)
{
	if (!HasAuthority() || IsSessionRegistered())
	{
		return;
	}

	UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
	FEpicodeNetworkSession* Session = Subsystem ? Subsystem->GetSession() : nullptr;
	if (!Session)
	{
		ClientSessionFailed(TEXT("SESSION_NOT_FOUND: the server has no active production session."));
		return;
	}

	FString Candidate = MakeSafePlayerId(DisplayName);
	FString PlayerId = Candidate;
	FString ExistingCharacterId;
	bool bRestoreTransform = false;
	FTransform RestoredTransform = FTransform::Identity;
	int32 Suffix = 2;
	while (const FEpicodeSessionPlayer* Existing = Session->FindPlayer(PlayerId))
	{
		if (!Existing->bConnected)
		{
			ExistingCharacterId = Existing->CharacterId;
			if (Existing->bHasRuntimeTransform)
			{
				bRestoreTransform = true;
				RestoredTransform = FTransform(Existing->RuntimeRotation, Existing->RuntimeLocation);
			}
			break;
		}
		PlayerId = FString::Printf(TEXT("%s-%d"), *Candidate, Suffix++);
	}

	FString Error;
	FString JoinedPlayerId;
	if (!Subsystem->JoinSession(PlayerId, DisplayName, EEpicodeSessionRole::Performer, JoinedPlayerId, Error))
	{
		ClientSessionFailed(Error);
		return;
	}

	FString CharacterId = ExistingCharacterId;
	FString MeshPath = UEpicodeNetworkSettings::Get()->DefaultCharacterMesh;
	FTransform SpawnTransform = bRestoreTransform ? RestoredTransform : FTransform::Identity;
	bool bSceneCharacter = false;
	if (!CharacterId.IsEmpty())
	{
		FTransform SceneTransform = FTransform::Identity;
		bSceneCharacter = Subsystem->GetCharacterInfo(CharacterId, MeshPath, SceneTransform);
		if (bSceneCharacter && !bRestoreTransform)
		{
			SpawnTransform = SceneTransform;
		}
	}
	if (CharacterId.IsEmpty())
	{
		bSceneCharacter = Subsystem->GetNextFreeCharacter(CharacterId, MeshPath, SpawnTransform);
		if (!bSceneCharacter || CharacterId.IsEmpty())
		{
			CharacterId = FString::Printf(TEXT("runtime-character-%s"), *JoinedPlayerId);
			MeshPath = UEpicodeNetworkSettings::Get()->DefaultCharacterMesh;
		}
	}
	const FString RequesterId = Session->GetOwnerPlayerId().IsEmpty() ? JoinedPlayerId : Session->GetOwnerPlayerId();
	if (!Subsystem->AssignCharacter(RequesterId, JoinedPlayerId, CharacterId, Error))
	{
		FString LeaveError;
		Subsystem->LeaveSession(JoinedPlayerId, LeaveError);
		ClientSessionFailed(Error);
		return;
	}

	SessionPlayerId = JoinedPlayerId;
	SessionCharacterId = CharacterId;
	if (const FEpicodeSessionPlayer* Player = Session->FindPlayer(JoinedPlayerId))
	{
		SessionRole = LexToString(Player->Role);
	}

	AGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr;
	FTransform FinalSpawnTransform = SpawnTransform;
	const bool bUseSpawnTransform = bSceneCharacter || bRestoreTransform;
	if (bSceneCharacter)
	{
		const AEpicodeNetworkCharacter* CharacterDefaults = GetDefault<AEpicodeNetworkCharacter>();
		const double CapsuleHalfHeight = CharacterDefaults && CharacterDefaults->GetCapsuleComponent()
			? CharacterDefaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
			: 88.0;
		FinalSpawnTransform.SetLocation(AEpicodeNetworkCharacter::MakeCapsuleSpawnLocation(SpawnTransform.GetLocation(), CapsuleHalfHeight));
	}
	if (!GetPawn() && bUseSpawnTransform && GetWorld())
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AEpicodeNetworkCharacter* Character = GetWorld()->SpawnActor<AEpicodeNetworkCharacter>(
			AEpicodeNetworkCharacter::StaticClass(), FinalSpawnTransform.GetLocation(), FinalSpawnTransform.Rotator(), SpawnParams);
		if (Character)
		{
			Possess(Character);
			SetControlRotation(FinalSpawnTransform.Rotator());
			if (GameMode)
			{
				GameMode->SetPlayerDefaults(Character);
			}
		}
	}
	if (!GetPawn() && GameMode)
	{
		GameMode->RestartPlayer(this);
	}
	if (!GetPawn())
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector SpawnLocation = bUseSpawnTransform ? FinalSpawnTransform.GetLocation() : FVector(0.0, 0.0, 120.0);
		const FRotator SpawnRotation = bUseSpawnTransform ? FinalSpawnTransform.Rotator() : FRotator::ZeroRotator;
		AEpicodeNetworkCharacter* Character = GetWorld()->SpawnActor<AEpicodeNetworkCharacter>(
			AEpicodeNetworkCharacter::StaticClass(), SpawnLocation, SpawnRotation, SpawnParams);
		if (Character)
		{
			Possess(Character);
		}
	}
	if (AEpicodeNetworkCharacter* Character = Cast<AEpicodeNetworkCharacter>(GetPawn()))
	{
		if (bUseSpawnTransform)
		{
			Character->SetActorLocationAndRotation(FinalSpawnTransform.GetLocation(), FinalSpawnTransform.Rotator(), false, nullptr, ETeleportType::TeleportPhysics);
		}
		Character->SetNetworkIdentity(JoinedPlayerId, CharacterId);
		Character->SetAppearance(MeshPath);
	}
	if (APawn* Pawn = GetPawn())
	{
		UE_LOG(LogEpicodeNetwork, Log, TEXT("Player '%s' possesses pawn '%s' (%s)"), *JoinedPlayerId, *Pawn->GetName(), *Pawn->GetClass()->GetName());
		UE_LOG(LogEpicodeNetwork, Log, TEXT("Spawn mapping player='%s' character='%s' location=%s scene=%d restored=%d appearance='%s'"),
			*JoinedPlayerId,
			*CharacterId,
			*Pawn->GetActorLocation().ToString(),
			bSceneCharacter ? 1 : 0,
			bRestoreTransform ? 1 : 0,
			*MeshPath);
	}
	else
	{
		UE_LOG(LogEpicodeNetwork, Warning, TEXT("Player '%s' has no possessed pawn after registration."), *JoinedPlayerId);
	}

	UE_LOG(LogEpicodeNetwork, Log, TEXT("Player '%s' joined session as '%s' with character '%s' (scene=%d, mesh=%s)"),
		*DisplayName, *JoinedPlayerId, *CharacterId, bSceneCharacter ? 1 : 0, *MeshPath);
	ClientSessionJoined(JoinedPlayerId, CharacterId, SessionRole);
}

void AEpicodeNetworkPlayerController::ClientSessionJoined_Implementation(const FString& InPlayerId, const FString& InCharacterId, const FString& InRole)
{
	UE_LOG(LogEpicodeNetwork, Log, TEXT("Session identity received: player=%s character=%s role=%s pawn=%s"),
		*InPlayerId, *InCharacterId, *InRole, GetPawn() ? *GetPawn()->GetName() : TEXT("none"));
}

void AEpicodeNetworkPlayerController::ClientSessionFailed_Implementation(const FString& Error)
{
	UE_LOG(LogEpicodeNetwork, Warning, TEXT("Session registration failed: %s"), *Error);
}

void AEpicodeNetworkPlayerController::OnRep_SessionPlayerId()
{
	UE_LOG(LogEpicodeNetwork, Log, TEXT("Replicated session identity: player=%s character=%s role=%s"),
		*SessionPlayerId, *SessionCharacterId, *SessionRole);
}
