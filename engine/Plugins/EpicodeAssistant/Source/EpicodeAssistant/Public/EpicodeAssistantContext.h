#pragma once

#include "CoreMinimal.h"
#include "EpicodeAssistantTypes.h"

/** Everything the assistant is allowed to know about the current editor state. */
struct EPICODEASSISTANT_API FConversationContextInput
{
	FString SceneName;
	FString SceneId;
	int32 EntityCount = 0;
	FString SelectionName;
	FString SelectionType;
	FString LibrarySummary;
	FString CommandSummary;
	TArray<FConversationMessage> History;
};

/** Builds a deterministic, character-budgeted context block. */
class EPICODEASSISTANT_API FConversationContextBuilder
{
public:
	/** Rough token estimate used by the budget (UTF-8 agnostic). */
	static int32 EstimateTokens(const FString& Text);

	/** Keeps editor facts first and trims oldest history to fit MaxChars. */
	static FString Build(const FConversationContextInput& Input, int32 MaxChars);
};
