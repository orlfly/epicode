#pragma once

#include "CoreMinimal.h"
#include "EpicodeSandboxTypes.h"

struct EPICODESANDBOX_API FEpicodeSandboxWorld
{
	FString WorldId;
	FString SessionId;
	FString ProductionId;
	FString SceneId;
	int64 Tick = 0;
	int32 Seed = 0;
	TMap<FString, FEpicodeSandboxEntity> Entities;
	TMap<FString, FEpicodeSandboxParticipant> Participants;
	FEpicodeSandboxNarrativeState Narrative;
	TSharedPtr<FJsonObject> UnknownFields;

	static FEpicodeSandboxWorld Create(const FString& InSessionId, const FString& InProductionId, const FString& InSceneId, int32 InSeed);

	bool AddEntity(const FEpicodeSandboxEntity& InEntity, FString& OutError);
	bool RemoveEntity(const FString& InEntityId, FString& OutError);
	FEpicodeSandboxEntity* FindEntity(const FString& InEntityId);
	const FEpicodeSandboxEntity* FindEntity(const FString& InEntityId) const;
	bool AddParticipant(const FEpicodeSandboxParticipant& InParticipant, FString& OutError);
	FEpicodeSandboxParticipant* FindParticipant(const FString& InParticipantId);
	const FEpicodeSandboxParticipant* FindParticipant(const FString& InParticipantId) const;
	TArray<FString> GetSortedEntityIds() const;
	TArray<FString> GetSortedParticipantIds() const;
	bool Validate(FString& OutError) const;

	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeSandboxWorld& OutWorld, FString& OutError);
	FString ToCanonicalString() const;
};

class EPICODESANDBOX_API FEpicodeSandboxEventStream
{
public:
	void Reset(int64 InStartSequence = 1);
	FEpicodeSandboxEvent& Append(int64 InTick, const FString& InType, const FString& InActorId, const TSharedPtr<FJsonObject>& InPayload = nullptr, const FString& InTargetId = FString());
	void AppendExisting(const FEpicodeSandboxEvent& InEvent, FString& OutError);
	const TArray<FEpicodeSandboxEvent>& GetEvents() const { return Events; }
	int64 GetLastSequence() const { return NextSequence - 1; }
	int64 GetNextSequence() const { return NextSequence; }
	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeSandboxEventStream& OutStream, FString& OutError);

private:
	TArray<FEpicodeSandboxEvent> Events;
	int64 NextSequence = 1;
};

class EPICODESANDBOX_API FEpicodeSandboxDigest
{
public:
	static FString ComputeWorldDigest(const FEpicodeSandboxWorld& InWorld);
	static FString ComputeEventDigest(const TArray<FEpicodeSandboxEvent>& InEvents);
	static FString ComputeCombinedDigest(const FEpicodeSandboxWorld& InWorld, const TArray<FEpicodeSandboxEvent>& InEvents);
};
