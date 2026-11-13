#include "Misc/AutomationTest.h"

#include "EpicodeStudioDirectorModel.h"
#include "SEpicodeDirectorWorkspace.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	TSharedPtr<FJsonObject> MakeSandboxSnapshot()
	{
		TSharedPtr<FJsonObject> Snapshot = MakeShared<FJsonObject>();
		Snapshot->SetStringField(TEXT("productionId"), TEXT("prod-studio"));
		Snapshot->SetStringField(TEXT("state"), TEXT("running"));
		Snapshot->SetStringField(TEXT("digest"), TEXT("abc123"));
		Snapshot->SetBoolField(TEXT("replayable"), true);
		Snapshot->SetObjectField(TEXT("budgets"), []()
		{
			TSharedPtr<FJsonObject> Budgets = MakeShared<FJsonObject>();
			Budgets->SetNumberField(TEXT("maxEntities"), 100);
			Budgets->SetNumberField(TEXT("maxEventsPerSecond"), 250);
			return Budgets;
		}());
		Snapshot->SetArrayField(TEXT("warnings"), { MakeShared<FJsonValueString>(TEXT("budget near limit")) });
		return Snapshot;
	}

	TSharedPtr<FJsonObject> MakeDirectorSnapshot()
	{
		TSharedPtr<FJsonObject> Snapshot = MakeShared<FJsonObject>();
		Snapshot->SetBoolField(TEXT("paused"), false);
		Snapshot->SetStringField(TEXT("beatId"), TEXT("beat-open"));
		Snapshot->SetStringField(TEXT("objectiveId"), TEXT("obj-find-key"));
		Snapshot->SetArrayField(TEXT("messages"), { MakeShared<FJsonValueString>(TEXT("Cue fired")) });
		TArray<TSharedPtr<FJsonValue>> Events;
		TSharedPtr<FJsonObject> Event = MakeShared<FJsonObject>();
		Event->SetNumberField(TEXT("sequence"), 4);
		Event->SetStringField(TEXT("type"), TEXT("director.paused"));
		Event->SetStringField(TEXT("actorId"), TEXT("director-1"));
		Event->SetStringField(TEXT("beatId"), TEXT("beat-open"));
		Events.Add(MakeShared<FJsonValueObject>(Event));
		Snapshot->SetArrayField(TEXT("events"), Events);
		return Snapshot;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeStudioSandboxPanelTest, "Epicode.Studio.SandboxPanel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeStudioSandboxPanelTest::RunTest(const FString& Parameters)
{
	TSharedRef<SEpicodeDirectorWorkspace> Panel = SNew(SEpicodeDirectorWorkspace);
	Panel->SetSandboxSnapshot(MakeSandboxSnapshot());
	const FEpicodeStudioDirectorModel& Model = Panel->GetModel();
	TestTrue(TEXT("production detected"), Model.HasProduction());
	TestTrue(TEXT("sandbox active"), Model.IsSandboxActive());
	TestEqual(TEXT("production id"), Model.GetProductionId(), FString(TEXT("prod-studio")));
	TestTrue(TEXT("budget lines"), Model.GetBudgetLines().Num() >= 2);
	TestTrue(TEXT("validation warning"), Model.GetValidationWarnings().Num() == 1);
	TestTrue(TEXT("budget line contains maxEntities"), Model.GetBudgetLines().ContainsByPredicate([](const FString& Line) { return Line.Contains(TEXT("maxEntities")); }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeStudioDirectorWorkspaceTest, "Epicode.Studio.DirectorWorkspace", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeStudioDirectorWorkspaceTest::RunTest(const FString& Parameters)
{
	TSharedRef<SEpicodeDirectorWorkspace> Panel = SNew(SEpicodeDirectorWorkspace);
	Panel->SetSandboxSnapshot(MakeSandboxSnapshot());
	Panel->SetDirectorSnapshot(MakeDirectorSnapshot());
	FEpicodeStudioDirectorModel& Model = Panel->GetMutableModel();
	TestEqual(TEXT("default tab"), Model.GetActiveTab(), EEpicodeDirectorTab::Overview);
	Model.SetActiveTab(EEpicodeDirectorTab::Timeline);
	TestEqual(TEXT("tab changes"), Model.GetActiveTab(), EEpicodeDirectorTab::Timeline);
	Model.SetZoom(100.0);
	TestEqual(TEXT("zoom clamped high"), Model.GetZoom(), 8.0);
	Model.SetZoom(0.01);
	TestEqual(TEXT("zoom clamped low"), Model.GetZoom(), 0.25);
	Model.SetZoom(2.0);
	TestEqual(TEXT("zoom applied"), Model.GetZoom(), 2.0);
	Model.SetPlayhead(7);
	TestEqual(TEXT("playhead"), Model.GetPlayhead(), static_cast<int64>(7));
	Model.SetEventFilter(TEXT("paused"));
	TestEqual(TEXT("filtered events"), Model.GetFilteredEvents().Num(), 1);
	Model.SetEventFilter(TEXT("missing"));
	TestEqual(TEXT("empty filter result"), Model.GetFilteredEvents().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeStudioBeatGraphTest, "Epicode.Studio.BeatGraph", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeStudioBeatGraphTest::RunTest(const FString& Parameters)
{
	FEpicodeStudioDirectorModel Model;
	TestFalse(TEXT("empty model has no production"), Model.HasProduction());
	TestTrue(TEXT("empty director snapshot applies"), Model.ApplyDirectorSnapshot(MakeDirectorSnapshot()));
	TestEqual(TEXT("current beat"), Model.GetCurrentBeatId(), FString(TEXT("beat-open")));
	TestEqual(TEXT("current objective"), Model.GetCurrentObjectiveId(), FString(TEXT("obj-find-key")));
	TestEqual(TEXT("cue line"), Model.GetCueLines().Num(), 1);
	TestEqual(TEXT("event row"), Model.GetEvents().Num(), 1);
	TestEqual(TEXT("event type"), Model.GetEvents()[0].Type, FString(TEXT("director.paused")));
	TestFalse(TEXT("invalid snapshot rejected"), Model.ApplyDirectorSnapshot(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeStudioDirectorControlsTest, "Epicode.Studio.DirectorControls", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeStudioDirectorControlsTest::RunTest(const FString& Parameters)
{
	FEpicodeStudioDirectorModel Model;
	Model.SetRole(TEXT("observer"));
	TestTrue(TEXT("observer read only"), Model.IsReadOnly());
	TestFalse(TEXT("observer cannot control"), Model.CanControl());
	TestFalse(TEXT("observer cannot enter ghost"), Model.EnterGhostMode());
	TestTrue(TEXT("observer error"), Model.GetLastError().Contains(TEXT("VIEWER_READ_ONLY")));
	Model.SetRole(TEXT("performer"));
	TestFalse(TEXT("performer cannot control"), Model.CanControl());
	Model.SetRole(TEXT("director"));
	TestTrue(TEXT("director can control"), Model.CanControl());
	TestTrue(TEXT("director ghost mode"), Model.EnterGhostMode());
	TestTrue(TEXT("ghost mode"), Model.IsGhostMode());
	TestTrue(TEXT("director acquires takeover"), Model.AcquireTakeover(TEXT("performer-1"), TEXT("director-1")));
	TestEqual(TEXT("takeover owner"), Model.GetTakeoverDirector(TEXT("performer-1")), FString(TEXT("director-1")));
	TestTrue(TEXT("release takeover"), Model.ReleaseTakeover(TEXT("performer-1")));
	TestTrue(TEXT("back to live"), Model.EnterLiveMode());
	TestFalse(TEXT("live is not ghost"), Model.IsGhostMode());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeStudioReplayControlsTest, "Epicode.Studio.ReplayControls", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeStudioReplayControlsTest::RunTest(const FString& Parameters)
{
	FEpicodeStudioDirectorModel Model;
	Model.SetRole(TEXT("director"));
	Model.SetLatestTakeVersion(3);
	TestTrue(TEXT("select take"), Model.SelectTake(TEXT("take-1"), 2));
	TestEqual(TEXT("take id"), Model.GetSelectedTakeId(), FString(TEXT("take-1")));
	TestEqual(TEXT("take version"), Model.GetSelectedTakeVersion(), 2);
	TestFalse(TEXT("missing take rejected"), Model.SelectTake(TEXT("take-1"), 4));
	TestTrue(TEXT("missing take error"), Model.GetLastError().Contains(TEXT("TAKE_NOT_FOUND")));
	TestFalse(TEXT("empty take rejected"), Model.SelectTake(FString(), 1));
	TestTrue(TEXT("resim mode"), Model.EnterResimMode());
	Model.SetBranchId(TEXT("branch-1"));
	TestEqual(TEXT("branch id"), Model.GetBranchId(), FString(TEXT("branch-1")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeStudioDirectorResponsivenessTest, "Epicode.Studio.DirectorResponsiveness", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeStudioDirectorResponsivenessTest::RunTest(const FString& Parameters)
{
	FEpicodeStudioDirectorModel Model;
	TestEqual(TEXT("frame budget"), Model.GetFrameBudgetMs(), 100.0);
	Model.SetLastApplyMs(12.5);
	TestTrue(TEXT("within budget"), Model.MeetsResponsivenessBudget());
	Model.SetLastApplyMs(101.0);
	TestFalse(TEXT("over budget"), Model.MeetsResponsivenessBudget());
	Model.Reset();
	TestEqual(TEXT("reset clears production"), Model.GetProductionId(), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeStudioDocsTest, "Epicode.Studio.Docs", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeStudioDocsTest::RunTest(const FString& Parameters)
{
	FString StudioDoc;
	FString AcceptanceDoc;
	const FString DocsRoot = FPaths::Combine(FPaths::ProjectDir(), TEXT("../Docs"));
	const bool bStudio = FFileHelper::LoadFileToString(StudioDoc, *FPaths::Combine(DocsRoot, TEXT("Studio.md")));
	const bool bAcceptance = FFileHelper::LoadFileToString(AcceptanceDoc, *FPaths::Combine(DocsRoot, TEXT("StudioAcceptance.md")));
	TestTrue(TEXT("Studio docs exist"), bStudio);
	TestTrue(TEXT("acceptance docs exist"), bAcceptance);
	if (bStudio)
	{
		TestTrue(TEXT("studio docs mention sandbox"), StudioDoc.Contains(TEXT("Sandbox")));
		TestTrue(TEXT("studio docs mention director"), StudioDoc.Contains(TEXT("Director")));
		TestTrue(TEXT("studio docs mention replay"), StudioDoc.Contains(TEXT("Replay")));
	}
	if (bAcceptance)
	{
		TestTrue(TEXT("acceptance mentions workspace"), AcceptanceDoc.Contains(TEXT("Director")));
	}
	return true;
}

#endif
