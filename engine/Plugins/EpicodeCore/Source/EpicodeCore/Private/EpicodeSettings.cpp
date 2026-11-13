#include "EpicodeSettings.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/App.h"
#include "Misc/Base64.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"

#if PLATFORM_LINUX
#include <sys/stat.h>
#endif

namespace
{
	FString ReadEnv(const TCHAR* Name)
	{
		return FPlatformMisc::GetEnvironmentVariable(Name).TrimStartAndEnd();
	}

	bool ParsePortEnv(const TCHAR* Name, int32& InOutPort)
	{
		const FString Value = ReadEnv(Name);
		if (Value.IsEmpty())
		{
			return false;
		}
		const int32 Parsed = FCString::Atoi(*Value);
		if (Parsed <= 0 || Parsed > 65535)
		{
			return false;
		}
		InOutPort = Parsed;
		return true;
	}
}

UEpicodeSettings::UEpicodeSettings()
{
	ImportRoots.Add(FPaths::ProjectDir());
	ImportRoots.Add(TEXT("/tmp/epicode-imports"));
}

void UEpicodeSettings::PostInitProperties()
{
	Super::PostInitProperties();
	ApplyEnvironmentOverrides();
}

void UEpicodeSettings::PostReloadConfig(FProperty* PropertyThatWasLoaded)
{
	Super::PostReloadConfig(PropertyThatWasLoaded);
	ApplyEnvironmentOverrides();
}

void UEpicodeSettings::ApplyEnvironmentOverrides()
{
	const FString HostOverride = ReadEnv(TEXT("EPICODE_API_HOST"));
	if (!HostOverride.IsEmpty())
	{
		ApiHost = HostOverride;
	}

	ParsePortEnv(TEXT("EPICODE_API_HTTP_PORT"), ApiHttpPort);
	ParsePortEnv(TEXT("EPICODE_API_WS_PORT"), ApiWsPort);
	const FString MaxRequestsOverride = ReadEnv(TEXT("EPICODE_MAX_RPS"));
	if (!MaxRequestsOverride.IsEmpty())
	{
		const int32 Parsed = FCString::Atoi(*MaxRequestsOverride);
		if (Parsed > 0)
		{
			MaxRequestsPerSecond = Parsed;
		}
	}

	const FString AllowRemote = ReadEnv(TEXT("EPICODE_ALLOW_REMOTE"));
	if (!AllowRemote.IsEmpty())
	{
		bAllowRemote = AllowRemote.Equals(TEXT("1")) || AllowRemote.Equals(TEXT("true"), ESearchCase::IgnoreCase);
	}

	const FString OutputOverride = ReadEnv(TEXT("EPICODE_OUTPUT_DIR"));
	if (!OutputOverride.IsEmpty())
	{
		RenderOutputRoot = OutputOverride;
	}
}

FString UEpicodeSettings::ResolveProjectPath(const FString& InPath) const
{
	if (InPath.IsEmpty())
	{
		return FPaths::ProjectDir();
	}
	if (FPaths::IsRelative(InPath))
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), InPath));
	}
	return FPaths::ConvertRelativePathToFull(InPath);
}

FString UEpicodeSettings::GetHttpBaseUrl() const
{
	return FString::Printf(TEXT("http://%s:%d"), *ApiHost, ApiHttpPort);
}

FString UEpicodeSettings::GetWebSocketUrl() const
{
	return FString::Printf(TEXT("ws://%s:%d/api/v1/ws"), *ApiHost, ApiWsPort);
}

TArray<FString> UEpicodeSettings::GetResolvedImportRoots() const
{
	TArray<FString> Result;
	for (const FString& Root : ImportRoots)
	{
		Result.Add(FPaths::ConvertRelativePathToFull(ResolveProjectPath(Root)));
	}
	return Result;
}

bool UEpicodeSettings::Validate(FString& OutError) const
{
	if (ApiHost.IsEmpty())
	{
		OutError = TEXT("ApiHost is empty.");
		return false;
	}
	if (ApiHttpPort <= 0 || ApiHttpPort > 65535 || ApiWsPort <= 0 || ApiWsPort > 65535)
	{
		OutError = TEXT("API ports must be in the range 1..65535.");
		return false;
	}
	if (ApiHttpPort == ApiWsPort)
	{
		OutError = TEXT("HTTP and WebSocket ports must differ.");
		return false;
	}
	const bool bLoopback = ApiHost.Equals(TEXT("127.0.0.1")) || ApiHost.Equals(TEXT("localhost"), ESearchCase::IgnoreCase) || ApiHost.Equals(TEXT("::1"));
	if (!bLoopback && !bAllowRemote)
	{
		OutError = TEXT("Non-loopback API binding requires bAllowRemote=true.");
		return false;
	}
	if (bRequireAuth)
	{
		FString TokenError;
		if (!EnsureApiToken(TokenError))
		{
			OutError = TokenError;
			return false;
		}
	}
	return true;
}

FString UEpicodeSettings::GetApiToken() const
{
	const FString EnvToken = ReadEnv(TEXT("EPICODE_API_TOKEN"));
	if (!EnvToken.IsEmpty())
	{
		return EnvToken;
	}

	const FString TokenPath = GetApiTokenFilePath();
	FString Token;
	if (FFileHelper::LoadFileToString(Token, *TokenPath))
	{
		Token.TrimStartAndEndInline();
		if (!Token.IsEmpty())
		{
			return Token;
		}
	}

	Token = GenerateToken();
	if (FFileHelper::SaveStringToFile(Token, *TokenPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
#if PLATFORM_LINUX
		chmod(TCHAR_TO_UTF8(*TokenPath), S_IRUSR | S_IWUSR);
#endif
	}
	return Token;
}

bool UEpicodeSettings::EnsureApiToken(FString& OutError) const
{
	if (!bRequireAuth)
	{
		return true;
	}
	const FString EnvToken = ReadEnv(TEXT("EPICODE_API_TOKEN"));
	if (!EnvToken.IsEmpty())
	{
		return true;
	}
	const FString TokenPath = GetApiTokenFilePath();
	FString Existing;
	if (FFileHelper::LoadFileToString(Existing, *TokenPath) && !Existing.TrimStartAndEnd().IsEmpty())
	{
		return true;
	}
	const FString Directory = FPaths::GetPath(TokenPath);
	if (!IFileManager::Get().MakeDirectory(*Directory, true))
	{
		OutError = FString::Printf(TEXT("Unable to create token directory: %s"), *Directory);
		return false;
	}
	const FString Token = GenerateToken();
	if (!FFileHelper::SaveStringToFile(Token, *TokenPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("Unable to write API token file: %s"), *TokenPath);
		return false;
	}
#if PLATFORM_LINUX
	chmod(TCHAR_TO_UTF8(*TokenPath), S_IRUSR | S_IWUSR);
#endif
	return true;
}

FString UEpicodeSettings::GenerateToken(bool bUseSystemRandom)
{
	TArray<uint8> Bytes;
	Bytes.SetNumZeroed(32);
	bool bFilled = false;
	(void)bUseSystemRandom;
#if PLATFORM_LINUX
	if (bUseSystemRandom)
	{
		if (IFileHandle* Handle = FPlatformFileManager::Get().GetPlatformFile().OpenRead(TEXT("/dev/urandom")))
		{
			bFilled = Handle->Read(Bytes.GetData(), Bytes.Num());
			delete Handle;
		}
	}
#endif
	if (!bFilled)
	{
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const FGuid Guid = FGuid::NewGuid();
			const uint32 Values[4] = { Guid.A, Guid.B, Guid.C, Guid.D };
			FMemory::Memcpy(Bytes.GetData() + Index * sizeof(Values), Values, sizeof(Values));
		}
	}
	return FBase64::Encode(Bytes);
}
