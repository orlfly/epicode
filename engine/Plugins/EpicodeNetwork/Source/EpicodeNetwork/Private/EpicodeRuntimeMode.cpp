#include "EpicodeRuntimeMode.h"

#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	FString ReadEnvironment(const TCHAR* InName)
	{
		return FPlatformMisc::GetEnvironmentVariable(InName).TrimStartAndEnd();
	}
}

FString FEpicodeRuntimeMode::ResolveServerMode(const FString& InCommandLine,
	const FString& InEnvironmentMode, const FString& InFallbackReason)
{
	(void)InFallbackReason;
	const FString EnvironmentMode = InEnvironmentMode.TrimStartAndEnd().ToLower();
	if (!EnvironmentMode.IsEmpty() && EnvironmentMode != TEXT("auto"))
	{
		return EnvironmentMode;
	}
	if (FParse::Param(*InCommandLine, TEXT("server"))
		|| FParse::Param(*InCommandLine, TEXT("SERVER")))
	{
		return TEXT("editor-fallback");
	}
	return TEXT("none");
}

bool FEpicodeRuntimeMode::ParseBoolEnvironment(const FString& InValue, bool bDefault)
{
	const FString Value = InValue.TrimStartAndEnd().ToLower();
	if (Value == TEXT("1") || Value == TEXT("true") || Value == TEXT("yes") || Value == TEXT("on"))
	{
		return true;
	}
	if (Value == TEXT("0") || Value == TEXT("false") || Value == TEXT("no") || Value == TEXT("off"))
	{
		return false;
	}
	return bDefault;
}

FEpicodeRuntimeMode FEpicodeRuntimeMode::Resolve()
{
	FEpicodeRuntimeMode Mode;
	const FString EnvironmentMode = ReadEnvironment(TEXT("EPICODE_SERVER_MODE"));
	const FString EnvironmentReason = ReadEnvironment(TEXT("EPICODE_SERVER_FALLBACK_REASON"));
	Mode.ServerMode = ResolveServerMode(FCommandLine::Get(), EnvironmentMode, EnvironmentReason);
	Mode.bServerTargetSupported = ParseBoolEnvironment(
		ReadEnvironment(TEXT("EPICODE_SERVER_TARGET_SUPPORTED")),
		Mode.ServerMode == TEXT("server-target"));

	Mode.ServerFallbackReason = EnvironmentReason;
	if (Mode.ServerMode == TEXT("editor-fallback") && Mode.ServerFallbackReason.IsEmpty())
	{
		Mode.ServerFallbackReason = TEXT("EpicodeVideoServer is unavailable; using UnrealEditor-Cmd -server.");
	}
	Mode.ServerBinary = ReadEnvironment(TEXT("EPICODE_SERVER_BINARY"));
	if (Mode.ServerBinary.IsEmpty() && Mode.ServerMode != TEXT("none"))
	{
		Mode.ServerBinary = FPlatformProcess::ExecutablePath();
	}
	Mode.ClientMode = ReadEnvironment(TEXT("EPICODE_CLIENT_MODE"));
	if (Mode.ClientMode.IsEmpty())
	{
		Mode.ClientMode = TEXT("none");
	}
	return Mode;
}

TSharedPtr<FJsonObject> FEpicodeRuntimeMode::ToJson() const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	ApplyToJson(Json);
	return Json;
}

void FEpicodeRuntimeMode::ApplyToJson(const TSharedPtr<FJsonObject>& InOutJson) const
{
	if (!InOutJson.IsValid())
	{
		return;
	}
	InOutJson->SetStringField(TEXT("serverMode"), ServerMode);
	InOutJson->SetBoolField(TEXT("serverTargetSupported"), bServerTargetSupported);
	InOutJson->SetStringField(TEXT("fallbackReason"), ServerFallbackReason);
	InOutJson->SetStringField(TEXT("serverBinary"), ServerBinary);
	InOutJson->SetStringField(TEXT("clientMode"), ClientMode);
}
