#include "EpicodeRenderModule.h"

#include "Editor.h"
#include "EpicodeCommand.h"
#include "EpicodeEngineSubsystem.h"
#include "EpicodeRenderCommands.h"
#include "EpicodeRenderService.h"
#include "EpicodeSceneSubsystem.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY(LogEpicodeRender);

void FEpicodeRenderModule::StartupModule()
{
	FString RequestPath;
	if (FParse::Value(FCommandLine::Get(), TEXT("EpicodeRenderWorker="), RequestPath) && !RequestPath.IsEmpty())
	{
		WorkerRequestPath = RequestPath;
		PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FEpicodeRenderModule::StartWorker);
	}
	else
	{
		FEpicodeRenderService::Get().Initialize();
	}
	FEpicodeCommandProviderRegistry::AddProvider(TEXT("EpicodeRender"), [](FEpicodeCommandRegistry& Registry)
	{
		FEpicodeRenderCommands::Register(Registry);
	});
	if (UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get())
	{
		if (FEpicodeCommandRegistry* Registry = EngineSubsystem->GetCommandRegistry())
		{
			FEpicodeRenderCommands::Register(*Registry);
		}
	}
	UE_LOG(LogEpicodeRender, Log, TEXT("EpicodeRender module started"));
}

void FEpicodeRenderModule::ShutdownModule()
{
	if (WorkerTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(WorkerTickerHandle);
		WorkerTickerHandle.Reset();
	}
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}
	FEpicodeCommandProviderRegistry::RemoveProvider(TEXT("EpicodeRender"));
	if (WorkerRequestPath.IsEmpty())
	{
		FEpicodeRenderService::Get().Shutdown();
	}
	UE_LOG(LogEpicodeRender, Log, TEXT("EpicodeRender module stopped"));
}

void FEpicodeRenderModule::StartWorker()
{
	WorkerStartSeconds = FPlatformTime::Seconds();
	WorkerTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FEpicodeRenderModule::TickWorker), 0.25f);
}

bool FEpicodeRenderModule::TickWorker(float DeltaSeconds)
{
	if (GEditor && GEditor->GetEditorWorldContext().World() && UEpicodeSceneSubsystem::GetPrimaryWorld())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(WorkerTickerHandle);
		WorkerTickerHandle.Reset();
		FEpicodeRenderService::RunWorker(WorkerRequestPath);
		return false;
	}
	if (FPlatformTime::Seconds() - WorkerStartSeconds > 120.0)
	{
		FTSTicker::GetCoreTicker().RemoveTicker(WorkerTickerHandle);
		WorkerTickerHandle.Reset();
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("errorCode"), TEXT("RENDER_WORKER_START_TIMEOUT"));
		Result->SetStringField(TEXT("errorMessage"), TEXT("The editor world was not ready within 120 seconds."));
		Result->SetNumberField(TEXT("renderedFrames"), 0);
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(Result, Writer);
		FFileHelper::SaveStringToFile(Text, *FPaths::Combine(FPaths::GetPath(WorkerRequestPath), TEXT("result.json")));
		UE_LOG(LogEpicodeRender, Error, TEXT("Render worker timed out waiting for the editor world."));
		FPlatformMisc::RequestExit(false);
		return false;
	}
	return true;
}

IMPLEMENT_MODULE(FEpicodeRenderModule, EpicodeRender);
