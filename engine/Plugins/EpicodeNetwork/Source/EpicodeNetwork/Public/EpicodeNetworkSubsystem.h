#pragma once

#include "CoreMinimal.h"
#include "EpicodeNetworkSceneLoader.h"
#include "EpicodeNetworkSession.h"
#include "EpicodeNetworkTypes.h"
#include "EpicodeWorldState.h"
#include "Subsystems/EngineSubsystem.h"
#include "Containers/Ticker.h"
#include "EpicodeNetworkSubsystem.generated.h"

class FEpicodeNetworkSandboxBridge;

UCLASS()
class EPICODENETWORK_API UEpicodeNetworkSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static UEpicodeNetworkSubsystem* Get();

	bool CreateSession(const FString& InSceneId, const FString& InOwnerPlayerId, const FString& InOwnerDisplayName, int32 InMaxPlayers, double InReconnectGraceSeconds, FString& OutError, int32 InMaxViewers = 0);
	bool StartSession(FString& OutError);
	bool JoinSession(const FString& InPlayerId, const FString& InDisplayName, EEpicodeSessionRole InRole, FString& OutPlayerId, FString& OutError, EEpicodeParticipantKind InParticipantKind = EEpicodeParticipantKind::Performer);
	bool LeaveSession(const FString& InPlayerId, FString& OutError);
	bool ReconnectSession(const FString& InPlayerId, FString& OutError);
	bool AcquireAgentLease(const FString& InPlayerId, const FString& InCharacterId, FString& OutLeaseId, double& OutExpiresAt, FString& OutError);
	bool HeartbeatAgentLease(const FString& InPlayerId, FString& OutLeaseId, double& OutExpiresAt, FString& OutError);
	bool ReleaseAgentLease(const FString& InPlayerId, FString& OutError);
	bool MoveAgent(const FString& InPlayerId, const FVector& InTarget, FString& OutError);
	bool LookAgent(const FString& InPlayerId, const FVector& InTarget, FString& OutError);
	bool StopAgent(const FString& InPlayerId, FString& OutError);
	TSharedPtr<FJsonObject> GetAgentState(const FString& InPlayerId, FString& OutError) const;
	FString IssueAgentToken(const FString& InPlayerId);
	bool ResolveAgentToken(const FString& InToken, FString& OutPlayerId, FString& OutError) const;
	bool CheckAgentCommandRate(const FString& InPlayerId, FString& OutError);
	void RevokeAgentToken(const FString& InPlayerId);
	bool EndSession(FString& OutError);
	bool ClearEndedSession(FString& OutError);
	bool KickPlayer(const FString& InRequesterId, const FString& InTargetId, FString& OutError);
	bool SetPlayerRole(const FString& InRequesterId, const FString& InTargetId, EEpicodeSessionRole InRole, FString& OutError);
	bool AssignCharacter(const FString& InRequesterId, const FString& InTargetPlayerId, const FString& InCharacterId, FString& OutError);
	bool ReleaseCharacter(const FString& InRequesterId, const FString& InTargetPlayerId, FString& OutError);
	bool TransferCharacter(const FString& InRequesterId, const FString& InFromPlayerId, const FString& InToPlayerId, const FString& InCharacterId, FString& OutError);
	bool RecordPlayerTransform(const FString& InPlayerId, const FVector& InLocation, const FRotator& InRotation, FString& OutError);
	bool PublishPerformanceEvent(const FString& InPlayerId, const FString& InCharacterId, const FString& InType, const TSharedPtr<FJsonObject>& InPayload);
	bool PublishWorldEvent(const FString& InPlayerId, const FString& InEntityId, const FString& InType, const TSharedPtr<FJsonObject>& InPayload = nullptr);
	FEpicodeWorldState& GetWorldState() { return WorldState; }
	const FEpicodeWorldState& GetWorldState() const { return WorldState; }

	void Sync();
	void SpawnRuntimeSceneActors();
	void DestroyRuntimeSceneActors();
	void SyncRuntimeWorldActors();
	void DestroyRuntimeWorldActors();
	bool HasActiveSession() const;
	void AttachToWorld();
	static bool ShouldTravelToLevel(const UWorld* InWorld, const FString& LevelPath);
	bool ExportSession(FString& OutSessionPath, FString& OutEventPath, FString& OutError);
	bool LoadScene(const FString& InSceneId, FEpicodeNetworkSceneSummary& OutSummary, FString& OutError) const;
	bool GetNextFreeCharacter(FString& OutEntityId, FString& OutMeshPath, FTransform& OutTransform) const;
	bool GetCharacterInfo(const FString& InEntityId, FString& OutMeshPath, FTransform& OutTransform) const;
	const FEpicodeNetworkSceneSummary& GetSceneSummary() const { return SceneSummary; }

	FEpicodeNetworkSession* GetSession() const { return Session.Get(); }
	const FEpicodeNetworkSandboxBridge* GetSandboxBridge() const { return SandboxBridge.Get(); }
	FEpicodeNetworkSandboxBridge* GetSandboxBridgeMutable() { return SandboxBridge.Get(); }
	const FString& GetLastError() const { return LastError; }
	TSharedPtr<FJsonObject> GetStatusJson() const;

private:
	void MaybeExport();
	UWorld* FindGameWorld() const;
	bool CreateSandboxBridge(const FEpicodeNetworkSceneSummary& InSummary);
	void SyncSandboxPlayer(const FString& InPlayerId);
	void CloseSandboxBridge();

	TSharedPtr<FEpicodeNetworkSession> Session;
	TSharedPtr<FEpicodeNetworkSandboxBridge> SandboxBridge;
	FEpicodeNetworkSceneSummary SceneSummary;
	FEpicodeWorldState WorldState;
	TMap<FString, FString> AgentTokens;
	TMap<FString, TWeakObjectPtr<class AEpicodeAgentController>> AgentControllers;
	struct FAgentRateWindow
	{
		double WindowStart = 0.0;
		int32 Count = 0;
	};
	TMap<FString, FAgentRateWindow> AgentRateWindows;
	FCriticalSection AgentRateLock;
	FString LastError;
	TArray<TWeakObjectPtr<AActor>> RuntimeSceneActors;
	TWeakObjectPtr<class AEpicodeRuntimeEnvironmentActor> RuntimeEnvironmentActor;
	TMap<FString, TWeakObjectPtr<AActor>> RuntimeItemActors;
	TMap<FString, TWeakObjectPtr<AActor>> RuntimeBuildActors;
	int64 LastExportedEventSequence = -1;
	uint64 LastExportedWorldVersion = 0;
	FTSTicker::FDelegateHandle TickerHandle;
};
