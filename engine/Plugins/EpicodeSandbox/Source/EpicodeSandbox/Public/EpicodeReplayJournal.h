#pragma once

#include "CoreMinimal.h"
#include "EpicodeSandboxWorld.h"

struct EPICODESANDBOX_API FEpicodeReplayJournalHeader
{
	int32 SchemaVersion = 1;
	FString SessionId;
	FString ContentDigest;
	int32 Seed = 0;
	int64 StartSequence = 1;

	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeReplayJournalHeader& OutHeader, FString& OutError);
	FString ComputeChecksum() const;
};

struct EPICODESANDBOX_API FEpicodeReplayJournalEntry
{
	FEpicodeSandboxEvent Event;
	FString Time;
	FString WorldDigest;
	FString Checksum;

	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeReplayJournalEntry& OutEntry, FString& OutError);
	FString ComputeChecksum() const;
};

class EPICODESANDBOX_API FEpicodeReplayChecksum
{
public:
	static FString Sha1(const FString& InText);
	static FString Combine(const TArray<FString>& InChecksums);
};

class EPICODESANDBOX_API FEpicodeReplayJournal
{
public:
	bool Create(const FString& InRoot, const FEpicodeReplayJournalHeader& InHeader, FString& OutError);
	bool Open(const FString& InRoot, FString& OutError);
	bool Append(const FEpicodeSandboxEvent& InEvent, const FString& InWorldDigest, const FString& InTime, FString& OutError);
	bool Finalize(FString& OutError);

	bool IsOpen() const { return bOpen; }
	bool IsFinalized() const { return bFinalized; }
	const FEpicodeReplayJournalHeader& GetHeader() const { return Header; }
	const TArray<FEpicodeReplayJournalEntry>& GetEntries() const { return Entries; }
	int64 GetFirstSequence() const { return Header.StartSequence; }
	int64 GetLastSequence() const { return Entries.Num() > 0 ? Entries.Last().Event.Sequence : Header.StartSequence - 1; }
	const FString& GetFinalDigest() const { return FinalDigest; }
	static FString GetJournalPath(const FString& InRoot);
	static FString GetMetadataPath(const FString& InRoot);

private:
	bool WriteHeader(FString& OutError);
	bool AppendLine(const FString& InLine, FString& OutError);

	FString Root;
	FEpicodeReplayJournalHeader Header;
	TArray<FEpicodeReplayJournalEntry> Entries;
	FString FinalDigest;
	bool bOpen = false;
	bool bFinalized = false;
};
