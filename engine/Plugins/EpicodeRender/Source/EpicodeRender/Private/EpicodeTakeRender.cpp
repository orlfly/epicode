#include "EpicodeTakeRender.h"

#include "EpicodeContentDigest.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString SerializeTakeJson(const TSharedPtr<FJsonObject>& InJson)
	{
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(InJson.ToSharedRef(), Writer);
		return Text;
	}

	FString DefaultTakeRoot()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Epicode"), TEXT("Takes"));
	}
}

TSharedPtr<FJsonObject> FEpicodeTakeRenderTrack::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("cameraId"), CameraId);
	Result->SetNumberField(TEXT("startSequence"), static_cast<double>(StartSequence));
	Result->SetNumberField(TEXT("endSequence"), static_cast<double>(EndSequence));
	return Result;
}

bool FEpicodeTakeRenderTrack::FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeTakeRenderTrack& OutTrack, FString& OutError)
{
	if (!InJson.IsValid())
	{
		OutError = TEXT("TAKE_TRACK_INVALID: missing track");
		return false;
	}
	if (!InJson->TryGetStringField(TEXT("cameraId"), OutTrack.CameraId) || OutTrack.CameraId.IsEmpty())
	{
		OutError = TEXT("TAKE_CAMERA_MISSING: cameraId is required");
		return false;
	}
	double Start = 0.0;
	double End = 0.0;
	InJson->TryGetNumberField(TEXT("startSequence"), Start);
	InJson->TryGetNumberField(TEXT("endSequence"), End);
	OutTrack.StartSequence = static_cast<int64>(Start);
	OutTrack.EndSequence = static_cast<int64>(End);
	if (OutTrack.StartSequence > OutTrack.EndSequence)
	{
		OutError = TEXT("TAKE_TRACK_INVALID: startSequence must not exceed endSequence");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FEpicodeTakeRenderRequest::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("takeRoot"), TakeRoot);
	Result->SetStringField(TEXT("takeId"), TakeId);
	Result->SetNumberField(TEXT("takeVersion"), TakeVersion);
	Result->SetStringField(TEXT("outputDirectory"), OutputDirectory);
	Result->SetStringField(TEXT("format"), Format);
	Result->SetStringField(TEXT("expectedContentDigest"), ExpectedContentDigest);
	TArray<TSharedPtr<FJsonValue>> TrackValues;
	for (const FEpicodeTakeRenderTrack& Track : Tracks)
	{
		TrackValues.Add(MakeShared<FJsonValueObject>(Track.ToJson()));
	}
	Result->SetArrayField(TEXT("tracks"), TrackValues);
	return Result;
}

bool FEpicodeTakeRenderRequest::FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeTakeRenderRequest& OutRequest, FString& OutError)
{
	if (!InJson.IsValid())
	{
		OutError = TEXT("take render requires parameters");
		return false;
	}
	if (!InJson->TryGetStringField(TEXT("takeId"), OutRequest.TakeId) || OutRequest.TakeId.IsEmpty())
	{
		OutError = TEXT("takeId is required");
		return false;
	}
	double Version = 0.0;
	if (!InJson->TryGetNumberField(TEXT("takeVersion"), Version) || Version < 1)
	{
		OutError = TEXT("takeVersion must be at least 1");
		return false;
	}
	OutRequest.TakeVersion = static_cast<int32>(Version);
	InJson->TryGetStringField(TEXT("takeRoot"), OutRequest.TakeRoot);
	if (OutRequest.TakeRoot.IsEmpty())
	{
		OutRequest.TakeRoot = DefaultTakeRoot();
	}
	InJson->TryGetStringField(TEXT("outputDirectory"), OutRequest.OutputDirectory);
	InJson->TryGetStringField(TEXT("format"), OutRequest.Format);
	if (OutRequest.Format.IsEmpty())
	{
		OutRequest.Format = TEXT("png");
	}
	InJson->TryGetStringField(TEXT("expectedContentDigest"), OutRequest.ExpectedContentDigest);
	const TArray<TSharedPtr<FJsonValue>>* Tracks = nullptr;
	if (InJson->TryGetArrayField(TEXT("tracks"), Tracks) && Tracks)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Tracks)
		{
			FEpicodeTakeRenderTrack Track;
			if (!FEpicodeTakeRenderTrack::FromJson(Value.IsValid() ? Value->AsObject() : nullptr, Track, OutError))
			{
				return false;
			}
			OutRequest.Tracks.Add(Track);
		}
	}
	return true;
}

FString FEpicodeTakeRender::ComputeTakeDigest(const FEpicodeDirectorTake& InTake)
{
	return FEpicodeContentDigest::HashString(SerializeTakeJson(InTake.ToJson()));
}

bool FEpicodeTakeRender::ValidateTracks(const FEpicodeDirectorTake& InTake, const TArray<FEpicodeTakeRenderTrack>& InTracks, FString& OutError)
{
	TArray<FEpicodeTakeRenderTrack> Effective;
	return ResolveTracks(InTake, InTracks, Effective, OutError);
}

bool FEpicodeTakeRender::ResolveTracks(
	const FEpicodeDirectorTake& InTake,
	const TArray<FEpicodeTakeRenderTrack>& InTracks,
	TArray<FEpicodeTakeRenderTrack>& OutTracks,
	FString& OutError)
{
	TArray<FEpicodeTakeRenderTrack> Effective = InTracks;
	if (Effective.Num() == 0 && InTake.CameraIds.Num() > 0)
	{
		FEpicodeTakeRenderTrack Track;
		Track.CameraId = InTake.CameraIds[0];
		Track.StartSequence = InTake.InSequence;
		Track.EndSequence = InTake.OutSequence;
		Effective.Add(Track);
	}
	if (Effective.Num() == 0)
	{
		OutError = TEXT("TAKE_CAMERA_MISSING: take has no camera tracks");
		return false;
	}
	int64 LastEnd = InTake.InSequence;
	for (const FEpicodeTakeRenderTrack& Track : Effective)
	{
		if (!InTake.CameraIds.Contains(Track.CameraId))
		{
			OutError = FString::Printf(TEXT("TAKE_CAMERA_MISSING: %s"), *Track.CameraId);
			return false;
		}
		if (Track.StartSequence < InTake.InSequence || Track.EndSequence > InTake.OutSequence)
		{
			OutError = TEXT("TAKE_TRACK_OUT_OF_RANGE: track is outside the take range");
			return false;
		}
		if (Track.StartSequence < LastEnd)
		{
			OutError = TEXT("TAKE_TRACK_OVERLAP: camera tracks overlap");
			return false;
		}
		LastEnd = Track.EndSequence;
	}
	OutTracks = MoveTemp(Effective);
	return true;
}

int64 FEpicodeTakeRender::FrameForSequence(const FEpicodeDirectorTake& InTake, int64 InSequence)
{
	if (InSequence <= InTake.InSequence)
	{
		return InTake.InTick;
	}
	if (InSequence >= InTake.OutSequence)
	{
		return InTake.OutTick;
	}
	const int64 SequenceSpan = InTake.OutSequence - InTake.InSequence;
	const int64 TickSpan = InTake.OutTick - InTake.InTick;
	if (SequenceSpan <= 0)
	{
		return InTake.InTick;
	}
	return InTake.InTick + ((InSequence - InTake.InSequence) * TickSpan) / SequenceSpan;
}

bool FEpicodeTakeRender::Prepare(
	const FEpicodeTakeRenderRequest& InRequest,
	FEpicodeDirectorTake& OutTake,
	FEpicodeRenderRequest& OutRenderRequest,
	FString& OutError)
{
	if (InRequest.TakeId.IsEmpty() || InRequest.TakeVersion < 1)
	{
		OutError = TEXT("TAKE_INVALID: takeId and takeVersion are required");
		return false;
	}
	const FString Root = InRequest.TakeRoot.IsEmpty() ? DefaultTakeRoot() : InRequest.TakeRoot;
	if (!FEpicodeTakeLibrary::Load(Root, InRequest.TakeId, InRequest.TakeVersion, OutTake, OutError))
	{
		OutError = TEXT("TAKE_NOT_FOUND: ") + OutError;
		return false;
	}
	if (OutTake.ContentDigest.IsEmpty())
	{
		OutError = TEXT("TAKE_DIGEST_MISSING: take has no content digest");
		return false;
	}
	if (!InRequest.ExpectedContentDigest.IsEmpty() && InRequest.ExpectedContentDigest != OutTake.ContentDigest)
	{
		OutError = TEXT("TAKE_DIGEST_MISMATCH: expected digest does not match take");
		return false;
	}
	if (!ValidateTracks(OutTake, InRequest.Tracks, OutError))
	{
		return false;
	}
	FString OutputDirectory = InRequest.OutputDirectory;
	if (OutputDirectory.IsEmpty())
	{
		OutputDirectory = FPaths::Combine(Root, FString::Printf(TEXT("%s-v%d"), *InRequest.TakeId, InRequest.TakeVersion), TEXT("render"));
	}
	OutRenderRequest.OutputDirectory = FPaths::ConvertRelativePathToFull(OutputDirectory);
	OutRenderRequest.Format = InRequest.Format.IsEmpty() ? TEXT("png") : InRequest.Format;
	OutRenderRequest.StartFrame = static_cast<int32>(OutTake.InTick);
	OutRenderRequest.EndFrame = static_cast<int32>(OutTake.OutTick);
	OutRenderRequest.FrameRate = 24.0;
	OutRenderRequest.Width = 1920;
	OutRenderRequest.Height = 1080;
	OutRenderRequest.bOverwrite = false;
	OutRenderRequest.bIncludeAudio = false;
	if (InRequest.Tracks.Num() > 0)
	{
		OutRenderRequest.CameraId = InRequest.Tracks[0].CameraId;
	}
	else if (OutTake.CameraIds.Num() > 0)
	{
		OutRenderRequest.CameraId = OutTake.CameraIds[0];
	}
	if (OutRenderRequest.EndFrame <= OutRenderRequest.StartFrame)
	{
		OutError = TEXT("TAKE_RANGE_INVALID: outTick must be greater than inTick");
		return false;
	}
	IFileManager::Get().MakeDirectory(*OutRenderRequest.OutputDirectory, true);
	const FString Probe = FPaths::Combine(OutRenderRequest.OutputDirectory, TEXT(".epicode-write-probe"));
	if (!FFileHelper::SaveStringToFile(TEXT("probe"), *Probe))
	{
		OutError = TEXT("TAKE_OUTPUT_UNWRITABLE: cannot write to output directory");
		return false;
	}
	IFileManager::Get().Delete(*Probe, false, true, true);
	return true;
}

bool FEpicodeTakeRender::WriteManifest(
	const FEpicodeTakeRenderRequest& InRequest,
	const FEpicodeDirectorTake& InTake,
	const FEpicodeRenderRequest& InRenderRequest,
	const FString& InJobId,
	FString& OutManifestPath,
	FString& OutError,
	const TArray<FString>& InJobIds)
{
	TArray<FEpicodeTakeRenderTrack> Tracks;
	if (!ResolveTracks(InTake, InRequest.Tracks, Tracks, OutError))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Manifest = MakeShared<FJsonObject>();
	Manifest->SetStringField(TEXT("schema"), TEXT("epicode.take-render.v1"));
	Manifest->SetStringField(TEXT("jobId"), InJobId);
	if (InJobIds.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> JobValues;
		for (const FString& JobId : InJobIds)
		{
			JobValues.Add(MakeShared<FJsonValueString>(JobId));
		}
		Manifest->SetArrayField(TEXT("jobIds"), JobValues);
	}
	Manifest->SetStringField(TEXT("takeId"), InTake.Id);
	Manifest->SetNumberField(TEXT("takeVersion"), InTake.Version);
	Manifest->SetStringField(TEXT("takeChecksum"), InTake.Checksum);
	Manifest->SetStringField(TEXT("contentDigest"), InTake.ContentDigest);
	Manifest->SetStringField(TEXT("computedDigest"), ComputeTakeDigest(InTake));
	Manifest->SetStringField(TEXT("outputDirectory"), InRenderRequest.OutputDirectory);
	Manifest->SetStringField(TEXT("format"), InRenderRequest.Format);
	Manifest->SetNumberField(TEXT("startFrame"), InRenderRequest.StartFrame);
	Manifest->SetNumberField(TEXT("endFrame"), InRenderRequest.EndFrame);
	Manifest->SetNumberField(TEXT("frameRate"), InRenderRequest.FrameRate);
	TArray<TSharedPtr<FJsonValue>> TrackValues;
	for (int32 Index = 0; Index < Tracks.Num(); ++Index)
	{
		TSharedPtr<FJsonObject> TrackJson = Tracks[Index].ToJson();
		const FString TrackName = FString::Printf(TEXT("track_%02d_%s"), Index, *Tracks[Index].CameraId);
		TrackJson->SetStringField(TEXT("track"), TrackName);
		TrackJson->SetStringField(TEXT("outputPattern"), TrackName + TEXT("_%04d"));
		TrackJson->SetStringField(TEXT("outputDirectory"),
			FPaths::Combine(InRenderRequest.OutputDirectory, TrackName));
		TrackJson->SetNumberField(TEXT("startFrame"), static_cast<double>(FrameForSequence(InTake, Tracks[Index].StartSequence)));
		TrackJson->SetNumberField(TEXT("endFrame"), static_cast<double>(FrameForSequence(InTake, Tracks[Index].EndSequence)));
		if (InJobIds.IsValidIndex(Index))
		{
			TrackJson->SetStringField(TEXT("jobId"), InJobIds[Index]);
		}
		TrackValues.Add(MakeShared<FJsonValueObject>(TrackJson));
	}
	Manifest->SetArrayField(TEXT("tracks"), TrackValues);
	Manifest->SetStringField(TEXT("createdAt"), FDateTime::UtcNow().ToIso8601());
	OutManifestPath = FPaths::Combine(InRenderRequest.OutputDirectory, TEXT("take-render.json"));
	if (!FFileHelper::SaveStringToFile(SerializeTakeJson(Manifest), *OutManifestPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = TEXT("TAKE_MANIFEST_FAILED: cannot write manifest");
		return false;
	}
	return true;
}
