#include "EpicodeRenderTypes.h"

#include "EpicodeCommand.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"

namespace
{
	FString StateToString(EEpicodeRenderState State)
	{
		switch (State)
		{
		case EEpicodeRenderState::Queued:
			return TEXT("queued");
		case EEpicodeRenderState::Running:
			return TEXT("running");
		case EEpicodeRenderState::Succeeded:
			return TEXT("succeeded");
		case EEpicodeRenderState::Failed:
			return TEXT("failed");
		case EEpicodeRenderState::Cancelled:
			return TEXT("cancelled");
		default:
			return TEXT("unknown");
		}
	}
}

TSharedPtr<FJsonObject> FEpicodeRenderRequest::ToJson() const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("sceneId"), SceneId);
	Json->SetStringField(TEXT("cameraId"), CameraId);
	Json->SetStringField(TEXT("outputDirectory"), OutputDirectory);
	Json->SetStringField(TEXT("format"), Format);
	Json->SetStringField(TEXT("codec"), Codec);
	Json->SetNumberField(TEXT("width"), Width);
	Json->SetNumberField(TEXT("height"), Height);
	Json->SetNumberField(TEXT("frameRate"), FrameRate);
	Json->SetNumberField(TEXT("startFrame"), StartFrame);
	Json->SetNumberField(TEXT("endFrame"), EndFrame);
	Json->SetNumberField(TEXT("quality"), Quality);
	Json->SetBoolField(TEXT("overwrite"), bOverwrite);
	Json->SetBoolField(TEXT("includeAudio"), bIncludeAudio);
	return Json;
}

bool FEpicodeRenderRequest::FromJson(const TSharedPtr<FJsonObject>& Object, FEpicodeRenderRequest& OutRequest, FString& OutError)
{
	if (!Object.IsValid())
	{
		OutError = TEXT("Render request must be an object.");
		return false;
	}
	EpicodeJson::GetOptionalString(Object, TEXT("sceneId"), OutRequest.SceneId);
	EpicodeJson::GetOptionalString(Object, TEXT("cameraId"), OutRequest.CameraId);
	EpicodeJson::GetOptionalString(Object, TEXT("outputDirectory"), OutRequest.OutputDirectory);
	EpicodeJson::GetOptionalString(Object, TEXT("format"), OutRequest.Format, TEXT("png"));
	EpicodeJson::GetOptionalString(Object, TEXT("codec"), OutRequest.Codec, TEXT("h264"));
	double Width = 1920.0;
	double Height = 1080.0;
	EpicodeJson::GetOptionalNumber(Object, TEXT("width"), Width, 1920.0);
	EpicodeJson::GetOptionalNumber(Object, TEXT("height"), Height, 1080.0);
	EpicodeJson::GetOptionalNumber(Object, TEXT("frameRate"), OutRequest.FrameRate, 24.0);
	double StartFrame = 0.0;
	double EndFrame = 120.0;
	EpicodeJson::GetOptionalNumber(Object, TEXT("startFrame"), StartFrame, 0.0);
	EpicodeJson::GetOptionalNumber(Object, TEXT("endFrame"), EndFrame, 120.0);
	double Quality = 20.0;
	EpicodeJson::GetOptionalNumber(Object, TEXT("quality"), Quality, 20.0);
	OutRequest.Width = FMath::RoundToInt(Width);
	OutRequest.Height = FMath::RoundToInt(Height);
	OutRequest.StartFrame = FMath::RoundToInt(StartFrame);
	OutRequest.EndFrame = FMath::RoundToInt(EndFrame);
	OutRequest.Quality = FMath::RoundToInt(Quality);
	EpicodeJson::GetOptionalBool(Object, TEXT("overwrite"), OutRequest.bOverwrite, true);
	EpicodeJson::GetOptionalBool(Object, TEXT("includeAudio"), OutRequest.bIncludeAudio, false);
	if (OutRequest.Width < 16 || OutRequest.Width > 8192 || OutRequest.Height < 16 || OutRequest.Height > 8192)
	{
		OutError = TEXT("width and height must be between 16 and 8192.");
		return false;
	}
	if (OutRequest.FrameRate < 1.0 || OutRequest.FrameRate > 240.0)
	{
		OutError = TEXT("frameRate must be between 1 and 240.");
		return false;
	}
	if (OutRequest.EndFrame <= OutRequest.StartFrame)
	{
		OutError = TEXT("endFrame must be greater than startFrame.");
		return false;
	}
	OutRequest.Format = OutRequest.Format.ToLower();
	if (OutRequest.Format == TEXT("jpeg"))
	{
		OutRequest.Format = TEXT("jpg");
	}
	if (OutRequest.Format != TEXT("png") && OutRequest.Format != TEXT("exr")
		&& OutRequest.Format != TEXT("jpg") && OutRequest.Format != TEXT("mp4"))
	{
		OutError = TEXT("format must be png, exr, jpg or mp4.");
		return false;
	}
	if (OutRequest.Format == TEXT("mp4") && (OutRequest.Quality < 16 || OutRequest.Quality > 51))
	{
		OutError = TEXT("quality must be between 16 and 51 for mp4.");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FEpicodeRenderResult::ToJson() const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetBoolField(TEXT("success"), bSuccess);
	Json->SetStringField(TEXT("errorCode"), ErrorCode);
	Json->SetStringField(TEXT("errorMessage"), ErrorMessage);
	Json->SetBoolField(TEXT("retryable"), bRetryable);
	Json->SetStringField(TEXT("diagnostics"), Diagnostics);
	Json->SetStringField(TEXT("outputDirectory"), OutputDirectory);
	Json->SetNumberField(TEXT("renderedFrames"), RenderedFrames);
	Json->SetNumberField(TEXT("durationSeconds"), DurationSeconds);
	TArray<TSharedPtr<FJsonValue>> Files;
	for (const FString& File : OutputFiles)
	{
		Files.Add(MakeShared<FJsonValueString>(File));
	}
	Json->SetArrayField(TEXT("outputFiles"), Files);
	TSharedPtr<FJsonObject> ChecksumJson = MakeShared<FJsonObject>();
	for (const TPair<FString, FString>& Pair : Checksums)
	{
		ChecksumJson->SetStringField(Pair.Key, Pair.Value);
	}
	Json->SetObjectField(TEXT("checksums"), ChecksumJson);
	return Json;
}

bool FEpicodeRenderResult::FromJson(const TSharedPtr<FJsonObject>& Object, FEpicodeRenderResult& OutResult)
{
	if (!Object.IsValid())
	{
		return false;
	}
	Object->TryGetBoolField(TEXT("success"), OutResult.bSuccess);
	Object->TryGetStringField(TEXT("errorCode"), OutResult.ErrorCode);
	Object->TryGetStringField(TEXT("errorMessage"), OutResult.ErrorMessage);
	Object->TryGetBoolField(TEXT("retryable"), OutResult.bRetryable);
	Object->TryGetStringField(TEXT("diagnostics"), OutResult.Diagnostics);
	Object->TryGetStringField(TEXT("outputDirectory"), OutResult.OutputDirectory);
	double RenderedFrames = 0.0;
	double DurationSeconds = 0.0;
	Object->TryGetNumberField(TEXT("renderedFrames"), RenderedFrames);
	Object->TryGetNumberField(TEXT("durationSeconds"), DurationSeconds);
	OutResult.RenderedFrames = FMath::RoundToInt(RenderedFrames);
	OutResult.DurationSeconds = DurationSeconds;
	const TArray<TSharedPtr<FJsonValue>> Files = EpicodeJson::GetOptionalArray(Object, TEXT("outputFiles"));
	for (const TSharedPtr<FJsonValue>& Value : Files)
	{
		if (Value.IsValid() && Value->Type == EJson::String)
		{
			OutResult.OutputFiles.Add(Value->AsString());
		}
	}
	const TSharedPtr<FJsonObject>* ChecksumJson = nullptr;
	if (Object->TryGetObjectField(TEXT("checksums"), ChecksumJson) && ChecksumJson)
	{
		for (const auto& Pair : (*ChecksumJson)->Values)
		{
			if (Pair.Value.IsValid() && Pair.Value->Type == EJson::String)
			{
				OutResult.Checksums.Add(FString(Pair.Key.ToView()), Pair.Value->AsString());
			}
		}
	}
	return true;
}

void FEpicodeRenderResult::ClassifyFailure(FEpicodeRenderResult& Result)
{
	const FString Message = Result.ErrorMessage.ToLower();
	if (Message.Contains(TEXT("out of memory")) || Message.Contains(TEXT("out of vram")) || Message.Contains(TEXT("oom")))
	{
		Result.bRetryable = true;
		Result.Diagnostics = TEXT("GPU memory pressure detected; retry after reducing resolution or closing other GPU work.");
	}
	else if (Message.Contains(TEXT("device lost")) || Message.Contains(TEXT("device removed")) || Message.Contains(TEXT("gpu crash")))
	{
		Result.bRetryable = true;
		Result.Diagnostics = TEXT("GPU device loss detected; retry and check the render worker log for driver errors.");
	}
	else if (Message.Contains(TEXT("no space left")) || Message.Contains(TEXT("disk full")) || Message.Contains(TEXT("not enough space")))
	{
		Result.bRetryable = false;
		Result.Diagnostics = TEXT("Output disk is full; free space before retrying.");
	}
	else if (Result.ErrorCode == TEXT("RENDER_WORKER_NO_CAMERA") || Result.ErrorCode == TEXT("RENDER_WORKER_SCENE_FAILED") || Result.ErrorCode == TEXT("RENDER_WORKER_NO_WORLD") || Result.ErrorCode == TEXT("RENDER_WORKER_MAP_FAILED"))
	{
		Result.bRetryable = false;
		Result.Diagnostics = TEXT("Render input is invalid or missing; fix the scene/camera before retrying.");
	}
	else if (Result.ErrorCode == TEXT("RENDER_CANCELLED") || Result.ErrorCode == TEXT("RENDER_OUTPUT_NOT_WRITABLE"))
	{
		Result.bRetryable = false;
		Result.Diagnostics = TEXT("The job was cancelled or the output directory is not writable.");
	}
	else
	{
		Result.bRetryable = !Result.ErrorCode.IsEmpty() && Result.ErrorCode != TEXT("RENDER_CANCELLED");
		Result.Diagnostics = Result.bRetryable ? TEXT("Transient render failure; retry may succeed.") : FString();
	}
}

bool FEpicodeRenderVerifier::Verify(const FEpicodeRenderRequest& Request, FEpicodeRenderResult& Result, FString& OutError)
{
	const bool bMp4 = Request.Format.Equals(TEXT("mp4"), ESearchCase::IgnoreCase);
	const int32 ExpectedFrames = bMp4 ? 1 : FMath::Max(1, Request.EndFrame - Request.StartFrame);
	if (Result.OutputFiles.Num() != ExpectedFrames)
	{
		OutError = FString::Printf(TEXT("Expected %d output files, found %d."), ExpectedFrames, Result.OutputFiles.Num());
		return false;
	}
	IImageWrapperModule& ImageModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	for (const FString& File : Result.OutputFiles)
	{
		const int64 Size = IFileManager::Get().FileSize(*File);
		if (Size <= 0)
		{
			OutError = FString::Printf(TEXT("Output file is missing or empty: %s"), *File);
			return false;
		}
		TArray<uint8> Data;
		if (!FFileHelper::LoadFileToArray(Data, *File))
		{
			OutError = FString::Printf(TEXT("Could not read output file: %s"), *File);
			return false;
		}
		Result.Checksums.Add(FPaths::GetCleanFilename(File), FSHA1::HashBuffer(Data.GetData(), Data.Num()).ToString());

		const FString Extension = FPaths::GetExtension(File).ToLower();
		if (Extension == TEXT("png") || Extension == TEXT("jpg") || Extension == TEXT("jpeg"))
		{
			const EImageFormat Format = Extension == TEXT("png") ? EImageFormat::PNG : EImageFormat::JPEG;
			TSharedPtr<IImageWrapper> Wrapper = ImageModule.CreateImageWrapper(Format);
			if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Data.GetData(), Data.Num()))
			{
				OutError = FString::Printf(TEXT("Could not decode output image: %s"), *File);
				return false;
			}
			if (Wrapper->GetWidth() != Request.Width || Wrapper->GetHeight() != Request.Height)
			{
				OutError = FString::Printf(TEXT("Unexpected resolution %dx%d for %s, expected %dx%d."),
					Wrapper->GetWidth(), Wrapper->GetHeight(), *File, Request.Width, Request.Height);
				return false;
			}
		}
		else if (bMp4 && Extension == TEXT("mp4"))
		{
			int32 ReturnCode = 0;
			FString StdOut;
			const FString Args = FString::Printf(TEXT("-v error -show_entries format=duration -of csv=p=0 \"%s\""), *File);
			if (FPlatformProcess::ExecProcess(TEXT("ffprobe"), *Args, &ReturnCode, &StdOut, nullptr) && ReturnCode == 0)
			{
				const double ActualDuration = FCString::Atod(*StdOut.TrimStartAndEnd());
				const double ExpectedDuration = static_cast<double>(Request.EndFrame - Request.StartFrame) / FMath::Max(1.0, Request.FrameRate);
				if (ActualDuration <= 0.0 || FMath::Abs(ActualDuration - ExpectedDuration) > 0.5)
				{
					OutError = FString::Printf(TEXT("Unexpected MP4 duration %.3fs for %s, expected %.3fs."), ActualDuration, *File, ExpectedDuration);
					return false;
				}
			}
		}
	}
	return true;
}

TSharedPtr<FJsonObject> FEpicodeRenderJob::ToJson() const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("jobId"), Id);
	Json->SetStringField(TEXT("state"), StateToString(State));
	Json->SetStringField(TEXT("stage"), Stage);
	Json->SetNumberField(TEXT("progress"), Progress);
	Json->SetStringField(TEXT("createdAt"), CreatedAt.ToIso8601());
	if (StartedAt != FDateTime())
	{
		Json->SetStringField(TEXT("startedAt"), StartedAt.ToIso8601());
	}
	if (FinishedAt != FDateTime())
	{
		Json->SetStringField(TEXT("finishedAt"), FinishedAt.ToIso8601());
	}
	Json->SetStringField(TEXT("jobDirectory"), JobDirectory);
	Json->SetNumberField(TEXT("processId"), ProcessId);
	Json->SetObjectField(TEXT("request"), Request.ToJson());
	Json->SetObjectField(TEXT("result"), Result.ToJson());
	return Json;
}
