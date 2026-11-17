#include "EpicodeAssistantSecrets.h"

#include "EpicodeSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if PLATFORM_LINUX || PLATFORM_MAC
#include <sys/stat.h>
#endif

namespace
{
	const TCHAR* KeyVariableName = TEXT("DEEPSEEK_API_KEY");

	FString TrimSecret(FString Value)
	{
		Value.TrimStartAndEndInline();
		if (Value.Len() >= 2)
		{
			const TCHAR First = Value[0];
			const TCHAR Last = Value[Value.Len() - 1];
			if ((First == TEXT('"') && Last == TEXT('"'))
				|| (First == TEXT('\'') && Last == TEXT('\'')))
			{
				Value = Value.Mid(1, Value.Len() - 2);
			}
		}
		return Value.TrimStartAndEnd();
	}

	bool LooksLikeKey(const FString& Value)
	{
		return Value.Len() >= 8 && !Value.Contains(TEXT(" "));
	}
}

FString FConversationSecretStore::GetEnvironmentVariableName()
{
	return KeyVariableName;
}

FString FConversationSecretStore::GetDefaultKeyFilePath()
{
	const UEpicodeSettings* Settings = UEpicodeSettings::Get();
	FString Configured = Settings != nullptr ? Settings->AssistantApiKeyFile : FString();
	if (Configured.IsEmpty())
	{
		Configured = TEXT("Saved/Epicode/deepseek.env");
	}
	return Settings != nullptr
		? Settings->ResolveProjectPath(Configured)
		: FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), Configured));
}

bool FConversationSecretStore::Read(
	FString& OutKey,
	FString& OutSource,
	FString& OutError)
{
	OutKey.Reset();
	OutSource = TEXT("missing");
	OutError.Reset();

	const FString EnvironmentValue = TrimSecret(
		FPlatformMisc::GetEnvironmentVariable(KeyVariableName));
	if (!EnvironmentValue.IsEmpty())
	{
		if (!LooksLikeKey(EnvironmentValue))
		{
			OutError = FString::Printf(
				TEXT("%s 格式无效（至少 8 个字符且不含空格）"),
				KeyVariableName);
			return false;
		}
		OutKey = EnvironmentValue;
		OutSource = TEXT("environment");
		return true;
	}

	const FString Path = GetDefaultKeyFilePath();
	FString Contents;
	if (!FFileHelper::LoadFileToString(Contents, *Path))
	{
		OutError = FString::Printf(
			TEXT("未找到 %s，也未找到 API Key 文件 %s"),
			KeyVariableName,
			*Path);
		return false;
	}

	FString FileKey;
	TArray<FString> Lines;
	Contents.ParseIntoArrayLines(Lines, false);
	for (FString Line : Lines)
	{
		Line.TrimStartAndEndInline();
		if (Line.IsEmpty() || Line.StartsWith(TEXT("#")))
		{
			continue;
		}
		if (Line.StartsWith(TEXT("export ")))
		{
			Line = Line.Mid(7).TrimStartAndEnd();
		}

		int32 EqualsIndex = INDEX_NONE;
		if (Line.FindChar(TEXT('='), EqualsIndex))
		{
			const FString Name = Line.Left(EqualsIndex).TrimStartAndEnd();
			if (Name != KeyVariableName && Name != TEXT("EPICODE_DEEPSEEK_API_KEY"))
			{
				continue;
			}
			FileKey = TrimSecret(Line.Mid(EqualsIndex + 1));
		}
		else
		{
			FileKey = TrimSecret(Line);
		}
		if (!FileKey.IsEmpty())
		{
			break;
		}
	}

	if (!LooksLikeKey(FileKey))
	{
		OutError = FString::Printf(
			TEXT("API Key 文件 %s 中没有有效的 %s"),
			*Path,
			KeyVariableName);
		return false;
	}

	OutKey = FileKey;
	OutSource = TEXT("file");
	return true;
}

FString FConversationSecretStore::Mask(const FString& Secret)
{
	if (Secret.IsEmpty())
	{
		return TEXT("未设置");
	}
	if (Secret.Len() <= 8)
	{
		return TEXT("********");
	}
	return Secret.Left(4) + TEXT("…") + Secret.Right(4);
}

bool FConversationSecretStore::EnsureTemplateFile(FString& OutPath, FString& OutError)
{
	OutPath = GetDefaultKeyFilePath();
	OutError.Reset();

	const FString Directory = FPaths::GetPath(OutPath);
	if (!IFileManager::Get().MakeDirectory(*Directory, true))
	{
		OutError = FString::Printf(TEXT("无法创建目录：%s"), *Directory);
		return false;
	}

	if (!IFileManager::Get().FileExists(*OutPath))
	{
		const FString Template =
			TEXT("# DeepSeek API Key，仅本机使用，不要提交到版本库。\n")
			TEXT("# 也可以直接设置环境变量 DEEPSEEK_API_KEY。\n")
			TEXT("DEEPSEEK_API_KEY=\n");
		if (!FFileHelper::SaveStringToFile(
			Template,
			*OutPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("无法创建 API Key 文件：%s"), *OutPath);
			return false;
		}
	}

#if PLATFORM_LINUX || PLATFORM_MAC
	// The editor runs as the current user; restrict the file to owner read/write.
	if (chmod(TCHAR_TO_UTF8(*OutPath), S_IRUSR | S_IWUSR) != 0)
	{
		OutError = FString::Printf(TEXT("无法设置 API Key 文件权限：%s"), *OutPath);
		return false;
	}
#endif
	return true;
}
