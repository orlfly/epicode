#include "EpicodeControlAPIModule.h"

#include "EpicodeAPIServer.h"
#include "EpicodeCommand.h"
#include "EpicodeSettings.h"
#include "EpicodeStudioAPICommands.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogEpicodeAPI);

void FEpicodeControlAPIModule::StartupModule()
{
	FEpicodeCommandProviderRegistry::AddProvider(TEXT("EpicodeStudioAPI"), [](FEpicodeCommandRegistry& Registry)
	{
		FEpicodeStudioAPICommands::Register(Registry);
	});
	PostEngineInitHandle = FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &FEpicodeControlAPIModule::StartApi);
	UE_LOG(LogEpicodeAPI, Log, TEXT("EpicodeControlAPI module started"));
}

void FEpicodeControlAPIModule::ShutdownModule()
{
	FEpicodeCommandProviderRegistry::RemoveProvider(TEXT("EpicodeStudioAPI"));
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::OnFEngineLoopInitComplete.Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}
	FEpicodeAPIServer::Get().Stop();
	UE_LOG(LogEpicodeAPI, Log, TEXT("EpicodeControlAPI module stopped"));
}

void FEpicodeControlAPIModule::StartApi()
{
	if (IsRunningGame() && !IsRunningDedicatedServer())
	{
		UE_LOG(LogEpicodeAPI, Log, TEXT("Epicode API stays disabled in game client mode."));
		return;
	}
	FString Error;
	if (!FEpicodeAPIServer::Get().Start(Error))
	{
		UE_LOG(LogEpicodeAPI, Warning, TEXT("Epicode API did not start: %s"), *Error);
	}
}

IMPLEMENT_MODULE(FEpicodeControlAPIModule, EpicodeControlAPI);
