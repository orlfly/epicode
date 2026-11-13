#pragma once

#include "CoreMinimal.h"
#include "EpicodeTypes.h"

struct EPICODECORE_API FEpicodeSceneMigration
{
	static bool Migrate(TSharedPtr<FJsonObject>& InOutJson, int32 FromVersion, int32 ToVersion, FString& OutError);
};

struct EPICODECORE_API FEpicodeAgentSpawnPoint
{
	FString Id;
	FEpicodeTransform Transform;
	TSharedPtr<FJsonObject> Extra;

	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeAgentSpawnPoint& OutPoint, FString& OutError);
};

struct EPICODECORE_API FEpicodeAgentRuntime
{
	TArray<FString> ControllableCharacterIds;
	TArray<FEpicodeAgentSpawnPoint> SpawnPoints;
	TArray<FString> AllowedActions;
	TSharedPtr<FJsonObject> Extra;

	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeAgentRuntime& OutRuntime, FString& OutError);
};

struct EPICODECORE_API FEpicodeViewerRuntime
{
	TArray<FString> CameraIds;
	FString DefaultCameraId;
	FEpicodeTransform SpawnTransform;
	bool bShowOverlay = true;
	TSharedPtr<FJsonObject> Extra;

	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeViewerRuntime& OutRuntime, FString& OutError);
};

struct EPICODECORE_API FEpicodeRuntimeMetadata
{
	FEpicodeAgentRuntime Agent;
	FEpicodeViewerRuntime Viewer;
	TSharedPtr<FJsonObject> Extra;

	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeRuntimeMetadata& OutMetadata, FString& OutError);
};

struct EPICODECORE_API FEpicodeSceneDocument
{
	static constexpr int32 CurrentSchemaVersion = 2;

	int32 SchemaVersion = CurrentSchemaVersion;
	FString SceneId;
	FString Name;
	FString LevelPath;
	TArray<FEpicodeEntityRecord> Entities;
	TSharedPtr<FJsonObject> Environment;
	FString ActiveCameraId;
	FEpicodeRuntimeMetadata Runtime;
	uint64 Revision = 0;

	static FEpicodeSceneDocument CreateDefault(const FString& InName = TEXT("Untitled"));

	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeSceneDocument& OutDocument, FString& OutError);

	bool Validate(FString& OutError) const;
	bool ValidateForRuntime(FString& OutError) const;
	bool ValidateRuntimeReferences(FString& OutError) const;
	FEpicodeEntityRecord* FindEntity(const FGuid& InId);
	const FEpicodeEntityRecord* FindEntity(const FGuid& InId) const;
	FEpicodeEntityRecord* FindEntity(const FString& InId);
	const FEpicodeEntityRecord* FindEntity(const FString& InId) const;

	TArray<FGuid> GetChildren(const FGuid& InParentId) const;
	bool IsDescendant(const FGuid& InAncestor, const FGuid& InCandidate) const;
	FString MakeUniqueName(const FString& InBaseName, const FGuid& InParentId) const;
	FString MakeUniqueEntityName(const FString& InBaseName) const;

	void RemoveEntityCascade(const FGuid& InId, TArray<FString>& OutRemovedIds);
	bool DuplicateEntity(const FGuid& InId, FGuid& OutNewId, const FVector& InOffset, FString& OutError);
	bool ReparentEntity(const FGuid& InId, const FGuid& InNewParent, bool bKeepWorldTransform, FString& OutError);

	FEpicodeTransform ComputeWorldTransform(const FGuid& InId) const;
	FEpicodeTransform ComputeLocalTransform(const FGuid& InId, const FGuid& InParentId, const FEpicodeTransform& InWorld) const;

	void MarkModified();
};
