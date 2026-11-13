#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "EpicodeNetworkTypes.generated.h"

UENUM(BlueprintType)
enum class EEpicodeSessionState : uint8
{
	Created,
	Starting,
	Active,
	Ending,
	Ended
};

UENUM(BlueprintType)
enum class EEpicodeSessionRole : uint8
{
	Owner,
	Director,
	Performer,
	Observer
};

UENUM(BlueprintType)
enum class EEpicodeParticipantKind : uint8
{
	Performer,
	Agent,
	Viewer
};

enum class EEpicodeSessionAction : uint8
{
	ManageSession,
	QuerySession,
	ControlOwnCharacter,
	ControlScene,
	WorldInteract,
	ViewPrivateInventory
};

EPICODENETWORK_API FString LexToString(EEpicodeSessionState State);
EPICODENETWORK_API FString LexToString(EEpicodeSessionRole Role);
EPICODENETWORK_API FString LexToString(EEpicodeParticipantKind Kind);
EPICODENETWORK_API bool ParseSessionState(const FString& Value, EEpicodeSessionState& OutState);
EPICODENETWORK_API bool ParseSessionRole(const FString& Value, EEpicodeSessionRole& OutRole);
EPICODENETWORK_API bool ParseParticipantKind(const FString& Value, EEpicodeParticipantKind& OutKind);

struct EPICODENETWORK_API FEpicodeSessionPlayer
{
	FString PlayerId;
	FString DisplayName;
	EEpicodeSessionRole Role = EEpicodeSessionRole::Performer;
	EEpicodeParticipantKind ParticipantKind = EEpicodeParticipantKind::Performer;
	bool bConnected = false;
	double DisconnectedAt = 0.0;
	FString CharacterId;
	FString LeaseId;
	double LeaseExpiresAt = 0.0;
	double LeaseSeconds = 0.0;
	uint64 StateVersion = 0;
	FString ViewerCameraMode = TEXT("free");
	FString ViewerCameraId;
	bool bHasRuntimeTransform = false;
	FVector RuntimeLocation = FVector::ZeroVector;
	FRotator RuntimeRotation = FRotator::ZeroRotator;

	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeSessionPlayer& OutPlayer, FString& OutError);
};

struct EPICODENETWORK_API FEpicodeNetworkEvent
{
	int64 Sequence = 0;
	double Timestamp = 0.0;
	FString Type;
	FString PlayerId;
	FString EntityId;
	TSharedPtr<FJsonObject> Payload;

	TSharedPtr<FJsonObject> ToJson() const;
};

struct EPICODENETWORK_API FEpicodeSessionPolicy
{
	static bool CanPerform(EEpicodeSessionRole Role, EEpicodeSessionAction Action);
	static bool CanControlCharacter(EEpicodeSessionRole Role);
};
