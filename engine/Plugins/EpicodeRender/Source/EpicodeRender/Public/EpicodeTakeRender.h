#pragma once

#include "CoreMinimal.h"
#include "EpicodeDirector.h"
#include "EpicodeRenderTypes.h"

struct EPICODERENDER_API FEpicodeTakeRenderTrack
{
	FString CameraId;
	int64 StartSequence = 0;
	int64 EndSequence = 0;

	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeTakeRenderTrack& OutTrack, FString& OutError);
};

struct EPICODERENDER_API FEpicodeTakeRenderRequest
{
	FString TakeRoot;
	FString TakeId;
	int32 TakeVersion = 0;
	FString OutputDirectory;
	FString Format = TEXT("png");
	FString ExpectedContentDigest;
	TArray<FEpicodeTakeRenderTrack> Tracks;

	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeTakeRenderRequest& OutRequest, FString& OutError);
};

class EPICODERENDER_API FEpicodeTakeRender
{
public:
	static bool Prepare(
		const FEpicodeTakeRenderRequest& InRequest,
		FEpicodeDirectorTake& OutTake,
		FEpicodeRenderRequest& OutRenderRequest,
		FString& OutError);

	static bool ValidateTracks(
		const FEpicodeDirectorTake& InTake,
		const TArray<FEpicodeTakeRenderTrack>& InTracks,
		FString& OutError);

	// Expands the implicit first-camera track and validates ordering, range and cameras.
	static bool ResolveTracks(
		const FEpicodeDirectorTake& InTake,
		const TArray<FEpicodeTakeRenderTrack>& InTracks,
		TArray<FEpicodeTakeRenderTrack>& OutTracks,
		FString& OutError);

	// Maps a take sequence number onto the render frame range of the take.
	static int64 FrameForSequence(const FEpicodeDirectorTake& InTake, int64 InSequence);

	static bool WriteManifest(
		const FEpicodeTakeRenderRequest& InRequest,
		const FEpicodeDirectorTake& InTake,
		const FEpicodeRenderRequest& InRenderRequest,
		const FString& InJobId,
		FString& OutManifestPath,
		FString& OutError,
		const TArray<FString>& InJobIds = TArray<FString>());

	static FString ComputeTakeDigest(const FEpicodeDirectorTake& InTake);
};
