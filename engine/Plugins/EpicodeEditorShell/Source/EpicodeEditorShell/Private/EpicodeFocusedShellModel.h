#pragma once

#include "CoreMinimal.h"
#include "EpicodeCommand.h"
#include "EpicodeLocalLibrary.h"

class UEpicodeSceneSubsystem;

struct FEpicodeFocusedEntityRow
{
	FGuid Id;
	FString Name;
	FString Type;
	FString TypeLabel;
	bool bVisible = true;
};

struct FEpicodeFocusedSceneRow
{
	FString Id;
	FString Name;
	bool bCurrent = false;
	bool bAvailable = true;
	TArray<FString> Tags;
};

struct FEpicodeFocusedAssetRow
{
	FString Name;
	FString AssetPath;
	FString Type;
	bool bAvailable = true;
	FString Error;
	TArray<FString> Tags;
};

/**
 * Small command-backed model for the focused shell. It intentionally uses the
 * same FEpicodeCommandRegistry as Studio and the control API so the shell does
 * not invent a second editing path.
 */
class FEpicodeFocusedShellModel
{
public:
	void Refresh();
	void RefreshAssets();
	void HideAsset(const FString& AssetPath);
	void RestoreHiddenAssets();
	FEpicodeCommandResult Execute(const FString& Method, const TSharedPtr<FJsonObject>& Params = nullptr);
	FEpicodeCommandResult QueryCommand(
		const FString& Method, const TSharedPtr<FJsonObject>& Params = nullptr) const;

	const TArray<FEpicodeFocusedEntityRow>& GetEntities() const { return Entities; }
	const TArray<FEpicodeFocusedSceneRow>& GetScenes() const { return Scenes; }
	const TArray<FEpicodeFocusedAssetRow>& GetAssets() const { return Assets; }
	const FGuid& GetSelectedEntityId() const { return SelectedEntityId; }
	void SetSelectedEntityId(const FGuid& InId) { SelectedEntityId = InId; }

	bool HasScene() const { return bHasScene; }
	bool IsDirty() const { return bDirty; }
	const FString& GetSceneName() const { return SceneName; }
	const FString& GetSceneId() const { return SceneId; }
	const FString& GetLastMessage() const { return LastMessage; }
	void SetLastMessage(const FString& InMessage) { LastMessage = InMessage; }
	uint64 GetRevision() const { return Revision; }
	int32 GetSceneCount() const { return Scenes.Num(); }
	int32 GetEntityCount() const { return Entities.Num(); }
	UEpicodeSceneSubsystem* GetSceneSubsystem() const;

	const FEpicodeFocusedEntityRow* FindEntity(const FGuid& InId) const;
	const FEpicodeEntityRecord* GetSelectedEntityRecord() const;
	const FEpicodeLibraryEntry* FindLibraryScene(const FString& SceneId) const;
	FGuid GetActiveCameraId() const;
	FString GetSelectedEntityLabel() const;
	FString GetSignature() const;
	FString GetAssetSignature() const;

private:
	void LoadHiddenAssets();
	void SaveHiddenAssets() const;

	TArray<FEpicodeFocusedEntityRow> Entities;
	TArray<FEpicodeFocusedSceneRow> Scenes;
	TArray<FEpicodeFocusedAssetRow> Assets;
	FGuid SelectedEntityId;
	FString SceneName;
	FString SceneId;
	FString LastMessage;
	FEpicodeLocalLibrary LocalLibrary;
	uint64 Revision = 0;
	bool bHasScene = false;
	bool bDirty = false;
	bool bHiddenAssetsLoaded = false;
	TSet<FString> HiddenAssetPaths;
};
