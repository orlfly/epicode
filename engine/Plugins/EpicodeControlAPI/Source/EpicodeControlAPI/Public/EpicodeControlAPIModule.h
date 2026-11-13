#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

DECLARE_LOG_CATEGORY_EXTERN(LogEpicodeAPI, Log, All);

class FEpicodeControlAPIModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void StartApi();

	FDelegateHandle PostEngineInitHandle;
};
