#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct EPICODECORE_API FEpicodeSceneSnapshot
{
	uint64 Version = 0;
	FString SceneId;
	FString Name;
	TSharedPtr<FJsonObject> Json;

	TSharedPtr<FJsonObject> ToJson() const;
};
