#include "Misc/AutomationTest.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "EpicodeAssistantHistory.h"
#include "EpicodeAssistantTypes.h"
#include "EpicodeCommand.h"
#include "EpicodeEngineSubsystem.h"
#include "EpicodeLocalLibrary.h"
#include "EpicodeSceneSubsystem.h"
#include "EpicodeSettings.h"
#include "EpicodeEditorShellModule.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/FileManager.h"
#include "LevelEditor.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
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

	void TickShellImport(const TSharedRef<SEpicodeFocusedShell>& Shell)
	{
		for (int32 Attempt = 0;
			Attempt < 300 && !Shell->GetImportJobIdForSmokeTest().IsEmpty();
			++Attempt)
		{
			FTSTicker::GetCoreTicker().Tick(0.05f);
			Shell->PollAssetImportForSmokeTest();
			if (!Shell->GetImportJobIdForSmokeTest().IsEmpty())
			{
				FPlatformProcess::Sleep(0.01f);
			}
		}
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
	FEpicodeEditorShellLegacyTabsTest,
	"Epicode.EditorShell.LegacyTabs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeEditorShellLegacyTabsTest::RunTest(const FString& Parameters)
{
	const bool bFocused = FEpicodeEditorShellModule::IsFocusedModeRequested();
	const TSharedRef<FGlobalTabmanager> TabManager = FGlobalTabmanager::Get();
	TestEqual(TEXT("studio tab matches mode"),
		TabManager->HasTabSpawner(TEXT("EpicodeStudio")), !bFocused);
	TestEqual(TEXT("multiplayer tab matches mode"),
		TabManager->HasTabSpawner(TEXT("EpicodeMultiplayer")), !bFocused);
	TestEqual(TEXT("director tab matches mode"),
		TabManager->HasTabSpawner(TEXT("EpicodeDirectorWorkspace")), !bFocused);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEpicodeEditorShellAssetImportAsyncTest,
	"Epicode.EditorShell.AssetImportAsync",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeEditorShellAssetImportAsyncTest::RunTest(const FString& Parameters)
{
	FScopedEditorShellLayout LayoutGuard;
	UEpicodeSceneSubsystem* Scene = GetTestScene();
	if (!Scene || !StartTestScene(Scene, TEXT("Editor Shell Asset Import Test")))
	{
		AddError(TEXT("Failed to create the asset import test scene."));
		return false;
	}
	const int32 InitialEntityCount = Scene->GetDocument().Entities.Num();

	const FString FixturePath = FPaths::Combine(
		FPaths::ProjectDir(), TEXT("Tests/Fixtures/cube.fbx"));
	TestTrue(TEXT("asset import fixture exists"), IFileManager::Get().FileExists(*FixturePath));

	const FString Root = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("Epicode/Tests/AssetImport"),
		FGuid::NewGuid().ToString(EGuidFormats::Digits));
	IFileManager::Get().MakeDirectory(*Root, true);
	const FString SourcePath = FPaths::Combine(
		Root, FString::Printf(TEXT("async_prop_%s.fbx"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	TestTrue(TEXT("fixture copied"), IFileManager::Get().Copy(*SourcePath, *FixturePath) == COPY_OK);

	FString ImportedAssetPath;
	FString ImportedPackageDir;
	{
		TSharedRef<SEpicodeFocusedShell> Shell = SNew(SEpicodeFocusedShell);
		const TArray<FString> Before = Shell->GetAssetPathsForSmokeTest();
		const FString Destination = FString::Printf(
			TEXT("/Game/Epicode/Tests/AssetImportAsync/%s"),
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		TestTrue(TEXT("async import submitted"), Shell->StartAssetImportForSmokeTest(
			SourcePath, TEXT("Prop"), Destination));
		TestFalse(TEXT("async job id"), Shell->GetImportJobIdForSmokeTest().IsEmpty());
		TestEqual(TEXT("initial stage"), Shell->GetImportStageForSmokeTest(), FString(TEXT("queued")));
		TestTrue(TEXT("initial progress bounded"), Shell->GetImportProgressForSmokeTest() >= 0.0f
			&& Shell->GetImportProgressForSmokeTest() <= 1.0f);

		TickShellImport(Shell);
		TestTrue(TEXT("async job finished"), Shell->GetImportJobIdForSmokeTest().IsEmpty());
		TestTrue(TEXT("success outcome"), Shell->GetLastImportOutcomeForSmokeTest().Contains(TEXT("已导入")));
		TestTrue(TEXT("success reports progress"), FMath::IsNearlyEqual(
			Shell->GetLastImportProgressForSmokeTest(), 1.0f, 0.01f));

		const TArray<FString>& ImportedAssets = Shell->GetLastImportedAssetPathsForSmokeTest();
		TestTrue(TEXT("imported asset list"), ImportedAssets.Num() > 0);
		if (ImportedAssets.Num() > 0)
		{
			ImportedAssetPath = ImportedAssets[0];
		}
		TestFalse(TEXT("imported asset path"), ImportedAssetPath.IsEmpty());
		if (!ImportedAssetPath.IsEmpty())
		{
			const FString PackageName = FPackageName::ObjectPathToPackageName(ImportedAssetPath);
			const FString PackageFile = FPackageName::LongPackageNameToFilename(
				PackageName, FPackageName::GetAssetPackageExtension());
			TestTrue(TEXT("imported package saved"), IFileManager::Get().FileExists(*PackageFile));
			ImportedPackageDir = FPaths::GetPath(PackageFile);
		}
		TestEqual(TEXT("isolated destination leaves library unchanged"),
			Shell->GetAssetPathsForSmokeTest().Num(), Before.Num());

		Shell->SetImportConflictChoiceForSmokeTest(
			EEpicodeImportConflictChoice::Cancel);
		TestFalse(TEXT("conflict cancel aborts submission"),
			Shell->StartAssetImportForSmokeTest(
				SourcePath, TEXT("Prop"), Destination));
		TestTrue(TEXT("conflict cancel message"),
			Shell->GetLastMessageForSmokeTest().Contains(TEXT("取消")));
		TestTrue(TEXT("conflict cancel leaves no job"),
			Shell->GetImportJobIdForSmokeTest().IsEmpty());

		Shell->SetImportConflictChoiceForSmokeTest(
			EEpicodeImportConflictChoice::Overwrite);
		TestTrue(TEXT("conflict overwrite submits"),
			Shell->StartAssetImportForSmokeTest(
				SourcePath, TEXT("Prop"), Destination));
		TickShellImport(Shell);
		TestTrue(TEXT("conflict overwrite succeeds"),
			Shell->GetLastImportOutcomeForSmokeTest().Contains(TEXT("已导入")));
		if (!ImportedAssetPath.IsEmpty())
		{
			const FString PackageName =
				FPackageName::ObjectPathToPackageName(ImportedAssetPath);
			const FString RepackedFile = FPackageName::LongPackageNameToFilename(
				PackageName, FPackageName::GetAssetPackageExtension());
			TestTrue(TEXT("overwrite keeps package"), IFileManager::Get().FileExists(*RepackedFile));
		}

		Shell->SetImportConflictChoiceForSmokeTest(
			EEpicodeImportConflictChoice::Copy);
		TestTrue(TEXT("conflict copy submits"),
			Shell->StartAssetImportForSmokeTest(
				SourcePath, TEXT("Prop"), Destination));
		TickShellImport(Shell);
		TestTrue(TEXT("conflict copy succeeds"),
			Shell->GetLastImportOutcomeForSmokeTest().Contains(TEXT("已导入")));
		if (!ImportedAssetPath.IsEmpty())
		{
			TestFalse(TEXT("copy uses a new asset path"),
				Shell->GetLastImportedAssetPathsForSmokeTest().Contains(ImportedAssetPath));
		}

		TestEqual(TEXT("scene entities cleaned after import"),
			Scene->GetDocument().Entities.Num(), InitialEntityCount);
		Shell->RefreshLibraryForSmokeTest();
	}
	if (!ImportedAssetPath.IsEmpty())
	{
		if (UObject* ImportedObject = LoadObject<UObject>(nullptr, *ImportedAssetPath))
		{
			FAssetRegistryModule& AssetRegistryModule =
				FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			AssetRegistryModule.Get().AssetDeleted(ImportedObject);
		}
	}
	if (!ImportedPackageDir.IsEmpty())
	{
		IFileManager::Get().DeleteDirectory(
			*FPaths::Combine(FPaths::ProjectContentDir(),
				TEXT("Epicode/Tests/AssetImportAsync")),
			false, true);
	}
	IFileManager::Get().DeleteDirectory(*Root, false, true);

	const FString CancelRoot = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("Epicode/Tests/AssetImportCancel"),
		FGuid::NewGuid().ToString(EGuidFormats::Digits));
	IFileManager::Get().MakeDirectory(*CancelRoot, true);
	const FString CancelSource = FPaths::Combine(CancelRoot, TEXT("cancel_prop.fbx"));
	TestTrue(TEXT("cancel fixture copied"),
		IFileManager::Get().Copy(*CancelSource, *FixturePath) == COPY_OK);
	{
		TSharedRef<SEpicodeFocusedShell> Shell = SNew(SEpicodeFocusedShell);
		const int32 BeforeCount = Shell->GetAssetCountForSmokeTest();
		TestTrue(TEXT("cancel import submitted"),
			Shell->StartAssetImportForSmokeTest(CancelSource, TEXT("Prop")));
		TestTrue(TEXT("cancel requested"), Shell->CancelAssetImportForSmokeTest());
		TickShellImport(Shell);
		TestTrue(TEXT("cancel job cleared"), Shell->GetImportJobIdForSmokeTest().IsEmpty());
		TestTrue(TEXT("cancel outcome"),
			Shell->GetLastImportOutcomeForSmokeTest().Contains(TEXT("取消")));
		TestEqual(TEXT("cancel keeps asset count"),
			Shell->GetAssetCountForSmokeTest(), BeforeCount);
	}
	IFileManager::Get().DeleteDirectory(*CancelRoot, false, true);

	const FString FailureRoot = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("Epicode/Tests/AssetImportFailure"),
		FGuid::NewGuid().ToString(EGuidFormats::Digits));
	IFileManager::Get().MakeDirectory(*FailureRoot, true);
	const FString FailureSource = FPaths::Combine(FailureRoot, TEXT("broken_prop.fbx"));
	FFileHelper::SaveStringToFile(TEXT("not a valid fbx"), *FailureSource);
	{
		TSharedRef<SEpicodeFocusedShell> Shell = SNew(SEpicodeFocusedShell);
		const int32 BeforeCount = Shell->GetAssetCountForSmokeTest();
		TestTrue(TEXT("failure import submitted"),
			Shell->StartAssetImportForSmokeTest(FailureSource, TEXT("Prop")));
		IFileManager::Get().Delete(*FailureSource, false, true, true);
		TickShellImport(Shell);
		TestTrue(TEXT("failure job cleared"), Shell->GetImportJobIdForSmokeTest().IsEmpty());
		TestTrue(TEXT("failure outcome"),
			Shell->GetLastImportOutcomeForSmokeTest().Contains(TEXT("失败")));
		TestEqual(TEXT("failure keeps asset count"),
			Shell->GetAssetCountForSmokeTest(), BeforeCount);
		TestEqual(TEXT("failure leaves scene clean"),
			Scene->GetDocument().Entities.Num(), InitialEntityCount);
	}
	IFileManager::Get().DeleteDirectory(*FailureRoot, false, true);

	const FString TestSceneId = Scene->GetCurrentSceneId();
	FString CloseError;
	if (Scene->HasScene())
	{
		Scene->CloseScene(true, CloseError);
	}
	if (!TestSceneId.IsEmpty())
	{
		FString TrashPath;
		FString DeleteError;
		if (Scene->DeleteScene(TestSceneId, TrashPath, DeleteError))
		{
			IFileManager::Get().DeleteDirectory(*TrashPath, false, true);
		}
	}
	{
		FEpicodeLocalLibrary Library;
		FString LibraryError;
		Library.Refresh(LibraryError);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEpicodeEditorShellAssetDeleteTest,
	"Epicode.EditorShell.AssetDelete",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeEditorShellAssetDeleteTest::RunTest(const FString& Parameters)
{
	FScopedEditorShellLayout LayoutGuard;
	UEpicodeSceneSubsystem* Scene = GetTestScene();
	UEpicodeEngineSubsystem* Engine = GetTestEngine();
	if (!Scene || !Engine || !StartTestScene(Scene, TEXT("Editor Shell Asset Delete Test")))
	{
		AddError(TEXT("Failed to create the asset delete test scene."));
		return false;
	}

	const FString FixturePath = FPaths::Combine(
		FPaths::ProjectDir(), TEXT("Tests/Fixtures/cube.fbx"));
	const FString Root = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("Epicode/Tests/AssetDelete"),
		FGuid::NewGuid().ToString(EGuidFormats::Digits));
	IFileManager::Get().MakeDirectory(*Root, true);
	const FString SourcePath = FPaths::Combine(Root, TEXT("delete_prop.fbx"));
	TestTrue(TEXT("fixture copied"),
		IFileManager::Get().Copy(*SourcePath, *FixturePath) == COPY_OK);

	TSharedPtr<SEpicodeFocusedShell> Shell = SNew(SEpicodeFocusedShell);
	const FString Destination = FString::Printf(
		TEXT("/Game/Epicode/Tests/AssetDelete/%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	TestTrue(TEXT("asset imported for delete test"), Shell->StartAssetImportForSmokeTest(
		SourcePath, TEXT("Prop"), Destination));
	TickShellImport(Shell.ToSharedRef());
	TestTrue(TEXT("asset import succeeded"),
		Shell->GetLastImportOutcomeForSmokeTest().Contains(TEXT("已导入")));

	FString MeshAssetPath;
	for (const FString& AssetPath : Shell->GetLastImportedAssetPathsForSmokeTest())
	{
		if (Cast<UStaticMesh>(LoadObject<UObject>(nullptr, *AssetPath)))
		{
			MeshAssetPath = AssetPath;
			break;
		}
	}
	TestFalse(TEXT("static mesh asset found"), MeshAssetPath.IsEmpty());
	if (MeshAssetPath.IsEmpty())
	{
		Shell->RefreshLibraryForSmokeTest();
		FString CleanupError;
		Scene->CloseScene(true, CleanupError);
		IFileManager::Get().DeleteDirectory(*Root, false, true);
		return false;
	}

	FString PackageFile;
	TestTrue(TEXT("asset package path"),
		FPackageName::TryConvertLongPackageNameToFilename(
			FPackageName::ObjectPathToPackageName(MeshAssetPath),
			PackageFile, FPackageName::GetAssetPackageExtension()));
	TestTrue(TEXT("asset package exists"), IFileManager::Get().FileExists(*PackageFile));

	const FVector DropLocation(320.0, -180.0, 0.0);
	TestTrue(TEXT("asset placed at drop location"),
		Shell->PlaceAssetAtForSmokeTest(MeshAssetPath, TEXT("Prop"), DropLocation));
	TestTrue(TEXT("drop placement keeps selection"),
		Shell->GetSelectedLocationForSmokeTest().Equals(DropLocation, 1.0));
	const FString EntityId = Shell->GetSelectedEntityIdForSmokeTest();
	TestFalse(TEXT("drop placement selected entity"), EntityId.IsEmpty());

	FString DeleteError;
	TestFalse(TEXT("referenced asset deletion blocked"),
		Shell->DeleteLibraryAssetForSmokeTest(MeshAssetPath, TEXT("delete_prop"), DeleteError));
	TestTrue(TEXT("blocked deletion reports reference"), DeleteError.Contains(TEXT("引用")));
	TestTrue(TEXT("blocked deletion keeps file"), IFileManager::Get().FileExists(*PackageFile));

	if (!EntityId.IsEmpty())
	{
		TSharedPtr<FJsonObject> DeleteParams = MakeShared<FJsonObject>();
		DeleteParams->SetStringField(TEXT("id"), EntityId);
		TestTrue(TEXT("referencing prop deleted"),
			ExecuteTestCommand(Engine, TEXT("entity.delete"), DeleteParams).bSuccess);
	}

	FEpicodeSceneDocument SavedDocument =
		FEpicodeSceneDocument::CreateDefault(TEXT("Saved Asset Reference Test"));
	FEpicodeEntityRecord SavedEntity;
	SavedEntity.Id = FGuid::NewGuid();
	SavedEntity.Type = EEpicodeEntityType::Prop;
	SavedEntity.Name = TEXT("Saved Referenced Prop");
	SavedEntity.Data = MakeShared<FJsonObject>();
	SavedEntity.Data->SetStringField(TEXT("staticMesh"), MeshAssetPath);
	SavedDocument.Entities.Add(SavedEntity);
	FString SavedDocumentPath;
	FString SavedSceneError;
	TestTrue(TEXT("saved scene reference written"),
		FEpicodeSceneLibrary::Save(
			SavedDocument,
			UEpicodeSettings::Get()->GetSceneRootPath(),
			SavedDocumentPath,
			SavedSceneError));
	TestFalse(TEXT("saved scene reference blocks deletion"),
		Shell->DeleteLibraryAssetForSmokeTest(MeshAssetPath, TEXT("delete_prop"), DeleteError));
	TestTrue(TEXT("saved reference reported"),
		DeleteError.Contains(TEXT("Saved Asset Reference Test")));
	IFileManager::Get().DeleteDirectory(*FPaths::GetPath(SavedDocumentPath), false, true);

	TestTrue(TEXT("unreferenced asset deleted"),
		Shell->DeleteLibraryAssetForSmokeTest(MeshAssetPath, TEXT("delete_prop"), DeleteError));
	TestFalse(TEXT("deleted asset file removed"), IFileManager::Get().FileExists(*PackageFile));

	const FString DestinationRoot = FPaths::Combine(
		FPaths::ProjectContentDir(), TEXT("Epicode/Tests/AssetDelete"));
	Shell->RefreshLibraryForSmokeTest();
	Shell.Reset();
	IFileManager::Get().DeleteDirectory(*DestinationRoot, false, true);
	IFileManager::Get().DeleteDirectory(*Root, false, true);

	const FString TestSceneId = Scene->GetCurrentSceneId();
	FString CloseError;
	if (Scene->HasScene())
	{
		Scene->CloseScene(true, CloseError);
	}
	if (!TestSceneId.IsEmpty())
	{
		FString TrashPath;
		FString DeleteSceneError;
		if (Scene->DeleteScene(TestSceneId, TrashPath, DeleteSceneError))
		{
			IFileManager::Get().DeleteDirectory(*TrashPath, false, true);
		}
	}
	{
		FEpicodeLocalLibrary Library;
		FString LibraryError;
		Library.Refresh(LibraryError);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEpicodeAssistantChatTest,
	"Epicode.Assistant.Chat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeAssistantChatTest::RunTest(const FString& Parameters)
{
	FScopedEditorShellLayout LayoutGuard;
	const FString HistoryRoot = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("Epicode/Tests/AssistantChat"),
		FGuid::NewGuid().ToString(EGuidFormats::Digits));
	FConversationHistoryStore::FScopedRootOverride HistoryGuard(HistoryRoot);
	IFileManager::Get().DeleteDirectory(*HistoryRoot, false, true);
	IFileManager::Get().MakeDirectory(*HistoryRoot, true);

	TSharedPtr<SEpicodeFocusedShell> Shell = SNew(SEpicodeFocusedShell);

	TSharedPtr<FMockConversationBackend> Mock = MakeShared<FMockConversationBackend>();
	Mock->ResponseChunks = { TEXT("先选中对象，"), TEXT("再设置位置。") };
	Shell->SetAssistantBackendForSmokeTest(Mock);
	TestEqual(TEXT("mock backend active"),
		Shell->GetChatBackendNameForSmokeTest(), FString(TEXT("mock")));

	TestTrue(TEXT("message accepted"),
		Shell->SendChatMessageForSmokeTest(TEXT("如何移动对象？")));
	for (int32 Poll = 0; Poll < 10; ++Poll)
	{
		Shell->PollChatForSmokeTest();
	}
	const FString Transcript = Shell->GetChatTranscriptForSmokeTest();
	TestTrue(TEXT("user message in transcript"), Transcript.Contains(TEXT("如何移动对象？")));
	TestTrue(TEXT("streamed assistant text"), Transcript.Contains(TEXT("再设置位置。")));
	TestEqual(TEXT("stream completed"), Shell->GetChatStatusForSmokeTest(), FString(TEXT("生成完成")));

	const FString ConversationId = Shell->GetChatConversationIdForSmokeTest();
	TestFalse(TEXT("conversation id assigned"), ConversationId.IsEmpty());
	const FString HistoryFile = FPaths::Combine(
		FConversationHistoryStore::GetHistoryRoot(),
		FConversationHistoryStore::SanitizeId(ConversationId) + TEXT(".json"));
	TestTrue(TEXT("history file written"), IFileManager::Get().FileExists(*HistoryFile));

	TestTrue(TEXT("conversation reloaded"),
		Shell->LoadChatConversationForSmokeTest(ConversationId));
	TestTrue(TEXT("reloaded transcript keeps assistant"),
		Shell->GetChatTranscriptForSmokeTest().Contains(TEXT("先选中对象")));

	TSharedPtr<FMockConversationBackend> ErrorBackend = MakeShared<FMockConversationBackend>();
	ErrorBackend->ResponseChunks = { TEXT("部分回复") };
	ErrorBackend->bErrorAfterChunks = true;
	ErrorBackend->StreamErrorCode = TEXT("assistant_test_error");
	ErrorBackend->StreamErrorMessage = TEXT("模拟后端失败");
	Shell->SetAssistantBackendForSmokeTest(ErrorBackend);
	TestTrue(TEXT("failing request accepted"),
		Shell->SendChatMessageForSmokeTest(TEXT("触发失败")));
	for (int32 Poll = 0; Poll < 10; ++Poll)
	{
		Shell->PollChatForSmokeTest();
	}
	TestEqual(TEXT("stream failed"), Shell->GetChatStatusForSmokeTest(), FString(TEXT("生成失败")));
	TestTrue(TEXT("error surfaced"),
		Shell->GetChatTranscriptForSmokeTest().Contains(TEXT("模拟后端失败")));

	TSharedPtr<FMockConversationBackend> SlowBackend = MakeShared<FMockConversationBackend>();
	SlowBackend->ResponseChunks = { TEXT("迟到") };
	SlowBackend->PollsBeforeChunks = 100;
	Shell->SetAssistantBackendForSmokeTest(SlowBackend);
	TestTrue(TEXT("slow request accepted"),
		Shell->SendChatMessageForSmokeTest(TEXT("停止测试")));
	Shell->StopChatForSmokeTest();
	TestEqual(TEXT("stream stopped"), Shell->GetChatStatusForSmokeTest(), FString(TEXT("已停止")));

	Shell->SetAssistantBackendByNameForSmokeTest(TEXT("unavailable"));
	TestFalse(TEXT("unavailable backend rejects send"),
		Shell->SendChatMessageForSmokeTest(TEXT("不会发送")));
	TestEqual(TEXT("unavailable status"),
		Shell->GetChatStatusForSmokeTest(), FString(TEXT("后端不可用")));

	Shell.Reset();
	IFileManager::Get().DeleteDirectory(*HistoryRoot, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEpicodeAssistantScriptFlowTest,
	"Epicode.Assistant.ScriptFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeAssistantScriptFlowTest::RunTest(const FString& Parameters)
{
	FScopedEditorShellLayout LayoutGuard;
	const FString HistoryRoot = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("Epicode/Tests/AssistantScriptFlow"),
		FGuid::NewGuid().ToString(EGuidFormats::Digits));
	FConversationHistoryStore::FScopedRootOverride HistoryGuard(HistoryRoot);
	IFileManager::Get().DeleteDirectory(*HistoryRoot, false, true);
	IFileManager::Get().MakeDirectory(*HistoryRoot, true);

	UEpicodeSceneSubsystem* Scene = GetTestScene();
	if (!Scene || !StartTestScene(Scene, TEXT("Assistant Script Flow")))
	{
		AddError(TEXT("scene is required"));
		return false;
	}

	TSharedPtr<SEpicodeFocusedShell> Shell = SNew(SEpicodeFocusedShell);
	TSharedPtr<FMockConversationBackend> Mock = MakeShared<FMockConversationBackend>();
	Mock->ResponseChunks = {
		TEXT("已生成脚本：\n```json\n{\"schemaVersion\":1,\"summary\":\"添加脚本灯\","),
		TEXT("\"operations\":[{\"clientId\":\"lamp\",\"method\":\"light.create\",")
			TEXT("\"params\":{\"lightType\":\"point\",\"name\":\"Chat Lamp\"}}]}\n```")
	};
	Shell->SetAssistantBackendForSmokeTest(Mock);
	TestTrue(TEXT("script request accepted"),
		Shell->SendChatMessageForSmokeTest(TEXT("加一盏灯")));
	for (int32 Poll = 0; Poll < 10; ++Poll)
	{
		Shell->PollChatForSmokeTest();
	}

	TestTrue(TEXT("script captured"), Shell->HasPendingChatScriptForSmokeTest());
	TestEqual(TEXT("script summary"),
		Shell->GetPendingChatScriptSummaryForSmokeTest(), FString(TEXT("添加脚本灯")));
	TestEqual(TEXT("script operation count"),
		Shell->GetPendingChatScriptOperationCountForSmokeTest(), 1);
	TestTrue(TEXT("script valid"),
		Shell->GetPendingChatScriptStatusForSmokeTest().Contains(TEXT("可以应用")));

	const int32 BeforeApply = Scene->GetDocument().Entities.Num();
	TestTrue(TEXT("script applied"), Shell->ApplyPendingChatScriptForSmokeTest());
	TestEqual(TEXT("script created entity"),
		Scene->GetDocument().Entities.Num(), BeforeApply + 1);
	TestTrue(TEXT("apply status"),
		Shell->GetPendingChatScriptStatusForSmokeTest().StartsWith(TEXT("已应用")));

	Shell->CancelPendingChatScriptForSmokeTest();
	TestFalse(TEXT("cancel clears card"), Shell->HasPendingChatScriptForSmokeTest());

	TSharedPtr<FMockConversationBackend> Invalid = MakeShared<FMockConversationBackend>();
	Invalid->ResponseChunks = {
		TEXT("```json\n{\"schemaVersion\":1,\"summary\":\"非法脚本\","),
		TEXT("\"operations\":[{\"method\":\"import.file\",\"params\":{}}]}\n```")
	};
	Shell->SetAssistantBackendForSmokeTest(Invalid);
	TestTrue(TEXT("invalid request accepted"),
		Shell->SendChatMessageForSmokeTest(TEXT("导入文件")));
	for (int32 Poll = 0; Poll < 10; ++Poll)
	{
		Shell->PollChatForSmokeTest();
	}
	TestTrue(TEXT("invalid script captured"), Shell->HasPendingChatScriptForSmokeTest());
	TestTrue(TEXT("invalid script rejected by validation"),
		Shell->GetPendingChatScriptStatusForSmokeTest().Contains(TEXT("校验失败")));
	for (int32 Poll = 0; Poll < 20; ++Poll)
	{
		Shell->PollChatForSmokeTest();
	}
	TestTrue(TEXT("auto repair stops after max attempts"),
		Shell->GetPendingChatScriptStatusForSmokeTest().Contains(TEXT("自动修复 2 次后仍失败")));
	const int32 BeforeInvalid = Scene->GetDocument().Entities.Num();
	TestFalse(TEXT("invalid script cannot apply"),
		Shell->ApplyPendingChatScriptForSmokeTest());
	TestEqual(TEXT("invalid script leaves scene unchanged"),
		Scene->GetDocument().Entities.Num(), BeforeInvalid);

	Shell->CancelPendingChatScriptForSmokeTest();
	Shell.Reset();
	FString CloseError;
	Scene->CloseScene(true, CloseError);
	IFileManager::Get().DeleteDirectory(*HistoryRoot, false, true);
	return true;
}

#endif
