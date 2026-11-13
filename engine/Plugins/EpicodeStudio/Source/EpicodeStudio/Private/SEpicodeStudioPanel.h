#pragma once

#include "CoreMinimal.h"
#include "EpicodeCommand.h"
#include "EpicodeStudioViewModel.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class SCheckBox;
class STextBlock;
class SVerticalBox;
class SWidgetSwitcher;
class SScrollBox;

class EPICODESTUDIO_API SEpicodeStudioPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEpicodeStudioPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	friend class FEpicodeStudioPanelTest;

	TSharedRef<SWidget> BuildScenePanel();
	TSharedRef<SWidget> BuildOutlinerPanel();
	TSharedRef<SWidget> BuildCharacterPanel();
	TSharedRef<SWidget> BuildPropPanel();
	TSharedRef<SWidget> BuildLightPanel();
	TSharedRef<SWidget> BuildEnvironmentPanel();
	TSharedRef<SWidget> BuildImportPanel();
	TSharedRef<SWidget> BuildRenderPanel();
	TSharedRef<SWidget> BuildApiPanel();
	TSharedRef<SWidget> BuildToolbar();
	TSharedRef<SWidget> BuildLogPanel();

	TSharedRef<SWidget> MakeInput(const FString& Label, TSharedPtr<SEditableTextBox>& OutInput, const FString& Hint, const FString& Initial = FString());
	TSharedRef<SWidget> MakeButton(const FString& Label, TFunction<void()> OnClick);
	void AddRow(const TSharedPtr<SVerticalBox>& Box, const TSharedRef<SWidget>& Row);
	void AddEmptyState(const TSharedPtr<SVerticalBox>& Box, const FString& Text);

	FEpicodeCommandResult Execute(const FString& Method, const TSharedPtr<FJsonObject>& Params);
	void Log(const FString& Text);
	void RefreshAll();
	void RefreshSceneList();
	void RefreshOutliner();
	void RefreshOutlinerInspector();
	void RefreshCharacterList();
	void RefreshPropList();
	void RefreshLightList();
	void RefreshEnvironment();
	void RefreshImportList();
	void RefreshRenderList();
	void RefreshApiPanel();
	void SpawnCharacter();
	void SpawnProp();
	void CreateLight();
	void ApplyEnvironment();

	TSharedPtr<SWidgetSwitcher> Switcher;
	TSharedPtr<SVerticalBox> SceneList;
	TSharedPtr<SVerticalBox> OutlinerList;
	TSharedPtr<SVerticalBox> OutlinerInspector;
	TSharedPtr<SVerticalBox> CharacterList;
	TSharedPtr<SVerticalBox> PropList;
	TSharedPtr<SVerticalBox> LightList;
	TSharedPtr<SVerticalBox> ImportList;
	TSharedPtr<SVerticalBox> RenderList;
	TSharedPtr<SVerticalBox> EnvironmentBox;
	TSharedPtr<STextBlock> ApiSummary;
	TSharedPtr<STextBlock> EnvironmentSummary;
	TSharedPtr<STextBlock> OutlinerSelectionText;
	TSharedPtr<STextBlock> StatusText;
	TSharedPtr<STextBlock> LogText;
	TSharedPtr<SScrollBox> LogScroll;
	TSharedPtr<SEditableTextBox> SceneNameInput;
	TSharedPtr<SEditableTextBox> SceneSaveAsInput;
	TSharedPtr<SEditableTextBox> OutlinerSearchInput;
	TSharedPtr<SEditableTextBox> OutlinerRenameInput;
	TSharedPtr<SEditableTextBox> OutlinerParentInput;
	TSharedPtr<SEditableTextBox> OutlinerLocationInput;
	TSharedPtr<SEditableTextBox> OutlinerRotationInput;
	TSharedPtr<SEditableTextBox> OutlinerScaleInput;
	TSharedPtr<SEditableTextBox> OutlinerAnimationInput;
	TSharedPtr<SEditableTextBox> OutlinerLightTypeInput;
	TSharedPtr<SEditableTextBox> OutlinerLightIntensityInput;
	TSharedPtr<SEditableTextBox> OutlinerCameraFovInput;
	TSharedPtr<SEditableTextBox> OutlinerMaterialSlotInput;
	TSharedPtr<SEditableTextBox> CharacterNameInput;
	TSharedPtr<SEditableTextBox> CharacterAnimationInput;
	TSharedPtr<SEditableTextBox> CharacterFilterInput;
	TSharedPtr<SEditableTextBox> PropNameInput;
	TSharedPtr<SEditableTextBox> PropFilterInput;
	TSharedPtr<SEditableTextBox> MaterialSlotInput;
	TSharedPtr<SEditableTextBox> LightNameInput;
	TSharedPtr<SEditableTextBox> LightTypeInput;
	TSharedPtr<SEditableTextBox> LightIntensityInput;
	TSharedPtr<SEditableTextBox> LightFilterInput;
	TSharedPtr<SEditableTextBox> EnvTimeInput;
	TSharedPtr<SEditableTextBox> EnvFogInput;
	TSharedPtr<SEditableTextBox> EnvCloudInput;
	TSharedPtr<SEditableTextBox> EnvExposureInput;
	TSharedPtr<SEditableTextBox> EnvBloomInput;
	TSharedPtr<SEditableTextBox> ImportPathInput;
	TSharedPtr<SEditableTextBox> ImportKindInput;
	TSharedPtr<SCheckBox> ImportAutoPlaceCheck;
	TSharedPtr<SCheckBox> ImportReplaceCheck;
	TArray<FString> SelectedImportFiles;
	TSet<FString> OutlinerCollapsedIds;
	FString CharacterMeshPath;
	FString PropMeshPath;
	FString PropMaterialPath;
	FString OutlinerMaterialPath;

	FEpicodeStudioViewModel ViewModel;
	FString LogBuffer;
};
