#pragma once

#include "CoreMinimal.h"
#include "EpicodeAssistantTypes.h"

/** JSON-backed per-conversation history under Saved/Epicode/Chat. */
class EPICODEASSISTANT_API FConversationHistoryStore
{
public:
	/** Absolute directory used by the editor. */
	static FString GetHistoryRoot();

	/** Creates a unique, filesystem-safe conversation id. */
	static FString NewConversationId();

	/** Restricts an id to characters safe for a single filename. */
	static FString SanitizeId(const FString& Id);

	/** Persists user/assistant messages atomically. System lines are not persisted. */
	static bool Save(
		const FString& ConversationId,
		const TArray<FConversationMessage>& Messages,
		FString& OutError);

	/** Loads a conversation. Missing or malformed files return false. */
	static bool Load(
		const FString& ConversationId,
		TArray<FConversationMessage>& OutMessages,
		FString& OutError);

	/** Returns conversation ids, newest first. */
	static TArray<FString> ListIds(FString& OutError);

	static bool Delete(const FString& ConversationId, FString& OutError);

	/** Test seam: overrides the root for the lifetime of this object. */
	struct FScopedRootOverride
	{
		explicit FScopedRootOverride(const FString& InRoot);
		~FScopedRootOverride();

	private:
		FString PreviousRoot;
	};
};
