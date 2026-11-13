#pragma once

#include "CoreMinimal.h"
#include "EpicodeMultiplayerViewModel.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class STextBlock;
class SVerticalBox;

class EPICODESTUDIO_API SEpicodeMultiplayerPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEpicodeMultiplayerPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	TSharedPtr<FEpicodeMultiplayerViewModel> GetViewModel() const { return ViewModel; }
	void SetViewModelSnapshot(const TSharedPtr<FJsonObject>& InSnapshot);

private:
	TSharedRef<SWidget> BuildConnectionPanel();
	TSharedRef<SWidget> BuildPlayerPanel();
	TSharedRef<SWidget> BuildPerformerPanel();
	TSharedRef<SWidget> BuildDirectorPanel();
	TSharedRef<SWidget> BuildStatePanel();
	void Refresh();
	void CreateSession();
	void JoinSession();
	void EndSession();
	void KickSelectedPlayer();
	void AssignCharacter();
	void SetPlayerRole();

	TSharedRef<SWidget> MakeInput(const FString& Label, TSharedPtr<SEditableTextBox>& OutInput, const FString& Hint);
	TSharedRef<SWidget> MakeButton(const FString& Label, TFunction<void()> OnClick, TFunction<bool()> CanClick = nullptr);

	TSharedPtr<FEpicodeMultiplayerViewModel> ViewModel;
	TSharedPtr<STextBlock> StatusText;
	TSharedPtr<STextBlock> PlayerText;
	TSharedPtr<STextBlock> CharacterText;
	TSharedPtr<STextBlock> PerformerText;
	TSharedPtr<STextBlock> DirectorText;
	TSharedPtr<STextBlock> ReconnectText;
	TSharedPtr<SEditableTextBox> ServerInput;
	TSharedPtr<SEditableTextBox> SceneInput;
	TSharedPtr<SEditableTextBox> PlayerNameInput;
	TSharedPtr<SEditableTextBox> TargetPlayerInput;
	TSharedPtr<SEditableTextBox> CharacterIdInput;
	TSharedPtr<SEditableTextBox> RoleInput;
};
