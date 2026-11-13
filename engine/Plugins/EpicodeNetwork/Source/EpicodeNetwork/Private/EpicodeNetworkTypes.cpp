#include "EpicodeNetworkTypes.h"

FString LexToString(EEpicodeSessionState State)
{
	switch (State)
	{
	case EEpicodeSessionState::Created:
		return TEXT("created");
	case EEpicodeSessionState::Starting:
		return TEXT("starting");
	case EEpicodeSessionState::Active:
		return TEXT("active");
	case EEpicodeSessionState::Ending:
		return TEXT("ending");
	case EEpicodeSessionState::Ended:
		return TEXT("ended");
	default:
		return TEXT("unknown");
	}
}

FString LexToString(EEpicodeSessionRole Role)
{
	switch (Role)
	{
	case EEpicodeSessionRole::Owner:
		return TEXT("owner");
	case EEpicodeSessionRole::Director:
		return TEXT("director");
	case EEpicodeSessionRole::Performer:
		return TEXT("performer");
	case EEpicodeSessionRole::Observer:
		return TEXT("observer");
	default:
		return TEXT("unknown");
	}
}

FString LexToString(EEpicodeParticipantKind Kind)
{
	switch (Kind)
	{
	case EEpicodeParticipantKind::Performer:
		return TEXT("performer");
	case EEpicodeParticipantKind::Agent:
		return TEXT("agent");
	case EEpicodeParticipantKind::Viewer:
		return TEXT("viewer");
	default:
		return TEXT("unknown");
	}
}

bool ParseSessionState(const FString& Value, EEpicodeSessionState& OutState)
{
	const FString Lower = Value.ToLower();
	if (Lower == TEXT("created"))
	{
		OutState = EEpicodeSessionState::Created;
		return true;
	}
	if (Lower == TEXT("starting"))
	{
		OutState = EEpicodeSessionState::Starting;
		return true;
	}
	if (Lower == TEXT("active"))
	{
		OutState = EEpicodeSessionState::Active;
		return true;
	}
	if (Lower == TEXT("ending"))
	{
		OutState = EEpicodeSessionState::Ending;
		return true;
	}
	if (Lower == TEXT("ended"))
	{
		OutState = EEpicodeSessionState::Ended;
		return true;
	}
	return false;
}

bool ParseSessionRole(const FString& Value, EEpicodeSessionRole& OutRole)
{
	const FString Lower = Value.ToLower();
	if (Lower == TEXT("owner"))
	{
		OutRole = EEpicodeSessionRole::Owner;
		return true;
	}
	if (Lower == TEXT("director"))
	{
		OutRole = EEpicodeSessionRole::Director;
		return true;
	}
	if (Lower == TEXT("performer"))
	{
		OutRole = EEpicodeSessionRole::Performer;
		return true;
	}
	if (Lower == TEXT("observer"))
	{
		OutRole = EEpicodeSessionRole::Observer;
		return true;
	}
	return false;
}

bool ParseParticipantKind(const FString& Value, EEpicodeParticipantKind& OutKind)
{
	const FString Lower = Value.ToLower();
	if (Lower == TEXT("performer") || Lower == TEXT("game"))
	{
		OutKind = EEpicodeParticipantKind::Performer;
		return true;
	}
	if (Lower == TEXT("agent"))
	{
		OutKind = EEpicodeParticipantKind::Agent;
		return true;
	}
	if (Lower == TEXT("viewer") || Lower == TEXT("observer"))
	{
		OutKind = EEpicodeParticipantKind::Viewer;
		return true;
	}
	return false;
}

TSharedPtr<FJsonObject> FEpicodeSessionPlayer::ToJson() const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("playerId"), PlayerId);
	Json->SetStringField(TEXT("displayName"), DisplayName);
	Json->SetStringField(TEXT("role"), LexToString(Role));
	Json->SetStringField(TEXT("participantKind"), LexToString(ParticipantKind));
	Json->SetBoolField(TEXT("connected"), bConnected);
	Json->SetStringField(TEXT("characterId"), CharacterId);
	Json->SetStringField(TEXT("leaseId"), LeaseId);
	if (LeaseExpiresAt > 0.0)
	{
		Json->SetNumberField(TEXT("leaseExpiresAt"), LeaseExpiresAt);
	}
	Json->SetNumberField(TEXT("leaseSeconds"), LeaseSeconds);
	Json->SetNumberField(TEXT("stateVersion"), static_cast<double>(StateVersion));
	if (ParticipantKind == EEpicodeParticipantKind::Viewer)
	{
		Json->SetStringField(TEXT("viewerCameraMode"), ViewerCameraMode);
		Json->SetStringField(TEXT("viewerCameraId"), ViewerCameraId);
	}
	Json->SetBoolField(TEXT("hasRuntimeTransform"), bHasRuntimeTransform);
	if (bHasRuntimeTransform)
	{
		Json->SetStringField(TEXT("runtimeLocation"), RuntimeLocation.ToString());
		Json->SetStringField(TEXT("runtimeRotation"), RuntimeRotation.ToString());
	}
	if (!bConnected)
	{
		Json->SetNumberField(TEXT("disconnectedAt"), DisconnectedAt);
	}
	return Json;
}

bool FEpicodeSessionPlayer::FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeSessionPlayer& OutPlayer, FString& OutError)
{
	if (!InJson.IsValid())
	{
		OutError = TEXT("Player JSON is invalid.");
		return false;
	}
	if (!InJson->TryGetStringField(TEXT("playerId"), OutPlayer.PlayerId) || OutPlayer.PlayerId.IsEmpty())
	{
		OutError = TEXT("Player is missing playerId.");
		return false;
	}
	InJson->TryGetStringField(TEXT("displayName"), OutPlayer.DisplayName);
	InJson->TryGetBoolField(TEXT("connected"), OutPlayer.bConnected);
	InJson->TryGetStringField(TEXT("characterId"), OutPlayer.CharacterId);
	InJson->TryGetNumberField(TEXT("disconnectedAt"), OutPlayer.DisconnectedAt);
	InJson->TryGetStringField(TEXT("leaseId"), OutPlayer.LeaseId);
	InJson->TryGetNumberField(TEXT("leaseExpiresAt"), OutPlayer.LeaseExpiresAt);
	InJson->TryGetNumberField(TEXT("leaseSeconds"), OutPlayer.LeaseSeconds);
	double StateVersion = 0.0;
	InJson->TryGetNumberField(TEXT("stateVersion"), StateVersion);
	OutPlayer.StateVersion = static_cast<uint64>(StateVersion);
	InJson->TryGetStringField(TEXT("viewerCameraMode"), OutPlayer.ViewerCameraMode);
	InJson->TryGetStringField(TEXT("viewerCameraId"), OutPlayer.ViewerCameraId);
	InJson->TryGetBoolField(TEXT("hasRuntimeTransform"), OutPlayer.bHasRuntimeTransform);
	FString LocationText;
	if (InJson->TryGetStringField(TEXT("runtimeLocation"), LocationText))
	{
		OutPlayer.RuntimeLocation.InitFromString(LocationText);
	}
	FString RotationText;
	if (InJson->TryGetStringField(TEXT("runtimeRotation"), RotationText))
	{
		OutPlayer.RuntimeRotation.InitFromString(RotationText);
	}
	FString RoleText;
	if (InJson->TryGetStringField(TEXT("role"), RoleText) && !ParseSessionRole(RoleText, OutPlayer.Role))
	{
		OutError = FString::Printf(TEXT("Unknown session role: %s"), *RoleText);
		return false;
	}
	FString KindText;
	if (InJson->TryGetStringField(TEXT("participantKind"), KindText) && !ParseParticipantKind(KindText, OutPlayer.ParticipantKind))
	{
		OutError = FString::Printf(TEXT("Unknown participant kind: %s"), *KindText);
		return false;
	}
	InJson->TryGetStringField(TEXT("leaseId"), OutPlayer.LeaseId);
	InJson->TryGetNumberField(TEXT("leaseExpiresAt"), OutPlayer.LeaseExpiresAt);
	InJson->TryGetNumberField(TEXT("leaseSeconds"), OutPlayer.LeaseSeconds);
	double StateVersionNumber = 0.0;
	InJson->TryGetNumberField(TEXT("stateVersion"), StateVersionNumber);
	OutPlayer.StateVersion = static_cast<uint64>(FMath::Max(0.0, StateVersionNumber));
	return true;
}

TSharedPtr<FJsonObject> FEpicodeNetworkEvent::ToJson() const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetNumberField(TEXT("sequence"), static_cast<double>(Sequence));
	Json->SetNumberField(TEXT("timestamp"), Timestamp);
	Json->SetStringField(TEXT("type"), Type);
	Json->SetStringField(TEXT("playerId"), PlayerId);
	Json->SetStringField(TEXT("entityId"), EntityId);
	if (Payload.IsValid())
	{
		Json->SetObjectField(TEXT("payload"), Payload);
	}
	return Json;
}

bool FEpicodeSessionPolicy::CanPerform(EEpicodeSessionRole Role, EEpicodeSessionAction Action)
{
	switch (Role)
	{
	case EEpicodeSessionRole::Owner:
		return Action == EEpicodeSessionAction::ManageSession
			|| Action == EEpicodeSessionAction::QuerySession
			|| Action == EEpicodeSessionAction::ControlOwnCharacter
			|| Action == EEpicodeSessionAction::ControlScene
			|| Action == EEpicodeSessionAction::WorldInteract
			|| Action == EEpicodeSessionAction::ViewPrivateInventory;
	case EEpicodeSessionRole::Director:
		return Action == EEpicodeSessionAction::QuerySession
			|| Action == EEpicodeSessionAction::ControlScene;
	case EEpicodeSessionRole::Performer:
		return Action == EEpicodeSessionAction::QuerySession
			|| Action == EEpicodeSessionAction::ControlOwnCharacter
			|| Action == EEpicodeSessionAction::WorldInteract
			|| Action == EEpicodeSessionAction::ViewPrivateInventory;
	case EEpicodeSessionRole::Observer:
		return Action == EEpicodeSessionAction::QuerySession;
	default:
		return false;
	}
}

bool FEpicodeSessionPolicy::CanControlCharacter(EEpicodeSessionRole Role)
{
	return Role == EEpicodeSessionRole::Owner || Role == EEpicodeSessionRole::Performer;
}
