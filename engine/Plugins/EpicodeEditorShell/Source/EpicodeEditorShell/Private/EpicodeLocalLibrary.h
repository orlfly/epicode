#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

enum class EEpicodeLibraryEntryType : uint8
{
	Scene,
	Character,
	Prop
};

struct FEpicodeLibraryEntry
{
	FString Id;
	EEpicodeLibraryEntryType Type = EEpicodeLibraryEntryType::Scene;
	FString Name;
	FString Path;
	FString LevelPath;
	int32 EntityCount = 0;
	FDateTime ModifiedAt;
	bool bAvailable = true;
	FString Error;
	TArray<FString> Tags;

	FString GetTypeString() const;
	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& Object, FEpicodeLibraryEntry& OutEntry);
	static bool TypeFromString(const FString& Value, EEpicodeLibraryEntryType& OutType);
};

/**
 * Persistent index for the focused editor's local library. The index is a
 * cache: it is rebuilt from the scene library and asset registry when missing,
 * corrupt, or written by a newer schema.
 */
class FEpicodeLocalLibrary
{
public:
	static constexpr int32 CurrentSchemaVersion = 1;

	explicit FEpicodeLocalLibrary(FString InIndexPath = FString());

	/** Load the index and merge the current scene/asset scans. Rebuilds if needed. */
	bool Refresh(FString& OutError);

	/** Discard the index and rescan scenes and imported assets. */
	bool Rebuild(FString& OutError);

	/** Persist the current entries without rescanning. */
	bool Save(FString& OutError) const;

	/** Load only the index file; corruption is backed up and reported for rebuild. */
	bool LoadFromDisk(FString& OutError);

	bool UpsertEntry(const FEpicodeLibraryEntry& Entry, FString& OutError);
	bool RemoveEntry(const FString& Id, FString& OutError);

	const TArray<FEpicodeLibraryEntry>& GetEntries() const { return Entries; }
	const FEpicodeLibraryEntry* FindEntry(const FString& Id) const;
	int32 GetSchemaVersion() const { return SchemaVersion; }
	const FString& GetIndexPath() const { return IndexPath; }
	const FString& GetLastWarning() const { return LastWarning; }

	static FString GetDefaultIndexPath();

private:
	bool LoadInternal(FString& OutError, bool& bOutNeedsRebuild);
	bool ScanScenes(TArray<FEpicodeLibraryEntry>& OutEntries, FString& OutError) const;
	bool ScanAssets(TArray<FEpicodeLibraryEntry>& OutEntries, FString& OutError) const;
	void BackupCorruptIndex();
	static FString MakeSceneEntryId(const FString& SceneId);
	static FString MakeAssetEntryId(const FString& AssetPath);

	FString IndexPath;
	FString LastWarning;
	TArray<FEpicodeLibraryEntry> Entries;
	int32 SchemaVersion = CurrentSchemaVersion;
};
