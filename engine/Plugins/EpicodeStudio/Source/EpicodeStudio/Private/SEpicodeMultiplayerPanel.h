#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "EpicodeMultiplayerViewModel.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class STextBlock;
class SVerticalBox;
class FEpicodeControlApiClient;

class EPICODESTUDIO_API SEpicodeMultiplayerPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEpicodeMultiplayerPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SEpicodeMultiplayerPanel() override;

	TSharedPtr<FEpicodeMultiplayerViewModel> GetViewModel() const { return ViewModel; }
	void SetViewModelSnapshot(const TSharedPtr<FJsonObject>& InSnapshot);

	/** Test seams for the remote control API path. */
	bool ConnectRemoteForTest(const FString& InBaseUrl, const FString& InToken);
	void UseLocalModeForTest();
	void SetRemoteStateForTest(bool bInRemoteMode, bool bInConnected, const FString& InStatus);
	bool SendRemoteCommandForTest(const FString& Method, const TSharedPtr<FJsonObject>& Params);
	void SetControlApiClientForTest(const TSharedPtr<FEpicodeControlApiClient>& InClient);
	TSharedPtr<FEpicodeControlApiClient> GetControlApiClientForTest() const { return ControlApiClient; }
	bool IsRemoteModeForTest() const { return bRemoteMode; }
	FString GetConnectionStatusForTest() const { return ConnectionStatus; }
	FString GetModeTextForTest() const;
	FString GetServerUrlForTest() const;

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
	void ConnectRemote();
	void DisconnectRemote();
	void UseLocalMode();
	void SendRemoteCommand(const FString& Method, const TSharedPtr<FJsonObject>& Params);
	void PollRemote();
	void ApplyRemoteResponse(const FString& Method, const TSharedPtr<FJsonObject>& Result, const FString& Status);
	void SetConnectionStatus(const FString& InStatus);
	bool HandlePollTick(float DeltaTime);

	TSharedRef<SWidget> MakeInput(const FString& Label, TSharedPtr<SEditableTextBox>& OutInput, const FString& Hint);
	TSharedRef<SWidget> MakeButton(const FString& Label, TFunction<void()> OnClick, TFunction<bool()> CanClick = nullptr);

	TSharedPtr<FEpicodeMultiplayerViewModel> ViewModel;
	TSharedPtr<STextBlock> StatusText;
	TSharedPtr<STextBlock> PlayerText;
	TSharedPtr<STextBlock> CharacterText;
	TSharedPtr<STextBlock> PerformerText;
	TSharedPtr<STextBlock> DirectorText;
	TSharedPtr<STextBlock> ReconnectText;
	TSharedPtr<STextBlock> ConnectionText;
	TSharedPtr<SEditableTextBox> ServerInput;
	TSharedPtr<SEditableTextBox> TokenInput;
	TSharedPtr<SEditableTextBox> SceneInput;
	TSharedPtr<SEditableTextBox> PlayerNameInput;
	TSharedPtr<SEditableTextBox> TargetPlayerInput;
	TSharedPtr<SEditableTextBox> CharacterIdInput;
	TSharedPtr<SEditableTextBox> RoleInput;
	TSharedPtr<FEpicodeControlApiClient> ControlApiClient;
	FTSTicker::FDelegateHandle PollHandle;
	FString ConnectionStatus;
	bool bRemoteMode = false;
	bool bConnected = false;
	bool bPollInFlight = false;
};
