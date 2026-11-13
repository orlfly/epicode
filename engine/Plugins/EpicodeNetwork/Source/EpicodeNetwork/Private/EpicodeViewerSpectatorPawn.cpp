#include "EpicodeViewerSpectatorPawn.h"

#include "Camera/CameraComponent.h"
#include "Net/UnrealNetwork.h"

AEpicodeViewerSpectatorPawn::AEpicodeViewerSpectatorPawn()
{
	bReplicates = true;
	ViewerCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ViewerCamera"));
	if (ViewerCamera)
	{
		ViewerCamera->SetupAttachment(GetRootComponent());
		ViewerCamera->SetRelativeLocation(FVector(0.0, 0.0, 64.0));
		ViewerCamera->bUsePawnControlRotation = true;
	}
}

void AEpicodeViewerSpectatorPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AEpicodeViewerSpectatorPawn, CameraMode);
	DOREPLIFETIME(AEpicodeViewerSpectatorPawn, CameraId);
	DOREPLIFETIME(AEpicodeViewerSpectatorPawn, FollowTargetId);
}

void AEpicodeViewerSpectatorPawn::SetCameraMode(const FString& InMode, const FString& InCameraId, const FString& InFollowTargetId)
{
	CameraMode = InMode.IsEmpty() ? TEXT("free") : InMode;
	CameraId = InCameraId;
	FollowTargetId = InFollowTargetId;
}
