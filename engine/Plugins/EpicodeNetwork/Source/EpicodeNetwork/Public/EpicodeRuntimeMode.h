#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** Resolved server/client launch mode, sourced from the launch scripts. */
struct EPICODENETWORK_API FEpicodeRuntimeMode
{
	FString ServerMode = TEXT("none");
	bool bServerTargetSupported = true;
	FString ServerFallbackReason;
	FString ServerBinary;
	FString ClientMode = TEXT("none");

	static FEpicodeRuntimeMode Resolve();
	TSharedPtr<FJsonObject> ToJson() const;
	void ApplyToJson(const TSharedPtr<FJsonObject>& InOutJson) const;

	static FString ResolveServerMode(const FString& InCommandLine, const FString& InEnvironmentMode,
		const FString& InFallbackReason);
	static bool ParseBoolEnvironment(const FString& InValue, bool bDefault);
};
