#include "Misc/AutomationTest.h"

#include "EpicodeCommand.h"
#include "EpicodeContentDigest.h"
#include "EpicodeEngineSubsystem.h"
#include "EpicodeSandboxSettings.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	FString MakeTempDirectory(const FString& InName)
	{
		const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Epicode"), TEXT("SandboxTests"), InName + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
		IFileManager::Get().MakeDirectory(*Directory, true);
		return Directory;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeFoundationDeterminismConfigTest, "Epicode.Foundation.DeterminismConfig", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeFoundationDeterminismConfigTest::RunTest(const FString& Parameters)
{
	const UEpicodeSandboxSettings* Defaults = UEpicodeSandboxSettings::Get();
	TestNotNull(TEXT("sandbox settings"), Defaults);
	if (!Defaults)
	{
		return false;
	}
	TestEqual(TEXT("fixed tick"), Defaults->FixedTickRate, 30);
	TestEqual(TEXT("checkpoint interval"), Defaults->CheckpointIntervalEvents, 1000);
	TestTrue(TEXT("sandbox enabled"), Defaults->bEnableSandboxDirector);
	TestTrue(TEXT("replay enabled"), Defaults->bEnableReplay);
	FString Error;
	TestTrue(TEXT("defaults validate"), Defaults->Validate(Error));

	UEpicodeSandboxSettings* Invalid = NewObject<UEpicodeSandboxSettings>();
	Invalid->FixedTickRate = 0;
	TestFalse(TEXT("zero tick rejected"), Invalid->Validate(Error));

	Invalid->FixedTickRate = 30;
	Invalid->RandomSeed = -1;
	TestFalse(TEXT("negative seed rejected"), Invalid->Validate(Error));

	Invalid->RandomSeed = 1;
	Invalid->CheckpointIntervalEvents = 10000000;
	TestFalse(TEXT("checkpoint beyond retention rejected"), Invalid->Validate(Error));

	Invalid->CheckpointIntervalEvents = 1000;
	Invalid->RuleSchemaVersion.Reset();
	TestFalse(TEXT("empty schema rejected"), Invalid->Validate(Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeFoundationContentDigestTest, "Epicode.Foundation.ContentDigest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeFoundationContentDigestTest::RunTest(const FString& Parameters)
{
	const FString Directory = MakeTempDirectory(TEXT("digest"));
	TestTrue(TEXT("file a"), FFileHelper::SaveStringToFile(TEXT("alpha"), *FPaths::Combine(Directory, TEXT("a.txt"))));
	TestTrue(TEXT("file b"), FFileHelper::SaveStringToFile(TEXT("beta"), *FPaths::Combine(Directory, TEXT("b.txt"))));

	FString First;
	FString Second;
	FString Error;
	TestTrue(TEXT("first digest"), FEpicodeContentDigest::ComputeDirectoryDigest(Directory, First, Error));
	TestTrue(TEXT("second digest"), FEpicodeContentDigest::ComputeDirectoryDigest(Directory, Second, Error));
	TestEqual(TEXT("stable digest"), First, Second);
	TestFalse(TEXT("digest is non-empty"), First.IsEmpty());

	TestTrue(TEXT("modify file"), FFileHelper::SaveStringToFile(TEXT("gamma"), *FPaths::Combine(Directory, TEXT("b.txt"))));
	FString Changed;
	TestTrue(TEXT("changed digest"), FEpicodeContentDigest::ComputeDirectoryDigest(Directory, Changed, Error));
	TestNotEqual(TEXT("content change changes digest"), Changed, First);

	FString Missing;
	TestFalse(TEXT("missing directory rejected"), FEpicodeContentDigest::ComputeDirectoryDigest(FPaths::Combine(Directory, TEXT("missing")), Missing, Error));
	TestFalse(TEXT("missing error"), Error.IsEmpty());
	TestEqual(TEXT("hash stable"), FEpicodeContentDigest::HashString(TEXT("epicode")), FEpicodeContentDigest::HashString(TEXT("epicode")));
	TestNotEqual(TEXT("hash changes"), FEpicodeContentDigest::HashString(TEXT("epicode")), FEpicodeContentDigest::HashString(TEXT("epicode2")));

	IFileManager::Get().DeleteDirectory(*Directory, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeFoundationHealthTest, "Epicode.Foundation.Health", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeFoundationHealthTest::RunTest(const FString& Parameters)
{
	UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get();
	TestNotNull(TEXT("engine subsystem"), EngineSubsystem);
	if (!EngineSubsystem || !EngineSubsystem->GetCommandRegistry())
	{
		return false;
	}
	FEpicodeCommandResult Result;
	FEpicodeCommandContext Context;
	FString Error;
	EngineSubsystem->GetCommandRegistry()->Execute(TEXT("system.health"), MakeShared<FJsonObject>(), Context, Result, Error);
	TestTrue(TEXT("health succeeds"), Result.bSuccess);
	if (!Result.bSuccess || !Result.Result.IsValid())
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* Sandbox = nullptr;
	const TSharedPtr<FJsonObject>* Director = nullptr;
	TestTrue(TEXT("sandbox status"), Result.Result->TryGetObjectField(TEXT("sandbox"), Sandbox) && Sandbox && Sandbox->IsValid());
	TestTrue(TEXT("director status"), Result.Result->TryGetObjectField(TEXT("director"), Director) && Director && Director->IsValid());
	if (Sandbox && Sandbox->IsValid())
	{
		TestEqual(TEXT("sandbox module"), (*Sandbox)->GetStringField(TEXT("module")), FString(TEXT("EpicodeSandbox")));
		TestTrue(TEXT("sandbox tick"), (*Sandbox)->GetIntegerField(TEXT("fixedTickRate")) > 0);
		TestTrue(TEXT("sandbox journal writable"), (*Sandbox)->GetBoolField(TEXT("journalWritable")));
	}
	if (Director && Director->IsValid())
	{
		TestEqual(TEXT("director module"), (*Director)->GetStringField(TEXT("module")), FString(TEXT("EpicodeDirector")));
		TestTrue(TEXT("director replay enabled"), (*Director)->GetBoolField(TEXT("replayEnabled")));
		TestTrue(TEXT("director queue depth"), (*Director)->GetIntegerField(TEXT("replayQueueDepth")) >= 0);
	}
	return true;
}
