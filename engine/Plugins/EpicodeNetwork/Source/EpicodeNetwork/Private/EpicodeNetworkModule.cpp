#include "EpicodeNetworkModule.h"

#include "EpicodeCommand.h"
#include "EpicodeDiagnostics.h"
#include "EpicodeEngineSubsystem.h"
#include "EpicodeNetworkCommands.h"
#include "EpicodeNetworkSubsystem.h"
#include "EpicodeProcessRole.h"
#include "Interfaces/IPluginManager.h"
#include "Modules/ModuleManager.h"
#include "Misc/CommandLine.h"

DEFINE_LOG_CATEGORY(LogEpicodeNetwork);

FString FEpicodeNetworkModule::ResolveProcessRole(const FString& InCommandLine, bool bIsEditor)
{
	return FEpicodeProcessRole::Resolve(InCommandLine, bIsEditor);
}

void FEpicodeNetworkModule::StartupModule()
{
	const FString ProcessRole = ResolveProcessRole(FCommandLine::Get(), GIsEditor);
	UE_LOG(LogEpicodeNetwork, Log, TEXT("Epicode process role: %s"), *ProcessRole);
	FEpicodeCommandProviderRegistry::AddProvider(TEXT("EpicodeNetwork"), [](FEpicodeCommandRegistry& Registry)
	{
		FEpicodeNetworkCommands::Register(Registry);
	});
	if (UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get())
	{
		if (FEpicodeCommandRegistry* Registry = EngineSubsystem->GetCommandRegistry())
		{
			FEpicodeNetworkCommands::Register(*Registry);
		}
	}
	FEpicodeDiagnostics::SetNetworkStatusProvider([]()
	{
		UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
		TSharedPtr<FJsonObject> Status = Subsystem ? Subsystem->GetStatusJson() : MakeShared<FJsonObject>();
		Status->SetBoolField(TEXT("moduleLoaded"), FModuleManager::Get().IsModuleLoaded(TEXT("EpicodeNetwork")));
		return Status;
	});
	FString PluginVersion = TEXT("unknown");
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("EpicodeNetwork")))
	{
		PluginVersion = Plugin->GetDescriptor().VersionName;
	}
	UE_LOG(LogEpicodeNetwork, Log, TEXT("EpicodeNetwork module started (version %s)"), *PluginVersion);
}

void FEpicodeNetworkModule::ShutdownModule()
{
	FEpicodeCommandProviderRegistry::RemoveProvider(TEXT("EpicodeNetwork"));
	FEpicodeDiagnostics::ClearNetworkStatusProvider();
	UE_LOG(LogEpicodeNetwork, Log, TEXT("EpicodeNetwork module stopped"));
}

IMPLEMENT_MODULE(FEpicodeNetworkModule, EpicodeNetwork);
