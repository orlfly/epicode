#pragma once

#include "CoreMinimal.h"
#include "EpicodeNetworkTypes.h"
#include "EpicodeProductionDocument.h"
#include "EpicodeSandboxProjection.h"
#include "EpicodeSandboxSession.h"
#include "EpicodeSceneDocument.h"

class EPICODENETWORK_API FEpicodeNetworkSandboxBridge
{
public:
	bool Create(const FEpicodeProductionDocument& InProduction, const FEpicodeSceneDocument& InScene, const FEpicodeSandboxSessionConfig& InConfig, FString& OutError);
	bool CreateLegacy(const FEpicodeSceneDocument& InScene, const FEpicodeSandboxSessionConfig& InConfig, FString& OutError);
	bool Start(FString& OutError);
	bool Close(FString& OutError);
	void Tick(double InDeltaSeconds);

	bool BindPlayer(const FString& InPlayerId, EEpicodeSandboxParticipantKind InKind, const FString& InCharacterId, FString& OutError);
	bool SetPlayerConnected(const FString& InPlayerId, bool bInConnected, FString& OutError);
	bool AssignCharacter(const FString& InPlayerId, const FString& InCharacterId, FString& OutError);
	bool RecordPlayerTransform(const FString& InPlayerId, const FVector& InLocation, const FRotator& InRotation, FString& OutError);
	bool CanSubmit(const FString& InPlayerId, EEpicodeSessionAction InAction, FString& OutError) const;

	TArray<FEpicodeSandboxProjectionCommand> BuildProjection();
	TArray<FEpicodeNetworkEvent> CollectEventsSince(int64 InSequence) const;
	TSharedPtr<FJsonObject> GetReplicationSnapshot() const;

	bool IsLegacy() const { return bLegacy; }
	bool IsReplayable() const { return !bLegacy; }
	bool IsActive() const;
	EEpicodeSandboxSessionState GetState() const;
	const FEpicodeSandboxSession& GetSession() const { return Session; }
	FEpicodeSandboxSession& GetMutableSession() { return Session; }
	const FEpicodeSandboxWorld& GetWorld() const { return Session.GetWorld(); }
	FEpicodeSandboxWorld& GetMutableWorld() { return Session.GetMutableWorld(); }
	FString GetDigest() const;
	int64 GetEventSequence() const;
	int64 GetLastProjectedSequence() const { return LastProjectedSequence; }

private:
	bool FinishCreate(const FEpicodeSandboxSessionConfig& InConfig, FString& OutError);

	FEpicodeSandboxSession Session;
	FEpicodeSandboxSessionConfig Config;
	TSet<FString> ProjectedEntityIds;
	int64 LastProjectedSequence = 0;
	bool bLegacy = false;
	bool bCreated = false;
};
