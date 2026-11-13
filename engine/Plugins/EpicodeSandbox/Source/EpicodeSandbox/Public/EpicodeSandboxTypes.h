#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

enum class EEpicodeSandboxEntityKind : uint8
{
	Character,
	Npc,
	Prop,
	Item,
	Build,
	Resource,
	Trigger,
	Light,
	Camera
};

enum class EEpicodeSandboxParticipantKind : uint8
{
	Performer,
	Agent,
	Viewer,
	Director,
	Npc,
	Owner
};

enum class EEpicodeSandboxCommandCategory : uint8
{
	Input,
	Rule,
	Projection,
	System
};

enum class EEpicodeSandboxValueType : uint8
{
	Boolean,
	Integer,
	Number,
	String
};

EPICODESANDBOX_API FString LexToString(EEpicodeSandboxEntityKind InKind);
EPICODESANDBOX_API bool ParseSandboxEntityKind(const FString& InValue, EEpicodeSandboxEntityKind& OutKind);
EPICODESANDBOX_API FString LexToString(EEpicodeSandboxParticipantKind InKind);
EPICODESANDBOX_API bool ParseSandboxParticipantKind(const FString& InValue, EEpicodeSandboxParticipantKind& OutKind);
EPICODESANDBOX_API FString LexToString(EEpicodeSandboxCommandCategory InCategory);
EPICODESANDBOX_API int32 GetSandboxCommandSortOrder(EEpicodeSandboxCommandCategory InCategory);

struct EPICODESANDBOX_API FEpicodeSandboxValue
{
	EEpicodeSandboxValueType Type = EEpicodeSandboxValueType::Integer;
	bool BoolValue = false;
	int64 IntValue = 0;
	double NumberValue = 0.0;
	FString StringValue;

	static FEpicodeSandboxValue MakeBool(bool InValue);
	static FEpicodeSandboxValue MakeInt(int64 InValue);
	static FEpicodeSandboxValue MakeNumber(double InValue);
	static FEpicodeSandboxValue MakeString(const FString& InValue);

	TSharedPtr<FJsonValue> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonValue>& InJson, FEpicodeSandboxValue& OutValue, FString& OutError);
	FString ToCanonicalString() const;
	FString ToDisplayString() const;
	bool AsBool() const;
	int64 AsInt() const;
	double AsNumber() const;
};

struct EPICODESANDBOX_API FEpicodeSandboxEntity
{
	FString Id;
	EEpicodeSandboxEntityKind Kind = EEpicodeSandboxEntityKind::Prop;
	FString ArchetypeId;
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	bool bVisible = true;
	FString OwnerId;
	TMap<FString, int32> Inventory;
	TSharedPtr<FJsonObject> Data;
	TSharedPtr<FJsonObject> UnknownFields;

	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeSandboxEntity& OutEntity, FString& OutError);
	FString ToCanonicalString() const;
	int32 GetItemCount(const FString& InItemId) const;
	void SetItemCount(const FString& InItemId, int32 InCount);
	bool AddItem(const FString& InItemId, int32 InDelta, FString& OutError);
};

struct EPICODESANDBOX_API FEpicodeSandboxParticipant
{
	FString Id;
	EEpicodeSandboxParticipantKind Kind = EEpicodeSandboxParticipantKind::Performer;
	FString CharacterEntityId;
	bool bConnected = true;
	bool bTakeover = false;
	FString ConnectedControllerId;
	TSharedPtr<FJsonObject> UnknownFields;

	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeSandboxParticipant& OutParticipant, FString& OutError);
	FString ToCanonicalString() const;
};

struct EPICODESANDBOX_API FEpicodeSandboxNarrativeState
{
	FString CurrentBeatId;
	FString CurrentObjectiveId;
	FString Note;
	TMap<FString, FEpicodeSandboxValue> Variables;
	TMap<FString, bool> Objectives;
	TArray<FString> CompletedEvents;

	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeSandboxNarrativeState& OutState, FString& OutError);
	FString ToCanonicalString() const;
	const FEpicodeSandboxValue* FindVariable(const FString& InName) const;
	void SetVariable(const FString& InName, const FEpicodeSandboxValue& InValue);
};

struct EPICODESANDBOX_API FEpicodeSandboxEvent
{
	int64 Sequence = 0;
	int64 Tick = 0;
	FString Type;
	FString ActorId;
	FString TargetId;
	TSharedPtr<FJsonObject> Payload;

	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeSandboxEvent& OutEvent, FString& OutError);
	FString ToCanonicalString() const;
};

struct EPICODESANDBOX_API FEpicodeSandboxQueuedCommand
{
	int64 Sequence = 0;
	int64 Tick = 0;
	EEpicodeSandboxCommandCategory Category = EEpicodeSandboxCommandCategory::Input;
	TSharedPtr<FJsonObject> Payload;
};

struct EPICODESANDBOX_API FEpicodeSandboxProjectionCommand
{
	FString Operation = TEXT("upsert");
	FString EntityId;
	EEpicodeSandboxEntityKind Kind = EEpicodeSandboxEntityKind::Prop;
	FString ArchetypeId;
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	TSharedPtr<FJsonObject> Data;

	TSharedPtr<FJsonObject> ToJson() const;
};

struct EPICODESANDBOX_API FEpicodeSandboxRandomDraw
{
	int64 Tick = 0;
	FString Domain;
	FString Kind;
	double Value = 0.0;
};
