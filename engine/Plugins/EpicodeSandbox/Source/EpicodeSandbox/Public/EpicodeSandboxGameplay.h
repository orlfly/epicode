#pragma once

#include "CoreMinimal.h"
#include "EpicodeSandboxRandom.h"
#include "EpicodeSandboxRules.h"
#include "EpicodeSandboxWorld.h"

struct EPICODESANDBOX_API FEpicodeSandboxResourceRuntime
{
	FString RuleId;
	int32 Remaining = 0;
	int64 NextRespawnTick = 0;
};

struct EPICODESANDBOX_API FEpicodeSandboxSpawnerRuntime
{
	int64 NextSpawnTick = 0;
	int32 SpawnedCount = 0;
};

class EPICODESANDBOX_API FEpicodeSandboxGameplay
{
public:
	bool Initialize(const FEpicodeSandboxRules& InRules, FEpicodeSandboxWorld& InWorld, FEpicodeSandboxRandomStreams& InRandom, FEpicodeSandboxEventStream& InEvents, int32 InTickRate, FString& OutError);

	bool ExecuteInteraction(const FString& InParticipantId, const FString& InEntityId, const FString& InVerb, FString& OutError);
	int32 Tick(int64 InTick, double InDeltaSeconds, FString& OutError);
	int32 TickSpawners(int64 InTick, FString& OutError);
	int32 TickNpcs(int64 InTick, double InDeltaSeconds, FString& OutError);
	int32 TickResources(int64 InTick, FString& OutError);
	bool Collect(const FString& InParticipantId, const FString& InNodeEntityId, int32 InAmount, FString& OutError);
	bool ConsumeItem(const FString& InParticipantId, const FString& InItemId, int32 InAmount, FString& OutError);
	bool Craft(const FString& InParticipantId, const FString& InRecipeId, FString& OutError);

	static bool CanPerform(EEpicodeSandboxParticipantKind InKind, const FString& InAction);
	bool CheckPermission(const FString& InParticipantId, const FString& InAction, FString& OutError) const;
	bool CanSpawn(int32 InAdditional, FString& OutError) const;
	TSharedPtr<FJsonObject> GetBudgetReport() const;

	const FEpicodeSandboxRules& GetRules() const { return Rules; }
	const TMap<FString, FEpicodeSandboxResourceRuntime>& GetResourceStates() const { return ResourceStates; }
	const TMap<FString, FEpicodeSandboxSpawnerRuntime>& GetSpawnerStates() const { return SpawnerStates; }

private:
	FEpicodeSandboxEntity* GetParticipantEntity(const FString& InParticipantId, FString& OutError);
	const FEpicodeSandboxEntity* GetParticipantEntity(const FString& InParticipantId, FString& OutError) const;
	bool AppendEvent(int64 InTick, const FString& InType, const FString& InActorId, const TSharedPtr<FJsonObject>& InPayload, FString& OutError);
	bool ApplyOutcome(const FEpicodeSandboxOutcome& InOutcome, FEpicodeSandboxEntity& InActor, TArray<TSharedPtr<FJsonValue>>& OutChanges, FString& OutError);
	int32 CountSpawnerEntities(const FString& InSpawnerId) const;
	int32 CountZoneEntities(const FString& InZoneId) const;
	const FEpicodeSandboxNpcProfile& ResolveNpcProfile(const FEpicodeSandboxEntity& InEntity) const;
	bool MoveEntityToward(FEpicodeSandboxEntity& InEntity, const FVector& InTarget, double InSpeed, double InDeltaSeconds);

	FEpicodeSandboxRules Rules;
	FEpicodeSandboxWorld* World = nullptr;
	FEpicodeSandboxRandomStreams* Random = nullptr;
	FEpicodeSandboxEventStream* Events = nullptr;
	int32 TickRate = 30;
	int64 LastBudgetTick = -1;
	int32 EventsThisTick = 0;
	TMap<FString, FEpicodeSandboxResourceRuntime> ResourceStates;
	TMap<FString, FEpicodeSandboxSpawnerRuntime> SpawnerStates;
	TMap<FString, int64> InteractionCooldowns;
	FEpicodeSandboxNpcProfile FallbackNpcProfile;
};
