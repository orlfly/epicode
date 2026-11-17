#include "EpicodeFocusedShellModel.h"

#include "EpicodeEngineSubsystem.h"
#include "EpicodeSceneSubsystem.h"
#include "EpicodeTypes.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString TypeLabel(EEpicodeEntityType Type)
	{
		switch (Type)
		{
		case EEpicodeEntityType::Character:
			return TEXT("角色");
		case EEpicodeEntityType::Prop:
			return TEXT("物品");
		case EEpicodeEntityType::Light:
			return TEXT("灯光");
		case EEpicodeEntityType::Camera:
			return TEXT("摄像");
		default:
			return TEXT("分组");
		}
	}
}

UEpicodeSceneSubsystem* FEpicodeFocusedShellModel::GetSceneSubsystem() const
{
	return UEpicodeSceneSubsystem::GetPrimary();
}

void FEpicodeFocusedShellModel::Refresh()
{
	TMap<FString, TArray<FString>> PreviousSceneTags;
	for (const FEpicodeFocusedSceneRow& Row : Scenes)
	{
		PreviousSceneTags.Add(Row.Id, Row.Tags);
	}
	Entities.Reset();
	Scenes.Reset();
	bHasScene = false;
	bDirty = false;
	Revision = 0;
	SceneName.Reset();
	SceneId.Reset();

	UEpicodeSceneSubsystem* Scene = GetSceneSubsystem();
	if (!Scene)
	{
		SelectedEntityId.Invalidate();
		return;
	}

	for (const FEpicodeSceneLibraryEntry& Entry : Scene->ListScenes())
	{
		FEpicodeFocusedSceneRow Row;
		Row.Id = Entry.SceneId;
		Row.Name = Entry.Name;
		Row.bCurrent = Entry.bCurrent;
		Row.bAvailable = Entry.bAvailable;
		if (const FEpicodeLibraryEntry* IndexEntry = LocalLibrary.FindEntry(
			FString::Printf(TEXT("scene:%s"), *Entry.SceneId)))
		{
			Row.Tags = IndexEntry->Tags;
		}
		else if (const TArray<FString>* OldTags = PreviousSceneTags.Find(Entry.SceneId))
		{
			Row.Tags = *OldTags;
		}
		Scenes.Add(MoveTemp(Row));
	}

	if (!Scene->HasScene())
	{
		SelectedEntityId.Invalidate();
		return;
	}

	bHasScene = true;
	bDirty = Scene->IsDirty();
	Revision = Scene->GetDocument().Revision;
	SceneName = Scene->GetCurrentSceneName();
	SceneId = Scene->GetCurrentSceneId();

	for (const FEpicodeEntityRecord& Record : Scene->GetDocument().Entities)
	{
		FEpicodeFocusedEntityRow Row;
		Row.Id = Record.Id;
		Row.Name = Record.Name;
		Row.Type = Record.GetTypeName();
		Row.TypeLabel = TypeLabel(Record.Type);
		Row.bVisible = Record.bVisible;
		Entities.Add(MoveTemp(Row));
	}

	if (SelectedEntityId.IsValid() && !FindEntity(SelectedEntityId))
	{
		SelectedEntityId.Invalidate();
	}
}

void FEpicodeFocusedShellModel::RefreshAssets()
{
	LoadHiddenAssets();
	Assets.Reset();

	FString Error;
	if (!LocalLibrary.Refresh(Error))
	{
		LastMessage = FString::Printf(TEXT("本地库索引刷新失败：%s"), *Error);
		return;
	}
	if (!LocalLibrary.GetLastWarning().IsEmpty())
	{
		LastMessage = LocalLibrary.GetLastWarning();
	}

	for (const FEpicodeLibraryEntry& Entry : LocalLibrary.GetEntries())
	{
		if (Entry.Type == EEpicodeLibraryEntryType::Scene
			|| HiddenAssetPaths.Contains(Entry.Path))
		{
			continue;
		}
		FEpicodeFocusedAssetRow Row;
		Row.Name = Entry.Name;
		Row.AssetPath = Entry.Path;
		Row.Type = Entry.Type == EEpicodeLibraryEntryType::Character
			? TEXT("Character")
			: TEXT("Prop");
		Row.bAvailable = Entry.bAvailable;
		Row.Error = Entry.Error;
		Row.Tags = Entry.Tags;
		Assets.Add(MoveTemp(Row));
	}

	Assets.Sort([](const FEpicodeFocusedAssetRow& Left, const FEpicodeFocusedAssetRow& Right)
	{
		return Left.Name < Right.Name;
	});

	for (FEpicodeFocusedSceneRow& Row : Scenes)
	{
		if (const FEpicodeLibraryEntry* Entry = FindLibraryScene(Row.Id))
		{
			Row.Tags = Entry->Tags;
		}
	}
}

namespace
{
	FString GetHiddenAssetsPath()
	{
		return FPaths::Combine(
			FPaths::ProjectSavedDir(), TEXT("Epicode/Editor/asset-library.json"));
	}
}

void FEpicodeFocusedShellModel::LoadHiddenAssets()
{
	if (bHiddenAssetsLoaded)
	{
		return;
	}
	bHiddenAssetsLoaded = true;
	HiddenAssetPaths.Reset();

	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *GetHiddenAssetsPath()))
	{
		return;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return;
	}
	const TArray<TSharedPtr<FJsonValue>>* Hidden = nullptr;
	if (!Root->TryGetArrayField(TEXT("hidden"), Hidden) || !Hidden)
	{
		return;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Hidden)
	{
		if (Value.IsValid() && Value->Type == EJson::String)
		{
			HiddenAssetPaths.Add(Value->AsString());
		}
	}
}

void FEpicodeFocusedShellModel::SaveHiddenAssets() const
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), 1);
	TArray<TSharedPtr<FJsonValue>> Hidden;
	for (const FString& AssetPath : HiddenAssetPaths)
	{
		Hidden.Add(MakeShared<FJsonValueString>(AssetPath));
	}
	Root->SetArrayField(TEXT("hidden"), Hidden);

	const FString Path = GetHiddenAssetsPath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	FString Text;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	FFileHelper::SaveStringToFile(Text, *Path);
}

void FEpicodeFocusedShellModel::HideAsset(const FString& AssetPath)
{
	LoadHiddenAssets();
	HiddenAssetPaths.Add(AssetPath);
	SaveHiddenAssets();
	RefreshAssets();
}

void FEpicodeFocusedShellModel::RestoreHiddenAssets()
{
	bHiddenAssetsLoaded = true;
	HiddenAssetPaths.Reset();
	SaveHiddenAssets();
	RefreshAssets();
}

FEpicodeCommandResult FEpicodeFocusedShellModel::Execute(
	const FString& Method, const TSharedPtr<FJsonObject>& Params)
{
	FEpicodeCommandResult Result;
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	if (!Engine || !Engine->GetCommandRegistry())
	{
		Result = FEpicodeCommandResult::Failure(
			TEXT("NOT_INITIALIZED"), TEXT("Epicode engine subsystem is not ready."));
	}
	else
	{
		FEpicodeCommandContext Context;
		Context.World = UEpicodeSceneSubsystem::GetPrimaryWorld();
		Context.Source = TEXT("focused");
		Result = Engine->ExecuteCommand(Method, Params, Context);
	}

	if (Result.bSuccess)
	{
		LastMessage = FString::Printf(TEXT("%s 完成"), *Method);
	}
	else
	{
		LastMessage = FString::Printf(
			TEXT("%s 失败：%s"), *Method,
			Result.ErrorMessage.IsEmpty() ? *Result.ErrorCode : *Result.ErrorMessage);
	}

	Refresh();
	return Result;
}

FEpicodeCommandResult FEpicodeFocusedShellModel::QueryCommand(
	const FString& Method, const TSharedPtr<FJsonObject>& Params) const
{
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	if (!Engine || !Engine->GetCommandRegistry())
	{
		return FEpicodeCommandResult::Failure(
			TEXT("NOT_INITIALIZED"), TEXT("Epicode engine subsystem is not ready."));
	}
	FEpicodeCommandContext Context;
	Context.World = UEpicodeSceneSubsystem::GetPrimaryWorld();
	Context.Source = TEXT("focused");
	return Engine->ExecuteCommand(Method, Params, Context);
}

const FEpicodeFocusedEntityRow* FEpicodeFocusedShellModel::FindEntity(const FGuid& InId) const
{
	return Entities.FindByPredicate([&InId](const FEpicodeFocusedEntityRow& Row)
	{
		return Row.Id == InId;
	});
}

const FEpicodeEntityRecord* FEpicodeFocusedShellModel::GetSelectedEntityRecord() const
{
	UEpicodeSceneSubsystem* Scene = GetSceneSubsystem();
	if (!Scene || !SelectedEntityId.IsValid())
	{
		return nullptr;
	}
	return Scene->FindEntity(SelectedEntityId);
}

const FEpicodeLibraryEntry* FEpicodeFocusedShellModel::FindLibraryScene(
	const FString& InSceneId) const
{
	return LocalLibrary.FindEntry(FString::Printf(TEXT("scene:%s"), *InSceneId));
}

FGuid FEpicodeFocusedShellModel::GetActiveCameraId() const
{
	UEpicodeSceneSubsystem* Scene = GetSceneSubsystem();
	return Scene ? Scene->GetActiveCameraId() : FGuid();
}

FString FEpicodeFocusedShellModel::GetSelectedEntityLabel() const
{
	const FEpicodeFocusedEntityRow* Row = FindEntity(SelectedEntityId);
	if (!Row)
	{
		return TEXT("无选择");
	}
	return FString::Printf(TEXT("%s · %s"), *Row->TypeLabel, *Row->Name);
}

FString FEpicodeFocusedShellModel::GetSignature() const
{
	return FString::Printf(
		TEXT("%s|%llu|%d|%d|%s|%d"),
		*SceneId,
		Revision,
		bHasScene ? 1 : 0,
		bDirty ? 1 : 0,
		*SelectedEntityId.ToString(EGuidFormats::Digits),
		Scenes.Num());
}

FString FEpicodeFocusedShellModel::GetAssetSignature() const
{
	FString Signature;
	for (const FEpicodeFocusedAssetRow& Asset : Assets)
	{
		Signature += Asset.AssetPath;
		Signature += TEXT("|");
		Signature += Asset.bAvailable ? TEXT("1") : TEXT("0");
		Signature += TEXT("|");
	}
	return Signature;
}
