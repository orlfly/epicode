#include "EpicodeCoreModule.h"

#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogEpicodeCore);

void FEpicodeCoreModule::StartupModule()
{
	UE_LOG(LogEpicodeCore, Log, TEXT("EpicodeCore module started"));
}

void FEpicodeCoreModule::ShutdownModule()
{
	UE_LOG(LogEpicodeCore, Log, TEXT("EpicodeCore module stopped"));
}

IMPLEMENT_MODULE(FEpicodeCoreModule, EpicodeCore);
