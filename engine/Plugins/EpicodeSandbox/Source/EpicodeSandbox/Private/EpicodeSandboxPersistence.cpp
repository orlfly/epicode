#include "EpicodeSandboxPersistence.h"

#include "EpicodeSandboxJson.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FString FEpicodeSandboxPersistence::GetSnapshotPath(const FString& InSessionRoot)
{
	return FPaths::Combine(InSessionRoot, TEXT("snapshot.json"));
}

bool FEpicodeSandboxPersistence::Save(const FEpicodeSandboxWorld& InWorld, const FEpicodeSandboxEventStream& InEvents, const FString& InSessionRoot, FString& OutError)
{
	FString WorldError;
	if (!InWorld.Validate(WorldError))
	{
		OutError = FString::Printf(TEXT("SESSION_SAVE_FAILED: %s"), *WorldError);
		return false;
	}
	if (!IFileManager::Get().MakeDirectory(*InSessionRoot, true))
	{
		OutError = TEXT("SESSION_SAVE_FAILED: unable to create session directory");
		return false;
	}

	TSharedPtr<FJsonObject> Snapshot = MakeShared<FJsonObject>();
	Snapshot->SetNumberField(TEXT("schemaVersion"), 1);
	Snapshot->SetStringField(TEXT("digest"), FEpicodeSandboxDigest::ComputeCombinedDigest(InWorld, InEvents.GetEvents()));
	Snapshot->SetObjectField(TEXT("world"), InWorld.ToJson());
	Snapshot->SetObjectField(TEXT("events"), InEvents.ToJson());

	const FString FinalPath = GetSnapshotPath(InSessionRoot);
	const FString TempPath = FinalPath + TEXT(".tmp");
	if (!FFileHelper::SaveStringToFile(FEpicodeSandboxJson::Serialize(Snapshot, true), *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		IFileManager::Get().Delete(*TempPath);
		OutError = TEXT("SESSION_SAVE_FAILED: unable to write snapshot");
		return false;
	}
	if (!IFileManager::Get().Move(*FinalPath, *TempPath, true, true))
	{
		IFileManager::Get().Delete(*TempPath);
		OutError = TEXT("SESSION_SAVE_FAILED: unable to publish snapshot");
		return false;
	}
	return true;
}

bool FEpicodeSandboxPersistence::Load(const FString& InSessionRoot, FEpicodeSandboxWorld& OutWorld, FEpicodeSandboxEventStream& OutEvents, FString& OutError)
{
	const FString SnapshotPath = GetSnapshotPath(InSessionRoot);
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *SnapshotPath))
	{
		OutError = FString::Printf(TEXT("SESSION_RESTORE_FAILED: unable to read %s"), *SnapshotPath);
		return false;
	}
	TSharedPtr<FJsonObject> Snapshot;
	const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Snapshot) || !Snapshot.IsValid())
	{
		OutError = TEXT("SESSION_RESTORE_FAILED: snapshot is not valid JSON");
		return false;
	}
	const TSharedPtr<FJsonObject>* WorldJson = nullptr;
	const TSharedPtr<FJsonObject>* EventsJson = nullptr;
	if (!Snapshot->TryGetObjectField(TEXT("world"), WorldJson) || !WorldJson || !Snapshot->TryGetObjectField(TEXT("events"), EventsJson) || !EventsJson)
	{
		OutError = TEXT("SESSION_RESTORE_FAILED: snapshot is missing world or events");
		return false;
	}

	FEpicodeSandboxWorld World;
	FEpicodeSandboxEventStream Events;
	if (!FEpicodeSandboxWorld::FromJson(*WorldJson, World, OutError))
	{
		OutError = TEXT("SESSION_RESTORE_FAILED: ") + OutError;
		return false;
	}
	if (!FEpicodeSandboxEventStream::FromJson(*EventsJson, Events, OutError))
	{
		OutError = TEXT("SESSION_RESTORE_FAILED: ") + OutError;
		return false;
	}
	FString ExpectedDigest;
	Snapshot->TryGetStringField(TEXT("digest"), ExpectedDigest);
	const FString ActualDigest = FEpicodeSandboxDigest::ComputeCombinedDigest(World, Events.GetEvents());
	if (ExpectedDigest.IsEmpty() || ExpectedDigest != ActualDigest)
	{
		OutError = TEXT("SESSION_RESTORE_FAILED: snapshot checksum mismatch");
		return false;
	}
	OutWorld = World;
	OutEvents = Events;
	return true;
}
