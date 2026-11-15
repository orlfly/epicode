#include "SEpicodeFocusedShell.h"

#include "AssetEditorViewportLayout.h"
#include "Editor.h"
#include "Editor/UnrealEdTypes.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Framework/Application/SlateApplication.h"
#include "ILevelEditor.h"
#include "SEpicodeFocusedLevelViewport.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SEpicodeFocusedShell"

void SEpicodeFocusedShell::Construct(const FArguments& InArgs)
{
	const TSharedPtr<ILevelEditor> LevelEditor = InArgs._LevelEditor;
	if (!LevelEditor.IsValid())
	{
		ViewportError = TEXT("LevelEditor instance is not available");
	}
	else
	{
		TSharedRef<SEpicodeFocusedLevelViewport> NewViewport =
			MakeShared<SEpicodeFocusedLevelViewport>();

		FAssetEditorViewportConstructionArgs ViewportArgs;
		ViewportArgs.ViewportType = LVT_Perspective;
		ViewportArgs.bRealtime = true;
		ViewportArgs.ConfigKey = NAME_None;

		NewViewport->Construct(
			SLevelViewport::FArguments().ParentLevelEditor(LevelEditor),
			ViewportArgs);

		LevelEditor->AddStandaloneLevelViewport(NewViewport);
		Viewport = NewViewport;
	}

	TSharedRef<SWidget> ViewportContent = Viewport.IsValid()
		? StaticCastSharedRef<SWidget>(Viewport.ToSharedRef())
		: StaticCastSharedRef<SWidget>(
			SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
				.Padding(FMargin(24.0f))
				[
					SNew(STextBlock)
						.Text(FText::FromString(
							FString::Printf(
								TEXT("Focused viewport could not be created.\n%s\n\nRun with --legacy for the full editor."),
								*ViewportError)))
						.AutoWrapText(true)
				]);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildMenuBar()
			]
		+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				ViewportContent
			]
		+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildStatusBar()
			]
	];
}

void SEpicodeFocusedShell::FocusViewport()
{
	if (Viewport.IsValid())
	{
		GCurrentLevelEditingViewportClient = &Viewport->GetLevelViewportClient();
		Viewport->SetKeyboardFocusToThisViewport();
	}
}

void SEpicodeFocusedShell::SelectFirstActorForSmokeTest()
{
	if (!Viewport.IsValid())
	{
		return;
	}

	FLevelEditorViewportClient& Client = Viewport->GetLevelViewportClient();
	UWorld* World = Client.GetWorld();
	if (!World)
	{
		return;
	}

	AActor* SelectedActor = nullptr;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		if (*It)
		{
			SelectedActor = *It;
			break;
		}
	}

	if (!SelectedActor)
	{
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			if (*It)
			{
				SelectedActor = *It;
				break;
			}
		}
	}

	if (!SelectedActor)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor && !Actor->IsA<AWorldSettings>())
			{
				SelectedActor = Actor;
				break;
			}
		}
	}

	if (!SelectedActor)
	{
		return;
	}

	GEditor->SelectNone(false, true);
	GEditor->SelectActor(SelectedActor, true, true);
	Client.ReceivedFocus(Client.Viewport);
	Client.SetWidgetMode(UE::Widget::WM_Translate);
	GCurrentLevelEditingViewportClient = &Client;
	Viewport->OnFocusViewportToSelection();
	Viewport->Invalidate();
	SmokeActor = SelectedActor;
	UE_LOG(LogTemp, Display,
		TEXT("FocusedEditor smoke selection: %s location=%s widgetMode=%d widgetLocation=%s showWidget=%s"),
		*SelectedActor->GetName(),
		*SelectedActor->GetActorLocation().ToString(),
		static_cast<int32>(Client.GetWidgetMode()),
		*Client.GetWidgetLocation().ToString(),
		Client.GetShowWidget() ? TEXT("true") : TEXT("false"));
}

void SEpicodeFocusedShell::SetWidgetMode(UE::Widget::EWidgetMode Mode)
{
	if (!Viewport.IsValid())
	{
		return;
	}

	FLevelEditorViewportClient& Client = Viewport->GetLevelViewportClient();
	Client.SetWidgetMode(Mode);
	GCurrentLevelEditingViewportClient = &Client;
	Viewport->Invalidate();
	UE_LOG(LogTemp, Display,
		TEXT("FocusedEditor smoke widget mode: %d"), static_cast<int32>(Client.GetWidgetMode()));
}

void SEpicodeFocusedShell::MoveSelectedActorForUndoSmokeTest()
{
	AActor* Actor = SmokeActor.Get();
	if (!Actor)
	{
		UE_LOG(LogTemp, Warning, TEXT("FocusedEditor undo smoke skipped: no selected actor"));
		return;
	}

	const FVector Before = Actor->GetActorLocation();
	GEditor->BeginTransaction(FText::FromString(TEXT("Epicode Focused Editor Undo Smoke")));
	Actor->Modify();
	Actor->SetActorLocation(Before + FVector(100.0, 0.0, 0.0));
	GEditor->EndTransaction();
	const FVector Moved = Actor->GetActorLocation();

	GEditor->UndoTransaction();
	const FVector Restored = Actor->GetActorLocation();
	const bool bRestored = Restored.Equals(Before, 0.01);
	UE_LOG(LogTemp, Display,
		TEXT("FocusedEditor undo smoke: actor=%s before=%s moved=%s restored=%s pass=%s"),
		*Actor->GetName(),
		*Before.ToString(),
		*Moved.ToString(),
		*Restored.ToString(),
		bRestored ? TEXT("true") : TEXT("false"));
}

TSharedRef<SWidget> SEpicodeFocusedShell::MakeMenuButton(const FString& Label)
{
	return SNew(SButton)
		.ButtonStyle(&FAppStyle::Get(), "Menu.Button")
		.ContentPadding(FMargin(8.0f, 3.0f))
		.OnClicked(FOnClicked::CreateLambda([]()
		{
			return FReply::Handled();
		}))
		[
			SNew(STextBlock)
				.Text(FText::FromString(Label))
				.Font(FAppStyle::Get().GetFontStyle("NormalFont"))
		];
}

TSharedRef<SWidget> SEpicodeFocusedShell::BuildMenuBar()
{
	TSharedRef<SHorizontalBox> MenuRow = SNew(SHorizontalBox);

	MenuRow->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(0.0f, 0.0f, 14.0f, 0.0f))
		[
			SNew(STextBlock)
				.Text(FText::FromString(TEXT("EPICODE")))
				.Font(FAppStyle::Get().GetFontStyle("HeadingExtraSmall"))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.92f, 0.92f, 1.0f)))
		];

	const TCHAR* MenuLabels[] = {
		TEXT("场景"), TEXT("角色"), TEXT("物品"), TEXT("灯光"), TEXT("摄像"),
		TEXT("本地库"), TEXT("编辑"), TEXT("视图"), TEXT("对话"), TEXT("帮助")
	};
	for (const TCHAR* Label : MenuLabels)
	{
		MenuRow->AddSlot()
			.AutoWidth()
			.Padding(FMargin(1.0f, 0.0f))
			[
				MakeMenuButton(Label)
			];
	}

	MenuRow->AddSlot()
		.FillWidth(1.0f)
		[
			SNew(SSpacer)
		];
	MenuRow->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))
		[
			SNew(STextBlock)
				.Text(LOCTEXT("FocusedModeLabel", "聚焦模式 · 视口 PoC"))
				.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.62f, 0.62f, 0.62f, 1.0f)))
		];

	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
		.Padding(FMargin(8.0f, 4.0f))
		[
			MenuRow
		];
}

TSharedRef<SWidget> SEpicodeFocusedShell::BuildStatusBar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
		.Padding(FMargin(8.0f, 3.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
						.Text(Viewport.IsValid()
							? LOCTEXT("ViewportReady", "视口就绪")
							: LOCTEXT("ViewportFailed", "视口初始化失败"))
						.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
						.ColorAndOpacity(FSlateColor(
							Viewport.IsValid()
								? FLinearColor(0.55f, 0.85f, 0.55f, 1.0f)
								: FLinearColor(0.95f, 0.45f, 0.45f, 1.0f)))
				]
			+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SSpacer)
				]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
						.Text(LOCTEXT("ShellStatus", "EpicodeEditorShell / -EpicodeFocusedEditor"))
						.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.58f, 0.58f, 0.58f, 1.0f)))
				]
		];
}

#undef LOCTEXT_NAMESPACE
