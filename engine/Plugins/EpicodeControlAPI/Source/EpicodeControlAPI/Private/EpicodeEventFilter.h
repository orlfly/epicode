#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "EpicodeEventBus.h"

/**
 * Immutable event subscription filter shared by WebSocket clients. A filter
 * with no fields accepts every event; categories match the event type prefix
 * (for example "character" accepts "character.move.completed").
 */
struct EPICODECONTROLAPI_API FEpicodeEventFilter
{
	FString SessionId;
	FString ParticipantId;
	FString CharacterId;
	TArray<FString> Categories;

	bool IsEmpty() const;
	bool Matches(const FEpicodeEvent& Event) const;
	bool MatchesJson(const TSharedPtr<FJsonObject>& EventJson) const;
	/** True when replaying from Since would skip events still known to the bus. */
	static bool NeedsGap(uint64 Since, uint64 OldestSequence);

	/** Parses filter.session, filter.participant, filter.character and filter.category from a path query. */
	static FEpicodeEventFilter FromQueryString(const FString& Path);
};
