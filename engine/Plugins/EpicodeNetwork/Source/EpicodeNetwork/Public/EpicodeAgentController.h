#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "EpicodeAgentController.generated.h"

class UEpicodeNetworkSubsystem;

UCLASS()
class EPICODENETWORK_API AEpicodeAgentController : public AAIController
{
	GENERATED_BODY()

public:
	AEpicodeAgentController();

	void Configure(const FString& InPlayerId, const FString& InCharacterId);
	bool MoveToAgentLocation(const FVector& InTarget, float InAcceptanceRadius, FString& OutError, bool bAllowDirectMovement = true);
	void StopAgent();
	void LookAtAgentTarget(const FVector& InTarget);
	/** Advance one direct-movement step; used when the world timer is driven externally. */
	void AdvanceDirectMovement();
	const FString& GetAgentPlayerId() const { return AgentPlayerId; }
	const FString& GetAgentCharacterId() const { return AgentCharacterId; }

	virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override;

private:
	void PublishMoveEvent(const FString& InType, const FString& InMessage = FString()) const;
	void StartDirectMove(const FVector& InTarget);
	void UpdateDirectMove();
	void StopDirectMove();

	FString AgentPlayerId;
	FString AgentCharacterId;
	FVector DirectMoveTarget = FVector::ZeroVector;
	FTimerHandle DirectMoveTimer;
	bool bDirectMoving = false;
};
