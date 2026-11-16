#include "Misc/AutomationTest.h"

#include "Editor.h"
#include "EpicodeCommand.h"
#include "EpicodeEngineSubsystem.h"
#include "EpicodeSceneSubsystem.h"
#include "HAL/FileManager.h"
#include "LevelEditor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "SEpicodeFocusedShell.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	struct FScopedEditorShellLayout
	{
		FScopedEditorShellLayout()
			: Path(FPaths::Combine(
				FPaths::ProjectSavedDir(), TEXT("Epicode/Editor/layout.json")))
		{
			bHadFile = FFileHelper::LoadFileToString(Original, *Path);
		}

		~FScopedEditorShellLayout()
		{
			if (bHadFile)
			{
				FFileHelper::SaveStringToFile(Original, *Path);
			}
			else
			{
				IFileManager::Get().Delete(*Path, false, true, true);
			}
		}

		FString Path;
		FString Original;
		bool bHadFile = false;
	};

	UEpicodeSceneSubsystem* GetTestScene()
	{
		return UEpicodeSceneSubsystem::GetPrimary();
	}

	UEpicodeEngineSubsystem* GetTestEngine()
	{
		return UEpicodeEngineSubsystem::Get();
	}

	bool StartTestScene(UEpicodeSceneSubsystem* Scene, const FString& Name)
	{
		if (!Scene)
		{
			return false;
		}
		FString Error;
		if (Scene->HasScene())
		{
			Scene->CloseScene(true, Error);
		}
		return Scene->NewScene(Name, Error);
	}

	FEpicodeCommandResult ExecuteTestCommand(
		UEpicodeEngineSubsystem* Engine,
		const TCHAR* Method,
		const TSharedPtr<FJsonObject>& Params = MakeShared<FJsonObject>())
	{
		return Engine
			? Engine->ExecuteCommand(Method, Params, FEpicodeCommandContext())
			: FEpicodeCommandResult();
	}

	TSharedPtr<FJsonObject> MakeLightParams(const FString& Type, const FString& Name)
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("lightType"), Type);
		Params->SetStringField(TEXT("name"), Name);
		return Params;
	}

	TSharedPtr<FJsonObject> MakeCameraParams(const FString& Name)
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("name"), Name);
		return Params;
	}

	TSharedPtr<FJsonObject> MakePropParams(const FString& Name)
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("name"), Name);
		Params->SetStringField(TEXT("staticMesh"), TEXT("/Engine/EngineMeshes/Cube.Cube"));
		return Params;
	}

	TSharedPtr<FJsonObject> MakeCharacterParams(const FString& Name)
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("name"), Name);
		Params->SetStringField(
			TEXT("skeletalMesh"),
			TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP.TutorialTPP"));
		return Params;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEpicodeEditorShellViewportTest,
	"Epicode.EditorShell.Viewport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeEditorShellViewportTest::RunTest(const FString& Parameters)
{
	FScopedEditorShellLayout LayoutGuard;
	UEpicodeSceneSubsystem* Scene = GetTestScene();
	UEpicodeEngineSubsystem* Engine = GetTestEngine();
	if (!Scene || !Engine)
	{
		AddError(TEXT("Epicode scene and engine subsystems are required."));
		return false;
	}
	if (!StartTestScene(Scene, TEXT("Editor Shell Viewport Test")))
	{
		AddError(TEXT("Failed to create the viewport test scene."));
		return false;
	}

	TestTrue(TEXT("prop spawned"), ExecuteTestCommand(Engine, TEXT("prop.spawn"), MakePropParams(TEXT("Viewport Prop"))).bSuccess);
	TestTrue(TEXT("light spawned"), ExecuteTestCommand(Engine, TEXT("light.create"), MakeLightParams(TEXT("point"), TEXT("Viewport Light"))).bSuccess);
	TestTrue(TEXT("camera spawned"), ExecuteTestCommand(Engine, TEXT("camera.create"), MakeCameraParams(TEXT("Viewport Camera"))).bSuccess);

	FLevelEditorModule& LevelEditorModule =
		FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	TSharedPtr<ILevelEditor> LevelEditor = LevelEditorModule.GetFirstLevelEditor();
	if (!LevelEditor.IsValid())
	{
		AddError(TEXT("The level editor instance is required for the viewport test."));
		FString CloseError;
		Scene->CloseScene(true, CloseError);
		return false;
	}

	bool bViewportReady = false;
	{
		TSharedRef<SEpicodeFocusedShell> Shell =
			SNew(SEpicodeFocusedShell).LevelEditor(LevelEditor);
		bViewportReady = Shell->IsViewportReady();
		if (!bViewportReady)
		{
			AddError(FString::Printf(
				TEXT("Focused viewport was not created: %s"), *Shell->GetViewportError()));
		}
		else
		{
			TestEqual(TEXT("four entities before delete"), Shell->GetEntityCountForSmokeTest(), 4);
			TestTrue(TEXT("prop selected"), Shell->SelectEntityByTypeForSmokeTest(TEXT("Prop")));
			TestTrue(
				TEXT("selected entity is the prop"),
				Shell->GetInspectorSummaryForSmokeTest().StartsWith(TEXT("Prop|")));

			Shell->SetWidgetMode(UE::Widget::WM_Translate);
			TestEqual(TEXT("translate gizmo"), Shell->GetWidgetModeForSmokeTest(), FString(TEXT("移动")));
			Shell->SetWidgetMode(UE::Widget::WM_Rotate);
			TestEqual(TEXT("rotate gizmo"), Shell->GetWidgetModeForSmokeTest(), FString(TEXT("旋转")));
			Shell->SetWidgetMode(UE::Widget::WM_Scale);
			TestEqual(TEXT("scale gizmo"), Shell->GetWidgetModeForSmokeTest(), FString(TEXT("缩放")));
			Shell->SetWidgetMode(UE::Widget::WM_Translate);

			TestTrue(
				TEXT("numeric transform applied"),
				Shell->SetSelectedLocationForSmokeTest(FVector(25.0, 50.0, 75.0)));
			TestTrue(
				TEXT("numeric transform stored"),
				Shell->GetSelectedLocationForSmokeTest().Equals(FVector(25.0, 50.0, 75.0), 0.01));

			TestTrue(TEXT("delete shortcut handled"), Shell->ProcessShortcutForSmokeTest(EKeys::Delete));
			TestEqual(TEXT("three entities after delete"), Shell->GetEntityCountForSmokeTest(), 3);
			TestTrue(TEXT("undo shortcut handled"), Shell->ProcessShortcutForSmokeTest(EKeys::Z, true));
			TestEqual(TEXT("four entities after undo"), Shell->GetEntityCountForSmokeTest(), 4);
		}
	}

	FString CloseError;
	Scene->CloseScene(true, CloseError);
	return bViewportReady;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEpicodeEditorShellScopeTest,
	"Epicode.EditorShell.Scope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeEditorShellScopeTest::RunTest(const FString& Parameters)
{
	FScopedEditorShellLayout LayoutGuard;
	TSharedRef<SEpicodeFocusedShell> Shell = SNew(SEpicodeFocusedShell);

	const TArray<FString> ExpectedMenus =
	{
		TEXT("场景"),
		TEXT("角色"),
		TEXT("物品"),
		TEXT("灯光"),
		TEXT("摄像"),
		TEXT("本地库"),
		TEXT("编辑"),
		TEXT("视图"),
		TEXT("对话"),
		TEXT("帮助")
	};
	const TArray<FString> Labels = Shell->GetMenuLabelsForSmokeTest();
	TestEqual(TEXT("ten focused menus"), Labels.Num(), ExpectedMenus.Num());
	for (const FString& Label : ExpectedMenus)
	{
		TestTrue(FString::Printf(TEXT("menu present: %s"), *Label), Labels.Contains(Label));
	}

	const TArray<FString> ForbiddenMenus =
	{
		TEXT("环境"),
		TEXT("地形"),
		TEXT("植被"),
		TEXT("蓝图"),
		TEXT("动画蓝图"),
		TEXT("Sequencer"),
		TEXT("PIE"),
		TEXT("渲染队列"),
		TEXT("导航"),
		TEXT("物理约束"),
		TEXT("分组"),
		TEXT("材质图")
	};
	for (const FString& Label : ForbiddenMenus)
	{
		TestFalse(FString::Printf(TEXT("forbidden menu absent: %s"), *Label), Labels.Contains(Label));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEpicodeEditorShellInspectorTest,
	"Epicode.EditorShell.Inspector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeEditorShellInspectorTest::RunTest(const FString& Parameters)
{
	FScopedEditorShellLayout LayoutGuard;
	UEpicodeSceneSubsystem* Scene = GetTestScene();
	UEpicodeEngineSubsystem* Engine = GetTestEngine();
	if (!Scene || !Engine)
	{
		AddError(TEXT("Epicode scene and engine subsystems are required."));
		return false;
	}
	if (!StartTestScene(Scene, TEXT("Editor Shell Inspector Test")))
	{
		AddError(TEXT("Failed to create the inspector test scene."));
		return false;
	}

	TestTrue(TEXT("light spawned"), ExecuteTestCommand(Engine, TEXT("light.create"), MakeLightParams(TEXT("point"), TEXT("Inspector Light"))).bSuccess);
	TestTrue(TEXT("camera spawned"), ExecuteTestCommand(Engine, TEXT("camera.create"), MakeCameraParams(TEXT("Inspector Camera"))).bSuccess);
	TestTrue(TEXT("prop spawned"), ExecuteTestCommand(Engine, TEXT("prop.spawn"), MakePropParams(TEXT("Inspector Prop"))).bSuccess);
	TestTrue(TEXT("character spawned"), ExecuteTestCommand(Engine, TEXT("character.spawn"), MakeCharacterParams(TEXT("Inspector Character"))).bSuccess);

	{
		TSharedRef<SEpicodeFocusedShell> Shell = SNew(SEpicodeFocusedShell);
		TestEqual(
			TEXT("empty inspector"),
			Shell->GetInspectorSummaryForSmokeTest(),
			FString(TEXT("no-selection")));

		TestTrue(TEXT("light selected"), Shell->SelectEntityByTypeForSmokeTest(TEXT("Light")));
		TestTrue(
			TEXT("light inspector summary"),
			Shell->GetInspectorSummaryForSmokeTest().StartsWith(TEXT("Light|")));
		TestTrue(TEXT("light intensity"), Shell->SetSelectedNumberForSmokeTest(TEXT("intensity"), 4200.0));
		TestTrue(TEXT("light attenuation"), Shell->SetSelectedNumberForSmokeTest(TEXT("attenuationRadius"), 2.5));
		TestTrue(TEXT("light hidden"), Shell->SetSelectedVisibilityForSmokeTest(false));
		TestTrue(TEXT("light shown"), Shell->SetSelectedVisibilityForSmokeTest(true));

		Shell->SetInspectorSnapForSmokeTest(true);
		const double Grid = Shell->GetInspectorSnapGridForSmokeTest();
		TestTrue(TEXT("snap grid available"), Grid >= 1.0);
		Shell->SetSelectedLocationForSmokeTest(
			FVector(Grid * 7.0 + 3.0, Grid * 4.0 + 3.0, Grid + 3.0));
		const FVector Snapped = Shell->GetSelectedLocationForSmokeTest();
		TestTrue(TEXT("position snap X"), FMath::IsNearlyEqual(Snapped.X, Grid * 7.0, 0.01));
		TestTrue(TEXT("position snap Y"), FMath::IsNearlyEqual(Snapped.Y, Grid * 4.0, 0.01));
		TestTrue(TEXT("position snap Z"), FMath::IsNearlyEqual(Snapped.Z, Grid, 0.01));
		Shell->SetInspectorSnapForSmokeTest(false);

		TestTrue(TEXT("camera selected"), Shell->SelectEntityByTypeForSmokeTest(TEXT("Camera")));
		TestTrue(TEXT("camera FOV"), Shell->SetSelectedNumberForSmokeTest(TEXT("fieldOfView"), 60.0));
		TestTrue(TEXT("camera activated"), Shell->ActivateSelectedCameraForSmokeTest());
		TestTrue(
			TEXT("active camera summary"),
			Shell->GetInspectorSummaryForSmokeTest().Contains(TEXT("activeCamera=1")));

		TestTrue(TEXT("character selected"), Shell->SelectEntityByTypeForSmokeTest(TEXT("Character")));
		TestTrue(TEXT("character speed"), Shell->SetSelectedAnimationSpeedForSmokeTest(1.75));
		TestTrue(TEXT("character paused"), Shell->SetSelectedPausedForSmokeTest(true));
		TestTrue(
			TEXT("character inspector summary"),
			Shell->GetInspectorSummaryForSmokeTest().StartsWith(TEXT("Character|")));

		TestTrue(TEXT("prop selected"), Shell->SelectEntityByTypeForSmokeTest(TEXT("Prop")));
		TestTrue(TEXT("prop physics"), Shell->SetSelectedPhysicsForSmokeTest(true));
		TestTrue(
			TEXT("prop inspector summary"),
			Shell->GetInspectorSummaryForSmokeTest().StartsWith(TEXT("Prop|")));
	}

	FString CloseError;
	Scene->CloseScene(true, CloseError);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEpicodeEditorShellLibraryPanelTest,
	"Epicode.EditorShell.LibraryPanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeEditorShellLibraryPanelTest::RunTest(const FString& Parameters)
{
	FScopedEditorShellLayout LayoutGuard;
	TSharedRef<SEpicodeFocusedShell> Shell = SNew(SEpicodeFocusedShell);

	const int32 SceneCount = Shell->GetSceneCountForSmokeTest();
	TestEqual(
		TEXT("all scenes visible by default"),
		Shell->GetFilteredSceneCountForSmokeTest(),
		SceneCount);
	if (SceneCount > 0)
	{
		const FString FirstScene = Shell->GetFirstSceneNameForSmokeTest();
		TestFalse(TEXT("first scene name available"), FirstScene.IsEmpty());
		Shell->SetSceneSearchForSmokeTest(FirstScene);
		TestTrue(
			TEXT("scene search keeps matching rows"),
			Shell->GetFilteredSceneCountForSmokeTest() >= 1);
		TestTrue(
			TEXT("scene search narrows rows"),
			Shell->GetFilteredSceneCountForSmokeTest() <= SceneCount);

		Shell->SetSceneSearchForSmokeTest(TEXT("NoSuchSceneZZZ"));
		TestEqual(
			TEXT("scene search empty state"),
			Shell->GetFilteredSceneCountForSmokeTest(),
			0);
		Shell->SetSceneSearchForSmokeTest(TEXT(""));
		TestEqual(
			TEXT("scene search cleared"),
			Shell->GetFilteredSceneCountForSmokeTest(),
			SceneCount);
	}

	Shell->SetSceneTagFilterForSmokeTest(TEXT("NoSuchTagZZZ"));
	TestEqual(
		TEXT("scene tag filter empty state"),
		Shell->GetFilteredSceneCountForSmokeTest(),
		0);
	Shell->SetSceneTagFilterForSmokeTest(TEXT(""));
	TestEqual(
		TEXT("scene tag filter cleared"),
		Shell->GetFilteredSceneCountForSmokeTest(),
		SceneCount);

	const int32 AssetCount = Shell->GetAssetCountForSmokeTest();
	Shell->SetAssetSearchForSmokeTest(TEXT("NoSuchAssetZZZ"));
	TestEqual(
		TEXT("asset search empty state"),
		Shell->GetFilteredAssetCountForSmokeTest(),
		0);
	Shell->SetAssetSearchForSmokeTest(TEXT(""));
	TestEqual(
		TEXT("asset search cleared"),
		Shell->GetFilteredAssetCountForSmokeTest(),
		AssetCount);

	Shell->SetAssetTagFilterForSmokeTest(TEXT("NoSuchTagZZZ"));
	TestEqual(
		TEXT("asset tag filter empty state"),
		Shell->GetFilteredAssetCountForSmokeTest(),
		0);
	Shell->SetAssetTagFilterForSmokeTest(TEXT(""));

	Shell->SetAssetTypeFilterForSmokeTest(TEXT("Character"));
	TestTrue(
		TEXT("character filter within total"),
		Shell->GetFilteredAssetCountForSmokeTest() <= AssetCount);
	Shell->SetAssetTypeFilterForSmokeTest(TEXT("Prop"));
	TestTrue(
		TEXT("prop filter within total"),
		Shell->GetFilteredAssetCountForSmokeTest() <= AssetCount);
	Shell->SetAssetTypeFilterForSmokeTest(TEXT("All"));
	TestEqual(
		TEXT("asset type filter cleared"),
		Shell->GetFilteredAssetCountForSmokeTest(),
		AssetCount);

	Shell->RefreshLibraryForSmokeTest();
	TestEqual(
		TEXT("manual refresh preserves scenes"),
		Shell->GetSceneCountForSmokeTest(),
		SceneCount);
	TestEqual(
		TEXT("manual refresh preserves assets"),
		Shell->GetAssetCountForSmokeTest(),
		AssetCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEpicodeEditorShellSceneImportTest,
	"Epicode.EditorShell.SceneImport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeEditorShellSceneImportTest::RunTest(const FString& Parameters)
{
	FScopedEditorShellLayout LayoutGuard;
	UEpicodeSceneSubsystem* Scene = GetTestScene();
	if (!Scene)
	{
		AddError(TEXT("Epicode scene subsystem is required."));
		return false;
	}

	FString Error;
	if (Scene->HasScene())
	{
		Scene->CloseScene(true, Error);
	}

	const FString FixturePath = FPaths::Combine(
		FPaths::ProjectDir(), TEXT("Tests/Fixtures/scene-import.json"));
	TestTrue(TEXT("scene import fixture exists"), IFileManager::Get().FileExists(*FixturePath));

	FString FirstId;
	FString CopyId;
	{
		TSharedRef<SEpicodeFocusedShell> Shell = SNew(SEpicodeFocusedShell);

		const FString InvalidRoot = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("Epicode/Tests/SceneImport"),
			FGuid::NewGuid().ToString(EGuidFormats::Digits));
		const FString InvalidPath = FPaths::Combine(InvalidRoot, TEXT("invalid.json"));
		IFileManager::Get().MakeDirectory(*InvalidRoot, true);
		FFileHelper::SaveStringToFile(
			TEXT("{\"schemaVersion\":999,\"sceneId\":\"01A1DDAA-0000-7000-8000-000000000099\",\"name\":\"Bad\"}"),
			*InvalidPath);
		TestFalse(
			TEXT("unsupported schema rejected"),
			Shell->ImportSceneFromFileForSmokeTest(InvalidPath));
		TestTrue(
			TEXT("failed import has no selection"),
			Shell->GetLastImportedSceneIdForSmokeTest().IsEmpty());
		IFileManager::Get().DeleteDirectory(*InvalidRoot, false, true);

		TestTrue(
			TEXT("scene imported"),
			Shell->ImportSceneFromFileForSmokeTest(FixturePath));
		FirstId = Shell->GetLastImportedSceneIdForSmokeTest();
		TestFalse(TEXT("first scene id"), FirstId.IsEmpty());
		TestEqual(
			TEXT("imported scene selected in library"),
			Shell->GetSelectedLibrarySceneIdForSmokeTest(),
			FirstId);

		TestTrue(
			TEXT("scene imported as copy"),
			Shell->ImportSceneFromFileForSmokeTest(FixturePath, true));
		CopyId = Shell->GetLastImportedSceneIdForSmokeTest();
		TestFalse(TEXT("copy scene id"), CopyId.IsEmpty());
		TestTrue(TEXT("copy uses a new scene id"), CopyId != FirstId);
		TestEqual(
			TEXT("copy selected in library"),
			Shell->GetSelectedLibrarySceneIdForSmokeTest(),
			CopyId);

		const TArray<FEpicodeSceneLibraryEntry> Scenes = Scene->ListScenes();
		TestTrue(
			TEXT("original scene in library"),
			Scenes.ContainsByPredicate([&FirstId](const FEpicodeSceneLibraryEntry& Entry)
			{
				return Entry.SceneId == FirstId;
			}));
		TestTrue(
			TEXT("copy in library"),
			Scenes.ContainsByPredicate([&CopyId](const FEpicodeSceneLibraryEntry& Entry)
			{
				return Entry.SceneId == CopyId;
			}));
	}

	for (const FString& SceneId : { FirstId, CopyId })
	{
		if (SceneId.IsEmpty())
		{
			continue;
		}
		FString TrashPath;
		FString DeleteError;
		if (Scene->DeleteScene(SceneId, TrashPath, DeleteError))
		{
			IFileManager::Get().DeleteDirectory(*TrashPath, false, true);
		}
	}
	return true;
}

#endif
