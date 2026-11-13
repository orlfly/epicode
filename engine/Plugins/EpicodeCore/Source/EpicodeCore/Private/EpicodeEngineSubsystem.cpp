#include "EpicodeEngineSubsystem.h"

#include "EpicodeCoreModule.h"
#include "EpicodeSceneCommands.h"
#include "Engine/Engine.h"

void UEpicodeEngineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	CommandRegistry = MakeShared<FEpicodeCommandRegistry>();
	EventBus = MakeShared<FEpicodeEventBus>();
	FEpicodeSceneCommands::Register(*CommandRegistry);
	FEpicodeCommandProviderRegistry::RegisterAll(*CommandRegistry);
	UE_LOG(LogEpicodeCore, Log, TEXT("Epicode engine subsystem initialized with %d commands"), CommandRegistry->GetMethodNames().Num());
}

void UEpicodeEngineSubsystem::Deinitialize()
{
	CommandRegistry.Reset();
	EventBus.Reset();
	TransactionSink = nullptr;
	Super::Deinitialize();
}

UEpicodeEngineSubsystem* UEpicodeEngineSubsystem::Get()
{
	return GEngine ? GEngine->GetEngineSubsystem<UEpicodeEngineSubsystem>() : nullptr;
}

FEpicodeCommandResult UEpicodeEngineSubsystem::ExecuteCommand(const FString& Method, const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
{
	if (!CommandRegistry.IsValid())
	{
		return FEpicodeCommandResult::Failure(TEXT("NOT_INITIALIZED"), TEXT("Epicode engine subsystem is not initialized."));
	}
	FEpicodeCommandResult Result;
	FString UnknownMethod;
	if (!CommandRegistry->Execute(Method, Params, Context, Result, UnknownMethod))
	{
		return FEpicodeCommandResult::Failure(TEXT("METHOD_NOT_FOUND"), FString::Printf(TEXT("Unknown method: %s"), *Method));
	}
	return Result;
}
