#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "EpicodeNetworkGameMode.generated.h"

UCLASS()
class EPICODENETWORK_API AEpicodeNetworkGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AEpicodeNetworkGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual APlayerController* SpawnPlayerController(ENetRole InRemoteRole, const FString& Options) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

	const FString& GetSessionSceneId() const { return SessionSceneId; }

	UPROPERTY(EditDefaultsOnly, Category = "Epicode|Viewer")
	TSubclassOf<APlayerController> ViewerControllerClass;

private:
	FString SessionSceneId;
};
