#pragma once

#include "CoreMinimal.h"

struct EPICODECONTROLAPI_API FEpicodeAgentAuditEntry
{
	FString Method;
	FString RequestId;
	FString ParticipantId;
	FString Source;
	FString ErrorCode;
	int32 HttpStatus = 200;
	bool bOk = true;
	bool bIdempotentReplay = false;
	double StateVersion = -1.0;
	uint64 EventSequence = 0;
};

/**
 * Minimal, redacted audit trail for Agent commands. Only routing metadata and
 * outcome fields are serialized; tokens, idempotency keys and request params
 * never reach the log.
 */
class EPICODECONTROLAPI_API FEpicodeAgentAuditLog
{
public:
	static FEpicodeAgentAuditLog& Get();

	void Record(const FEpicodeAgentAuditEntry& Entry);
	FString GetLogPath() const;
	void SetLogPathForTests(const FString& InPath);

	static FString BuildLine(const FEpicodeAgentAuditEntry& Entry);
	static bool AppendRotating(const FString& Path, int32 MaxBytes, const FString& Line);

private:
	FString LogPath;
	mutable FCriticalSection Lock;
};
