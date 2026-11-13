#include "EpicodeRenderCommands.h"

#include "EpicodeCommand.h"
#include "EpicodeRenderService.h"
#include "EpicodeTakeRender.h"
#include "EpicodeSettings.h"
#include "Misc/Paths.h"

namespace
{
	FEpicodeCommandResult FailureFrom(const FString& Code, const FString& Message)
	{
		return FEpicodeCommandResult::Failure(Code, Message);
	}

	FEpicodeCommandResult FailureFromError(const FString& InError, const FString& InFallback)
	{
		int32 Colon = INDEX_NONE;
		if (InError.FindChar(TEXT(':'), Colon))
		{
			return FailureFrom(InError.Left(Colon).TrimStartAndEnd(), InError.Mid(Colon + 1).TrimStartAndEnd());
		}
		return FailureFrom(InFallback, InError);
	}
}

void FEpicodeRenderCommands::Register(FEpicodeCommandRegistry& Registry)
{
	Registry.Register(TEXT("render.capabilities"), TEXT("List render formats and limits."), [](const TSharedPtr<FJsonObject>&, const FEpicodeCommandContext&)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Formats;
		Formats.Add(MakeShared<FJsonValueString>(TEXT("png")));
		Formats.Add(MakeShared<FJsonValueString>(TEXT("exr")));
		Formats.Add(MakeShared<FJsonValueString>(TEXT("jpg")));
		Formats.Add(MakeShared<FJsonValueString>(TEXT("mp4")));
		Result->SetArrayField(TEXT("formats"), Formats);
		Result->SetNumberField(TEXT("maxWidth"), 8192);
		Result->SetNumberField(TEXT("maxHeight"), 8192);
		Result->SetNumberField(TEXT("maxFrameRate"), 240);
		Result->SetStringField(TEXT("worker"), FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/Linux/UnrealEditor-Cmd"))));
		Result->SetStringField(TEXT("outputRoot"), UEpicodeSettings::Get()->GetRenderOutputRootPath());
		return FEpicodeCommandResult::Success(Result);
	}, false);

	Registry.Register(TEXT("take.render"), TEXT("Validate and submit a deterministic take render."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FEpicodeTakeRenderRequest TakeRequest;
		FString Error;
		if (!FEpicodeTakeRenderRequest::FromJson(Params, TakeRequest, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error);
		}
		FEpicodeDirectorTake Take;
		FEpicodeRenderRequest RenderRequest;
		if (!FEpicodeTakeRender::Prepare(TakeRequest, Take, RenderRequest, Error))
		{
			return FailureFromError(Error, TEXT("TAKE_RENDER_FAILED"));
		}
		TArray<FEpicodeTakeRenderTrack> Tracks;
		if (!FEpicodeTakeRender::ResolveTracks(Take, TakeRequest.Tracks, Tracks, Error))
		{
			return FailureFromError(Error, TEXT("TAKE_RENDER_FAILED"));
		}
		TArray<TSharedPtr<FEpicodeRenderJob>> Jobs;
		TArray<FString> JobIds;
		TArray<TSharedPtr<FJsonValue>> TrackValues;
		for (int32 Index = 0; Index < Tracks.Num(); ++Index)
		{
			FEpicodeRenderRequest TrackRequest = RenderRequest;
			TrackRequest.CameraId = Tracks[Index].CameraId;
			TrackRequest.StartFrame = static_cast<int32>(FEpicodeTakeRender::FrameForSequence(Take, Tracks[Index].StartSequence));
			TrackRequest.EndFrame = static_cast<int32>(FEpicodeTakeRender::FrameForSequence(Take, Tracks[Index].EndSequence));
			const FString TrackName = FString::Printf(TEXT("track_%02d_%s"), Index, *Tracks[Index].CameraId);
			TrackRequest.OutputDirectory = FPaths::Combine(RenderRequest.OutputDirectory, TrackName);
			if (TrackRequest.EndFrame <= TrackRequest.StartFrame)
			{
				for (const TSharedPtr<FEpicodeRenderJob>& Submitted : Jobs)
				{
					FEpicodeRenderService::Get().Cancel(Submitted->Id);
				}
				return FailureFrom(TEXT("TAKE_TRACK_INVALID"), FString::Printf(TEXT("track %s has no renderable frames"), *TrackName));
			}
			TSharedPtr<FEpicodeRenderJob> Job = FEpicodeRenderService::Get().Submit(TrackRequest, Error);
			if (!Job.IsValid())
			{
				for (const TSharedPtr<FEpicodeRenderJob>& Submitted : Jobs)
				{
					FEpicodeRenderService::Get().Cancel(Submitted->Id);
				}
				return FailureFrom(TEXT("TAKE_RENDER_SUBMIT_FAILED"), Error);
			}
			if (Job->State == EEpicodeRenderState::Failed)
			{
				for (const TSharedPtr<FEpicodeRenderJob>& Submitted : Jobs)
				{
					FEpicodeRenderService::Get().Cancel(Submitted->Id);
				}
				return FailureFrom(Job->Result.ErrorCode.IsEmpty() ? TEXT("TAKE_RENDER_SUBMIT_FAILED") : Job->Result.ErrorCode, Job->Result.ErrorMessage);
			}
			Jobs.Add(Job);
			JobIds.Add(Job->Id);
			TSharedPtr<FJsonObject> TrackJson = Tracks[Index].ToJson();
			TrackJson->SetStringField(TEXT("track"), TrackName);
			TrackJson->SetStringField(TEXT("jobId"), Job->Id);
			TrackJson->SetNumberField(TEXT("startFrame"), TrackRequest.StartFrame);
			TrackJson->SetNumberField(TEXT("endFrame"), TrackRequest.EndFrame);
			TrackJson->SetStringField(TEXT("outputDirectory"), TrackRequest.OutputDirectory);
			TrackValues.Add(MakeShared<FJsonValueObject>(TrackJson));
		}
		FString ManifestPath;
		if (!FEpicodeTakeRender::WriteManifest(TakeRequest, Take, RenderRequest, JobIds[0], ManifestPath, Error, JobIds))
		{
			return FailureFrom(TEXT("TAKE_MANIFEST_FAILED"), Error);
		}
		TSharedPtr<FJsonObject> Result = Jobs[0]->ToJson();
		Result->SetStringField(TEXT("takeId"), Take.Id);
		Result->SetNumberField(TEXT("takeVersion"), Take.Version);
		Result->SetStringField(TEXT("manifestPath"), ManifestPath);
		Result->SetStringField(TEXT("contentDigest"), Take.ContentDigest);
		Result->SetNumberField(TEXT("trackCount"), Tracks.Num());
		Result->SetArrayField(TEXT("tracks"), TrackValues);
		TArray<TSharedPtr<FJsonValue>> JobValues;
		for (const TSharedPtr<FEpicodeRenderJob>& Submitted : Jobs)
		{
			JobValues.Add(MakeShared<FJsonValueObject>(Submitted->ToJson()));
		}
		Result->SetArrayField(TEXT("jobs"), JobValues);
		return FEpicodeCommandResult::Success(Result, JobIds);
	});

	Registry.Register(TEXT("render.submit"), TEXT("Submit a Movie Render Pipeline job."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FEpicodeRenderRequest Request;
		FString Error;
		if (!FEpicodeRenderRequest::FromJson(Params, Request, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error);
		}
		TSharedPtr<FEpicodeRenderJob> Job = FEpicodeRenderService::Get().Submit(Request, Error);
		if (!Job.IsValid())
		{
			return FailureFrom(TEXT("RENDER_SUBMIT_FAILED"), Error);
		}
		if (Job->State == EEpicodeRenderState::Failed)
		{
			return FailureFrom(Job->Result.ErrorCode.IsEmpty() ? TEXT("RENDER_SUBMIT_FAILED") : Job->Result.ErrorCode, Job->Result.ErrorMessage);
		}
		return FEpicodeCommandResult::Success(Job->ToJson(), { Job->Id });
	});

	Registry.Register(TEXT("scene.render"), TEXT("Submit a render job for the current scene."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FEpicodeRenderRequest Request;
		FString Error;
		if (!FEpicodeRenderRequest::FromJson(Params, Request, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error);
		}
		TSharedPtr<FEpicodeRenderJob> Job = FEpicodeRenderService::Get().Submit(Request, Error);
		if (!Job.IsValid())
		{
			return FailureFrom(TEXT("RENDER_SUBMIT_FAILED"), Error);
		}
		if (Job->State == EEpicodeRenderState::Failed)
		{
			return FailureFrom(Job->Result.ErrorCode.IsEmpty() ? TEXT("RENDER_SUBMIT_FAILED") : Job->Result.ErrorCode, Job->Result.ErrorMessage);
		}
		return FEpicodeCommandResult::Success(Job->ToJson(), { Job->Id });
	});

	Registry.Register(TEXT("render.status"), TEXT("Return a render job."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString JobId;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("jobId"), JobId, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error);
		}
		TSharedPtr<FEpicodeRenderJob> Job = FEpicodeRenderService::Get().GetJob(JobId);
		if (!Job.IsValid())
		{
			return FailureFrom(TEXT("JOB_NOT_FOUND"), TEXT("Render job not found."));
		}
		return FEpicodeCommandResult::Success(Job->ToJson());
	}, false);

	Registry.Register(TEXT("render.list"), TEXT("List render jobs."), [](const TSharedPtr<FJsonObject>&, const FEpicodeCommandContext&)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const TSharedPtr<FEpicodeRenderJob>& Job : FEpicodeRenderService::Get().GetJobs())
		{
			if (Job.IsValid())
			{
				Values.Add(MakeShared<FJsonValueObject>(Job->ToJson()));
			}
		}
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("jobs"), Values);
		return FEpicodeCommandResult::Success(Result);
	}, false);

	Registry.Register(TEXT("render.cancel"), TEXT("Cancel a render job."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString JobId;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("jobId"), JobId, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error);
		}
		if (!FEpicodeRenderService::Get().Cancel(JobId))
		{
			return FailureFrom(TEXT("JOB_NOT_CANCELLABLE"), TEXT("Render job is not running or was not found."));
		}
		TSharedPtr<FEpicodeRenderJob> Job = FEpicodeRenderService::Get().GetJob(JobId);
		return FEpicodeCommandResult::Success(Job.IsValid() ? Job->ToJson() : MakeShared<FJsonObject>());
	});

	Registry.Register(TEXT("render.retry"), TEXT("Retry a finished render job with the same request."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString JobId;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("jobId"), JobId, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error);
		}
		TSharedPtr<FEpicodeRenderJob> Job = FEpicodeRenderService::Get().Retry(JobId, Error);
		if (!Job.IsValid())
		{
			return FailureFrom(TEXT("RENDER_RETRY_FAILED"), Error);
		}
		return FEpicodeCommandResult::Success(Job->ToJson(), { Job->Id });
	});
}
