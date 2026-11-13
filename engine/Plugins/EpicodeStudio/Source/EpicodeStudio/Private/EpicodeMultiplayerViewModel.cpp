#include "EpicodeMultiplayerViewModel.h"

namespace
{
	FString GetString(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		FString Value;
		if (Json.IsValid())
		{
			Json->TryGetStringField(Field, Value);
		}
		return Value;
	}

	TArray<TSharedPtr<FJsonObject>> GetObjectArray(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		TArray<TSharedPtr<FJsonObject>> Result;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Json.IsValid() && Json->TryGetArrayField(Field, Values) && Values)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				if (Value.IsValid() && Value->AsObject().IsValid())
				{
					Result.Add(Value->AsObject());
				}
			}
		}
		return Result;
	}
}

void FEpicodeMultiplayerViewModel::SetSnapshot(const TSharedPtr<FJsonObject>& InSnapshot)
{
	LastSnapshot = InSnapshot;
	SessionState = GetString(InSnapshot, TEXT("state"));
	SceneId = GetString(InSnapshot, TEXT("sceneId"));
	OwnerPlayerId = GetString(InSnapshot, TEXT("ownerPlayerId"));
	Players = GetObjectArray(InSnapshot, TEXT("players"));
	Characters = GetObjectArray(InSnapshot, TEXT("characters"));
	LocalPerformance.Reset();
	DirectorCameras.Reset();
	DirectorLights.Reset();

	const TArray<TSharedPtr<FJsonObject>> PerformanceEvents = GetObjectArray(InSnapshot, TEXT("performanceEvents"));
	for (const TSharedPtr<FJsonObject>& Event : PerformanceEvents)
	{
		if (GetString(Event, TEXT("playerId")) != LocalPlayerId)
		{
			continue;
		}
		const TSharedPtr<FJsonObject>* Payload = nullptr;
		if (Event->TryGetObjectField(TEXT("payload"), Payload) && Payload)
		{
			LocalPerformance = *Payload;
		}
	}

	for (const TSharedPtr<FJsonObject>& Camera : GetObjectArray(InSnapshot, TEXT("cameras")))
	{
		DirectorCameras.Add(GetString(Camera, TEXT("name")));
	}
	for (const TSharedPtr<FJsonObject>& Light : GetObjectArray(InSnapshot, TEXT("lights")))
	{
		DirectorLights.Add(GetString(Light, TEXT("name")));
	}
}

void FEpicodeMultiplayerViewModel::SetLocalPlayerId(const FString& InPlayerId)
{
	if (LocalPlayerId == InPlayerId)
	{
		return;
	}
	LocalPlayerId = InPlayerId;
	LocalPerformance.Reset();
	if (LastSnapshot.IsValid())
	{
		SetSnapshot(LastSnapshot);
	}
}

FString FEpicodeMultiplayerViewModel::GetLocalRole() const
{
	if (LocalPlayerId.IsEmpty())
	{
		return FString();
	}
	for (const TSharedPtr<FJsonObject>& Player : Players)
	{
		if (GetString(Player, TEXT("playerId")) == LocalPlayerId)
		{
			return GetString(Player, TEXT("role"));
		}
	}
	return FString();
}

int32 FEpicodeMultiplayerViewModel::GetConnectedCount() const
{
	int32 Count = 0;
	for (const TSharedPtr<FJsonObject>& Player : Players)
	{
		bool bConnected = false;
		if (Player->TryGetBoolField(TEXT("connected"), bConnected) && bConnected)
		{
			++Count;
		}
	}
	return Count;
}

TArray<FString> FEpicodeMultiplayerViewModel::GetPlayerRows() const
{
	TArray<FString> Rows;
	for (const TSharedPtr<FJsonObject>& Player : Players)
	{
		bool bConnected = false;
		Player->TryGetBoolField(TEXT("connected"), bConnected);
		const FString CharacterId = GetString(Player, TEXT("characterId"));
		Rows.Add(FString::Printf(TEXT("%s [%s]%s%s"),
			*GetString(Player, TEXT("playerId")),
			*GetString(Player, TEXT("role")),
			bConnected ? TEXT("") : TEXT(" (reconnecting)"),
			CharacterId.IsEmpty() ? TEXT(" - no character") : *FString::Printf(TEXT(" - %s"), *CharacterId)));
	}
	return Rows;
}

TArray<FString> FEpicodeMultiplayerViewModel::GetCharacterRows() const
{
	TArray<FString> Rows;
	for (const TSharedPtr<FJsonObject>& Character : Characters)
	{
		Rows.Add(FString::Printf(TEXT("%s -> %s"),
			*GetString(Character, TEXT("entityId")),
			*GetString(Character, TEXT("playerId"))));
	}
	return Rows;
}

TArray<FString> FEpicodeMultiplayerViewModel::GetDirectorEntries() const
{
	TArray<FString> Entries;
	for (const FString& Camera : DirectorCameras)
	{
		Entries.Add(FString::Printf(TEXT("camera: %s"), *Camera));
	}
	for (const FString& Light : DirectorLights)
	{
		Entries.Add(FString::Printf(TEXT("light: %s"), *Light));
	}
	for (const TSharedPtr<FJsonObject>& Player : Players)
	{
		Entries.Add(FString::Printf(TEXT("player: %s (%s)"),
			*GetString(Player, TEXT("playerId")),
			*GetString(Player, TEXT("role"))));
	}
	return Entries;
}

FString FEpicodeMultiplayerViewModel::GetPerformerHudSummary() const
{
	if (GetLocalRole() == TEXT("observer"))
	{
		return TEXT("No performance state received.");
	}
	if (!LocalPerformance.IsValid())
	{
		return LocalPlayerId.IsEmpty() ? TEXT("No local performer selected.") : TEXT("No performance state received.");
	}
	bool bSpeaking = false;
	LocalPerformance->TryGetBoolField(TEXT("speaking"), bSpeaking);
	double Intensity = 0.0;
	LocalPerformance->TryGetNumberField(TEXT("intensity"), Intensity);
	FString Source;
	LocalPerformance->TryGetStringField(TEXT("resolvedSource"), Source);
	if (Source.IsEmpty())
	{
		LocalPerformance->TryGetStringField(TEXT("visemeSource"), Source);
	}
	return FString::Printf(TEXT("Action: %s | Queue: %d | Emotion: %s (%.2f) | Speaking: %s | Viseme: %s"),
		*GetString(LocalPerformance, TEXT("actionTag")),
		LocalPerformance.IsValid() && LocalPerformance->HasField(TEXT("actionQueue")) ? LocalPerformance->GetArrayField(TEXT("actionQueue")).Num() : 0,
		*GetString(LocalPerformance, TEXT("emotion")),
		Intensity,
		bSpeaking ? TEXT("yes") : TEXT("no"),
		Source.IsEmpty() ? TEXT("none") : *Source);
}

FString FEpicodeMultiplayerViewModel::GetSessionStateText() const
{
	if (!HasSession())
	{
		return TEXT("No active session");
	}
	if (IsEnded())
	{
		return TEXT("Session ended");
	}
	if (GetConnectedCount() < Players.Num())
	{
		return TEXT("Reconnecting");
	}
	return FString::Printf(TEXT("Live (%s)"), *SessionState);
}

FString FEpicodeMultiplayerViewModel::GetReconnectText() const
{
	for (const TSharedPtr<FJsonObject>& Player : Players)
	{
		bool bConnected = true;
		Player->TryGetBoolField(TEXT("connected"), bConnected);
		if (!bConnected)
		{
			return FString::Printf(TEXT("%s is reconnecting inside the grace period."), *GetString(Player, TEXT("playerId")));
		}
	}
	return TEXT("All players connected.");
}

TSharedPtr<FEpicodeMultiplayerViewModel> FEpicodeMultiplayerViewModel::MakeEmpty()
{
	return MakeShared<FEpicodeMultiplayerViewModel>();
}
