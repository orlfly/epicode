#include "EpicodeEventBus.h"

TSharedPtr<FJsonObject> FEpicodeEvent::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("sequence"), static_cast<double>(Sequence));
	Result->SetStringField(TEXT("type"), Type);
	Result->SetStringField(TEXT("sceneId"), SceneId);
	if (!EntityId.IsEmpty())
	{
		Result->SetStringField(TEXT("entityId"), EntityId);
	}
	Result->SetStringField(TEXT("timestamp"), Timestamp.ToIso8601());
	Result->SetObjectField(TEXT("payload"), Payload.IsValid() ? Payload : MakeShared<FJsonObject>());
	return Result;
}

FEpicodeEventBus::FEpicodeEventBus() = default;

uint64 FEpicodeEventBus::Publish(const FString& InType, const FString& InSceneId, const FString& InEntityId, const TSharedPtr<FJsonObject>& InPayload)
{
	FScopeLock ScopeLock(&Lock);
	FEpicodeEvent Event;
	Event.Sequence = NextSequence++;
	Event.Type = InType;
	Event.SceneId = InSceneId;
	Event.EntityId = InEntityId;
	Event.Timestamp = FDateTime::UtcNow();
	Event.Payload = InPayload.IsValid() ? InPayload : MakeShared<FJsonObject>();
	History.Add(Event);
	if (History.Num() > MaxHistory)
	{
		History.RemoveAt(0, History.Num() - MaxHistory, EAllowShrinking::No);
	}
	return Event.Sequence;
}

TArray<FEpicodeEvent> FEpicodeEventBus::GetSince(uint64 InSequence, int32 MaxCount) const
{
	FScopeLock ScopeLock(&Lock);
	TArray<FEpicodeEvent> Result;
	for (const FEpicodeEvent& Event : History)
	{
		if (Event.Sequence > InSequence)
		{
			Result.Add(Event);
			if (MaxCount > 0 && Result.Num() >= MaxCount)
			{
				break;
			}
		}
	}
	return Result;
}

uint64 FEpicodeEventBus::GetLatestSequence() const
{
	FScopeLock ScopeLock(&Lock);
	return NextSequence > 0 ? NextSequence - 1 : 0;
}

uint64 FEpicodeEventBus::GetOldestSequence() const
{
	FScopeLock ScopeLock(&Lock);
	return History.Num() > 0 ? History[0].Sequence : NextSequence;
}

int32 FEpicodeEventBus::GetHistorySize() const
{
	FScopeLock ScopeLock(&Lock);
	return History.Num();
}

void FEpicodeEventBus::Reset()
{
	FScopeLock ScopeLock(&Lock);
	History.Reset();
	NextSequence = 1;
}
