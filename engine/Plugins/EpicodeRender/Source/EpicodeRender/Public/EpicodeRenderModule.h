#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Modules/ModuleInterface.h"

DECLARE_LOG_CATEGORY_EXTERN(LogEpicodeRender, Log, All);

class FEpicodeRenderModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void StartWorker();
	bool TickWorker(float DeltaSeconds);

	FString WorkerRequestPath;
	FDelegateHandle PostEngineInitHandle;
	FTSTicker::FDelegateHandle WorkerTickerHandle;
	double WorkerStartSeconds = 0.0;
};
