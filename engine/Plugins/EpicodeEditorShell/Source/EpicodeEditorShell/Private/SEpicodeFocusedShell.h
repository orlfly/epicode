#pragma once

#include "CoreMinimal.h"
#include "EpicodeFocusedShellModel.h"
#include "EpicodeTypes.h"
#include "InputCoreTypes.h"
#include "SceneTypes.h"
#include "UnrealWidgetFwd.h"
#include "Widgets/SCompoundWidget.h"

class AActor;
class FMenuBuilder;
class ILevelEditor;
class SEditableTextBox;
class SMultiLineEditableTextBox;
class SBox;
class SScrollBox;
class STextBlock;
class SVerticalBox;
class SEpicodeFocusedLevelViewport;

class SEpicodeFocusedShell : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEpicodeFocusedShell)
		{}
		SLATE_ARGUMENT(TSharedPtr<ILevelEditor>, LevelEditor)
	SLATE_END_ARGS()

	virtual ~SEpicodeFocusedShell() override;
	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	bool IsViewportReady() const { return Viewport.IsValid(); }
	const FString& GetViewportError() const { return ViewportError; }
	void CreateWorkbenchSceneForSmokeTest();
	void SpawnWorkbenchEntitiesForSmokeTest();
	void SelectFirstSceneEntityForSmokeTest();
	void SetPanelsForSmokeTest(bool bLeftVisible, bool bRightVisible);
	void OpenMenuForSmokeTest(const FString& Label);
	void CloseMenuForSmokeTest(const FString& Label);
	TArray<FString> GetMenuLabelsForSmokeTest() const;
	bool ProcessShortcutForSmokeTest(const FKey& Key, bool bCtrl = false, bool bShift = false);
	bool RetryViewportForSmokeTest();
	bool ImportSceneFromFileForSmokeTest(const FString& SourcePath);
	bool ImportAssetForSmokeTest(const FString& SourcePath, const FString& Kind);
	bool PlaceFirstAssetForSmokeTest(const FString& Type);
	bool HideFirstAssetForSmokeTest(const FString& Type);
	bool RestoreHiddenAssetsForSmokeTest();
	void SetInspectorWorldSpaceForSmokeTest(bool bWorldSpace);
	void SetInspectorSnapForSmokeTest(bool bSnap);
	double GetInspectorSnapGridForSmokeTest() const { return InspectorSnapGrid.X; }
	bool SetSelectedLocationForSmokeTest(const FVector& Location);
	bool SetSelectedVisibilityForSmokeTest(bool bVisible);
	bool SetSelectedNumberForSmokeTest(const FString& Field, double Value);
	bool SetSelectedPhysicsForSmokeTest(bool bSimulate);
	bool SetSelectedAnimationSpeedForSmokeTest(double PlayRate);
	bool SetSelectedPausedForSmokeTest(bool bPaused);
	bool SelectEntityByTypeForSmokeTest(const FString& Type);
	bool SelectLastEntityOfTypeForSmokeTest(const FString& Type);
	bool ActivateSelectedCameraForSmokeTest();
	FVector GetSelectedLocationForSmokeTest() const;
	bool GetSelectedVisibilityForSmokeTest() const;
	double GetSelectedNumberForSmokeTest(const FString& Field) const;
	FString GetSelectedDataStringForSmokeTest(const FString& Field) const;
	FGuid GetActiveCameraIdForSmokeTest() const { return Model.GetActiveCameraId(); }
	FString GetInspectorSummaryForSmokeTest() const;
	int32 GetEntityCountForSmokeTest() const { return Model.GetEntityCount(); }
	int32 GetAssetCountForSmokeTest() const { return Model.GetAssets().Num(); }
	FString GetWidgetModeForSmokeTest() const { return GetTransformModeLabel(); }
	void FocusViewport();
	void SelectFirstActorForSmokeTest();
	void SetWidgetMode(UE::Widget::EWidgetMode Mode);
	void MoveSelectedActorForUndoSmokeTest();

private:
	TSharedRef<SWidget> BuildMenuBar();
	TSharedRef<SWidget> BuildWorkspace();
	TSharedRef<SWidget> BuildViewportContent();
	TSharedRef<SWidget> BuildLeftPanel();
	TSharedRef<SWidget> BuildChatPanel();
	TSharedRef<SWidget> BuildStatusBar();
	TSharedRef<SWidget> BuildEmptyStateBanner();
	TSharedRef<SWidget> MakeMenuCombo(const FString& Label, TFunction<TSharedRef<SWidget>()> InBuildMenu);
	TSharedRef<SWidget> MakeFilterButton(const FString& Label, const FString& Value);
	TSharedRef<SWidget> MakePanelHeader(const FString& Title, TSharedRef<SWidget> Extra);
	TSharedRef<SWidget> MakeInspectorRow(const FString& Label, TSharedRef<SWidget> Value);
	TSharedRef<SWidget> MakeInspectorSection(const FString& Title);
	TSharedRef<SWidget> MakeInspectorSpin(
		double Value, double MinValue, double MaxValue, double Delta,
		TFunction<void(double)> OnCommit, const FString& ToolTip = FString());
	TSharedRef<SWidget> MakeInspectorVector(
		const FString& Label, const FVector& Value, double MinValue, double MaxValue, double Delta,
		TFunction<void(const FVector&)> OnCommit,
		FString AxisX = TEXT("X"), FString AxisY = TEXT("Y"), FString AxisZ = TEXT("Z"));
	TSharedRef<SWidget> MakeInspectorRotator(
		const FString& Label, const FRotator& Value, TFunction<void(const FRotator&)> OnCommit);
	void ApplySelectedTransform(const FEpicodeTransform& Transform);
	void ApplySelectedVisibility(bool bVisible);
	void ApplySelectedName(const FString& Name);
	void ApplySelectedDataField(const FString& Field, const TSharedPtr<FJsonValue>& Value);
	void ApplyCharacterAnimation(const FString& AnimationPath, bool bLoop, double PlayRate);
	void SetCharacterPaused(bool bPaused);
	void SetCharacterSpeed(double PlayRate);
	void SetPropPhysics(bool bSimulate);
	void SetPropMaterial(int32 Slot, const FString& MaterialPath);
	void ClearPropMaterial(int32 Slot);
	void SetLightNumber(const FString& Field, double Value);
	void SetLightColor(const FLinearColor& Color);
	void SetLightBool(const FString& Field, bool bValue);
	void SetCameraNumber(const FString& Field, double Value);

	void BuildSceneMenu(FMenuBuilder& MenuBuilder);
	void BuildOpenSceneMenu(FMenuBuilder& MenuBuilder);
	void BuildRecentSceneMenu(FMenuBuilder& MenuBuilder);
	void BuildCharacterMenu(FMenuBuilder& MenuBuilder);
	void BuildPropMenu(FMenuBuilder& MenuBuilder);
	void BuildAssetPlaceMenu(FMenuBuilder& MenuBuilder, FString Type);
	void BuildAssetDeleteMenu(FMenuBuilder& MenuBuilder, FString Type);
	void BuildLightMenu(FMenuBuilder& MenuBuilder);
	void BuildCameraMenu(FMenuBuilder& MenuBuilder);
	void BuildLibraryMenu(FMenuBuilder& MenuBuilder);
	void BuildEditMenu(FMenuBuilder& MenuBuilder);
	void BuildTransformMenu(FMenuBuilder& MenuBuilder);
	void BuildCameraSpeedMenu(FMenuBuilder& MenuBuilder);
	void BuildViewMenu(FMenuBuilder& MenuBuilder);
	void BuildChatMenu(FMenuBuilder& MenuBuilder);
	void BuildHelpMenu(FMenuBuilder& MenuBuilder);

	void RefreshUi();
	void LoadLayout();
	void SaveLayout();
	void MarkLayoutDirty();
	void MarkRecentScene(const FString& InSceneId);
	bool CreateViewport();
	void RetryViewport();
	void ApplySavedCamera();
	void BindFocusedShortcuts();
	FString GetLayoutPath() const;
	void RefreshObjectList();
	void RefreshAssetList();
	void RefreshInspector();
	void RefreshSceneList();
	void RefreshStatus();
	void SyncSelectionFromViewport();
	void SelectEntity(const FGuid& InId, bool bFocus);
	void AddChatLine(const FString& Speaker, const FString& Text);
	void NewChat();
	void ClearChat();
	FString GetSceneNameInput() const;

	void NewScene();
	void SaveScene();
	void SaveSceneAs();
	void CloseScene();
	void OpenSceneById(FString InSceneId);
	void DeleteSceneById(FString InSceneId);
	void ImportSceneFromFile();
	bool ImportSceneFile(const FString& SourcePath, FString& OutSceneId, FString& OutError);
	void ImportAssetIntoLibrary(const FString& Kind);
	bool SaveImportedAssets(const FEpicodeCommandResult& Result, FString& OutError);
	void RemoveImportedSceneEntities(const FEpicodeCommandResult& Result, bool bSceneWasClean);
	bool PlaceAsset(const FString& AssetPath, const FString& Type);
	FEpicodeTransform GetPlacementTransform() const;
	void DeleteSelectedEntity();
	void DeleteSelectedEntityOfType(EEpicodeEntityType Type);
	void CreateLight(const FString& LightType);
	void CreateCamera();
	void ActivateSelectedCamera();
	void SetTransformMode(UE::Widget::EWidgetMode Mode);
	void SetCameraSpeed(int32 SpeedSetting);
	bool IsCameraSpeed(int32 SpeedSetting) const;
	void SetViewMode(EViewModeIndex Mode);
	bool IsViewMode(EViewModeIndex Mode) const;
	void ToggleGrid();
	void FocusSelection();
	void SelectAllEntities();
	void DeselectAll();
	void Undo();
	void Redo();
	void ToggleLeftPanel();
	void ToggleRightPanel();
	bool IsLeftPanelVisible() const { return bLeftPanelVisible; }
	bool IsRightPanelVisible() const { return bRightPanelVisible; }
	FString GetTransformModeLabel() const;

	TSharedPtr<SEpicodeFocusedLevelViewport> Viewport;
	TWeakPtr<ILevelEditor> LevelEditorWeak;
	TSharedPtr<SBox> ViewportHost;
	FEpicodeFocusedShellModel Model;
	TWeakObjectPtr<AActor> SmokeActor;
	FString ViewportError;

	TSharedPtr<SEditableTextBox> ObjectSearchBox;
	TSharedPtr<SEditableTextBox> SceneNameInput;
	TSharedPtr<SVerticalBox> ObjectListBox;
	TSharedPtr<SVerticalBox> InspectorBox;
	TSharedPtr<STextBlock> InspectorTypeText;
	TSharedPtr<SVerticalBox> SceneListBox;
	TSharedPtr<SVerticalBox> AssetListBox;
	TSharedPtr<SVerticalBox> ChatHistoryBox;
	TSharedPtr<SScrollBox> ChatScroll;
	TMap<FString, TSharedPtr<class SComboButton>> MenuCombos;
	TSharedPtr<SMultiLineEditableTextBox> ChatInput;
	TSharedPtr<STextBlock> StatusSceneText;
	TSharedPtr<STextBlock> StatusSelectionText;
	TSharedPtr<STextBlock> StatusModeText;
	TSharedPtr<STextBlock> StatusLibraryText;
	TSharedPtr<STextBlock> StatusChatText;
	TSharedPtr<STextBlock> StatusSaveText;
	TSharedPtr<STextBlock> StatusRevisionText;
	TSharedPtr<STextBlock> StatusMessageText;
	TSharedPtr<STextBlock> ChatStatusText;

	FString ObjectSearch;
	FString TypeFilter = TEXT("All");
	FString LastSignature;
	FString LastAssetSignature;
	FString LastInspectorSignature;
	TArray<FString> RecentSceneIds;
	FVector SavedCameraLocation = FVector::ZeroVector;
	FRotator SavedCameraRotation = FRotator::ZeroRotator;
	float LeftPanelWidth = 0.22f;
	float RightPanelWidth = 0.22f;
	FVector InspectorSnapGrid = FVector(10.0);
	double LastRefreshTime = 0.0;
	double LastLayoutSaveTime = 0.0;
	bool bHasSavedCamera = false;
	bool bLayoutDirty = false;
	bool bSkipViewportForSmoke = false;
	bool bLeftPanelVisible = true;
	bool bRightPanelVisible = true;
	bool bInspectorWorldSpace = false;
	bool bInspectorSnap = false;
};
