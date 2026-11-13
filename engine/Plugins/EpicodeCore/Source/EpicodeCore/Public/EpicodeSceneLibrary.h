#pragma once

#include "CoreMinimal.h"
#include "EpicodeSceneDocument.h"

struct EPICODECORE_API FEpicodeSceneLibraryEntry
{
	FString SceneId;
	FString Name;
	FString Directory;
	FString DocumentPath;
	FDateTime ModifiedTime;
	bool bCurrent = false;
	bool bAvailable = true;
	FString Error;

	TSharedPtr<FJsonObject> ToJson() const;
};

class EPICODECORE_API FEpicodeSceneLibrary
{
public:
	static TArray<FEpicodeSceneLibraryEntry> Scan(const FString& InRoot, const FString& InCurrentSceneId);
	static bool Load(const FString& InDocumentPath, FEpicodeSceneDocument& OutDocument, FString& OutError);
	static bool Save(const FEpicodeSceneDocument& InDocument, const FString& InRoot, FString& OutDocumentPath, FString& OutError);
	static bool Delete(const FEpicodeSceneLibraryEntry& InEntry, const FString& InTrashRoot, const FString& InCurrentSceneId, FString& OutTrashPath, FString& OutError);
	static bool Find(const FString& InRoot, const FString& InSceneId, FEpicodeSceneLibraryEntry& OutEntry);
	static FString GetSceneDirectory(const FString& InRoot, const FString& InSceneId);
};
