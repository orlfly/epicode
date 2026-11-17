#pragma once

#include "CoreMinimal.h"

/** Reads the DeepSeek API key from the environment or a local, untracked file. */
class EPICODEASSISTANT_API FConversationSecretStore
{
public:
	static FString GetEnvironmentVariableName();

	/** Absolute path of the default key file. */
	static FString GetDefaultKeyFilePath();

	/**
	 * Reads DEEPSEEK_API_KEY first, then the configured key file.
	 * OutSource is "environment", "file", or "missing"; OutError never contains the key.
	 */
	static bool Read(
		FString& OutKey,
		FString& OutSource,
		FString& OutError);

	/** Safe for logs and UI: first/last four characters only. */
	static FString Mask(const FString& Secret);

	/** Creates an empty template with owner-only permissions when missing. */
	static bool EnsureTemplateFile(FString& OutPath, FString& OutError);
};
