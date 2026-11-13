#include "EpicodeReplayRetention.h"

#include "EpicodeSandboxJson.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	FString JournalLine(const TSharedPtr<FJsonObject>& InObject)
	{
		return FEpicodeSandboxJson::Serialize(InObject, false) + TEXT("\n");
	}
}

FString FEpicodeReplayRetention::GetArchivePath(const FString& InRoot)
{
	return FPaths::Combine(InRoot, TEXT("journal.archive.jsonl"));
}

bool FEpicodeReplayRetention::Compact(
	const FString& InRoot,
	int64 InKeepFromSequence,
	EEpicodeSandboxParticipantKind InActorKind,
	FString& OutError)
{
	if (!FEpicodeReplayState::CanReplay(InActorKind, OutError))
	{
		return false;
	}
	FEpicodeReplayJournal Journal;
	if (!Journal.Open(InRoot, OutError))
	{
		return false;
	}
	if (InKeepFromSequence <= Journal.GetFirstSequence())
	{
		return true;
	}
	FEpicodeReplayCheckpoint Boundary;
	if (!FEpicodeReplayCheckpoints::LoadNearest(InRoot, InKeepFromSequence, Boundary, OutError))
	{
		return false;
	}
	const int64 KeepFrom = Boundary.Sequence;
	if (KeepFrom <= Journal.GetFirstSequence() - 1)
	{
		return true;
	}

	TArray<FEpicodeReplayJournalEntry> KeptEntries;
	for (const FEpicodeReplayJournalEntry& Entry : Journal.GetEntries())
	{
		if (Entry.Event.Sequence > KeepFrom)
		{
			KeptEntries.Add(Entry);
		}
	}
	FEpicodeReplayJournalHeader Header = Journal.GetHeader();
	Header.StartSequence = KeepFrom + 1;

	FString NewText = JournalLine(Header.ToJson());
	TArray<FString> Checksums;
	for (const FEpicodeReplayJournalEntry& Entry : KeptEntries)
	{
		NewText += JournalLine(Entry.ToJson());
		Checksums.Add(Entry.Checksum);
	}
	TSharedPtr<FJsonObject> Footer = MakeShared<FJsonObject>();
	Footer->SetStringField(TEXT("kind"), TEXT("footer"));
	Footer->SetNumberField(TEXT("lastSequence"), static_cast<double>(Journal.GetLastSequence()));
	Footer->SetStringField(TEXT("digest"), FEpicodeReplayChecksum::Combine(Checksums));
	NewText += JournalLine(Footer);

	const FString JournalPath = FEpicodeReplayJournal::GetJournalPath(InRoot);
	const FString TempPath = JournalPath + TEXT(".compact.tmp");
	const FString ArchivePath = GetArchivePath(InRoot);
	if (!FFileHelper::SaveStringToFile(NewText, *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = TEXT("REPLAY_COMPACTION_FAILED: unable to write compacted journal");
		return false;
	}
	if (IFileManager::Get().FileExists(*ArchivePath))
	{
		IFileManager::Get().Delete(*ArchivePath);
	}
	if (!IFileManager::Get().Move(*ArchivePath, *JournalPath, true, true))
	{
		IFileManager::Get().Delete(*TempPath);
		OutError = TEXT("REPLAY_COMPACTION_FAILED: unable to archive journal");
		return false;
	}
	if (!IFileManager::Get().Move(*JournalPath, *TempPath, true, true))
	{
		IFileManager::Get().Move(*JournalPath, *ArchivePath, true, true);
		OutError = TEXT("REPLAY_COMPACTION_FAILED: unable to publish compacted journal");
		return false;
	}
	for (int64 Sequence : FEpicodeReplayCheckpoints::List(InRoot))
	{
		if (Sequence < Boundary.Sequence)
		{
			IFileManager::Get().Delete(*FEpicodeReplayCheckpoints::GetCheckpointPath(InRoot, Sequence));
		}
	}
	return true;
}

bool FEpicodeReplayRetention::ApplyPolicy(
	const FString& InRoot,
	const FEpicodeReplayRetentionPolicy& InPolicy,
	EEpicodeSandboxParticipantKind InActorKind,
	FString& OutError)
{
	if (!FEpicodeReplayState::CanReplay(InActorKind, OutError))
	{
		return false;
	}
	FEpicodeReplayJournal Journal;
	if (!Journal.Open(InRoot, OutError))
	{
		return false;
	}
	int64 KeepFrom = 0;
	if (InPolicy.MaxEvents > 0 && Journal.GetEntries().Num() > InPolicy.MaxEvents)
	{
		KeepFrom = Journal.GetLastSequence() - InPolicy.MaxEvents + 1;
	}
	if (InPolicy.RetentionHours > 0)
	{
		const FDateTime Modified = IFileManager::Get().GetTimeStamp(*FEpicodeReplayJournal::GetJournalPath(InRoot));
		if (Modified != FDateTime::MinValue() && FDateTime::UtcNow() - Modified > FTimespan::FromHours(InPolicy.RetentionHours))
		{
			KeepFrom = FMath::Max<int64>(KeepFrom, Journal.GetLastSequence());
		}
	}
	if (KeepFrom <= Journal.GetFirstSequence())
	{
		return true;
	}
	return Compact(InRoot, KeepFrom, InActorKind, OutError);
}
