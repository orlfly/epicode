#include "EpicodeProductionLibrary.h"

#include "EpicodeCommand.h"
#include "EpicodeProductionValidation.h"
#include "EpicodeSceneDocument.h"
#include "EpicodeTypes.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	bool IsSafeIdentifier(const FString& InId)
	{
		return !InId.IsEmpty()
			&& !InId.Contains(TEXT("/"))
			&& !InId.Contains(TEXT("\\"))
			&& !InId.Contains(TEXT(".."))
			&& !InId.Contains(TEXT(":"));
	}

	FString SerializeJsonObject(const TSharedPtr<FJsonObject>& InObject, bool bPretty)
	{
		FString Output;
		if (!InObject.IsValid())
		{
			return Output;
		}
		if (bPretty)
		{
			const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
			FJsonSerializer::Serialize(InObject.ToSharedRef(), Writer);
		}
		else
		{
			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
			FJsonSerializer::Serialize(InObject.ToSharedRef(), Writer);
		}
		return Output;
	}

	bool ParseJsonObject(const FString& InJson, TSharedPtr<FJsonObject>& OutObject, FString& OutError)
	{
		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(InJson);
		if (!FJsonSerializer::Deserialize(Reader, OutObject) || !OutObject.IsValid())
		{
			OutError = TEXT("PRODUCTION_JSON_INVALID: production.json is not valid JSON");
			return false;
		}
		return true;
	}
}

TSharedPtr<FJsonObject> FEpicodeProductionLibraryEntry::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("productionId"), ProductionId);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("directory"), Directory);
	Result->SetStringField(TEXT("documentPath"), DocumentPath);
	Result->SetStringField(TEXT("modifiedTime"), ModifiedTime.ToIso8601());
	Result->SetBoolField(TEXT("current"), bCurrent);
	Result->SetBoolField(TEXT("available"), bAvailable);
	if (!Error.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), Error);
	}
	return Result;
}

TArray<FEpicodeProductionLibraryEntry> FEpicodeProductionLibrary::Scan(const FString& InRoot, const FString& InCurrentProductionId)
{
	TArray<FEpicodeProductionLibraryEntry> Entries;
	TArray<FString> Directories;
	IFileManager::Get().FindFiles(Directories, *(FPaths::Combine(InRoot, TEXT("*"))), false, true);
	Directories.Sort();

	for (const FString& DirectoryName : Directories)
	{
		const FString Directory = FPaths::Combine(InRoot, DirectoryName);
		const FString DocumentPath = FPaths::Combine(Directory, GetDocumentFileName());
		if (!FPaths::FileExists(DocumentPath))
		{
			continue;
		}
		FEpicodeProductionLibraryEntry Entry;
		Entry.Directory = Directory;
		Entry.DocumentPath = DocumentPath;
		Entry.ModifiedTime = IFileManager::Get().GetTimeStamp(*DocumentPath);
		Entry.bCurrent = DirectoryName.Equals(InCurrentProductionId, ESearchCase::IgnoreCase);
		Entry.ProductionId = DirectoryName;
		FEpicodeProductionDocument Document;
		if (!Load(DocumentPath, Document, Entry.Error))
		{
			Entry.bAvailable = false;
		}
		else
		{
			Entry.ProductionId = Document.ProductionId;
			Entry.Name = Document.Name;
			Entry.bCurrent = Document.ProductionId.Equals(InCurrentProductionId, ESearchCase::IgnoreCase) || Entry.bCurrent;
		}
		Entries.Add(Entry);
	}
	return Entries;
}

bool FEpicodeProductionLibrary::Load(const FString& InDocumentPath, FEpicodeProductionDocument& OutDocument, FString& OutError)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *InDocumentPath))
	{
		OutError = FString::Printf(TEXT("PRODUCTION_NOT_FOUND: unable to read %s"), *InDocumentPath);
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	if (!ParseJsonObject(Json, Root, OutError))
	{
		return false;
	}
	return FEpicodeProductionDocument::FromJson(Root, OutDocument, OutError);
}

bool FEpicodeProductionLibrary::Create(const FEpicodeProductionDocument& InDocument, const FString& InRoot, FString& OutDocumentPath, FString& OutError)
{
	if (!IsSafeIdentifier(InDocument.ProductionId))
	{
		OutError = TEXT("PRODUCTION_ID_INVALID: productionId must be a safe directory name");
		return false;
	}
	if (!InDocument.Validate(OutError))
	{
		return false;
	}
	const FString Directory = GetProductionDirectory(InRoot, InDocument.ProductionId);
	const FString DocumentPath = FPaths::Combine(Directory, GetDocumentFileName());
	if (FPaths::FileExists(DocumentPath))
	{
		OutError = TEXT("PRODUCTION_EXISTS: production already exists");
		return false;
	}
	if (!WriteAtomic(InDocument, DocumentPath, OutError))
	{
		return false;
	}
	OutDocumentPath = DocumentPath;
	return true;
}

bool FEpicodeProductionLibrary::Save(const FEpicodeProductionDocument& InDocument, const FString& InRoot, FString& OutDocumentPath, FString& OutError)
{
	if (!IsSafeIdentifier(InDocument.ProductionId))
	{
		OutError = TEXT("PRODUCTION_ID_INVALID: productionId must be a safe directory name");
		return false;
	}
	if (!InDocument.Validate(OutError))
	{
		return false;
	}
	const FString DocumentPath = GetDocumentPath(InRoot, InDocument.ProductionId);
	if (!WriteAtomic(InDocument, DocumentPath, OutError))
	{
		return false;
	}
	OutDocumentPath = DocumentPath;
	return true;
}

bool FEpicodeProductionLibrary::SaveAs(const FEpicodeProductionDocument& InDocument, const FString& InRoot, const FString& InNewName, FEpicodeProductionDocument& OutDocument, FString& OutDocumentPath, FString& OutError)
{
	OutDocument = InDocument;
	OutDocument.ProductionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	OutDocument.Name = InNewName.TrimStartAndEnd().IsEmpty() ? InDocument.Name : InNewName.TrimStartAndEnd();
	for (int32 Attempt = 0; Attempt < 8 && FPaths::FileExists(GetDocumentPath(InRoot, OutDocument.ProductionId)); ++Attempt)
	{
		OutDocument.ProductionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	}
	if (FPaths::FileExists(GetDocumentPath(InRoot, OutDocument.ProductionId)))
	{
		OutError = TEXT("PRODUCTION_EXISTS: unable to allocate a new production id");
		return false;
	}
	return Create(OutDocument, InRoot, OutDocumentPath, OutError);
}

bool FEpicodeProductionLibrary::Delete(const FEpicodeProductionLibraryEntry& InEntry, const FString& InTrashRoot, const FString& InCurrentProductionId, FString& OutTrashPath, FString& OutError)
{
	if (!IsSafeIdentifier(InEntry.ProductionId))
	{
		OutError = TEXT("PRODUCTION_ID_INVALID: productionId must be a safe directory name");
		return false;
	}
	if (InEntry.ProductionId.Equals(InCurrentProductionId, ESearchCase::IgnoreCase))
	{
		OutError = TEXT("PRODUCTION_IN_USE: cannot delete the open production");
		return false;
	}
	if (!IFileManager::Get().DirectoryExists(*InEntry.Directory))
	{
		OutError = TEXT("PRODUCTION_NOT_FOUND: production directory does not exist");
		return false;
	}
	const FDateTime Now = FDateTime::UtcNow();
	FString Destination = FPaths::Combine(InTrashRoot, FString::Printf(TEXT("%s-%s"), *InEntry.ProductionId, *Now.ToString(TEXT("%Y%m%d-%H%M%S"))));
	if (IFileManager::Get().DirectoryExists(*Destination))
	{
		Destination += TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	}
	if (!IFileManager::Get().MakeDirectory(*InTrashRoot, true))
	{
		OutError = TEXT("PRODUCTION_TRASH_FAILED: unable to create trash root");
		return false;
	}
	if (!IFileManager::Get().Move(*Destination, *InEntry.Directory, true, true))
	{
		OutError = TEXT("PRODUCTION_TRASH_FAILED: unable to move production into trash");
		return false;
	}
	OutTrashPath = Destination;
	return true;
}

bool FEpicodeProductionLibrary::Find(const FString& InRoot, const FString& InProductionId, FEpicodeProductionLibraryEntry& OutEntry)
{
	if (!IsSafeIdentifier(InProductionId))
	{
		return false;
	}
	const FString Directory = GetProductionDirectory(InRoot, InProductionId);
	const FString DocumentPath = FPaths::Combine(Directory, GetDocumentFileName());
	if (!FPaths::FileExists(DocumentPath))
	{
		return false;
	}
	OutEntry = FEpicodeProductionLibraryEntry();
	OutEntry.ProductionId = InProductionId;
	OutEntry.Directory = Directory;
	OutEntry.DocumentPath = DocumentPath;
	OutEntry.ModifiedTime = IFileManager::Get().GetTimeStamp(*DocumentPath);
	FEpicodeProductionDocument Document;
	if (!Load(DocumentPath, Document, OutEntry.Error))
	{
		OutEntry.bAvailable = false;
		return true;
	}
	OutEntry.ProductionId = Document.ProductionId;
	OutEntry.Name = Document.Name;
	return true;
}

FString FEpicodeProductionLibrary::GetProductionDirectory(const FString& InRoot, const FString& InProductionId)
{
	return FPaths::Combine(InRoot, InProductionId);
}

FString FEpicodeProductionLibrary::GetDocumentPath(const FString& InRoot, const FString& InProductionId)
{
	return FPaths::Combine(GetProductionDirectory(InRoot, InProductionId), GetDocumentFileName());
}

bool FEpicodeProductionLibrary::WriteAtomic(const FEpicodeProductionDocument& InDocument, const FString& InDocumentPath, FString& OutError)
{
	FString ValidationError;
	if (!InDocument.Validate(ValidationError))
	{
		OutError = ValidationError;
		return false;
	}
	const FString Directory = FPaths::GetPath(InDocumentPath);
	if (!IFileManager::Get().MakeDirectory(*Directory, true))
	{
		OutError = TEXT("PRODUCTION_WRITE_FAILED: unable to create production directory");
		return false;
	}
	const FString Json = SerializeJsonObject(InDocument.ToJson(), true);
	const FString TempPath = InDocumentPath + TEXT(".tmp-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	if (!FFileHelper::SaveStringToFile(Json, *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		IFileManager::Get().Delete(*TempPath);
		OutError = TEXT("PRODUCTION_WRITE_FAILED: unable to write temporary production document");
		return false;
	}

	const bool bHadExisting = FPaths::FileExists(InDocumentPath);
	const FString BackupPath = InDocumentPath + TEXT(".bak-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	if (bHadExisting && !IFileManager::Get().Move(*BackupPath, *InDocumentPath, true, true))
	{
		IFileManager::Get().Delete(*TempPath);
		OutError = TEXT("PRODUCTION_WRITE_FAILED: unable to stage existing production document");
		return false;
	}
	if (!IFileManager::Get().Move(*InDocumentPath, *TempPath, true, true))
	{
		if (bHadExisting)
		{
			IFileManager::Get().Move(*InDocumentPath, *BackupPath, true, true);
		}
		IFileManager::Get().Delete(*TempPath);
		OutError = TEXT("PRODUCTION_WRITE_FAILED: unable to publish production document");
		return false;
	}
	if (bHadExisting)
	{
		IFileManager::Get().Delete(*BackupPath);
	}
	return true;
}

FEpicodeProductionEditSession::FEpicodeProductionEditSession(const FEpicodeProductionDocument& InDocument)
	: Document(InDocument)
{
}

bool FEpicodeProductionEditSession::Apply(const FString& InDescription, TFunctionRef<bool(FEpicodeProductionDocument&, FString&)> InMutation, FString& OutError)
{
	const FString Snapshot = SerializeDocument(Document);
	FEpicodeProductionDocument Candidate;
	if (!DeserializeDocument(Snapshot, Candidate, OutError))
	{
		return false;
	}
	if (TransactionSink)
	{
		TransactionSink->BeginTransaction(InDescription);
	}
	if (!InMutation(Candidate, OutError))
	{
		if (TransactionSink)
		{
			TransactionSink->CancelTransaction();
		}
		return false;
	}
	if (!ValidateCandidate(Candidate, OutError))
	{
		if (TransactionSink)
		{
			TransactionSink->CancelTransaction();
		}
		return false;
	}
	PushUndo(Snapshot);
	Document = Candidate;
	RedoStack.Reset();
	bDirty = true;
	if (TransactionSink)
	{
		TransactionSink->CommitTransaction();
	}
	return true;
}

bool FEpicodeProductionEditSession::Undo(FString& OutError)
{
	if (UndoStack.Num() == 0)
	{
		OutError = TEXT("UNDO_UNAVAILABLE: no production edits to undo");
		return false;
	}
	const FString Current = SerializeDocument(Document);
	const FString Previous = UndoStack.Pop();
	FEpicodeProductionDocument Restored;
	if (!DeserializeDocument(Previous, Restored, OutError))
	{
		UndoStack.Add(Previous);
		return false;
	}
	RedoStack.Add(Current);
	Document = Restored;
	bDirty = true;
	return true;
}

bool FEpicodeProductionEditSession::Redo(FString& OutError)
{
	if (RedoStack.Num() == 0)
	{
		OutError = TEXT("REDO_UNAVAILABLE: no production edits to redo");
		return false;
	}
	const FString Current = SerializeDocument(Document);
	const FString Next = RedoStack.Pop();
	FEpicodeProductionDocument Restored;
	if (!DeserializeDocument(Next, Restored, OutError))
	{
		RedoStack.Add(Next);
		return false;
	}
	UndoStack.Add(Current);
	Document = Restored;
	bDirty = true;
	return true;
}

bool FEpicodeProductionEditSession::Save(const FString& InRoot, FString& OutDocumentPath, FString& OutError)
{
	if (!ValidateCandidate(Document, OutError))
	{
		return false;
	}
	if (!FEpicodeProductionLibrary::Save(Document, InRoot, OutDocumentPath, OutError))
	{
		return false;
	}
	bDirty = false;
	return true;
}

bool FEpicodeProductionEditSession::ReloadFromDisk(const FString& InRoot, FString& OutError)
{
	const FString DocumentPath = FEpicodeProductionLibrary::GetDocumentPath(InRoot, Document.ProductionId);
	if (!FEpicodeProductionLibrary::Load(DocumentPath, Document, OutError))
	{
		return false;
	}
	UndoStack.Reset();
	RedoStack.Reset();
	bDirty = false;
	return true;
}

bool FEpicodeProductionEditSession::ValidateCandidate(const FEpicodeProductionDocument& InCandidate, FString& OutError) const
{
	if (!InCandidate.Validate(OutError))
	{
		return false;
	}
	if (!SceneRoot.IsEmpty())
	{
		FEpicodeProductionValidationResult Result;
		if (!FEpicodeProductionValidator::ValidateBinding(InCandidate, SceneRoot, Result))
		{
			OutError = Result.GetFirstError();
			return false;
		}
	}
	return true;
}

FString FEpicodeProductionEditSession::SerializeDocument(const FEpicodeProductionDocument& InDocument) const
{
	return SerializeJsonObject(InDocument.ToJson(), false);
}

bool FEpicodeProductionEditSession::DeserializeDocument(const FString& InJson, FEpicodeProductionDocument& OutDocument, FString& OutError) const
{
	TSharedPtr<FJsonObject> Root;
	if (!ParseJsonObject(InJson, Root, OutError))
	{
		return false;
	}
	return FEpicodeProductionDocument::FromJson(Root, OutDocument, OutError);
}

void FEpicodeProductionEditSession::PushUndo(const FString& InSnapshot)
{
	UndoStack.Add(InSnapshot);
	constexpr int32 MaxHistory = 100;
	if (UndoStack.Num() > MaxHistory)
	{
		UndoStack.RemoveAt(0, UndoStack.Num() - MaxHistory);
	}
}

FEpicodeProductionDocument FEpicodeProductionLegacy::MakeDefaultView(const FEpicodeSceneDocument& InScene)
{
	FEpicodeProductionDocument Document = FEpicodeProductionDocument::CreateDefault(InScene.SceneId, InScene.Name);
	Document.ProductionId = TEXT("legacy-") + (InScene.SceneId.IsEmpty() ? FGuid::NewGuid().ToString(EGuidFormats::Digits) : InScene.SceneId);
	int32 SlotIndex = 0;
	for (const FEpicodeEntityRecord& Entity : InScene.Entities)
	{
		if (Entity.Type != EEpicodeEntityType::Character)
		{
			continue;
		}
		FEpicodeProductionCastMember Member;
		Member.Slot = FString::Printf(TEXT("character-%d"), ++SlotIndex);
		Member.EntityId = Entity.Id.ToString(EGuidFormats::DigitsWithHyphens);
		Member.ParticipantKind = TEXT("performer");
		Document.Cast.Add(Member);
	}
	Document.WorldRules = FEpicodeProductionDocument::MakeDefaultWorldRules();
	Document.Narrative = FEpicodeProductionDocument::MakeDefaultNarrative();
	return Document;
}
