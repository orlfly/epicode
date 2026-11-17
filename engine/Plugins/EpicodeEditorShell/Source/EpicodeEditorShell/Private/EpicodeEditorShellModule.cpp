#include "EpicodeEditorShellModule.h"

#include "Editor.h"
#include "EditorModeManager.h"
#include "Containers/Ticker.h"
#include "EpicodeCommand.h"
#include "EpicodeEngineSubsystem.h"
#include "EpicodeSceneSubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "LevelEditor.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "SEpicodeFocusedShell.h"
#include "Widgets/SWindow.h"

DEFINE_LOG_CATEGORY(LogEpicodeEditorShell);

namespace
{
	class FEpicodeFocusedTransactionSink final : public IEpicodeTransactionSink
	{
	public:
		virtual void BeginTransaction(const FString& Description) override
		{
			if (GEditor && TransactionIndex == INDEX_NONE)
			{
				TransactionIndex = GEditor->BeginTransaction(FText::FromString(Description));
				if (UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary())
				{
					if (UEpicodeUndoRecord* Record = Scene->GetUndoRecord())
					{
						Record->Modify();
					}
				}
			}
		}

		virtual void CommitTransaction() override
		{
			if (GEditor && TransactionIndex != INDEX_NONE)
			{
				GEditor->EndTransaction();
				TransactionIndex = INDEX_NONE;
			}
		}

		virtual void CancelTransaction() override
		{
			if (GEditor && TransactionIndex != INDEX_NONE)
			{
				GEditor->CancelTransaction(TransactionIndex);
				TransactionIndex = INDEX_NONE;
				if (UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary())
				{
					Scene->SyncUndoSnapshot();
				}
			}
		}

		virtual void MarkModified(UObject* Object) override
		{
			if (Object)
			{
				Object->Modify();
			}
		}

	private:
		int32 TransactionIndex = INDEX_NONE;
	};

	FAutoConsoleCommand GEpicodeFocusedEditorStatusCommand(
		TEXT("Epicode.FocusedEditor.Status"),
		TEXT("Logs whether the focused editor shell was requested and created."),
		FConsoleCommandDelegate::CreateStatic(&FEpicodeEditorShellModule::LogStatus));
	FAutoConsoleCommand GEpicodeFocusedEditorScreenshotCommand(
		TEXT("Epicode.FocusedEditor.Screenshot"),
		TEXT("Captures the focused editor shell window to Saved/Screenshots/EditorShellPoC.png."),
		FConsoleCommandDelegate::CreateStatic(&FEpicodeEditorShellModule::TakeScreenshot));
}

bool FEpicodeEditorShellModule::IsFocusedModeRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("EpicodeFocusedEditor"));
}

void FEpicodeEditorShellModule::StartupModule()
{
	bFocusedModeRequested = IsFocusedModeRequested();
	if (!bFocusedModeRequested)
	{
		UE_LOG(LogEpicodeEditorShell, Log, TEXT("EpicodeEditorShell loaded in legacy mode"));
		return;
	}

	EditorInitializedHandle = FEditorDelegates::OnEditorInitialized.AddRaw(
		this, &FEpicodeEditorShellModule::HandleEditorInitialized);
	EditorPreExitHandle = FEditorDelegates::OnEditorPreExit.AddRaw(
		this, &FEpicodeEditorShellModule::HandleEditorPreExit);

	UE_LOG(LogEpicodeEditorShell, Log, TEXT("EpicodeEditorShell focused mode requested"));
}

void FEpicodeEditorShellModule::ShutdownModule()
{
	if (TransactionSink.IsValid())
	{
		if (UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get())
		{
			if (Engine->GetTransactionSink() == TransactionSink.Get())
			{
				Engine->SetTransactionSink(nullptr);
			}
		}
		TransactionSink.Reset();
	}
	if (EditorInitializedHandle.IsValid())
	{
		FEditorDelegates::OnEditorInitialized.Remove(EditorInitializedHandle);
		EditorInitializedHandle.Reset();
	}
	if (EditorPreExitHandle.IsValid())
	{
		FEditorDelegates::OnEditorPreExit.Remove(EditorPreExitHandle);
		EditorPreExitHandle.Reset();
	}
	if (RetryHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RetryHandle);
		RetryHandle.Reset();
	}
	if (ScreenshotHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ScreenshotHandle);
		ScreenshotHandle.Reset();
	}
	if (SelectionHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(SelectionHandle);
		SelectionHandle.Reset();
	}
	if (GizmoSmokeHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(GizmoSmokeHandle);
		GizmoSmokeHandle.Reset();
	}
	if (WorkbenchSmokeHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(WorkbenchSmokeHandle);
		WorkbenchSmokeHandle.Reset();
	}
	if (ShortcutSmokeHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ShortcutSmokeHandle);
		ShortcutSmokeHandle.Reset();
	}
	if (ViewportRetrySmokeHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ViewportRetrySmokeHandle);
		ViewportRetrySmokeHandle.Reset();
	}
	if (SceneImportSmokeHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(SceneImportSmokeHandle);
		SceneImportSmokeHandle.Reset();
	}
	if (AssetLibrarySmokeHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(AssetLibrarySmokeHandle);
		AssetLibrarySmokeHandle.Reset();
	}
	if (InspectorSmokeHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(InspectorSmokeHandle);
		InspectorSmokeHandle.Reset();
	}

	if (ShellWidget.IsValid())
	{
		ShellWidget.Reset();
	}
	if (bChangedViewportITF)
	{
		UE_LOG(LogEpicodeEditorShell, Warning,
			TEXT("Viewport ITF state was not restored before editor shutdown"));
		bChangedViewportITF = false;
	}
	ShellWindow.Reset();
	StockEditorWindow.Reset();
}

void FEpicodeEditorShellModule::HandleEditorInitialized(double Duration)
{
	if (!bFocusedModeRequested || bShellCreationAttempted)
	{
		return;
	}
	bShellCreationAttempted = true;

	if (FApp::IsUnattended() || FParse::Param(FCommandLine::Get(), TEXT("nullrhi")))
	{
		UE_LOG(LogEpicodeEditorShell, Warning,
			TEXT("Focused editor window skipped in unattended/nullrhi mode"));
		return;
	}

	if (!FSlateApplication::IsInitialized())
	{
		FailFocusedStartup(TEXT("Slate application is not initialized"));
		return;
	}

	UE_LOG(LogEpicodeEditorShell, Log,
		TEXT("Editor initialized in %.3f seconds; creating focused shell"), Duration);

	EnsureTransactionSink();
	if (!TryCreateShellWindow())
	{
		// OnEditorInitialized can fire before the Level Editor tab has spawned.
		// Retry on the core ticker instead of falling back to the stock editor.
		RetryStartTime = FPlatformTime::Seconds();
		RetryHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FEpicodeEditorShellModule::HandleRetryTick),
			0.1f);
		UE_LOG(LogEpicodeEditorShell, Log,
			TEXT("Waiting for the LevelEditor instance before creating the focused shell"));
	}
}

void FEpicodeEditorShellModule::EnsureTransactionSink()
{
	if (TransactionSink.IsValid())
	{
		return;
	}
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	if (!Engine || Engine->GetTransactionSink() != nullptr)
	{
		return;
	}
	TransactionSink = MakeShared<FEpicodeFocusedTransactionSink>();
	Engine->SetTransactionSink(TransactionSink.Get());
	UE_LOG(LogEpicodeEditorShell, Log, TEXT("Focused editor transaction sink installed"));
}

bool FEpicodeEditorShellModule::TryCreateShellWindow()
{
	EnsureTransactionSink();
	FLevelEditorModule& LevelEditorModule =
		FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	TSharedPtr<ILevelEditor> LevelEditor = LevelEditorModule.GetFirstLevelEditor();
	if (!LevelEditor.IsValid())
	{
		return false;
	}

	if (!bChangedViewportITF)
	{
		bOriginalSupportsViewportITF = GLevelEditorModeTools().SupportsViewportITF();
		GLevelEditorModeTools().SetSupportsViewportITF(false);
		bChangedViewportITF = true;
		UE_LOG(LogEpicodeEditorShell, Log,
			TEXT("Focused mode uses the legacy transform widget (SupportsViewportITF=false)"));
	}

	// Capture the stock main frame before adding the focused window. Hiding the
	// OS window keeps the editor initialized and leaves --legacy startup intact.
	TSharedPtr<SWindow> StockWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
	if (!StockWindow.IsValid())
	{
		const TArray<TSharedRef<SWindow>> Windows = FSlateApplication::Get().GetTopLevelWindows();
		float LargestArea = 0.0f;
		for (const TSharedRef<SWindow>& Candidate : Windows)
		{
			const FVector2D Size = Candidate->GetSizeInScreen();
			const float Area = static_cast<float>(Size.X * Size.Y);
			if (Area > LargestArea)
			{
				LargestArea = Area;
				StockWindow = Candidate;
			}
		}
	}

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(FText::FromString(TEXT("Epicode")))
		.ClientSize(FVector2D(1600.0f, 900.0f))
		.SupportsMaximize(true)
		.SupportsMinimize(true)
		.UseOSWindowBorder(true)
		[
			SAssignNew(ShellWidget, SEpicodeFocusedShell)
				.LevelEditor(LevelEditor)
		];

	const bool bRetrySmoke = FParse::Param(
		FCommandLine::Get(), TEXT("EpicodeFocusedEditorViewportRetrySmoke"));
	if (!ShellWidget.IsValid() || (!ShellWidget->IsViewportReady() && !bRetrySmoke))
	{
		const FString Reason = ShellWidget.IsValid()
			? ShellWidget->GetViewportError()
			: FString(TEXT("Focused shell widget was not created"));
		FailFocusedStartup(Reason);
		return true;
	}

	Window->SetOnWindowClosed(
		FOnWindowClosed::CreateRaw(this, &FEpicodeEditorShellModule::HandleShellClosed));

	FSlateApplication::Get().AddWindow(Window);
	ShellWindow = Window;

	if (StockWindow.IsValid() && StockWindow != Window)
	{
		StockEditorWindow = StockWindow;
		StockWindow->HideWindow();
		UE_LOG(LogEpicodeEditorShell, Log,
			TEXT("Hid stock editor window '%s'"),
			*StockWindow->GetTitle().ToString());
	}

	Window->ShowWindow();
	ShellWidget->FocusViewport();
	if (FParse::Param(FCommandLine::Get(), TEXT("EpicodeFocusedEditorGizmoSmoke")))
	{
		GizmoSmokeStep = 0;
		GizmoSmokeHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FEpicodeEditorShellModule::HandleGizmoSmokeTick),
			1.5f);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("EpicodeFocusedEditorWorkbenchSmoke")))
	{
		WorkbenchSmokeStep = 0;
		WorkbenchSmokeHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FEpicodeEditorShellModule::HandleWorkbenchSmokeTick),
			1.0f);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("EpicodeFocusedEditorShortcutSmoke")))
	{
		ShortcutSmokeStep = 0;
		ShortcutSmokeHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FEpicodeEditorShellModule::HandleShortcutSmokeTick),
			1.0f);
	}
	else if (bRetrySmoke)
	{
		ViewportRetrySmokeStep = 0;
		ViewportRetrySmokeHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FEpicodeEditorShellModule::HandleViewportRetrySmokeTick),
			1.0f);
	}
	else if (FParse::Value(
		FCommandLine::Get(),
		TEXT("EpicodeFocusedEditorSceneImportSmoke="),
		SceneImportSmokePath))
	{
		SceneImportSmokeHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FEpicodeEditorShellModule::HandleSceneImportSmokeTick),
			1.0f);
	}
	else if (FParse::Value(
		FCommandLine::Get(),
		TEXT("EpicodeFocusedEditorAssetLibrarySmoke="),
		AssetLibrarySmokePath))
	{
		FParse::Value(
			FCommandLine::Get(),
			TEXT("EpicodeFocusedEditorAssetLibraryCharacterSmoke="),
			AssetLibrarySmokeCharacterPath);
		AssetLibrarySmokeStep = 0;
		AssetLibrarySmokeHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FEpicodeEditorShellModule::HandleAssetLibrarySmokeTick),
			1.0f);
	}
	else if (FParse::Param(
		FCommandLine::Get(), TEXT("EpicodeFocusedEditorInspectorSmoke")))
	{
		InspectorSmokeStep = 0;
		InspectorSmokeHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FEpicodeEditorShellModule::HandleInspectorSmokeTick),
			1.0f);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("EpicodeFocusedEditorSelectFirst")))
	{
		SelectionHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FEpicodeEditorShellModule::HandleSelectionTick),
			1.5f);
	}

	UE_LOG(LogEpicodeEditorShell, Log,
		TEXT("Focused editor shell created (stock window hidden: %s)"),
		StockEditorWindow.IsValid() ? TEXT("yes") : TEXT("no"));

	if (!FParse::Param(FCommandLine::Get(), TEXT("EpicodeFocusedEditorGizmoSmoke"))
		&& FParse::Param(FCommandLine::Get(), TEXT("EpicodeFocusedEditorScreenshot")))
	{
		ScreenshotHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FEpicodeEditorShellModule::HandleScreenshotTick),
			5.0f);
	}
	return true;
}

bool FEpicodeEditorShellModule::HandleRetryTick(float DeltaTime)
{
	if (ShellWindow.IsValid())
	{
		RetryHandle.Reset();
		return false;
	}

	if (TryCreateShellWindow())
	{
		RetryHandle.Reset();
		return false;
	}

	if (FPlatformTime::Seconds() - RetryStartTime > 30.0)
	{
		RetryHandle.Reset();
		FailFocusedStartup(TEXT("LevelEditor instance did not become available within 30 seconds"));
		return false;
	}

	return true;
}

bool FEpicodeEditorShellModule::HandleSelectionTick(float DeltaTime)
{
	SelectionHandle.Reset();
	if (ShellWidget.IsValid())
	{
		ShellWidget->SelectFirstActorForSmokeTest();
	}
	return false;
}

bool FEpicodeEditorShellModule::HandleGizmoSmokeTick(float DeltaTime)
{
	if (!ShellWidget.IsValid())
	{
		GizmoSmokeHandle.Reset();
		return false;
	}

	switch (GizmoSmokeStep++)
	{
	case 0:
		ShellWidget->SelectFirstActorForSmokeTest();
		break;
	case 1:
		WriteScreenshot(TEXT("EditorShellPoC-translate.png"));
		break;
	case 2:
		ShellWidget->SetWidgetMode(UE::Widget::WM_Rotate);
		break;
	case 3:
		WriteScreenshot(TEXT("EditorShellPoC-rotate.png"));
		break;
	case 4:
		ShellWidget->SetWidgetMode(UE::Widget::WM_Scale);
		break;
	case 5:
		WriteScreenshot(TEXT("EditorShellPoC-scale.png"));
		break;
	case 6:
		ShellWidget->MoveSelectedActorForUndoSmokeTest();
		break;
	default:
		GizmoSmokeHandle.Reset();
		UE_LOG(LogEpicodeEditorShell, Display, TEXT("FocusedEditor gizmo smoke complete"));
		if (FParse::Param(FCommandLine::Get(), TEXT("EpicodeFocusedEditorExitSmoke"))
			&& ShellWindow.IsValid())
		{
			UE_LOG(LogEpicodeEditorShell, Display,
				TEXT("FocusedEditor exit smoke: closing focused shell window"));
			ShellWindow->RequestDestroyWindow();
		}
		return false;
	}
	return true;
}

bool FEpicodeEditorShellModule::HandleWorkbenchSmokeTick(float DeltaTime)
{
	if (!ShellWidget.IsValid())
	{
		WorkbenchSmokeHandle.Reset();
		return false;
	}

	switch (WorkbenchSmokeStep++)
	{
	case 0:
		ShellWidget->CreateWorkbenchSceneForSmokeTest();
		break;
	case 1:
		ShellWidget->SpawnWorkbenchEntitiesForSmokeTest();
		break;
	case 2:
		ShellWidget->SelectFirstSceneEntityForSmokeTest();
		break;
	case 3:
		WriteScreenshot(TEXT("EditorShellWorkbench.png"));
		break;
	case 4:
		ShellWidget->SetPanelsForSmokeTest(false, false);
		break;
	case 5:
		WriteScreenshot(TEXT("EditorShellCollapsed.png"));
		break;
	case 6:
		ShellWidget->SetPanelsForSmokeTest(true, true);
		ShellWidget->OpenMenuForSmokeTest(TEXT("场景"));
		break;
	case 7:
		WriteScreenshot(TEXT("EditorShellMenu.png"));
		WriteActiveWindowScreenshot(TEXT("EditorShellMenuPopup.png"));
		break;
	default:
		ShellWidget->CloseMenuForSmokeTest(TEXT("场景"));
		WorkbenchSmokeHandle.Reset();
		UE_LOG(LogEpicodeEditorShell, Display, TEXT("FocusedEditor workbench smoke complete"));
		if (FParse::Param(FCommandLine::Get(), TEXT("EpicodeFocusedEditorExitSmoke"))
			&& ShellWindow.IsValid())
		{
			UE_LOG(LogEpicodeEditorShell, Display,
				TEXT("FocusedEditor exit smoke: closing focused shell window"));
			ShellWindow->RequestDestroyWindow();
		}
		return false;
	}
	return true;
}

bool FEpicodeEditorShellModule::HandleShortcutSmokeTick(float DeltaTime)
{
	if (!ShellWidget.IsValid())
	{
		ShortcutSmokeHandle.Reset();
		return false;
	}

	auto Process = [this](const FKey& Key, bool bCtrl, bool bShift, const TCHAR* Label)
	{
		const bool bHandled = ShellWidget->ProcessShortcutForSmokeTest(Key, bCtrl, bShift);
		UE_LOG(LogEpicodeEditorShell, Display,
			TEXT("FocusedEditor shortcut smoke: %s handled=%s mode=%s entities=%d"),
			Label,
			bHandled ? TEXT("true") : TEXT("false"),
			*ShellWidget->GetWidgetModeForSmokeTest(),
			ShellWidget->GetEntityCountForSmokeTest());
	};

	switch (ShortcutSmokeStep++)
	{
	case 0:
		ShellWidget->CreateWorkbenchSceneForSmokeTest();
		break;
	case 1:
		ShellWidget->SpawnWorkbenchEntitiesForSmokeTest();
		break;
	case 2:
		ShellWidget->SelectFirstSceneEntityForSmokeTest();
		break;
	case 3:
		Process(EKeys::W, false, false, TEXT("W"));
		break;
	case 4:
		Process(EKeys::E, false, false, TEXT("E"));
		break;
	case 5:
		Process(EKeys::R, false, false, TEXT("R"));
		break;
	case 6:
		Process(EKeys::Q, false, false, TEXT("Q"));
		break;
	case 7:
		Process(EKeys::F, false, false, TEXT("F"));
		break;
	case 8:
		Process(EKeys::Delete, false, false, TEXT("Delete"));
		break;
	case 9:
		Process(EKeys::Z, true, false, TEXT("Ctrl+Z"));
		break;
	case 10:
		Process(EKeys::S, true, false, TEXT("Ctrl+S"));
		break;
	default:
		ShortcutSmokeHandle.Reset();
		UE_LOG(LogEpicodeEditorShell, Display, TEXT("FocusedEditor shortcut smoke complete"));
		if (FParse::Param(FCommandLine::Get(), TEXT("EpicodeFocusedEditorExitSmoke"))
			&& ShellWindow.IsValid())
		{
			ShellWindow->RequestDestroyWindow();
		}
		return false;
	}
	return true;
}

bool FEpicodeEditorShellModule::HandleViewportRetrySmokeTick(float DeltaTime)
{
	if (!ShellWidget.IsValid())
	{
		ViewportRetrySmokeHandle.Reset();
		return false;
	}

	if (ViewportRetrySmokeStep++ == 0)
	{
		const bool bReady = ShellWidget->RetryViewportForSmokeTest();
		UE_LOG(LogEpicodeEditorShell, Display,
			TEXT("FocusedEditor viewport retry smoke: ready=%s"), bReady ? TEXT("true") : TEXT("false"));
		return true;
	}

	ViewportRetrySmokeHandle.Reset();
	WriteScreenshot(TEXT("EditorShellViewportRetry.png"));

	if (FParse::Param(FCommandLine::Get(), TEXT("EpicodeFocusedEditorExitSmoke"))
		&& ShellWindow.IsValid())
	{
		ShellWindow->RequestDestroyWindow();
	}
	return false;
}

bool FEpicodeEditorShellModule::HandleSceneImportSmokeTick(float DeltaTime)
{
	SceneImportSmokeHandle.Reset();
	if (!ShellWidget.IsValid())
	{
		return false;
	}

	const bool bImported = ShellWidget->ImportSceneFromFileForSmokeTest(SceneImportSmokePath);
	UE_LOG(LogEpicodeEditorShell, Display,
		TEXT("FocusedEditor scene import smoke: success=%s source=%s"),
		bImported ? TEXT("true") : TEXT("false"),
		*SceneImportSmokePath);
	WriteScreenshot(TEXT("EditorShellSceneImport.png"));

	if (FParse::Param(FCommandLine::Get(), TEXT("EpicodeFocusedEditorExitSmoke"))
		&& ShellWindow.IsValid())
	{
		ShellWindow->RequestDestroyWindow();
	}
	return false;
}

bool FEpicodeEditorShellModule::HandleAssetLibrarySmokeTick(float DeltaTime)
{
	if (!ShellWidget.IsValid())
	{
		AssetLibrarySmokeHandle.Reset();
		return false;
	}

	switch (AssetLibrarySmokeStep++)
	{
	case 0:
		ShellWidget->CreateWorkbenchSceneForSmokeTest();
		break;
	case 1:
	{
		const bool bImported = ShellWidget->ImportAssetForSmokeTest(
			AssetLibrarySmokePath, TEXT("Prop"));
		UE_LOG(LogEpicodeEditorShell, Display,
			TEXT("FocusedEditor asset library smoke: imported=%s assets=%d"),
			bImported ? TEXT("true") : TEXT("false"),
			ShellWidget->GetAssetCountForSmokeTest());
		break;
	}
	case 2:
	{
		if (AssetLibrarySmokeCharacterPath.IsEmpty())
		{
			return true;
		}
		const bool bImported = ShellWidget->ImportAssetForSmokeTest(
			AssetLibrarySmokeCharacterPath, TEXT("Character"));
		UE_LOG(LogEpicodeEditorShell, Display,
			TEXT("FocusedEditor asset library smoke: characterImported=%s assets=%d"),
			bImported ? TEXT("true") : TEXT("false"),
			ShellWidget->GetAssetCountForSmokeTest());
		break;
	}
	case 3:
	{
		const bool bPlaced = ShellWidget->PlaceFirstAssetForSmokeTest(TEXT("Prop"));
		UE_LOG(LogEpicodeEditorShell, Display,
			TEXT("FocusedEditor asset library smoke: placed=%s entities=%d"),
			bPlaced ? TEXT("true") : TEXT("false"),
			ShellWidget->GetEntityCountForSmokeTest());
		break;
	}
	case 4:
	{
		if (AssetLibrarySmokeCharacterPath.IsEmpty())
		{
			return true;
		}
		const bool bPlaced = ShellWidget->PlaceFirstAssetForSmokeTest(TEXT("Character"));
		UE_LOG(LogEpicodeEditorShell, Display,
			TEXT("FocusedEditor asset library smoke: characterPlaced=%s entities=%d"),
			bPlaced ? TEXT("true") : TEXT("false"),
			ShellWidget->GetEntityCountForSmokeTest());
		break;
	}
	case 5:
	{
		const bool bHidden = ShellWidget->HideFirstAssetForSmokeTest(TEXT("Prop"));
		UE_LOG(LogEpicodeEditorShell, Display,
			TEXT("FocusedEditor asset library smoke: hidden=%s assets=%d"),
			bHidden ? TEXT("true") : TEXT("false"),
			ShellWidget->GetAssetCountForSmokeTest());
		break;
	}
	case 6:
	{
		const bool bRestored = ShellWidget->RestoreHiddenAssetsForSmokeTest();
		UE_LOG(LogEpicodeEditorShell, Display,
			TEXT("FocusedEditor asset library smoke: restored=%s assets=%d"),
			bRestored ? TEXT("true") : TEXT("false"),
			ShellWidget->GetAssetCountForSmokeTest());
		break;
	}
	case 7:
		WriteScreenshot(TEXT("EditorShellAssetLibrary.png"));
		break;
	default:
		AssetLibrarySmokeHandle.Reset();
		if (FParse::Param(FCommandLine::Get(), TEXT("EpicodeFocusedEditorExitSmoke"))
			&& ShellWindow.IsValid())
		{
			ShellWindow->RequestDestroyWindow();
		}
		return false;
	}
	return true;
}

bool FEpicodeEditorShellModule::HandleInspectorSmokeTick(float DeltaTime)
{
	if (!ShellWidget.IsValid())
	{
		InspectorSmokeHandle.Reset();
		return false;
	}

	const FString FixtureDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Tests/Fixtures"));
	const FString PropPath = FPaths::Combine(FixtureDir, TEXT("cube.fbx"));
	const FString CharacterPath = FPaths::Combine(FixtureDir, TEXT("skeletal.fbx"));

	switch (InspectorSmokeStep++)
	{
	case 0:
		ShellWidget->CreateWorkbenchSceneForSmokeTest();
		ShellWidget->SpawnWorkbenchEntitiesForSmokeTest();
		break;
	case 1:
	{
		const bool bImported = ShellWidget->ImportAssetForSmokeTest(PropPath, TEXT("Prop"));
		UE_LOG(LogEpicodeEditorShell, Display,
			TEXT("FocusedEditor inspector smoke: propImported=%s"),
			bImported ? TEXT("true") : TEXT("false"));
		break;
	}
	case 2:
	{
		const bool bPlaced = ShellWidget->PlaceFirstAssetForSmokeTest(TEXT("Prop"));
		const bool bSelected = bPlaced && ShellWidget->SelectEntityByTypeForSmokeTest(TEXT("Prop"));
		const bool bMoved = bSelected && ShellWidget->SetSelectedLocationForSmokeTest(FVector(100.0, 200.0, 50.0));
		const FVector BeforeUndo = ShellWidget->GetSelectedLocationForSmokeTest();
		const double SnapGrid = ShellWidget->GetInspectorSnapGridForSmokeTest();
		ShellWidget->SetInspectorSnapForSmokeTest(true);
		ShellWidget->SetSelectedLocationForSmokeTest(FVector(SnapGrid * 7.0 + 3.0, SnapGrid * 4.0 + 3.0, SnapGrid + 3.0));
		const FVector Snapped = ShellWidget->GetSelectedLocationForSmokeTest();
		const bool bSnap = FMath::IsNearlyEqual(Snapped.X, SnapGrid * 7.0, 0.01)
			&& FMath::IsNearlyEqual(Snapped.Y, SnapGrid * 4.0, 0.01)
			&& FMath::IsNearlyEqual(Snapped.Z, SnapGrid, 0.01);
		ShellWidget->SetInspectorSnapForSmokeTest(false);
		ShellWidget->SetSelectedLocationForSmokeTest(BeforeUndo);
		const bool bHidden = bSelected && ShellWidget->SetSelectedVisibilityForSmokeTest(false);
		const bool bShown = bSelected && ShellWidget->SetSelectedVisibilityForSmokeTest(true);
		const bool bPhysics = bSelected && ShellWidget->SetSelectedPhysicsForSmokeTest(true);
		UE_LOG(LogEpicodeEditorShell, Display,
			TEXT("FocusedEditor inspector smoke: prop selected=%s moved=%s snap=%s grid=%.1f hidden=%s shown=%s physics=%s summary=%s"),
			bSelected ? TEXT("true") : TEXT("false"),
			bMoved ? TEXT("true") : TEXT("false"),
			bSnap ? TEXT("true") : TEXT("false"),
			SnapGrid,
			bHidden ? TEXT("true") : TEXT("false"),
			bShown ? TEXT("true") : TEXT("false"),
			bPhysics ? TEXT("true") : TEXT("false"),
			*ShellWidget->GetInspectorSummaryForSmokeTest());
		break;
	}
	case 3:
		WriteScreenshot(TEXT("EditorShellInspectorProp.png"));
		break;
	case 4:
	{
		const bool bSelected = ShellWidget->SelectEntityByTypeForSmokeTest(TEXT("Light"));
		const bool bIntensity = bSelected && ShellWidget->SetSelectedNumberForSmokeTest(TEXT("intensity"), 8000.0);
		const bool bAttenuation = bSelected && ShellWidget->SetSelectedNumberForSmokeTest(TEXT("attenuationRadius"), 1500.0);
		UE_LOG(LogEpicodeEditorShell, Display,
			TEXT("FocusedEditor inspector smoke: light selected=%s intensity=%s attenuation=%s summary=%s"),
			bSelected ? TEXT("true") : TEXT("false"),
			bIntensity ? TEXT("true") : TEXT("false"),
			bAttenuation ? TEXT("true") : TEXT("false"),
			*ShellWidget->GetInspectorSummaryForSmokeTest());
		break;
	}
	case 5:
		WriteScreenshot(TEXT("EditorShellInspectorLight.png"));
		break;
	case 6:
	{
		const bool bSelected = ShellWidget->SelectLastEntityOfTypeForSmokeTest(TEXT("Camera"));
		const bool bFov = bSelected && ShellWidget->SetSelectedNumberForSmokeTest(TEXT("fieldOfView"), 55.0);
		const bool bActive = bSelected && ShellWidget->ActivateSelectedCameraForSmokeTest();
		UE_LOG(LogEpicodeEditorShell, Display,
			TEXT("FocusedEditor inspector smoke: camera selected=%s fov=%s active=%s activeId=%s"),
			bSelected ? TEXT("true") : TEXT("false"),
			bFov ? TEXT("true") : TEXT("false"),
			bActive ? TEXT("true") : TEXT("false"),
			*ShellWidget->GetActiveCameraIdForSmokeTest().ToString(EGuidFormats::DigitsWithHyphens));
		break;
	}
	case 7:
		WriteScreenshot(TEXT("EditorShellInspectorCamera.png"));
		break;
	case 8:
	{
		const bool bImported = ShellWidget->ImportAssetForSmokeTest(CharacterPath, TEXT("Character"));
		UE_LOG(LogEpicodeEditorShell, Display,
			TEXT("FocusedEditor inspector smoke: characterImported=%s"),
			bImported ? TEXT("true") : TEXT("false"));
		break;
	}
	case 9:
	{
		const bool bPlaced = ShellWidget->PlaceFirstAssetForSmokeTest(TEXT("Character"));
		const bool bSelected = bPlaced && ShellWidget->SelectEntityByTypeForSmokeTest(TEXT("Character"));
		const bool bSpeed = bSelected && ShellWidget->SetSelectedAnimationSpeedForSmokeTest(2.5);
		const bool bPaused = bSelected && ShellWidget->SetSelectedPausedForSmokeTest(true);
		UE_LOG(LogEpicodeEditorShell, Display,
			TEXT("FocusedEditor inspector smoke: character selected=%s speed=%s paused=%s summary=%s"),
			bSelected ? TEXT("true") : TEXT("false"),
			bSpeed ? TEXT("true") : TEXT("false"),
			bPaused ? TEXT("true") : TEXT("false"),
			*ShellWidget->GetInspectorSummaryForSmokeTest());
		break;
	}
	case 10:
		WriteScreenshot(TEXT("EditorShellInspectorCharacter.png"));
		break;
	case 11:
	{
		const bool bSelected = ShellWidget->SelectEntityByTypeForSmokeTest(TEXT("Prop"));
		const FVector BeforeUndo = ShellWidget->GetSelectedLocationForSmokeTest();
		ShellWidget->SetSelectedLocationForSmokeTest(FVector(400.0, 410.0, 420.0));
		const bool bUndo = ShellWidget->ProcessShortcutForSmokeTest(EKeys::Z, true, false);
		const bool bReselected = ShellWidget->SelectEntityByTypeForSmokeTest(TEXT("Prop"));
		const bool bUndoRestored = bReselected
			&& ShellWidget->GetSelectedLocationForSmokeTest().Equals(BeforeUndo, 0.01);
		UE_LOG(LogEpicodeEditorShell, Display,
			TEXT("FocusedEditor inspector smoke: undo selected=%s handled=%s reselected=%s restored=%s"),
			bSelected ? TEXT("true") : TEXT("false"),
			bUndo ? TEXT("true") : TEXT("false"),
			bReselected ? TEXT("true") : TEXT("false"),
			bUndoRestored ? TEXT("true") : TEXT("false"));
		break;
	}
	default:
		InspectorSmokeHandle.Reset();
		UE_LOG(LogEpicodeEditorShell, Display, TEXT("FocusedEditor inspector smoke complete"));
		if (FParse::Param(FCommandLine::Get(), TEXT("EpicodeFocusedEditorExitSmoke"))
			&& ShellWindow.IsValid())
		{
			ShellWindow->RequestDestroyWindow();
		}
		return false;
	}
	return true;
}

bool FEpicodeEditorShellModule::HandleScreenshotTick(float DeltaTime)
{
	ScreenshotHandle.Reset();
	TakeScreenshot();
	return false;
}

void FEpicodeEditorShellModule::HandleShellClosed(const TSharedRef<SWindow>& Window)
{
	UE_LOG(LogEpicodeEditorShell, Log, TEXT("Focused editor shell closed"));
	ShellWidget.Reset();
	ShellWindow.Reset();

	if (TSharedPtr<SWindow> StockWindow = StockEditorWindow.Pin())
	{
		StockWindow->ShowWindow();
		StockEditorWindow.Reset();
		if (FParse::Param(FCommandLine::Get(), TEXT("EpicodeFocusedEditorExitSmoke")))
		{
			UE_LOG(LogEpicodeEditorShell, Display,
				TEXT("FocusedEditor exit smoke: stock editor restored, requesting exit"));
			FPlatformMisc::RequestExit(false);
		}
		return;
	}

	FPlatformMisc::RequestExit(false);
}

void FEpicodeEditorShellModule::HandleEditorPreExit()
{
	if (!bChangedViewportITF)
	{
		return;
	}

	GLevelEditorModeTools().SetSupportsViewportITF(bOriginalSupportsViewportITF);
	bChangedViewportITF = false;
	UE_LOG(LogEpicodeEditorShell, Log, TEXT("Restored level editor viewport ITF state"));
}

void FEpicodeEditorShellModule::FailFocusedStartup(const FString& Reason)
{
	UE_LOG(LogEpicodeEditorShell, Error,
		TEXT("Focused editor startup failed: %s"), *Reason);

	if (!FApp::IsUnattended() && !FParse::Param(FCommandLine::Get(), TEXT("nullrhi")))
	{
		FPlatformMisc::RequestExitWithStatus(true, 2);
	}
}

void FEpicodeEditorShellModule::LogStatus()
{
	const FEpicodeEditorShellModule* Module =
		FModuleManager::GetModulePtr<FEpicodeEditorShellModule>(TEXT("EpicodeEditorShell"));
	const bool bRequested = IsFocusedModeRequested();
	const bool bShell = Module && Module->ShellWindow.IsValid();
	const bool bStockHidden = Module && Module->StockEditorWindow.IsValid();

	UE_LOG(LogEpicodeEditorShell, Display,
		TEXT("FocusedEditor status: requested=%s shell=%s stock_hidden=%s"),
		bRequested ? TEXT("true") : TEXT("false"),
		bShell ? TEXT("true") : TEXT("false"),
		bStockHidden ? TEXT("true") : TEXT("false"));
}

void FEpicodeEditorShellModule::TakeScreenshot()
{
	WriteScreenshot(TEXT("EditorShellPoC.png"));
}

void FEpicodeEditorShellModule::WriteScreenshot(const FString& FileName)
{
	FEpicodeEditorShellModule* Module =
		FModuleManager::GetModulePtr<FEpicodeEditorShellModule>(TEXT("EpicodeEditorShell"));
	if (!Module || !Module->ShellWindow.IsValid())
	{
		UE_LOG(LogEpicodeEditorShell, Warning,
			TEXT("FocusedEditor screenshot skipped: focused shell window is not available"));
		return;
	}

	TArray<FColor> Colors;
	FIntVector Size;
	if (!FSlateApplication::Get().TakeScreenshot(
		Module->ShellWindow.ToSharedRef(), Colors, Size)
		|| Size.X <= 0 || Size.Y <= 0 || Colors.Num() == 0)
	{
		UE_LOG(LogEpicodeEditorShell, Error,
			TEXT("FocusedEditor screenshot failed: Slate returned no image"));
		return;
	}

	const FString Path = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("Screenshots"), FileName);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);

	TArray64<uint8> PngData;
	FImageUtils::PNGCompressImageArray(
		Size.X, Size.Y, TArrayView64<const FColor>(Colors.GetData(), Colors.Num()), PngData);
	if (!FFileHelper::SaveArrayToFile(PngData, *Path))
	{
		UE_LOG(LogEpicodeEditorShell, Error,
			TEXT("FocusedEditor screenshot could not be written: %s"), *Path);
		return;
	}

	UE_LOG(LogEpicodeEditorShell, Display,
		TEXT("FocusedEditor screenshot written: %s (%dx%d, %d bytes)"),
		*Path, Size.X, Size.Y, PngData.Num());
}

void FEpicodeEditorShellModule::WriteActiveWindowScreenshot(const FString& FileName)
{
	const TSharedPtr<SWindow> ActiveWindow =
		FSlateApplication::Get().GetActiveTopLevelWindow();
	if (!ActiveWindow.IsValid())
	{
		UE_LOG(LogEpicodeEditorShell, Warning,
			TEXT("FocusedEditor active-window screenshot skipped: no active window"));
		return;
	}

	UE_LOG(LogEpicodeEditorShell, Display,
		TEXT("FocusedEditor active window: title='%s' size=%s"),
		*ActiveWindow->GetTitle().ToString(),
		*ActiveWindow->GetSizeInScreen().ToString());

	TArray<FColor> Colors;
	FIntVector Size;
	if (!FSlateApplication::Get().TakeScreenshot(ActiveWindow.ToSharedRef(), Colors, Size)
		|| Size.X <= 0 || Size.Y <= 0 || Colors.Num() == 0)
	{
		UE_LOG(LogEpicodeEditorShell, Error,
			TEXT("FocusedEditor active-window screenshot failed: Slate returned no image"));
		return;
	}

	const FString Path = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("Screenshots"), FileName);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);

	TArray64<uint8> PngData;
	FImageUtils::PNGCompressImageArray(
		Size.X, Size.Y, TArrayView64<const FColor>(Colors.GetData(), Colors.Num()), PngData);
	if (!FFileHelper::SaveArrayToFile(PngData, *Path))
	{
		UE_LOG(LogEpicodeEditorShell, Error,
			TEXT("FocusedEditor active-window screenshot could not be written: %s"), *Path);
		return;
	}

	UE_LOG(LogEpicodeEditorShell, Display,
		TEXT("FocusedEditor active-window screenshot written: %s (%dx%d, %d bytes)"),
		*Path, Size.X, Size.Y, PngData.Num());
}

IMPLEMENT_MODULE(FEpicodeEditorShellModule, EpicodeEditorShell);
