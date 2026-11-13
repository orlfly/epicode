#pragma once

#include "CoreMinimal.h"
#include "EpicodeSandboxTypes.h"

struct FEpicodeSandboxWorld;

enum class EEpicodeSandboxCompareOp : uint8
{
	Equal,
	NotEqual,
	Less,
	LessEqual,
	Greater,
	GreaterEqual
};

struct EPICODESANDBOX_API FEpicodeSandboxCondition
{
	bool bAlwaysTrue = false;
	bool bAlwaysFalse = false;
	bool bNegate = false;
	FString Variable;
	EEpicodeSandboxCompareOp Op = EEpicodeSandboxCompareOp::Equal;
	FEpicodeSandboxValue Value;

	static bool Parse(const FString& InExpression, FEpicodeSandboxCondition& OutCondition, FString& OutError);
	bool Evaluate(const FEpicodeSandboxNarrativeState& InState) const;
	FString ToExpression() const;
};

struct EPICODESANDBOX_API FEpicodeSandboxOutcome
{
	FString Action;
	FString Key;
	FEpicodeSandboxValue Value;

	static bool Parse(const FString& InExpression, FEpicodeSandboxOutcome& OutOutcome, FString& OutError);
};

struct EPICODESANDBOX_API FEpicodeSandboxInteractionPoint
{
	FString Id;
	FString EntityId;
	TArray<FString> Verbs;
	double MaxDistance = 150.0;
	double CooldownSeconds = 0.0;
	TArray<FEpicodeSandboxCondition> Conditions;
	TArray<FEpicodeSandboxOutcome> Outcomes;
};

struct EPICODESANDBOX_API FEpicodeSandboxZone
{
	FString Id;
	FString Name;
	int32 MaxEntities = 0;
	bool bHasBounds = false;
	FVector Center = FVector::ZeroVector;
	FVector Extent = FVector(500, 500, 200);
};

struct EPICODESANDBOX_API FEpicodeSandboxSpawnEntry
{
	EEpicodeSandboxEntityKind Kind = EEpicodeSandboxEntityKind::Prop;
	FString ArchetypeId;
	double Weight = 1.0;
};

struct EPICODESANDBOX_API FEpicodeSandboxSpawnTable
{
	FString Id;
	TArray<FEpicodeSandboxSpawnEntry> Entries;
};

struct EPICODESANDBOX_API FEpicodeSandboxSpawner
{
	FString Id;
	FString ZoneId;
	FString SpawnTableId;
	double IntervalSeconds = 10.0;
	int32 MaxAlive = 4;
};

struct EPICODESANDBOX_API FEpicodeSandboxResourceRule
{
	FString Id;
	FString EntityId;
	FString ItemId;
	int32 Capacity = 1;
	double RespawnSeconds = 30.0;
};

struct EPICODESANDBOX_API FEpicodeSandboxNpcProfile
{
	FString Id;
	FString Behavior = TEXT("idle");
	double PerceptionRadius = 500.0;
	double Speed = 100.0;
	FString TargetTag;
	TArray<FVector> PatrolPoints;
};

struct EPICODESANDBOX_API FEpicodeSandboxRecipe
{
	FString Id;
	TMap<FString, int32> Inputs;
	TMap<FString, int32> Outputs;
	double DurationSeconds = 0.0;
};

struct EPICODESANDBOX_API FEpicodeSandboxBudget
{
	int32 MaxEntities = 1000;
	int32 MaxEventsPerSecond = 1000;
	int32 MaxSpawnPerSecond = 50;
	int32 MaxDirectorCues = 1000;
};

struct EPICODESANDBOX_API FEpicodeSandboxRules
{
	TArray<FEpicodeSandboxInteractionPoint> Interactions;
	TArray<FEpicodeSandboxZone> Zones;
	TArray<FEpicodeSandboxSpawner> Spawners;
	TArray<FEpicodeSandboxSpawnTable> SpawnTables;
	TArray<FEpicodeSandboxResourceRule> Resources;
	TArray<FEpicodeSandboxNpcProfile> NpcProfiles;
	TArray<FEpicodeSandboxRecipe> Recipes;
	FEpicodeSandboxBudget Budget;

	static bool Parse(const TSharedPtr<FJsonObject>& InWorldRules, FEpicodeSandboxRules& OutRules, FString& OutError);
	bool Validate(const FEpicodeSandboxWorld* InWorld, FString& OutError) const;

	const FEpicodeSandboxInteractionPoint* FindInteractionById(const FString& InId) const;
	const FEpicodeSandboxInteractionPoint* FindInteractionByEntity(const FString& InEntityId) const;
	const FEpicodeSandboxZone* FindZone(const FString& InId) const;
	const FEpicodeSandboxSpawnTable* FindSpawnTable(const FString& InId) const;
	const FEpicodeSandboxSpawnEntry* PickSpawnEntry(const FEpicodeSandboxSpawnTable& InTable, double InRoll) const;
	const FEpicodeSandboxResourceRule* FindResourceByEntity(const FString& InEntityId) const;
	const FEpicodeSandboxNpcProfile* FindNpcProfile(const FString& InId) const;
	const FEpicodeSandboxRecipe* FindRecipe(const FString& InId) const;
};
