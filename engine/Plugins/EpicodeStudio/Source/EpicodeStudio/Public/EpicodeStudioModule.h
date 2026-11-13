#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class SDockTab;
class FSpawnTabArgs;
class IEpicodeTransactionSink;

DECLARE_LOG_CATEGORY_EXTERN(LogEpicodeStudio, Log, All);

class FEpicodeStudioModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TSharedRef<SDockTab> SpawnStudioTab(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnMultiplayerTab(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnDirectorTab(const FSpawnTabArgs& Args);
	void RegisterTransactionSink();
	void UnregisterTransactionSink();
	void HandlePostUndoRedo();

	TSharedPtr<IEpicodeTransactionSink> TransactionSink;
	FDelegateHandle PostEngineInitHandle;
	FDelegateHandle PostUndoRedoHandle;
};
