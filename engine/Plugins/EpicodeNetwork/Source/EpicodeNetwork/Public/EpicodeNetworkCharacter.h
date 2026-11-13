#pragma once

#include "CoreMinimal.h"
#include "EpicodePerformanceTypes.h"
#include "GameFramework/Character.h"
#include "EpicodeNetworkCharacter.generated.h"

UCLASS()
class EPICODENETWORK_API AEpicodeNetworkCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AEpicodeNetworkCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void SetNetworkIdentity(const FString& InPlayerId, const FString& InCharacterId);
	/** Authoritative appearance assignment; the mesh path is replicated to clients. */
	bool SetAppearance(const FString& InMeshPath);
	bool ApplyAppearance(const FString& InMeshPath);

	static bool IsTeleportDistanceAllowed(double InDistance, double InMaxDistance = 500.0);
	/** Validates a server-side movement delta against the configured speed budget. */
	static bool IsMoveDistanceAllowed(double InDeltaSeconds, double InDistance, double InMaxSpeed, double InTolerance = 1.15);
	/** Returns the server location only when the client drift exceeds the correction threshold. */
	static FVector ComputeAuthoritativeCorrection(const FVector& InServerLocation, const FVector& InClientLocation, double InMaxCorrectionDistance);
	/** Offsets a scene entity location so the capsule centre stays above the authored ground point. */
	static FVector MakeCapsuleSpawnLocation(const FVector& InSceneLocation, double InCapsuleHalfHeight);

	bool RequestAction(const FString& InActionTag, bool bInterrupt, double InDurationSeconds, FString& OutError);
	bool SetExpression(const FString& InEmotion, double InIntensity, FString& OutError);
	bool SetVoice(bool bInSpeaking, EEpicodeVisemeSource InSource, FString& OutWarning, FString& OutError);
	bool SetMotionMatching(bool bEnabled, bool bMotionDataAvailable, FString& OutError);
	void PlayLocalActionPrediction(const FString& InActionTag);
	void ApplyLocalMicroExpression(double InIntensity);
	void ApplyLocalIKCorrections(double InHandWeight, double InFootWeight, double InLookAtWeight);

	UFUNCTION(Server, Reliable)
	void ServerRequestAction(const FString& InActionTag, bool bInterrupt, float InDurationSeconds);

	UFUNCTION(Server, Reliable)
	void ServerSetExpression(const FString& InEmotion, float InIntensity);

	UFUNCTION(Server, Reliable)
	void ServerSetVoice(bool bInSpeaking, EEpicodeVisemeSource InSource);

	/** Replicated owner identity, assigned by the authoritative server. */
	UPROPERTY(ReplicatedUsing = OnRep_PlayerId, BlueprintReadOnly, Category = "Epicode|Network")
	FString PlayerId;

	UPROPERTY(ReplicatedUsing = OnRep_CharacterId, BlueprintReadOnly, Category = "Epicode|Network")
	FString CharacterId;

	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Epicode|Network")
	FString AppearanceMeshPath;

	UPROPERTY(ReplicatedUsing = OnRep_Performance, BlueprintReadOnly, Category = "Epicode|Performance")
	FEpicodePerformanceState PerformanceState;

	UPROPERTY(BlueprintReadOnly, Category = "Epicode|Performance")
	float LocalMicroExpressionIntensity = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Epicode|Performance")
	FVector LocalIKWeights = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, Category = "Epicode|Network", meta = (ClampMin = "0.0"))
	double MaxTeleportDistance = 500.0;

	UFUNCTION(Server, Reliable)
	void ServerRequestTeleport(const FVector& TargetLocation, const FRotator& TargetRotation);

protected:
	UFUNCTION()
	void OnRep_PlayerId();

	UFUNCTION()
	void OnRep_CharacterId();

	UFUNCTION()
	void OnRep_Appearance();

	UFUNCTION()
	void OnRep_Performance();

private:
	float TransformRecordAccumulator = 0.0f;
	double ActionRemainingSeconds = 0.0;
};
