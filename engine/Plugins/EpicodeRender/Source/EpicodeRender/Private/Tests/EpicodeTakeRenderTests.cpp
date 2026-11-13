#include "Misc/AutomationTest.h"

#include "EpicodeDirector.h"
#include "EpicodeRenderService.h"
#include "EpicodeTakeRender.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FString MakeTakeRoot()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Epicode"), TEXT("TakeRenderTests"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
	}

	FEpicodeDirectorTake MakeTake(const FString& InId, int32 InVersion = 1)
	{
		FEpicodeDirectorTake Take;
		Take.Id = InId;
		Take.Version = InVersion;
		Take.InSequence = 10;
		Take.OutSequence = 30;
		Take.InTick = 100;
		Take.OutTick = 200;
		Take.BeatId = TEXT("beat-1");
		Take.ResolutionPreset = TEXT("1080p");
		Take.ContentDigest = TEXT("content-digest-1");
		Take.CameraIds = { TEXT("camera-a"), TEXT("camera-b") };
		Take.Checksum = Take.ComputeChecksum();
		return Take;
	}

	bool SaveTake(const FString& InRoot, FEpicodeDirectorTake& InOutTake)
	{
		FString Error;
		return FEpicodeTakeLibrary::Save(InRoot, InOutTake, Error);
	}

	FEpicodeTakeRenderRequest MakeRequest(const FString& InRoot, const FString& InTakeId, const FString& InOutput)
	{
		FEpicodeTakeRenderRequest Request;
		Request.TakeRoot = InRoot;
		Request.TakeId = InTakeId;
		Request.TakeVersion = 1;
		Request.OutputDirectory = InOutput;
		Request.Format = TEXT("png");
		Request.ExpectedContentDigest = TEXT("content-digest-1");
		return Request;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeRenderTakeTest, "Epicode.Render.Take", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeRenderTakeTest::RunTest(const FString& Parameters)
{
	const FString Root = MakeTakeRoot();
	FEpicodeDirectorTake Take = MakeTake(TEXT("take-render"));
	TestTrue(TEXT("take saved"), SaveTake(Root, Take));
	const FString Output = FPaths::Combine(Root, TEXT("output"));
	FEpicodeTakeRenderRequest Request = MakeRequest(Root, TEXT("take-render"), Output);
	FEpicodeDirectorTake Loaded;
	FEpicodeRenderRequest RenderRequest;
	FString Error;
	TestTrue(TEXT("prepare succeeds"), FEpicodeTakeRender::Prepare(Request, Loaded, RenderRequest, Error));
	TestEqual(TEXT("take version"), Loaded.Version, 1);
	TestEqual(TEXT("render start frame"), RenderRequest.StartFrame, 100);
	TestEqual(TEXT("render end frame"), RenderRequest.EndFrame, 200);
	TestEqual(TEXT("output directory"), RenderRequest.OutputDirectory, FPaths::ConvertRelativePathToFull(Output));

	Request.TakeId = TEXT("missing-take");
	TestFalse(TEXT("missing take rejected"), FEpicodeTakeRender::Prepare(Request, Loaded, RenderRequest, Error));
	TestTrue(TEXT("missing take code"), Error.Contains(TEXT("TAKE_NOT_FOUND")));

	const FString Blocked = FPaths::Combine(Root, TEXT("blocked"));
	TestTrue(TEXT("blocking file created"), FFileHelper::SaveStringToFile(TEXT("x"), *Blocked));
	Request = MakeRequest(Root, TEXT("take-render"), FPaths::Combine(Blocked, TEXT("nested")));
	TestFalse(TEXT("unwritable output rejected"), FEpicodeTakeRender::Prepare(Request, Loaded, RenderRequest, Error));
	TestTrue(FString::Printf(TEXT("unwritable code (%s)"), *Error), Error.Contains(TEXT("TAKE_OUTPUT_UNWRITABLE")));

	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeRenderTakeDeterminismTest, "Epicode.Render.TakeDeterminism", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeRenderTakeDeterminismTest::RunTest(const FString& Parameters)
{
	const FString Root = MakeTakeRoot();
	FEpicodeDirectorTake Take = MakeTake(TEXT("take-det"));
	TestTrue(TEXT("take saved"), SaveTake(Root, Take));
	FEpicodeTakeRenderRequest Request = MakeRequest(Root, TEXT("take-det"), FPaths::Combine(Root, TEXT("out")));
	FEpicodeDirectorTake Loaded;
	FEpicodeRenderRequest RenderRequest;
	FString Error;
	TestTrue(TEXT("digest matches"), FEpicodeTakeRender::Prepare(Request, Loaded, RenderRequest, Error));
	TestEqual(TEXT("computed digest stable"), FEpicodeTakeRender::ComputeTakeDigest(Take), FEpicodeTakeRender::ComputeTakeDigest(Loaded));

	Request.ExpectedContentDigest = TEXT("other-digest");
	TestFalse(TEXT("digest mismatch rejected"), FEpicodeTakeRender::Prepare(Request, Loaded, RenderRequest, Error));
	TestTrue(FString::Printf(TEXT("digest mismatch code (%s)"), *Error), Error.Contains(TEXT("TAKE_DIGEST_MISMATCH")));

	Request.ExpectedContentDigest.Reset();
	FEpicodeDirectorTake NoDigest = MakeTake(TEXT("take-no-digest"));
	NoDigest.ContentDigest.Reset();
	NoDigest.Checksum = NoDigest.ComputeChecksum();
	TestTrue(TEXT("no digest take saved"), SaveTake(Root, NoDigest));
	Request.TakeId = TEXT("take-no-digest");
	TestFalse(TEXT("missing digest rejected"), FEpicodeTakeRender::Prepare(Request, Loaded, RenderRequest, Error));
	TestTrue(FString::Printf(TEXT("missing digest code (%s)"), *Error), Error.Contains(TEXT("TAKE_DIGEST_MISSING")));

	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeRenderTakeTracksTest, "Epicode.Render.TakeTracks", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeRenderTakeTracksTest::RunTest(const FString& Parameters)
{
	const FString Root = MakeTakeRoot();
	FEpicodeDirectorTake Take = MakeTake(TEXT("take-tracks"));
	TestTrue(TEXT("take saved"), SaveTake(Root, Take));
	FEpicodeTakeRenderRequest Request = MakeRequest(Root, TEXT("take-tracks"), FPaths::Combine(Root, TEXT("out")));

	FEpicodeTakeRenderTrack First;
	First.CameraId = TEXT("camera-a");
	First.StartSequence = 10;
	First.EndSequence = 20;
	FEpicodeTakeRenderTrack Second;
	Second.CameraId = TEXT("camera-b");
	Second.StartSequence = 20;
	Second.EndSequence = 30;
	Request.Tracks = { First, Second };
	FEpicodeDirectorTake Loaded;
	FEpicodeRenderRequest RenderRequest;
	FString Error;
	TestTrue(TEXT("multi camera tracks accepted"), FEpicodeTakeRender::Prepare(Request, Loaded, RenderRequest, Error));
	TestEqual(TEXT("track count"), Request.Tracks.Num(), 2);
	TestEqual(TEXT("track one starts at in tick"), FEpicodeTakeRender::FrameForSequence(Take, First.StartSequence), static_cast<int64>(100));
	TestEqual(TEXT("track split frame"), FEpicodeTakeRender::FrameForSequence(Take, 20), static_cast<int64>(150));
	TestEqual(TEXT("track two ends at out tick"), FEpicodeTakeRender::FrameForSequence(Take, Second.EndSequence), static_cast<int64>(200));
	TArray<FEpicodeTakeRenderTrack> ImplicitTracks;
	TestTrue(TEXT("implicit single camera track"), FEpicodeTakeRender::ResolveTracks(Take, {}, ImplicitTracks, Error));
	TestEqual(TEXT("implicit track count"), ImplicitTracks.Num(), 1);
	TestEqual(TEXT("implicit track camera"), ImplicitTracks[0].CameraId, FString(TEXT("camera-a")));
	TestEqual(TEXT("implicit track starts at take range"), ImplicitTracks[0].StartSequence, Take.InSequence);
	TestEqual(TEXT("implicit track ends at take range"), ImplicitTracks[0].EndSequence, Take.OutSequence);

	Request.Tracks[1].StartSequence = 15;
	TestFalse(TEXT("overlap rejected"), FEpicodeTakeRender::ValidateTracks(Take, Request.Tracks, Error));
	TestTrue(TEXT("overlap code"), Error.Contains(TEXT("TAKE_TRACK_OVERLAP")));

	Request.Tracks[1].StartSequence = 20;
	Request.Tracks[0].CameraId = TEXT("missing-camera");
	TestFalse(TEXT("missing camera rejected"), FEpicodeTakeRender::ValidateTracks(Take, Request.Tracks, Error));
	TestTrue(TEXT("missing camera code"), Error.Contains(TEXT("TAKE_CAMERA_MISSING")));

	Request.Tracks.Reset();
	FEpicodeDirectorTake NoCamera = Take;
	NoCamera.CameraIds.Reset();
	NoCamera.Checksum = NoCamera.ComputeChecksum();
	TestFalse(TEXT("no camera tracks rejected"), FEpicodeTakeRender::ValidateTracks(NoCamera, Request.Tracks, Error));
	TestTrue(TEXT("no camera code"), Error.Contains(TEXT("TAKE_CAMERA_MISSING")));

	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeRenderTakeLifecycleTest, "Epicode.Render.TakeLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeRenderTakeLifecycleTest::RunTest(const FString& Parameters)
{
	const FString Root = MakeTakeRoot();
	FEpicodeDirectorTake Take = MakeTake(TEXT("take-life"));
	TestTrue(TEXT("take saved"), SaveTake(Root, Take));
	FEpicodeTakeRenderRequest Request = MakeRequest(Root, TEXT("take-life"), FPaths::Combine(Root, TEXT("out")));
	FEpicodeDirectorTake Loaded;
	FEpicodeRenderRequest RenderRequest;
	FString Error;
	if (!TestTrue(TEXT("prepare"), FEpicodeTakeRender::Prepare(Request, Loaded, RenderRequest, Error)))
	{
		IFileManager::Get().DeleteDirectory(*Root, false, true);
		return false;
	}
	FString ManifestPath;
	TestTrue(TEXT("manifest written"), FEpicodeTakeRender::WriteManifest(Request, Loaded, RenderRequest, TEXT("job-1"), ManifestPath, Error));
	TestTrue(TEXT("manifest exists"), IFileManager::Get().FileExists(*ManifestPath));
	FString Manifest;
	TestTrue(TEXT("manifest readable"), FFileHelper::LoadFileToString(Manifest, *ManifestPath));
	TestTrue(TEXT("manifest has take id"), Manifest.Contains(TEXT("take-life")));
	TestTrue(TEXT("manifest has job id"), Manifest.Contains(TEXT("job-1")));
	TestTrue(TEXT("manifest has output pattern"), Manifest.Contains(TEXT("track_00_camera-a")));
	TestFalse(TEXT("unknown render job cannot cancel"), FEpicodeRenderService::Get().Cancel(TEXT("missing-job")));
	FString RetryError;
	TestFalse(TEXT("unknown render job cannot retry"), FEpicodeRenderService::Get().Retry(TEXT("missing-job"), RetryError).IsValid());
	TestTrue(FString::Printf(TEXT("retry error (%s)"), *RetryError), RetryError.Contains(TEXT("not found"), ESearchCase::IgnoreCase));

	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

#endif
