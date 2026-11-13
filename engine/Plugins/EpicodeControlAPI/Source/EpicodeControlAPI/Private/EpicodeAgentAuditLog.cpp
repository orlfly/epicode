#include "EpicodeAgentAuditLog.h"

#include "EpicodeNetworkSettings.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FEpicodeAgentAuditLog& FEpicodeAgentAuditLog::Get()
{
	static FEpicodeAgentAuditLog Instance;
	return Instance;
}

FString FEpicodeAgentAuditLog::GetLogPath() const
{
	FScopeLock ScopeLock(&Lock);
	return LogPath.IsEmpty() ? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Logs"), TEXT("agent-audit.log")) : LogPath;
}

void FEpicodeAgentAuditLog::SetLogPathForTests(const FString& InPath)
{
	FScopeLock ScopeLock(&Lock);
	LogPath = InPath;
}

FString FEpicodeAgentAuditLog::BuildLine(const FEpicodeAgentAuditEntry& Entry)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("timestamp"), FDateTime::UtcNow().ToIso8601());
	Json->SetStringField(TEXT("method"), Entry.Method);
	Json->SetStringField(TEXT("requestId"), Entry.RequestId);
	Json->SetStringField(TEXT("participantId"), Entry.ParticipantId);
	Json->SetStringField(TEXT("source"), Entry.Source);
	Json->SetBoolField(TEXT("ok"), Entry.bOk);
	Json->SetNumberField(TEXT("httpStatus"), Entry.HttpStatus);
	Json->SetBoolField(TEXT("idempotentReplay"), Entry.bIdempotentReplay);
	if (!Entry.ErrorCode.IsEmpty())
	{
		Json->SetStringField(TEXT("errorCode"), Entry.ErrorCode);
	}
	if (Entry.StateVersion >= 0.0)
	{
		Json->SetNumberField(TEXT("stateVersion"), Entry.StateVersion);
	}
	if (Entry.EventSequence > 0)
	{
		Json->SetNumberField(TEXT("eventSequence"), static_cast<double>(Entry.EventSequence));
	}
	FString Text;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
	FJsonSerializer::Serialize(Json.ToSharedRef(), Writer);
	return Text;
}

bool FEpicodeAgentAuditLog::AppendRotating(const FString& Path, int32 MaxBytes, const FString& Line)
{
	const FString Directory = FPaths::GetPath(Path);
	if (!Directory.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*Directory, true);
	}
	const int64 ExistingSize = IFileManager::Get().FileSize(*Path);
	if (ExistingSize >= 0 && ExistingSize + Line.Len() > FMath::Max(1024, MaxBytes))
	{
		const FString Rotated = Path + TEXT(".1");
		IFileManager::Get().Delete(*Rotated, false, true, true);
		IFileManager::Get().Move(*Rotated, *Path, true, true);
	}
	return FFileHelper::SaveStringToFile(
		Line + LINE_TERMINATOR,
		*Path,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(),
		FILEWRITE_Append);
}

void FEpicodeAgentAuditLog::Record(const FEpicodeAgentAuditEntry& Entry)
{
	const FString Path = GetLogPath();
	const FString Line = BuildLine(Entry);
	const int32 MaxBytes = UEpicodeNetworkSettings::Get()->AgentAuditLogMaxBytes;
	FScopeLock ScopeLock(&Lock);
	AppendRotating(Path, MaxBytes, Line);
}
