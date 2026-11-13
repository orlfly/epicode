#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SpectatorPawn.h"
#include "EpicodeViewerSpectatorPawn.generated.h"

class UCameraComponent;

/**
 * Server-authoritative spectator pawn used by world viewers. It never carries a
 * performer character and exposes camera mode/follow state for replication.
 */
UCLASS()
class EPICODENETWORK_API AEpicodeViewerSpectatorPawn : public ASpectatorPawn
{
	GENERATED_BODY()

public:
	AEpicodeViewerSpectatorPawn();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category = "Epicode|Viewer")
	void SetCameraMode(const FString& InMode, const FString& InCameraId = FString(), const FString& InFollowTargetId = FString());

	UFUNCTION(BlueprintPure, Category = "Epicode|Viewer")
	const FString& GetCameraMode() const { return CameraMode; }

	UFUNCTION(BlueprintPure, Category = "Epicode|Viewer")
	const FString& GetCameraId() const { return CameraId; }

	UFUNCTION(BlueprintPure, Category = "Epicode|Viewer")
	const FString& GetFollowTargetId() const { return FollowTargetId; }

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Epicode|Viewer")
	FString CameraMode = TEXT("free");

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Epicode|Viewer")
	FString CameraId;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Epicode|Viewer")
	FString FollowTargetId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Epicode|Viewer")
	TObjectPtr<UCameraComponent> ViewerCamera;
};
