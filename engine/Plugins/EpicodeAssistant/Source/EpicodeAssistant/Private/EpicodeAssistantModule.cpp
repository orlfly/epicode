#include "Modules/ModuleManager.h"

#include "EpicodeCommand.h"
#include "EpicodeEngineSubsystem.h"
#include "EpicodeScript.h"

class FEpicodeAssistantModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FEpicodeCommandProviderRegistry::AddProvider(
			TEXT("EpicodeAssistantScript"),
			[](FEpicodeCommandRegistry& Registry)
			{
				FEpicodeScriptService::Register(Registry);
			});
		if (UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get())
		{
			if (FEpicodeCommandRegistry* Registry = EngineSubsystem->GetCommandRegistry())
			{
				FEpicodeScriptService::Register(*Registry);
			}
		}
	}

	virtual void ShutdownModule() override
	{
		FEpicodeCommandProviderRegistry::RemoveProvider(TEXT("EpicodeAssistantScript"));
	}
};

IMPLEMENT_MODULE(FEpicodeAssistantModule, EpicodeAssistant)
