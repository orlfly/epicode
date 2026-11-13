#include "EpicodeReplayCheckpoint.h"

#include "EpicodeReplayJournal.h"
#include "EpicodeSandboxJson.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	bool ParseJsonFile(const FString& InPath, TSharedPtr<FJsonObject>& OutObject)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *InPath))
		{
			return false;
		}
		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Text);
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
	}
}

TSharedPtr<FJsonObject> FEpicodeReplayCheckpoint::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("schemaVersion"), SchemaVersion);
	Result->SetNumberField(TEXT("sequence"), static_cast<double>(Sequence));
	Result->SetNumberField(TEXT("tick"), static_cast<double>(Tick));
	Result->SetNumberField(TEXT("tickRate"), TickRate);
	Result->SetNumberField(TEXT("seed"), Seed);
	Result->SetStringField(TEXT("contentDigest"), ContentDigest);
	Result->SetObjectField(TEXT("world"), World.ToJson());
	Result->SetObjectField(TEXT("random"), Random.ToJson());
	Result->SetStringField(TEXT("checksum"), ComputeChecksum());
	return Result;
}

bool FEpicodeReplayCheckpoint::FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeReplayCheckpoint& OutCheckpoint, FString& OutError)
{
	if (!InJson.IsValid())
	{
		OutError = TEXT("SNAPSHOT_CORRUPT: missing checkpoint");
		return false;
	}
	double Sequence = 0.0;
	double Tick = 0.0;
	const TSharedPtr<FJsonObject>* WorldJson = nullptr;
	const TSharedPtr<FJsonObject>* RandomJson = nullptr;
	if (!InJson->TryGetNumberField(TEXT("schemaVersion"), OutCheckpoint.SchemaVersion)
		|| !InJson->TryGetNumberField(TEXT("sequence"), Sequence)
		|| !InJson->TryGetNumberField(TEXT("tick"), Tick)
		|| !InJson->TryGetNumberField(TEXT("tickRate"), OutCheckpoint.TickRate)
		|| !InJson->TryGetNumberField(TEXT("seed"), OutCheckpoint.Seed)
		|| !InJson->TryGetStringField(TEXT("contentDigest"), OutCheckpoint.ContentDigest)
		|| !InJson->TryGetObjectField(TEXT("world"), WorldJson) || !WorldJson
		|| !InJson->TryGetObjectField(TEXT("random"), RandomJson) || !RandomJson)
	{
		OutError = TEXT("SNAPSHOT_CORRUPT: incomplete checkpoint");
		return false;
	}
	OutCheckpoint.Sequence = static_cast<int64>(Sequence);
	OutCheckpoint.Tick = static_cast<int64>(Tick);
	if (OutCheckpoint.SchemaVersion != 1 || OutCheckpoint.Sequence < 0 || OutCheckpoint.TickRate <= 0)
	{
		OutError = TEXT("SNAPSHOT_CORRUPT: invalid checkpoint metadata");
		return false;
	}
	if (!FEpicodeSandboxWorld::FromJson(*WorldJson, OutCheckpoint.World, OutError))
	{
		OutError = TEXT("SNAPSHOT_CORRUPT: ") + OutError;
		return false;
	}
	if (!FEpicodeSandboxRandomStreams::FromJson(*RandomJson, OutCheckpoint.Random, OutError))
	{
		OutError = TEXT("SNAPSHOT_CORRUPT: ") + OutError;
		return false;
	}
	const FString ExpectedChecksum = OutCheckpoint.ComputeChecksum();
	FString ActualChecksum;
	if (!InJson->TryGetStringField(TEXT("checksum"), ActualChecksum) || ActualChecksum != ExpectedChecksum)
	{
		OutError = TEXT("SNAPSHOT_CORRUPT: checksum mismatch");
		return false;
	}
	OutCheckpoint.Checksum = ActualChecksum;
	return true;
}

FString FEpicodeReplayCheckpoint::ComputeChecksum() const
{
	TSharedPtr<FJsonObject> Canonical = MakeShared<FJsonObject>();
	Canonical->SetNumberField(TEXT("schemaVersion"), SchemaVersion);
	Canonical->SetNumberField(TEXT("sequence"), static_cast<double>(Sequence));
	Canonical->SetNumberField(TEXT("tick"), static_cast<double>(Tick));
	Canonical->SetNumberField(TEXT("tickRate"), TickRate);
	Canonical->SetNumberField(TEXT("seed"), Seed);
	Canonical->SetStringField(TEXT("contentDigest"), ContentDigest);
	Canonical->SetObjectField(TEXT("world"), World.ToJson());
	Canonical->SetObjectField(TEXT("random"), Random.ToJson());
	return FEpicodeReplayChecksum::Sha1(FEpicodeSandboxJson::CanonicalString(Canonical));
}

FString FEpicodeReplayCheckpoints::GetCheckpointDirectory(const FString& InRoot)
{
	return FPaths::Combine(InRoot, TEXT("checkpoints"));
}

FString FEpicodeReplayCheckpoints::GetCheckpointPath(const FString& InRoot, int64 InSequence)
{
	return FPaths::Combine(GetCheckpointDirectory(InRoot), FString::Printf(TEXT("checkpoint-%lld.json"), InSequence));
}

bool FEpicodeReplayCheckpoints::Save(const FString& InRoot, const FEpicodeReplayCheckpoint& InCheckpoint, FString& OutError)
{
	if (InCheckpoint.Sequence < 0 || InCheckpoint.TickRate <= 0)
	{
		OutError = TEXT("SNAPSHOT_SAVE_FAILED: invalid checkpoint");
		return false;
	}
	FString WorldError;
	if (!InCheckpoint.World.Validate(WorldError))
	{
		OutError = FString::Printf(TEXT("SNAPSHOT_SAVE_FAILED: %s"), *WorldError);
		return false;
	}
	const FString Directory = GetCheckpointDirectory(InRoot);
	if (!IFileManager::Get().MakeDirectory(*Directory, true))
	{
		OutError = TEXT("SNAPSHOT_SAVE_FAILED: unable to create checkpoint directory");
		return false;
	}
	const FString FinalPath = GetCheckpointPath(InRoot, InCheckpoint.Sequence);
	const FString TempPath = FinalPath + TEXT(".tmp");
	const TSharedPtr<FJsonObject> Json = InCheckpoint.ToJson();
	if (!FFileHelper::SaveStringToFile(FEpicodeSandboxJson::Serialize(Json, true), *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		IFileManager::Get().Delete(*TempPath);
		OutError = TEXT("SNAPSHOT_SAVE_FAILED: unable to write checkpoint");
		return false;
	}
	if (!IFileManager::Get().Move(*FinalPath, *TempPath, true, true))
	{
		IFileManager::Get().Delete(*TempPath);
		OutError = TEXT("SNAPSHOT_SAVE_FAILED: unable to publish checkpoint");
		return false;
	}
	return true;
}

TArray<int64> FEpicodeReplayCheckpoints::List(const FString& InRoot)
{
	TArray<int64> Sequences;
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(GetCheckpointDirectory(InRoot), TEXT("checkpoint-*.json")), true, false);
	for (const FString& File : Files)
	{
		FString Base = FPaths::GetBaseFilename(File);
		FString Number = Base.RightChop(FCString::Strlen(TEXT("checkpoint-")));
		if (Number.IsNumeric())
		{
			Sequences.Add(FCString::Strtoui64(*Number, nullptr, 10));
		}
	}
	Sequences.Sort();
	return Sequences;
}

bool FEpicodeReplayCheckpoints::LoadAt(const FString& InRoot, int64 InSequence, FEpicodeReplayCheckpoint& OutCheckpoint, FString& OutError)
{
	TSharedPtr<FJsonObject> Json;
	if (!ParseJsonFile(GetCheckpointPath(InRoot, InSequence), Json))
	{
		OutError = FString::Printf(TEXT("SNAPSHOT_MISSING: checkpoint %lld"), InSequence);
		return false;
	}
	if (!FEpicodeReplayCheckpoint::FromJson(Json, OutCheckpoint, OutError))
	{
		return false;
	}
	if (OutCheckpoint.Sequence != InSequence)
	{
		OutError = TEXT("SNAPSHOT_CORRUPT: sequence does not match filename");
		return false;
	}
	return true;
}

bool FEpicodeReplayCheckpoints::LoadNearest(const FString& InRoot, int64 InTargetSequence, FEpicodeReplayCheckpoint& OutCheckpoint, FString& OutError)
{
	const TArray<int64> Sequences = List(InRoot);
	int64 Chosen = -1;
	for (int64 Sequence : Sequences)
	{
		if (Sequence <= InTargetSequence && Sequence >= Chosen)
		{
			Chosen = Sequence;
		}
	}
	if (Chosen < 0)
	{
		OutError = FString::Printf(TEXT("SNAPSHOT_MISSING: no checkpoint at or before %lld"), InTargetSequence);
		return false;
	}
	return LoadAt(InRoot, Chosen, OutCheckpoint, OutError);
}
