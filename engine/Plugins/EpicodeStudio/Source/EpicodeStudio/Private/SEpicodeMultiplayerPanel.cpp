#include "SEpicodeMultiplayerPanel.h"

#include "EpicodeCommand.h"
#include "EpicodeControlApiClient.h"
#include "EpicodeEngineSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	FString GetInputText(const TSharedPtr<SEditableTextBox>& Input)
	{
		return Input.IsValid() ? Input->GetText().ToString() : FString();
	}
}

void SEpicodeMultiplayerPanel::Construct(const FArguments& InArgs)
{
	ViewModel = FEpicodeMultiplayerViewModel::MakeEmpty();
	ControlApiClient = MakeShared<FEpicodeControlApiClient>();
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[BuildConnectionPanel()]
		+ SVerticalBox::Slot().AutoHeight()[BuildStatePanel()]
		+ SVerticalBox::Slot().FillHeight(0.35f)[BuildPlayerPanel()]
		+ SVerticalBox::Slot().FillHeight(0.25f)[BuildPerformerPanel()]
		+ SVerticalBox::Slot().FillHeight(0.25f)[BuildDirectorPanel()]
	];
	if (ServerInput.IsValid())
	{
		ServerInput->SetText(FText::FromString(TEXT("http://127.0.0.1:8710")));
	}
	if (TokenInput.IsValid())
	{
		TokenInput->SetIsPassword(true);
	}
	SetConnectionStatus(TEXT("Local mode"));
	Refresh();

	TWeakPtr<SEpicodeMultiplayerPanel> WeakThis = SharedThis(this);
	PollHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([WeakThis](float DeltaTime)
		{
			if (const TSharedPtr<SEpicodeMultiplayerPanel> Pinned = WeakThis.Pin())
			{
				return Pinned->HandlePollTick(DeltaTime);
			}
			return false;
		}),
		1.0f);
}

SEpicodeMultiplayerPanel::~SEpicodeMultiplayerPanel()
{
	if (PollHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PollHandle);
		PollHandle.Reset();
	}
}

TSharedRef<SWidget> SEpicodeMultiplayerPanel::MakeInput(const FString& Label, TSharedPtr<SEditableTextBox>& OutInput, const FString& Hint)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f)
		[
			SNew(STextBlock).Text(FText::FromString(Label))
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(4.0f)
		[
			SAssignNew(OutInput, SEditableTextBox).HintText(FText::FromString(Hint))
		];
}

TSharedRef<SWidget> SEpicodeMultiplayerPanel::MakeButton(const FString& Label, TFunction<void()> OnClick, TFunction<bool()> CanClick)
{
	return SNew(SButton)
		.Text(FText::FromString(Label))
		.IsEnabled_Lambda([CanClick]() { return CanClick ? CanClick() : true; })
		.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); });
}

TSharedRef<SWidget> SEpicodeMultiplayerPanel::BuildConnectionPanel()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("Session")))
		]
		+ SVerticalBox::Slot().AutoHeight()[MakeInput(TEXT("API"), ServerInput, TEXT("http://127.0.0.1:8710"))]
		+ SVerticalBox::Slot().AutoHeight()[MakeInput(TEXT("Token"), TokenInput, TEXT("Bearer token"))]
		+ SVerticalBox::Slot().AutoHeight()[MakeInput(TEXT("Scene"), SceneInput, TEXT("scene id"))]
		+ SVerticalBox::Slot().AutoHeight()[MakeInput(TEXT("Player"), PlayerNameInput, TEXT("Player"))]
		+ SVerticalBox::Slot().AutoHeight()[MakeInput(TEXT("Role"), RoleInput, TEXT("performer"))]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f)[MakeButton(TEXT("Connect"), [this]() { ConnectRemote(); })]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f)[MakeButton(TEXT("Disconnect"), [this]() { DisconnectRemote(); })]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f)[MakeButton(TEXT("Local"), [this]() { UseLocalMode(); })]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f)[MakeButton(TEXT("Create"), [this]() { CreateSession(); })]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f)[MakeButton(TEXT("Join"), [this]() { JoinSession(); })]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f)[MakeButton(TEXT("Refresh"), [this]() { Refresh(); })]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f)[MakeButton(TEXT("End"), [this]() { EndSession(); })]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f)[MakeInput(TEXT("Target"), TargetPlayerInput, TEXT("player id"))]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f)[MakeButton(TEXT("Kick"), [this]() { KickSelectedPlayer(); }, [this]() { return ViewModel.IsValid() && ViewModel->CanManage(); })]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f)[MakeButton(TEXT("Set Role"), [this]() { SetPlayerRole(); }, [this]() { return ViewModel.IsValid() && ViewModel->CanManage(); })]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f)[MakeInput(TEXT("Character"), CharacterIdInput, TEXT("entity id"))]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f)[MakeButton(TEXT("Assign"), [this]() { AssignCharacter(); }, [this]() { return ViewModel.IsValid() && ViewModel->CanAssignCharacters(); })]
		];
}

TSharedRef<SWidget> SEpicodeMultiplayerPanel::BuildStatePanel()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)[SAssignNew(ConnectionText, STextBlock).Text(FText::FromString(TEXT("Local mode")))]
		+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)[SAssignNew(StatusText, STextBlock).Text(FText::FromString(TEXT("No active session")))]
		+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)[SAssignNew(ReconnectText, STextBlock).Text(FText::FromString(TEXT("All players connected.")))];
}

TSharedRef<SWidget> SEpicodeMultiplayerPanel::BuildPlayerPanel()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)[SNew(STextBlock).Text(FText::FromString(TEXT("Players and characters")))]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()[SAssignNew(PlayerText, STextBlock).Text(FText::FromString(TEXT("No players")))]
			+ SScrollBox::Slot()[SAssignNew(CharacterText, STextBlock).Text(FText::FromString(TEXT("No characters")))]
		];
}

TSharedRef<SWidget> SEpicodeMultiplayerPanel::BuildPerformerPanel()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)[SNew(STextBlock).Text(FText::FromString(TEXT("Performer HUD")))]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(4.0f)
		[
			SAssignNew(PerformerText, STextBlock).Text(FText::FromString(TEXT("No performance state received."))).AutoWrapText(true)
		];
}

TSharedRef<SWidget> SEpicodeMultiplayerPanel::BuildDirectorPanel()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)[SNew(STextBlock).Text(FText::FromString(TEXT("Director console")))]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(4.0f)
		[
			SAssignNew(DirectorText, STextBlock).Text(FText::FromString(TEXT("No director entries"))).AutoWrapText(true)
		];
}

void SEpicodeMultiplayerPanel::SetViewModelSnapshot(const TSharedPtr<FJsonObject>& InSnapshot)
{
	ViewModel->SetLocalPlayerId(GetInputText(PlayerNameInput));
	ViewModel->SetSnapshot(InSnapshot);
	StatusText->SetText(FText::FromString(ViewModel->GetSessionStateText()));
	ReconnectText->SetText(FText::FromString(ViewModel->GetReconnectText()));
	PlayerText->SetText(FText::FromString(FString::Join(ViewModel->GetPlayerRows(), TEXT("\n"))));
	CharacterText->SetText(FText::FromString(FString::Join(ViewModel->GetCharacterRows(), TEXT("\n"))));
	PerformerText->SetText(FText::FromString(ViewModel->GetPerformerHudSummary()));
	DirectorText->SetText(FText::FromString(FString::Join(ViewModel->GetDirectorEntries(), TEXT("\n"))));
}

void SEpicodeMultiplayerPanel::SetConnectionStatus(const FString& InStatus)
{
	ConnectionStatus = InStatus;
	if (ConnectionText.IsValid())
	{
		ConnectionText->SetText(FText::FromString(InStatus));
	}
}

void SEpicodeMultiplayerPanel::Refresh()
{
	if (bRemoteMode)
	{
		PollRemote();
		return;
	}
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	if (!Engine)
	{
		SetConnectionStatus(TEXT("Local engine subsystem is unavailable."));
		return;
	}
	const FEpicodeCommandResult Result = Engine->ExecuteCommand(TEXT("session.snapshot"), MakeShared<FJsonObject>(), FEpicodeCommandContext());
	if (Result.bSuccess && Result.Result.IsValid())
	{
		SetViewModelSnapshot(Result.Result);
		return;
	}
	const FEpicodeCommandResult Status = Engine->ExecuteCommand(TEXT("session.status"), MakeShared<FJsonObject>(), FEpicodeCommandContext());
	if (Status.bSuccess && Status.Result.IsValid())
	{
		ViewModel->SetSnapshot(Status.Result);
		StatusText->SetText(FText::FromString(ViewModel->GetSessionStateText()));
	}
}

void SEpicodeMultiplayerPanel::ConnectRemote()
{
	const FString RawUrl = GetInputText(ServerInput);
	if (!FEpicodeControlApiClient::IsValidBaseUrl(RawUrl))
	{
		SetConnectionStatus(TEXT("Invalid control API address. Use http://host:port."));
		return;
	}
	if (!ControlApiClient)
	{
		ControlApiClient = MakeShared<FEpicodeControlApiClient>();
	}
	ControlApiClient->SetBaseUrl(RawUrl);
	ControlApiClient->SetToken(GetInputText(TokenInput));
	ControlApiClient->SetTimeoutSeconds(3.0);
	SetConnectionStatus(TEXT("Connecting to control API..."));

	TWeakPtr<SEpicodeMultiplayerPanel> WeakThis = SharedThis(this);
	ControlApiClient->Request(TEXT("system.health"), MakeShared<FJsonObject>(),
		[WeakThis](const FEpicodeControlApiResponse& Response)
		{
			const TSharedPtr<SEpicodeMultiplayerPanel> Pinned = WeakThis.Pin();
			if (!Pinned.IsValid())
			{
				return;
			}
			if (!Response.bSuccess)
			{
				Pinned->bRemoteMode = false;
				Pinned->bConnected = false;
				Pinned->SetConnectionStatus(EpicodeControlApiStatusText(Response));
				return;
			}
			Pinned->bRemoteMode = true;
			Pinned->bConnected = true;
			const FString Masked = Pinned->ControlApiClient.IsValid()
				? Pinned->ControlApiClient->GetMaskedToken()
				: FString();
			Pinned->SetConnectionStatus(FString::Printf(TEXT("Remote connected: %s token=%s"),
				Pinned->ControlApiClient.IsValid() ? *Pinned->ControlApiClient->GetBaseUrl() : TEXT(""),
				Masked.IsEmpty() ? TEXT("****") : *Masked));
			Pinned->Refresh();
		});
}

void SEpicodeMultiplayerPanel::DisconnectRemote()
{
	bRemoteMode = false;
	bConnected = false;
	bPollInFlight = false;
	SetConnectionStatus(TEXT("Local mode (remote disconnected)."));
	Refresh();
}

void SEpicodeMultiplayerPanel::UseLocalMode()
{
	DisconnectRemote();
	SetConnectionStatus(TEXT("Local mode"));
}

void SEpicodeMultiplayerPanel::SendRemoteCommand(const FString& Method, const TSharedPtr<FJsonObject>& Params)
{
	if (!bRemoteMode || !bConnected || !ControlApiClient.IsValid())
	{
		SetConnectionStatus(TEXT("Remote mode is not connected. Use Connect or Local."));
		return;
	}
	TWeakPtr<SEpicodeMultiplayerPanel> WeakThis = SharedThis(this);
	ControlApiClient->Request(Method, Params,
		[WeakThis, Method](const FEpicodeControlApiResponse& Response)
		{
			const TSharedPtr<SEpicodeMultiplayerPanel> Pinned = WeakThis.Pin();
			if (!Pinned.IsValid())
			{
				return;
			}
			if (!Response.bSuccess)
			{
				Pinned->SetConnectionStatus(EpicodeControlApiStatusText(Response));
				return;
			}
			Pinned->ApplyRemoteResponse(Method, Response.Result, TEXT("OK"));
		});
}

void SEpicodeMultiplayerPanel::ApplyRemoteResponse(const FString& Method,
	const TSharedPtr<FJsonObject>& Result, const FString& Status)
{
	if (Result.IsValid()
		&& (Result->HasField(TEXT("players"))
			|| Result->HasField(TEXT("characters"))
			|| Result->HasField(TEXT("state"))
			|| Result->HasField(TEXT("sessionId"))))
	{
		SetViewModelSnapshot(Result);
	}
	SetConnectionStatus(FString::Printf(TEXT("Remote %s: %s"), *Method, *Status));
}

void SEpicodeMultiplayerPanel::PollRemote()
{
	if (!bRemoteMode || !bConnected || bPollInFlight || !ControlApiClient.IsValid())
	{
		return;
	}
	bPollInFlight = true;
	TWeakPtr<SEpicodeMultiplayerPanel> WeakThis = SharedThis(this);
	ControlApiClient->Request(TEXT("session.snapshot"), MakeShared<FJsonObject>(),
		[WeakThis](const FEpicodeControlApiResponse& Response)
		{
			const TSharedPtr<SEpicodeMultiplayerPanel> Pinned = WeakThis.Pin();
			if (!Pinned.IsValid())
			{
				return;
			}
			Pinned->bPollInFlight = false;
			if (Response.bSuccess && Response.Result.IsValid())
			{
				Pinned->SetViewModelSnapshot(Response.Result);
				return;
			}
			if (Response.ErrorCode == TEXT("SESSION_NOT_FOUND"))
			{
				Pinned->SetConnectionStatus(TEXT("Remote connected; no active session."));
				return;
			}
			Pinned->SetConnectionStatus(EpicodeControlApiStatusText(Response));
		});
}

bool SEpicodeMultiplayerPanel::HandlePollTick(float DeltaTime)
{
	PollRemote();
	return true;
}

bool SEpicodeMultiplayerPanel::ConnectRemoteForTest(const FString& InBaseUrl, const FString& InToken)
{
	const bool bValid = FEpicodeControlApiClient::IsValidBaseUrl(InBaseUrl);
	if (ServerInput.IsValid())
	{
		ServerInput->SetText(FText::FromString(InBaseUrl));
	}
	if (TokenInput.IsValid())
	{
		TokenInput->SetText(FText::FromString(InToken));
	}
	if (bValid)
	{
		ConnectRemote();
	}
	else
	{
		SetConnectionStatus(TEXT("Invalid control API address. Use http://host:port."));
	}
	return bValid;
}

void SEpicodeMultiplayerPanel::UseLocalModeForTest()
{
	UseLocalMode();
}

void SEpicodeMultiplayerPanel::SetRemoteStateForTest(bool bInRemoteMode, bool bInConnected, const FString& InStatus)
{
	bRemoteMode = bInRemoteMode;
	bConnected = bInConnected;
	SetConnectionStatus(InStatus);
}

bool SEpicodeMultiplayerPanel::SendRemoteCommandForTest(const FString& Method, const TSharedPtr<FJsonObject>& Params)
{
	if (!bRemoteMode || !bConnected || !ControlApiClient.IsValid())
	{
		return false;
	}
	SendRemoteCommand(Method, Params);
	return true;
}

void SEpicodeMultiplayerPanel::SetControlApiClientForTest(const TSharedPtr<FEpicodeControlApiClient>& InClient)
{
	ControlApiClient = InClient;
}

FString SEpicodeMultiplayerPanel::GetModeTextForTest() const
{
	if (!bRemoteMode)
	{
		return TEXT("Local");
	}
	return bConnected ? TEXT("Remote") : TEXT("Remote (disconnected)");
}

FString SEpicodeMultiplayerPanel::GetServerUrlForTest() const
{
	return GetInputText(ServerInput);
}

void SEpicodeMultiplayerPanel::CreateSession()
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("sceneId"), GetInputText(SceneInput));
	Params->SetStringField(TEXT("playerId"), GetInputText(PlayerNameInput));
	Params->SetStringField(TEXT("displayName"), GetInputText(PlayerNameInput));
	if (bRemoteMode)
	{
		SendRemoteCommand(TEXT("session.create"), Params);
		return;
	}
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	if (!Engine)
	{
		SetConnectionStatus(TEXT("Local engine subsystem is unavailable."));
		return;
	}
	Engine->ExecuteCommand(TEXT("session.create"), Params, FEpicodeCommandContext());
	Refresh();
}

void SEpicodeMultiplayerPanel::JoinSession()
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("playerId"), GetInputText(PlayerNameInput));
	Params->SetStringField(TEXT("displayName"), GetInputText(PlayerNameInput));
	Params->SetStringField(TEXT("role"), TEXT("performer"));
	if (bRemoteMode)
	{
		SendRemoteCommand(TEXT("session.join"), Params);
		return;
	}
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	if (!Engine)
	{
		SetConnectionStatus(TEXT("Local engine subsystem is unavailable."));
		return;
	}
	Engine->ExecuteCommand(TEXT("session.join"), Params, FEpicodeCommandContext());
	Refresh();
}

void SEpicodeMultiplayerPanel::EndSession()
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("playerId"), GetInputText(PlayerNameInput));
	if (bRemoteMode)
	{
		SendRemoteCommand(TEXT("session.end"), Params);
		return;
	}
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	if (!Engine)
	{
		SetConnectionStatus(TEXT("Local engine subsystem is unavailable."));
		return;
	}
	Engine->ExecuteCommand(TEXT("session.end"), Params, FEpicodeCommandContext());
	Refresh();
}

void SEpicodeMultiplayerPanel::KickSelectedPlayer()
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("requesterId"), GetInputText(PlayerNameInput));
	Params->SetStringField(TEXT("targetPlayerId"), GetInputText(TargetPlayerInput));
	if (bRemoteMode)
	{
		SendRemoteCommand(TEXT("session.kick"), Params);
		return;
	}
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	if (!Engine)
	{
		SetConnectionStatus(TEXT("Local engine subsystem is unavailable."));
		return;
	}
	Engine->ExecuteCommand(TEXT("session.kick"), Params, FEpicodeCommandContext());
	Refresh();
}

void SEpicodeMultiplayerPanel::AssignCharacter()
{
	if (!ViewModel->CanAssignCharacters())
	{
		return;
	}
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("requesterId"), GetInputText(PlayerNameInput));
	Params->SetStringField(TEXT("targetPlayerId"), GetInputText(TargetPlayerInput));
	Params->SetStringField(TEXT("characterId"), GetInputText(CharacterIdInput));
	if (bRemoteMode)
	{
		SendRemoteCommand(TEXT("session.assignCharacter"), Params);
		return;
	}
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	if (!Engine)
	{
		SetConnectionStatus(TEXT("Local engine subsystem is unavailable."));
		return;
	}
	Engine->ExecuteCommand(TEXT("session.assignCharacter"), Params, FEpicodeCommandContext());
	Refresh();
}

void SEpicodeMultiplayerPanel::SetPlayerRole()
{
	if (!ViewModel->CanManage())
	{
		return;
	}
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("requesterId"), GetInputText(PlayerNameInput));
	Params->SetStringField(TEXT("targetPlayerId"), GetInputText(TargetPlayerInput));
	Params->SetStringField(TEXT("role"), GetInputText(RoleInput));
	if (bRemoteMode)
	{
		SendRemoteCommand(TEXT("session.setRole"), Params);
		return;
	}
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	if (!Engine)
	{
		SetConnectionStatus(TEXT("Local engine subsystem is unavailable."));
		return;
	}
	Engine->ExecuteCommand(TEXT("session.setRole"), Params, FEpicodeCommandContext());
	Refresh();
}
