#include "EpicodeEditorShellModule.h"

#include "Editor.h"
#include "EditorModeManager.h"
#include "Containers/Ticker.h"
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

bool FEpicodeEditorShellModule::TryCreateShellWindow()
{
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

	if (!ShellWidget.IsValid() || !ShellWidget->IsViewportReady())
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

IMPLEMENT_MODULE(FEpicodeEditorShellModule, EpicodeEditorShell);
