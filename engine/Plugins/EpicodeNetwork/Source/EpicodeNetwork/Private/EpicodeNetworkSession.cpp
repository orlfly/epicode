#include "EpicodeNetworkSession.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	constexpr int32 MaxRetainedEvents = 4096;

	FString MakePlayerId()
	{
		return FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	}

	TSharedPtr<FJsonObject> MakePayload(const FString& Key, const FString& Value)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(Key, Value);
		return Payload;
	}
}

int32 FEpicodeNetworkSession::CountConnectedPlayers() const
{
	int32 Count = 0;
	for (const FEpicodeSessionPlayer& Player : Players)
	{
		if (Player.bConnected)
		{
			++Count;
		}
	}
	return Count;
}

int32 FEpicodeNetworkSession::CountConnectedPerformers() const
{
	int32 Count = 0;
	for (const FEpicodeSessionPlayer& Player : Players)
	{
		if (Player.bConnected && Player.ParticipantKind != EEpicodeParticipantKind::Viewer)
		{
			++Count;
		}
	}
	return Count;
}

int32 FEpicodeNetworkSession::CountConnectedViewers() const
{
	int32 Count = 0;
	for (const FEpicodeSessionPlayer& Player : Players)
	{
		if (Player.bConnected && Player.ParticipantKind == EEpicodeParticipantKind::Viewer)
		{
			++Count;
		}
	}
	return Count;
}

int32 FEpicodeNetworkSession::GetConnectedPerformerCount() const
{
	return CountConnectedPerformers();
}

int32 FEpicodeNetworkSession::GetConnectedViewerCount() const
{
	return CountConnectedViewers();
}

const FEpicodeSessionPlayer* FEpicodeNetworkSession::FindPlayer(const FString& InPlayerId) const
{
	for (const FEpicodeSessionPlayer& Player : Players)
	{
		if (Player.PlayerId == InPlayerId)
		{
			return &Player;
		}
	}
	return nullptr;
}

FEpicodeSessionPlayer* FEpicodeNetworkSession::FindPlayer(const FString& InPlayerId)
{
	for (FEpicodeSessionPlayer& Player : Players)
	{
		if (Player.PlayerId == InPlayerId)
		{
			return &Player;
		}
	}
	return nullptr;
}

bool FEpicodeNetworkSession::ValidatePlayer(const FString& InPlayerId, FEpicodeSessionPlayer*& OutPlayer, FString& OutError) const
{
	OutPlayer = nullptr;
	if (InPlayerId.IsEmpty())
	{
		OutError = TEXT("INVALID_PARAMS: playerId is required.");
		return false;
	}
	const FEpicodeSessionPlayer* Found = FindPlayer(InPlayerId);
	if (!Found)
	{
		OutError = FString::Printf(TEXT("PLAYER_NOT_FOUND: player '%s' is not part of the session."), *InPlayerId);
		return false;
	}
	OutPlayer = const_cast<FEpicodeSessionPlayer*>(Found);
	return true;
}

bool FEpicodeNetworkSession::IsCharacterOwnedByOther(const FString& InCharacterId, const FString& InPlayerId) const
{
	if (InCharacterId.IsEmpty())
	{
		return false;
	}
	for (const FEpicodeSessionPlayer& Player : Players)
	{
		if (Player.PlayerId != InPlayerId && Player.CharacterId == InCharacterId)
		{
			return true;
		}
	}
	return false;
}

bool FEpicodeNetworkSession::OwnsCharacter(const FString& InPlayerId, const FString& InCharacterId) const
{
	const FEpicodeSessionPlayer* Player = FindPlayer(InPlayerId);
	return Player && Player->bConnected && Player->CharacterId == InCharacterId && !InCharacterId.IsEmpty();
}

bool FEpicodeNetworkSession::Create(const FString& InSceneId, const FString& InOwnerPlayerId, const FString& InOwnerDisplayName, int32 InMaxPlayers, double InReconnectGraceSeconds, FString& OutError, int32 InMaxViewers)
{
	if (State != EEpicodeSessionState::Ended || !SessionId.IsEmpty())
	{
		OutError = TEXT("SESSION_ALREADY_EXISTS: end the current session before creating another.");
		return false;
	}
	if (InSceneId.IsEmpty())
	{
		OutError = TEXT("INVALID_PARAMS: sceneId is required.");
		return false;
	}
	SessionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	SceneId = InSceneId;
	OwnerPlayerId = InOwnerPlayerId.IsEmpty() ? MakePlayerId() : InOwnerPlayerId;
	State = EEpicodeSessionState::Created;
	SceneVersion = 0;
	MaxPlayers = FMath::Max(1, InMaxPlayers);
	MaxViewers = FMath::Max(1, InMaxViewers);
	ReconnectGraceSeconds = FMath::Max(0.0, InReconnectGraceSeconds);
	CreatedAt = FPlatformTime::Seconds();
	WorldVersion = 1;
	EventSequence = 0;
	Players.Reset();
	Events.Reset();

	FEpicodeSessionPlayer Owner;
	Owner.PlayerId = OwnerPlayerId;
	Owner.DisplayName = InOwnerDisplayName.IsEmpty() ? TEXT("Owner") : InOwnerDisplayName;
	Owner.Role = EEpicodeSessionRole::Owner;
	Owner.bConnected = true;
	Players.Add(Owner);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("sceneId"), SceneId);
	Payload->SetStringField(TEXT("ownerPlayerId"), OwnerPlayerId);
	PublishEvent(TEXT("session.created"), OwnerPlayerId, FString(), Payload);
	return true;
}

bool FEpicodeNetworkSession::Start(FString& OutError)
{
	if (State == EEpicodeSessionState::Active)
	{
		return true;
	}
	if (State != EEpicodeSessionState::Created)
	{
		OutError = FString::Printf(TEXT("Cannot start a session in state '%s'."), *LexToString(State));
		return false;
	}
	State = EEpicodeSessionState::Active;
	PublishEvent(TEXT("session.started"), OwnerPlayerId);
	return true;
}

bool FEpicodeNetworkSession::Join(const FString& InPlayerId, const FString& InDisplayName, EEpicodeSessionRole InRole, FString& OutPlayerId, FString& OutError, EEpicodeParticipantKind InParticipantKind)
{
	if (State != EEpicodeSessionState::Active)
	{
		OutError = FString::Printf(TEXT("Cannot join a session in state '%s'."), *LexToString(State));
		return false;
	}

	const FString PlayerId = InPlayerId.IsEmpty() ? MakePlayerId() : InPlayerId;
	OutPlayerId = PlayerId;

	if (FEpicodeSessionPlayer* Existing = FindPlayer(PlayerId))
	{
		if (Existing->ParticipantKind != InParticipantKind)
		{
			OutError = FString::Printf(TEXT("PARTICIPANT_KIND_MISMATCH: player '%s' is %s, not %s."),
				*PlayerId, *LexToString(Existing->ParticipantKind), *LexToString(InParticipantKind));
			return false;
		}
		if (Existing->bConnected)
		{
			OutError = FString::Printf(TEXT("PLAYER_ALREADY_CONNECTED: player '%s' is already connected."), *PlayerId);
			return false;
		}
		const double Now = FPlatformTime::Seconds();
		if (ReconnectGraceSeconds <= 0.0 || Now - Existing->DisconnectedAt > ReconnectGraceSeconds)
		{
			Existing->CharacterId.Reset();
			OutError = FString::Printf(TEXT("RECONNECT_EXPIRED: reconnect grace expired for player '%s'."), *PlayerId);
			return false;
		}
		Existing->bConnected = true;
		Existing->DisconnectedAt = 0.0;
		PublishEvent(TEXT("player.reconnected"), PlayerId);
		return true;
	}

	if (InParticipantKind == EEpicodeParticipantKind::Viewer)
	{
		if (CountConnectedViewers() >= MaxViewers)
		{
			OutError = TEXT("VIEWER_SLOTS_FULL: the session has no free viewer slots.");
			return false;
		}
	}
	else if (CountConnectedPerformers() >= MaxPlayers)
	{
		OutError = TEXT("SESSION_FULL: the session has no free player slots.");
		return false;
	}

	FEpicodeSessionPlayer Player;
	Player.PlayerId = PlayerId;
	Player.DisplayName = InDisplayName.IsEmpty() ? PlayerId : InDisplayName;
	Player.ParticipantKind = InParticipantKind;
	Player.Role = InParticipantKind == EEpicodeParticipantKind::Viewer
		? EEpicodeSessionRole::Observer
		: (InRole == EEpicodeSessionRole::Owner ? EEpicodeSessionRole::Performer : InRole);
	Player.bConnected = true;
	Players.Add(Player);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("role"), LexToString(Player.Role));
	Payload->SetStringField(TEXT("participantKind"), LexToString(Player.ParticipantKind));
	PublishEvent(TEXT("player.joined"), PlayerId, FString(), Payload);
	return true;
}

bool FEpicodeNetworkSession::Leave(const FString& InPlayerId, FString& OutError, double InNowSeconds)
{
	FEpicodeSessionPlayer* Player = nullptr;
	if (!ValidatePlayer(InPlayerId, Player, OutError))
	{
		return false;
	}
	if (!Player->bConnected)
	{
		OutError = FString::Printf(TEXT("PLAYER_ALREADY_DISCONNECTED: player '%s' is already disconnected."), *InPlayerId);
		return false;
	}
	Player->bConnected = false;
	Player->DisconnectedAt = InNowSeconds >= 0.0 ? InNowSeconds : FPlatformTime::Seconds();
	PublishEvent(TEXT("player.left"), InPlayerId);
	return true;
}

bool FEpicodeNetworkSession::Reconnect(const FString& InPlayerId, FString& OutError, double InNowSeconds)
{
	FEpicodeSessionPlayer* Player = nullptr;
	if (!ValidatePlayer(InPlayerId, Player, OutError))
	{
		return false;
	}
	if (State != EEpicodeSessionState::Active)
	{
		OutError = FString::Printf(TEXT("Cannot reconnect to a session in state '%s'."), *LexToString(State));
		return false;
	}
	if (Player->bConnected)
	{
		return true;
	}
	const double Now = InNowSeconds >= 0.0 ? InNowSeconds : FPlatformTime::Seconds();
	if (ReconnectGraceSeconds <= 0.0 || Now - Player->DisconnectedAt > ReconnectGraceSeconds)
	{
		Player->CharacterId.Reset();
		OutError = TEXT("RECONNECT_EXPIRED: the reconnect grace period has elapsed.");
		return false;
	}
	Player->bConnected = true;
	Player->DisconnectedAt = 0.0;
	PublishEvent(TEXT("player.reconnected"), InPlayerId);
	return true;
}

bool FEpicodeNetworkSession::End(FString& OutError)
{
	if (State == EEpicodeSessionState::Ended)
	{
		OutError = TEXT("SESSION_ALREADY_ENDED: the session has already ended.");
		return false;
	}
	State = EEpicodeSessionState::Ended;
	for (FEpicodeSessionPlayer& Player : Players)
	{
		Player.bConnected = false;
	}
	PublishEvent(TEXT("session.ended"), OwnerPlayerId);
	return true;
}

bool FEpicodeNetworkSession::Kick(const FString& InRequesterId, const FString& InTargetId, FString& OutError)
{
	FEpicodeSessionPlayer* Requester = nullptr;
	if (!ValidatePlayer(InRequesterId, Requester, OutError))
	{
		return false;
	}
	if (Requester->Role != EEpicodeSessionRole::Owner)
	{
		OutError = TEXT("FORBIDDEN: only the session owner can kick players.");
		return false;
	}
	if (InTargetId == OwnerPlayerId)
	{
		OutError = TEXT("The session owner cannot be kicked.");
		return false;
	}
	const int32 Index = Players.IndexOfByPredicate([&InTargetId](const FEpicodeSessionPlayer& Player) { return Player.PlayerId == InTargetId; });
	if (Index == INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("PLAYER_NOT_FOUND: player '%s' is not part of the session."), *InTargetId);
		return false;
	}
	Players.RemoveAt(Index);
	PublishEvent(TEXT("player.kicked"), InRequesterId, FString(), MakePayload(TEXT("targetPlayerId"), InTargetId));
	return true;
}

bool FEpicodeNetworkSession::SetRole(const FString& InRequesterId, const FString& InTargetId, EEpicodeSessionRole InRole, FString& OutError)
{
	FEpicodeSessionPlayer* Requester = nullptr;
	if (!ValidatePlayer(InRequesterId, Requester, OutError))
	{
		return false;
	}
	if (Requester->Role != EEpicodeSessionRole::Owner)
	{
		OutError = TEXT("FORBIDDEN: only the session owner can change roles.");
		return false;
	}
	FEpicodeSessionPlayer* Target = FindPlayer(InTargetId);
	if (!Target)
	{
		OutError = FString::Printf(TEXT("PLAYER_NOT_FOUND: player '%s' is not part of the session."), *InTargetId);
		return false;
	}
	if (Target->Role == EEpicodeSessionRole::Owner || InRole == EEpicodeSessionRole::Owner)
	{
		OutError = TEXT("Ownership cannot be transferred through setRole.");
		return false;
	}
	Target->Role = InRole;
	if (!FEpicodeSessionPolicy::CanControlCharacter(InRole) && !Target->CharacterId.IsEmpty())
	{
		Target->CharacterId.Reset();
	}
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("targetPlayerId"), InTargetId);
	Payload->SetStringField(TEXT("role"), LexToString(InRole));
	PublishEvent(TEXT("player.role_changed"), InRequesterId, FString(), Payload);
	return true;
}

bool FEpicodeNetworkSession::AssignCharacter(const FString& InRequesterId, const FString& InTargetPlayerId, const FString& InCharacterId, FString& OutError)
{
	if (InCharacterId.IsEmpty())
	{
		OutError = TEXT("characterId is required.");
		return false;
	}
	FEpicodeSessionPlayer* Requester = nullptr;
	if (!ValidatePlayer(InRequesterId, Requester, OutError))
	{
		return false;
	}
	FEpicodeSessionPlayer* Target = FindPlayer(InTargetPlayerId);
	if (!Target)
	{
		OutError = FString::Printf(TEXT("PLAYER_NOT_FOUND: player '%s' is not part of the session."), *InTargetPlayerId);
		return false;
	}
	if (Requester->Role != EEpicodeSessionRole::Owner && InRequesterId != InTargetPlayerId)
	{
		OutError = TEXT("FORBIDDEN: only the owner can assign characters to other players.");
		return false;
	}
	if (!Target->bConnected)
	{
		OutError = TEXT("TARGET_DISCONNECTED: characters can only be assigned to connected players.");
		return false;
	}
	if (Target->ParticipantKind == EEpicodeParticipantKind::Viewer)
	{
		OutError = TEXT("VIEWER_READ_ONLY: viewer participants cannot control characters.");
		return false;
	}
	if (!FEpicodeSessionPolicy::CanControlCharacter(Target->Role))
	{
		OutError = TEXT("FORBIDDEN: this player role cannot control a character.");
		return false;
	}
	if (!Target->CharacterId.IsEmpty() && Target->CharacterId != InCharacterId)
	{
		OutError = TEXT("PLAYER_ALREADY_HAS_CHARACTER: release the current character first.");
		return false;
	}
	if (IsCharacterOwnedByOther(InCharacterId, InTargetPlayerId))
	{
		OutError = TEXT("CHARACTER_ALREADY_OWNED: another player controls this character.");
		return false;
	}
	Target->CharacterId = InCharacterId;
	BumpWorldVersion();
	PublishEvent(TEXT("character.assigned"), InRequesterId, InCharacterId, MakePayload(TEXT("playerId"), InTargetPlayerId));
	return true;
}

bool FEpicodeNetworkSession::ReleaseCharacter(const FString& InRequesterId, const FString& InTargetPlayerId, FString& OutError)
{
	FEpicodeSessionPlayer* Requester = nullptr;
	if (!ValidatePlayer(InRequesterId, Requester, OutError))
	{
		return false;
	}
	if (Requester->Role != EEpicodeSessionRole::Owner && InRequesterId != InTargetPlayerId)
	{
		OutError = TEXT("FORBIDDEN: only the owner can release another player's character.");
		return false;
	}
	FEpicodeSessionPlayer* Target = FindPlayer(InTargetPlayerId);
	if (!Target)
	{
		OutError = FString::Printf(TEXT("PLAYER_NOT_FOUND: player '%s' is not part of the session."), *InTargetPlayerId);
		return false;
	}
	if (Target->CharacterId.IsEmpty())
	{
		return true;
	}
	const FString CharacterId = Target->CharacterId;
	Target->CharacterId.Reset();
	Target->LeaseId.Reset();
	Target->LeaseExpiresAt = 0.0;
	Target->LeaseSeconds = 0.0;
	++Target->StateVersion;
	BumpWorldVersion();
	PublishEvent(TEXT("character.released"), InRequesterId, CharacterId, MakePayload(TEXT("playerId"), InTargetPlayerId));
	return true;
}

bool FEpicodeNetworkSession::TransferCharacter(const FString& InRequesterId, const FString& InFromPlayerId, const FString& InToPlayerId, const FString& InCharacterId, FString& OutError)
{
	FEpicodeSessionPlayer* Requester = nullptr;
	if (!ValidatePlayer(InRequesterId, Requester, OutError))
	{
		return false;
	}
	if (Requester->Role != EEpicodeSessionRole::Owner)
	{
		OutError = TEXT("FORBIDDEN: only the owner can transfer character ownership.");
		return false;
	}
	FEpicodeSessionPlayer* From = FindPlayer(InFromPlayerId);
	FEpicodeSessionPlayer* To = FindPlayer(InToPlayerId);
	if (!From || !To)
	{
		OutError = TEXT("CHARACTER_TRANSFER_TARGET_MISSING: both players must be part of the session.");
		return false;
	}
	if (From->CharacterId != InCharacterId || InCharacterId.IsEmpty())
	{
		OutError = TEXT("NOT_CHARACTER_OWNER: the source player does not control this character.");
		return false;
	}
	if (!To->bConnected)
	{
		OutError = TEXT("TARGET_DISCONNECTED: characters can only be transferred to connected players.");
		return false;
	}
	if (!FEpicodeSessionPolicy::CanControlCharacter(To->Role))
	{
		OutError = TEXT("FORBIDDEN: the target role cannot control a character.");
		return false;
	}
	if (!To->CharacterId.IsEmpty())
	{
		OutError = TEXT("PLAYER_ALREADY_HAS_CHARACTER: release the target character first.");
		return false;
	}
	From->CharacterId.Reset();
	From->LeaseId.Reset();
	From->LeaseExpiresAt = 0.0;
	From->LeaseSeconds = 0.0;
	++From->StateVersion;
	To->CharacterId = InCharacterId;
	BumpWorldVersion();
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("fromPlayerId"), InFromPlayerId);
	Payload->SetStringField(TEXT("toPlayerId"), InToPlayerId);
	PublishEvent(TEXT("character.transferred"), InRequesterId, InCharacterId, Payload);
	return true;
}

bool FEpicodeNetworkSession::AcquireLease(const FString& InPlayerId, const FString& InCharacterId, double InLeaseSeconds, FString& OutLeaseId, double& OutExpiresAt, FString& OutError)
{
	FEpicodeSessionPlayer* Player = FindPlayer(InPlayerId);
	if (!Player)
	{
		OutError = FString::Printf(TEXT("PLAYER_NOT_FOUND: player '%s' is not part of the session."), *InPlayerId);
		return false;
	}
	if (Player->ParticipantKind != EEpicodeParticipantKind::Agent)
	{
		OutError = TEXT("NOT_AGENT: only agent participants can acquire leases.");
		return false;
	}
	if (!Player->bConnected)
	{
		OutError = TEXT("PLAYER_DISCONNECTED: reconnect before acquiring a lease.");
		return false;
	}
	if (InCharacterId.IsEmpty())
	{
		OutError = TEXT("CHARACTER_NOT_ASSIGNED: characterId is required.");
		return false;
	}
	if (Player->CharacterId != InCharacterId)
	{
		if (!Player->CharacterId.IsEmpty())
		{
			OutError = TEXT("PLAYER_ALREADY_HAS_CHARACTER: release the current character first.");
			return false;
		}
		if (!AssignCharacter(InPlayerId, InPlayerId, InCharacterId, OutError))
		{
			return false;
		}
	}
	const double Now = FPlatformTime::Seconds();
	const double Duration = FMath::Max(1.0, InLeaseSeconds);
	Player = FindPlayer(InPlayerId);
	Player->LeaseId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	Player->LeaseSeconds = Duration;
	Player->LeaseExpiresAt = Now + Duration;
	++Player->StateVersion;
	BumpWorldVersion();
	OutLeaseId = Player->LeaseId;
	OutExpiresAt = Player->LeaseExpiresAt;
	PublishEvent(TEXT("agent.lease.acquired"), InPlayerId, InCharacterId, MakePayload(TEXT("leaseId"), OutLeaseId));
	return true;
}

bool FEpicodeNetworkSession::HeartbeatLease(const FString& InPlayerId, FString& OutLeaseId, double& OutExpiresAt, FString& OutError)
{
	FEpicodeSessionPlayer* Player = FindPlayer(InPlayerId);
	if (!Player || Player->ParticipantKind != EEpicodeParticipantKind::Agent)
	{
		OutError = TEXT("NOT_AGENT: no agent lease for this player.");
		return false;
	}
	if (Player->LeaseId.IsEmpty() || Player->LeaseExpiresAt <= FPlatformTime::Seconds())
	{
		OutError = TEXT("LEASE_EXPIRED: acquire a character lease again.");
		return false;
	}
	Player->LeaseExpiresAt = FPlatformTime::Seconds() + FMath::Max(1.0, Player->LeaseSeconds);
	++Player->StateVersion;
	OutLeaseId = Player->LeaseId;
	OutExpiresAt = Player->LeaseExpiresAt;
	return true;
}

bool FEpicodeNetworkSession::ReleaseLease(const FString& InPlayerId, FString& OutError)
{
	FEpicodeSessionPlayer* Player = FindPlayer(InPlayerId);
	if (!Player)
	{
		OutError = FString::Printf(TEXT("PLAYER_NOT_FOUND: player '%s' is not part of the session."), *InPlayerId);
		return false;
	}
	const FString CharacterId = Player->CharacterId;
	if (!CharacterId.IsEmpty())
	{
		return ReleaseCharacter(InPlayerId, InPlayerId, OutError);
	}
	Player->LeaseId.Reset();
	Player->LeaseExpiresAt = 0.0;
	Player->LeaseSeconds = 0.0;
	++Player->StateVersion;
	return true;
}

bool FEpicodeNetworkSession::RecordPlayerTransform(const FString& InPlayerId, const FVector& InLocation, const FRotator& InRotation, FString& OutError)
{
	FEpicodeSessionPlayer* Player = FindPlayer(InPlayerId);
	if (!Player)
	{
		OutError = FString::Printf(TEXT("PLAYER_NOT_FOUND: player '%s' is not part of the session."), *InPlayerId);
		return false;
	}
	if (InLocation.ContainsNaN() || InRotation.ContainsNaN())
	{
		OutError = TEXT("INVALID_PARAMS: runtime transform contains NaN.");
		return false;
	}
	Player->bHasRuntimeTransform = true;
	Player->RuntimeLocation = InLocation;
	Player->RuntimeRotation = InRotation;
	return true;
}

void FEpicodeNetworkSession::Tick(double InNowSeconds)
{
	if (ReconnectGraceSeconds < 0.0)
	{
		return;
	}
	for (FEpicodeSessionPlayer& Player : Players)
	{
		if (!Player.LeaseId.IsEmpty() && Player.LeaseExpiresAt > 0.0 && InNowSeconds >= Player.LeaseExpiresAt)
		{
			const FString CharacterId = Player.CharacterId;
			Player.CharacterId.Reset();
			Player.LeaseId.Reset();
			Player.LeaseExpiresAt = 0.0;
			Player.LeaseSeconds = 0.0;
			++Player.StateVersion;
			BumpWorldVersion();
			PublishEvent(TEXT("agent.lease.expired"), Player.PlayerId, CharacterId, MakePayload(TEXT("playerId"), Player.PlayerId));
			continue;
		}
		if (Player.bConnected || Player.CharacterId.IsEmpty())
		{
			continue;
		}
		if (InNowSeconds - Player.DisconnectedAt <= ReconnectGraceSeconds)
		{
			continue;
		}
		const FString CharacterId = Player.CharacterId;
		Player.CharacterId.Reset();
		PublishEvent(TEXT("character.released"), FString(), CharacterId, MakePayload(TEXT("playerId"), Player.PlayerId));
	}
}

bool FEpicodeNetworkSession::CanPerform(const FString& InPlayerId, EEpicodeSessionAction InAction, FString& OutError) const
{
	const FEpicodeSessionPlayer* Player = FindPlayer(InPlayerId);
	if (!Player)
	{
		OutError = FString::Printf(TEXT("PLAYER_NOT_FOUND: player '%s' is not part of the session."), *InPlayerId);
		return false;
	}
	if (!Player->bConnected)
	{
		OutError = TEXT("PLAYER_DISCONNECTED: reconnect before issuing commands.");
		return false;
	}
	if (Player->ParticipantKind == EEpicodeParticipantKind::Viewer && InAction != EEpicodeSessionAction::QuerySession)
	{
		OutError = TEXT("VIEWER_READ_ONLY: viewer participants cannot modify the session or world.");
		return false;
	}
	if (!FEpicodeSessionPolicy::CanPerform(Player->Role, InAction))
	{
		OutError = FString::Printf(TEXT("FORBIDDEN: role '%s' cannot perform this action."), *LexToString(Player->Role));
		return false;
	}
	return true;
}

bool FEpicodeNetworkSession::CanControlCharacter(const FString& InPlayerId, const FString& InCharacterId, FString& OutError) const
{
	const FEpicodeSessionPlayer* Player = FindPlayer(InPlayerId);
	if (!Player)
	{
		OutError = FString::Printf(TEXT("PLAYER_NOT_FOUND: player '%s' is not part of the session."), *InPlayerId);
		return false;
	}
	if (Player->ParticipantKind == EEpicodeParticipantKind::Viewer)
	{
		OutError = TEXT("VIEWER_READ_ONLY: viewer participants cannot control characters.");
		return false;
	}
	if (!Player->bConnected)
	{
		OutError = TEXT("PLAYER_DISCONNECTED: reconnect before controlling a character.");
		return false;
	}
	if (!FEpicodeSessionPolicy::CanControlCharacter(Player->Role))
	{
		OutError = FString::Printf(TEXT("FORBIDDEN: role '%s' cannot control a character."), *LexToString(Player->Role));
		return false;
	}
	if (Player->CharacterId.IsEmpty())
	{
		OutError = TEXT("NO_CHARACTER: assign a character before controlling it.");
		return false;
	}
	if (Player->CharacterId != InCharacterId)
	{
		OutError = TEXT("NOT_CHARACTER_OWNER: this player controls a different character.");
		return false;
	}
	return true;
}

bool FEpicodeNetworkSession::PublishEvent(const FString& InType, const FString& InPlayerId, const FString& InEntityId, const TSharedPtr<FJsonObject>& InPayload)
{
	FEpicodeNetworkEvent Event;
	Event.Sequence = ++EventSequence;
	Event.Timestamp = FPlatformTime::Seconds();
	Event.Type = InType;
	Event.PlayerId = InPlayerId;
	Event.EntityId = InEntityId;
	Event.Payload = InPayload;
	Events.Add(Event);
	if (Events.Num() > MaxRetainedEvents)
	{
		Events.RemoveAt(0, Events.Num() - MaxRetainedEvents);
	}
	return true;
}

TArray<FEpicodeNetworkEvent> FEpicodeNetworkSession::GetEventsSince(int64 InSince, int32 InMaxEvents, bool& bOutNeedsFullSnapshot) const
{
	bOutNeedsFullSnapshot = false;
	if (Events.Num() > 0 && InSince < Events[0].Sequence - 1)
	{
		bOutNeedsFullSnapshot = true;
	}
	const int32 Limit = InMaxEvents <= 0 ? 128 : InMaxEvents;
	TArray<FEpicodeNetworkEvent> Result;
	for (const FEpicodeNetworkEvent& Event : Events)
	{
		if (Event.Sequence <= InSince)
		{
			continue;
		}
		Result.Add(Event);
		if (Result.Num() >= Limit)
		{
			break;
		}
	}
	return Result;
}

TSharedPtr<FJsonObject> FEpicodeNetworkSession::ToJson() const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("sessionId"), SessionId);
	Json->SetStringField(TEXT("state"), LexToString(State));
	Json->SetStringField(TEXT("sceneId"), SceneId);
	Json->SetNumberField(TEXT("sceneVersion"), SceneVersion);
	Json->SetStringField(TEXT("ownerPlayerId"), OwnerPlayerId);
	Json->SetNumberField(TEXT("maxPlayers"), MaxPlayers);
	Json->SetNumberField(TEXT("maxViewers"), MaxViewers);
	Json->SetNumberField(TEXT("performerCount"), CountConnectedPerformers());
	Json->SetNumberField(TEXT("viewerCount"), CountConnectedViewers());
	Json->SetNumberField(TEXT("reconnectGraceSeconds"), ReconnectGraceSeconds);
	Json->SetNumberField(TEXT("createdAt"), CreatedAt);
	Json->SetNumberField(TEXT("worldVersion"), static_cast<double>(WorldVersion));
	Json->SetNumberField(TEXT("eventSequence"), static_cast<double>(EventSequence));

	TArray<TSharedPtr<FJsonValue>> PlayerValues;
	TArray<TSharedPtr<FJsonValue>> CharacterValues;
	TArray<TSharedPtr<FJsonValue>> ViewerCameraValues;
	TArray<TSharedPtr<FJsonValue>> AgentLeaseValues;
	for (const FEpicodeSessionPlayer& Player : Players)
	{
		PlayerValues.Add(MakeShared<FJsonValueObject>(Player.ToJson()));
		if (Player.ParticipantKind == EEpicodeParticipantKind::Viewer)
		{
			if (!Player.bConnected)
			{
				continue;
			}
			TSharedPtr<FJsonObject> Camera = MakeShared<FJsonObject>();
			Camera->SetStringField(TEXT("playerId"), Player.PlayerId);
			Camera->SetStringField(TEXT("mode"), Player.ViewerCameraMode);
			Camera->SetStringField(TEXT("cameraId"), Player.ViewerCameraId);
			ViewerCameraValues.Add(MakeShared<FJsonValueObject>(Camera));
		}
		if (Player.ParticipantKind == EEpicodeParticipantKind::Agent && !Player.LeaseId.IsEmpty())
		{
			TSharedPtr<FJsonObject> Lease = MakeShared<FJsonObject>();
			Lease->SetStringField(TEXT("playerId"), Player.PlayerId);
			Lease->SetStringField(TEXT("characterId"), Player.CharacterId);
			Lease->SetStringField(TEXT("leaseId"), Player.LeaseId);
			Lease->SetNumberField(TEXT("leaseExpiresAt"), Player.LeaseExpiresAt);
			Lease->SetNumberField(TEXT("leaseSeconds"), Player.LeaseSeconds);
			Lease->SetNumberField(TEXT("stateVersion"), static_cast<double>(Player.StateVersion));
			AgentLeaseValues.Add(MakeShared<FJsonValueObject>(Lease));
		}
		if (!Player.CharacterId.IsEmpty())
		{
			TSharedPtr<FJsonObject> Character = MakeShared<FJsonObject>();
			Character->SetStringField(TEXT("entityId"), Player.CharacterId);
			Character->SetStringField(TEXT("playerId"), Player.PlayerId);
			Character->SetStringField(TEXT("replicationClass"), TEXT("high"));
			Character->SetNumberField(TEXT("netUpdateFrequency"), 30.0);
			CharacterValues.Add(MakeShared<FJsonValueObject>(Character));
		}
	}
	Json->SetArrayField(TEXT("players"), PlayerValues);
	Json->SetArrayField(TEXT("viewerCameras"), ViewerCameraValues);
	Json->SetArrayField(TEXT("agentLeases"), AgentLeaseValues);
	Json->SetArrayField(TEXT("characters"), CharacterValues);
	TArray<TSharedPtr<FJsonValue>> EventValues;
	for (const FEpicodeNetworkEvent& Event : Events)
	{
		EventValues.Add(MakeShared<FJsonValueObject>(Event.ToJson()));
	}
	Json->SetArrayField(TEXT("events"), EventValues);
	TArray<TSharedPtr<FJsonValue>> PerformanceValues;
	for (const FEpicodeNetworkEvent& Event : Events)
	{
		if (Event.Type.StartsWith(TEXT("performance.")))
		{
			PerformanceValues.Add(MakeShared<FJsonValueObject>(Event.ToJson()));
		}
	}
	Json->SetArrayField(TEXT("performanceEvents"), PerformanceValues);
	TArray<TSharedPtr<FJsonValue>> WorldValues;
	for (const FEpicodeNetworkEvent& Event : Events)
	{
		if (Event.Type.StartsWith(TEXT("item.")) || Event.Type.StartsWith(TEXT("craft.")) || Event.Type.StartsWith(TEXT("build.")))
		{
			WorldValues.Add(MakeShared<FJsonValueObject>(Event.ToJson()));
		}
	}
	Json->SetArrayField(TEXT("worldEvents"), WorldValues);
	return Json;
}

bool FEpicodeNetworkSession::Export(const FString& InDirectory, FString& OutSessionPath, FString& OutEventPath, FString& OutError) const
{
	if (InDirectory.IsEmpty())
	{
		OutError = TEXT("Export directory is empty.");
		return false;
	}
	if (!IFileManager::Get().MakeDirectory(*InDirectory, true))
	{
		OutError = FString::Printf(TEXT("Failed to create session directory: %s"), *InDirectory);
		return false;
	}

	FString SessionText;
	const TSharedRef<TJsonWriter<TCHAR>> SessionWriter = TJsonWriterFactory<TCHAR>::Create(&SessionText);
	if (!FJsonSerializer::Serialize(ToJson().ToSharedRef(), SessionWriter))
	{
		OutError = TEXT("Failed to serialize session snapshot.");
		return false;
	}
	OutSessionPath = FPaths::Combine(InDirectory, TEXT("session.json"));
	if (!FFileHelper::SaveStringToFile(SessionText, *OutSessionPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("Failed to write session snapshot: %s"), *OutSessionPath);
		return false;
	}

	FString EventLines;
	for (const FEpicodeNetworkEvent& Event : Events)
	{
		FString Line;
		const TSharedRef<TJsonWriter<TCHAR>> EventWriter = TJsonWriterFactory<TCHAR>::Create(&Line);
		if (!FJsonSerializer::Serialize(Event.ToJson().ToSharedRef(), EventWriter))
		{
			OutError = TEXT("Failed to serialize session event.");
			return false;
		}
		EventLines += Line;
		EventLines += TEXT("\n");
	}
	OutEventPath = FPaths::Combine(InDirectory, TEXT("events.jsonl"));
	if (!FFileHelper::SaveStringToFile(EventLines, *OutEventPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("Failed to write session events: %s"), *OutEventPath);
		return false;
	}
	return true;
}
