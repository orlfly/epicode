#include "EpicodeContentDigest.h"

#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"

namespace
{
	FCriticalSection GCachedDigestLock;
	FString GCachedBuildDigest;

	void UpdateString(FSHA1& Hasher, const FString& Value)
	{
		const FTCHARToUTF8 Utf8(*Value);
		Hasher.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		Hasher.Update(reinterpret_cast<const uint8*>("|"), 1);
	}
}

FString FEpicodeContentDigest::HashString(const FString& InValue)
{
	FSHA1 Hasher;
	UpdateString(Hasher, InValue);
	return Hasher.Finalize().ToString();
}

bool FEpicodeContentDigest::ComputeDirectoryDigest(const FString& Root, FString& OutDigest, FString& OutError)
{
	OutDigest.Reset();
	OutError.Reset();
	if (Root.IsEmpty())
	{
		OutError = TEXT("content root is empty");
		return false;
	}
	if (!IFileManager::Get().DirectoryExists(*Root))
	{
		OutError = FString::Printf(TEXT("content root does not exist: %s"), *Root);
		return false;
	}

	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*"), true, false);
	Files.Sort();

	FSHA1 Hasher;
	for (const FString& File : Files)
	{
		const FString Relative = File.RightChop(Root.Len()).TrimStartAndEnd();
		const int64 Size = IFileManager::Get().FileSize(*File);
		if (Size < 0)
		{
			OutError = FString::Printf(TEXT("unable to stat content file: %s"), *File);
			return false;
		}
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *File))
		{
			OutError = FString::Printf(TEXT("unable to read content file: %s"), *File);
			return false;
		}
		UpdateString(Hasher, Relative);
		Hasher.Update(reinterpret_cast<const uint8*>(&Size), sizeof(Size));
		Hasher.Update(Bytes.GetData(), Bytes.Num());
	}
	OutDigest = Hasher.Finalize().ToString();
	return true;
}

bool FEpicodeContentDigest::ComputeBuildDigest(const FString& RuleSchemaVersion, FString& OutDigest, FString& OutError)
{
	OutDigest.Reset();
	OutError.Reset();

	FString ContentDigest;
	if (!ComputeDirectoryDigest(FPaths::ProjectContentDir(), ContentDigest, OutError))
	{
		return false;
	}
	FString ConfigDigest;
	if (!ComputeDirectoryDigest(FPaths::ProjectConfigDir(), ConfigDigest, OutError))
	{
		return false;
	}

	FSHA1 Hasher;
	UpdateString(Hasher, FEngineVersion::Current().ToString(EVersionComponent::Patch));
	UpdateString(Hasher, FApp::GetProjectName());
	UpdateString(Hasher, FString(LexToString(FApp::GetBuildConfiguration())));
	UpdateString(Hasher, RuleSchemaVersion);
	UpdateString(Hasher, ContentDigest);
	UpdateString(Hasher, ConfigDigest);
	OutDigest = Hasher.Finalize().ToString();

	FScopeLock Lock(&GCachedDigestLock);
	GCachedBuildDigest = OutDigest;
	return true;
}

FString FEpicodeContentDigest::GetCachedBuildDigest()
{
	FScopeLock Lock(&GCachedDigestLock);
	return GCachedBuildDigest;
}

void FEpicodeContentDigest::InvalidateCache()
{
	FScopeLock Lock(&GCachedDigestLock);
	GCachedBuildDigest.Reset();
}
