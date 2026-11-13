#include "EpicodeNetworkGameMode.h"

#include "EpicodeNetworkCharacter.h"
#include "EpicodeNetworkModule.h"
#include "EpicodeNetworkPlayerController.h"
#include "EpicodeNetworkSubsystem.h"
#include "EpicodeViewerPlayerController.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"

AEpicodeNetworkGameMode::AEpicodeNetworkGameMode()
{
	DefaultPawnClass = AEpicodeNetworkCharacter::StaticClass();
	PlayerControllerClass = AEpicodeNetworkPlayerController::StaticClass();
	ViewerControllerClass = AEpicodeViewerPlayerController::StaticClass();
	bStartPlayersAsSpectators = false;
}

void AEpicodeNetworkGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	if (!HasAuthority())
	{
		return;
	}
	FParse::Value(FCommandLine::Get(), TEXT("EpicodeSessionScene="), SessionSceneId);
	if (SessionSceneId.IsEmpty())
	{
		UE_LOG(LogEpicodeNetwork, Log, TEXT("No -EpicodeSessionScene supplied; waiting for an external session."));
		return;
	}

	UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
	if (!Subsystem)
	{
		UE_LOG(LogEpicodeNetwork, Error, TEXT("Cannot create session: network subsystem is unavailable."));
		return;
	}
	if (Subsystem->HasActiveSession())
	{
		Subsystem->AttachToWorld();
		UE_LOG(LogEpicodeNetwork, Log, TEXT("Attached to existing session '%s' after level travel"),
			*Subsystem->GetSession()->GetSessionId());
		return;
	}

	FString Error;
	int32 GraceSeconds = -1;
	FParse::Value(FCommandLine::Get(), TEXT("EpicodeReconnectGrace="), GraceSeconds);
	if (!Subsystem->CreateSession(SessionSceneId, TEXT("server"), TEXT("Server"), 0, static_cast<double>(GraceSeconds), Error))
	{
		UE_LOG(LogEpicodeNetwork, Error, TEXT("Failed to create session for scene '%s': %s"), *SessionSceneId, *Error);
		return;
	}
	UE_LOG(LogEpicodeNetwork, Log, TEXT("Session created for scene '%s' as '%s'"),
		*SessionSceneId, *Subsystem->GetSession()->GetSessionId());
}

void AEpicodeNetworkGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	UE_LOG(LogEpicodeNetwork, Log, TEXT("PostLogin for %s"), NewPlayer ? *NewPlayer->GetName() : TEXT("null"));
}

APlayerController* AEpicodeNetworkGameMode::SpawnPlayerController(ENetRole InRemoteRole, const FString& Options)
{
	FString ViewerOption;
	const bool bViewerFromOptions = FParse::Value(*Options, TEXT("EpicodeViewer="), ViewerOption) && ViewerOption != TEXT("0");
	if (bViewerFromOptions || FParse::Param(*Options, TEXT("EpicodeViewer")) || FParse::Param(FCommandLine::Get(), TEXT("EpicodeViewer")))
	{
		UClass* ViewerClass = ViewerControllerClass ? ViewerControllerClass.Get() : AEpicodeViewerPlayerController::StaticClass();
		APlayerController* Viewer = SpawnPlayerControllerCommon(
			InRemoteRole,
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			ViewerClass);
		if (AEpicodeViewerPlayerController* ViewerController = Cast<AEpicodeViewerPlayerController>(Viewer))
		{
			FString Token;
			FParse::Value(*Options, TEXT("EpicodeViewerToken="), Token);
			if (Token.IsEmpty())
			{
				FParse::Value(FCommandLine::Get(), TEXT("EpicodeViewerToken="), Token);
			}
			FString CameraMode;
			FParse::Value(*Options, TEXT("EpicodeViewerCamera="), CameraMode, false);
			if (CameraMode.IsEmpty())
			{
				FParse::Value(FCommandLine::Get(), TEXT("EpicodeViewerCamera="), CameraMode, false);
			}
			ViewerController->SetPendingViewerToken(Token);
			if (!CameraMode.IsEmpty())
			{
				ViewerController->SetPendingCameraMode(CameraMode);
			}
		}
		return Viewer;
	}
	return Super::SpawnPlayerController(InRemoteRole, Options);
}

void AEpicodeNetworkGameMode::RestartPlayer(AController* NewPlayer)
{
	if (Cast<AEpicodeViewerPlayerController>(NewPlayer))
	{
		if (APawn* ExistingPawn = NewPlayer->GetPawn())
		{
			ExistingPawn->Destroy();
		}
		return;
	}
	Super::RestartPlayer(NewPlayer);
}

void AEpicodeNetworkGameMode::Logout(AController* Exiting)
{
	if (Exiting && HasAuthority())
	{
		if (AEpicodeViewerPlayerController* Viewer = Cast<AEpicodeViewerPlayerController>(Exiting))
		{
			const FString ViewerId = Viewer->GetViewerId();
			if (!ViewerId.IsEmpty())
			{
				if (UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get())
				{
					FString Error;
					Subsystem->LeaveSession(ViewerId, Error);
					UE_LOG(LogEpicodeNetwork, Log, TEXT("Viewer '%s' disconnected from session"), *ViewerId);
				}
			}
		}
		if (AEpicodeNetworkPlayerController* Controller = Cast<AEpicodeNetworkPlayerController>(Exiting))
		{
			const FString PlayerId = Controller->GetSessionPlayerId();
			if (!PlayerId.IsEmpty())
			{
				if (UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get())
				{
					FString Error;
					Subsystem->LeaveSession(PlayerId, Error);
					UE_LOG(LogEpicodeNetwork, Log, TEXT("Player '%s' disconnected from session"), *PlayerId);
				}
			}
		}
	}
	Super::Logout(Exiting);
}
