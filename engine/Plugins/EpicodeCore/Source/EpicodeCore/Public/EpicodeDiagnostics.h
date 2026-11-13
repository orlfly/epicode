#pragma once

#include "CoreMinimal.h"

struct FEpicodeDiagnosticsResult
{
	bool bOk = false;
	bool bDegraded = false;
	FString EngineVersion;
	FString ProjectVersion = TEXT("0.1.0");
	FString RenderBackend;
	FString GpuAdapter;
	int32 GpuVendorId = 0;
	int32 GpuDeviceId = 0;
	TArray<FString> Messages;
};

class EPICODECORE_API FEpicodeDiagnostics
{
public:
	static FEpicodeDiagnosticsResult Run();
	static FString ToJson(const FEpicodeDiagnosticsResult& Result);

	using FStatusProvider = TFunction<TSharedPtr<FJsonObject>()>;
	static void SetNetworkStatusProvider(FStatusProvider InProvider);
	static TSharedPtr<FJsonObject> GetNetworkStatus();
	static void ClearNetworkStatusProvider();

	/** Registers a named runtime component status provider (sandbox, director, ...). */
	static void SetComponentStatusProvider(const FString& Name, FStatusProvider InProvider);
	static void ClearComponentStatusProvider(const FString& Name);
	static TMap<FString, TSharedPtr<FJsonObject>> GetComponentStatuses();
};
