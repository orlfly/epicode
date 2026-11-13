#include "EpicodeDirectorModule.h"

#include "EpicodeDiagnostics.h"
#include "EpicodeProcessRole.h"
#include "EpicodeSandboxSettings.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CommandLine.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogEpicodeDirector, Log, All);

FString FEpicodeDirectorModule::GetModuleVersion()
{
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("EpicodeDirector")))
	{
		return Plugin->GetDescriptor().VersionName;
	}
	return TEXT("unknown");
}

void FEpicodeDirectorModule::StartupModule()
{
	const FString Role = FEpicodeProcessRole::Resolve(FCommandLine::Get(), GIsEditor);
	UE_LOG(LogEpicodeDirector, Log, TEXT("EpicodeDirector module started (version %s, role %s)"), *GetModuleVersion(), *Role);

	FEpicodeDiagnostics::SetComponentStatusProvider(TEXT("director"), []()
	{
		const UEpicodeSandboxSettings* Settings = UEpicodeSandboxSettings::Get();
		TSharedPtr<FJsonObject> Status = MakeShared<FJsonObject>();
		Status->SetStringField(TEXT("module"), TEXT("EpicodeDirector"));
		Status->SetStringField(TEXT("version"), GetModuleVersion());
		Status->SetBoolField(TEXT("enabled"), Settings->bEnableSandboxDirector);
		Status->SetBoolField(TEXT("replayEnabled"), Settings->bEnableReplay);
		Status->SetNumberField(TEXT("replayQueueDepth"), 0);
		Status->SetNumberField(TEXT("activeReplays"), 0);
		Status->SetNumberField(TEXT("maxConcurrentReplays"), Settings->MaxConcurrentReplays);
		return Status;
	});
}

void FEpicodeDirectorModule::ShutdownModule()
{
	FEpicodeDiagnostics::ClearComponentStatusProvider(TEXT("director"));
	UE_LOG(LogEpicodeDirector, Log, TEXT("EpicodeDirector module stopped"));
}

IMPLEMENT_MODULE(FEpicodeDirectorModule, EpicodeDirector);
