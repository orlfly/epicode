#include "EpicodeImportModule.h"

#include "EpicodeCommand.h"
#include "EpicodeEngineSubsystem.h"
#include "EpicodeImportCommands.h"
#include "EpicodeImportService.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogEpicodeImport);

void FEpicodeImportModule::StartupModule()
{
	FEpicodeImportService::Get().Initialize();
	FEpicodeCommandProviderRegistry::AddProvider(TEXT("EpicodeImport"), [](FEpicodeCommandRegistry& Registry)
	{
		FEpicodeImportCommands::Register(Registry);
	});
	if (UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get())
	{
		if (FEpicodeCommandRegistry* Registry = EngineSubsystem->GetCommandRegistry())
		{
			FEpicodeImportCommands::Register(*Registry);
		}
	}
	UE_LOG(LogEpicodeImport, Log, TEXT("EpicodeImport module started"));
}

void FEpicodeImportModule::ShutdownModule()
{
	FEpicodeCommandProviderRegistry::RemoveProvider(TEXT("EpicodeImport"));
	FEpicodeImportService::Get().Shutdown();
	UE_LOG(LogEpicodeImport, Log, TEXT("EpicodeImport module stopped"));
}

IMPLEMENT_MODULE(FEpicodeImportModule, EpicodeImport);
