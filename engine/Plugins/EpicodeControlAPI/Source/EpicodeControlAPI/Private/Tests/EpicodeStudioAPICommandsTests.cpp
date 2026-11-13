#include "Misc/AutomationTest.h"

#include "EpicodeCommand.h"
#include "EpicodeDirector.h"
#include "EpicodeEngineSubsystem.h"
#include "EpicodeEventFilter.h"
#include "EpicodeNetworkSubsystem.h"
#include "EpicodeProductionDocument.h"
#include "EpicodeProductionLibrary.h"
#include "EpicodeReplayCheckpoint.h"
#include "EpicodeReplayEngine.h"
#include "EpicodeReplayJournal.h"
#include "EpicodeSandboxSettings.h"
#include "EpicodeSceneDocument.h"
#include "EpicodeSceneLibrary.h"
#include "EpicodeSettings.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FEpicodeCommandResult ExecuteCommand(const FString& InMethod, const TSharedPtr<FJsonObject>& InParams)
	{
		UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get();
		FEpicodeCommandResult Result;
		FString Error;
		if (!EngineSubsystem || !EngineSubsystem->GetCommandRegistry())
		{
			return FEpicodeCommandResult::Failure(TEXT("NOT_INITIALIZED"), TEXT("Command registry is unavailable."));
		}
		EngineSubsystem->GetCommandRegistry()->Execute(InMethod, InParams.IsValid() ? InParams : MakeShared<FJsonObject>(), FEpicodeCommandContext(), Result, Error);
		if (!Error.IsEmpty() && !Result.bSuccess)
		{
			Result.ErrorDetail = Error;
		}
		return Result;
	}

	void CleanupScene(const FString& InSceneId)
	{
		const FString Root = UEpicodeSettings::Get()->GetSceneRootPath();
		IFileManager::Get().DeleteDirectory(*FPaths::Combine(Root, InSceneId), false, true);
	}

	void CleanupProduction(const FString& InProductionId)
	{
		FEpicodeProductionLibraryEntry Entry;
		if (FEpicodeProductionLibrary::Find(UEpicodeSandboxSettings::Get()->GetProductionRootPath(), InProductionId, Entry))
		{
			FString TrashPath;
			FString Error;
			FEpicodeProductionLibrary::Delete(Entry, UEpicodeSandboxSettings::Get()->GetProductionTrashRootPath(), FString(), TrashPath, Error);
		}
	}

	FString MakeScene(const FString& InName)
	{
		FEpicodeSceneDocument Scene = FEpicodeSceneDocument::CreateDefault(InName);
		FEpicodeEntityRecord Character;
		Character.Id = FGuid::NewGuid();
		Character.Type = EEpicodeEntityType::Character;
		Character.Name = TEXT("API Hero");
		Character.Data = MakeShared<FJsonObject>();
		Scene.Entities.Add(Character);
		FEpicodeEntityRecord Prop;
		Prop.Id = FGuid::NewGuid();
		Prop.Type = EEpicodeEntityType::Prop;
		Prop.Name = TEXT("API Crate");
		Prop.Data = MakeShared<FJsonObject>();
		Scene.Entities.Add(Prop);
		FString Path;
		FString Error;
		if (!FEpicodeSceneLibrary::Save(Scene, UEpicodeSettings::Get()->GetSceneRootPath(), Path, Error))
		{
			return FString();
		}
		return Scene.SceneId;
	}

	FString MakeSceneAndProduction(const FString& InName, const FString& InProductionId)
	{
		const FString SceneId = MakeScene(InName);
		if (SceneId.IsEmpty())
		{
			return FString();
		}
		FEpicodeProductionDocument Document = FEpicodeProductionDocument::CreateDefault(SceneId, InName);
		Document.ProductionId = InProductionId;
		FString Path;
		FString Error;
		if (!FEpicodeProductionLibrary::Create(Document, UEpicodeSandboxSettings::Get()->GetProductionRootPath(), Path, Error))
		{
			CleanupScene(SceneId);
			return FString();
		}
		return SceneId;
	}

	void CleanupSession()
	{
		if (UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get())
		{
			FString Error;
			if (Subsystem->HasActiveSession())
			{
				Subsystem->EndSession(Error);
			}
			if (Subsystem->GetSession())
			{
				Subsystem->ClearEndedSession(Error);
			}
		}
	}

	FString MakeReplayRoot()
	{
		const FString Root = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Epicode"), TEXT("APITests"), TEXT("Replay-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
		IFileManager::Get().MakeDirectory(*Root, true);
		FEpicodeReplayJournalHeader Header;
		Header.SessionId = TEXT("api-replay");
		Header.ContentDigest = TEXT("content-digest");
		Header.Seed = 42;
		Header.StartSequence = 1;
		FEpicodeReplayJournal Journal;
		FString Error;
		if (!Journal.Create(Root, Header, Error))
		{
			return FString();
		}
		FEpicodeSandboxEvent Event;
		Event.Sequence = 1;
		Event.Tick = 0;
		Event.Type = TEXT("narrative.variable");
		Event.ActorId = TEXT("director");
		Event.Payload = MakeShared<FJsonObject>();
		Event.Payload->SetStringField(TEXT("name"), TEXT("mood"));
		Event.Payload->SetStringField(TEXT("value"), TEXT("calm"));
		if (!Journal.Append(Event, TEXT("world-digest"), TEXT("2026-01-01T00:00:00Z"), Error) || !Journal.Finalize(Error))
		{
			return FString();
		}
		FEpicodeReplayCheckpoint Checkpoint;
		Checkpoint.Sequence = 1;
		Checkpoint.Tick = 0;
		Checkpoint.TickRate = 30;
		Checkpoint.Seed = 42;
		Checkpoint.ContentDigest = Header.ContentDigest;
		Checkpoint.World = FEpicodeSandboxWorld::Create(TEXT("api-replay"), TEXT("api-production"), TEXT("api-scene"), 42);
		Checkpoint.Random.Initialize(42);
		Checkpoint.Checksum = Checkpoint.ComputeChecksum();
		if (!FEpicodeReplayCheckpoints::Save(Root, Checkpoint, Error))
		{
			return FString();
		}
		return Root;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeAPIProductionTest, "Epicode.API.Production", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeAPIProductionTest::RunTest(const FString& Parameters)
{
	const FString ProductionId = TEXT("api-production-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	TSharedPtr<FJsonObject> CreateParams = MakeShared<FJsonObject>();
	CreateParams->SetStringField(TEXT("sceneId"), TEXT("api-scene"));
	CreateParams->SetStringField(TEXT("name"), TEXT("API Production"));
	CreateParams->SetStringField(TEXT("productionId"), ProductionId);
	FEpicodeCommandResult Created = ExecuteCommand(TEXT("production.create"), CreateParams);
	TestTrue(TEXT("production create"), Created.bSuccess);
	if (Created.bSuccess && Created.Result.IsValid())
	{
		TestEqual(TEXT("production id"), Created.Result->GetStringField(TEXT("productionId")), ProductionId);
	}

	FEpicodeCommandResult MissingScene = ExecuteCommand(TEXT("production.create"), MakeShared<FJsonObject>());
	TestFalse(TEXT("missing scene rejected"), MissingScene.bSuccess);
	TestEqual(TEXT("missing scene code"), MissingScene.ErrorCode, FString(TEXT("INVALID_PARAMS")));

	TSharedPtr<FJsonObject> OpenParams = MakeShared<FJsonObject>();
	OpenParams->SetStringField(TEXT("productionId"), ProductionId);
	FEpicodeCommandResult Opened = ExecuteCommand(TEXT("production.open"), OpenParams);
	TestTrue(TEXT("production open"), Opened.bSuccess);
	if (Opened.Result.IsValid())
	{
		TSharedPtr<FJsonObject> Document = MakeShared<FJsonObject>();
		const TArray<TSharedPtr<FJsonValue>>* Cast = nullptr;
		const TSharedPtr<FJsonObject>* WorldRules = nullptr;
		const TSharedPtr<FJsonObject>* Narrative = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Cameras = nullptr;
		Opened.Result->TryGetArrayField(TEXT("cast"), Cast);
		Opened.Result->TryGetObjectField(TEXT("worldRules"), WorldRules);
		Opened.Result->TryGetObjectField(TEXT("narrative"), Narrative);
		Opened.Result->TryGetArrayField(TEXT("cameraPresets"), Cameras);
		Document->SetStringField(TEXT("productionId"), ProductionId);
		Document->SetStringField(TEXT("name"), TEXT("API Production Renamed"));
		Document->SetStringField(TEXT("sceneId"), TEXT("api-scene"));
		Document->SetNumberField(TEXT("schemaVersion"), FEpicodeProductionDocument::CurrentSchemaVersion);
		if (Cast) Document->SetArrayField(TEXT("cast"), *Cast);
		if (WorldRules) Document->SetObjectField(TEXT("worldRules"), *WorldRules);
		if (Narrative) Document->SetObjectField(TEXT("narrative"), *Narrative);
		if (Cameras) Document->SetArrayField(TEXT("cameraPresets"), *Cameras);
		Document->SetObjectField(TEXT("extensions"), MakeShared<FJsonObject>());
		TSharedPtr<FJsonObject> SaveParams = MakeShared<FJsonObject>();
		SaveParams->SetStringField(TEXT("productionId"), ProductionId);
		SaveParams->SetObjectField(TEXT("document"), Document);
		FEpicodeCommandResult Saved = ExecuteCommand(TEXT("production.save"), SaveParams);
		TestTrue(TEXT("production save"), Saved.bSuccess);
		FEpicodeCommandResult Renamed = ExecuteCommand(TEXT("production.open"), OpenParams);
		if (Renamed.Result.IsValid())
		{
			TestEqual(TEXT("production save persists"), Renamed.Result->GetStringField(TEXT("name")), FString(TEXT("API Production Renamed")));
		}
	}

	FEpicodeCommandResult Listed = ExecuteCommand(TEXT("production.list"), nullptr);
	TestTrue(TEXT("production list"), Listed.bSuccess);
	if (Listed.Result.IsValid())
	{
		TestTrue(TEXT("production list count"), Listed.Result->GetIntegerField(TEXT("count")) >= 1);
	}

	TSharedPtr<FJsonObject> Rules = FEpicodeProductionDocument::MakeDefaultWorldRules();
	const TSharedPtr<FJsonObject>* Budgets = nullptr;
	if (Rules->TryGetObjectField(TEXT("budgets"), Budgets) && Budgets)
	{
		(*Budgets)->SetNumberField(TEXT("maxEntities"), 42);
	}
	TSharedPtr<FJsonObject> RulesParams = MakeShared<FJsonObject>();
	RulesParams->SetStringField(TEXT("productionId"), ProductionId);
	RulesParams->SetObjectField(TEXT("rules"), Rules);
	FEpicodeCommandResult Updated = ExecuteCommand(TEXT("sandbox.rules.update"), RulesParams);
	TestTrue(TEXT("rules update"), Updated.bSuccess);
	FEpicodeCommandResult Reopened = ExecuteCommand(TEXT("production.open"), OpenParams);
	if (Reopened.bSuccess && Reopened.Result.IsValid())
	{
		const TSharedPtr<FJsonObject>* WorldRules = nullptr;
		const TSharedPtr<FJsonObject>* SavedBudgets = nullptr;
		TestTrue(TEXT("rules saved"), Reopened.Result->TryGetObjectField(TEXT("worldRules"), WorldRules) && WorldRules
			&& (*WorldRules)->TryGetObjectField(TEXT("budgets"), SavedBudgets) && SavedBudgets);
		if (SavedBudgets && SavedBudgets->IsValid())
		{
			TestEqual(TEXT("rules budget persisted"), (*SavedBudgets)->GetIntegerField(TEXT("maxEntities")), 42);
		}
	}

	TSharedPtr<FJsonObject> DeleteParams = MakeShared<FJsonObject>();
	DeleteParams->SetStringField(TEXT("productionId"), ProductionId);
	FEpicodeCommandResult Deleted = ExecuteCommand(TEXT("production.delete"), DeleteParams);
	TestTrue(TEXT("production delete"), Deleted.bSuccess);
	FEpicodeCommandResult Missing = ExecuteCommand(TEXT("production.open"), OpenParams);
	TestFalse(TEXT("deleted production missing"), Missing.bSuccess);
	TestEqual(TEXT("deleted production code"), Missing.ErrorCode, FString(TEXT("PRODUCTION_NOT_FOUND")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeAPISandboxTest, "Epicode.API.Sandbox", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeAPISandboxTest::RunTest(const FString& Parameters)
{
	const FString ProductionId = TEXT("api-sandbox-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString SceneId = MakeSceneAndProduction(TEXT("API Sandbox Scene"), ProductionId);
	if (!TestFalse(TEXT("sandbox scene created"), SceneId.IsEmpty()))
	{
		return false;
	}
	UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
	if (!TestNotNull(TEXT("network subsystem"), Subsystem))
	{
		CleanupScene(SceneId);
		CleanupProduction(ProductionId);
		return false;
	}
	FString Error;
	const bool bCreated = Subsystem->CreateSession(SceneId, TEXT("api-owner"), TEXT("API Owner"), 4, 30.0, Error, 2);
	TestTrue(TEXT("sandbox session created"), bCreated);
	if (!bCreated)
	{
		CleanupScene(SceneId);
		CleanupProduction(ProductionId);
		return false;
	}
	TestNotNull(TEXT("sandbox bridge active"), Subsystem->GetSandboxBridge());

	FEpicodeCommandResult State = ExecuteCommand(TEXT("sandbox.state"), nullptr);
	TestTrue(TEXT("sandbox state"), State.bSuccess);
	if (State.Result.IsValid())
	{
		TestFalse(TEXT("sandbox digest"), State.Result->GetStringField(TEXT("digest")).IsEmpty());
		TestTrue(TEXT("sandbox replayable"), State.Result->GetBoolField(TEXT("replayable")));
	}

	FEpicodeCommandResult Rules = ExecuteCommand(TEXT("sandbox.rules.get"), nullptr);
	TestTrue(TEXT("sandbox rules"), Rules.bSuccess);
	FEpicodeCommandResult Budget = ExecuteCommand(TEXT("sandbox.budget"), nullptr);
	TestTrue(TEXT("sandbox budget"), Budget.bSuccess);

	TSharedPtr<FJsonObject> SpawnParams = MakeShared<FJsonObject>();
	SpawnParams->SetStringField(TEXT("actorId"), TEXT("api-owner"));
	SpawnParams->SetStringField(TEXT("entityId"), TEXT("api-spawned-prop"));
	SpawnParams->SetStringField(TEXT("kind"), TEXT("prop"));
	FEpicodeCommandResult Spawned = ExecuteCommand(TEXT("sandbox.spawn"), SpawnParams);
	TestTrue(TEXT("sandbox spawn"), Spawned.bSuccess);
	TSharedPtr<FJsonObject> InvalidSpawn = MakeShared<FJsonObject>();
	InvalidSpawn->SetStringField(TEXT("actorId"), TEXT("api-owner"));
	InvalidSpawn->SetStringField(TEXT("kind"), TEXT("not-a-kind"));
	FEpicodeCommandResult InvalidSpawnResult = ExecuteCommand(TEXT("sandbox.spawn"), InvalidSpawn);
	TestFalse(TEXT("invalid spawn rejected"), InvalidSpawnResult.bSuccess);
	TestEqual(TEXT("invalid spawn code"), InvalidSpawnResult.ErrorCode, FString(TEXT("INVALID_PARAMS")));

	FString ViewerId;
	TSharedPtr<FJsonObject> JoinParams = MakeShared<FJsonObject>();
	JoinParams->SetStringField(TEXT("playerId"), TEXT("api-viewer"));
	JoinParams->SetStringField(TEXT("role"), TEXT("observer"));
	JoinParams->SetStringField(TEXT("participantKind"), TEXT("viewer"));
	FEpicodeCommandResult Joined = ExecuteCommand(TEXT("session.join"), JoinParams);
	TestTrue(TEXT("viewer joined"), Joined.bSuccess);
	if (Joined.bSuccess && Joined.Result.IsValid())
	{
		ViewerId = Joined.Result->GetStringField(TEXT("playerId"));
	}
	TestFalse(TEXT("viewer id is returned"), ViewerId.IsEmpty());
	if (!ViewerId.IsEmpty())
	{
		TSharedPtr<FJsonObject> ViewerSpawn = MakeShared<FJsonObject>();
		ViewerSpawn->SetStringField(TEXT("actorId"), ViewerId);
		ViewerSpawn->SetStringField(TEXT("entityId"), TEXT("viewer-prop"));
		FEpicodeCommandResult Denied = ExecuteCommand(TEXT("sandbox.spawn"), ViewerSpawn);
		TestFalse(TEXT("viewer cannot spawn"), Denied.bSuccess);
		TestEqual(TEXT("viewer rejection code"), Denied.ErrorCode, FString(TEXT("VIEWER_READ_ONLY")));
		TSharedPtr<FJsonObject> ViewerProduction = MakeShared<FJsonObject>();
		ViewerProduction->SetStringField(TEXT("sceneId"), TEXT("api-scene"));
		ViewerProduction->SetStringField(TEXT("actorId"), ViewerId);
		FEpicodeCommandResult ViewerAuthoring = ExecuteCommand(TEXT("production.create"), ViewerProduction);
		TestFalse(TEXT("viewer cannot author"), ViewerAuthoring.bSuccess);
		TestEqual(TEXT("viewer authoring code"), ViewerAuthoring.ErrorCode, FString(TEXT("VIEWER_READ_ONLY")));
	}

	CleanupSession();
	CleanupScene(SceneId);
	CleanupProduction(ProductionId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeAPIDirectorTest, "Epicode.API.Director", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeAPIDirectorTest::RunTest(const FString& Parameters)
{
	const FString ProductionId = TEXT("api-director-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString SceneId = MakeSceneAndProduction(TEXT("API Director Scene"), ProductionId);
	if (!TestFalse(TEXT("director scene created"), SceneId.IsEmpty()))
	{
		return false;
	}
	UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
	if (!TestNotNull(TEXT("network subsystem"), Subsystem))
	{
		CleanupScene(SceneId);
		CleanupProduction(ProductionId);
		return false;
	}
	FString Error;
	if (!TestTrue(TEXT("director session created"), Subsystem->CreateSession(SceneId, TEXT("api-director"), TEXT("Director"), 4, 30.0, Error, 2)))
	{
		CleanupScene(SceneId);
		CleanupProduction(ProductionId);
		return false;
	}

	FEpicodeCommandResult Status = ExecuteCommand(TEXT("director.status"), nullptr);
	TestTrue(TEXT("director status"), Status.bSuccess);
	if (Status.Result.IsValid())
	{
		TestFalse(TEXT("director not paused"), Status.Result->GetBoolField(TEXT("paused")));
	}

	TSharedPtr<FJsonObject> ActorParams = MakeShared<FJsonObject>();
	ActorParams->SetStringField(TEXT("actorId"), TEXT("api-director"));
	FEpicodeCommandResult Paused = ExecuteCommand(TEXT("director.pause"), ActorParams);
	TestTrue(TEXT("director pause"), Paused.bSuccess);
	FEpicodeCommandResult PausedStatus = ExecuteCommand(TEXT("director.status"), nullptr);
	TestTrue(TEXT("director paused status"), PausedStatus.bSuccess);
	if (PausedStatus.Result.IsValid())
	{
		TestTrue(TEXT("director is paused"), PausedStatus.Result->GetBoolField(TEXT("paused")));
	}
	FEpicodeCommandResult Resumed = ExecuteCommand(TEXT("director.resume"), ActorParams);
	TestTrue(TEXT("director resume"), Resumed.bSuccess);

	TSharedPtr<FJsonObject> ScaleParams = MakeShared<FJsonObject>();
	ScaleParams->SetStringField(TEXT("actorId"), TEXT("api-director"));
	ScaleParams->SetNumberField(TEXT("scale"), 0.5);
	FEpicodeCommandResult Scaled = ExecuteCommand(TEXT("director.setTimeScale"), ScaleParams);
	TestTrue(TEXT("director scale"), Scaled.bSuccess);

	TSharedPtr<FJsonObject> BeatParams = MakeShared<FJsonObject>();
	BeatParams->SetStringField(TEXT("actorId"), TEXT("api-director"));
	BeatParams->SetStringField(TEXT("beatId"), TEXT("missing-beat"));
	FEpicodeCommandResult MissingBeat = ExecuteCommand(TEXT("director.enterBeat"), BeatParams);
	TestFalse(TEXT("missing beat rejected"), MissingBeat.bSuccess);
	TestTrue(TEXT("missing beat code"), MissingBeat.ErrorCode.Contains(TEXT("DIRECTOR_BEAT_NOT_FOUND")));

	TSharedPtr<FJsonObject> PerformerJoin = MakeShared<FJsonObject>();
	PerformerJoin->SetStringField(TEXT("playerId"), TEXT("api-performer"));
	PerformerJoin->SetStringField(TEXT("role"), TEXT("performer"));
	TestTrue(TEXT("performer joins"), ExecuteCommand(TEXT("session.join"), PerformerJoin).bSuccess);
	TSharedPtr<FJsonObject> PerformerPause = MakeShared<FJsonObject>();
	PerformerPause->SetStringField(TEXT("actorId"), TEXT("api-performer"));
	FEpicodeCommandResult PerformerDenied = ExecuteCommand(TEXT("director.pause"), PerformerPause);
	TestFalse(TEXT("performer cannot direct"), PerformerDenied.bSuccess);
	TestEqual(TEXT("performer rejection code"), PerformerDenied.ErrorCode, FString(TEXT("FORBIDDEN")));

	TSharedPtr<FJsonObject> ViewerJoin = MakeShared<FJsonObject>();
	ViewerJoin->SetStringField(TEXT("playerId"), TEXT("api-director-viewer"));
	ViewerJoin->SetStringField(TEXT("role"), TEXT("observer"));
	ViewerJoin->SetStringField(TEXT("participantKind"), TEXT("viewer"));
	TestTrue(TEXT("director viewer joins"), ExecuteCommand(TEXT("session.join"), ViewerJoin).bSuccess);
	TSharedPtr<FJsonObject> ViewerPause = MakeShared<FJsonObject>();
	ViewerPause->SetStringField(TEXT("actorId"), TEXT("api-director-viewer"));
	FEpicodeCommandResult ViewerDenied = ExecuteCommand(TEXT("director.pause"), ViewerPause);
	TestFalse(TEXT("viewer cannot direct"), ViewerDenied.bSuccess);
	TestEqual(TEXT("viewer director code"), ViewerDenied.ErrorCode, FString(TEXT("VIEWER_READ_ONLY")));

	CleanupSession();
	CleanupScene(SceneId);
	CleanupProduction(ProductionId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeAPIReplayTest, "Epicode.API.Replay", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeAPIReplayTest::RunTest(const FString& Parameters)
{
	const FString Root = MakeReplayRoot();
	if (!TestFalse(TEXT("replay journal created"), Root.IsEmpty()))
	{
		return false;
	}
	TSharedPtr<FJsonObject> GhostParams = MakeShared<FJsonObject>();
	GhostParams->SetStringField(TEXT("root"), Root);
	GhostParams->SetNumberField(TEXT("startSequence"), 1);
	GhostParams->SetNumberField(TEXT("count"), 10);
	FEpicodeCommandResult Ghost = ExecuteCommand(TEXT("replay.ghost"), GhostParams);
	TestTrue(TEXT("replay ghost"), Ghost.bSuccess);
	if (Ghost.Result.IsValid())
	{
		TestEqual(TEXT("ghost event count"), Ghost.Result->GetArrayField(TEXT("events")).Num(), 1);
	}

	TSharedPtr<FJsonObject> JobParams = MakeShared<FJsonObject>();
	JobParams->SetStringField(TEXT("root"), Root);
	JobParams->SetNumberField(TEXT("targetSequence"), 1);
	FEpicodeCommandResult Job = ExecuteCommand(TEXT("replay.job"), JobParams);
	TestTrue(TEXT("replay job"), Job.bSuccess);
	if (Job.Result.IsValid())
	{
		TestEqual(TEXT("replay job state"), Job.Result->GetStringField(TEXT("state")), FString(TEXT("completed")));
		TSharedPtr<FJsonObject> CancelParams = MakeShared<FJsonObject>();
		CancelParams->SetStringField(TEXT("jobId"), Job.Result->GetStringField(TEXT("id")));
		FEpicodeCommandResult Cancel = ExecuteCommand(TEXT("replay.cancel"), CancelParams);
		TestFalse(TEXT("completed job cannot cancel"), Cancel.bSuccess);
		TestEqual(TEXT("cancel terminal code"), Cancel.ErrorCode, FString(TEXT("REPLAY_JOB_TERMINAL")));
	}

	const FString BranchRoot = Root + TEXT("-branch");
	TSharedPtr<FJsonObject> BranchParams = MakeShared<FJsonObject>();
	BranchParams->SetStringField(TEXT("parentRoot"), Root);
	BranchParams->SetStringField(TEXT("branchRoot"), BranchRoot);
	BranchParams->SetStringField(TEXT("branchId"), TEXT("api-branch"));
	BranchParams->SetNumberField(TEXT("forkSequence"), 1);
	FEpicodeCommandResult Branch = ExecuteCommand(TEXT("replay.branch"), BranchParams);
	TestTrue(TEXT("replay branch"), Branch.bSuccess);
	TestTrue(TEXT("branch metadata exists"), IFileManager::Get().FileExists(*FEpicodeReplayBranch::GetBranchMetadataPath(BranchRoot)));

	const FString TakeRoot = Root + TEXT("-takes");
	FEpicodeDirectorTake TakeFixture;
	TakeFixture.Id = TEXT("api-take");
	TakeFixture.Version = 1;
	TakeFixture.InSequence = 1;
	TakeFixture.OutSequence = 2;
	TakeFixture.InTick = 0;
	TakeFixture.OutTick = 1;
	TakeFixture.BeatId = TEXT("beat-1");
	TakeFixture.ResolutionPreset = TEXT("1080p");
	TakeFixture.ContentDigest = TEXT("content-digest");
	TakeFixture.Note = TEXT("API take");
	TakeFixture.Checksum = TakeFixture.ComputeChecksum();
	TSharedPtr<FJsonObject> TakeJson = TakeFixture.ToJson();
	TSharedPtr<FJsonObject> CreateTakeParams = MakeShared<FJsonObject>();
	CreateTakeParams->SetStringField(TEXT("root"), TakeRoot);
	CreateTakeParams->SetObjectField(TEXT("take"), TakeJson);
	FEpicodeCommandResult TakeCreated = ExecuteCommand(TEXT("take.create"), CreateTakeParams);
	TestTrue(TEXT("take create"), TakeCreated.bSuccess);
	TSharedPtr<FJsonObject> ReviseParams = MakeShared<FJsonObject>();
	ReviseParams->SetStringField(TEXT("root"), TakeRoot);
	ReviseParams->SetStringField(TEXT("takeId"), TEXT("api-take"));
	FEpicodeCommandResult TakeRevised = ExecuteCommand(TEXT("take.revise"), ReviseParams);
	TestTrue(TEXT("take revise"), TakeRevised.bSuccess);
	if (TakeRevised.Result.IsValid())
	{
		TestEqual(TEXT("take version increments"), TakeRevised.Result->GetIntegerField(TEXT("version")), 2);
	}
	TSharedPtr<FJsonObject> ListTakeParams = MakeShared<FJsonObject>();
	ListTakeParams->SetStringField(TEXT("root"), TakeRoot);
	ListTakeParams->SetStringField(TEXT("takeId"), TEXT("api-take"));
	FEpicodeCommandResult TakeList = ExecuteCommand(TEXT("take.list"), ListTakeParams);
	TestTrue(TEXT("take list"), TakeList.bSuccess);
	if (TakeList.Result.IsValid())
	{
		TestEqual(TEXT("take version count"), TakeList.Result->GetArrayField(TEXT("versions")).Num(), 2);
	}

	IFileManager::Get().DeleteDirectory(*Root, false, true);
	IFileManager::Get().DeleteDirectory(*BranchRoot, false, true);
	IFileManager::Get().DeleteDirectory(*TakeRoot, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeAPIEventsTest, "Epicode.API.Events", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeAPIEventsTest::RunTest(const FString& Parameters)
{
	FEpicodeEventFilter Filter = FEpicodeEventFilter::FromQueryString(TEXT("/api/v1/ws?since=4&filter.session=scene-1&filter.participant=agent-7&filter.category=director"));
	TestEqual(TEXT("event filter session"), Filter.SessionId, FString(TEXT("scene-1")));
	TestEqual(TEXT("event filter participant"), Filter.ParticipantId, FString(TEXT("agent-7")));
	TestEqual(TEXT("event filter category"), Filter.Categories.Num(), 1);
	TestTrue(TEXT("event gap detected"), FEpicodeEventFilter::NeedsGap(4, 10));
	TestFalse(TEXT("live tail has no gap"), FEpicodeEventFilter::NeedsGap(0, 10));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeAPIMethodsTest, "Epicode.API.Methods", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeAPIMethodsTest::RunTest(const FString& Parameters)
{
	FEpicodeCommandResult Methods = ExecuteCommand(TEXT("system.methods"), nullptr);
	TestTrue(TEXT("methods succeed"), Methods.bSuccess);
	if (!Methods.Result.IsValid())
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Methods.Result->TryGetArrayField(TEXT("methods"), Values) || !Values)
	{
		return false;
	}
	TSet<FString> Names;
	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		const TSharedPtr<FJsonObject> Method = Value->AsObject();
		if (Method.IsValid())
		{
			Names.Add(Method->GetStringField(TEXT("name")));
		}
	}
	TestTrue(TEXT("production methods registered"), Names.Contains(TEXT("production.create")));
	TestTrue(TEXT("sandbox methods registered"), Names.Contains(TEXT("sandbox.state")));
	TestTrue(TEXT("director methods registered"), Names.Contains(TEXT("director.pause")));
	TestTrue(TEXT("replay methods registered"), Names.Contains(TEXT("replay.job")));
	TestTrue(TEXT("take methods registered"), Names.Contains(TEXT("take.create")));
	FEpicodeCommandResult Health = ExecuteCommand(TEXT("system.health"), nullptr);
	TestTrue(TEXT("health succeeds"), Health.bSuccess);
	return true;
}

#endif
