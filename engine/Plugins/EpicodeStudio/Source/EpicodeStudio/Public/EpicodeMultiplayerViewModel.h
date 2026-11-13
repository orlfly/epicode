#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** Presentation-only multiplayer state derived from session snapshots. */
class EPICODESTUDIO_API FEpicodeMultiplayerViewModel
{
public:
	void SetSnapshot(const TSharedPtr<FJsonObject>& InSnapshot);
	void SetLocalPlayerId(const FString& InPlayerId);
	const FString& GetLocalPlayerId() const { return LocalPlayerId; }

	FString GetSessionState() const { return SessionState; }
	FString GetSceneId() const { return SceneId; }
	FString GetOwnerPlayerId() const { return OwnerPlayerId; }
	FString GetLocalRole() const;
	int32 GetPlayerCount() const { return Players.Num(); }
	int32 GetConnectedCount() const;
	bool HasSession() const { return !SessionState.IsEmpty(); }
	bool IsEnded() const { return SessionState == TEXT("ended"); }
	bool IsObserver() const { return GetLocalRole() == TEXT("observer"); }
	bool CanManage() const { return GetLocalRole() == TEXT("owner"); }
	bool CanAssignCharacters() const { return GetLocalRole() == TEXT("owner") || GetLocalRole() == TEXT("director"); }
	bool CanControlOwnCharacter() const { return GetLocalRole() == TEXT("owner") || GetLocalRole() == TEXT("performer"); }

	const TArray<TSharedPtr<FJsonObject>>& GetPlayers() const { return Players; }
	const TArray<TSharedPtr<FJsonObject>>& GetCharacters() const { return Characters; }
	TArray<FString> GetPlayerRows() const;
	TArray<FString> GetCharacterRows() const;
	TArray<FString> GetDirectorEntries() const;

	FString GetPerformerHudSummary() const;
	FString GetSessionStateText() const;
	FString GetReconnectText() const;

	// Used by the panel's empty state before any snapshot arrives.
	static TSharedPtr<FEpicodeMultiplayerViewModel> MakeEmpty();

private:
	FString LocalPlayerId;
	FString SessionState;
	FString SceneId;
	FString OwnerPlayerId;
	TArray<TSharedPtr<FJsonObject>> Players;
	TArray<TSharedPtr<FJsonObject>> Characters;
	TSharedPtr<FJsonObject> LocalPerformance;
	TArray<FString> DirectorCameras;
	TArray<FString> DirectorLights;
	TSharedPtr<FJsonObject> LastSnapshot;
};
