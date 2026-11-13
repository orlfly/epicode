#include "EpicodeSceneSubsystem.h"

#include "EpicodeCommand.h"
#include "EpicodeCoreModule.h"
#include "EpicodeEngineSubsystem.h"
#include "EpicodeEntityActors.h"
#include "EpicodeEventBus.h"
#include "EpicodeSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	bool IsEntityTypeValid(EEpicodeEntityType Type)
	{
		return Type == EEpicodeEntityType::Group
			|| Type == EEpicodeEntityType::Character
			|| Type == EEpicodeEntityType::Prop
			|| Type == EEpicodeEntityType::Light
			|| Type == EEpicodeEntityType::Camera;
	}
}

void UEpicodeSceneSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EnvironmentController.Initialize(GetWorld());
	UndoRecord = NewObject<UEpicodeUndoRecord>(this);
	UndoRecord->SetFlags(RF_Transactional);
}

void UEpicodeSceneSubsystem::Deinitialize()
{
	DestroyProjection();
	EnvironmentController.Shutdown();
	Super::Deinitialize();
}

bool UEpicodeSceneSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// The author plane owns editor and preview worlds. Runtime game worlds are
	// projected from a session by EpicodeNetwork and must not spawn editor actors.
	return WorldType == EWorldType::Editor || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

UEpicodeSceneSubsystem* UEpicodeSceneSubsystem::Get(UWorld* InWorld)
{
	return InWorld ? InWorld->GetSubsystem<UEpicodeSceneSubsystem>() : nullptr;
}

UWorld* UEpicodeSceneSubsystem::GetPrimaryWorld()
{
	if (!GEngine)
	{
		return nullptr;
	}
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* World = Context.World();
		if (!World)
		{
			continue;
		}
		if (Context.WorldType == EWorldType::Editor || Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE)
		{
			return World;
		}
	}
	return nullptr;
}

UEpicodeSceneSubsystem* UEpicodeSceneSubsystem::GetPrimary()
{
	return Get(GetPrimaryWorld());
}

void UEpicodeSceneSubsystem::MarkDocumentModified(bool bInDirty)
{
	Document.MarkModified();
	bDirty = bInDirty;
	BuildSnapshot();
	SyncUndoSnapshot();
}

FString UEpicodeSceneSubsystem::GetDocumentJson() const
{
	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Document.ToJson().ToSharedRef(), Writer);
	return Out;
}

bool UEpicodeSceneSubsystem::ApplyDocumentJson(const FString& InJson, FString& OutError)
{
	if (InJson.IsEmpty())
	{
		OutError = TEXT("Undo snapshot is empty.");
		return false;
	}
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJson);
	TSharedPtr<FJsonObject> Object;
	if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
	{
		OutError = TEXT("Undo snapshot is not valid JSON.");
		return false;
	}
	FEpicodeSceneDocument Loaded;
	if (!FEpicodeSceneDocument::FromJson(Object, Loaded, OutError))
	{
		return false;
	}
	RestoreDocument(Loaded);
	return true;
}

void UEpicodeSceneSubsystem::SyncUndoSnapshot()
{
	if (UndoRecord)
	{
		UndoRecord->SceneJson = GetDocumentJson();
	}
}

TSharedPtr<FEpicodeSceneSnapshot> UEpicodeSceneSubsystem::BuildSnapshot() const
{
	TSharedPtr<FEpicodeSceneSnapshot> NewSnapshot = MakeShared<FEpicodeSceneSnapshot>();
	NewSnapshot->Version = Document.Revision;
	NewSnapshot->SceneId = Document.SceneId;
	NewSnapshot->Name = Document.Name;
	NewSnapshot->Json = Document.ToJson();
	{
		FScopeLock Lock(&SnapshotLock);
		const_cast<UEpicodeSceneSubsystem*>(this)->Snapshot = NewSnapshot;
	}
	return NewSnapshot;
}

TSharedPtr<const FEpicodeSceneSnapshot> UEpicodeSceneSubsystem::GetSnapshot() const
{
	FScopeLock Lock(&SnapshotLock);
	return Snapshot;
}

void UEpicodeSceneSubsystem::RestoreDocument(const FEpicodeSceneDocument& InDocument)
{
	DestroyProjection();
	EnvironmentController.Shutdown();
	Document = InDocument;
	bHasScene = true;
	bDirty = true;
	ActiveCameraId = FGuid();
	if (!Document.ActiveCameraId.IsEmpty())
	{
		FGuid::Parse(Document.ActiveCameraId, ActiveCameraId);
	}
	EnvironmentController.Initialize(GetWorld());
	FString EnvironmentError;
	EnvironmentController.Apply(Document.Environment, EnvironmentError);
	EnvironmentController.EnsureActors();
	RebuildProjection();
	BuildSnapshot();
	SyncUndoSnapshot();
}

bool UEpicodeSceneSubsystem::NewScene(const FString& InName, FString& OutError)
{
	if (bHasScene && bDirty)
	{
		OutError = TEXT("The current scene has unsaved changes.");
		return false;
	}
	DestroyProjection();
	EnvironmentController.Shutdown();
	Document = FEpicodeSceneDocument::CreateDefault(InName.IsEmpty() ? TEXT("Untitled") : InName);
	CurrentDocumentPath.Reset();
	bHasScene = true;
	bDirty = false;
	ActiveCameraId = FGuid();
	EnvironmentController.Initialize(GetWorld());
	FString EnvironmentError;
	if (!EnvironmentController.Apply(Document.Environment, EnvironmentError))
	{
		OutError = EnvironmentError;
		return false;
	}
	EnvironmentController.EnsureActors();
	RebuildProjection();
	EnsureDefaultCamera(OutError);
	Document.MarkModified();
	bDirty = false;
	BuildSnapshot();
	SyncUndoSnapshot();
	EnvironmentController.EnsureActors();
	PublishEvent(TEXT("scene.created"));
	return true;
}

bool UEpicodeSceneSubsystem::OpenScene(const FString& InSceneId, FString& OutError)
{
	const FString SceneRoot = UEpicodeSettings::Get()->GetSceneRootPath();
	FEpicodeSceneLibraryEntry Entry;
	if (!FEpicodeSceneLibrary::Find(SceneRoot, InSceneId, Entry))
	{
		OutError = FString::Printf(TEXT("Scene not found: %s"), *InSceneId);
		return false;
	}
	if (!Entry.bAvailable)
	{
		OutError = Entry.Error.IsEmpty() ? TEXT("Scene is unavailable.") : Entry.Error;
		return false;
	}
	FEpicodeSceneDocument Loaded;
	if (!FEpicodeSceneLibrary::Load(Entry.DocumentPath, Loaded, OutError))
	{
		return false;
	}
	DestroyProjection();
	EnvironmentController.Shutdown();
	Document = MoveTemp(Loaded);
	CurrentDocumentPath = Entry.DocumentPath;
	bHasScene = true;
	bDirty = false;
	ActiveCameraId = FGuid();
	if (!Document.ActiveCameraId.IsEmpty())
	{
		FGuid::Parse(Document.ActiveCameraId, ActiveCameraId);
	}
	EnvironmentController.Initialize(GetWorld());
	RebuildProjection();
	if (!Document.Environment.IsValid())
	{
		Document.Environment = MakeEpicodeDefaultEnvironment();
	}
	FString EnvironmentError;
	if (!EnvironmentController.Apply(Document.Environment, EnvironmentError))
	{
		OutError = EnvironmentError;
		return false;
	}
	if (!FindEntity(ActiveCameraId) || FindEntity(ActiveCameraId)->Type != EEpicodeEntityType::Camera)
	{
		EnsureDefaultCamera(OutError);
	}
	BuildSnapshot();
	SyncUndoSnapshot();
	PublishEvent(TEXT("scene.loaded"));
	return true;
}

bool UEpicodeSceneSubsystem::SwitchScene(const FString& InSceneId, FString& OutError)
{
	if (bHasScene && InSceneId == Document.SceneId)
	{
		return true;
	}
	if (bHasScene && bDirty)
	{
		OutError = TEXT("The current scene has unsaved changes.");
		return false;
	}
	return OpenScene(InSceneId, OutError);
}

bool UEpicodeSceneSubsystem::DeleteScene(const FString& InSceneId, FString& OutTrashPath, FString& OutError)
{
	if (bHasScene && InSceneId == Document.SceneId)
	{
		OutError = TEXT("The current scene cannot be deleted while it is open.");
		return false;
	}
	const FString SceneRoot = UEpicodeSettings::Get()->GetSceneRootPath();
	FEpicodeSceneLibraryEntry Entry;
	if (!FEpicodeSceneLibrary::Find(SceneRoot, InSceneId, Entry))
	{
		OutError = FString::Printf(TEXT("Scene not found: %s"), *InSceneId);
		return false;
	}
	if (!FEpicodeSceneLibrary::Delete(Entry, UEpicodeSettings::Get()->GetSceneTrashRootPath(), Document.SceneId, OutTrashPath, OutError))
	{
		return false;
	}
	PublishEvent(TEXT("scene.deleted"));
	return true;
}

bool UEpicodeSceneSubsystem::SaveScene(FString& OutPath, FString& OutError)
{
	if (!bHasScene)
	{
		OutError = TEXT("No scene is open.");
		return false;
	}
	if (!FEpicodeSceneLibrary::Save(Document, UEpicodeSettings::Get()->GetSceneRootPath(), OutPath, OutError))
	{
		return false;
	}
	CurrentDocumentPath = OutPath;
	bDirty = false;
	BuildSnapshot();
	PublishEvent(TEXT("scene.saved"));
	return true;
}

bool UEpicodeSceneSubsystem::SaveSceneAs(const FString& InNewName, FString& OutPath, FString& OutError)
{
	if (!bHasScene)
	{
		OutError = TEXT("No scene is open.");
		return false;
	}
	const FString OldSceneId = Document.SceneId;
	Document.SceneId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	Document.Name = InNewName.IsEmpty() ? TEXT("Untitled") : InNewName;
	Document.LevelPath = FString::Printf(TEXT("/Game/Epicode/Scenes/%s"), *Document.Name.Replace(TEXT(" "), TEXT("")));
	if (!FEpicodeSceneLibrary::Save(Document, UEpicodeSettings::Get()->GetSceneRootPath(), OutPath, OutError))
	{
		Document.SceneId = OldSceneId;
		return false;
	}
	CurrentDocumentPath = OutPath;
	bDirty = false;
	BuildSnapshot();
	PublishEvent(TEXT("scene.saved"));
	return true;
}

bool UEpicodeSceneSubsystem::CloseScene(bool bDiscard, FString& OutError)
{
	if (!bHasScene)
	{
		return true;
	}
	if (bDirty && !bDiscard)
	{
		OutError = TEXT("The current scene has unsaved changes.");
		return false;
	}
	DestroyProjection();
	EnvironmentController.Shutdown();
	Document = FEpicodeSceneDocument();
	CurrentDocumentPath.Reset();
	bHasScene = false;
	bDirty = false;
	ActiveCameraId = FGuid();
	{
		FScopeLock Lock(&SnapshotLock);
		Snapshot.Reset();
	}
	PublishEvent(TEXT("scene.closed"));
	return true;
}

TArray<FEpicodeSceneLibraryEntry> UEpicodeSceneSubsystem::ListScenes() const
{
	return FEpicodeSceneLibrary::Scan(UEpicodeSettings::Get()->GetSceneRootPath(), Document.SceneId);
}

FEpicodeEntityRecord* UEpicodeSceneSubsystem::FindEntity(const FGuid& InId)
{
	return Document.FindEntity(InId);
}

const FEpicodeEntityRecord* UEpicodeSceneSubsystem::FindEntity(const FGuid& InId) const
{
	return Document.FindEntity(InId);
}

AActor* UEpicodeSceneSubsystem::GetEntityActor(const FGuid& InId) const
{
	const TWeakObjectPtr<AActor>* Found = EntityActors.Find(InId.ToString(EGuidFormats::DigitsWithHyphens));
	return Found ? Found->Get() : nullptr;
}

void UEpicodeSceneSubsystem::AttachEntityActor(AActor* InActor, const FEpicodeEntityRecord& InRecord)
{
	if (!InActor)
	{
		return;
	}
	AActor* ParentActor = InRecord.ParentId.IsValid() ? GetEntityActor(InRecord.ParentId) : nullptr;
	if (ParentActor)
	{
		InActor->AttachToActor(ParentActor, FAttachmentTransformRules::KeepRelativeTransform);
		InActor->SetActorRelativeTransform(InRecord.Transform.ToFTransform());
	}
	else
	{
		InActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		InActor->SetActorTransform(InRecord.Transform.ToFTransform());
	}
}

void UEpicodeSceneSubsystem::SyncEntityActor(const FEpicodeEntityRecord& InRecord)
{
	AActor* Actor = GetEntityActor(InRecord.Id);
	if (!Actor)
	{
		return;
	}
	AttachEntityActor(Actor, InRecord);
	EpicodeActorFactory::ApplyRecord(Actor, InRecord);
	AttachEntityActor(Actor, InRecord);
}

bool UEpicodeSceneSubsystem::CreateEntity(EEpicodeEntityType InType, const FString& InName, const FGuid& InParentId, const FEpicodeTransform& InTransform, const TSharedPtr<FJsonObject>& InData, FGuid& OutId, FString& OutError)
{
	if (!bHasScene)
	{
		OutError = TEXT("No scene is open.");
		return false;
	}
	if (!IsEntityTypeValid(InType))
	{
		OutError = TEXT("Unknown entity type.");
		return false;
	}
	if (!InTransform.IsFinite())
	{
		OutError = TEXT("Entity transform contains a non-finite value.");
		return false;
	}
	if (InParentId.IsValid() && !FindEntity(InParentId))
	{
		OutError = TEXT("Parent entity not found.");
		return false;
	}

	FEpicodeEntityRecord Record;
	Record.Id = FGuid::NewGuid();
	Record.Type = InType;
	Record.Name = Document.MakeUniqueName(InName.IsEmpty() ? LexToString(InType) : InName, InParentId);
	Record.ParentId = InParentId;
	Record.Transform = InTransform;
	Record.Data = InData.IsValid() ? InData : MakeShared<FJsonObject>();
	if (!Record.IsValid())
	{
		OutError = TEXT("Entity record is invalid.");
		return false;
	}

	AActor* Actor = EpicodeActorFactory::SpawnForRecord(GetWorld(), Record);
	if (!Actor)
	{
		OutError = TEXT("Unable to spawn the entity actor.");
		return false;
	}
	Document.Entities.Add(Record);
	EntityActors.Add(Record.Id.ToString(EGuidFormats::DigitsWithHyphens), Actor);
	AttachEntityActor(Actor, Record);
	EpicodeActorFactory::ApplyRecord(Actor, Record);
	OutId = Record.Id;
	MarkDocumentModified(true);
	PublishEvent(TEXT("entity.created"), Record.Id, Record.ToJson());
	return true;
}

bool UEpicodeSceneSubsystem::DeleteEntity(const FGuid& InId, bool bCascade, TArray<FString>& OutRemovedIds, FString& OutError)
{
	if (!FindEntity(InId))
	{
		OutError = TEXT("Entity not found.");
		return false;
	}
	TArray<FGuid> ToRemove;
	ToRemove.Add(InId);
	if (bCascade)
	{
		for (int32 Index = 0; Index < ToRemove.Num(); ++Index)
		{
			ToRemove.Append(Document.GetChildren(ToRemove[Index]));
		}
	}
	for (const FGuid& Id : ToRemove)
	{
		const FString Key = Id.ToString(EGuidFormats::DigitsWithHyphens);
		if (TWeakObjectPtr<AActor>* Found = EntityActors.Find(Key))
		{
			if (AActor* Actor = Found->Get())
			{
				Actor->Destroy();
			}
			EntityActors.Remove(Key);
		}
		OutRemovedIds.Add(Key);
	}
	if (bCascade)
	{
		TArray<FString> RemovedByDocument;
		Document.RemoveEntityCascade(InId, RemovedByDocument);
	}
	else
	{
		for (const FGuid& ChildId : Document.GetChildren(InId))
		{
			if (FEpicodeEntityRecord* Child = Document.FindEntity(ChildId))
			{
				Child->ParentId.Invalidate();
				if (AActor* ChildActor = GetEntityActor(ChildId))
				{
					AttachEntityActor(ChildActor, *Child);
				}
			}
		}
		Document.Entities.RemoveAll([&InId](const FEpicodeEntityRecord& Entity) { return Entity.Id == InId; });
	}
	if (ActiveCameraId == InId || !FindEntity(ActiveCameraId))
	{
		EnsureDefaultCamera(OutError);
	}
	MarkDocumentModified(true);
	for (const FString& RemovedId : OutRemovedIds)
	{
		FGuid RemovedGuid;
		FGuid::Parse(RemovedId, RemovedGuid);
		PublishEvent(TEXT("entity.deleted"), RemovedGuid);
	}
	return true;
}

bool UEpicodeSceneSubsystem::RenameEntity(const FGuid& InId, const FString& InNewName, FString& OutError)
{
	FEpicodeEntityRecord* Record = FindEntity(InId);
	if (!Record)
	{
		OutError = TEXT("Entity not found.");
		return false;
	}
	if (InNewName.IsEmpty())
	{
		OutError = TEXT("Entity name cannot be empty.");
		return false;
	}
	for (const FEpicodeEntityRecord& Other : Document.Entities)
	{
		if (Other.Id != InId && Other.ParentId == Record->ParentId && Other.Name.Equals(InNewName, ESearchCase::IgnoreCase))
		{
			OutError = TEXT("A sibling entity already uses this name.");
			return false;
		}
	}
	Record->Name = InNewName;
	MarkDocumentModified(true);
	PublishEvent(TEXT("entity.updated"), Record->Id, Record->ToJson());
	return true;
}

bool UEpicodeSceneSubsystem::DuplicateEntity(const FGuid& InId, const FVector& InOffset, FGuid& OutNewId, FString& OutError)
{
	if (!Document.DuplicateEntity(InId, OutNewId, InOffset, OutError))
	{
		return false;
	}
	const FEpicodeEntityRecord* Record = FindEntity(OutNewId);
	if (!Record)
	{
		OutError = TEXT("Duplicated entity not found.");
		return false;
	}
	AActor* Actor = EpicodeActorFactory::SpawnForRecord(GetWorld(), *Record);
	if (!Actor)
	{
		Document.Entities.RemoveAll([&OutNewId](const FEpicodeEntityRecord& Entity) { return Entity.Id == OutNewId; });
		OutError = TEXT("Unable to spawn the duplicated entity actor.");
		return false;
	}
	EntityActors.Add(OutNewId.ToString(EGuidFormats::DigitsWithHyphens), Actor);
	AttachEntityActor(Actor, *Record);
	EpicodeActorFactory::ApplyRecord(Actor, *Record);
	MarkDocumentModified(true);
	PublishEvent(TEXT("entity.created"), OutNewId, Record->ToJson());
	return true;
}

bool UEpicodeSceneSubsystem::ReparentEntity(const FGuid& InId, const FGuid& InNewParentId, bool bKeepWorldTransform, FString& OutError)
{
	if (!Document.ReparentEntity(InId, InNewParentId, bKeepWorldTransform, OutError))
	{
		return false;
	}
	const FEpicodeEntityRecord* Record = FindEntity(InId);
	if (Record)
	{
		SyncEntityActor(*Record);
		MarkDocumentModified(true);
		PublishEvent(TEXT("entity.updated"), InId, Record->ToJson());
	}
	return true;
}

bool UEpicodeSceneSubsystem::SetEntityTransform(const FGuid& InId, const FEpicodeTransform& InTransform, bool bWorldSpace, FString& OutError)
{
	FEpicodeEntityRecord* Record = FindEntity(InId);
	if (!Record)
	{
		OutError = TEXT("Entity not found.");
		return false;
	}
	if (!InTransform.IsFinite())
	{
		OutError = TEXT("Transform contains a non-finite value.");
		return false;
	}
	Record->Transform = bWorldSpace ? Document.ComputeLocalTransform(InId, Record->ParentId, InTransform) : InTransform;
	if (AActor* Actor = GetEntityActor(InId))
	{
		if (Record->ParentId.IsValid())
		{
			Actor->SetActorRelativeTransform(Record->Transform.ToFTransform());
		}
		else
		{
			Actor->SetActorTransform(Record->Transform.ToFTransform());
		}
	}
	MarkDocumentModified(true);
	PublishEvent(TEXT("entity.updated"), InId, Record->ToJson());
	return true;
}

bool UEpicodeSceneSubsystem::SetEntityVisible(const FGuid& InId, bool bVisible, FString& OutError)
{
	FEpicodeEntityRecord* Record = FindEntity(InId);
	if (!Record)
	{
		OutError = TEXT("Entity not found.");
		return false;
	}
	Record->bVisible = bVisible;
	if (AActor* Actor = GetEntityActor(InId))
	{
		Actor->SetActorHiddenInGame(!bVisible);
	}
	MarkDocumentModified(true);
	PublishEvent(TEXT("entity.updated"), InId, Record->ToJson());
	return true;
}

bool UEpicodeSceneSubsystem::SetEntityData(const FGuid& InId, const TSharedPtr<FJsonObject>& InData, FString& OutError)
{
	FEpicodeEntityRecord* Record = FindEntity(InId);
	if (!Record)
	{
		OutError = TEXT("Entity not found.");
		return false;
	}
	Record->Data = InData.IsValid() ? InData : MakeShared<FJsonObject>();
	SyncEntityActor(*Record);
	MarkDocumentModified(true);
	PublishEvent(TEXT("entity.updated"), InId, Record->ToJson());
	return true;
}

bool UEpicodeSceneSubsystem::SetEntityParentAndData(const FGuid& InId, const FGuid& InParentId, const TSharedPtr<FJsonObject>& InData, FString& OutError)
{
	return ReparentEntity(InId, InParentId, false, OutError) && SetEntityData(InId, InData, OutError);
}

void UEpicodeSceneSubsystem::NotifyEntityMutated(const FGuid& InId, const TSharedPtr<FJsonObject>& InPayload)
{
	const FEpicodeEntityRecord* Record = FindEntity(InId);
	MarkDocumentModified(true);
	PublishEvent(TEXT("entity.updated"), InId, InPayload.IsValid() ? InPayload : (Record ? Record->ToJson() : nullptr));
}

bool UEpicodeSceneSubsystem::SetEnvironment(const TSharedPtr<FJsonObject>& InEnvironment, FString& OutError)
{
	if (!bHasScene)
	{
		OutError = TEXT("No scene is open.");
		return false;
	}
	if (!EnvironmentController.Apply(InEnvironment, OutError))
	{
		return false;
	}
	Document.Environment = InEnvironment;
	MarkDocumentModified(true);
	PublishEvent(TEXT("environment.updated"), FGuid(), InEnvironment);
	return true;
}

bool UEpicodeSceneSubsystem::ApplyEnvironmentPreset(const FString& InPresetName, FString& OutError)
{
	return SetEnvironment(FEpicodeEnvironmentController::MakePreset(InPresetName), OutError);
}

TSharedPtr<FJsonObject> UEpicodeSceneSubsystem::CaptureEnvironment() const
{
	return EnvironmentController.Capture();
}

bool UEpicodeSceneSubsystem::SetRuntimeMetadata(const FEpicodeRuntimeMetadata& InRuntime, const FString& InLevelPath, bool bValidateForRuntime, FString& OutError)
{
	if (!bHasScene)
	{
		OutError = TEXT("No scene is open.");
		return false;
	}
	const FEpicodeSceneDocument Backup = Document;
	Document.Runtime = InRuntime;
	if (!InLevelPath.IsEmpty())
	{
		Document.LevelPath = InLevelPath;
	}
	const bool bValid = bValidateForRuntime ? Document.ValidateForRuntime(OutError) : Document.ValidateRuntimeReferences(OutError);
	if (!bValid)
	{
		Document = Backup;
		return false;
	}
	MarkDocumentModified(true);
	PublishEvent(TEXT("scene.runtime.updated"), FGuid(), Document.Runtime.ToJson());
	return true;
}

bool UEpicodeSceneSubsystem::SetActiveCamera(const FGuid& InCameraId, FString& OutError)
{
	const FEpicodeEntityRecord* Record = FindEntity(InCameraId);
	if (!Record || Record->Type != EEpicodeEntityType::Camera)
	{
		OutError = TEXT("Camera entity not found.");
		return false;
	}
	ActiveCameraId = InCameraId;
	Document.ActiveCameraId = InCameraId.ToString(EGuidFormats::DigitsWithHyphens);
	MarkDocumentModified(true);
	PublishEvent(TEXT("camera.activated"), InCameraId, Record->ToJson());
	return true;
}

AActor* UEpicodeSceneSubsystem::GetActiveCameraActor() const
{
	return GetEntityActor(ActiveCameraId);
}

void UEpicodeSceneSubsystem::EnsureDefaultCamera(FString& OutError)
{
	if (FindEntity(ActiveCameraId) && FindEntity(ActiveCameraId)->Type == EEpicodeEntityType::Camera)
	{
		return;
	}
	for (const FEpicodeEntityRecord& Entity : Document.Entities)
	{
		if (Entity.Type == EEpicodeEntityType::Camera)
		{
			ActiveCameraId = Entity.Id;
			Document.ActiveCameraId = Entity.Id.ToString(EGuidFormats::DigitsWithHyphens);
			return;
		}
	}
	FEpicodeTransform Transform;
	Transform.Location = FVector(0.0, -600.0, 200.0);
	Transform.Rotation = FRotator(0.0, 0.0, 0.0);
	TSharedPtr<FJsonObject> CameraData = MakeShared<FJsonObject>();
	CameraData->SetNumberField(TEXT("fieldOfView"), 35.0);
	CameraData->SetNumberField(TEXT("focalLength"), 50.0);
	CameraData->SetNumberField(TEXT("aperture"), 2.8);
	CameraData->SetNumberField(TEXT("focusDistance"), 600.0);
	FGuid CameraId;
	if (!CreateEntity(EEpicodeEntityType::Camera, TEXT("Default Camera"), FGuid(), Transform, CameraData, CameraId, OutError))
	{
		return;
	}
	ActiveCameraId = CameraId;
	Document.ActiveCameraId = CameraId.ToString(EGuidFormats::DigitsWithHyphens);
	MarkDocumentModified(true);
}

void UEpicodeSceneSubsystem::RebuildProjection()
{
	DestroyProjection();
	for (const FEpicodeEntityRecord& Record : Document.Entities)
	{
		AActor* Actor = EpicodeActorFactory::SpawnForRecord(GetWorld(), Record);
		if (Actor)
		{
			EntityActors.Add(Record.Id.ToString(EGuidFormats::DigitsWithHyphens), Actor);
		}
	}
	for (const FEpicodeEntityRecord& Record : Document.Entities)
	{
		if (AActor* Actor = GetEntityActor(Record.Id))
		{
			AttachEntityActor(Actor, Record);
			EpicodeActorFactory::ApplyRecord(Actor, Record);
			AttachEntityActor(Actor, Record);
		}
	}
	BuildSnapshot();
}

void UEpicodeSceneSubsystem::DestroyProjection()
{
	for (const TPair<FString, TWeakObjectPtr<AActor>>& Pair : EntityActors)
	{
		if (AActor* Actor = Pair.Value.Get())
		{
			Actor->Destroy();
		}
	}
	EntityActors.Reset();
}

void UEpicodeSceneSubsystem::PublishEvent(const FString& InType, const FGuid& InEntityId, const TSharedPtr<FJsonObject>& InPayload)
{
	if (UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get())
	{
		if (FEpicodeEventBus* EventBus = EngineSubsystem->GetEventBus())
		{
			EventBus->Publish(InType, Document.SceneId, InEntityId.IsValid() ? InEntityId.ToString(EGuidFormats::DigitsWithHyphens) : FString(), InPayload);
		}
	}
}

FEpicodeEventBus* UEpicodeSceneSubsystem::GetEventBus() const
{
	UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get();
	return EngineSubsystem ? EngineSubsystem->GetEventBus() : nullptr;
}

IEpicodeTransactionSink* UEpicodeSceneSubsystem::GetTransactionSink() const
{
	UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get();
	return EngineSubsystem ? EngineSubsystem->GetTransactionSink() : nullptr;
}
