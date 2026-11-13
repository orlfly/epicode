#include "EpicodeProductionValidation.h"

#include "Dom/JsonValue.h"
#include "EpicodeContentDigest.h"
#include "EpicodeSceneDocument.h"
#include "EpicodeSceneLibrary.h"
#include "EpicodeSandboxSettings.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	FString ReadCode(const FString& InError)
	{
		int32 Colon = INDEX_NONE;
		if (InError.FindChar(TEXT(':'), Colon))
		{
			return InError.Left(Colon);
		}
		return InError;
	}

	void ScanContentPaths(const TSharedPtr<FJsonValue>& InValue, const FString& InPath, FEpicodeProductionValidationResult& OutResult)
	{
		if (!InValue.IsValid())
		{
			return;
		}
		if (InValue->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Object = InValue->AsObject();
			if (!Object.IsValid())
			{
				return;
			}
			for (const auto& Pair : Object->Values)
			{
				const FString Key(*Pair.Key);
				const FString ChildPath = InPath + TEXT(".") + Key;
				if (Pair.Value.IsValid() && Pair.Value->Type == EJson::String && Key.Contains(TEXT("path"), ESearchCase::IgnoreCase))
				{
					const FString Value = Pair.Value->AsString();
					if (!FEpicodeProductionValidator::IsAllowedAssetPath(Value))
					{
						OutResult.AddIssue(TEXT("PATH_NOT_ALLOWED"), ChildPath, FString::Printf(TEXT("asset path escapes allowed roots: %s"), *Value));
					}
				}
				ScanContentPaths(Pair.Value, ChildPath, OutResult);
			}
		}
		else if (InValue->Type == EJson::Array)
		{
			for (int32 Index = 0; Index < InValue->AsArray().Num(); ++Index)
			{
				ScanContentPaths(InValue->AsArray()[Index], FString::Printf(TEXT("%s[%d]"), *InPath, Index), OutResult);
			}
		}
	}

	void ScanRuleIds(const TSharedPtr<FJsonValue>& InValue, const FString& InPath, TSet<FString>& InOutIds, FEpicodeProductionValidationResult& OutResult)
	{
		if (!InValue.IsValid())
		{
			return;
		}
		if (InValue->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Object = InValue->AsObject();
			if (!Object.IsValid())
			{
				return;
			}
			FString Id;
			if (Object->TryGetStringField(TEXT("id"), Id) && !Id.TrimStartAndEnd().IsEmpty())
			{
				if (InOutIds.Contains(Id.ToLower()))
				{
					OutResult.AddIssue(TEXT("DUPLICATE_RULE_ID"), InPath + TEXT(".id"), FString::Printf(TEXT("duplicate id %s"), *Id));
					return;
				}
				InOutIds.Add(Id.ToLower());
			}
			for (const auto& Pair : Object->Values)
			{
				ScanRuleIds(Pair.Value, InPath + TEXT(".") + FString(*Pair.Key), InOutIds, OutResult);
			}
		}
		else if (InValue->Type == EJson::Array)
		{
			for (int32 Index = 0; Index < InValue->AsArray().Num(); ++Index)
			{
				ScanRuleIds(InValue->AsArray()[Index], FString::Printf(TEXT("%s[%d]"), *InPath, Index), InOutIds, OutResult);
			}
		}
	}

	void ScanEntityReferences(const TSharedPtr<FJsonValue>& InValue, const FString& InPath, const FEpicodeSceneDocument& InScene, FEpicodeProductionValidationResult& OutResult)
	{
		if (!InValue.IsValid())
		{
			return;
		}
		if (InValue->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Object = InValue->AsObject();
			if (!Object.IsValid())
			{
				return;
			}
			for (const auto& Pair : Object->Values)
			{
				const FString Key(*Pair.Key);
				const FString ChildPath = InPath + TEXT(".") + Key;
				if (Pair.Value.IsValid() && Pair.Value->Type == EJson::String && Key.Equals(TEXT("entityId"), ESearchCase::IgnoreCase))
				{
					const FString EntityId = Pair.Value->AsString();
					if (!InScene.FindEntity(EntityId))
					{
						OutResult.AddIssue(TEXT("PRODUCTION_REFERENCE_NOT_FOUND"), ChildPath, FString::Printf(TEXT("entity %s does not exist"), *EntityId), EntityId);
					}
				}
				ScanEntityReferences(Pair.Value, ChildPath, InScene, OutResult);
			}
		}
		else if (InValue->Type == EJson::Array)
		{
			for (int32 Index = 0; Index < InValue->AsArray().Num(); ++Index)
			{
				ScanEntityReferences(InValue->AsArray()[Index], FString::Printf(TEXT("%s[%d]"), *InPath, Index), InScene, OutResult);
			}
		}
	}

	void ScanEntityReferences(const TSharedPtr<FJsonObject>& InObject, const FString& InPath, const FEpicodeSceneDocument& InScene, FEpicodeProductionValidationResult& OutResult)
	{
		ScanEntityReferences(MakeShared<FJsonValueObject>(InObject), InPath, InScene, OutResult);
	}
}

TSharedPtr<FJsonObject> FEpicodeProductionValidationIssue::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("code"), Code);
	Result->SetStringField(TEXT("path"), Path);
	Result->SetStringField(TEXT("message"), Message);
	if (!EntityId.IsEmpty())
	{
		Result->SetStringField(TEXT("entityId"), EntityId);
	}
	return Result;
}

void FEpicodeProductionValidationResult::AddIssue(const FString& Code, const FString& Path, const FString& Message, const FString& EntityId)
{
	FEpicodeProductionValidationIssue& Issue = Issues.AddDefaulted_GetRef();
	Issue.Code = Code;
	Issue.Path = Path;
	Issue.Message = Message;
	Issue.EntityId = EntityId;
	bSuccess = false;
}

FString FEpicodeProductionValidationResult::GetFirstCode() const
{
	return Issues.Num() > 0 ? Issues[0].Code : FString();
}

FString FEpicodeProductionValidationResult::GetFirstError() const
{
	if (Issues.Num() == 0)
	{
		return FString();
	}
	return FString::Printf(TEXT("%s: %s: %s"), *Issues[0].Code, *Issues[0].Path, *Issues[0].Message);
}

TSharedPtr<FJsonObject> FEpicodeProductionValidationResult::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bSuccess);
	Result->SetStringField(TEXT("sceneId"), SceneId);
	Result->SetNumberField(TEXT("sceneSchemaVersion"), SceneSchemaVersion);
	Result->SetNumberField(TEXT("entityCount"), EntityCount);
	Result->SetStringField(TEXT("sceneDigest"), SceneDigest);
	TArray<TSharedPtr<FJsonValue>> IssueValues;
	for (const FEpicodeProductionValidationIssue& Issue : Issues)
	{
		IssueValues.Add(MakeShared<FJsonValueObject>(Issue.ToJson()));
	}
	Result->SetArrayField(TEXT("issues"), IssueValues);
	return Result;
}

bool FEpicodeProductionValidator::IsAllowedAssetPath(const FString& InPath)
{
	const FString Path = InPath.TrimStartAndEnd();
	if (Path.IsEmpty())
	{
		return true;
	}
	if (Path.Contains(TEXT("://")))
	{
		return false;
	}
	if (Path.Len() >= 2 && Path[1] == TEXT(':'))
	{
		return false;
	}
	TArray<FString> Segments;
	Path.ParseIntoArray(Segments, TEXT("/"), true);
	for (const FString& Segment : Segments)
	{
		if (Segment == TEXT(".."))
		{
			return false;
		}
	}
	if (FPaths::IsRelative(Path))
	{
		return true;
	}
	return Path.StartsWith(TEXT("/Game/"), ESearchCase::CaseSensitive)
		|| Path.StartsWith(TEXT("/Engine/"), ESearchCase::CaseSensitive);
}

bool FEpicodeProductionValidator::ValidateDocument(const FEpicodeProductionDocument& InDocument, FEpicodeProductionValidationResult& OutResult)
{
	OutResult = FEpicodeProductionValidationResult();
	FString DocumentError;
	if (!InDocument.Validate(DocumentError))
	{
		OutResult.AddIssue(ReadCode(DocumentError), TEXT("production"), DocumentError);
	}

	if (InDocument.WorldRules.IsValid())
	{
		ScanContentPaths(MakeShared<FJsonValueObject>(InDocument.WorldRules), TEXT("worldRules"), OutResult);
	}
	if (InDocument.Narrative.IsValid())
	{
		ScanContentPaths(MakeShared<FJsonValueObject>(InDocument.Narrative), TEXT("narrative"), OutResult);
	}

	TSet<FString> RuleIds;
	if (InDocument.WorldRules.IsValid())
	{
		ScanRuleIds(MakeShared<FJsonValueObject>(InDocument.WorldRules), TEXT("worldRules"), RuleIds, OutResult);
	}
	if (InDocument.Narrative.IsValid())
	{
		ScanRuleIds(MakeShared<FJsonValueObject>(InDocument.Narrative), TEXT("narrative"), RuleIds, OutResult);
	}

	const UEpicodeSandboxSettings* Settings = UEpicodeSandboxSettings::Get();
	const TSharedPtr<FJsonObject>* Budgets = nullptr;
	if (Settings && InDocument.WorldRules.IsValid() && InDocument.WorldRules->TryGetObjectField(TEXT("budgets"), Budgets) && Budgets)
	{
		int32 MaxEntities = 0;
		int32 MaxEventsPerSecond = 0;
		if ((*Budgets)->TryGetNumberField(TEXT("maxEntities"), MaxEntities) && (MaxEntities <= 0 || MaxEntities > Settings->MaxEntities))
		{
			OutResult.AddIssue(TEXT("PRODUCTION_BUDGET_EXCEEDED"), TEXT("worldRules.budgets.maxEntities"), FString::Printf(TEXT("maxEntities %d exceeds allowed %d"), MaxEntities, Settings->MaxEntities));
		}
		if ((*Budgets)->TryGetNumberField(TEXT("maxEventsPerSecond"), MaxEventsPerSecond) && (MaxEventsPerSecond <= 0 || MaxEventsPerSecond > Settings->MaxEventsPerSecond))
		{
			OutResult.AddIssue(TEXT("PRODUCTION_BUDGET_EXCEEDED"), TEXT("worldRules.budgets.maxEventsPerSecond"), FString::Printf(TEXT("maxEventsPerSecond %d exceeds allowed %d"), MaxEventsPerSecond, Settings->MaxEventsPerSecond));
		}
	}
	return OutResult.bSuccess;
}

bool FEpicodeProductionValidator::ValidateBinding(const FEpicodeProductionDocument& InDocument, const FString& InSceneRoot, FEpicodeProductionValidationResult& OutResult)
{
	ValidateDocument(InDocument, OutResult);
	OutResult.SceneId = InDocument.SceneId;

	if (InDocument.SceneId.TrimStartAndEnd().IsEmpty())
	{
		return false;
	}

	FEpicodeSceneLibraryEntry Entry;
	if (!FEpicodeSceneLibrary::Find(InSceneRoot, InDocument.SceneId, Entry))
	{
		OutResult.AddIssue(TEXT("SCENE_NOT_FOUND"), TEXT("sceneId"), FString::Printf(TEXT("scene %s was not found under %s"), *InDocument.SceneId, *InSceneRoot));
		return false;
	}

	FString SceneJson;
	if (!FFileHelper::LoadFileToString(SceneJson, *Entry.DocumentPath))
	{
		OutResult.AddIssue(TEXT("SCENE_INVALID"), TEXT("sceneId"), FString::Printf(TEXT("unable to read scene %s"), *Entry.DocumentPath));
		return false;
	}
	OutResult.SceneDigest = FEpicodeContentDigest::HashString(SceneJson);

	TSharedPtr<FJsonObject> RawScene;
	const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(SceneJson);
	if (!FJsonSerializer::Deserialize(Reader, RawScene) || !RawScene.IsValid())
	{
		OutResult.AddIssue(TEXT("SCENE_INVALID"), TEXT("sceneId"), TEXT("scene document is not valid JSON"));
		return false;
	}
	int32 RawVersion = 0;
	RawScene->TryGetNumberField(TEXT("schemaVersion"), RawVersion);
	if (RawVersion > FEpicodeSceneDocument::CurrentSchemaVersion)
	{
		OutResult.AddIssue(TEXT("SCENE_VERSION_UNSUPPORTED"), TEXT("sceneId"), FString::Printf(TEXT("scene schema %d is newer than supported %d"), RawVersion, FEpicodeSceneDocument::CurrentSchemaVersion));
		return false;
	}

	FEpicodeSceneDocument Scene;
	FString SceneError;
	if (!FEpicodeSceneLibrary::Load(Entry.DocumentPath, Scene, SceneError))
	{
		OutResult.AddIssue(TEXT("SCENE_INVALID"), TEXT("sceneId"), SceneError);
		return false;
	}
	OutResult.SceneSchemaVersion = Scene.SchemaVersion;
	OutResult.EntityCount = Scene.Entities.Num();

	TSet<FString> Slots;
	for (int32 Index = 0; Index < InDocument.Cast.Num(); ++Index)
	{
		const FEpicodeProductionCastMember& Member = InDocument.Cast[Index];
		const FString Path = FString::Printf(TEXT("cast[%d]"), Index);
		if (Slots.Contains(Member.Slot.ToLower()))
		{
			OutResult.AddIssue(TEXT("DUPLICATE_CAST_SLOT"), Path + TEXT(".slot"), FString::Printf(TEXT("slot %s is duplicated"), *Member.Slot));
			continue;
		}
		Slots.Add(Member.Slot.ToLower());

		const FEpicodeEntityRecord* Entity = Scene.FindEntity(Member.EntityId);
		if (!Entity)
		{
			OutResult.AddIssue(TEXT("PRODUCTION_REFERENCE_NOT_FOUND"), Path + TEXT(".entityId"), FString::Printf(TEXT("entity %s does not exist"), *Member.EntityId), Member.EntityId);
			continue;
		}
		if (Entity->Type != EEpicodeEntityType::Character)
		{
			OutResult.AddIssue(TEXT("CAST_ENTITY_TYPE_INVALID"), Path + TEXT(".entityId"), FString::Printf(TEXT("entity %s is %s, expected Character"), *Member.EntityId, *Entity->GetTypeName()), Member.EntityId);
		}
		if (InDocument.FindCastByEntity(Member.EntityId) != &Member)
		{
			OutResult.AddIssue(TEXT("DUPLICATE_CAST_ENTITY"), Path + TEXT(".entityId"), FString::Printf(TEXT("entity %s is referenced by multiple cast slots"), *Member.EntityId), Member.EntityId);
		}
	}

	ScanEntityReferences(InDocument.WorldRules, TEXT("worldRules"), Scene, OutResult);
	ScanEntityReferences(InDocument.Narrative, TEXT("narrative"), Scene, OutResult);
	return OutResult.bSuccess;
}
