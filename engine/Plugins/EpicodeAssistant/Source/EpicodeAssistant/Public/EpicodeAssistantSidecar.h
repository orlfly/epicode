#pragma once

#include "CoreMinimal.h"
#include "EpicodeAssistantTypes.h"

struct FSidecarProcess;

/** Configuration for the isolated DeepSeek Harness Python process. */
struct EPICODEASSISTANT_API FSidecarBackendConfig
{
	FString Command = TEXT("python3");
	FString Script = TEXT("Scripts/Assistant/DeepSeekHarnessSidecar.py");
	FString ProjectDir;
	FString Mode = TEXT("stdio");
	FString Model = TEXT("deepseek-chat");
	FString BaseUrl = TEXT("https://api.deepseek.com");
	FString ApiKeyFile;
	FString DshHome;
	FString VenvDir;
	int32 IdleSeconds = 300;
	float RequestTimeoutSeconds = 120.0f;
};

/**
 * Incremental parser for the sidecar JSONL protocol. It accepts arbitrary byte
 * boundaries so a UTF-8 code point or a JSON line may span pipe reads.
 */
class EPICODEASSISTANT_API FSidecarJsonLineParser
{
public:
	void Append(const void* Data, int64 Length);
	void Append(const FString& Text);
	void AppendLine(const FString& Line);
	void TakeEvents(const FString& RequestId, TArray<FConversationEvent>& OutEvents);
	void Reset();

	bool IsDone() const { return bDone; }
	bool HasOutput() const { return bHasOutput; }
	bool HasProtocolError() const { return bProtocolError; }

private:
	void ProcessLine(const FString& Line);
	void AppendEvent(FConversationEvent Event);

	TArray<uint8> PendingBytes;
	TArray<FConversationEvent> Events;
	bool bDone = false;
	bool bHasOutput = false;
	bool bProtocolError = false;
};

/**
 * DeepSeek Harness sidecar transport. The Python process owns the SDK and its
 * runtime; this class only manages pipes, request routing, cancellation and
 * crash recovery.
 */
class EPICODEASSISTANT_API FDeepSeekHarnessSidecarBackend final : public IConversationBackend
{
public:
	explicit FDeepSeekHarnessSidecarBackend(FSidecarBackendConfig InConfig);
	virtual ~FDeepSeekHarnessSidecarBackend() override;

	virtual FString GetName() const override { return TEXT("deepseek_sidecar"); }
	virtual bool IsAvailable(FString& OutReason) const override;
	virtual TSharedPtr<IConversationStream> Start(
		const FConversationRequest& Request,
		FString& OutError) override;

	const FSidecarBackendConfig& GetConfig() const { return Config; }

private:
	FSidecarBackendConfig Config;
	TSharedPtr<FSidecarProcess> Process;
	TWeakPtr<IConversationStream> ActiveStream;
};
