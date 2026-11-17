#pragma once

#include "CoreMinimal.h"
#include "EpicodeAssistantTypes.h"

/**
 * Incremental parser for OpenAI-compatible SSE frames. It accepts arbitrary
 * byte boundaries so a UTF-8 code point or an SSE line may span HTTP chunks.
 */
class EPICODEASSISTANT_API FOpenAICompatibleSseParser
{
public:
	void Append(const void* Data, int64 Length, const FString& RequestId);
	void Append(const FString& Text, const FString& RequestId);
	void TakeEvents(TArray<FConversationEvent>& OutEvents);
	void Reset();

	bool IsDone() const { return bDone; }
	bool HasOutput() const { return bHasOutput; }

private:
	void ProcessLine(const FString& Line, const FString& RequestId);

	TArray<uint8> PendingBytes;
	TArray<FConversationEvent> Events;
	bool bDone = false;
	bool bHasOutput = false;
};

/** DeepSeek/OpenAI-compatible HTTP fallback. API key is held in memory only. */
class EPICODEASSISTANT_API FOpenAICompatibleChatBackend final : public IConversationBackend
{
public:
	FOpenAICompatibleChatBackend(
		FString InEndpoint,
		FString InModel,
		FString InApiKey,
		float InTimeoutSeconds = 120.0f);

	virtual ~FOpenAICompatibleChatBackend() override;

	virtual FString GetName() const override { return TEXT("openai_compat"); }
	virtual bool IsAvailable(FString& OutReason) const override;
	virtual TSharedPtr<IConversationStream> Start(
		const FConversationRequest& Request,
		FString& OutError) override;

	static FString MakeChatUrl(const FString& Endpoint);

private:
	FString Endpoint;
	FString Model;
	FString ApiKey;
	float TimeoutSeconds = 120.0f;
};
