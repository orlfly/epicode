#include "EpicodeSceneLibrary.h"

#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

TSharedPtr<FJsonObject> FEpicodeSceneLibraryEntry::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("sceneId"), SceneId);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("directory"), Directory);
	Result->SetStringField(TEXT("documentPath"), DocumentPath);
	Result->SetStringField(TEXT("modifiedAt"), ModifiedTime.ToIso8601());
	Result->SetBoolField(TEXT("current"), bCurrent);
	Result->SetBoolField(TEXT("available"), bAvailable);
	if (!Error.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), Error);
	}
	return Result;
}

FString FEpicodeSceneLibrary::GetSceneDirectory(const FString& InRoot, const FString& InSceneId)
{
	return FPaths::Combine(InRoot, InSceneId);
}

TArray<FEpicodeSceneLibraryEntry> FEpicodeSceneLibrary::Scan(const FString& InRoot, const FString& InCurrentSceneId)
{
	TArray<FEpicodeSceneLibraryEntry> Result;
	TArray<FString> Directories;
	IFileManager::Get().FindFiles(Directories, *FPaths::Combine(InRoot, TEXT("*")), false, true);
	for (const FString& DirectoryName : Directories)
	{
		const FString Directory = FPaths::Combine(InRoot, DirectoryName);
		const FString DocumentPath = FPaths::Combine(Directory, TEXT("scene.json"));
		if (!IFileManager::Get().FileExists(*DocumentPath))
		{
			continue;
		}

		FEpicodeSceneLibraryEntry Entry;
		Entry.Directory = Directory;
		Entry.DocumentPath = DocumentPath;
		Entry.ModifiedTime = IFileManager::Get().GetTimeStamp(*DocumentPath);

		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *DocumentPath))
		{
			Entry.SceneId = DirectoryName;
			Entry.Name = DirectoryName;
			Entry.bAvailable = false;
			Entry.Error = TEXT("Unable to read scene document.");
			Result.Add(Entry);
			continue;
		}

		TSharedPtr<FJsonObject> Json;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		{
			Entry.SceneId = DirectoryName;
			Entry.Name = DirectoryName;
			Entry.bAvailable = false;
			Entry.Error = TEXT("Scene document is not valid JSON.");
			Result.Add(Entry);
			continue;
		}

		Json->TryGetStringField(TEXT("sceneId"), Entry.SceneId);
		Json->TryGetStringField(TEXT("name"), Entry.Name);
		if (Entry.SceneId.IsEmpty())
		{
			Entry.SceneId = DirectoryName;
		}
		if (Entry.Name.IsEmpty())
		{
			Entry.Name = Entry.SceneId;
		}
		Entry.bCurrent = !InCurrentSceneId.IsEmpty() && Entry.SceneId == InCurrentSceneId;

		FEpicodeSceneDocument Document;
		FString Error;
		if (!FEpicodeSceneDocument::FromJson(Json, Document, Error))
		{
			Entry.bAvailable = false;
			Entry.Error = Error;
		}
		Result.Add(Entry);
	}

	Result.Sort([](const FEpicodeSceneLibraryEntry& A, const FEpicodeSceneLibraryEntry& B)
	{
		return A.Name < B.Name;
	});
	return Result;
}

bool FEpicodeSceneLibrary::Load(const FString& InDocumentPath, FEpicodeSceneDocument& OutDocument, FString& OutError)
{
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *InDocumentPath))
	{
		OutError = FString::Printf(TEXT("Unable to read scene document: %s"), *InDocumentPath);
		return false;
	}
	TSharedPtr<FJsonObject> Json;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		OutError = FString::Printf(TEXT("Scene document is not valid JSON: %s"), *InDocumentPath);
		return false;
	}
	return FEpicodeSceneDocument::FromJson(Json, OutDocument, OutError);
}

bool FEpicodeSceneLibrary::Save(const FEpicodeSceneDocument& InDocument, const FString& InRoot, FString& OutDocumentPath, FString& OutError)
{
	if (InDocument.SceneId.IsEmpty())
	{
		OutError = TEXT("Scene has no id.");
		return false;
	}
	const FString Directory = GetSceneDirectory(InRoot, InDocument.SceneId);
	if (!IFileManager::Get().MakeDirectory(*Directory, true))
	{
		OutError = FString::Printf(TEXT("Unable to create scene directory: %s"), *Directory);
		return false;
	}
	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	if (!FJsonSerializer::Serialize(InDocument.ToJson(), Writer))
	{
		OutError = TEXT("Unable to serialize scene document.");
		return false;
	}
	OutDocumentPath = FPaths::Combine(Directory, TEXT("scene.json"));
	const FString TempPath = OutDocumentPath + TEXT(".tmp");
	if (!FFileHelper::SaveStringToFile(JsonText, *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("Unable to write temporary scene file: %s"), *TempPath);
		return false;
	}
	if (!IFileManager::Get().Move(*OutDocumentPath, *TempPath, true, true))
	{
		IFileManager::Get().Delete(*TempPath);
		OutError = FString::Printf(TEXT("Unable to replace scene file: %s"), *OutDocumentPath);
		return false;
	}
	return true;
}

bool FEpicodeSceneLibrary::Delete(const FEpicodeSceneLibraryEntry& InEntry, const FString& InTrashRoot, const FString& InCurrentSceneId, FString& OutTrashPath, FString& OutError)
{
	if (!InCurrentSceneId.IsEmpty() && InEntry.SceneId == InCurrentSceneId)
	{
		OutError = TEXT("The current scene cannot be deleted while it is open.");
		return false;
	}
	if (!IFileManager::Get().DirectoryExists(*InEntry.Directory))
	{
		OutError = FString::Printf(TEXT("Scene directory does not exist: %s"), *InEntry.Directory);
		return false;
	}
	if (!IFileManager::Get().MakeDirectory(*InTrashRoot, true))
	{
		OutError = FString::Printf(TEXT("Unable to create scene trash directory: %s"), *InTrashRoot);
		return false;
	}
	const FString Timestamp = FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"));
	OutTrashPath = FPaths::Combine(InTrashRoot, FString::Printf(TEXT("%s-%s"), *Timestamp, *InEntry.SceneId));
	if (IFileManager::Get().DirectoryExists(*OutTrashPath))
	{
		OutTrashPath += FString::Printf(TEXT("-%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
	}
	if (!IFileManager::Get().Move(*OutTrashPath, *InEntry.Directory, true, true))
	{
		OutError = FString::Printf(TEXT("Unable to move scene to trash: %s"), *InEntry.Directory);
		return false;
	}
	return true;
}

bool FEpicodeSceneLibrary::Find(const FString& InRoot, const FString& InSceneId, FEpicodeSceneLibraryEntry& OutEntry)
{
	for (const FEpicodeSceneLibraryEntry& Entry : Scan(InRoot, InSceneId))
	{
		if (Entry.SceneId == InSceneId)
		{
			OutEntry = Entry;
			return true;
		}
	}
	return false;
}
