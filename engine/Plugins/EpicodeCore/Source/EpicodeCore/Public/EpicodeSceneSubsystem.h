#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "EpicodeSceneDocument.h"
#include "EpicodeSceneLibrary.h"
#include "EpicodeSceneSnapshot.h"
#include "EpicodeEnvironmentController.h"
#include "EpicodeSceneSubsystem.generated.h"

class AActor;
class FEpicodeEventBus;
class IEpicodeTransactionSink;

UCLASS()
class EPICODECORE_API UEpicodeUndoRecord : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FString SceneJson;
};

UCLASS()
class EPICODECORE_API UEpicodeSceneSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	static UEpicodeSceneSubsystem* Get(UWorld* InWorld);
	static UWorld* GetPrimaryWorld();
	static UEpicodeSceneSubsystem* GetPrimary();

	bool NewScene(const FString& InName, FString& OutError);
	bool OpenScene(const FString& InSceneId, FString& OutError);
	bool SwitchScene(const FString& InSceneId, FString& OutError);
	bool DeleteScene(const FString& InSceneId, FString& OutTrashPath, FString& OutError);
	bool SaveScene(FString& OutPath, FString& OutError);
	bool SaveSceneAs(const FString& InNewName, FString& OutPath, FString& OutError);
	bool CloseScene(bool bDiscard, FString& OutError);

	TArray<FEpicodeSceneLibraryEntry> ListScenes() const;
	FEpicodeSceneDocument& GetDocument() { return Document; }
	const FEpicodeSceneDocument& GetDocument() const { return Document; }
	FString GetCurrentSceneId() const { return Document.SceneId; }
	FString GetCurrentSceneName() const { return Document.Name; }
	FString GetCurrentDocumentPath() const { return CurrentDocumentPath; }
	bool HasScene() const { return bHasScene; }
	bool IsDirty() const { return bDirty; }
	void SetDirty(bool bInDirty) { bDirty = bInDirty; }
	FString GetDocumentJson() const;
	bool ApplyDocumentJson(const FString& InJson, FString& OutError);
	void SyncUndoSnapshot();
	UEpicodeUndoRecord* GetUndoRecord() const { return UndoRecord; }

	TSharedPtr<const FEpicodeSceneSnapshot> GetSnapshot() const;
	TSharedPtr<FEpicodeSceneSnapshot> BuildSnapshot() const;
	void RestoreDocument(const FEpicodeSceneDocument& InDocument);

	FEpicodeEntityRecord* FindEntity(const FGuid& InId);
	const FEpicodeEntityRecord* FindEntity(const FGuid& InId) const;
	AActor* GetEntityActor(const FGuid& InId) const;

	bool CreateEntity(EEpicodeEntityType InType, const FString& InName, const FGuid& InParentId, const FEpicodeTransform& InTransform, const TSharedPtr<FJsonObject>& InData, FGuid& OutId, FString& OutError);
	bool DeleteEntity(const FGuid& InId, bool bCascade, TArray<FString>& OutRemovedIds, FString& OutError);
	bool RenameEntity(const FGuid& InId, const FString& InNewName, FString& OutError);
	bool DuplicateEntity(const FGuid& InId, const FVector& InOffset, FGuid& OutNewId, FString& OutError);
	bool ReparentEntity(const FGuid& InId, const FGuid& InNewParentId, bool bKeepWorldTransform, FString& OutError);
	bool SetEntityTransform(const FGuid& InId, const FEpicodeTransform& InTransform, bool bWorldSpace, FString& OutError);
	bool SetEntityVisible(const FGuid& InId, bool bVisible, FString& OutError);
	bool SetEntityData(const FGuid& InId, const TSharedPtr<FJsonObject>& InData, FString& OutError);
	bool SetEntityParentAndData(const FGuid& InId, const FGuid& InParentId, const TSharedPtr<FJsonObject>& InData, FString& OutError);
	void NotifyEntityMutated(const FGuid& InId, const TSharedPtr<FJsonObject>& InPayload = nullptr);

	bool SetEnvironment(const TSharedPtr<FJsonObject>& InEnvironment, FString& OutError);
	bool ApplyEnvironmentPreset(const FString& InPresetName, FString& OutError);
	TSharedPtr<FJsonObject> CaptureEnvironment() const;
	bool SetRuntimeMetadata(const FEpicodeRuntimeMetadata& InRuntime, const FString& InLevelPath, bool bValidateForRuntime, FString& OutError);
	bool SetActiveCamera(const FGuid& InCameraId, FString& OutError);
	FGuid GetActiveCameraId() const { return ActiveCameraId; }
	AActor* GetActiveCameraActor() const;

	void PublishEvent(const FString& InType, const FGuid& InEntityId = FGuid(), const TSharedPtr<FJsonObject>& InPayload = nullptr);
	FEpicodeEventBus* GetEventBus() const;
	IEpicodeTransactionSink* GetTransactionSink() const;

	void RebuildProjection();
	void DestroyProjection();

private:
	void EnsureDefaultCamera(FString& OutError);
	void SyncEntityActor(const FEpicodeEntityRecord& InRecord);
	void AttachEntityActor(AActor* InActor, const FEpicodeEntityRecord& InRecord);
	void MarkDocumentModified(bool bInDirty = true);

	FEpicodeSceneDocument Document;
	FString CurrentDocumentPath;
	bool bHasScene = false;
	bool bDirty = false;
	FGuid ActiveCameraId;
	TMap<FString, TWeakObjectPtr<AActor>> EntityActors;
	FEpicodeEnvironmentController EnvironmentController;
	UPROPERTY()
	TObjectPtr<UEpicodeUndoRecord> UndoRecord;
	mutable FCriticalSection SnapshotLock;
	TSharedPtr<FEpicodeSceneSnapshot> Snapshot;
};
