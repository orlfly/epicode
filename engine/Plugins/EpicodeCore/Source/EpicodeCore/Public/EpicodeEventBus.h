#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct EPICODECORE_API FEpicodeEvent
{
	uint64 Sequence = 0;
	FString Type;
	FString SceneId;
	FString EntityId;
	FDateTime Timestamp;
	TSharedPtr<FJsonObject> Payload;

	TSharedPtr<FJsonObject> ToJson() const;
};

class EPICODECORE_API FEpicodeEventBus
{
public:
	FEpicodeEventBus();

	uint64 Publish(const FString& InType, const FString& InSceneId, const FString& InEntityId, const TSharedPtr<FJsonObject>& InPayload = nullptr);
	TArray<FEpicodeEvent> GetSince(uint64 InSequence, int32 MaxCount = 512) const;
	uint64 GetLatestSequence() const;
	uint64 GetOldestSequence() const;
	int32 GetHistorySize() const;
	void Reset();

	static constexpr int32 MaxHistory = 4096;

private:
	mutable FCriticalSection Lock;
	TArray<FEpicodeEvent> History;
	uint64 NextSequence = 1;
};
