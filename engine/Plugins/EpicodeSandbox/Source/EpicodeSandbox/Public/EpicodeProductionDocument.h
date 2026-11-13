#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FEpicodeSceneDocument;

struct EPICODESANDBOX_API FEpicodeProductionCastMember
{
	FString Slot;
	FString EntityId;
	FString ParticipantKind = TEXT("performer");
	TSharedPtr<FJsonObject> Extra;

	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeProductionCastMember& OutMember, FString& OutError);
};

struct EPICODESANDBOX_API FEpicodeProductionCameraPreset
{
	FString Id;
	FString Name;
	FString CameraId;
	TSharedPtr<FJsonObject> Settings;
	TSharedPtr<FJsonObject> Extra;

	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeProductionCameraPreset& OutPreset, FString& OutError);
};

struct EPICODESANDBOX_API FEpicodeProductionMigration
{
	static bool Migrate(TSharedPtr<FJsonObject>& InOutJson, int32 FromVersion, int32 ToVersion, FString& OutError);
};

struct EPICODESANDBOX_API FEpicodeProductionDocument
{
	static constexpr int32 CurrentSchemaVersion = 1;

	int32 SchemaVersion = CurrentSchemaVersion;
	FString ProductionId;
	FString Name;
	FString SceneId;
	TArray<FEpicodeProductionCastMember> Cast;
	TSharedPtr<FJsonObject> WorldRules;
	TSharedPtr<FJsonObject> Narrative;
	TArray<FEpicodeProductionCameraPreset> CameraPresets;
	TSharedPtr<FJsonObject> Extensions;
	TSharedPtr<FJsonObject> UnknownFields;
	bool bPendingUpgrade = false;

	static FEpicodeProductionDocument CreateDefault(const FString& InSceneId, const FString& InName = TEXT("Untitled Production"));
	static TSharedPtr<FJsonObject> MakeDefaultWorldRules();
	static TSharedPtr<FJsonObject> MakeDefaultNarrative();

	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeProductionDocument& OutDocument, FString& OutError);

	bool Validate(FString& OutError) const;
	const FEpicodeProductionCastMember* FindCastBySlot(const FString& InSlot) const;
	FEpicodeProductionCastMember* FindCastBySlot(const FString& InSlot);
	const FEpicodeProductionCastMember* FindCastByEntity(const FString& InEntityId) const;
};
