#include "Misc/AutomationTest.h"

#include "EpicodeRenderService.h"
#include "EpicodeRenderTypes.h"
#include "EpicodeScopedTestSceneRoot.h"
#include "EpicodeSceneSubsystem.h"
#include "EpicodeSettings.h"
#include "EpicodeDiagnostics.h"
#include "HAL/FileManager.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	class FEpicodeRenderTimeoutCheckCommand final : public IAutomationLatentCommand
	{
	public:
		FEpicodeRenderTimeoutCheckCommand(
			FAutomationTestBase* InTest,
			UEpicodeSceneSubsystem* InScene,
			const FString& InJobId,
			int32 InOldTimeout,
			TSharedPtr<FEpicodeScopedTestSceneRoot> InSceneRootGuard)
			: Test(InTest)
			, Scene(InScene)
			, JobId(InJobId)
			, OldTimeout(InOldTimeout)
			, SceneRootGuard(MoveTemp(InSceneRootGuard))
		{
		}

		virtual bool Update() override
		{
			const TSharedPtr<FEpicodeRenderJob> Job = FEpicodeRenderService::Get().GetJob(JobId);
			if (Job.IsValid() && Job->State != EEpicodeRenderState::Failed && Job->State != EEpicodeRenderState::Succeeded && Job->State != EEpicodeRenderState::Cancelled && Frames++ < 240)
			{
				return false;
			}
			if (Test)
			{
				Test->TestTrue(TEXT("timed out job failed"), Job.IsValid() && Job->State == EEpicodeRenderState::Failed);
				Test->TestEqual(TEXT("timeout error code"), Job.IsValid() ? Job->Result.ErrorCode : FString(), FString(TEXT("RENDER_TIMEOUT")));
			}
			if (UEpicodeSettings* Settings = GetMutableDefault<UEpicodeSettings>())
			{
				Settings->RenderTimeoutSeconds = OldTimeout;
			}
			if (Scene)
			{
				FString Error;
				Scene->CloseScene(true, Error);
			}
			return true;
		}

	private:
		FAutomationTestBase* Test = nullptr;
		UEpicodeSceneSubsystem* Scene = nullptr;
		FString JobId;
		int32 OldTimeout = 1800;
		int32 Frames = 0;
		TSharedPtr<FEpicodeScopedTestSceneRoot> SceneRootGuard;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeRenderRequestTest, "Epicode.Render.Job", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeRenderRequestTest::RunTest(const FString& Parameters)
{
	FEpicodeRenderRequest Request;
	Request.Format = TEXT("png");
	Request.Width = 320;
	Request.Height = 180;
	Request.StartFrame = 0;
	Request.EndFrame = 3;
	Request.FrameRate = 24.0;

	const TSharedPtr<FJsonObject> Json = Request.ToJson();
	FEpicodeRenderRequest Loaded;
	FString Error;
	TestTrue(TEXT("request round trip"), FEpicodeRenderRequest::FromJson(Json, Loaded, Error));
	TestEqual(TEXT("width"), Loaded.Width, 320);
	TestEqual(TEXT("height"), Loaded.Height, 180);
	TestEqual(TEXT("format"), Loaded.Format, FString(TEXT("png")));

	TSharedPtr<FJsonObject> InvalidRange = Request.ToJson();
	InvalidRange->SetNumberField(TEXT("endFrame"), 0);
	TestFalse(TEXT("bad frame range rejected"), FEpicodeRenderRequest::FromJson(InvalidRange, Loaded, Error));

	TSharedPtr<FJsonObject> InvalidSize = Request.ToJson();
	InvalidSize->SetNumberField(TEXT("width"), 1);
	TestFalse(TEXT("bad width rejected"), FEpicodeRenderRequest::FromJson(InvalidSize, Loaded, Error));

	TSharedPtr<FJsonObject> InvalidFormat = Request.ToJson();
	InvalidFormat->SetStringField(TEXT("format"), TEXT("gif"));
	TestFalse(TEXT("bad format rejected"), FEpicodeRenderRequest::FromJson(InvalidFormat, Loaded, Error));

	TSharedPtr<FJsonObject> Mp4 = Request.ToJson();
	Mp4->SetStringField(TEXT("format"), TEXT("mp4"));
	Mp4->SetNumberField(TEXT("quality"), 12);
	TestFalse(TEXT("bad mp4 quality rejected"), FEpicodeRenderRequest::FromJson(Mp4, Loaded, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeRenderResultTest, "Epicode.Render.Verify", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeRenderResultTest::RunTest(const FString& Parameters)
{
	FEpicodeRenderResult Result;
	Result.bSuccess = true;
	Result.RenderedFrames = 3;
	Result.OutputFiles.Add(TEXT("/tmp/frame_0000.png"));
	Result.OutputFiles.Add(TEXT("/tmp/frame_0001.png"));
	Result.OutputFiles.Add(TEXT("/tmp/frame_0002.png"));
	Result.Checksums.Add(TEXT("frame_0000.png"), TEXT("abc123"));

	const TSharedPtr<FJsonObject> Json = Result.ToJson();
	FEpicodeRenderResult Loaded;
	TestTrue(TEXT("result round trip"), FEpicodeRenderResult::FromJson(Json, Loaded));
	TestTrue(TEXT("success"), Loaded.bSuccess);
	TestEqual(TEXT("frames"), Loaded.RenderedFrames, 3);
	TestEqual(TEXT("files"), Loaded.OutputFiles.Num(), 3);
	TestEqual(TEXT("checksums"), Loaded.Checksums.Num(), 1);
	TestEqual(TEXT("checksum value"), Loaded.Checksums.FindRef(TEXT("frame_0000.png")), FString(TEXT("abc123")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeRenderOutputVerifyTest, "Epicode.Render.OutputVerify", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeRenderOutputVerifyTest::RunTest(const FString& Parameters)
{
	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("EpicodeTests"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString ImagePath = FPaths::Combine(Directory, TEXT("frame.png"));

	IImageWrapperModule& ImageModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Image = ImageModule.CreateImageWrapper(EImageFormat::PNG);
	TArray<uint8> Raw;
	Raw.SetNumZeroed(2 * 2 * 4);
	TestTrue(TEXT("raw image prepared"), Image.IsValid() && Image->SetRaw(Raw.GetData(), Raw.Num(), 2, 2, ERGBFormat::BGRA, 8));
	const TArray64<uint8> Compressed = Image->GetCompressed();
	TestTrue(TEXT("test png written"), FFileHelper::SaveArrayToFile(Compressed, *ImagePath));

	FEpicodeRenderRequest Request;
	Request.Format = TEXT("png");
	Request.Width = 2;
	Request.Height = 2;
	Request.StartFrame = 0;
	Request.EndFrame = 1;
	FEpicodeRenderResult Result;
	Result.OutputFiles.Add(ImagePath);
	FString Error;
	TestTrue(TEXT("valid output verifies"), FEpicodeRenderVerifier::Verify(Request, Result, Error));
	TestEqual(TEXT("checksum recorded"), Result.Checksums.Num(), 1);

	Request.Width = 4;
	TestFalse(TEXT("resolution mismatch rejected"), FEpicodeRenderVerifier::Verify(Request, Result, Error));

	Request.Width = 2;
	Request.EndFrame = 3;
	TestFalse(TEXT("frame count mismatch rejected"), FEpicodeRenderVerifier::Verify(Request, Result, Error));

	IFileManager::Get().DeleteDirectory(*Directory, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeRenderCancelTest, "Epicode.Render.Cancel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeRenderCancelTest::RunTest(const FString& Parameters)
{
	FEpicodeScopedTestSceneRoot SceneRootGuard(TEXT("RenderCancel"));
	UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
	if (!Scene)
	{
		AddError(TEXT("Scene subsystem is unavailable."));
		return false;
	}
	FString Error;
	if (Scene->HasScene())
	{
		Scene->CloseScene(true, Error);
	}
	if (!Scene->NewScene(TEXT("Render Cancel Test"), Error))
	{
		AddError(Error);
		return false;
	}

	FEpicodeRenderRequest Request;
	Request.Format = TEXT("png");
	Request.Width = 640;
	Request.Height = 360;
	Request.StartFrame = 0;
	Request.EndFrame = 240;
	Request.FrameRate = 24.0;
	TSharedPtr<FEpicodeRenderJob> Job = FEpicodeRenderService::Get().Submit(Request, Error);
	TestTrue(TEXT("render job submitted"), Job.IsValid());
	if (Job.IsValid())
	{
		TestTrue(TEXT("render job is running"), Job->State == EEpicodeRenderState::Running);
		TSharedPtr<FEpicodeRenderJob> QueuedJob = FEpicodeRenderService::Get().Submit(Request, Error);
		TestTrue(TEXT("second render queued"), QueuedJob.IsValid());
		if (QueuedJob.IsValid())
		{
			TestTrue(TEXT("queued state"), QueuedJob->State == EEpicodeRenderState::Queued);
			TestTrue(TEXT("queued render cancelled"), FEpicodeRenderService::Get().Cancel(QueuedJob->Id));
			TestTrue(TEXT("queued cancel state"), QueuedJob->State == EEpicodeRenderState::Cancelled);
		}
		TestTrue(TEXT("render job cancelled"), FEpicodeRenderService::Get().Cancel(Job->Id));
		TestTrue(TEXT("cancelled state"), Job->State == EEpicodeRenderState::Cancelled);
		TestEqual(TEXT("cancel error code"), Job->Result.ErrorCode, FString(TEXT("RENDER_CANCELLED")));
		TestFalse(TEXT("cancel again rejected"), FEpicodeRenderService::Get().Cancel(Job->Id));
	}
	TestFalse(TEXT("unknown job cancel rejected"), FEpicodeRenderService::Get().Cancel(TEXT("missing-job")));

	Scene->CloseScene(true, Error);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeRenderTimeoutTest, "Epicode.Render.Timeout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeRenderTimeoutTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FEpicodeScopedTestSceneRoot> SceneRootGuard =
		MakeShared<FEpicodeScopedTestSceneRoot>(TEXT("RenderTimeout"));
	UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
	UEpicodeSettings* Settings = GetMutableDefault<UEpicodeSettings>();
	if (!Scene || !Settings)
	{
		AddError(TEXT("Render timeout test requires scene and settings."));
		return false;
	}
	FString Error;
	if (Scene->HasScene())
	{
		Scene->CloseScene(true, Error);
	}
	if (!Scene->NewScene(TEXT("Render Timeout Test"), Error))
	{
		AddError(Error);
		return false;
	}
	const int32 OldTimeout = Settings->RenderTimeoutSeconds;
	Settings->RenderTimeoutSeconds = 1;

	FEpicodeRenderRequest Request;
	Request.Format = TEXT("png");
	Request.Width = 640;
	Request.Height = 360;
	Request.StartFrame = 0;
	Request.EndFrame = 240;
	Request.FrameRate = 24.0;
	TSharedPtr<FEpicodeRenderJob> Job = FEpicodeRenderService::Get().Submit(Request, Error);
	TestTrue(TEXT("timeout render submitted"), Job.IsValid());
	if (Job.IsValid())
	{
		ADD_LATENT_AUTOMATION_COMMAND(FEpicodeRenderTimeoutCheckCommand(
			this, Scene, Job->Id, OldTimeout, SceneRootGuard));
	}
	else
	{
		Settings->RenderTimeoutSeconds = OldTimeout;
		Scene->CloseScene(true, Error);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeRenderDiagnosticsTest, "Epicode.Render.Diagnostics", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeRenderDiagnosticsTest::RunTest(const FString& Parameters)
{
	FEpicodeRenderResult Oom;
	Oom.ErrorCode = TEXT("RENDER_ERROR");
	Oom.ErrorMessage = TEXT("Out of memory while allocating render target.");
	FEpicodeRenderResult::ClassifyFailure(Oom);
	TestTrue(TEXT("oom retryable"), Oom.bRetryable);
	TestTrue(TEXT("oom diagnostic"), Oom.Diagnostics.Contains(TEXT("memory")));

	FEpicodeRenderResult DeviceLost;
	DeviceLost.ErrorCode = TEXT("RENDER_ERROR");
	DeviceLost.ErrorMessage = TEXT("GPU device lost.");
	FEpicodeRenderResult::ClassifyFailure(DeviceLost);
	TestTrue(TEXT("device lost retryable"), DeviceLost.bRetryable);
	TestTrue(TEXT("device lost diagnostic"), DeviceLost.Diagnostics.Contains(TEXT("device loss")));

	FEpicodeRenderResult DiskFull;
	DiskFull.ErrorCode = TEXT("RENDER_FAILED");
	DiskFull.ErrorMessage = TEXT("No space left on device.");
	FEpicodeRenderResult::ClassifyFailure(DiskFull);
	TestFalse(TEXT("disk full not retryable"), DiskFull.bRetryable);
	TestTrue(TEXT("disk full diagnostic"), DiskFull.Diagnostics.Contains(TEXT("disk")));

	FEpicodeRenderResult MissingAsset;
	MissingAsset.ErrorCode = TEXT("RENDER_WORKER_SCENE_FAILED");
	MissingAsset.ErrorMessage = TEXT("Scene asset missing.");
	FEpicodeRenderResult::ClassifyFailure(MissingAsset);
	TestFalse(TEXT("missing asset not retryable"), MissingAsset.bRetryable);
	TestFalse(TEXT("missing asset diagnostic"), MissingAsset.Diagnostics.IsEmpty());

	const TSharedPtr<FJsonObject> Json = Oom.ToJson();
	FEpicodeRenderResult Loaded;
	TestTrue(TEXT("diagnostics round trip"), FEpicodeRenderResult::FromJson(Json, Loaded));
	TestTrue(TEXT("retryable round trip"), Loaded.bRetryable);
	TestEqual(TEXT("diagnostics round trip text"), Loaded.Diagnostics, Oom.Diagnostics);

	const FEpicodeDiagnosticsResult Health = FEpicodeDiagnostics::Run();
	TestFalse(TEXT("render backend reported"), Health.RenderBackend.IsEmpty());
	return true;
}

#endif
