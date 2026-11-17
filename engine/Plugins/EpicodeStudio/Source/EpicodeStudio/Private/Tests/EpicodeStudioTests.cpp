#include "Misc/AutomationTest.h"

#include "Editor.h"
#include "Editor/Transactor.h"
#include "EpicodeCommand.h"
#include "EpicodeEngineSubsystem.h"
#include "EpicodeScopedTestSceneRoot.h"
#include "EpicodeSceneSubsystem.h"
#include "EpicodeStudioViewModel.h"
#include "Misc/Paths.h"
#include "SEpicodeStudioPanel.h"
#include "SEpicodeMultiplayerPanel.h"
#include "EpicodeMultiplayerViewModel.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeUndoTest, "Epicode.Core.Undo", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeUndoTest::RunTest(const FString& Parameters)
{
	if (!GEditor || !GEditor->Trans)
	{
		AddError(TEXT("Editor transaction system is unavailable."));
		return false;
	}
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
	if (!Engine || !Scene)
	{
		AddError(TEXT("Epicode subsystems are unavailable."));
		return false;
	}
	FString Error;
	if (Scene->HasScene())
	{
		Scene->CloseScene(true, Error);
	}
	if (!Scene->NewScene(TEXT("Undo Test"), Error))
	{
		AddError(Error);
		return false;
	}
	const int32 Baseline = Scene->GetDocument().Entities.Num();

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("type"), TEXT("group"));
	Params->SetStringField(TEXT("name"), TEXT("Undo Group"));
	FEpicodeCommandContext Context;
	Context.bUndoable = true;
	const FEpicodeCommandResult Created = Engine->ExecuteCommand(TEXT("entity.create"), Params, Context);
	TestTrue(TEXT("undoable command succeeded"), Created.bSuccess);
	TestEqual(TEXT("entity created"), Scene->GetDocument().Entities.Num(), Baseline + 1);
	TestTrue(TEXT("editor recorded a transaction"), GEditor->Trans->CanUndo());

	TestTrue(TEXT("undo executed"), GEditor->Trans->Undo());
	TestEqual(TEXT("undo removed entity"), Scene->GetDocument().Entities.Num(), Baseline);
	TestTrue(TEXT("redo executed"), GEditor->Trans->Redo());
	TestEqual(TEXT("redo restored entity"), Scene->GetDocument().Entities.Num(), Baseline + 1);

	const FString EntityId = Created.Result->GetObjectField(TEXT("entity"))->GetStringField(TEXT("id"));
	FGuid EntityGuid;
	TestTrue(TEXT("entity id parses"), FGuid::Parse(EntityId, EntityGuid));

	TSharedPtr<FJsonObject> Transform = MakeShared<FJsonObject>();
	Transform->SetArrayField(TEXT("location"), {
		MakeShared<FJsonValueNumber>(100.0),
		MakeShared<FJsonValueNumber>(0.0),
		MakeShared<FJsonValueNumber>(0.0)
	});
	Transform->SetArrayField(TEXT("rotation"), {
		MakeShared<FJsonValueNumber>(0.0),
		MakeShared<FJsonValueNumber>(0.0),
		MakeShared<FJsonValueNumber>(0.0)
	});
	Transform->SetArrayField(TEXT("scale"), {
		MakeShared<FJsonValueNumber>(1.0),
		MakeShared<FJsonValueNumber>(1.0),
		MakeShared<FJsonValueNumber>(1.0)
	});
	TSharedPtr<FJsonObject> TransformParams = MakeShared<FJsonObject>();
	TransformParams->SetStringField(TEXT("id"), EntityId);
	TransformParams->SetObjectField(TEXT("transform"), Transform);
	TestTrue(TEXT("transform command succeeded"), Engine->ExecuteCommand(TEXT("entity.setTransform"), TransformParams, Context).bSuccess);
	const FEpicodeEntityRecord* Record = Scene->FindEntity(EntityGuid);
	TestTrue(TEXT("transform applied"), Record && Record->Transform.Location.Equals(FVector(100.0, 0.0, 0.0)));
	TestTrue(TEXT("transform undo executed"), GEditor->Trans->Undo());
	Record = Scene->FindEntity(EntityGuid);
	TestTrue(TEXT("transform undone"), Record && Record->Transform.Location.Equals(FVector::ZeroVector));
	TestTrue(TEXT("transform redo executed"), GEditor->Trans->Redo());
	Record = Scene->FindEntity(EntityGuid);
	TestTrue(TEXT("transform redone"), Record && Record->Transform.Location.Equals(FVector(100.0, 0.0, 0.0)));

	TSharedPtr<FJsonObject> DeleteParams = MakeShared<FJsonObject>();
	DeleteParams->SetStringField(TEXT("id"), EntityId);
	TestTrue(TEXT("delete command succeeded"), Engine->ExecuteCommand(TEXT("entity.delete"), DeleteParams, Context).bSuccess);
	TestEqual(TEXT("delete removed entity"), Scene->GetDocument().Entities.Num(), Baseline);
	TestTrue(TEXT("delete undo executed"), GEditor->Trans->Undo());
	TestEqual(TEXT("delete undone"), Scene->GetDocument().Entities.Num(), Baseline + 1);
	TestTrue(TEXT("delete redo executed"), GEditor->Trans->Redo());
	TestEqual(TEXT("delete redone"), Scene->GetDocument().Entities.Num(), Baseline);

	TSharedPtr<FJsonObject> EnvironmentParams = MakeShared<FJsonObject>();
	EnvironmentParams->SetStringField(TEXT("preset"), TEXT("sunset"));
	TestTrue(TEXT("environment command succeeded"), Engine->ExecuteCommand(TEXT("environment.applyPreset"), EnvironmentParams, Context).bSuccess);
	TestEqual(TEXT("environment applied"), Scene->GetDocument().Environment->GetStringField(TEXT("preset")), FString(TEXT("sunset")));
	TestTrue(TEXT("environment undo executed"), GEditor->Trans->Undo());
	TestEqual(TEXT("environment undone"), Scene->GetDocument().Environment->GetStringField(TEXT("preset")), FString(TEXT("clear")));
	TestTrue(TEXT("environment redo executed"), GEditor->Trans->Redo());
	TestEqual(TEXT("environment redone"), Scene->GetDocument().Environment->GetStringField(TEXT("preset")), FString(TEXT("sunset")));

	Scene->CloseScene(true, Error);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeImportUndoTest, "Epicode.Import.Undo", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeImportUndoTest::RunTest(const FString& Parameters)
{
	if (!GEditor || !GEditor->Trans)
	{
		AddError(TEXT("Editor transaction system is unavailable."));
		return false;
	}
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
	if (!Engine || !Scene)
	{
		AddError(TEXT("Epicode subsystems are unavailable."));
		return false;
	}
	FString Error;
	if (Scene->HasScene())
	{
		Scene->CloseScene(true, Error);
	}
	if (!Scene->NewScene(TEXT("Import Undo Test"), Error))
	{
		AddError(Error);
		return false;
	}
	const int32 Baseline = Scene->GetDocument().Entities.Num();

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("filePath"), FPaths::Combine(FPaths::ProjectDir(), TEXT("Tests/Fixtures/cube.gltf")));
	Params->SetStringField(TEXT("kind"), TEXT("prop"));
	Params->SetStringField(TEXT("destinationPath"), TEXT("/Game/Epicode/ImportedUndo"));
	Params->SetBoolField(TEXT("replaceExisting"), true);
	Params->SetBoolField(TEXT("autoPlace"), true);
	FEpicodeCommandContext Context;
	Context.bUndoable = true;
	const FEpicodeCommandResult Imported = Engine->ExecuteCommand(TEXT("import.file"), Params, Context);
	TestTrue(TEXT("import succeeded"), Imported.bSuccess);
	TestEqual(TEXT("import created an entity"), Scene->GetDocument().Entities.Num(), Baseline + 1);
	TestTrue(TEXT("import recorded a transaction"), GEditor->Trans->CanUndo());

	TestTrue(TEXT("import undo executed"), GEditor->Trans->Undo());
	TestEqual(TEXT("import undo removed entity"), Scene->GetDocument().Entities.Num(), Baseline);
	TestTrue(TEXT("import redo executed"), GEditor->Trans->Redo());
	TestEqual(TEXT("import redo restored entity"), Scene->GetDocument().Entities.Num(), Baseline + 1);

	Scene->CloseScene(true, Error);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeStudioResponsivenessTest, "Epicode.Studio.Responsiveness", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeStudioResponsivenessTest::RunTest(const FString& Parameters)
{
	FEpicodeScopedTestSceneRoot SceneRootGuard(TEXT("StudioResponsiveness"));
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
	if (!Engine || !Scene)
	{
		AddError(TEXT("Epicode subsystems are unavailable."));
		return false;
	}
	FString Error;
	if (Scene->HasScene())
	{
		Scene->CloseScene(true, Error);
	}
	if (!Scene->NewScene(TEXT("Responsiveness Test"), Error))
	{
		AddError(Error);
		return false;
	}

	FEpicodeCommandContext Context;
	Context.Source = TEXT("studio");

	TSharedPtr<FJsonObject> ImportParams = MakeShared<FJsonObject>();
	ImportParams->SetStringField(TEXT("filePath"), FPaths::Combine(FPaths::ProjectDir(), TEXT("Tests/Fixtures/cube.gltf")));
	ImportParams->SetStringField(TEXT("kind"), TEXT("prop"));
	ImportParams->SetStringField(TEXT("destinationPath"), TEXT("/Game/Epicode/Responsiveness"));
	ImportParams->SetBoolField(TEXT("replaceExisting"), true);
	const double ImportStart = FPlatformTime::Seconds();
	const FEpicodeCommandResult ImportResult = Engine->ExecuteCommand(TEXT("import.submit"), ImportParams, Context);
	const double ImportElapsed = FPlatformTime::Seconds() - ImportStart;
	TestTrue(TEXT("import submit accepted"), ImportResult.bSuccess);
	TestTrue(TEXT("import submit returns within 100ms"), ImportElapsed < 0.1);

	TSharedPtr<FJsonObject> RenderParams = MakeShared<FJsonObject>();
	RenderParams->SetStringField(TEXT("format"), TEXT("png"));
	RenderParams->SetNumberField(TEXT("width"), 64);
	RenderParams->SetNumberField(TEXT("height"), 64);
	RenderParams->SetNumberField(TEXT("startFrame"), 0);
	RenderParams->SetNumberField(TEXT("endFrame"), 240);
	const double RenderStart = FPlatformTime::Seconds();
	const FEpicodeCommandResult RenderResult = Engine->ExecuteCommand(TEXT("scene.render"), RenderParams, Context);
	const double RenderElapsed = FPlatformTime::Seconds() - RenderStart;
	TestTrue(TEXT("render submit accepted"), RenderResult.bSuccess);
	TestTrue(TEXT("render submit returns within 100ms"), RenderElapsed < 0.1);

	if (ImportResult.bSuccess && ImportResult.Result.IsValid())
	{
		TSharedPtr<FJsonObject> CancelParams = MakeShared<FJsonObject>();
		CancelParams->SetStringField(TEXT("jobId"), ImportResult.Result->GetStringField(TEXT("jobId")));
		Engine->ExecuteCommand(TEXT("import.cancel"), CancelParams, Context);
	}
	if (RenderResult.bSuccess && RenderResult.Result.IsValid())
	{
		TSharedPtr<FJsonObject> CancelParams = MakeShared<FJsonObject>();
		CancelParams->SetStringField(TEXT("jobId"), RenderResult.Result->GetStringField(TEXT("jobId")));
		Engine->ExecuteCommand(TEXT("render.cancel"), CancelParams, Context);
	}

	Scene->CloseScene(true, Error);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeStudioViewModelTest, "Epicode.Studio.ViewModel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeStudioViewModelTest::RunTest(const FString& Parameters)
{
	FEpicodeStudioViewModel ViewModel;
	TestEqual(TEXT("default tab"), ViewModel.GetActiveTab(), 0);
	ViewModel.SetActiveTab(4);
	TestEqual(TEXT("tab switched"), ViewModel.GetActiveTab(), 4);

	TestTrue(TEXT("revision needs refresh"), ViewModel.NeedsRefresh(7));
	ViewModel.MarkRefreshed(7);
	TestFalse(TEXT("revision refreshed"), ViewModel.NeedsRefresh(7));
	TestTrue(TEXT("new revision needs refresh"), ViewModel.NeedsRefresh(8));

	ViewModel.SelectOnly(TEXT("a"));
	TestTrue(TEXT("selection contains a"), ViewModel.IsSelected(TEXT("a")));
	TestEqual(TEXT("selected entity a"), ViewModel.GetSelectedEntityId(), FString(TEXT("a")));
	TestEqual(TEXT("selection count one"), ViewModel.GetSelectedEntities().Num(), 1);

	ViewModel.AddSelectedEntity(TEXT("b"));
	TestTrue(TEXT("selection contains b"), ViewModel.IsSelected(TEXT("b")));
	TestEqual(TEXT("selected entity b"), ViewModel.GetSelectedEntityId(), FString(TEXT("b")));
	TestEqual(TEXT("selection count two"), ViewModel.GetSelectedEntities().Num(), 2);

	ViewModel.RemoveSelectedEntity(TEXT("b"));
	TestFalse(TEXT("selection removed b"), ViewModel.IsSelected(TEXT("b")));
	TestEqual(TEXT("selected entity falls back"), ViewModel.GetSelectedEntityId(), FString(TEXT("a")));

	ViewModel.ClearSelection();
	TestTrue(TEXT("selection cleared"), ViewModel.GetSelectedEntityId().IsEmpty());
	TestEqual(TEXT("selection count zero"), ViewModel.GetSelectedEntities().Num(), 0);

	ViewModel.SelectOnly(TEXT(""));
	TestTrue(TEXT("empty select clears id"), ViewModel.GetSelectedEntityId().IsEmpty());
	TestEqual(TEXT("empty select clears set"), ViewModel.GetSelectedEntities().Num(), 0);

	ViewModel.AddSelectedEntity(TEXT(""));
	TestEqual(TEXT("empty add is ignored"), ViewModel.GetSelectedEntities().Num(), 0);
	TestTrue(TEXT("empty add keeps id empty"), ViewModel.GetSelectedEntityId().IsEmpty());

	ViewModel.SelectOnly(TEXT("keep"));
	ViewModel.AddSelectedEntity(TEXT("stale"));
	ViewModel.AddSelectedEntity(TEXT("keep2"));
	TSet<FString> ExistingIds;
	ExistingIds.Add(TEXT("keep"));
	ViewModel.RemoveMissingSelection(ExistingIds);
	TestTrue(TEXT("stale selection removed"), !ViewModel.IsSelected(TEXT("stale")));
	TestTrue(TEXT("existing selection kept"), ViewModel.IsSelected(TEXT("keep")));
	TestTrue(TEXT("missing selected id cleared"), ViewModel.GetSelectedEntityId().IsEmpty());

	ViewModel.SetSelectedEntity(TEXT("keep2"));
	TestEqual(TEXT("set selected id"), ViewModel.GetSelectedEntityId(), FString(TEXT("keep2")));
	TestFalse(TEXT("set selected id does not alter set"), ViewModel.IsSelected(TEXT("keep2")));
	TestTrue(TEXT("existing set retained"), ViewModel.IsSelected(TEXT("keep")));

	ViewModel.ClearSelection();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeStudioPanelTest, "Epicode.Studio.Panel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeStudioPanelTest::RunTest(const FString& Parameters)
{
	FEpicodeScopedTestSceneRoot SceneRootGuard(TEXT("StudioPanel"));
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
	if (!Engine || !Scene)
	{
		AddError(TEXT("Epicode subsystems are unavailable."));
		return false;
	}
	FString Error;
	if (Scene->HasScene())
	{
		Scene->CloseScene(true, Error);
	}
	if (!Scene->NewScene(TEXT("Studio Panel Test"), Error))
	{
		AddError(Error);
		return false;
	}

	auto Execute = [Engine](const FString& Method, const TSharedPtr<FJsonObject>& Params)
	{
		return Engine->ExecuteCommand(Method, Params, FEpicodeCommandContext());
	};
	auto FindEntityId = [&Execute](const FString& Name) -> FString
	{
		const FEpicodeCommandResult Result = Execute(TEXT("entity.list"), MakeShared<FJsonObject>());
		const TArray<TSharedPtr<FJsonValue>>* Entities = nullptr;
		if (!Result.bSuccess || !Result.Result.IsValid() || !Result.Result->TryGetArrayField(TEXT("entities"), Entities) || !Entities)
		{
			return FString();
		}
		for (const TSharedPtr<FJsonValue>& Value : *Entities)
		{
			const TSharedPtr<FJsonObject> Entity = Value.IsValid() ? Value->AsObject() : nullptr;
			FString EntityName;
			FString Id;
			if (Entity.IsValid() && Entity->TryGetStringField(TEXT("name"), EntityName) && EntityName == Name && Entity->TryGetStringField(TEXT("id"), Id))
			{
				return Id;
			}
		}
		return FString();
	};
	auto CountJobs = [&Execute](const TCHAR* Method, const TCHAR* Field) -> int32
	{
		const FEpicodeCommandResult Result = Execute(Method, MakeShared<FJsonObject>());
		const TArray<TSharedPtr<FJsonValue>>* Jobs = nullptr;
		if (!Result.bSuccess || !Result.Result.IsValid() || !Result.Result->TryGetArrayField(Field, Jobs) || !Jobs)
		{
			return 0;
		}
		return Jobs->Num();
	};
	TSharedPtr<FJsonObject> GroupParams = MakeShared<FJsonObject>();
	GroupParams->SetStringField(TEXT("type"), TEXT("group"));
	GroupParams->SetStringField(TEXT("name"), TEXT("Panel Group"));
	TestTrue(TEXT("group created"), Execute(TEXT("entity.create"), GroupParams).bSuccess);

	TSharedPtr<FJsonObject> PropParams = MakeShared<FJsonObject>();
	PropParams->SetStringField(TEXT("staticMesh"), TEXT("/Engine/EngineMeshes/Cube.Cube"));
	PropParams->SetStringField(TEXT("name"), TEXT("Panel Prop"));
	TestTrue(TEXT("prop created"), Execute(TEXT("prop.spawn"), PropParams).bSuccess);

	TSharedPtr<FJsonObject> LightParams = MakeShared<FJsonObject>();
	LightParams->SetStringField(TEXT("lightType"), TEXT("point"));
	LightParams->SetStringField(TEXT("name"), TEXT("Panel Light"));
	TestTrue(TEXT("light created"), Execute(TEXT("light.create"), LightParams).bSuccess);

	TSharedPtr<FJsonObject> CameraParams = MakeShared<FJsonObject>();
	CameraParams->SetStringField(TEXT("name"), TEXT("Panel Camera"));
	TestTrue(TEXT("camera created"), Execute(TEXT("camera.create"), CameraParams).bSuccess);

	TSharedPtr<FJsonObject> CharacterParams = MakeShared<FJsonObject>();
	CharacterParams->SetStringField(TEXT("skeletalMesh"), TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP.TutorialTPP"));
	CharacterParams->SetStringField(TEXT("name"), TEXT("Panel Character"));
	TestTrue(TEXT("character created"), Execute(TEXT("character.spawn"), CharacterParams).bSuccess);

	const TSharedRef<SEpicodeStudioPanel> Panel = SNew(SEpicodeStudioPanel);
	TestTrue(TEXT("panel constructed"), Panel->GetVisibility() != EVisibility::Collapsed);
	Panel->Tick(FGeometry(), 0.0, 0.0);
	TestTrue(TEXT("panel survives tick"), Panel->GetVisibility() != EVisibility::Collapsed);

	TestTrue(TEXT("tab switcher exists"), Panel->Switcher.IsValid());
	TestEqual(TEXT("nine studio tabs"), Panel->Switcher->GetNumWidgets(), 9);
	for (int32 Index = 0; Index < 9; ++Index)
	{
		Panel->ViewModel.SetActiveTab(Index);
		Panel->Switcher->SlatePrepass(1.0f);
		TestEqual(TEXT("tab switch follows view model"), Panel->Switcher->GetActiveWidgetIndex(), Index);
		TestTrue(TEXT("tab widget exists"), Panel->Switcher->GetWidget(Index).IsValid());
	}

	const FString GroupId = FindEntityId(TEXT("Panel Group"));
	const FString PropId = FindEntityId(TEXT("Panel Prop"));
	const FString LightId = FindEntityId(TEXT("Panel Light"));
	const FString CameraId = FindEntityId(TEXT("Panel Camera"));
	const FString CharacterId = FindEntityId(TEXT("Panel Character"));
	TestFalse(TEXT("group id found"), GroupId.IsEmpty());
	TestFalse(TEXT("prop id found"), PropId.IsEmpty());
	TestFalse(TEXT("light id found"), LightId.IsEmpty());
	TestFalse(TEXT("camera id found"), CameraId.IsEmpty());
	TestFalse(TEXT("character id found"), CharacterId.IsEmpty());

	TSharedPtr<FJsonObject> ReparentParams = MakeShared<FJsonObject>();
	ReparentParams->SetStringField(TEXT("id"), PropId);
	ReparentParams->SetStringField(TEXT("parentId"), GroupId);
	ReparentParams->SetBoolField(TEXT("keepWorldTransform"), true);
	TestTrue(TEXT("prop reparented for outliner"), Execute(TEXT("entity.reparent"), ReparentParams).bSuccess);

	Panel->RefreshOutliner();
	TestTrue(TEXT("outliner lists entities"), Panel->OutlinerList.IsValid() && Panel->OutlinerList->NumSlots() >= 5);
	TestTrue(TEXT("outliner auto selects"), !Panel->ViewModel.GetSelectedEntityId().IsEmpty());
	const int32 ExpandedRows = Panel->OutlinerList->NumSlots();
	Panel->OutlinerCollapsedIds.Add(GroupId);
	Panel->RefreshOutliner();
	TestEqual(TEXT("collapsed group hides child"), Panel->OutlinerList->NumSlots(), ExpandedRows - 1);
	Panel->OutlinerCollapsedIds.Reset();
	Panel->RefreshOutliner();
	TestEqual(TEXT("expanded group restores child"), Panel->OutlinerList->NumSlots(), ExpandedRows);

	Panel->OutlinerSearchInput->SetText(FText::FromString(TEXT("NoSuchEntityZZZ")));
	Panel->RefreshOutliner();
	TestEqual(TEXT("outliner filter shows empty state"), Panel->OutlinerList->NumSlots(), 1);
	Panel->OutlinerSearchInput->SetText(FText::GetEmpty());
	Panel->RefreshOutliner();
	TestEqual(TEXT("outliner filter cleared"), Panel->OutlinerList->NumSlots(), ExpandedRows);

	Panel->ViewModel.SelectOnly(PropId);
	Panel->RefreshOutlinerInspector();
	TestTrue(TEXT("inspector selection text"), Panel->OutlinerSelectionText.IsValid() && Panel->OutlinerSelectionText->GetText().ToString().Contains(PropId));
	TestTrue(TEXT("prop inspector fields"), Panel->OutlinerMaterialSlotInput.IsValid() && Panel->OutlinerInspector->NumSlots() >= 10);
	Panel->ViewModel.SelectOnly(LightId);
	Panel->RefreshOutlinerInspector();
	TestTrue(TEXT("light inspector fields"), Panel->OutlinerLightTypeInput.IsValid() && Panel->OutlinerLightIntensityInput.IsValid());
	TestEqual(TEXT("light inspector type value"), Panel->OutlinerLightTypeInput->GetText().ToString(), FString(TEXT("point")));
	Panel->ViewModel.SelectOnly(CameraId);
	Panel->RefreshOutlinerInspector();
	TestTrue(TEXT("camera inspector fields"), Panel->OutlinerCameraFovInput.IsValid());
	Panel->ViewModel.SelectOnly(CharacterId);
	Panel->RefreshOutlinerInspector();
	TestTrue(TEXT("character inspector fields"), Panel->OutlinerAnimationInput.IsValid());

	Panel->RefreshCharacterList();
	TestTrue(TEXT("character list populated"), Panel->CharacterList.IsValid() && Panel->CharacterList->NumSlots() >= 1);
	Panel->RefreshPropList();
	TestTrue(TEXT("prop list populated"), Panel->PropList.IsValid() && Panel->PropList->NumSlots() >= 1);
	Panel->RefreshLightList();
	TestTrue(TEXT("light list populated"), Panel->LightList.IsValid() && Panel->LightList->NumSlots() >= 1);

	if (Panel->CharacterFilterInput.IsValid())
	{
		Panel->CharacterFilterInput->SetText(FText::FromString(TEXT("NoSuchCharacter")));
	}
	Panel->RefreshCharacterList();
	TestEqual(TEXT("filtered character list shows empty state"), Panel->CharacterList->NumSlots(), 1);
	if (Panel->CharacterFilterInput.IsValid())
	{
		Panel->CharacterFilterInput->SetText(FText::GetEmpty());
	}
	Panel->RefreshCharacterList();
	TestTrue(TEXT("cleared character filter restores rows"), Panel->CharacterList->NumSlots() >= 1);

	Panel->PropFilterInput->SetText(FText::FromString(TEXT("NoSuchProp")));
	Panel->RefreshPropList();
	TestEqual(TEXT("filtered prop list shows empty state"), Panel->PropList->NumSlots(), 1);
	Panel->PropFilterInput->SetText(FText::GetEmpty());
	Panel->RefreshPropList();
	TestTrue(TEXT("cleared prop filter restores rows"), Panel->PropList->NumSlots() >= 1);

	Panel->LightFilterInput->SetText(FText::FromString(TEXT("NoSuchLight")));
	Panel->RefreshLightList();
	TestEqual(TEXT("filtered light list shows empty state"), Panel->LightList->NumSlots(), 1);
	Panel->LightFilterInput->SetText(FText::GetEmpty());
	Panel->RefreshLightList();
	TestTrue(TEXT("cleared light filter restores rows"), Panel->LightList->NumSlots() >= 1);

	const int32 ImportJobsBefore = CountJobs(TEXT("import.list"), TEXT("jobs"));
	Panel->RefreshImportList();
	TestEqual(TEXT("import rows match jobs"), Panel->ImportList->NumSlots(), FMath::Max(1, ImportJobsBefore));
	const int32 RenderJobsBefore = CountJobs(TEXT("render.list"), TEXT("jobs"));
	Panel->RefreshRenderList();
	TestEqual(TEXT("render rows match jobs"), Panel->RenderList->NumSlots(), FMath::Max(1, RenderJobsBefore));

	TSharedPtr<FJsonObject> ImportParams = MakeShared<FJsonObject>();
	ImportParams->SetStringField(TEXT("filePath"), FPaths::Combine(FPaths::ProjectDir(), TEXT("Tests/Fixtures/cube.gltf")));
	ImportParams->SetStringField(TEXT("kind"), TEXT("prop"));
	ImportParams->SetStringField(TEXT("destinationPath"), TEXT("/Game/Epicode/StudioPanelValidation"));
	ImportParams->SetBoolField(TEXT("replaceExisting"), true);
	const FEpicodeCommandResult ImportSubmit = Execute(TEXT("import.submit"), ImportParams);
	TestTrue(TEXT("import submit from panel"), ImportSubmit.bSuccess);
	FString ImportJobId;
	if (ImportSubmit.bSuccess && ImportSubmit.Result.IsValid())
	{
		ImportSubmit.Result->TryGetStringField(TEXT("jobId"), ImportJobId);
	}
	Panel->RefreshImportList();
	TestTrue(TEXT("import tab lists submitted job"), Panel->ImportList->NumSlots() >= ImportJobsBefore + 1);

	TSharedPtr<FJsonObject> RenderParams = MakeShared<FJsonObject>();
	RenderParams->SetStringField(TEXT("format"), TEXT("png"));
	RenderParams->SetNumberField(TEXT("width"), 64);
	RenderParams->SetNumberField(TEXT("height"), 64);
	RenderParams->SetNumberField(TEXT("startFrame"), 0);
	RenderParams->SetNumberField(TEXT("endFrame"), 1);
	const FEpicodeCommandResult RenderSubmit = Execute(TEXT("scene.render"), RenderParams);
	TestTrue(TEXT("render submit from panel"), RenderSubmit.bSuccess);
	FString RenderJobId;
	if (RenderSubmit.bSuccess && RenderSubmit.Result.IsValid())
	{
		RenderSubmit.Result->TryGetStringField(TEXT("jobId"), RenderJobId);
	}
	Panel->RefreshRenderList();
	TestTrue(TEXT("render tab lists submitted job"), Panel->RenderList->NumSlots() >= RenderJobsBefore + 1);

	Panel->RefreshApiPanel();
	TestTrue(TEXT("API summary populated"), Panel->ApiSummary.IsValid());
	const FString ApiText = Panel->ApiSummary->GetText().ToString();
	TestTrue(TEXT("API status visible"), ApiText.Contains(TEXT("status:")) || ApiText.Contains(TEXT("health check failed")));
	TestTrue(TEXT("API command count visible"), ApiText.Contains(TEXT("commands:")) || ApiText.Contains(TEXT("health check failed")));
	TestTrue(TEXT("API HTTP settings visible"), ApiText.Contains(TEXT("HTTP:")) || ApiText.Contains(TEXT("health check failed")));

	if (!ImportJobId.IsEmpty())
	{
		TSharedPtr<FJsonObject> CancelParams = MakeShared<FJsonObject>();
		CancelParams->SetStringField(TEXT("jobId"), ImportJobId);
		Execute(TEXT("import.cancel"), CancelParams);
	}
	if (!RenderJobId.IsEmpty())
	{
		TSharedPtr<FJsonObject> CancelParams = MakeShared<FJsonObject>();
		CancelParams->SetStringField(TEXT("jobId"), RenderJobId);
		Execute(TEXT("render.cancel"), CancelParams);
	}

	Scene->CloseScene(true, Error);
	return true;
}

namespace
{
	TSharedPtr<FJsonObject> MakePlayer(const FString& Id, const FString& Role, bool bConnected, const FString& CharacterId = FString())
	{
		TSharedPtr<FJsonObject> Player = MakeShared<FJsonObject>();
		Player->SetStringField(TEXT("playerId"), Id);
		Player->SetStringField(TEXT("role"), Role);
		Player->SetBoolField(TEXT("connected"), bConnected);
		Player->SetStringField(TEXT("characterId"), CharacterId);
		return Player;
	}

	TSharedPtr<FJsonObject> MakeMultiplayerSnapshot(bool bOwner = true)
	{
		TSharedPtr<FJsonObject> Snapshot = MakeShared<FJsonObject>();
		Snapshot->SetStringField(TEXT("sessionId"), TEXT("session-1"));
		Snapshot->SetStringField(TEXT("state"), TEXT("active"));
		Snapshot->SetStringField(TEXT("sceneId"), TEXT("scene-1"));
		Snapshot->SetStringField(TEXT("ownerPlayerId"), TEXT("Alice"));
		TArray<TSharedPtr<FJsonValue>> Players;
		Players.Add(MakeShared<FJsonValueObject>(MakePlayer(TEXT("Alice"), bOwner ? TEXT("owner") : TEXT("performer"), true, TEXT("character-a"))));
		Players.Add(MakeShared<FJsonValueObject>(MakePlayer(TEXT("Bob"), TEXT("observer"), false)));
		Snapshot->SetArrayField(TEXT("players"), Players);
		TArray<TSharedPtr<FJsonValue>> Characters;
		TSharedPtr<FJsonObject> Character = MakeShared<FJsonObject>();
		Character->SetStringField(TEXT("entityId"), TEXT("character-a"));
		Character->SetStringField(TEXT("playerId"), TEXT("Alice"));
		Characters.Add(MakeShared<FJsonValueObject>(Character));
		Snapshot->SetArrayField(TEXT("characters"), Characters);
		TArray<TSharedPtr<FJsonValue>> Cameras;
		TSharedPtr<FJsonObject> Camera = MakeShared<FJsonObject>();
		Camera->SetStringField(TEXT("name"), TEXT("Main Camera"));
		Cameras.Add(MakeShared<FJsonValueObject>(Camera));
		Snapshot->SetArrayField(TEXT("cameras"), Cameras);
		return Snapshot;
	}

	TSharedPtr<FJsonObject> MakePerformanceSnapshot()
	{
		TSharedPtr<FJsonObject> Snapshot = MakeMultiplayerSnapshot();
		TArray<TSharedPtr<FJsonValue>> Events;
		TSharedPtr<FJsonObject> Event = MakeShared<FJsonObject>();
		Event->SetStringField(TEXT("type"), TEXT("performance.action"));
		Event->SetStringField(TEXT("playerId"), TEXT("Alice"));
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actionTag"), TEXT("wave"));
		Payload->SetStringField(TEXT("emotion"), TEXT("happy"));
		Payload->SetNumberField(TEXT("intensity"), 0.8);
		Payload->SetBoolField(TEXT("speaking"), true);
		Payload->SetStringField(TEXT("resolvedSource"), TEXT("preset"));
		Event->SetObjectField(TEXT("payload"), Payload);
		Events.Add(MakeShared<FJsonValueObject>(Event));
		Snapshot->SetArrayField(TEXT("performanceEvents"), Events);
		return Snapshot;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeStudioMultiplayerPanelTest, "Epicode.Studio.MultiplayerPanel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeStudioMultiplayerPanelTest::RunTest(const FString& Parameters)
{
	const TSharedRef<SEpicodeMultiplayerPanel> Panel = SNew(SEpicodeMultiplayerPanel);
	TestNotNull(TEXT("panel view model"), Panel->GetViewModel().Get());
	Panel->SetViewModelSnapshot(MakeMultiplayerSnapshot());
	TestTrue(TEXT("panel has session"), Panel->GetViewModel()->HasSession());
	TestEqual(TEXT("panel player count"), Panel->GetViewModel()->GetPlayerCount(), 2);
	TestEqual(TEXT("panel connected count"), Panel->GetViewModel()->GetConnectedCount(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeStudioMultiplayerRolesTest, "Epicode.Studio.MultiplayerRoles", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeStudioMultiplayerRolesTest::RunTest(const FString& Parameters)
{
	FEpicodeMultiplayerViewModel ViewModel;
	ViewModel.SetSnapshot(MakeMultiplayerSnapshot());
	ViewModel.SetLocalPlayerId(TEXT("Alice"));
	TestEqual(TEXT("owner role"), ViewModel.GetLocalRole(), FString(TEXT("owner")));
	TestTrue(TEXT("owner manages"), ViewModel.CanManage());
	TestTrue(TEXT("owner assigns"), ViewModel.CanAssignCharacters());
	TestTrue(TEXT("owner controls character"), ViewModel.CanControlOwnCharacter());
	TestFalse(TEXT("owner is not observer"), ViewModel.IsObserver());

	ViewModel.SetSnapshot(MakeMultiplayerSnapshot(false));
	TestEqual(TEXT("performer role"), ViewModel.GetLocalRole(), FString(TEXT("performer")));
	TestFalse(TEXT("performer cannot manage"), ViewModel.CanManage());
	TestFalse(TEXT("performer cannot assign"), ViewModel.CanAssignCharacters());
	TestTrue(TEXT("performer controls own character"), ViewModel.CanControlOwnCharacter());

	ViewModel.SetLocalPlayerId(TEXT("Bob"));
	TestEqual(TEXT("observer role"), ViewModel.GetLocalRole(), FString(TEXT("observer")));
	TestTrue(TEXT("observer is observer"), ViewModel.IsObserver());
	TestFalse(TEXT("observer cannot control"), ViewModel.CanControlOwnCharacter());
	TestEqual(TEXT("player rows"), ViewModel.GetPlayerRows().Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeStudioPerformerHudTest, "Epicode.Studio.PerformerHud", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeStudioPerformerHudTest::RunTest(const FString& Parameters)
{
	FEpicodeMultiplayerViewModel ViewModel;
	ViewModel.SetLocalPlayerId(TEXT("Alice"));
	ViewModel.SetSnapshot(MakePerformanceSnapshot());
	const FString Summary = ViewModel.GetPerformerHudSummary();
	TestTrue(TEXT("hud action"), Summary.Contains(TEXT("wave")));
	TestTrue(TEXT("hud emotion"), Summary.Contains(TEXT("happy")));
	TestTrue(TEXT("hud speaking"), Summary.Contains(TEXT("Speaking: yes")));
	TestTrue(TEXT("hud viseme"), Summary.Contains(TEXT("preset")));
	ViewModel.SetLocalPlayerId(TEXT("Bob"));
	TestTrue(TEXT("no performance for observer"), ViewModel.GetPerformerHudSummary().Contains(TEXT("No performance state")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeStudioDirectorConsoleTest, "Epicode.Studio.DirectorConsole", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeStudioDirectorConsoleTest::RunTest(const FString& Parameters)
{
	FEpicodeMultiplayerViewModel ViewModel;
	ViewModel.SetSnapshot(MakeMultiplayerSnapshot());
	ViewModel.SetLocalPlayerId(TEXT("Alice"));
	const TArray<FString> Entries = ViewModel.GetDirectorEntries();
	TestTrue(TEXT("director entries"), Entries.Num() >= 3);
	TestTrue(TEXT("camera entry"), FString::Join(Entries, TEXT("\n")).Contains(TEXT("camera: Main Camera")));
	TestTrue(TEXT("player entry"), FString::Join(Entries, TEXT("\n")).Contains(TEXT("player: Bob")));
	TestTrue(TEXT("owner can operate"), ViewModel.CanManage());
	ViewModel.SetLocalPlayerId(TEXT("Bob"));
	TestTrue(TEXT("observer read only"), ViewModel.IsObserver());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeStudioSessionStatesTest, "Epicode.Studio.SessionStates", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeStudioSessionStatesTest::RunTest(const FString& Parameters)
{
	FEpicodeMultiplayerViewModel ViewModel;
	ViewModel.SetSnapshot(MakeMultiplayerSnapshot());
	TestEqual(TEXT("reconnecting state"), ViewModel.GetSessionStateText(), FString(TEXT("Reconnecting")));
	TestTrue(TEXT("reconnect text"), ViewModel.GetReconnectText().Contains(TEXT("Bob")));

	TSharedPtr<FJsonObject> Ended = MakeMultiplayerSnapshot();
	Ended->SetStringField(TEXT("state"), TEXT("ended"));
	ViewModel.SetSnapshot(Ended);
	TestEqual(TEXT("ended state"), ViewModel.GetSessionStateText(), FString(TEXT("Session ended")));
	TestTrue(TEXT("ended flag"), ViewModel.IsEnded());
	return true;
}

#endif
