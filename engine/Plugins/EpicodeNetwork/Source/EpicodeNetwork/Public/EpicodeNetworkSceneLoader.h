#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "EpicodeTypes.h"

struct EPICODENETWORK_API FEpicodeNetworkSceneCharacter
{
	FString EntityId;
	FString MeshPath;
	FString ReplicationClass;
	FEpicodeTransform Transform;

	TSharedPtr<FJsonObject> ToJson() const;
};

struct EPICODENETWORK_API FEpicodeNetworkSceneEntity
{
	FString EntityId;
	FString Type;
	FString Name;
	bool bVisible = true;
	FEpicodeTransform Transform;
	TSharedPtr<FJsonObject> Data;

	TSharedPtr<FJsonObject> ToJson() const;
};

struct EPICODENETWORK_API FEpicodeNetworkSceneSummary
{
	FString SceneId;
	FString Name;
	FString LevelPath;
	FString DocumentPath;
	int32 SchemaVersion = 0;
	int32 EntityCount = 0;
	TArray<FString> EntityIds;
	TArray<FEpicodeNetworkSceneCharacter> Characters;
	TArray<FEpicodeNetworkSceneEntity> Props;
	TArray<FEpicodeNetworkSceneEntity> Lights;
	TArray<FEpicodeNetworkSceneEntity> Cameras;
	TSharedPtr<FJsonObject> Environment;
	TSharedPtr<FJsonObject> Runtime;

	TSharedPtr<FJsonObject> ToJson() const;
};

class EPICODENETWORK_API FEpicodeNetworkSceneLoader
{
public:
	static bool Load(const FString& InSceneId, FEpicodeNetworkSceneSummary& OutSummary, FString& OutError);
};
