#include "SEpicodeMultiplayerPanel.h"

#include "EpicodeCommand.h"
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
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[BuildConnectionPanel()]
		+ SVerticalBox::Slot().AutoHeight()[BuildStatePanel()]
		+ SVerticalBox::Slot().FillHeight(0.35f)[BuildPlayerPanel()]
		+ SVerticalBox::Slot().FillHeight(0.25f)[BuildPerformerPanel()]
		+ SVerticalBox::Slot().FillHeight(0.25f)[BuildDirectorPanel()]
	];
	Refresh();
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
		+ SVerticalBox::Slot().AutoHeight()[MakeInput(TEXT("Server"), ServerInput, TEXT("127.0.0.1:7777"))]
		+ SVerticalBox::Slot().AutoHeight()[MakeInput(TEXT("Scene"), SceneInput, TEXT("scene id"))]
		+ SVerticalBox::Slot().AutoHeight()[MakeInput(TEXT("Player"), PlayerNameInput, TEXT("Player"))]
		+ SVerticalBox::Slot().AutoHeight()[MakeInput(TEXT("Role"), RoleInput, TEXT("performer"))]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
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

void SEpicodeMultiplayerPanel::Refresh()
{
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	if (!Engine)
	{
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

void SEpicodeMultiplayerPanel::CreateSession()
{
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	if (!Engine)
	{
		return;
	}
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("sceneId"), GetInputText(SceneInput));
	Params->SetStringField(TEXT("playerId"), GetInputText(PlayerNameInput));
	Params->SetStringField(TEXT("displayName"), GetInputText(PlayerNameInput));
	Engine->ExecuteCommand(TEXT("session.create"), Params, FEpicodeCommandContext());
	Refresh();
}

void SEpicodeMultiplayerPanel::JoinSession()
{
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	if (!Engine)
	{
		return;
	}
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("playerId"), GetInputText(PlayerNameInput));
	Params->SetStringField(TEXT("displayName"), GetInputText(PlayerNameInput));
	Params->SetStringField(TEXT("role"), TEXT("performer"));
	Engine->ExecuteCommand(TEXT("session.join"), Params, FEpicodeCommandContext());
	Refresh();
}

void SEpicodeMultiplayerPanel::EndSession()
{
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	if (!Engine)
	{
		return;
	}
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("playerId"), GetInputText(PlayerNameInput));
	Engine->ExecuteCommand(TEXT("session.end"), Params, FEpicodeCommandContext());
	Refresh();
}

void SEpicodeMultiplayerPanel::KickSelectedPlayer()
{
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	if (!Engine)
	{
		return;
	}
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("requesterId"), GetInputText(PlayerNameInput));
	Params->SetStringField(TEXT("targetPlayerId"), GetInputText(TargetPlayerInput));
	Engine->ExecuteCommand(TEXT("session.kick"), Params, FEpicodeCommandContext());
	Refresh();
}

void SEpicodeMultiplayerPanel::AssignCharacter()
{
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	if (!Engine || !ViewModel->CanAssignCharacters())
	{
		return;
	}
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("requesterId"), GetInputText(PlayerNameInput));
	Params->SetStringField(TEXT("targetPlayerId"), GetInputText(TargetPlayerInput));
	Params->SetStringField(TEXT("characterId"), GetInputText(CharacterIdInput));
	Engine->ExecuteCommand(TEXT("session.assignCharacter"), Params, FEpicodeCommandContext());
	Refresh();
}

void SEpicodeMultiplayerPanel::SetPlayerRole()
{
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	if (!Engine || !ViewModel->CanManage())
	{
		return;
	}
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("requesterId"), GetInputText(PlayerNameInput));
	Params->SetStringField(TEXT("targetPlayerId"), GetInputText(TargetPlayerInput));
	Params->SetStringField(TEXT("role"), GetInputText(RoleInput));
	Engine->ExecuteCommand(TEXT("session.setRole"), Params, FEpicodeCommandContext());
	Refresh();
}
