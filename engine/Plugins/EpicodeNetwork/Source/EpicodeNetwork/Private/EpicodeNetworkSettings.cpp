#include "EpicodeNetworkSettings.h"

#include "EpicodeSettings.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if PLATFORM_LINUX
#include <sys/stat.h>
#endif

namespace
{
	FString ReadViewerEnvToken()
	{
		return FPlatformMisc::GetEnvironmentVariable(TEXT("EPICODE_VIEWER_TOKEN")).TrimStartAndEnd();
	}
}

UEpicodeNetworkSettings::UEpicodeNetworkSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("Epicode Network");
}

FString UEpicodeNetworkSettings::GetSessionRootPath() const
{
	if (SessionRoot.IsEmpty())
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	}
	if (FPaths::IsRelative(SessionRoot))
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), SessionRoot));
	}
	return FPaths::ConvertRelativePathToFull(SessionRoot);
}

FString UEpicodeNetworkSettings::GetViewerTokenFilePath() const
{
	if (ViewerTokenFile.IsEmpty())
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Epicode/viewer-token"));
	}
	if (FPaths::IsRelative(ViewerTokenFile))
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), ViewerTokenFile));
	}
	return FPaths::ConvertRelativePathToFull(ViewerTokenFile);
}

FString UEpicodeNetworkSettings::GetViewerToken() const
{
	const FString EnvToken = ReadViewerEnvToken();
	if (!EnvToken.IsEmpty())
	{
		return EnvToken;
	}
	const FString TokenPath = GetViewerTokenFilePath();
	FString Token;
	if (FFileHelper::LoadFileToString(Token, *TokenPath))
	{
		Token.TrimStartAndEndInline();
		if (!Token.IsEmpty())
		{
			return Token;
		}
	}
	Token = UEpicodeSettings::GenerateToken();
	const FString Directory = FPaths::GetPath(TokenPath);
	IFileManager::Get().MakeDirectory(*Directory, true);
	if (FFileHelper::SaveStringToFile(Token, *TokenPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
#if PLATFORM_LINUX
		chmod(TCHAR_TO_UTF8(*TokenPath), S_IRUSR | S_IWUSR);
#endif
	}
	return Token;
}

bool UEpicodeNetworkSettings::EnsureViewerToken(FString& OutError) const
{
	if (!ReadViewerEnvToken().IsEmpty())
	{
		return true;
	}
	const FString TokenPath = GetViewerTokenFilePath();
	FString Existing;
	if (FFileHelper::LoadFileToString(Existing, *TokenPath) && !Existing.TrimStartAndEnd().IsEmpty())
	{
		return true;
	}
	const FString Directory = FPaths::GetPath(TokenPath);
	if (!IFileManager::Get().MakeDirectory(*Directory, true))
	{
		OutError = FString::Printf(TEXT("Unable to create viewer token directory: %s"), *Directory);
		return false;
	}
	const FString Token = UEpicodeSettings::GenerateToken();
	if (!FFileHelper::SaveStringToFile(Token, *TokenPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("Unable to write viewer token file: %s"), *TokenPath);
		return false;
	}
#if PLATFORM_LINUX
	chmod(TCHAR_TO_UTF8(*TokenPath), S_IRUSR | S_IWUSR);
#endif
	return true;
}
