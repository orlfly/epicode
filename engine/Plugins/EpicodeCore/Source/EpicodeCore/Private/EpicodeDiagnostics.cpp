#include "EpicodeDiagnostics.h"

#include "EpicodeSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "RHIGlobals.h"
#include "Json.h"

namespace
{
	FCriticalSection GNetworkStatusLock;
	FEpicodeDiagnostics::FStatusProvider GNetworkStatusProvider;
	FCriticalSection GComponentStatusLock;
	TMap<FString, FEpicodeDiagnostics::FStatusProvider> GComponentStatusProviders;
}

void FEpicodeDiagnostics::SetNetworkStatusProvider(FStatusProvider InProvider)
{
	FScopeLock Lock(&GNetworkStatusLock);
	GNetworkStatusProvider = MoveTemp(InProvider);
}

TSharedPtr<FJsonObject> FEpicodeDiagnostics::GetNetworkStatus()
{
	FStatusProvider Provider;
	{
		FScopeLock Lock(&GNetworkStatusLock);
		Provider = GNetworkStatusProvider;
	}
	return Provider ? Provider() : nullptr;
}

void FEpicodeDiagnostics::ClearNetworkStatusProvider()
{
	FScopeLock Lock(&GNetworkStatusLock);
	GNetworkStatusProvider = nullptr;
}

void FEpicodeDiagnostics::SetComponentStatusProvider(const FString& Name, FStatusProvider InProvider)
{
	FScopeLock Lock(&GComponentStatusLock);
	if (InProvider)
	{
		GComponentStatusProviders.Add(Name, MoveTemp(InProvider));
	}
	else
	{
		GComponentStatusProviders.Remove(Name);
	}
}

void FEpicodeDiagnostics::ClearComponentStatusProvider(const FString& Name)
{
	FScopeLock Lock(&GComponentStatusLock);
	GComponentStatusProviders.Remove(Name);
}

TMap<FString, TSharedPtr<FJsonObject>> FEpicodeDiagnostics::GetComponentStatuses()
{
	TMap<FString, FStatusProvider> Providers;
	{
		FScopeLock Lock(&GComponentStatusLock);
		Providers = GComponentStatusProviders;
	}
	TMap<FString, TSharedPtr<FJsonObject>> Result;
	for (const TPair<FString, FStatusProvider>& Pair : Providers)
	{
		if (Pair.Value)
		{
			Result.Add(Pair.Key, Pair.Value());
		}
	}
	return Result;
}

FEpicodeDiagnosticsResult FEpicodeDiagnostics::Run()
{
	FEpicodeDiagnosticsResult Result;
	Result.EngineVersion = FEngineVersion::Current().ToString(EVersionComponent::Patch);

	const int32 Major = FEngineVersion::Current().GetMajor();
	const int32 Minor = FEngineVersion::Current().GetMinor();
	if (Major < 5 || (Major == 5 && Minor < 8))
	{
		Result.Messages.Add(FString::Printf(TEXT("Unreal Engine 5.8 or newer is required, found %s."), *Result.EngineVersion));
	}

	const TCHAR* RequiredModules[] = {
		TEXT("HTTPServer"),
		TEXT("EpicodeCore"),
		TEXT("EpicodeControlAPI")
	};
	for (const TCHAR* ModuleName : RequiredModules)
	{
		if (!FModuleManager::Get().IsModuleLoaded(ModuleName))
		{
			Result.Messages.Add(FString::Printf(TEXT("Required module is not loaded: %s"), ModuleName));
		}
	}
	const TCHAR* RequiredPlugins[] = {
		TEXT("Interchange"),
		TEXT("MovieRenderPipeline"),
		TEXT("EditorScriptingUtilities")
	};
	for (const TCHAR* PluginName : RequiredPlugins)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
		if (!Plugin.IsValid() || !Plugin->IsEnabled())
		{
			Result.Messages.Add(FString::Printf(TEXT("Required plugin is not enabled: %s"), PluginName));
		}
	}

	Result.RenderBackend = FApp::GetGraphicsRHI();
	if (Result.RenderBackend.IsEmpty())
	{
		Result.RenderBackend = TEXT("none");
	}
	Result.GpuAdapter = GRHIGlobals.GpuInfo.AdapterName;
	Result.GpuVendorId = static_cast<int32>(GRHIGlobals.GpuInfo.VendorId);
	Result.GpuDeviceId = static_cast<int32>(GRHIGlobals.GpuInfo.DeviceId);

	const UEpicodeSettings* Settings = UEpicodeSettings::Get();
	const FString OutputDir = Settings->GetRenderOutputRootPath();
	if (!IFileManager::Get().MakeDirectory(*OutputDir, true))
	{
		Result.Messages.Add(FString::Printf(TEXT("Render output directory is not writable: %s"), *OutputDir));
	}
	else
	{
		const FString Probe = FPaths::Combine(OutputDir, TEXT(".epicode-write-test"));
		if (!FFileHelper::SaveStringToFile(TEXT("ok"), *Probe))
		{
			Result.Messages.Add(FString::Printf(TEXT("Render output directory is not writable: %s"), *OutputDir));
		}
		else
		{
			IFileManager::Get().Delete(*Probe);
		}
	}

	FString SettingsError;
	if (!Settings->Validate(SettingsError))
	{
		Result.Messages.Add(SettingsError);
	}

	Result.bOk = Result.Messages.Num() == 0;
	Result.bDegraded = !Result.bOk;
	return Result;
}

FString FEpicodeDiagnostics::ToJson(const FEpicodeDiagnosticsResult& Result)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("status"), Result.bOk ? TEXT("ok") : TEXT("degraded"));
	Root->SetStringField(TEXT("engineVersion"), Result.EngineVersion);
	Root->SetStringField(TEXT("projectVersion"), Result.ProjectVersion);
	Root->SetStringField(TEXT("renderBackend"), Result.RenderBackend);
	Root->SetStringField(TEXT("gpuAdapter"), Result.GpuAdapter);
	Root->SetNumberField(TEXT("gpuVendorId"), Result.GpuVendorId);
	Root->SetNumberField(TEXT("gpuDeviceId"), Result.GpuDeviceId);
	TArray<TSharedPtr<FJsonValue>> Messages;
	for (const FString& Message : Result.Messages)
	{
		Messages.Add(MakeShared<FJsonValueString>(Message));
	}
	Root->SetArrayField(TEXT("messages"), Messages);

	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}
