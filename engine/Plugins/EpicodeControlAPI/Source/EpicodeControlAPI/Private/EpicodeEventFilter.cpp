#include "EpicodeEventFilter.h"

namespace
{
	FString GetQueryValue(const FString& Path, const TCHAR* Key)
	{
		const int32 Question = Path.Find(TEXT("?"));
		if (Question == INDEX_NONE)
		{
			return FString();
		}
		TArray<FString> Pairs;
		Path.Mid(Question + 1).ParseIntoArray(Pairs, TEXT("&"), true);
		for (const FString& Pair : Pairs)
		{
			FString Name;
			FString Value;
			if (!Pair.Split(TEXT("="), &Name, &Value))
			{
				continue;
			}
			if (Name.Equals(Key, ESearchCase::IgnoreCase))
			{
				return Value;
			}
		}
		return FString();
	}

	bool PayloadMatches(const TSharedPtr<FJsonObject>& Payload, const TCHAR* Field, const FString& Expected)
	{
		if (!Payload.IsValid() || Expected.IsEmpty())
		{
			return false;
		}
		FString Value;
		return Payload->TryGetStringField(Field, Value) && Value == Expected;
	}
}

bool FEpicodeEventFilter::IsEmpty() const
{
	return SessionId.IsEmpty() && ParticipantId.IsEmpty() && CharacterId.IsEmpty() && Categories.Num() == 0;
}

bool FEpicodeEventFilter::Matches(const FEpicodeEvent& Event) const
{
	if (IsEmpty())
	{
		return true;
	}
	if (!SessionId.IsEmpty())
	{
		FString EventSessionId = Event.SceneId;
		if (Event.Payload.IsValid())
		{
			Event.Payload->TryGetStringField(TEXT("sessionId"), EventSessionId);
		}
		if (EventSessionId != SessionId)
		{
			return false;
		}
	}
	if (!CharacterId.IsEmpty())
	{
		if (Event.EntityId != CharacterId
			&& !PayloadMatches(Event.Payload, TEXT("characterId"), CharacterId)
			&& !PayloadMatches(Event.Payload, TEXT("entityId"), CharacterId))
		{
			return false;
		}
	}
	if (!ParticipantId.IsEmpty())
	{
		if (!PayloadMatches(Event.Payload, TEXT("playerId"), ParticipantId)
			&& !PayloadMatches(Event.Payload, TEXT("requesterId"), ParticipantId)
			&& !PayloadMatches(Event.Payload, TEXT("agentPlayerId"), ParticipantId)
			&& !PayloadMatches(Event.Payload, TEXT("targetPlayerId"), ParticipantId)
			&& !PayloadMatches(Event.Payload, TEXT("ownerPlayerId"), ParticipantId))
		{
			return false;
		}
	}
	if (Categories.Num() > 0)
	{
		bool bMatched = false;
		for (const FString& Category : Categories)
		{
			if (Category == TEXT("*") || Event.Type == Category || Event.Type.StartsWith(Category + TEXT(".")))
			{
				bMatched = true;
				break;
			}
		}
		if (!bMatched)
		{
			return false;
		}
	}
	return true;
}

bool FEpicodeEventFilter::MatchesJson(const TSharedPtr<FJsonObject>& EventJson) const
{
	if (!EventJson.IsValid())
	{
		return false;
	}
	FEpicodeEvent Event;
	EventJson->TryGetStringField(TEXT("type"), Event.Type);
	EventJson->TryGetStringField(TEXT("sceneId"), Event.SceneId);
	EventJson->TryGetStringField(TEXT("entityId"), Event.EntityId);
	const TSharedPtr<FJsonObject>* Payload = nullptr;
	if (EventJson->TryGetObjectField(TEXT("payload"), Payload) && Payload)
	{
		Event.Payload = *Payload;
	}
	return Matches(Event);
}

bool FEpicodeEventFilter::NeedsGap(uint64 Since, uint64 OldestSequence)
{
	return Since > 0 && OldestSequence > 0 && Since + 1 < OldestSequence;
}

FEpicodeEventFilter FEpicodeEventFilter::FromQueryString(const FString& Path)
{
	FEpicodeEventFilter Filter;
	Filter.SessionId = GetQueryValue(Path, TEXT("filter.session"));
	if (Filter.SessionId.IsEmpty())
	{
		Filter.SessionId = GetQueryValue(Path, TEXT("sessionId"));
	}
	Filter.ParticipantId = GetQueryValue(Path, TEXT("filter.participant"));
	if (Filter.ParticipantId.IsEmpty())
	{
		Filter.ParticipantId = GetQueryValue(Path, TEXT("participantId"));
	}
	Filter.CharacterId = GetQueryValue(Path, TEXT("filter.character"));
	if (Filter.CharacterId.IsEmpty())
	{
		Filter.CharacterId = GetQueryValue(Path, TEXT("characterId"));
	}
	FString Categories = GetQueryValue(Path, TEXT("filter.category"));
	if (Categories.IsEmpty())
	{
		Categories = GetQueryValue(Path, TEXT("categories"));
	}
	Categories.ParseIntoArray(Filter.Categories, TEXT(","), true);
	for (FString& Category : Filter.Categories)
	{
		Category.TrimStartAndEndInline();
	}
	Filter.Categories.RemoveAll([](const FString& Category) { return Category.IsEmpty(); });
	return Filter;
}
