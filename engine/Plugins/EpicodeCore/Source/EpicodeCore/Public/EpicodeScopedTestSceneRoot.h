#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Redirects the scene library, scene trash and render output to a temporary
 * directory for the lifetime of an automation test, then restores the previous
 * settings. This keeps test scenes out of the user's production library.
 */
class EPICODECORE_API FEpicodeScopedTestSceneRoot
{
public:
	explicit FEpicodeScopedTestSceneRoot(const FString& InLabel = TEXT("Automation"));
	~FEpicodeScopedTestSceneRoot();

	FEpicodeScopedTestSceneRoot(const FEpicodeScopedTestSceneRoot&) = delete;
	FEpicodeScopedTestSceneRoot& operator=(const FEpicodeScopedTestSceneRoot&) = delete;

	const FString& GetRoot() const { return Root; }

private:
	FString Root;
	FString PreviousSceneRoot;
	FString PreviousSceneTrashRoot;
	FString PreviousRenderOutputRoot;
	bool bSettingsCaptured = false;
};

#endif
