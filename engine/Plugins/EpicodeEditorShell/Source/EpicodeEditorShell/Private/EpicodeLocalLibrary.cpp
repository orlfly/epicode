#include "EpicodeLocalLibrary.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "EpicodeSceneDocument.h"
#include "EpicodeSceneLibrary.h"
#include "EpicodeSceneSubsystem.h"
#include "EpicodeSettings.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	const TCHAR* SceneType = TEXT("scene");
	const TCHAR* CharacterType = TEXT("character");
	const TCHAR* PropType = TEXT("prop");

	void AddStringArray(
		const TArray<FString>& Values, const TCHAR* Field, const TSharedPtr<FJsonObject>& Object)
	{
		TArray<TSharedPtr<FJsonValue>> JsonValues;
		for (const FString& Value : Values)
		{
			JsonValues.Add(MakeShared<FJsonValueString>(Value));
		}
		Object->SetArrayField(Field, JsonValues);
	}

	void ReadStringArray(
		const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, TArray<FString>& OutValues)
	{
		OutValues.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object->TryGetArrayField(Field, Values) || !Values)
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			if (Value.IsValid() && Value->Type == EJson::String)
			{
				OutValues.Add(Value->AsString());
			}
		}
	}

	int32 CountEntities(const FString& ScenePath, FString& OutLevelPath, bool& bOutAvailable, FString& OutError)
	{
		FEpicodeSceneDocument Document;
		FString LoadError;
		if (!FEpicodeSceneLibrary::Load(ScenePath, Document, LoadError))
		{
			bOutAvailable = false;
			OutError = LoadError;
			return 0;
		}
		OutLevelPath = Document.LevelPath;
		return Document.Entities.Num();
	}

	void SortEntries(TArray<FEpicodeLibraryEntry>& Entries)
	{
		Entries.Sort([](const FEpicodeLibraryEntry& Left, const FEpicodeLibraryEntry& Right)
		{
			if (Left.Type != Right.Type)
			{
				return static_cast<uint8>(Left.Type) < static_cast<uint8>(Right.Type);
			}
			return Left.Name < Right.Name;
		});
	}
}

FString FEpicodeLibraryEntry::GetTypeString() const
{
	switch (Type)
	{
	case EEpicodeLibraryEntryType::Character:
		return CharacterType;
	case EEpicodeLibraryEntryType::Prop:
		return PropType;
	default:
		return SceneType;
	}
}

bool FEpicodeLibraryEntry::TypeFromString(
	const FString& Value, EEpicodeLibraryEntryType& OutType)
{
	if (Value.Equals(SceneType, ESearchCase::IgnoreCase))
	{
		OutType = EEpicodeLibraryEntryType::Scene;
		return true;
	}
	if (Value.Equals(CharacterType, ESearchCase::IgnoreCase))
	{
		OutType = EEpicodeLibraryEntryType::Character;
		return true;
	}
	if (Value.Equals(PropType, ESearchCase::IgnoreCase))
	{
		OutType = EEpicodeLibraryEntryType::Prop;
		return true;
	}
	return false;
}

TSharedPtr<FJsonObject> FEpicodeLibraryEntry::ToJson() const
{
	TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("id"), Id);
	Object->SetStringField(TEXT("type"), GetTypeString());
	Object->SetStringField(TEXT("name"), Name);
	Object->SetStringField(TEXT("path"), Path);
	if (!LevelPath.IsEmpty())
	{
		Object->SetStringField(TEXT("levelPath"), LevelPath);
	}
	Object->SetNumberField(TEXT("entityCount"), EntityCount);
	if (ModifiedAt.GetTicks() > 0)
	{
		Object->SetStringField(TEXT("modifiedAt"), ModifiedAt.ToIso8601());
	}
	Object->SetBoolField(TEXT("available"), bAvailable);
	if (!Error.IsEmpty())
	{
		Object->SetStringField(TEXT("error"), Error);
	}
	AddStringArray(Tags, TEXT("tags"), Object);
	return Object;
}

bool FEpicodeLibraryEntry::FromJson(
	const TSharedPtr<FJsonObject>& Object, FEpicodeLibraryEntry& OutEntry)
{
	if (!Object.IsValid()
		|| !Object->TryGetStringField(TEXT("id"), OutEntry.Id)
		|| OutEntry.Id.IsEmpty()
		|| !Object->TryGetStringField(TEXT("path"), OutEntry.Path)
		|| OutEntry.Path.IsEmpty())
	{
		return false;
	}

	FString TypeString;
	if (!Object->TryGetStringField(TEXT("type"), TypeString)
		|| !TypeFromString(TypeString, OutEntry.Type))
	{
		return false;
	}

	Object->TryGetStringField(TEXT("name"), OutEntry.Name);
	Object->TryGetStringField(TEXT("levelPath"), OutEntry.LevelPath);
	double EntityCount = 0.0;
	if (Object->TryGetNumberField(TEXT("entityCount"), EntityCount))
	{
		OutEntry.EntityCount = FMath::Max(0, static_cast<int32>(EntityCount));
	}
	FString ModifiedAt;
	if (Object->TryGetStringField(TEXT("modifiedAt"), ModifiedAt))
	{
		FDateTime::ParseIso8601(*ModifiedAt, OutEntry.ModifiedAt);
	}
	Object->TryGetBoolField(TEXT("available"), OutEntry.bAvailable);
	Object->TryGetStringField(TEXT("error"), OutEntry.Error);
	ReadStringArray(Object, TEXT("tags"), OutEntry.Tags);
	return true;
}

FEpicodeLocalLibrary::FEpicodeLocalLibrary(FString InIndexPath)
	: IndexPath(MoveTemp(InIndexPath))
{
	if (IndexPath.IsEmpty())
	{
		IndexPath = GetDefaultIndexPath();
	}
}

FString FEpicodeLocalLibrary::GetDefaultIndexPath()
{
	return FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("Epicode/Library/index.json"));
}

FString FEpicodeLocalLibrary::MakeSceneEntryId(const FString& SceneId)
{
	return FString::Printf(TEXT("scene:%s"), *SceneId);
}

FString FEpicodeLocalLibrary::MakeAssetEntryId(const FString& AssetPath)
{
	return FString::Printf(TEXT("asset:%s"), *AssetPath);
}

bool FEpicodeLocalLibrary::Refresh(FString& OutError)
{
	LastWarning.Reset();
	bool bNeedsRebuild = false;
	if (!LoadInternal(OutError, bNeedsRebuild))
	{
		return false;
	}
	if (bNeedsRebuild)
	{
		return Rebuild(OutError);
	}

	TArray<FEpicodeLibraryEntry> ScannedScenes;
	TArray<FEpicodeLibraryEntry> ScannedAssets;
	if (!ScanScenes(ScannedScenes, OutError) || !ScanAssets(ScannedAssets, OutError))
	{
		return false;
	}

	TMap<FString, FEpicodeLibraryEntry> Previous;
	for (const FEpicodeLibraryEntry& Entry : Entries)
	{
		Previous.Add(Entry.Id, Entry);
	}

	TArray<FEpicodeLibraryEntry> Refreshed;
	Refreshed.Reserve(ScannedScenes.Num() + ScannedAssets.Num());
	auto AddScanned = [&Previous, &Refreshed](const FEpicodeLibraryEntry& Entry)
	{
		FEpicodeLibraryEntry NewEntry = Entry;
		if (const FEpicodeLibraryEntry* Old = Previous.Find(Entry.Id))
		{
			NewEntry.Tags = Old->Tags;
		}
		Refreshed.Add(MoveTemp(NewEntry));
	};
	for (const FEpicodeLibraryEntry& Entry : ScannedScenes)
	{
		AddScanned(Entry);
	}

	TSet<FString> SeenAssets;
	for (const FEpicodeLibraryEntry& Entry : ScannedAssets)
	{
		SeenAssets.Add(Entry.Id);
		AddScanned(Entry);
	}

	for (const FEpicodeLibraryEntry& Old : Entries)
	{
		if (Old.Type == EEpicodeLibraryEntryType::Scene || SeenAssets.Contains(Old.Id))
		{
			continue;
		}
		FEpicodeLibraryEntry Missing = Old;
		Missing.bAvailable = false;
		Missing.Error = TEXT("资产注册表中找不到该资产");
		Refreshed.Add(MoveTemp(Missing));
	}

	SortEntries(Refreshed);
	Entries = MoveTemp(Refreshed);
	return Save(OutError);
}

bool FEpicodeLocalLibrary::Rebuild(FString& OutError)
{
	TArray<FEpicodeLibraryEntry> ScannedScenes;
	TArray<FEpicodeLibraryEntry> ScannedAssets;
	if (!ScanScenes(ScannedScenes, OutError) || !ScanAssets(ScannedAssets, OutError))
	{
		return false;
	}
	Entries.Reset();
	Entries.Append(ScannedScenes);
	Entries.Append(ScannedAssets);
	SortEntries(Entries);
	SchemaVersion = CurrentSchemaVersion;
	return Save(OutError);
}

bool FEpicodeLocalLibrary::Save(FString& OutError) const
{
	const FString Directory = FPaths::GetPath(IndexPath);
	if (!IFileManager::Get().MakeDirectory(*Directory, true))
	{
		OutError = FString::Printf(TEXT("无法创建本地库索引目录：%s"), *Directory);
		return false;
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), CurrentSchemaVersion);
	Root->SetStringField(TEXT("generatedAt"), FDateTime::UtcNow().ToIso8601());
	TArray<TSharedPtr<FJsonValue>> EntryValues;
	for (const FEpicodeLibraryEntry& Entry : Entries)
	{
		EntryValues.Add(MakeShared<FJsonValueObject>(Entry.ToJson()));
	}
	Root->SetArrayField(TEXT("entries"), EntryValues);

	FString Text;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
	if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
	{
		OutError = TEXT("无法序列化本地库索引");
		return false;
	}

	const FString TempPath = IndexPath + TEXT(".tmp");
	if (!FFileHelper::SaveStringToFile(Text, *TempPath))
	{
		OutError = FString::Printf(TEXT("无法写入本地库索引：%s"), *TempPath);
		return false;
	}
	IFileManager::Get().Delete(*IndexPath, false, true, true);
	if (!IFileManager::Get().Move(*IndexPath, *TempPath, true, true))
	{
		IFileManager::Get().Delete(*TempPath, false, true, true);
		OutError = FString::Printf(TEXT("无法替换本地库索引：%s"), *IndexPath);
		return false;
	}
	return true;
}

bool FEpicodeLocalLibrary::LoadFromDisk(FString& OutError)
{
	bool bNeedsRebuild = false;
	if (!LoadInternal(OutError, bNeedsRebuild))
	{
		return false;
	}
	if (bNeedsRebuild)
	{
		OutError = LastWarning.IsEmpty()
			? TEXT("本地库索引缺失或需要重建")
			: LastWarning;
		return false;
	}
	return true;
}

bool FEpicodeLocalLibrary::LoadInternal(FString& OutError, bool& bOutNeedsRebuild)
{
	bOutNeedsRebuild = false;
	Entries.Reset();
	SchemaVersion = CurrentSchemaVersion;

	if (!IFileManager::Get().FileExists(*IndexPath))
	{
		bOutNeedsRebuild = true;
		return true;
	}

	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *IndexPath) || Text.TrimStartAndEnd().IsEmpty())
	{
		LastWarning = FString::Printf(TEXT("本地库索引为空或不可读，已备份并重建：%s"), *IndexPath);
		BackupCorruptIndex();
		bOutNeedsRebuild = true;
		return true;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		LastWarning = FString::Printf(TEXT("本地库索引损坏，已备份并重建：%s"), *IndexPath);
		BackupCorruptIndex();
		bOutNeedsRebuild = true;
		return true;
	}

	double Version = 0.0;
	if (!Root->TryGetNumberField(TEXT("version"), Version)
		|| static_cast<int32>(Version) != CurrentSchemaVersion)
	{
		LastWarning = FString::Printf(
			TEXT("本地库索引 schema 不兼容（期望 %d），已备份并重建：%s"),
			CurrentSchemaVersion, *IndexPath);
		BackupCorruptIndex();
		bOutNeedsRebuild = true;
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Root->TryGetArrayField(TEXT("entries"), Values) || !Values)
	{
		LastWarning = FString::Printf(TEXT("本地库索引缺少 entries，已备份并重建：%s"), *IndexPath);
		BackupCorruptIndex();
		bOutNeedsRebuild = true;
		return true;
	}

	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
		FEpicodeLibraryEntry Entry;
		if (!Object.IsValid() || !FEpicodeLibraryEntry::FromJson(Object, Entry))
		{
			LastWarning = TEXT("本地库索引包含无效条目，已跳过");
			continue;
		}
		Entries.Add(MoveTemp(Entry));
	}
	return true;
}

bool FEpicodeLocalLibrary::UpsertEntry(
	const FEpicodeLibraryEntry& Entry, FString& OutError)
{
	if (Entry.Id.IsEmpty() || Entry.Path.IsEmpty())
	{
		OutError = TEXT("本地库条目缺少 id 或 path");
		return false;
	}
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		if (Entries[Index].Id == Entry.Id)
		{
			Entries[Index] = Entry;
			SortEntries(Entries);
			return Save(OutError);
		}
	}
	Entries.Add(Entry);
	SortEntries(Entries);
	return Save(OutError);
}

bool FEpicodeLocalLibrary::RemoveEntry(const FString& Id, FString& OutError)
{
	const int32 Removed = Entries.RemoveAll([&Id](const FEpicodeLibraryEntry& Entry)
	{
		return Entry.Id == Id;
	});
	if (Removed == 0)
	{
		return true;
	}
	return Save(OutError);
}

const FEpicodeLibraryEntry* FEpicodeLocalLibrary::FindEntry(const FString& Id) const
{
	return Entries.FindByPredicate([&Id](const FEpicodeLibraryEntry& Entry)
	{
		return Entry.Id == Id;
	});
}

void FEpicodeLocalLibrary::BackupCorruptIndex()
{
	if (!IFileManager::Get().FileExists(*IndexPath))
	{
		return;
	}
	const FString BackupPath = FString::Printf(
		TEXT("%s.corrupt-%lld.json"), *IndexPath, FDateTime::Now().GetTicks());
	IFileManager::Get().Move(*BackupPath, *IndexPath, true, true);
}

bool FEpicodeLocalLibrary::ScanScenes(
	TArray<FEpicodeLibraryEntry>& OutEntries, FString& OutError) const
{
	OutEntries.Reset();
	UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
	if (!Scene)
	{
		return true;
	}

	for (const FEpicodeSceneLibraryEntry& SceneEntry : Scene->ListScenes())
	{
		FEpicodeLibraryEntry Entry;
		Entry.Id = MakeSceneEntryId(SceneEntry.SceneId);
		Entry.Type = EEpicodeLibraryEntryType::Scene;
		Entry.Name = SceneEntry.Name;
		Entry.Path = SceneEntry.DocumentPath;
		Entry.ModifiedAt = SceneEntry.ModifiedTime;
		Entry.bAvailable = SceneEntry.bAvailable;
		Entry.Error = SceneEntry.Error;
		if (Entry.bAvailable)
		{
			Entry.EntityCount = CountEntities(
				Entry.Path, Entry.LevelPath, Entry.bAvailable, Entry.Error);
		}
		OutEntries.Add(MoveTemp(Entry));
	}

	SortEntries(OutEntries);
	return true;
}

bool FEpicodeLocalLibrary::ScanAssets(
	TArray<FEpicodeLibraryEntry>& OutEntries, FString& OutError) const
{
	OutEntries.Reset();
	const UEpicodeSettings* Settings = UEpicodeSettings::Get();
	if (!Settings || Settings->ImportContentRoot.IsEmpty())
	{
		return true;
	}

	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*Settings->ImportContentRoot));
	Filter.bRecursivePaths = true;

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssets(Filter, AssetDataList);
	for (const FAssetData& AssetData : AssetDataList)
	{
		const FTopLevelAssetPath ClassPath = AssetData.AssetClassPath;
		EEpicodeLibraryEntryType Type;
		if (ClassPath == USkeletalMesh::StaticClass()->GetClassPathName())
		{
			Type = EEpicodeLibraryEntryType::Character;
		}
		else if (ClassPath == UStaticMesh::StaticClass()->GetClassPathName())
		{
			Type = EEpicodeLibraryEntryType::Prop;
		}
		else
		{
			continue;
		}

		FEpicodeLibraryEntry Entry;
		Entry.Type = Type;
		Entry.Name = AssetData.AssetName.ToString();
		Entry.Path = AssetData.GetObjectPathString();
		Entry.Id = MakeAssetEntryId(Entry.Path);
		OutEntries.Add(MoveTemp(Entry));
	}

	SortEntries(OutEntries);
	return true;
}
