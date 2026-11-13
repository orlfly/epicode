#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "EpicodeViewerPlayerController.generated.h"

class AEpicodeViewerSpectatorPawn;

/**
 * Viewer-side controller. It never registers as a performer, never requests a
 * character and only controls its spectator pawn camera.
 */
UCLASS()
class EPICODENETWORK_API AEpicodeViewerPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AEpicodeViewerPlayerController();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupInputComponent() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	bool IsViewerRegistered() const { return !ViewerId.IsEmpty(); }
	const FString& GetViewerId() const { return ViewerId; }
	const FString& GetViewerCameraMode() const { return ViewerCameraMode; }
	const FString& GetSetupJson() const { return SetupJson; }
	bool IsOverlayVisible() const { return bOverlayVisible; }
	void SetPendingViewerToken(const FString& InToken) { PendingViewerToken = InToken; }
	void SetPendingCameraMode(const FString& InMode) { PendingCameraMode = InMode; }

	/** Shared server-side token/remote policy used by login and tests. */
	static bool AuthorizeViewer(const FString& InToken, bool bRemote, FString& OutError);

	UFUNCTION(Server, Reliable)
	void ServerRegisterViewer(const FString& DisplayName, const FString& Token, const FString& CameraMode);

	UFUNCTION(Server, Reliable)
	void ServerSetCameraMode(const FString& Mode, const FString& CameraId, const FString& InFollowTargetId);

	UFUNCTION(Server, Reliable)
	void ServerSetOverlayVisible(bool bVisible);

	UFUNCTION(Client, Reliable)
	void ClientViewerSetup(const FString& InSetupJson);

	UFUNCTION(Client, Reliable)
	void ClientViewerAuthFailed(const FString& Error);

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Epicode|Viewer")
	FString ViewerId;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Epicode|Viewer")
	FString ViewerCameraMode = TEXT("free");

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Epicode|Viewer")
	FString ViewerCameraId;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Epicode|Viewer")
	FString FollowTargetId;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Epicode|Viewer")
	bool bOverlayVisible = true;

private:
	void SelectCameraMode(const FString& Mode);
	void SelectFreeCamera();
	void SelectFollowCamera();
	void SelectFixedCamera();
	void SelectNextPresetCamera();
	void ToggleOverlay();
	void UpdateFollowCamera();
	bool IsRemoteViewerAllowed() const;
	AEpicodeViewerSpectatorPawn* EnsureSpectatorPawn();

	FString PendingViewerToken;
	FString PendingCameraMode = TEXT("free");
	FString SetupJson;
	float FollowUpdateAccumulator = 0.0f;
};
