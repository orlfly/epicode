#pragma once

#include "CoreMinimal.h"
#include "EpicodeProductionDocument.h"

struct EPICODESANDBOX_API FEpicodeProductionValidationIssue
{
	FString Code;
	FString Path;
	FString Message;
	FString EntityId;

	TSharedPtr<FJsonObject> ToJson() const;
};

struct EPICODESANDBOX_API FEpicodeProductionValidationResult
{
	bool bSuccess = true;
	FString SceneId;
	int32 SceneSchemaVersion = 0;
	int32 EntityCount = 0;
	FString SceneDigest;
	TArray<FEpicodeProductionValidationIssue> Issues;

	void AddIssue(const FString& Code, const FString& Path, const FString& Message, const FString& EntityId = FString());
	FString GetFirstCode() const;
	FString GetFirstError() const;
	TSharedPtr<FJsonObject> ToJson() const;
};

class EPICODESANDBOX_API FEpicodeProductionValidator
{
public:
	static bool ValidateDocument(const FEpicodeProductionDocument& InDocument, FEpicodeProductionValidationResult& OutResult);
	static bool ValidateBinding(const FEpicodeProductionDocument& InDocument, const FString& InSceneRoot, FEpicodeProductionValidationResult& OutResult);
	static bool IsAllowedAssetPath(const FString& InPath);
};
