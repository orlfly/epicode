#include "EpicodeNetworkSceneLoader.h"

#include "EpicodeSceneLibrary.h"
#include "EpicodeSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

TSharedPtr<FJsonObject> FEpicodeNetworkSceneSummary::ToJson() const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("sceneId"), SceneId);
	Json->SetStringField(TEXT("name"), Name);
	Json->SetStringField(TEXT("levelPath"), LevelPath);
	Json->SetStringField(TEXT("documentPath"), DocumentPath);
	Json->SetNumberField(TEXT("schemaVersion"), SchemaVersion);
	Json->SetNumberField(TEXT("entityCount"), EntityCount);
	TArray<TSharedPtr<FJsonValue>> Entities;
	for (const FString& EntityId : EntityIds)
	{
		Entities.Add(MakeShared<FJsonValueString>(EntityId));
	}
	Json->SetArrayField(TEXT("entityIds"), Entities);
	TArray<TSharedPtr<FJsonValue>> CharacterValues;
	for (const FEpicodeNetworkSceneCharacter& Character : Characters)
	{
		CharacterValues.Add(MakeShared<FJsonValueObject>(Character.ToJson()));
	}
	Json->SetArrayField(TEXT("characters"), CharacterValues);
	auto AddEntities = [&Json](const TCHAR* Field, const TArray<FEpicodeNetworkSceneEntity>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> EntityValues;
		for (const FEpicodeNetworkSceneEntity& Entity : Values)
		{
			EntityValues.Add(MakeShared<FJsonValueObject>(Entity.ToJson()));
		}
		Json->SetArrayField(Field, EntityValues);
	};
	AddEntities(TEXT("props"), Props);
	AddEntities(TEXT("lights"), Lights);
	AddEntities(TEXT("cameras"), Cameras);
	Json->SetObjectField(TEXT("environment"), Environment.IsValid() ? Environment : MakeShared<FJsonObject>());
	Json->SetObjectField(TEXT("runtime"), Runtime.IsValid() ? Runtime : MakeShared<FJsonObject>());
	return Json;
}

TSharedPtr<FJsonObject> FEpicodeNetworkSceneCharacter::ToJson() const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("entityId"), EntityId);
	Json->SetStringField(TEXT("meshPath"), MeshPath);
	Json->SetStringField(TEXT("replicationClass"), ReplicationClass);
	Json->SetObjectField(TEXT("transform"), Transform.ToJson());
	return Json;
}

TSharedPtr<FJsonObject> FEpicodeNetworkSceneEntity::ToJson() const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("entityId"), EntityId);
	Json->SetStringField(TEXT("type"), Type);
	Json->SetStringField(TEXT("name"), Name);
	Json->SetBoolField(TEXT("visible"), bVisible);
	Json->SetObjectField(TEXT("transform"), Transform.ToJson());
	Json->SetObjectField(TEXT("data"), Data.IsValid() ? Data : MakeShared<FJsonObject>());
	return Json;
}

bool FEpicodeNetworkSceneLoader::Load(const FString& InSceneId, FEpicodeNetworkSceneSummary& OutSummary, FString& OutError)
{
	if (InSceneId.IsEmpty())
	{
		OutError = TEXT("sceneId is required.");
		return false;
	}
	const FString Root = UEpicodeSettings::Get()->GetSceneRootPath();
	FEpicodeSceneLibraryEntry Entry;
	if (!FEpicodeSceneLibrary::Find(Root, InSceneId, Entry))
	{
		OutError = FString::Printf(TEXT("SCENE_NOT_FOUND: no scene '%s' under %s"), *InSceneId, *Root);
		return false;
	}
	if (Entry.SceneId != InSceneId)
	{
		OutError = FString::Printf(TEXT("SCENE_ID_MISMATCH: requested '%s' but found '%s'."), *InSceneId, *Entry.SceneId);
		return false;
	}
	FString RawText;
	if (!FFileHelper::LoadFileToString(RawText, *Entry.DocumentPath))
	{
		OutError = FString::Printf(TEXT("SCENE_UNREADABLE: cannot read %s"), *Entry.DocumentPath);
		return false;
	}
	TSharedPtr<FJsonObject> RawJson;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RawText);
	if (!FJsonSerializer::Deserialize(Reader, RawJson) || !RawJson.IsValid())
	{
		OutError = FString::Printf(TEXT("SCENE_INVALID: %s is not valid JSON."), *Entry.DocumentPath);
		return false;
	}
	int32 RawVersion = 0;
	RawJson->TryGetNumberField(TEXT("schemaVersion"), RawVersion);
	if (RawVersion > FEpicodeSceneDocument::CurrentSchemaVersion)
	{
		OutError = FString::Printf(TEXT("SCENE_VERSION_UNSUPPORTED: scene '%s' uses schema %d, runtime supports %d."),
			*InSceneId, RawVersion, FEpicodeSceneDocument::CurrentSchemaVersion);
		return false;
	}
	FEpicodeSceneDocument Document;
	if (!FEpicodeSceneLibrary::Load(Entry.DocumentPath, Document, OutError))
	{
		return false;
	}
	if (Document.SchemaVersion > FEpicodeSceneDocument::CurrentSchemaVersion)
	{
		OutError = FString::Printf(TEXT("SCENE_VERSION_UNSUPPORTED: scene '%s' uses schema %d, runtime supports %d."),
			*InSceneId, Document.SchemaVersion, FEpicodeSceneDocument::CurrentSchemaVersion);
		return false;
	}
	if (Document.SceneId != InSceneId)
	{
		OutError = FString::Printf(TEXT("SCENE_ID_MISMATCH: document contains '%s'."), *Document.SceneId);
		return false;
	}
	if (!Document.Validate(OutError))
	{
		return false;
	}

	OutSummary = FEpicodeNetworkSceneSummary();
	OutSummary.SceneId = Document.SceneId;
	OutSummary.Name = Document.Name;
	OutSummary.LevelPath = Document.LevelPath;
	OutSummary.DocumentPath = Entry.DocumentPath;
	OutSummary.SchemaVersion = Document.SchemaVersion;
	OutSummary.EntityCount = Document.Entities.Num();
	OutSummary.Environment = Document.Environment;
	OutSummary.Runtime = Document.Runtime.ToJson();
	for (const FEpicodeEntityRecord& Entity : Document.Entities)
	{
		OutSummary.EntityIds.Add(Entity.Id.ToString(EGuidFormats::DigitsWithHyphens));
		FEpicodeNetworkSceneEntity SummaryEntity;
		SummaryEntity.EntityId = Entity.Id.ToString(EGuidFormats::DigitsWithHyphens);
		SummaryEntity.Type = LexToString(Entity.Type);
		SummaryEntity.Name = Entity.Name;
		SummaryEntity.bVisible = Entity.bVisible;
		SummaryEntity.Transform = Document.ComputeWorldTransform(Entity.Id);
		SummaryEntity.Data = Entity.Data;
		if (Entity.Type == EEpicodeEntityType::Character)
		{
			FEpicodeNetworkSceneCharacter Character;
			Character.EntityId = Entity.Id.ToString(EGuidFormats::DigitsWithHyphens);
			Character.ReplicationClass = TEXT("high");
			Character.Transform = Document.ComputeWorldTransform(Entity.Id);
			if (Entity.Data.IsValid())
			{
				Entity.Data->TryGetStringField(TEXT("skeletalMesh"), Character.MeshPath);
			}
			OutSummary.Characters.Add(Character);
		}
		else if (Entity.Type == EEpicodeEntityType::Prop)
		{
			OutSummary.Props.Add(SummaryEntity);
		}
		else if (Entity.Type == EEpicodeEntityType::Light)
		{
			OutSummary.Lights.Add(SummaryEntity);
		}
		else if (Entity.Type == EEpicodeEntityType::Camera)
		{
			OutSummary.Cameras.Add(SummaryEntity);
		}
	}
	return true;
}
