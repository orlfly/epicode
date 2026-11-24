#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EpicodeLocalIK.h"
#include "EpicodeNetworkSettings.generated.h"

UCLASS(config = Epicode, defaultconfig, meta = (DisplayName = "Epicode Network"))
class EPICODENETWORK_API UEpicodeNetworkSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UEpicodeNetworkSettings();

	virtual FName GetContainerName() const override { return TEXT("Project"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("Epicode Network"); }

	static const UEpicodeNetworkSettings* Get() { return GetDefault<UEpicodeNetworkSettings>(); }

	FString GetSessionRootPath() const;
	FString GetViewerTokenFilePath() const;
	FString GetViewerToken() const;
	bool EnsureViewerToken(FString& OutError) const;

	UPROPERTY(EditAnywhere, Config, Category = "Session", meta = (ClampMin = "1", ClampMax = "65535"))
	int32 GamePort = 7777;

	UPROPERTY(EditAnywhere, Config, Category = "Session", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxPlayers = 8;

	UPROPERTY(EditAnywhere, Config, Category = "Viewer", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxViewers = 4;

	UPROPERTY(EditAnywhere, Config, Category = "Viewer")
	FString ViewerTokenFile = TEXT("Saved/Epicode/viewer-token");

	UPROPERTY(EditAnywhere, Config, Category = "Viewer")
	bool bAllowRemoteViewers = false;

	UPROPERTY(EditAnywhere, Config, Category = "Agent", meta = (ClampMin = "1"))
	double AgentLeaseSeconds = 30.0;

	UPROPERTY(EditAnywhere, Config, Category = "Agent", meta = (ClampMin = "1"))
	double AgentHeartbeatSeconds = 10.0;

	UPROPERTY(EditAnywhere, Config, Category = "Agent", meta = (ClampMin = "1"))
	double AgentCommandTimeoutSeconds = 5.0;

	UPROPERTY(EditAnywhere, Config, Category = "Agent")
	bool bAllowRemoteAgents = false;

	UPROPERTY(EditAnywhere, Config, Category = "Agent", meta = (ClampMin = "1", ClampMax = "1000"))
	int32 AgentCommandsPerSecond = 20;

	UPROPERTY(EditAnywhere, Config, Category = "Agent", meta = (ClampMin = "1024"))
	int32 AgentAuditLogMaxBytes = 1048576;

	UPROPERTY(EditAnywhere, Config, Category = "Session", meta = (ClampMin = "0"))
	double ReconnectGraceSeconds = 60.0;

	UPROPERTY(EditAnywhere, Config, Category = "Session")
	FString SessionRoot = TEXT("Saved/Epicode/Sessions");

	UPROPERTY(EditAnywhere, Config, Category = "Session")
	FString DefaultCharacterEntityId;

	UPROPERTY(EditAnywhere, Config, Category = "Session")
	FString DefaultCharacterMesh;

	UPROPERTY(EditAnywhere, Config, Category = "Session")
	FString OnlineSubsystem = TEXT("LAN");

	UPROPERTY(EditAnywhere, Config, Category = "Session")
	bool bAllowRemote = false;

	UPROPERTY(EditAnywhere, Config, Category = "Session")
	bool bAutoExportSession = true;

	UPROPERTY(EditAnywhere, Config, Category = "Local IK")
	FEpicodeLocalIKSettings LocalIK;
};
