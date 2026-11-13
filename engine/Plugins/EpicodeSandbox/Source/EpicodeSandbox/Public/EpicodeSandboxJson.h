#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class EPICODESANDBOX_API FEpicodeSandboxJson
{
public:
	static TSharedPtr<FJsonValue> CloneValue(const TSharedPtr<FJsonValue>& InValue);
	static TSharedPtr<FJsonObject> CloneObject(const TSharedPtr<FJsonObject>& InObject);
	static FString CanonicalString(const TSharedPtr<FJsonValue>& InValue);
	static FString CanonicalString(const TSharedPtr<FJsonObject>& InObject);
	static FString EscapeString(const FString& InValue);
	static FString Serialize(const TSharedPtr<FJsonObject>& InObject, bool bPretty = false);
};
