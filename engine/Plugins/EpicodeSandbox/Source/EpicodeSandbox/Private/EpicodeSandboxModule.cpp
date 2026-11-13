#include "EpicodeSandboxModule.h"

#include "EpicodeCommand.h"
#include "EpicodeContentDigest.h"
#include "EpicodeDiagnostics.h"
#include "EpicodeProcessRole.h"
#include "EpicodeSandboxCommands.h"
#include "EpicodeSandboxSettings.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogEpicodeSandbox, Log, All);

namespace
{
	bool ProbeWritable(const FString& Directory)
	{
		if (!IFileManager::Get().MakeDirectory(*Directory, true))
		{
			return false;
		}
		const FString Probe = FPaths::Combine(Directory, TEXT(".epicode-sandbox-write-test"));
		if (!FFileHelper::SaveStringToFile(TEXT("ok"), *Probe))
		{
			return false;
		}
		IFileManager::Get().Delete(*Probe);
		return true;
	}
}

FString FEpicodeSandboxModule::GetModuleVersion()
{
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("EpicodeSandbox")))
	{
		return Plugin->GetDescriptor().VersionName;
	}
	return TEXT("unknown");
}

void FEpicodeSandboxModule::StartupModule()
{
	const FString Role = FEpicodeProcessRole::Resolve(FCommandLine::Get(), GIsEditor);
	UE_LOG(LogEpicodeSandbox, Log, TEXT("EpicodeSandbox module started (version %s, role %s)"), *GetModuleVersion(), *Role);

	FEpicodeCommandProviderRegistry::AddProvider(TEXT("EpicodeSandbox"), [](FEpicodeCommandRegistry& Registry)
	{
		FEpicodeSandboxCommands::Register(Registry);
	});

	FEpicodeDiagnostics::SetComponentStatusProvider(TEXT("sandbox"), []()
	{
		const UEpicodeSandboxSettings* Settings = UEpicodeSandboxSettings::Get();
		TSharedPtr<FJsonObject> Status = MakeShared<FJsonObject>();
		Status->SetStringField(TEXT("module"), TEXT("EpicodeSandbox"));
		Status->SetStringField(TEXT("version"), GetModuleVersion());
		Status->SetBoolField(TEXT("enabled"), Settings->bEnableSandboxDirector);
		Status->SetNumberField(TEXT("fixedTickRate"), Settings->FixedTickRate);
		Status->SetNumberField(TEXT("randomSeed"), static_cast<double>(Settings->RandomSeed));
		Status->SetNumberField(TEXT("maxEntities"), Settings->MaxEntities);
		Status->SetNumberField(TEXT("maxEventsPerSecond"), Settings->MaxEventsPerSecond);
		Status->SetNumberField(TEXT("maxDirectorCues"), Settings->MaxDirectorCues);
		Status->SetNumberField(TEXT("checkpointIntervalEvents"), Settings->CheckpointIntervalEvents);
		Status->SetNumberField(TEXT("replayRetentionHours"), Settings->ReplayRetentionHours);
		Status->SetStringField(TEXT("productionRoot"), Settings->GetProductionRootPath());
		Status->SetStringField(TEXT("sessionRoot"), Settings->GetSessionRootPath());
		Status->SetBoolField(TEXT("journalWritable"), ProbeWritable(Settings->GetSessionRootPath()));

		FString Digest = FEpicodeContentDigest::GetCachedBuildDigest();
		if (Digest.IsEmpty())
		{
			FString Error;
			if (FEpicodeContentDigest::ComputeBuildDigest(Settings->RuleSchemaVersion, Digest, Error))
			{
				Status->SetStringField(TEXT("contentDigest"), Digest);
			}
			else
			{
				Status->SetStringField(TEXT("contentDigestError"), Error);
			}
		}
		else
		{
			Status->SetStringField(TEXT("contentDigest"), Digest);
		}
		return Status;
	});
}

void FEpicodeSandboxModule::ShutdownModule()
{
	FEpicodeCommandProviderRegistry::RemoveProvider(TEXT("EpicodeSandbox"));
	FEpicodeDiagnostics::ClearComponentStatusProvider(TEXT("sandbox"));
	UE_LOG(LogEpicodeSandbox, Log, TEXT("EpicodeSandbox module stopped"));
}

IMPLEMENT_MODULE(FEpicodeSandboxModule, EpicodeSandbox);
