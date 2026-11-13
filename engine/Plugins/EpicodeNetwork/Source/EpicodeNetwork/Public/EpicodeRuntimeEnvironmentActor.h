#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EpicodeEnvironmentController.h"
#include "EpicodeRuntimeEnvironmentActor.generated.h"

/**
 * Replicates the scene environment document to viewers. The server writes the
 * JSON and clients apply it through the shared environment controller.
 */
UCLASS()
class EPICODENETWORK_API AEpicodeRuntimeEnvironmentActor : public AActor
{
	GENERATED_BODY()

public:
	AEpicodeRuntimeEnvironmentActor();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void SetEnvironmentJson(const FString& InJson);
	const FString& GetEnvironmentJson() const { return EnvironmentJson; }
	bool ApplyEnvironment(FString& OutError);

	UPROPERTY(ReplicatedUsing = OnRep_Environment)
	FString EnvironmentJson;

private:
	UFUNCTION()
	void OnRep_Environment();

	FEpicodeEnvironmentController EnvironmentController;
	bool bEnvironmentInitialized = false;
};
