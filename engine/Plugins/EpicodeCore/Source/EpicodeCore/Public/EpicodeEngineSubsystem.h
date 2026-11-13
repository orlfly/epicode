#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "EpicodeCommand.h"
#include "EpicodeEventBus.h"
#include "EpicodeEngineSubsystem.generated.h"

UCLASS()
class EPICODECORE_API UEpicodeEngineSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static UEpicodeEngineSubsystem* Get();

	FEpicodeCommandRegistry* GetCommandRegistry() const { return CommandRegistry.Get(); }
	FEpicodeEventBus* GetEventBus() const { return EventBus.Get(); }
	void SetTransactionSink(IEpicodeTransactionSink* InSink) { TransactionSink = InSink; }
	IEpicodeTransactionSink* GetTransactionSink() const { return TransactionSink; }

	FEpicodeCommandResult ExecuteCommand(const FString& Method, const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context);

private:
	TSharedPtr<FEpicodeCommandRegistry> CommandRegistry;
	TSharedPtr<FEpicodeEventBus> EventBus;
	IEpicodeTransactionSink* TransactionSink = nullptr;
};
