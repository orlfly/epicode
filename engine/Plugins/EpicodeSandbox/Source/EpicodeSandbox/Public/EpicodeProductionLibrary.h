#pragma once

#include "CoreMinimal.h"
#include "EpicodeProductionDocument.h"

class IEpicodeTransactionSink;

struct EPICODESANDBOX_API FEpicodeProductionLibraryEntry
{
	FString ProductionId;
	FString Name;
	FString Directory;
	FString DocumentPath;
	FDateTime ModifiedTime;
	bool bCurrent = false;
	bool bAvailable = true;
	FString Error;

	TSharedPtr<FJsonObject> ToJson() const;
};

class EPICODESANDBOX_API FEpicodeProductionLibrary
{
public:
	static FString GetDocumentFileName() { return TEXT("production.json"); }

	static TArray<FEpicodeProductionLibraryEntry> Scan(const FString& InRoot, const FString& InCurrentProductionId = FString());
	static bool Load(const FString& InDocumentPath, FEpicodeProductionDocument& OutDocument, FString& OutError);
	static bool Create(const FEpicodeProductionDocument& InDocument, const FString& InRoot, FString& OutDocumentPath, FString& OutError);
	static bool Save(const FEpicodeProductionDocument& InDocument, const FString& InRoot, FString& OutDocumentPath, FString& OutError);
	static bool SaveAs(const FEpicodeProductionDocument& InDocument, const FString& InRoot, const FString& InNewName, FEpicodeProductionDocument& OutDocument, FString& OutDocumentPath, FString& OutError);
	static bool Delete(const FEpicodeProductionLibraryEntry& InEntry, const FString& InTrashRoot, const FString& InCurrentProductionId, FString& OutTrashPath, FString& OutError);
	static bool Find(const FString& InRoot, const FString& InProductionId, FEpicodeProductionLibraryEntry& OutEntry);
	static FString GetProductionDirectory(const FString& InRoot, const FString& InProductionId);
	static FString GetDocumentPath(const FString& InRoot, const FString& InProductionId);
	static bool WriteAtomic(const FEpicodeProductionDocument& InDocument, const FString& InDocumentPath, FString& OutError);
};

class EPICODESANDBOX_API FEpicodeProductionEditSession
{
public:
	explicit FEpicodeProductionEditSession(const FEpicodeProductionDocument& InDocument = FEpicodeProductionDocument());

	void SetSceneRoot(const FString& InSceneRoot) { SceneRoot = InSceneRoot; }
	const FString& GetSceneRoot() const { return SceneRoot; }
	void SetTransactionSink(IEpicodeTransactionSink* InSink) { TransactionSink = InSink; }
	const FEpicodeProductionDocument& GetDocument() const { return Document; }
	bool IsDirty() const { return bDirty; }

	bool Apply(const FString& InDescription, TFunctionRef<bool(FEpicodeProductionDocument&, FString&)> InMutation, FString& OutError);
	bool Undo(FString& OutError);
	bool Redo(FString& OutError);
	bool Save(const FString& InRoot, FString& OutDocumentPath, FString& OutError);
	bool ReloadFromDisk(const FString& InRoot, FString& OutError);

	int32 GetUndoCount() const { return UndoStack.Num(); }
	int32 GetRedoCount() const { return RedoStack.Num(); }

private:
	bool ValidateCandidate(const FEpicodeProductionDocument& InCandidate, FString& OutError) const;
	FString SerializeDocument(const FEpicodeProductionDocument& InDocument) const;
	bool DeserializeDocument(const FString& InJson, FEpicodeProductionDocument& OutDocument, FString& OutError) const;
	void PushUndo(const FString& InSnapshot);

	FEpicodeProductionDocument Document;
	FString SceneRoot;
	IEpicodeTransactionSink* TransactionSink = nullptr;
	TArray<FString> UndoStack;
	TArray<FString> RedoStack;
	bool bDirty = false;
};

struct EPICODESANDBOX_API FEpicodeProductionLegacy
{
	static FEpicodeProductionDocument MakeDefaultView(const FEpicodeSceneDocument& InScene);
};
