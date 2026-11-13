#pragma once

#include "CoreMinimal.h"
#include "EpicodeNetworkTypes.h"

class EPICODENETWORK_API FEpicodeNetworkSession
{
public:
	bool Create(const FString& InSceneId, const FString& InOwnerPlayerId, const FString& InOwnerDisplayName, int32 InMaxPlayers, double InReconnectGraceSeconds, FString& OutError, int32 InMaxViewers = 4);
	bool Start(FString& OutError);
	bool Join(const FString& InPlayerId, const FString& InDisplayName, EEpicodeSessionRole InRole, FString& OutPlayerId, FString& OutError, EEpicodeParticipantKind InParticipantKind = EEpicodeParticipantKind::Performer);
	bool Leave(const FString& InPlayerId, FString& OutError, double InNowSeconds = -1.0);
	bool Reconnect(const FString& InPlayerId, FString& OutError, double InNowSeconds = -1.0);
	bool End(FString& OutError);
	bool Kick(const FString& InRequesterId, const FString& InTargetId, FString& OutError);
	bool SetRole(const FString& InRequesterId, const FString& InTargetId, EEpicodeSessionRole InRole, FString& OutError);
	bool AssignCharacter(const FString& InRequesterId, const FString& InTargetPlayerId, const FString& InCharacterId, FString& OutError);
	bool ReleaseCharacter(const FString& InRequesterId, const FString& InTargetPlayerId, FString& OutError);
	bool TransferCharacter(const FString& InRequesterId, const FString& InFromPlayerId, const FString& InToPlayerId, const FString& InCharacterId, FString& OutError);
	bool AcquireLease(const FString& InPlayerId, const FString& InCharacterId, double InLeaseSeconds, FString& OutLeaseId, double& OutExpiresAt, FString& OutError);
	bool HeartbeatLease(const FString& InPlayerId, FString& OutLeaseId, double& OutExpiresAt, FString& OutError);
	bool ReleaseLease(const FString& InPlayerId, FString& OutError);
	bool RecordPlayerTransform(const FString& InPlayerId, const FVector& InLocation, const FRotator& InRotation, FString& OutError);

	void Tick(double InNowSeconds);

	bool CanPerform(const FString& InPlayerId, EEpicodeSessionAction InAction, FString& OutError) const;
	bool CanControlCharacter(const FString& InPlayerId, const FString& InCharacterId, FString& OutError) const;
	bool OwnsCharacter(const FString& InPlayerId, const FString& InCharacterId) const;

	const FEpicodeSessionPlayer* FindPlayer(const FString& InPlayerId) const;
	FEpicodeSessionPlayer* FindPlayer(const FString& InPlayerId);
	const FString& GetSessionId() const { return SessionId; }
	const FString& GetSceneId() const { return SceneId; }
	const FString& GetOwnerPlayerId() const { return OwnerPlayerId; }
	EEpicodeSessionState GetState() const { return State; }
	int32 GetSceneVersion() const { return SceneVersion; }
	int32 GetMaxPlayers() const { return MaxPlayers; }
	int32 GetMaxViewers() const { return MaxViewers; }
	int32 GetConnectedPerformerCount() const;
	int32 GetConnectedViewerCount() const;
	double GetReconnectGraceSeconds() const { return ReconnectGraceSeconds; }
	uint64 GetWorldVersion() const { return WorldVersion; }
	int64 GetEventSequence() const { return EventSequence; }
	const TArray<FEpicodeSessionPlayer>& GetPlayers() const { return Players; }
	const TArray<FEpicodeNetworkEvent>& GetEvents() const { return Events; }

	void SetSceneVersion(int32 InVersion) { SceneVersion = InVersion; }
	void BumpWorldVersion() { ++WorldVersion; }

	bool PublishEvent(const FString& InType, const FString& InPlayerId, const FString& InEntityId = FString(), const TSharedPtr<FJsonObject>& InPayload = nullptr);
	TArray<FEpicodeNetworkEvent> GetEventsSince(int64 InSince, int32 InMaxEvents, bool& bOutNeedsFullSnapshot) const;

	TSharedPtr<FJsonObject> ToJson() const;
	bool Export(const FString& InDirectory, FString& OutSessionPath, FString& OutEventPath, FString& OutError) const;

private:
	bool ValidatePlayer(const FString& InPlayerId, FEpicodeSessionPlayer*& OutPlayer, FString& OutError) const;
	bool IsCharacterOwnedByOther(const FString& InCharacterId, const FString& InPlayerId) const;
	int32 CountConnectedPlayers() const;
	int32 CountConnectedPerformers() const;
	int32 CountConnectedViewers() const;

	FString SessionId;
	FString SceneId;
	FString OwnerPlayerId;
	EEpicodeSessionState State = EEpicodeSessionState::Ended;
	int32 SceneVersion = 0;
	int32 MaxPlayers = 8;
	int32 MaxViewers = 4;
	double ReconnectGraceSeconds = 60.0;
	double CreatedAt = 0.0;
	uint64 WorldVersion = 0;
	int64 EventSequence = 0;
	TArray<FEpicodeSessionPlayer> Players;
	TArray<FEpicodeNetworkEvent> Events;
};
