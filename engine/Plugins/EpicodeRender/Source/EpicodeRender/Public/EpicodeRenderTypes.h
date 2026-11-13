#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "EpicodeTypes.h"
#include "GenericPlatform/GenericPlatformProcess.h"

enum class EEpicodeRenderState : uint8
{
	Queued,
	Running,
	Succeeded,
	Failed,
	Cancelled
};

struct EPICODERENDER_API FEpicodeRenderRequest
{
	FString SceneId;
	FString CameraId;
	FString OutputDirectory;
	FString Format = TEXT("png");
	FString Codec = TEXT("h264");
	int32 Width = 1920;
	int32 Height = 1080;
	double FrameRate = 24.0;
	int32 StartFrame = 0;
	int32 EndFrame = 120;
	int32 Quality = 20;
	bool bOverwrite = true;
	bool bIncludeAudio = false;

	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& Object, FEpicodeRenderRequest& OutRequest, FString& OutError);
};

struct EPICODERENDER_API FEpicodeRenderResult
{
	bool bSuccess = false;
	FString ErrorCode;
	FString ErrorMessage;
	bool bRetryable = false;
	FString Diagnostics;
	FString OutputDirectory;
	TArray<FString> OutputFiles;
	TMap<FString, FString> Checksums;
	int32 RenderedFrames = 0;
	double DurationSeconds = 0.0;

	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& Object, FEpicodeRenderResult& OutResult);
	static void ClassifyFailure(FEpicodeRenderResult& Result);
};

struct EPICODERENDER_API FEpicodeRenderVerifier
{
	// Checks frame count, non-empty files, PNG/JPG resolution and MP4 duration, and fills Result.Checksums.
	static bool Verify(const FEpicodeRenderRequest& Request, FEpicodeRenderResult& Result, FString& OutError);
};

struct EPICODERENDER_API FEpicodeRenderJob
{
	FString Id;
	FEpicodeRenderRequest Request;
	EEpicodeRenderState State = EEpicodeRenderState::Queued;
	FString Stage;
	float Progress = 0.0f;
	FDateTime CreatedAt = FDateTime::UtcNow();
	FDateTime StartedAt;
	FDateTime FinishedAt;
	FEpicodeRenderResult Result;
	FString JobDirectory;
	uint32 ProcessId = 0;
	FProcHandle ProcessHandle;
	double LastProgressWriteSeconds = 0.0;

	TSharedPtr<FJsonObject> ToJson() const;
};
