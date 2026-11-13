#pragma once

#include "CoreMinimal.h"
#include "EpicodeImportTypes.h"

class EPICODEIMPORT_API FEpicodeImportClassifier
{
public:
	static EEpicodeImportKind Classify(const FString& InFilePath);
	static bool IsSupportedExtension(const FString& InFilePath);
	static TArray<FEpicodeImportCapability> Capabilities();
	static TArray<FString> SupportedExtensions();
	static FString KindToString(EEpicodeImportKind Kind);
	static EEpicodeImportKind KindFromString(const FString& InKind);
	static bool IsMeshExtension(const FString& InFilePath);
};
