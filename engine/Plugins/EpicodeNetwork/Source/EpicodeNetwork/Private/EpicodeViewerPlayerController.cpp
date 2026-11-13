#include "EpicodeViewerPlayerController.h"

#include "EngineUtils.h"
#include "Engine/World.h"
#include "EpicodeNetworkCharacter.h"
#include "EpicodeNetworkModule.h"
#include "EpicodeNetworkPlayerController.h"
#include "EpicodeNetworkSession.h"
#include "EpicodeNetworkSettings.h"
#include "EpicodeNetworkSubsystem.h"
#include "EpicodeViewerSpectatorPawn.h"
#include "GameFramework/PlayerState.h"
#include "Misc/CommandLine.h"
#include "Net/UnrealNetwork.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

AEpicodeViewerPlayerController::AEpicodeViewerPlayerController()
{
	bReplicates = true;
}

void AEpicodeViewerPlayerController::BeginPlay()
{
	Super::BeginPlay();
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
		DisplayName = TEXT("Viewer");
	}
	FString Token = PendingViewerToken;
	if (Token.IsEmpty())
	{
		FParse::Value(FCommandLine::Get(), TEXT("EpicodeViewerToken="), Token);
	}
	ServerRegisterViewer(DisplayName, Token, PendingCameraMode);
}

void AEpicodeViewerPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || ViewerCameraMode != TEXT("follow"))
	{
		return;
	}
	FollowUpdateAccumulator += DeltaSeconds;
	if (FollowUpdateAccumulator >= 0.1f)
	{
		FollowUpdateAccumulator = 0.0f;
		UpdateFollowCamera();
	}
}

void AEpicodeViewerPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (!InputComponent)
	{
		return;
	}
	InputComponent->BindAction(TEXT("EpicodeViewerCameraFree"), IE_Pressed, this, &AEpicodeViewerPlayerController::SelectFreeCamera);
	InputComponent->BindAction(TEXT("EpicodeViewerCameraFollow"), IE_Pressed, this, &AEpicodeViewerPlayerController::SelectFollowCamera);
	InputComponent->BindAction(TEXT("EpicodeViewerCameraFixed"), IE_Pressed, this, &AEpicodeViewerPlayerController::SelectFixedCamera);
	InputComponent->BindAction(TEXT("EpicodeViewerCameraPreset"), IE_Pressed, this, &AEpicodeViewerPlayerController::SelectNextPresetCamera);
	InputComponent->BindAction(TEXT("EpicodeViewerOverlay"), IE_Pressed, this, &AEpicodeViewerPlayerController::ToggleOverlay);
}

void AEpicodeViewerPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AEpicodeViewerPlayerController, ViewerId);
	DOREPLIFETIME(AEpicodeViewerPlayerController, ViewerCameraMode);
	DOREPLIFETIME(AEpicodeViewerPlayerController, ViewerCameraId);
	DOREPLIFETIME(AEpicodeViewerPlayerController, FollowTargetId);
	DOREPLIFETIME(AEpicodeViewerPlayerController, bOverlayVisible);
}

bool AEpicodeViewerPlayerController::IsRemoteViewerAllowed() const
{
	const UEpicodeNetworkSettings* Settings = UEpicodeNetworkSettings::Get();
	if (Settings->bAllowRemoteViewers)
	{
		return true;
	}
	if (IsLocalController())
	{
		return true;
	}
	UNetConnection* Connection = GetNetConnection();
	if (!Connection)
	{
		return true;
	}
	const FString RemoteAddress = Connection->LowLevelGetRemoteAddress();
	return RemoteAddress.Contains(TEXT("127.0.0.1")) || RemoteAddress.Contains(TEXT("::1"));
}

bool AEpicodeViewerPlayerController::AuthorizeViewer(const FString& InToken, bool bRemote, FString& OutError)
{
	OutError.Reset();
	const UEpicodeNetworkSettings* Settings = UEpicodeNetworkSettings::Get();
	if (bRemote && !Settings->bAllowRemoteViewers)
	{
		OutError = TEXT("VIEWER_NOT_AUTHORIZED: remote viewers are disabled.");
		return false;
	}
	FString TokenError;
	if (!Settings->EnsureViewerToken(TokenError))
	{
		OutError = TEXT("VIEWER_NOT_AUTHORIZED: viewer token is not configured.");
		return false;
	}
	const FString ExpectedToken = Settings->GetViewerToken();
	if (InToken.IsEmpty() || InToken != ExpectedToken)
	{
		OutError = TEXT("VIEWER_NOT_AUTHORIZED: viewer token is missing or invalid.");
		return false;
	}
	return true;
}

AEpicodeViewerSpectatorPawn* AEpicodeViewerPlayerController::EnsureSpectatorPawn()
{
	if (AEpicodeViewerSpectatorPawn* Existing = Cast<AEpicodeViewerSpectatorPawn>(GetPawn()))
	{
		return Existing;
	}
	if (!GetWorld())
	{
		return nullptr;
	}
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AEpicodeViewerSpectatorPawn* Spectator = GetWorld()->SpawnActor<AEpicodeViewerSpectatorPawn>(
		AEpicodeViewerSpectatorPawn::StaticClass(), FVector(0.0, 0.0, 180.0), FRotator::ZeroRotator, SpawnParams);
	if (Spectator)
	{
		Possess(Spectator);
		SetViewTarget(Spectator);
	}
	return Spectator;
}

void AEpicodeViewerPlayerController::ServerRegisterViewer_Implementation(const FString& DisplayName, const FString& Token, const FString& CameraMode)
{
	if (!HasAuthority() || IsViewerRegistered())
	{
		return;
	}
	FString AuthError;
	if (!AuthorizeViewer(Token, !IsRemoteViewerAllowed(), AuthError))
	{
		ClientViewerAuthFailed(AuthError);
		return;
	}

	UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
	FEpicodeNetworkSession* Session = Subsystem ? Subsystem->GetSession() : nullptr;
	if (!Session)
	{
		ClientViewerAuthFailed(TEXT("SESSION_NOT_FOUND: the server has no active session."));
		return;
	}

	const FString SafeName = AEpicodeNetworkPlayerController::MakeSafePlayerId(DisplayName);
	FString PlayerId = SafeName + TEXT("-viewer");
	int32 Suffix = 2;
	while (Session->FindPlayer(PlayerId))
	{
		PlayerId = FString::Printf(TEXT("%s-viewer-%d"), *SafeName, Suffix++);
	}
	FString Error;
	FString JoinedPlayerId;
	if (!Subsystem->JoinSession(PlayerId, DisplayName, EEpicodeSessionRole::Observer, JoinedPlayerId, Error, EEpicodeParticipantKind::Viewer))
	{
		ClientViewerAuthFailed(Error);
		return;
	}

	ViewerId = JoinedPlayerId;
	ViewerCameraMode = CameraMode.IsEmpty() ? TEXT("free") : CameraMode;
	if (FEpicodeSessionPlayer* Player = Session->FindPlayer(ViewerId))
	{
		Player->ViewerCameraMode = ViewerCameraMode;
	}
	AEpicodeViewerSpectatorPawn* Spectator = EnsureSpectatorPawn();
	if (Spectator)
	{
		Spectator->SetCameraMode(ViewerCameraMode);
	}

	if (!ViewerCameraMode.Equals(TEXT("follow"), ESearchCase::IgnoreCase))
	{
		FollowTargetId.Reset();
	}
	else
	{
		for (TActorIterator<AEpicodeNetworkCharacter> It(GetWorld()); It; ++It)
		{
			if (It->CharacterId != ViewerId)
			{
				FollowTargetId = It->CharacterId;
				break;
			}
		}
	}

	TSharedPtr<FJsonObject> Setup = MakeShared<FJsonObject>();
	Setup->SetStringField(TEXT("viewerId"), ViewerId);
	Setup->SetStringField(TEXT("sessionId"), Session->GetSessionId());
	Setup->SetStringField(TEXT("state"), LexToString(Session->GetState()));
	Setup->SetStringField(TEXT("cameraMode"), ViewerCameraMode);
	Setup->SetStringField(TEXT("cameraId"), ViewerCameraId);
	Setup->SetStringField(TEXT("followTarget"), FollowTargetId);
	Setup->SetObjectField(TEXT("session"), Session->ToJson());
	TArray<TSharedPtr<FJsonValue>> Cameras;
	for (const FEpicodeNetworkSceneEntity& Camera : Subsystem->GetSceneSummary().Cameras)
	{
		TSharedPtr<FJsonObject> CameraJson = Camera.ToJson();
		Cameras.Add(MakeShared<FJsonValueObject>(CameraJson));
	}
	Setup->SetArrayField(TEXT("cameras"), Cameras);
	const TSharedPtr<FJsonObject> Runtime = Subsystem->GetSceneSummary().Runtime;
	if (Runtime.IsValid())
	{
		const TSharedPtr<FJsonObject>* ViewerRuntime = nullptr;
		if (Runtime->TryGetObjectField(TEXT("viewer"), ViewerRuntime) && ViewerRuntime && ViewerRuntime->IsValid())
		{
			FString DefaultCameraId;
			if ((*ViewerRuntime)->TryGetStringField(TEXT("defaultCameraId"), DefaultCameraId) && !DefaultCameraId.IsEmpty())
			{
				Setup->SetStringField(TEXT("defaultCameraId"), DefaultCameraId);
				if (ViewerCameraId.IsEmpty())
				{
					ViewerCameraId = DefaultCameraId;
				}
			}
			const TSharedPtr<FJsonObject>* SpawnTransform = nullptr;
			if ((*ViewerRuntime)->TryGetObjectField(TEXT("spawnTransform"), SpawnTransform) && SpawnTransform && SpawnTransform->IsValid())
			{
				Setup->SetObjectField(TEXT("defaultCameraTransform"), *SpawnTransform);
			}
		}
	}
	TArray<TSharedPtr<FJsonValue>> Participants;
	for (const FEpicodeSessionPlayer& Player : Session->GetPlayers())
	{
		Participants.Add(MakeShared<FJsonValueObject>(Player.ToJson()));
	}
	Setup->SetArrayField(TEXT("participants"), Participants);
	FString SetupText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&SetupText);
	FJsonSerializer::Serialize(Setup.ToSharedRef(), Writer);
	SetupJson = SetupText;
	ClientViewerSetup(SetupText);
	UE_LOG(LogEpicodeNetwork, Log, TEXT("Viewer '%s' registered with camera '%s'"), *ViewerId, *ViewerCameraMode);
}

void AEpicodeViewerPlayerController::ServerSetCameraMode_Implementation(const FString& Mode, const FString& CameraId, const FString& InFollowTargetId)
{
	if (!HasAuthority() || ViewerId.IsEmpty())
	{
		return;
	}
	FString Normalized = Mode.ToLower();
	if (Normalized != TEXT("free") && Normalized != TEXT("follow") && Normalized != TEXT("fixed") && Normalized != TEXT("preset"))
	{
		return;
	}
	ViewerCameraMode = Normalized;
	ViewerCameraId = CameraId;
	if (Normalized == TEXT("follow") && InFollowTargetId.IsEmpty())
	{
		if (UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get())
		{
			if (FEpicodeNetworkSession* Session = Subsystem->GetSession())
			{
				for (const FEpicodeSessionPlayer& Player : Session->GetPlayers())
				{
					if (!Player.CharacterId.IsEmpty() && Player.PlayerId != ViewerId)
					{
						this->FollowTargetId = Player.CharacterId;
						break;
					}
				}
			}
		}
	}
	else if (!InFollowTargetId.IsEmpty())
	{
		this->FollowTargetId = InFollowTargetId;
	}
	if (AEpicodeViewerSpectatorPawn* Spectator = Cast<AEpicodeViewerSpectatorPawn>(GetPawn()))
	{
		Spectator->SetCameraMode(ViewerCameraMode, ViewerCameraId, this->FollowTargetId);
	}
	if (UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get())
	{
		if (FEpicodeNetworkSession* Session = Subsystem->GetSession())
		{
			if (FEpicodeSessionPlayer* Player = Session->FindPlayer(ViewerId))
			{
				Player->ViewerCameraMode = ViewerCameraMode;
				Player->ViewerCameraId = ViewerCameraId;
			}
		}
	}
}

void AEpicodeViewerPlayerController::ServerSetOverlayVisible_Implementation(bool bVisible)
{
	if (HasAuthority() && !ViewerId.IsEmpty())
	{
		bOverlayVisible = bVisible;
	}
}

void AEpicodeViewerPlayerController::ClientViewerSetup_Implementation(const FString& InSetupJson)
{
	SetupJson = InSetupJson;
	UE_LOG(LogEpicodeNetwork, Log, TEXT("Viewer setup received: %s"), *SetupJson);
}

void AEpicodeViewerPlayerController::ClientViewerAuthFailed_Implementation(const FString& Error)
{
	UE_LOG(LogEpicodeNetwork, Warning, TEXT("Viewer authentication failed: %s"), *Error);
}

void AEpicodeViewerPlayerController::SelectCameraMode(const FString& Mode)
{
	ServerSetCameraMode(Mode, TEXT(""), TEXT(""));
}

void AEpicodeViewerPlayerController::SelectFreeCamera()
{
	SelectCameraMode(TEXT("free"));
}

void AEpicodeViewerPlayerController::SelectFollowCamera()
{
	SelectCameraMode(TEXT("follow"));
}

void AEpicodeViewerPlayerController::SelectFixedCamera()
{
	SelectCameraMode(TEXT("fixed"));
}

void AEpicodeViewerPlayerController::SelectNextPresetCamera()
{
	SelectCameraMode(TEXT("preset"));
}

void AEpicodeViewerPlayerController::ToggleOverlay()
{
	ServerSetOverlayVisible(!bOverlayVisible);
}

void AEpicodeViewerPlayerController::UpdateFollowCamera()
{
	if (FollowTargetId.IsEmpty() || !GetWorld())
	{
		return;
	}
	for (TActorIterator<AEpicodeNetworkCharacter> It(GetWorld()); It; ++It)
	{
		if (It->CharacterId != FollowTargetId)
		{
			continue;
		}
		if (APawn* Spectator = GetPawn())
		{
			const FVector TargetLocation = It->GetActorLocation() + FVector(0.0, 0.0, 120.0);
			Spectator->SetActorLocation(TargetLocation, false, nullptr, ETeleportType::TeleportPhysics);
			const FVector Direction = It->GetActorLocation() - TargetLocation;
			if (!Direction.IsNearlyZero())
			{
				Spectator->SetActorRotation(Direction.Rotation());
			}
		}
		break;
	}
}
