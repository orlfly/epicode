#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

DECLARE_LOG_CATEGORY_EXTERN(LogEpicodeNetwork, Log, All);

class FEpicodeNetworkModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Classifies a process as Editor, Headless, Server, Viewer or Agent. */
	static FString ResolveProcessRole(const FString& InCommandLine, bool bIsEditor);
};
