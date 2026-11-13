#include "EpicodeRenderService.h"

#include "EpicodeRenderModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "EditorAssetLibrary.h"
#include "EpicodeCommand.h"
#include "EpicodeEntityActors.h"
#include "EpicodeSceneSubsystem.h"
#include "EpicodeSettings.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "LevelSequence.h"
#include "MoviePipelineDeferredPasses.h"
#include "MoviePipelineExecutor.h"
#include "MoviePipelineEXROutput.h"
#include "MoviePipelineImageSequenceOutput.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelinePIEExecutor.h"
#include "MoviePipelineQueue.h"
#include "MoviePipelineQueueSubsystem.h"
#include "MovieScene.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "MovieSceneObjectBindingID.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString SerializeJson(const TSharedPtr<FJsonObject>& Json)
	{
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(Json.ToSharedRef(), Writer);
		return Text;
	}

	bool LoadJson(const FString& Path, TSharedPtr<FJsonObject>& OutJson)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			return false;
		}
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		return FJsonSerializer::Deserialize(Reader, OutJson) && OutJson.IsValid();
	}

	FString GetJobsRoot()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Epicode/RenderJobs")));
	}

	void CollectOutputFiles(const FString& Directory, TArray<FString>& OutFiles)
	{
		IFileManager::Get().FindFilesRecursive(OutFiles, *Directory, TEXT("*.*"), true, false);
	}

	void ClearExistingImages(const FString& Directory)
	{
		for (const TCHAR* Extension : { TEXT("png"), TEXT("jpg"), TEXT("jpeg"), TEXT("exr"), TEXT("mp4") })
		{
			TArray<FString> Files;
			IFileManager::Get().FindFiles(Files, *Directory, *FString::Printf(TEXT("*.%s"), Extension));
			for (const FString& File : Files)
			{
				IFileManager::Get().Delete(*FPaths::Combine(Directory, File), false, true, true);
			}
		}
	}

	void WriteResultFile(const FString& JobDirectory, FEpicodeRenderResult& Result)
	{
		FEpicodeRenderResult::ClassifyFailure(Result);
		IFileManager::Get().MakeDirectory(*JobDirectory, true);
		FFileHelper::SaveStringToFile(SerializeJson(Result.ToJson()), *FPaths::Combine(JobDirectory, TEXT("result.json")));
	}

	int32 CountRenderedFrames(const FString& Directory)
	{
		int32 Count = 0;
		for (const TCHAR* Extension : { TEXT("png"), TEXT("jpg"), TEXT("jpeg"), TEXT("exr") })
		{
			TArray<FString> Files;
			IFileManager::Get().FindFiles(Files, *Directory, *FString::Printf(TEXT("*.%s"), Extension));
			Count += Files.Num();
		}
		return Count;
	}

	void WriteProgressFile(const TSharedPtr<FEpicodeRenderJob>& Job, int32 RenderedFrames, const FString& State = TEXT("running"))
	{
		// Movie Render Pipeline treats endFrame as exclusive; a 0..3 request renders 3 frames.
		const int32 TotalFrames = FMath::Max(1, Job->Request.EndFrame - Job->Request.StartFrame);
		const float Progress = FMath::Clamp(static_cast<float>(RenderedFrames) / static_cast<float>(TotalFrames), 0.0f, 1.0f);
		Job->Progress = Progress;
		if (State == TEXT("running"))
		{
			Job->Stage = TEXT("rendering");
		}
		TSharedPtr<FJsonObject> ProgressJson = MakeShared<FJsonObject>();
		ProgressJson->SetStringField(TEXT("jobId"), Job->Id);
		ProgressJson->SetStringField(TEXT("state"), State);
		ProgressJson->SetStringField(TEXT("stage"), Job->Stage);
		ProgressJson->SetNumberField(TEXT("progress"), Progress);
		ProgressJson->SetNumberField(TEXT("renderedFrames"), RenderedFrames);
		ProgressJson->SetNumberField(TEXT("totalFrames"), TotalFrames);
		ProgressJson->SetStringField(TEXT("updatedAt"), FDateTime::UtcNow().ToIso8601());
		IFileManager::Get().MakeDirectory(*Job->JobDirectory, true);
		FFileHelper::SaveStringToFile(SerializeJson(ProgressJson), *FPaths::Combine(Job->JobDirectory, TEXT("progress.json")));
	}

	bool EncodeMp4(const FString& FramesDirectory, const FString& SequenceName, double FrameRate, int32 Quality, const FString& OutputPath, FString& OutError)
	{
		const FString InputPattern = FPaths::Combine(FramesDirectory, FString::Printf(TEXT("%s_%%04d.png"), *SequenceName));
		const FString Args = FString::Printf(
			TEXT("-nostdin -y -framerate %g -start_number 0 -i \"%s\" -c:v libx264 -crf %d -pix_fmt yuv420p \"%s\""),
			FrameRate, *InputPattern, FMath::Clamp(Quality, 16, 51), *OutputPath);
		int32 ReturnCode = 0;
		FString StdOut;
		FString StdErr;
		if (!FPlatformProcess::ExecProcess(TEXT("ffmpeg"), *Args, &ReturnCode, &StdOut, &StdErr, *FPaths::GetPath(OutputPath)))
		{
			OutError = TEXT("ffmpeg could not be launched.");
			return false;
		}
		if (ReturnCode != 0 || !FPaths::FileExists(OutputPath))
		{
			OutError = FString::Printf(TEXT("ffmpeg failed with code %d: %s"), ReturnCode, *StdErr.Left(2000));
			return false;
		}
		return true;
	}

	FString CameraActorName(const AActor* Camera)
	{
		return Camera ? Camera->GetActorNameOrLabel() : TEXT("Camera");
	}
}

FEpicodeRenderService& FEpicodeRenderService::Get()
{
	static FEpicodeRenderService Instance;
	return Instance;
}

void FEpicodeRenderService::Initialize()
{
	if (bInitialized)
	{
		return;
	}
	bInitialized = true;
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FEpicodeRenderService::Tick), 0.25f);
}

void FEpicodeRenderService::Shutdown()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	FScopeLock ScopeLock(&Lock);
	for (const TSharedPtr<FEpicodeRenderJob>& Job : Jobs)
	{
		if (Job.IsValid() && FPlatformProcess::IsProcRunning(Job->ProcessHandle))
		{
			FPlatformProcess::TerminateProc(Job->ProcessHandle, true);
			FPlatformProcess::CloseProc(Job->ProcessHandle);
		}
	}
	Jobs.Reset();
	bInitialized = false;
}

bool FEpicodeRenderService::WriteRequestFile(const TSharedPtr<FEpicodeRenderJob>& Job, FString& OutPath)
{
	OutPath = FPaths::Combine(Job->JobDirectory, TEXT("request.json"));
	IFileManager::Get().MakeDirectory(*Job->JobDirectory, true);
	const FString Text = SerializeJson(Job->Request.ToJson());
	if (!FFileHelper::SaveStringToFile(Text, *OutPath))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
	Meta->SetStringField(TEXT("jobId"), Job->Id);
	Meta->SetStringField(TEXT("createdAt"), Job->CreatedAt.ToIso8601());
	Meta->SetStringField(TEXT("projectFile"), FPaths::GetProjectFilePath());
	Meta->SetStringField(TEXT("engineRoot"), FPaths::EngineDir());
	FFileHelper::SaveStringToFile(SerializeJson(Meta), *FPaths::Combine(Job->JobDirectory, TEXT("meta.json")));
	return true;
}

bool FEpicodeRenderService::LaunchWorker(const TSharedPtr<FEpicodeRenderJob>& Job, FString& OutError)
{
	FString RequestPath;
	if (!WriteRequestFile(Job, RequestPath))
	{
		OutError = TEXT("Unable to write the render request file.");
		return false;
	}
	const FString EditorCmd = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/Linux/UnrealEditor-Cmd")));
	const FString ProjectFile = FPaths::GetProjectFilePath();
	if (!FPaths::FileExists(EditorCmd))
	{
		OutError = FString::Printf(TEXT("UnrealEditor-Cmd not found at %s"), *EditorCmd);
		return false;
	}
	const FString WorkerLog = FPaths::Combine(Job->JobDirectory, TEXT("worker.log"));
	const FString Args = FString::Printf(
		TEXT("\"%s\" /Engine/Maps/Entry -EpicodeRenderWorker=\"%s\" -abslog=\"%s\" -unattended -nosplash -NoSound -stdout -FullStdOutLogOutput -RenderOffscreen -windowed -ResX=%d -ResY=%d"),
		*ProjectFile, *RequestPath, *WorkerLog, Job->Request.Width, Job->Request.Height);
	uint32 ProcessId = 0;
	Job->ProcessHandle = FPlatformProcess::CreateProc(*EditorCmd, *Args, true, false, false, &ProcessId, 0, *Job->JobDirectory, nullptr, nullptr);
	Job->ProcessId = ProcessId;
	if (!Job->ProcessHandle.IsValid())
	{
		OutError = TEXT("Unable to launch UnrealEditor-Cmd.");
		return false;
	}
	Job->State = EEpicodeRenderState::Running;
	Job->Stage = TEXT("rendering");
	Job->Progress = 0.1f;
	return true;
}

TSharedPtr<FEpicodeRenderJob> FEpicodeRenderService::Submit(const FEpicodeRenderRequest& InRequest, FString& OutError)
{
	UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
	if (!Scene || !Scene->HasScene())
	{
		OutError = TEXT("No scene is open.");
		return nullptr;
	}
	if (!InRequest.SceneId.IsEmpty() && InRequest.SceneId != Scene->GetCurrentSceneId())
	{
		OutError = TEXT("The requested scene is not the open scene.");
		return nullptr;
	}
	FString SavePath;
	if (!Scene->SaveScene(SavePath, OutError))
	{
		return nullptr;
	}
	TSharedPtr<FEpicodeRenderJob> Job = MakeShared<FEpicodeRenderJob>();
	Job->Id = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	Job->Request = InRequest;
	Job->Request.SceneId = Scene->GetCurrentSceneId();
	if (Job->Request.CameraId.IsEmpty())
	{
		Job->Request.CameraId = Scene->GetActiveCameraId().ToString(EGuidFormats::DigitsWithHyphens);
	}
	Job->JobDirectory = FPaths::Combine(GetJobsRoot(), Job->Id);
	if (Job->Request.OutputDirectory.IsEmpty())
	{
		Job->Request.OutputDirectory = FPaths::Combine(
			UEpicodeSettings::Get()->GetRenderOutputRootPath(),
			FString::Printf(TEXT("%s_%s"), *Scene->GetCurrentSceneName(), *Job->Id));
	}
	Job->Request.OutputDirectory = FPaths::ConvertRelativePathToFull(Job->Request.OutputDirectory);
	IFileManager::Get().MakeDirectory(*Job->Request.OutputDirectory, true);
	int32 RunningCount = 0;
	{
		FScopeLock ScopeLock(&Lock);
		for (const TSharedPtr<FEpicodeRenderJob>& Existing : Jobs)
		{
			if (Existing.IsValid() && Existing->State == EEpicodeRenderState::Running)
			{
				++RunningCount;
			}
		}
	}
	const int32 ConcurrencyLimit = FMath::Max(1, UEpicodeSettings::Get()->RenderConcurrency);
	if (RunningCount >= ConcurrencyLimit)
	{
		Job->State = EEpicodeRenderState::Queued;
		Job->Stage = TEXT("queued");
		WriteProgressFile(Job, 0, TEXT("queued"));
		{
			FScopeLock ScopeLock(&Lock);
			Jobs.Add(Job);
		}
		return Job;
	}
	if (!LaunchWorker(Job, OutError))
	{
		Job->State = EEpicodeRenderState::Failed;
		Job->Stage = TEXT("launch-failed");
		Job->FinishedAt = FDateTime::UtcNow();
		Job->Result.ErrorCode = TEXT("RENDER_LAUNCH_FAILED");
		Job->Result.ErrorMessage = OutError;
		return Job;
	}
	Job->StartedAt = FDateTime::UtcNow();
	WriteProgressFile(Job, 0);
	{
		FScopeLock ScopeLock(&Lock);
		Jobs.Add(Job);
	}
	return Job;
}

TSharedPtr<FEpicodeRenderJob> FEpicodeRenderService::GetJob(const FString& InJobId) const
{
	FScopeLock ScopeLock(&Lock);
	for (const TSharedPtr<FEpicodeRenderJob>& Job : Jobs)
	{
		if (Job.IsValid() && Job->Id == InJobId)
		{
			return Job;
		}
	}
	return nullptr;
}

TArray<TSharedPtr<FEpicodeRenderJob>> FEpicodeRenderService::GetJobs() const
{
	FScopeLock ScopeLock(&Lock);
	return Jobs;
}

bool FEpicodeRenderService::Cancel(const FString& InJobId)
{
	FScopeLock ScopeLock(&Lock);
	for (const TSharedPtr<FEpicodeRenderJob>& Job : Jobs)
	{
		if (Job.IsValid() && Job->Id == InJobId && (Job->State == EEpicodeRenderState::Running || Job->State == EEpicodeRenderState::Queued))
		{
			if (Job->State == EEpicodeRenderState::Running)
			{
				FPlatformProcess::TerminateProc(Job->ProcessHandle, true);
				FPlatformProcess::CloseProc(Job->ProcessHandle);
				Job->ProcessHandle = FProcHandle();
			}
			Job->State = EEpicodeRenderState::Cancelled;
			Job->Stage = TEXT("cancelled");
			Job->Progress = 1.0f;
			Job->FinishedAt = FDateTime::UtcNow();
			Job->Result.ErrorCode = TEXT("RENDER_CANCELLED");
			Job->Result.ErrorMessage = TEXT("Render job was cancelled.");
			WriteResultFile(Job->JobDirectory, Job->Result);
			WriteProgressFile(Job, CountRenderedFrames(Job->Request.OutputDirectory), TEXT("cancelled"));
			return true;
		}
	}
	return false;
}

TSharedPtr<FEpicodeRenderJob> FEpicodeRenderService::Retry(const FString& InJobId, FString& OutError)
{
	TSharedPtr<FEpicodeRenderJob> Job = GetJob(InJobId);
	if (!Job.IsValid())
	{
		OutError = TEXT("Render job not found.");
		return nullptr;
	}
	if (Job->State == EEpicodeRenderState::Queued || Job->State == EEpicodeRenderState::Running)
	{
		OutError = TEXT("Render job is still active.");
		return nullptr;
	}
	return Submit(Job->Request, OutError);
}

bool FEpicodeRenderService::Tick(float DeltaSeconds)
{
	TArray<TSharedPtr<FEpicodeRenderJob>> Running;
	{
		FScopeLock ScopeLock(&Lock);
		for (const TSharedPtr<FEpicodeRenderJob>& Job : Jobs)
		{
			if (Job.IsValid() && Job->State == EEpicodeRenderState::Running)
			{
				Running.Add(Job);
			}
		}
	}
	for (const TSharedPtr<FEpicodeRenderJob>& Job : Running)
	{
		if (FPlatformProcess::IsProcRunning(Job->ProcessHandle))
		{
			const FDateTime StartedAt = Job->StartedAt != FDateTime() ? Job->StartedAt : Job->CreatedAt;
			const double TimeoutSeconds = static_cast<double>(FMath::Max(1, UEpicodeSettings::Get()->RenderTimeoutSeconds));
			if ((FDateTime::UtcNow() - StartedAt).GetTotalSeconds() > TimeoutSeconds)
			{
				FPlatformProcess::TerminateProc(Job->ProcessHandle, true);
				FPlatformProcess::CloseProc(Job->ProcessHandle);
				Job->ProcessHandle = FProcHandle();
				Job->State = EEpicodeRenderState::Failed;
				Job->Stage = TEXT("timeout");
				Job->Result.bSuccess = false;
				Job->Result.ErrorCode = TEXT("RENDER_TIMEOUT");
				Job->Result.ErrorMessage = TEXT("Render worker exceeded the timeout.");
				Job->FinishedAt = FDateTime::UtcNow();
				Job->Progress = 1.0f;
				WriteResultFile(Job->JobDirectory, Job->Result);
				WriteProgressFile(Job, CountRenderedFrames(Job->Request.OutputDirectory), TEXT("failed"));
				Job->Progress = 1.0f;
				FFileHelper::SaveStringToFile(SerializeJson(Job->ToJson()), *FPaths::Combine(Job->JobDirectory, TEXT("job.json")));
				continue;
			}
			const double Now = FPlatformTime::Seconds();
			if (Now - Job->LastProgressWriteSeconds >= 1.0)
			{
				Job->LastProgressWriteSeconds = Now;
				const FString FrameDirectory = Job->Request.Format.Equals(TEXT("mp4"), ESearchCase::IgnoreCase)
					? FPaths::Combine(Job->JobDirectory, TEXT("frames"))
					: Job->Request.OutputDirectory;
				WriteProgressFile(Job, CountRenderedFrames(FrameDirectory));
			}
			continue;
		}
		FPlatformProcess::CloseProc(Job->ProcessHandle);
		Job->ProcessHandle = FProcHandle();
		FinishJobFromDisk(Job);
	}
	TSharedPtr<FEpicodeRenderJob> NextJob;
	{
		FScopeLock ScopeLock(&Lock);
		bool bRunning = false;
		for (const TSharedPtr<FEpicodeRenderJob>& Job : Jobs)
		{
			if (Job.IsValid() && Job->State == EEpicodeRenderState::Running)
			{
				bRunning = true;
				break;
			}
		}
		if (!bRunning)
		{
			for (const TSharedPtr<FEpicodeRenderJob>& Job : Jobs)
			{
				if (Job.IsValid() && Job->State == EEpicodeRenderState::Queued)
				{
					NextJob = Job;
					break;
				}
			}
		}
	}
	if (NextJob.IsValid())
	{
		FString LaunchError;
		if (LaunchWorker(NextJob, LaunchError))
		{
			NextJob->StartedAt = FDateTime::UtcNow();
			WriteProgressFile(NextJob, 0);
		}
		else
		{
			NextJob->State = EEpicodeRenderState::Failed;
			NextJob->Stage = TEXT("launch-failed");
			NextJob->FinishedAt = FDateTime::UtcNow();
			NextJob->Result.ErrorCode = TEXT("RENDER_LAUNCH_FAILED");
			NextJob->Result.ErrorMessage = LaunchError;
			NextJob->Progress = 1.0f;
			WriteResultFile(NextJob->JobDirectory, NextJob->Result);
			WriteProgressFile(NextJob, 0, TEXT("failed"));
		}
	}
	return true;
}

void FEpicodeRenderService::FinishJobFromDisk(const TSharedPtr<FEpicodeRenderJob>& Job)
{
	TSharedPtr<FJsonObject> ResultJson;
	const FString ResultPath = FPaths::Combine(Job->JobDirectory, TEXT("result.json"));
	if (LoadJson(ResultPath, ResultJson) && FEpicodeRenderResult::FromJson(ResultJson, Job->Result))
	{
		Job->State = Job->Result.bSuccess ? EEpicodeRenderState::Succeeded : EEpicodeRenderState::Failed;
		Job->Stage = Job->Result.bSuccess ? TEXT("done") : TEXT("failed");
		if (Job->Result.bSuccess)
		{
			FString VerifyError;
			if (!FEpicodeRenderVerifier::Verify(Job->Request, Job->Result, VerifyError))
			{
				Job->Result.bSuccess = false;
				Job->Result.ErrorCode = TEXT("RENDER_VERIFY_FAILED");
				Job->Result.ErrorMessage = VerifyError;
				FEpicodeRenderResult::ClassifyFailure(Job->Result);
				Job->State = EEpicodeRenderState::Failed;
				Job->Stage = TEXT("failed");
			}
		}
	}
	else
	{
		Job->State = EEpicodeRenderState::Failed;
		Job->Stage = TEXT("failed");
		Job->Result.bSuccess = false;
		Job->Result.ErrorCode = TEXT("RENDER_WORKER_NO_RESULT");
		Job->Result.ErrorMessage = TEXT("The render worker exited without producing result.json.");
		FEpicodeRenderResult::ClassifyFailure(Job->Result);
		CollectOutputFiles(Job->Request.OutputDirectory, Job->Result.OutputFiles);
	}
	Job->Progress = 1.0f;
	Job->FinishedAt = FDateTime::UtcNow();
	WriteProgressFile(Job, Job->Result.RenderedFrames, Job->Result.bSuccess ? TEXT("succeeded") : TEXT("failed"));
	Job->Progress = 1.0f;
	FFileHelper::SaveStringToFile(SerializeJson(Job->ToJson()), *FPaths::Combine(Job->JobDirectory, TEXT("job.json")));
	UE_LOG(LogEpicodeRender, Log, TEXT("Render job %s finished with state %d"), *Job->Id, static_cast<int32>(Job->State));
}

void FEpicodeRenderService::CollectOutputFiles(const FString& Directory, TArray<FString>& OutFiles) const
{
	::CollectOutputFiles(Directory, OutFiles);
}

bool FEpicodeRenderService::RunWorker(const FString& RequestPath)
{
	TSharedPtr<FJsonObject> RequestJson;
	if (!LoadJson(RequestPath, RequestJson))
	{
		UE_LOG(LogEpicodeRender, Error, TEXT("Unable to read render request %s"), *RequestPath);
		FPlatformMisc::RequestExit(false);
		return false;
	}
	FEpicodeRenderRequest Request;
	FString Error;
	if (!FEpicodeRenderRequest::FromJson(RequestJson, Request, Error))
	{
		UE_LOG(LogEpicodeRender, Error, TEXT("Invalid render request: %s"), *Error);
		FPlatformMisc::RequestExit(false);
		return false;
	}
	const FString JobDirectory = FPaths::GetPath(RequestPath);
	const FString JobId = FPaths::GetBaseFilename(JobDirectory);
	const FString SafeId = JobId.Replace(TEXT("-"), TEXT(""));
	if (Request.bOverwrite)
	{
		ClearExistingImages(Request.OutputDirectory);
	}
	IFileManager::Get().MakeDirectory(*FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Epicode/Generated/Maps")), true);
	IFileManager::Get().MakeDirectory(*FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Epicode/Generated/Sequences")), true);
	const FString MapPackage = FString::Printf(TEXT("/Game/Epicode/Generated/Maps/M_%s"), *SafeId);

	UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
	if (!World)
	{
		FEpicodeRenderResult Result;
		Result.ErrorCode = TEXT("RENDER_WORKER_NO_WORLD");
		Result.ErrorMessage = TEXT("Unable to create a blank render map.");
		WriteResultFile(JobDirectory, Result);
		FPlatformMisc::RequestExit(false);
		return false;
	}
	if (!UEditorLoadingAndSavingUtils::SaveMap(World, MapPackage))
	{
		FEpicodeRenderResult Result;
		Result.ErrorCode = TEXT("RENDER_WORKER_MAP_FAILED");
		Result.ErrorMessage = TEXT("Unable to save the blank render map.");
		WriteResultFile(JobDirectory, Result);
		FPlatformMisc::RequestExit(false);
		return false;
	}
	UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::Get(World);
	if (!Scene || !Scene->OpenScene(Request.SceneId, Error))
	{
		FEpicodeRenderResult Result;
		Result.ErrorCode = TEXT("RENDER_WORKER_SCENE_FAILED");
		Result.ErrorMessage = Error.IsEmpty() ? TEXT("Scene subsystem is unavailable.") : Error;
		WriteResultFile(JobDirectory, Result);
		FPlatformMisc::RequestExit(false);
		return false;
	}
	if (!UEditorLoadingAndSavingUtils::SaveMap(World, MapPackage))
	{
		FEpicodeRenderResult Result;
		Result.ErrorCode = TEXT("RENDER_WORKER_MAP_FAILED");
		Result.ErrorMessage = TEXT("Unable to save the render map with scene entities.");
		WriteResultFile(JobDirectory, Result);
		FPlatformMisc::RequestExit(false);
		return false;
	}
	FGuid RequestedCameraId;
	FGuid::Parse(Request.CameraId, RequestedCameraId);
	AActor* CameraActor = Request.CameraId.IsEmpty()
		? Scene->GetActiveCameraActor()
		: Scene->GetEntityActor(RequestedCameraId);
	if (!CameraActor)
	{
		FEpicodeRenderResult Result;
		Result.ErrorCode = TEXT("RENDER_WORKER_NO_CAMERA");
		Result.ErrorMessage = TEXT("The active camera actor was not found.");
		WriteResultFile(JobDirectory, Result);
		FPlatformMisc::RequestExit(false);
		return false;
	}

	const FString SequencePackage = FString::Printf(TEXT("/Game/Epicode/Generated/Sequences/LS_%s"), *SafeId);
	const FString SequenceName = FString::Printf(TEXT("LS_%s"), *SafeId);
	UPackage* Package = CreatePackage(*SequencePackage);
	ULevelSequence* Sequence = NewObject<ULevelSequence>(Package, FName(*SequenceName), RF_Public | RF_Standalone | RF_Transactional);
	Sequence->Initialize();
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	const FFrameRate DisplayRate(FMath::RoundToInt(Request.FrameRate), 1);
	MovieScene->SetDisplayRate(DisplayRate);
	const FFrameRate TickRate = MovieScene->GetTickResolution();
	const FFrameNumber StartFrame = FFrameRate::TransformTime(FFrameTime(FFrameNumber(Request.StartFrame)), DisplayRate, TickRate).FloorToFrame();
	const FFrameNumber EndFrame = FFrameRate::TransformTime(FFrameTime(FFrameNumber(Request.EndFrame)), DisplayRate, TickRate).CeilToFrame();
	MovieScene->SetPlaybackRange(StartFrame, (EndFrame - StartFrame).Value);
	const FGuid CameraBinding = MovieScene->AddPossessable(CameraActorName(CameraActor), CameraActor->GetClass());
	Sequence->BindPossessableObject(CameraBinding, *CameraActor, World);
	if (UMovieSceneCameraCutTrack* CutTrack = MovieScene->AddTrack<UMovieSceneCameraCutTrack>())
	{
		const FMovieSceneObjectBindingID Binding = FMovieSceneObjectBindingID(UE::MovieScene::FRelativeObjectBindingID(CameraBinding));
		if (UMovieSceneCameraCutSection* Section = CutTrack->AddNewCameraCut(Binding, StartFrame))
		{
			Section->SetRange(TRange<FFrameNumber>(StartFrame, EndFrame));
		}
	}

	FAssetRegistryModule::AssetCreated(Sequence);
	Package->MarkPackageDirty();
	if (!UEditorAssetLibrary::SaveLoadedAsset(Sequence, false))
	{
		FEpicodeRenderResult Result;
		Result.ErrorCode = TEXT("RENDER_WORKER_SEQUENCE_SAVE_FAILED");
		Result.ErrorMessage = TEXT("Unable to save the generated level sequence.");
		WriteResultFile(JobDirectory, Result);
		FPlatformMisc::RequestExit(false);
		return false;
	}

	UMoviePipelineQueue* Queue = NewObject<UMoviePipelineQueue>(GetTransientPackage());
	UMoviePipelineExecutorJob* ExecutorJob = Queue->AllocateNewJob(UMoviePipelineExecutorJob::StaticClass());
	ExecutorJob->JobName = JobId;
	ExecutorJob->SetSequence(FSoftObjectPath(Sequence->GetPathName()));
	ExecutorJob->Map = FSoftObjectPath(MapPackage);
	UMoviePipelinePrimaryConfig* Config = ExecutorJob->GetConfiguration();
	const bool bMp4 = Request.Format == TEXT("mp4");
	const FString ImageDirectory = bMp4 ? FPaths::Combine(JobDirectory, TEXT("frames")) : Request.OutputDirectory;
	if (bMp4)
	{
		IFileManager::Get().MakeDirectory(*ImageDirectory, true);
	}
	if (UMoviePipelineOutputSetting* Output = Cast<UMoviePipelineOutputSetting>(Config->FindOrAddSettingByClass(UMoviePipelineOutputSetting::StaticClass())))
	{
		Output->OutputDirectory.Path = ImageDirectory;
		Output->FileNameFormat = TEXT("{sequence_name}_{frame_number}");
		Output->OutputResolution = FIntPoint(Request.Width, Request.Height);
		Output->bUseCustomFrameRate = true;
		Output->OutputFrameRate = FFrameRate(FMath::RoundToInt(Request.FrameRate), 1);
		Output->bUseCustomPlaybackRange = true;
		Output->CustomStartFrame = Request.StartFrame;
		Output->CustomEndFrame = Request.EndFrame;
		Output->bOverrideExistingOutput = Request.bOverwrite;
	}
	Config->FindOrAddSettingByClass(UMoviePipelineDeferredPassBase::StaticClass());
	if (Request.Format == TEXT("exr"))
	{
		Config->FindOrAddSettingByClass(UMoviePipelineImageSequenceOutput_EXR::StaticClass());
	}
	else if (Request.Format == TEXT("jpg"))
	{
		Config->FindOrAddSettingByClass(UMoviePipelineImageSequenceOutput_JPG::StaticClass());
	}
	else
	{
		Config->FindOrAddSettingByClass(UMoviePipelineImageSequenceOutput_PNG::StaticClass());
	}

	UMoviePipelineQueueSubsystem* QueueSubsystem = GEditor ? GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>() : nullptr;
	if (!QueueSubsystem)
	{
		FEpicodeRenderResult Result;
		Result.ErrorCode = TEXT("RENDER_WORKER_NO_QUEUE");
		Result.ErrorMessage = TEXT("Movie Render Pipeline queue subsystem is unavailable.");
		WriteResultFile(JobDirectory, Result);
		FPlatformMisc::RequestExit(false);
		return false;
	}
	UMoviePipelineExecutorBase* Executor = QueueSubsystem->RenderQueueInstanceWithExecutor(Queue, UMoviePipelinePIEExecutor::StaticClass());
	if (!Executor)
	{
		FEpicodeRenderResult Result;
		Result.ErrorCode = TEXT("RENDER_WORKER_EXECUTOR_FAILED");
		Result.ErrorMessage = TEXT("Unable to start the PIE render executor.");
		WriteResultFile(JobDirectory, Result);
		FPlatformMisc::RequestExit(false);
		return false;
	}
	TSharedPtr<bool> bFinalized = MakeShared<bool>(false);
	const FDateTime StartTime = FDateTime::UtcNow();
	Executor->OnExecutorFinished().AddLambda([JobDirectory, Request, ImageDirectory, SequenceName, bMp4, StartTime, bFinalized](UMoviePipelineExecutorBase*, bool bSuccess)
	{
		if (*bFinalized)
		{
			return;
		}
		*bFinalized = true;
		FEpicodeRenderResult Result;
		Result.OutputDirectory = Request.OutputDirectory;
		Result.DurationSeconds = (FDateTime::UtcNow() - StartTime).GetTotalSeconds();
		if (bSuccess && bMp4)
		{
			const FString Mp4Path = FPaths::Combine(Request.OutputDirectory, SequenceName + TEXT(".mp4"));
			FString EncodeError;
			if (EncodeMp4(ImageDirectory, SequenceName, Request.FrameRate, Request.Quality, Mp4Path, EncodeError))
			{
				Result.OutputFiles.Add(Mp4Path);
			}
			else
			{
				bSuccess = false;
				Result.ErrorCode = TEXT("RENDER_MP4_ENCODE_FAILED");
				Result.ErrorMessage = EncodeError;
			}
		}
		else if (bSuccess)
		{
			::CollectOutputFiles(Request.OutputDirectory, Result.OutputFiles);
		}
		if (!bSuccess && Result.ErrorCode.IsEmpty())
		{
			Result.ErrorCode = TEXT("RENDER_FAILED");
			Result.ErrorMessage = TEXT("Movie Render Pipeline reported a failure.");
		}
		Result.bSuccess = bSuccess;
		Result.RenderedFrames = bSuccess ? (Request.EndFrame - Request.StartFrame) : 0;
		WriteResultFile(JobDirectory, Result);
		FPlatformMisc::RequestExit(false);
	});
	Executor->OnExecutorErrored().AddLambda([JobDirectory, Request, StartTime, bFinalized](UMoviePipelineExecutorBase*, UMoviePipeline*, bool bFatal, FText ErrorText)
	{
		if (*bFinalized || !bFatal)
		{
			return;
		}
		*bFinalized = true;
		FEpicodeRenderResult Result;
		Result.ErrorCode = TEXT("RENDER_ERROR");
		Result.ErrorMessage = ErrorText.ToString();
		Result.OutputDirectory = Request.OutputDirectory;
		Result.DurationSeconds = (FDateTime::UtcNow() - StartTime).GetTotalSeconds();
		::CollectOutputFiles(Request.OutputDirectory, Result.OutputFiles);
		WriteResultFile(JobDirectory, Result);
		FPlatformMisc::RequestExit(false);
	});
	UE_LOG(LogEpicodeRender, Log, TEXT("Render worker started job %s with %s"), *JobId, *Sequence->GetPathName());
	return true;
}
