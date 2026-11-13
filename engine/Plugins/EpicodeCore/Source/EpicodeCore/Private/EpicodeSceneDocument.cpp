#include "EpicodeSceneDocument.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	void CopyUnknownFields(const TSharedPtr<FJsonObject>& Source, const TSet<FString>& Known, TSharedPtr<FJsonObject>& OutExtra)
	{
		if (!Source.IsValid())
		{
			return;
		}
		if (!OutExtra.IsValid())
		{
			OutExtra = MakeShared<FJsonObject>();
		}
		for (const auto& Pair : Source->Values)
		{
			const FString Key(*Pair.Key);
			if (!Known.Contains(Key))
			{
				OutExtra->SetField(Key, Pair.Value);
			}
		}
	}

	TArray<FString> ReadStringArray(const TSharedPtr<FJsonObject>& InJson, const TCHAR* Field)
	{
		TArray<FString> Result;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (InJson.IsValid() && InJson->TryGetArrayField(Field, Values) && Values)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				const FString Text = Value->AsString().TrimStartAndEnd();
				if (!Text.IsEmpty())
				{
					Result.Add(Text);
				}
			}
		}
		return Result;
	}

	void WriteStringArray(const TSharedPtr<FJsonObject>& OutJson, const TCHAR* Field, const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Array;
		for (const FString& Value : Values)
		{
			Array.Add(MakeShared<FJsonValueString>(Value));
		}
		OutJson->SetArrayField(Field, Array);
	}
}

TSharedPtr<FJsonObject> FEpicodeAgentSpawnPoint::ToJson() const
{
	TSharedPtr<FJsonObject> Result = Extra.IsValid() ? MakeShared<FJsonObject>(*Extra) : MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("id"), Id);
	Result->SetObjectField(TEXT("transform"), Transform.ToJson());
	return Result;
}

bool FEpicodeAgentSpawnPoint::FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeAgentSpawnPoint& OutPoint, FString& OutError)
{
	if (!InJson.IsValid())
	{
		OutError = TEXT("agent spawn point must be an object");
		return false;
	}
	if (!InJson->TryGetStringField(TEXT("id"), OutPoint.Id) || OutPoint.Id.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("agent spawn point.id is required");
		return false;
	}
	OutPoint.Id = OutPoint.Id.TrimStartAndEnd();
	const TSharedPtr<FJsonObject>* TransformJson = nullptr;
	if (InJson->TryGetObjectField(TEXT("transform"), TransformJson) && TransformJson)
	{
		if (!FEpicodeTransform::FromJson(*TransformJson, OutPoint.Transform, OutError))
		{
			return false;
		}
	}
	CopyUnknownFields(InJson, { TEXT("id"), TEXT("transform") }, OutPoint.Extra);
	return true;
}

TSharedPtr<FJsonObject> FEpicodeAgentRuntime::ToJson() const
{
	TSharedPtr<FJsonObject> Result = Extra.IsValid() ? MakeShared<FJsonObject>(*Extra) : MakeShared<FJsonObject>();
	WriteStringArray(Result, TEXT("controllableCharacterIds"), ControllableCharacterIds);
	WriteStringArray(Result, TEXT("allowedActions"), AllowedActions);
	TArray<TSharedPtr<FJsonValue>> SpawnValues;
	for (const FEpicodeAgentSpawnPoint& SpawnPoint : SpawnPoints)
	{
		SpawnValues.Add(MakeShared<FJsonValueObject>(SpawnPoint.ToJson()));
	}
	Result->SetArrayField(TEXT("spawnPoints"), SpawnValues);
	return Result;
}

bool FEpicodeAgentRuntime::FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeAgentRuntime& OutRuntime, FString& OutError)
{
	if (!InJson.IsValid())
	{
		OutError = TEXT("agent runtime must be an object");
		return false;
	}
	OutRuntime.ControllableCharacterIds = ReadStringArray(InJson, TEXT("controllableCharacterIds"));
	OutRuntime.AllowedActions = ReadStringArray(InJson, TEXT("allowedActions"));
	const TArray<TSharedPtr<FJsonValue>>* SpawnValues = nullptr;
	if (InJson->TryGetArrayField(TEXT("spawnPoints"), SpawnValues) && SpawnValues)
	{
		for (const TSharedPtr<FJsonValue>& Value : *SpawnValues)
		{
			FEpicodeAgentSpawnPoint SpawnPoint;
			if (!FEpicodeAgentSpawnPoint::FromJson(Value->AsObject(), SpawnPoint, OutError))
			{
				return false;
			}
			OutRuntime.SpawnPoints.Add(SpawnPoint);
		}
	}
	CopyUnknownFields(InJson, { TEXT("controllableCharacterIds"), TEXT("allowedActions"), TEXT("spawnPoints") }, OutRuntime.Extra);
	return true;
}

TSharedPtr<FJsonObject> FEpicodeViewerRuntime::ToJson() const
{
	TSharedPtr<FJsonObject> Result = Extra.IsValid() ? MakeShared<FJsonObject>(*Extra) : MakeShared<FJsonObject>();
	WriteStringArray(Result, TEXT("cameraIds"), CameraIds);
	Result->SetStringField(TEXT("defaultCameraId"), DefaultCameraId);
	Result->SetObjectField(TEXT("spawnTransform"), SpawnTransform.ToJson());
	Result->SetBoolField(TEXT("showOverlay"), bShowOverlay);
	return Result;
}

bool FEpicodeViewerRuntime::FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeViewerRuntime& OutRuntime, FString& OutError)
{
	if (!InJson.IsValid())
	{
		OutError = TEXT("viewer runtime must be an object");
		return false;
	}
	OutRuntime.CameraIds = ReadStringArray(InJson, TEXT("cameraIds"));
	InJson->TryGetStringField(TEXT("defaultCameraId"), OutRuntime.DefaultCameraId);
	const TSharedPtr<FJsonObject>* TransformJson = nullptr;
	if (InJson->TryGetObjectField(TEXT("spawnTransform"), TransformJson) && TransformJson)
	{
		if (!FEpicodeTransform::FromJson(*TransformJson, OutRuntime.SpawnTransform, OutError))
		{
			return false;
		}
	}
	InJson->TryGetBoolField(TEXT("showOverlay"), OutRuntime.bShowOverlay);
	CopyUnknownFields(InJson, { TEXT("cameraIds"), TEXT("defaultCameraId"), TEXT("spawnTransform"), TEXT("showOverlay") }, OutRuntime.Extra);
	return true;
}

TSharedPtr<FJsonObject> FEpicodeRuntimeMetadata::ToJson() const
{
	TSharedPtr<FJsonObject> Result = Extra.IsValid() ? MakeShared<FJsonObject>(*Extra) : MakeShared<FJsonObject>();
	Result->SetObjectField(TEXT("agent"), Agent.ToJson());
	Result->SetObjectField(TEXT("viewer"), Viewer.ToJson());
	return Result;
}

bool FEpicodeRuntimeMetadata::FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeRuntimeMetadata& OutMetadata, FString& OutError)
{
	if (!InJson.IsValid())
	{
		return true;
	}
	const TSharedPtr<FJsonObject>* AgentJson = nullptr;
	if (InJson->TryGetObjectField(TEXT("agent"), AgentJson) && AgentJson)
	{
		if (!FEpicodeAgentRuntime::FromJson(*AgentJson, OutMetadata.Agent, OutError))
		{
			return false;
		}
	}
	const TSharedPtr<FJsonObject>* ViewerJson = nullptr;
	if (InJson->TryGetObjectField(TEXT("viewer"), ViewerJson) && ViewerJson)
	{
		if (!FEpicodeViewerRuntime::FromJson(*ViewerJson, OutMetadata.Viewer, OutError))
		{
			return false;
		}
	}
	CopyUnknownFields(InJson, { TEXT("agent"), TEXT("viewer") }, OutMetadata.Extra);
	return true;
}

FEpicodeSceneDocument FEpicodeSceneDocument::CreateDefault(const FString& InName)
{
	FEpicodeSceneDocument Document;
	Document.SceneId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	Document.Name = InName;
	Document.LevelPath = FString::Printf(TEXT("/Game/Epicode/Scenes/%s"), *InName.Replace(TEXT(" "), TEXT("")));
	Document.Environment = MakeEpicodeDefaultEnvironment();
	Document.MarkModified();
	return Document;
}

TSharedPtr<FJsonObject> FEpicodeSceneDocument::ToJson() const
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schemaVersion"), SchemaVersion);
	Root->SetStringField(TEXT("sceneId"), SceneId);
	Root->SetStringField(TEXT("name"), Name);
	Root->SetStringField(TEXT("levelPath"), LevelPath);
	Root->SetStringField(TEXT("activeCameraId"), ActiveCameraId);
	Root->SetObjectField(TEXT("environment"), Environment.IsValid() ? Environment : MakeEpicodeDefaultEnvironment());
	Root->SetObjectField(TEXT("runtime"), Runtime.ToJson());

	TArray<TSharedPtr<FJsonValue>> EntityValues;
	for (const FEpicodeEntityRecord& Entity : Entities)
	{
		EntityValues.Add(MakeShared<FJsonValueObject>(Entity.ToJson()));
	}
	Root->SetArrayField(TEXT("entities"), EntityValues);
	return Root;
}

bool FEpicodeSceneMigration::Migrate(TSharedPtr<FJsonObject>& InOutJson, int32 FromVersion, int32 ToVersion, FString& OutError)
{
	if (!InOutJson.IsValid())
	{
		OutError = TEXT("scene document must be an object");
		return false;
	}
	if (FromVersion < 0 || ToVersion < FromVersion || ToVersion > FEpicodeSceneDocument::CurrentSchemaVersion)
	{
		OutError = FString::Printf(TEXT("Cannot migrate scene schema from %d to %d."), FromVersion, ToVersion);
		return false;
	}

	int32 Version = FromVersion;
	if (Version < 1)
	{
		// Version 0 documents predate schemaVersion and could store entities under "actors".
		if (!InOutJson->HasField(TEXT("entities")) && InOutJson->HasField(TEXT("actors")))
		{
			const TArray<TSharedPtr<FJsonValue>>* LegacyEntities = nullptr;
			if (InOutJson->TryGetArrayField(TEXT("actors"), LegacyEntities) && LegacyEntities)
			{
				InOutJson->SetArrayField(TEXT("entities"), *LegacyEntities);
			}
		}
		if (!InOutJson->HasField(TEXT("entities")))
		{
			InOutJson->SetArrayField(TEXT("entities"), {});
		}
		if (!InOutJson->HasField(TEXT("environment")))
		{
			InOutJson->SetObjectField(TEXT("environment"), MakeEpicodeDefaultEnvironment());
		}
		if (!InOutJson->HasField(TEXT("activeCameraId")))
		{
			InOutJson->SetStringField(TEXT("activeCameraId"), FString());
		}
		InOutJson->SetNumberField(TEXT("schemaVersion"), 1);
		Version = 1;
	}
	if (Version < 2)
	{
		if (!InOutJson->HasField(TEXT("runtime")) || !InOutJson->HasTypedField<EJson::Object>(TEXT("runtime")))
		{
			InOutJson->SetObjectField(TEXT("runtime"), MakeShared<FJsonObject>());
		}
		InOutJson->SetNumberField(TEXT("schemaVersion"), 2);
		Version = 2;
	}

	if (Version != ToVersion)
	{
		OutError = FString::Printf(TEXT("No migration path from scene schema %d to %d."), Version, ToVersion);
		return false;
	}
	return true;
}

bool FEpicodeSceneDocument::FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeSceneDocument& OutDocument, FString& OutError)
{
	if (!InJson.IsValid())
	{
		OutError = TEXT("scene document must be an object");
		return false;
	}

	int32 Version = 0;
	InJson->TryGetNumberField(TEXT("schemaVersion"), Version);
	if (Version > CurrentSchemaVersion)
	{
		OutError = FString::Printf(TEXT("Unsupported scene schema version %d, this build supports up to %d."), Version, CurrentSchemaVersion);
		return false;
	}
	if (Version < CurrentSchemaVersion)
	{
		TSharedPtr<FJsonObject> MutableJson = InJson;
		if (!FEpicodeSceneMigration::Migrate(MutableJson, Version, CurrentSchemaVersion, OutError))
		{
			return false;
		}
	}
	OutDocument = FEpicodeSceneDocument();
	OutDocument.SchemaVersion = CurrentSchemaVersion;

	if (!InJson->TryGetStringField(TEXT("sceneId"), OutDocument.SceneId) || OutDocument.SceneId.IsEmpty())
	{
		OutDocument.SceneId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	}
	InJson->TryGetStringField(TEXT("name"), OutDocument.Name);
	if (OutDocument.Name.IsEmpty())
	{
		OutDocument.Name = TEXT("Untitled");
	}
	InJson->TryGetStringField(TEXT("levelPath"), OutDocument.LevelPath);
	InJson->TryGetStringField(TEXT("activeCameraId"), OutDocument.ActiveCameraId);
	const TSharedPtr<FJsonObject>* RuntimeJson = nullptr;
	if (InJson->TryGetObjectField(TEXT("runtime"), RuntimeJson) && RuntimeJson)
	{
		if (!FEpicodeRuntimeMetadata::FromJson(*RuntimeJson, OutDocument.Runtime, OutError))
		{
			return false;
		}
	}

	const TSharedPtr<FJsonObject>* EnvironmentJson = nullptr;
	if (InJson->TryGetObjectField(TEXT("environment"), EnvironmentJson) && EnvironmentJson)
	{
		OutDocument.Environment = *EnvironmentJson;
	}
	else
	{
		OutDocument.Environment = MakeEpicodeDefaultEnvironment();
	}

	const TArray<TSharedPtr<FJsonValue>>* EntityValues = nullptr;
	if (InJson->TryGetArrayField(TEXT("entities"), EntityValues) && EntityValues)
	{
		OutDocument.Entities.Reserve(EntityValues->Num());
		for (const TSharedPtr<FJsonValue>& EntityValue : *EntityValues)
		{
			const TSharedPtr<FJsonObject> EntityJson = EntityValue->AsObject();
			FEpicodeEntityRecord Entity;
			FString EntityError;
			if (!FEpicodeEntityRecord::FromJson(EntityJson, Entity, EntityError))
			{
				OutError = FString::Printf(TEXT("Invalid entity: %s"), *EntityError);
				return false;
			}
			OutDocument.Entities.Add(Entity);
		}
	}

	if (!OutDocument.Validate(OutError))
	{
		return false;
	}
	OutDocument.MarkModified();
	return true;
}

bool FEpicodeSceneDocument::Validate(FString& OutError) const
{
	if (SchemaVersion < 1 || SchemaVersion > CurrentSchemaVersion)
	{
		OutError = FString::Printf(TEXT("Invalid schemaVersion %d"), SchemaVersion);
		return false;
	}
	if (SceneId.IsEmpty() || Name.IsEmpty())
	{
		OutError = TEXT("sceneId and name are required");
		return false;
	}

	TSet<FGuid> SeenIds;
	TSet<FString> SeenSiblingNames;
	for (const FEpicodeEntityRecord& Entity : Entities)
	{
		if (!Entity.IsValid())
		{
			OutError = TEXT("entity is invalid");
			return false;
		}
		if (SeenIds.Contains(Entity.Id))
		{
			OutError = FString::Printf(TEXT("Duplicate entity id %s"), *Entity.Id.ToString());
			return false;
		}
		SeenIds.Add(Entity.Id);
		const FString SiblingKey = FString::Printf(TEXT("%s|%s"), *Entity.ParentId.ToString(), *Entity.Name.ToLower());
		if (SeenSiblingNames.Contains(SiblingKey))
		{
			OutError = FString::Printf(TEXT("Duplicate sibling name %s"), *Entity.Name);
			return false;
		}
		SeenSiblingNames.Add(SiblingKey);
	}

	for (const FEpicodeEntityRecord& Entity : Entities)
	{
		if (Entity.ParentId.IsValid() && !SeenIds.Contains(Entity.ParentId))
		{
			OutError = FString::Printf(TEXT("Entity %s references missing parent %s"), *Entity.Name, *Entity.ParentId.ToString());
			return false;
		}
		if (Entity.ParentId.IsValid() && IsDescendant(Entity.Id, Entity.ParentId))
		{
			OutError = FString::Printf(TEXT("Entity %s has a cyclic parent chain"), *Entity.Name);
			return false;
		}
	}
	return ValidateRuntimeReferences(OutError);
}

bool FEpicodeSceneDocument::ValidateRuntimeReferences(FString& OutError) const
{
	TMap<FString, const FEpicodeEntityRecord*> ById;
	for (const FEpicodeEntityRecord& Entity : Entities)
	{
		ById.Add(Entity.Id.ToString(EGuidFormats::DigitsWithHyphens), &Entity);
	}

	TSet<FString> SeenCharacters;
	for (const FString& CharacterId : Runtime.Agent.ControllableCharacterIds)
	{
		if (CharacterId.IsEmpty())
		{
			OutError = TEXT("EMPTY_AGENT_CHARACTER: controllable character id is empty");
			return false;
		}
		if (SeenCharacters.Contains(CharacterId))
		{
			OutError = FString::Printf(TEXT("DUPLICATE_AGENT_CHARACTER: %s"), *CharacterId);
			return false;
		}
		SeenCharacters.Add(CharacterId);
		const FEpicodeEntityRecord* const* Found = ById.Find(CharacterId);
		if (!Found || !*Found)
		{
			OutError = FString::Printf(TEXT("AGENT_CHARACTER_NOT_FOUND: %s"), *CharacterId);
			return false;
		}
		if ((*Found)->Type != EEpicodeEntityType::Character)
		{
			OutError = FString::Printf(TEXT("AGENT_CHARACTER_TYPE_INVALID: %s is not a character"), *CharacterId);
			return false;
		}
	}

	TSet<FString> SeenSpawnPoints;
	for (const FEpicodeAgentSpawnPoint& SpawnPoint : Runtime.Agent.SpawnPoints)
	{
		const FString SpawnId = SpawnPoint.Id.TrimStartAndEnd();
		if (SpawnId.IsEmpty())
		{
			OutError = TEXT("EMPTY_AGENT_SPAWN_ID: agent spawn point id is empty");
			return false;
		}
		if (SeenSpawnPoints.Contains(SpawnId))
		{
			OutError = FString::Printf(TEXT("DUPLICATE_AGENT_SPAWN_ID: %s"), *SpawnId);
			return false;
		}
		if (!SpawnPoint.Transform.IsFinite())
		{
			OutError = FString::Printf(TEXT("INVALID_AGENT_SPAWN_TRANSFORM: %s"), *SpawnId);
			return false;
		}
		SeenSpawnPoints.Add(SpawnId);
	}

	TSet<FString> SeenActions;
	for (const FString& Action : Runtime.Agent.AllowedActions)
	{
		const FString Trimmed = Action.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			OutError = TEXT("EMPTY_AGENT_ACTION: allowed action is empty");
			return false;
		}
		if (SeenActions.Contains(Trimmed))
		{
			OutError = FString::Printf(TEXT("DUPLICATE_AGENT_ACTION: %s"), *Trimmed);
			return false;
		}
		SeenActions.Add(Trimmed);
	}

	TSet<FString> SeenCameras;
	for (const FString& CameraId : Runtime.Viewer.CameraIds)
	{
		if (CameraId.IsEmpty())
		{
			OutError = TEXT("EMPTY_VIEWER_CAMERA: viewer camera id is empty");
			return false;
		}
		if (SeenCameras.Contains(CameraId))
		{
			OutError = FString::Printf(TEXT("DUPLICATE_VIEWER_CAMERA: %s"), *CameraId);
			return false;
		}
		SeenCameras.Add(CameraId);
		const FEpicodeEntityRecord* const* Found = ById.Find(CameraId);
		if (!Found || !*Found)
		{
			OutError = FString::Printf(TEXT("VIEWER_CAMERA_NOT_FOUND: %s"), *CameraId);
			return false;
		}
		if ((*Found)->Type != EEpicodeEntityType::Camera)
		{
			OutError = FString::Printf(TEXT("VIEWER_CAMERA_TYPE_INVALID: %s is not a camera"), *CameraId);
			return false;
		}
	}

	if (!Runtime.Viewer.DefaultCameraId.IsEmpty())
	{
		if (!SeenCameras.Contains(Runtime.Viewer.DefaultCameraId))
		{
			OutError = FString::Printf(TEXT("VIEWER_DEFAULT_CAMERA_NOT_LISTED: %s"), *Runtime.Viewer.DefaultCameraId);
			return false;
		}
		const FEpicodeEntityRecord* const* Found = ById.Find(Runtime.Viewer.DefaultCameraId);
		if (!Found || !*Found || (*Found)->Type != EEpicodeEntityType::Camera)
		{
			OutError = FString::Printf(TEXT("VIEWER_DEFAULT_CAMERA_INVALID: %s"), *Runtime.Viewer.DefaultCameraId);
			return false;
		}
	}
	if (!Runtime.Viewer.SpawnTransform.IsFinite())
	{
		OutError = TEXT("INVALID_VIEWER_SPAWN_TRANSFORM: viewer spawn transform is not finite");
		return false;
	}
	return true;
}

bool FEpicodeSceneDocument::ValidateForRuntime(FString& OutError) const
{
	if (!Validate(OutError))
	{
		return false;
	}
	if (LevelPath.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("LEVEL_PATH_MISSING: runtime scene requires levelPath");
		return false;
	}
	return true;
}

FEpicodeEntityRecord* FEpicodeSceneDocument::FindEntity(const FGuid& InId)
{
	return Entities.FindByPredicate([&InId](const FEpicodeEntityRecord& Entity) { return Entity.Id == InId; });
}

const FEpicodeEntityRecord* FEpicodeSceneDocument::FindEntity(const FGuid& InId) const
{
	return Entities.FindByPredicate([&InId](const FEpicodeEntityRecord& Entity) { return Entity.Id == InId; });
}

FEpicodeEntityRecord* FEpicodeSceneDocument::FindEntity(const FString& InId)
{
	FGuid Parsed;
	return FGuid::Parse(InId, Parsed) ? FindEntity(Parsed) : nullptr;
}

const FEpicodeEntityRecord* FEpicodeSceneDocument::FindEntity(const FString& InId) const
{
	FGuid Parsed;
	return FGuid::Parse(InId, Parsed) ? FindEntity(Parsed) : nullptr;
}

TArray<FGuid> FEpicodeSceneDocument::GetChildren(const FGuid& InParentId) const
{
	TArray<FGuid> Result;
	for (const FEpicodeEntityRecord& Entity : Entities)
	{
		if (Entity.ParentId == InParentId)
		{
			Result.Add(Entity.Id);
		}
	}
	return Result;
}

bool FEpicodeSceneDocument::IsDescendant(const FGuid& InAncestor, const FGuid& InCandidate) const
{
	if (!InCandidate.IsValid())
	{
		return false;
	}
	const FEpicodeEntityRecord* Candidate = FindEntity(InCandidate);
	while (Candidate && Candidate->ParentId.IsValid())
	{
		if (Candidate->ParentId == InAncestor)
		{
			return true;
		}
		Candidate = FindEntity(Candidate->ParentId);
	}
	return false;
}

FString FEpicodeSceneDocument::MakeUniqueName(const FString& InBaseName, const FGuid& InParentId) const
{
	TSet<FString> Names;
	for (const FEpicodeEntityRecord& Entity : Entities)
	{
		if (Entity.ParentId == InParentId)
		{
			Names.Add(Entity.Name.ToLower());
		}
	}
	if (!Names.Contains(InBaseName.ToLower()))
	{
		return InBaseName;
	}
	for (int32 Index = 1; Index < 100000; ++Index)
	{
		const FString Candidate = FString::Printf(TEXT("%s_%d"), *InBaseName, Index);
		if (!Names.Contains(Candidate.ToLower()))
		{
			return Candidate;
		}
	}
	return FString::Printf(TEXT("%s_%s"), *InBaseName, *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
}

FString FEpicodeSceneDocument::MakeUniqueEntityName(const FString& InBaseName) const
{
	return MakeUniqueName(InBaseName, FGuid());
}

void FEpicodeSceneDocument::RemoveEntityCascade(const FGuid& InId, TArray<FString>& OutRemovedIds)
{
	TArray<FGuid> ToRemove;
	ToRemove.Add(InId);
	for (int32 Index = 0; Index < ToRemove.Num(); ++Index)
	{
		ToRemove.Append(GetChildren(ToRemove[Index]));
	}
	for (const FGuid& Id : ToRemove)
	{
		if (const FEpicodeEntityRecord* Entity = FindEntity(Id))
		{
			OutRemovedIds.Add(Id.ToString(EGuidFormats::DigitsWithHyphens));
		}
	}
	Entities.RemoveAll([&ToRemove](const FEpicodeEntityRecord& Entity) { return ToRemove.Contains(Entity.Id); });
}

bool FEpicodeSceneDocument::DuplicateEntity(const FGuid& InId, FGuid& OutNewId, const FVector& InOffset, FString& OutError)
{
	const FEpicodeEntityRecord* Source = FindEntity(InId);
	if (!Source)
	{
		OutError = TEXT("entity not found");
		return false;
	}
	FEpicodeEntityRecord Copy = *Source;
	Copy.Id = FGuid::NewGuid();
	Copy.Name = MakeUniqueName(Source->Name, Source->ParentId);
	Copy.Transform.Location += InOffset;
	if (!Copy.IsValid())
	{
		OutError = TEXT("duplicated entity is invalid");
		return false;
	}
	Entities.Add(Copy);
	OutNewId = Copy.Id;
	return true;
}

bool FEpicodeSceneDocument::ReparentEntity(const FGuid& InId, const FGuid& InNewParent, bool bKeepWorldTransform, FString& OutError)
{
	FEpicodeEntityRecord* Entity = FindEntity(InId);
	if (!Entity)
	{
		OutError = TEXT("entity not found");
		return false;
	}
	if (InNewParent.IsValid())
	{
		if (!FindEntity(InNewParent))
		{
			OutError = TEXT("new parent not found");
			return false;
		}
		if (InId == InNewParent || IsDescendant(InId, InNewParent))
		{
			OutError = TEXT("reparent would create a cycle");
			return false;
		}
	}
	const FEpicodeTransform World = ComputeWorldTransform(InId);
	Entity->ParentId = InNewParent;
	Entity->Name = MakeUniqueName(Entity->Name, InNewParent);
	if (bKeepWorldTransform)
	{
		Entity->Transform = ComputeLocalTransform(InId, InNewParent, World);
	}
	return true;
}

FEpicodeTransform FEpicodeSceneDocument::ComputeWorldTransform(const FGuid& InId) const
{
	const FEpicodeEntityRecord* Entity = FindEntity(InId);
	if (!Entity)
	{
		return FEpicodeTransform();
	}
	FTransform Result = Entity->Transform.ToFTransform();
	if (Entity->ParentId.IsValid())
	{
		const FEpicodeTransform ParentWorld = ComputeWorldTransform(Entity->ParentId);
		Result = Result * ParentWorld.ToFTransform();
	}
	return FEpicodeTransform::FromFTransform(Result);
}

FEpicodeTransform FEpicodeSceneDocument::ComputeLocalTransform(const FGuid& InId, const FGuid& InParentId, const FEpicodeTransform& InWorld) const
{
	if (!InParentId.IsValid())
	{
		return InWorld;
	}
	const FEpicodeTransform ParentWorld = ComputeWorldTransform(InParentId);
	return FEpicodeTransform::FromFTransform(InWorld.ToFTransform().GetRelativeTransform(ParentWorld.ToFTransform()));
}

void FEpicodeSceneDocument::MarkModified()
{
	++Revision;
}
