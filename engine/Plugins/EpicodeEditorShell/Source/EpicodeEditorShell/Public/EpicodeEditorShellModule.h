#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class SWindow;
class SEpicodeFocusedShell;

DECLARE_LOG_CATEGORY_EXTERN(LogEpicodeEditorShell, Log, All);

class FEpicodeEditorShellModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** True when the editor was launched with -EpicodeFocusedEditor. */
	static bool IsFocusedModeRequested();

	/** Console command implementation used by headless smoke tests. */
	static void LogStatus();
	static void TakeScreenshot();

private:
	void HandleEditorInitialized(double Duration);
	bool TryCreateShellWindow();
	bool HandleRetryTick(float DeltaTime);
	bool HandleSelectionTick(float DeltaTime);
	bool HandleGizmoSmokeTick(float DeltaTime);
	bool HandleWorkbenchSmokeTick(float DeltaTime);
	bool HandleShortcutSmokeTick(float DeltaTime);
	bool HandleViewportRetrySmokeTick(float DeltaTime);
	bool HandleSceneImportSmokeTick(float DeltaTime);
	bool HandleAssetLibrarySmokeTick(float DeltaTime);
	bool HandleScreenshotTick(float DeltaTime);
	void HandleShellClosed(const TSharedRef<SWindow>& Window);
	void HandleEditorPreExit();
	void FailFocusedStartup(const FString& Reason);
	static void WriteScreenshot(const FString& FileName);
	static void WriteActiveWindowScreenshot(const FString& FileName);

	TSharedPtr<SWindow> ShellWindow;
	TSharedPtr<SEpicodeFocusedShell> ShellWidget;
	TWeakPtr<SWindow> StockEditorWindow;
	FDelegateHandle EditorInitializedHandle;
	FDelegateHandle EditorPreExitHandle;
	FTSTicker::FDelegateHandle RetryHandle;
	FTSTicker::FDelegateHandle SelectionHandle;
	FTSTicker::FDelegateHandle GizmoSmokeHandle;
	FTSTicker::FDelegateHandle WorkbenchSmokeHandle;
	FTSTicker::FDelegateHandle ShortcutSmokeHandle;
	FTSTicker::FDelegateHandle ViewportRetrySmokeHandle;
	FTSTicker::FDelegateHandle SceneImportSmokeHandle;
	FTSTicker::FDelegateHandle AssetLibrarySmokeHandle;
	FTSTicker::FDelegateHandle ScreenshotHandle;
	int32 GizmoSmokeStep = 0;
	int32 WorkbenchSmokeStep = 0;
	int32 ShortcutSmokeStep = 0;
	int32 ViewportRetrySmokeStep = 0;
	FString SceneImportSmokePath;
	FString AssetLibrarySmokePath;
	FString AssetLibrarySmokeCharacterPath;
	int32 AssetLibrarySmokeStep = 0;
	double RetryStartTime = 0.0;
	bool bFocusedModeRequested = false;
	bool bShellCreationAttempted = false;
	bool bChangedViewportITF = false;
	bool bOriginalSupportsViewportITF = true;
};
