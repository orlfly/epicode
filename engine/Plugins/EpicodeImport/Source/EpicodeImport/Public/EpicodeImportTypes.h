#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "EpicodeTypes.h"

enum class EEpicodeImportKind : uint8
{
	Unknown,
	Scene,
	Character,
	Prop
};

enum class EEpicodeImportState : uint8
{
	Queued,
	Running,
	Succeeded,
	Failed,
	Cancelled
};

struct EPICODEIMPORT_API FEpicodeImportCapability
{
	FString Format;
	EEpicodeImportKind Kind = EEpicodeImportKind::Unknown;
	bool bAvailable = false;
	FString Reason;

	TSharedPtr<FJsonObject> ToJson() const;
};

struct EPICODEIMPORT_API FEpicodeImportRequest
{
	FString FilePath;
	EEpicodeImportKind Kind = EEpicodeImportKind::Unknown;
	FString EntityName;
	FString DestinationPath;
	FEpicodeTransform Transform;
	bool bAutoPlace = true;
	bool bReplaceExisting = false;
	bool bImportMaterials = true;
	float Scale = 1.0f;
	FRotator RotationOffset = FRotator::ZeroRotator;

	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& Object, FEpicodeImportRequest& OutRequest, FString& OutError);
};

struct EPICODEIMPORT_API FEpicodeImportResult
{
	bool bSuccess = false;
	FString ErrorCode;
	FString ErrorMessage;
	TArray<FString> Assets;
	TArray<FString> EntityIds;
	TArray<FString> Warnings;

	TSharedPtr<FJsonObject> ToJson() const;
};

struct EPICODEIMPORT_API FEpicodeImportJob
{
	FString Id;
	FEpicodeImportRequest Request;
	EEpicodeImportState State = EEpicodeImportState::Queued;
	FString Stage;
	float Progress = 0.0f;
	FDateTime CreatedAt = FDateTime::UtcNow();
	FDateTime FinishedAt;
	FEpicodeImportResult Result;

	TSharedPtr<FJsonObject> ToJson() const;
};
