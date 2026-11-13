#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "EpicodeNetworkPlayerController.generated.h"

UCLASS()
class EPICODENETWORK_API AEpicodeNetworkPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AEpicodeNetworkPlayerController();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	bool IsSessionRegistered() const { return !SessionPlayerId.IsEmpty(); }
	const FString& GetSessionPlayerId() const { return SessionPlayerId; }
	const FString& GetSessionCharacterId() const { return SessionCharacterId; }
	const FString& GetSessionRole() const { return SessionRole; }

	UPROPERTY(ReplicatedUsing = OnRep_SessionPlayerId, BlueprintReadOnly, Category = "Epicode|Network")
	FString SessionPlayerId;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Epicode|Network")
	FString SessionCharacterId;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Epicode|Network")
	FString SessionRole;

	UFUNCTION(Server, Reliable)
	void ServerRegisterPlayer(const FString& DisplayName);

	UFUNCTION(Client, Reliable)
	void ClientSessionJoined(const FString& InPlayerId, const FString& InCharacterId, const FString& InRole);

	UFUNCTION(Client, Reliable)
	void ClientSessionFailed(const FString& Error);

	UFUNCTION(Server, Unreliable)
	void ServerReportRemoteDrift(const FString& InCharacterId, const FVector& InLocation);

	static FString MakeSafePlayerId(const FString& InDisplayName);

protected:
	UFUNCTION()
	void OnRep_SessionPlayerId();

private:
	void StartSmokeMovement();
	void StepSmokeMovement();
	void RunSmokePerformance();
	void RunSmokeWorld();

	float DriftReportAccumulator = 0.0f;
	int32 SmokeMoveSteps = 0;
	FTimerHandle SmokeMoveTimer;
	FTimerHandle SmokePerformanceTimer;
	FTimerHandle SmokeWorldTimer;
};
