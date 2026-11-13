#include "EpicodeReplayJournal.h"

#include "EpicodeSandboxJson.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	FString JsonLine(const TSharedPtr<FJsonObject>& InObject)
	{
		return FEpicodeSandboxJson::Serialize(InObject, false) + TEXT("\n");
	}

	bool ParseLine(const FString& InLine, TSharedPtr<FJsonObject>& OutObject)
	{
		if (InLine.TrimStartAndEnd().IsEmpty())
		{
			return false;
		}
		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(InLine);
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
	}

}

TSharedPtr<FJsonObject> FEpicodeReplayJournalHeader::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("kind"), TEXT("header"));
	Result->SetNumberField(TEXT("schemaVersion"), SchemaVersion);
	Result->SetStringField(TEXT("sessionId"), SessionId);
	Result->SetStringField(TEXT("contentDigest"), ContentDigest);
	Result->SetNumberField(TEXT("seed"), Seed);
	Result->SetNumberField(TEXT("startSequence"), static_cast<double>(StartSequence));
	Result->SetStringField(TEXT("checksum"), ComputeChecksum());
	return Result;
}

bool FEpicodeReplayJournalHeader::FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeReplayJournalHeader& OutHeader, FString& OutError)
{
	if (!InJson.IsValid())
	{
		OutError = TEXT("JOURNAL_CORRUPT: missing header");
		return false;
	}
	double StartSequence = 1.0;
	if (!InJson->TryGetNumberField(TEXT("schemaVersion"), OutHeader.SchemaVersion)
		|| !InJson->TryGetStringField(TEXT("sessionId"), OutHeader.SessionId)
		|| !InJson->TryGetStringField(TEXT("contentDigest"), OutHeader.ContentDigest)
		|| !InJson->TryGetNumberField(TEXT("seed"), OutHeader.Seed)
		|| !InJson->TryGetNumberField(TEXT("startSequence"), StartSequence))
	{
		OutError = TEXT("JOURNAL_CORRUPT: incomplete header");
		return false;
	}
	OutHeader.StartSequence = static_cast<int64>(StartSequence);
	if (OutHeader.SchemaVersion != 1 || OutHeader.SessionId.IsEmpty() || OutHeader.StartSequence < 1)
	{
		OutError = TEXT("JOURNAL_CORRUPT: unsupported header");
		return false;
	}
	FString ExpectedChecksum;
	if (!InJson->TryGetStringField(TEXT("checksum"), ExpectedChecksum) || ExpectedChecksum != OutHeader.ComputeChecksum())
	{
		OutError = TEXT("JOURNAL_CORRUPT: header checksum mismatch");
		return false;
	}
	return true;
}

FString FEpicodeReplayJournalHeader::ComputeChecksum() const
{
	TSharedPtr<FJsonObject> Canonical = MakeShared<FJsonObject>();
	Canonical->SetNumberField(TEXT("schemaVersion"), SchemaVersion);
	Canonical->SetStringField(TEXT("sessionId"), SessionId);
	Canonical->SetStringField(TEXT("contentDigest"), ContentDigest);
	Canonical->SetNumberField(TEXT("seed"), Seed);
	Canonical->SetNumberField(TEXT("startSequence"), static_cast<double>(StartSequence));
	return FEpicodeReplayChecksum::Sha1(FEpicodeSandboxJson::CanonicalString(Canonical));
}

TSharedPtr<FJsonObject> FEpicodeReplayJournalEntry::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("kind"), TEXT("event"));
	Result->SetObjectField(TEXT("event"), Event.ToJson());
	Result->SetStringField(TEXT("time"), Time);
	Result->SetStringField(TEXT("worldDigest"), WorldDigest);
	Result->SetStringField(TEXT("checksum"), ComputeChecksum());
	return Result;
}

bool FEpicodeReplayJournalEntry::FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeReplayJournalEntry& OutEntry, FString& OutError)
{
	if (!InJson.IsValid())
	{
		OutError = TEXT("JOURNAL_CORRUPT: missing event");
		return false;
	}
	const TSharedPtr<FJsonObject>* EventJson = nullptr;
	if (!InJson->TryGetObjectField(TEXT("event"), EventJson) || !EventJson)
	{
		OutError = TEXT("JOURNAL_CORRUPT: event line has no event");
		return false;
	}
	if (!FEpicodeSandboxEvent::FromJson(*EventJson, OutEntry.Event, OutError))
	{
		return false;
	}
	InJson->TryGetStringField(TEXT("time"), OutEntry.Time);
	InJson->TryGetStringField(TEXT("worldDigest"), OutEntry.WorldDigest);
	FString ExpectedChecksum;
	if (!InJson->TryGetStringField(TEXT("checksum"), ExpectedChecksum) || ExpectedChecksum != OutEntry.ComputeChecksum())
	{
		OutError = TEXT("JOURNAL_CORRUPT: event checksum mismatch");
		return false;
	}
	OutEntry.Checksum = ExpectedChecksum;
	return true;
}

FString FEpicodeReplayJournalEntry::ComputeChecksum() const
{
	return FEpicodeReplayChecksum::Sha1(FEpicodeSandboxJson::CanonicalString(Event.ToJson()) + TEXT("|") + Time + TEXT("|") + WorldDigest);
}

FString FEpicodeReplayChecksum::Sha1(const FString& InText)
{
	FTCHARToUTF8 Utf8(*InText);
	FSHA1 Hasher;
	Hasher.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	return Hasher.Finalize().ToString();
}

FString FEpicodeReplayChecksum::Combine(const TArray<FString>& InChecksums)
{
	return Sha1(FString::Join(InChecksums, TEXT("|")));
}

FString FEpicodeReplayJournal::GetJournalPath(const FString& InRoot)
{
	return FPaths::Combine(InRoot, TEXT("journal.jsonl"));
}

FString FEpicodeReplayJournal::GetMetadataPath(const FString& InRoot)
{
	return FPaths::Combine(InRoot, TEXT("replay.json"));
}

bool FEpicodeReplayJournal::Create(const FString& InRoot, const FEpicodeReplayJournalHeader& InHeader, FString& OutError)
{
	if (InHeader.SessionId.IsEmpty() || InHeader.SchemaVersion != 1 || InHeader.StartSequence < 1)
	{
		OutError = TEXT("JOURNAL_CREATE_FAILED: invalid header");
		return false;
	}
	if (!IFileManager::Get().MakeDirectory(*InRoot, true))
	{
		OutError = TEXT("JOURNAL_CREATE_FAILED: unable to create directory");
		return false;
	}
	const FString JournalPath = GetJournalPath(InRoot);
	if (IFileManager::Get().FileExists(*JournalPath))
	{
		OutError = TEXT("JOURNAL_EXISTS: journal already exists");
		return false;
	}
	Root = InRoot;
	Header = InHeader;
	Entries.Reset();
	FinalDigest.Reset();
	bFinalized = false;
	bOpen = true;
	if (!WriteHeader(OutError))
	{
		bOpen = false;
		return false;
	}
	TSharedPtr<FJsonObject> Metadata = Header.ToJson();
	Metadata->RemoveField(TEXT("kind"));
	FFileHelper::SaveStringToFile(JsonLine(Metadata), *GetMetadataPath(InRoot), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	return true;
}

bool FEpicodeReplayJournal::WriteHeader(FString& OutError)
{
	return FFileHelper::SaveStringToFile(JsonLine(Header.ToJson()), *GetJournalPath(Root), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool FEpicodeReplayJournal::AppendLine(const FString& InLine, FString& OutError)
{
	if (!FFileHelper::SaveStringToFile(InLine, *GetJournalPath(Root), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append))
	{
		OutError = TEXT("JOURNAL_WRITE_FAILED: unable to append");
		return false;
	}
	return true;
}

bool FEpicodeReplayJournal::Append(const FEpicodeSandboxEvent& InEvent, const FString& InWorldDigest, const FString& InTime, FString& OutError)
{
	if (!bOpen)
	{
		OutError = TEXT("JOURNAL_NOT_OPEN: journal is not open");
		return false;
	}
	if (bFinalized)
	{
		OutError = TEXT("JOURNAL_FINALIZED: journal is finalized");
		return false;
	}
	const int64 ExpectedSequence = GetLastSequence() + 1;
	if (InEvent.Sequence != ExpectedSequence)
	{
		OutError = FString::Printf(TEXT("JOURNAL_SEQUENCE: expected %lld, got %lld"), ExpectedSequence, InEvent.Sequence);
		return false;
	}
	if (InEvent.Type.IsEmpty() || InEvent.Sequence <= 0)
	{
		OutError = TEXT("JOURNAL_EVENT_INVALID: missing type or sequence");
		return false;
	}
	FEpicodeReplayJournalEntry Entry;
	Entry.Event = InEvent;
	Entry.WorldDigest = InWorldDigest;
	Entry.Time = InTime.IsEmpty() ? FDateTime::UtcNow().ToIso8601() : InTime;
	Entry.Checksum = Entry.ComputeChecksum();
	if (!AppendLine(JsonLine(Entry.ToJson()), OutError))
	{
		return false;
	}
	Entries.Add(Entry);
	return true;
}

bool FEpicodeReplayJournal::Finalize(FString& OutError)
{
	if (!bOpen)
	{
		OutError = TEXT("JOURNAL_NOT_OPEN: journal is not open");
		return false;
	}
	if (bFinalized)
	{
		return true;
	}
	TArray<FString> Checksums;
	for (const FEpicodeReplayJournalEntry& Entry : Entries)
	{
		Checksums.Add(Entry.Checksum);
	}
	FinalDigest = FEpicodeReplayChecksum::Combine(Checksums);
	TSharedPtr<FJsonObject> Footer = MakeShared<FJsonObject>();
	Footer->SetStringField(TEXT("kind"), TEXT("footer"));
	Footer->SetNumberField(TEXT("lastSequence"), static_cast<double>(GetLastSequence()));
	Footer->SetStringField(TEXT("digest"), FinalDigest);
	if (!AppendLine(JsonLine(Footer), OutError))
	{
		return false;
	}
	bFinalized = true;
	return true;
}

bool FEpicodeReplayJournal::Open(const FString& InRoot, FString& OutError)
{
	Root = InRoot;
	Entries.Reset();
	FinalDigest.Reset();
	bOpen = false;
	bFinalized = false;
	Header = FEpicodeReplayJournalHeader();
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *GetJournalPath(InRoot)))
	{
		OutError = TEXT("JOURNAL_CORRUPT: unable to read journal");
		return false;
	}
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, false);
	bool bHeaderRead = false;
	bool bFooterRead = false;
	for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
	{
		TSharedPtr<FJsonObject> Line;
		if (!ParseLine(Lines[LineIndex], Line))
		{
			if (Lines[LineIndex].TrimStartAndEnd().IsEmpty())
			{
				continue;
			}
			OutError = FString::Printf(TEXT("JOURNAL_CORRUPT: invalid JSON at line %d"), LineIndex + 1);
			return false;
		}
		FString Kind;
		Line->TryGetStringField(TEXT("kind"), Kind);
		if (Kind == TEXT("header"))
		{
			if (bHeaderRead || LineIndex != 0 || !FEpicodeReplayJournalHeader::FromJson(Line, Header, OutError))
			{
				OutError = TEXT("JOURNAL_CORRUPT: invalid header placement");
				return false;
			}
			bHeaderRead = true;
			continue;
		}
		if (!bHeaderRead || bFooterRead)
		{
			OutError = TEXT("JOURNAL_CORRUPT: record outside header/footer bounds");
			return false;
		}
		if (Kind == TEXT("footer"))
		{
			double LastSequence = 0.0;
			FString Digest;
			if (!Line->TryGetNumberField(TEXT("lastSequence"), LastSequence) || !Line->TryGetStringField(TEXT("digest"), Digest))
			{
				OutError = TEXT("JOURNAL_CORRUPT: incomplete footer");
				return false;
			}
			TArray<FString> Checksums;
			for (const FEpicodeReplayJournalEntry& Entry : Entries)
			{
				Checksums.Add(Entry.Checksum);
			}
			const FString ActualDigest = FEpicodeReplayChecksum::Combine(Checksums);
			if (static_cast<int64>(LastSequence) != GetLastSequence() || Digest != ActualDigest)
			{
				OutError = TEXT("JOURNAL_CORRUPT: footer digest mismatch");
				return false;
			}
			FinalDigest = Digest;
			bFooterRead = true;
			continue;
		}
		if (Kind != TEXT("event"))
		{
			OutError = TEXT("JOURNAL_CORRUPT: unknown record kind");
			return false;
		}
		FEpicodeReplayJournalEntry Entry;
		if (!FEpicodeReplayJournalEntry::FromJson(Line, Entry, OutError))
		{
			return false;
		}
		const int64 ExpectedSequence = Entries.Num() == 0 ? Header.StartSequence : Entries.Last().Event.Sequence + 1;
		if (Entry.Event.Sequence != ExpectedSequence)
		{
			OutError = FString::Printf(TEXT("JOURNAL_CORRUPT: sequence gap at %lld"), Entry.Event.Sequence);
			return false;
		}
		Entries.Add(Entry);
	}
	if (!bHeaderRead)
	{
		OutError = TEXT("JOURNAL_CORRUPT: missing header");
		return false;
	}
	bFinalized = bFooterRead;
	bOpen = true;
	return true;
}
