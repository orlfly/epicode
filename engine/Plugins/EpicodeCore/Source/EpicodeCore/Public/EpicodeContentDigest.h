#pragma once

#include "CoreMinimal.h"

/** Stable content and build digests used by replay, takes and diagnostics. */
class EPICODECORE_API FEpicodeContentDigest
{
public:
	static FString HashString(const FString& InValue);

	/** Hashes sorted relative paths and file contents under Root. */
	static bool ComputeDirectoryDigest(const FString& Root, FString& OutDigest, FString& OutError);

	/** Combines engine, project, rule schema, content and config digests. */
	static bool ComputeBuildDigest(const FString& RuleSchemaVersion, FString& OutDigest, FString& OutError);

	static FString GetCachedBuildDigest();
	static void InvalidateCache();
};
