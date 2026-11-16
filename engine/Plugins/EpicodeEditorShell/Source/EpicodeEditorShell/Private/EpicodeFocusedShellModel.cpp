#include "EpicodeFocusedShellModel.h"

#include "EpicodeEngineSubsystem.h"
#include "EpicodeSettings.h"
#include "EpicodeSceneSubsystem.h"
#include "EpicodeTypes.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
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

	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*UEpicodeSettings::Get()->ImportContentRoot));
	Filter.bRecursivePaths = true;

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssets(Filter, AssetDataList);
	for (const FAssetData& AssetData : AssetDataList)
	{
		const FTopLevelAssetPath ClassPath = AssetData.AssetClassPath;
		FString Type;
		if (ClassPath == USkeletalMesh::StaticClass()->GetClassPathName())
		{
			Type = TEXT("Character");
		}
		else if (ClassPath == UStaticMesh::StaticClass()->GetClassPathName())
		{
			Type = TEXT("Prop");
		}
		else
		{
			continue;
		}

		const FString AssetPath = AssetData.GetObjectPathString();
		if (HiddenAssetPaths.Contains(AssetPath))
		{
			continue;
		}
		FEpicodeFocusedAssetRow Row;
		Row.Name = AssetData.AssetName.ToString();
		Row.AssetPath = AssetPath;
		Row.Type = Type;
		Assets.Add(MoveTemp(Row));
	}

	Assets.Sort([](const FEpicodeFocusedAssetRow& Left, const FEpicodeFocusedAssetRow& Right)
	{
		return Left.Name < Right.Name;
	});
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
	}
	return Signature;
}
