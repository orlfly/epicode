#include "EpicodeAgentController.h"

#include "EpicodeNetworkSubsystem.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "TimerManager.h"

AEpicodeAgentController::AEpicodeAgentController()
{
	bWantsPlayerState = false;
}

void AEpicodeAgentController::Configure(const FString& InPlayerId, const FString& InCharacterId)
{
	AgentPlayerId = InPlayerId;
	AgentCharacterId = InCharacterId;
}

bool AEpicodeAgentController::MoveToAgentLocation(const FVector& InTarget, float InAcceptanceRadius, FString& OutError, bool bAllowDirectMovement)
{
	if (!GetPawn())
	{
		OutError = TEXT("CHARACTER_NOT_ASSIGNED: agent controller has no pawn.");
		return false;
	}
	UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!NavSystem || !NavSystem->GetDefaultNavDataInstance(FNavigationSystem::DontCreate))
	{
		if (bAllowDirectMovement)
		{
			StartDirectMove(InTarget);
			return true;
		}
		OutError = TEXT("NAVIGATION_UNAVAILABLE: no navigation data is available in this world.");
		PublishMoveEvent(TEXT("character.move.blocked"), OutError);
		return false;
	}
	const EPathFollowingRequestResult::Type Request = MoveToLocation(InTarget, InAcceptanceRadius, true, true, true, false);
	if (Request == EPathFollowingRequestResult::Failed)
	{
		OutError = TEXT("NAVIGATION_UNAVAILABLE: no path could be found to the target.");
		PublishMoveEvent(TEXT("character.move.blocked"), OutError);
		return false;
	}
	return true;
}

void AEpicodeAgentController::StopAgent()
{
	StopDirectMove();
	StopMovement();
	PublishMoveEvent(TEXT("character.move.stopped"));
}

void AEpicodeAgentController::AdvanceDirectMovement()
{
	UpdateDirectMove();
}

void AEpicodeAgentController::StartDirectMove(const FVector& InTarget)
{
	StopDirectMove();
	DirectMoveTarget = InTarget;
	bDirectMoving = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(DirectMoveTimer, this, &AEpicodeAgentController::UpdateDirectMove, 0.05f, true);
	}
}

void AEpicodeAgentController::UpdateDirectMove()
{
	if (!bDirectMoving)
	{
		return;
	}
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		StopDirectMove();
		return;
	}
	const FVector Current = ControlledPawn->GetActorLocation();
	FVector Direction = DirectMoveTarget - Current;
	Direction.Z = 0.0;
	if (Direction.SizeSquared() <= FMath::Square(60.0))
	{
		StopDirectMove();
		PublishMoveEvent(TEXT("character.move.completed"));
		return;
	}
	const float Speed = 240.0f;
	const FVector Step = Direction.GetSafeNormal() * Speed * 0.05f;
	ControlledPawn->SetActorLocation(Current + Step, false, nullptr, ETeleportType::TeleportPhysics);
}

void AEpicodeAgentController::StopDirectMove()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DirectMoveTimer);
	}
	bDirectMoving = false;
}

void AEpicodeAgentController::LookAtAgentTarget(const FVector& InTarget)
{
	if (APawn* ControlledPawn = GetPawn())
	{
		const FVector Direction = InTarget - ControlledPawn->GetActorLocation();
		if (!Direction.IsNearlyZero())
		{
			SetControlRotation(Direction.Rotation());
		}
	}
}

void AEpicodeAgentController::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	Super::OnMoveCompleted(RequestID, Result);
	if (Result.IsSuccess())
	{
		PublishMoveEvent(TEXT("character.move.completed"));
	}
	else if (Result.Code == EPathFollowingResult::Blocked)
	{
		PublishMoveEvent(TEXT("character.move.blocked"), TEXT("BLOCKED"));
	}
}

void AEpicodeAgentController::PublishMoveEvent(const FString& InType, const FString& InMessage) const
{
	UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
	if (!Subsystem)
	{
		return;
	}
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("playerId"), AgentPlayerId);
	Payload->SetStringField(TEXT("characterId"), AgentCharacterId);
	if (!InMessage.IsEmpty())
	{
		Payload->SetStringField(TEXT("message"), InMessage);
	}
	if (const APawn* ControlledPawn = GetPawn())
	{
		Payload->SetStringField(TEXT("location"), ControlledPawn->GetActorLocation().ToString());
	}
	Subsystem->PublishWorldEvent(AgentPlayerId, AgentCharacterId, InType, Payload);
}
