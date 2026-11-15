#include "SEpicodeFocusedShell.h"

#include "AssetEditorViewportLayout.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Editor/UnrealEdTypes.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "EpicodeEntityActors.h"
#include "EpicodeSceneSubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "ILevelEditor.h"
#include "LevelEditorActions.h"
#include "LevelViewportActions.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "SEpicodeFocusedLevelViewport.h"
#include "Selection.h"
#include "EditorViewportCommands.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Settings/EditorViewportSettings.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SEpicodeFocusedShell"

namespace
{
	FText MenuText(const TCHAR* Text)
	{
		return FText::FromString(Text);
	}

	FSlateColor PanelTextColor()
	{
		return FSlateColor(FLinearColor(0.82f, 0.82f, 0.82f, 1.0f));
	}

	FSlateColor MutedTextColor()
	{
		return FSlateColor(FLinearColor(0.58f, 0.58f, 0.58f, 1.0f));
	}

	FSlateColor AccentTextColor()
	{
		return FSlateColor(FLinearColor(0.56f, 0.78f, 0.98f, 1.0f));
	}
}

void SEpicodeFocusedShell::Construct(const FArguments& InArgs)
{
	LoadLayout();
	bSkipViewportForSmoke = FParse::Param(
		FCommandLine::Get(), TEXT("EpicodeFocusedEditorViewportRetrySmoke"));

	const TSharedPtr<ILevelEditor> LevelEditor = InArgs._LevelEditor;
	LevelEditorWeak = LevelEditor;
	if (!LevelEditor.IsValid())
	{
		ViewportError = TEXT("LevelEditor instance is not available");
	}
	else if (bSkipViewportForSmoke)
	{
		ViewportError = TEXT("smoke: viewport creation intentionally skipped");
	}
	else if (CreateViewport())
	{
		ApplySavedCamera();
		BindFocusedShortcuts();
	}

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
				SNew(SOverlay)
				+ SOverlay::Slot()
					[
						BuildWorkspace()
					]
				+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Padding(FMargin(0.0f, 0.0f, 0.0f, 56.0f))
					[
						SNew(SBox)
							.MaxDesiredWidth(420.0f)
							[
								BuildEmptyStateBanner()
							]
					]
			]
		+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildStatusBar()
			]
	];

	Model.Refresh();
	LastSignature = Model.GetSignature();
	RefreshObjectList();
	RefreshSceneList();
	RefreshStatus();
	AddChatLine(TEXT("系统"), TEXT("对话后端尚未配置；场景命令、对象列表和视口可用。"));
}

void SEpicodeFocusedShell::Tick(
	const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (InCurrentTime - LastRefreshTime < 0.25)
	{
		return;
	}
	LastRefreshTime = InCurrentTime;
	RefreshUi();
	if (Viewport.IsValid())
	{
		FLevelEditorViewportClient& Client = Viewport->GetLevelViewportClient();
		const FVector Location = Client.GetViewLocation();
		const FRotator Rotation = Client.GetViewRotation();
		if (!Location.Equals(SavedCameraLocation) || !Rotation.Equals(SavedCameraRotation))
		{
			SavedCameraLocation = Location;
			SavedCameraRotation = Rotation;
			bHasSavedCamera = true;
			MarkLayoutDirty();
		}
	}
	if (bLayoutDirty && InCurrentTime - LastLayoutSaveTime >= 0.5)
	{
		SaveLayout();
	}
}

SEpicodeFocusedShell::~SEpicodeFocusedShell()
{
	SaveLayout();
}

void SEpicodeFocusedShell::MarkLayoutDirty()
{
	bLayoutDirty = true;
}

FString SEpicodeFocusedShell::GetLayoutPath() const
{
	return FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("Epicode/Editor/layout.json"));
}

void SEpicodeFocusedShell::LoadLayout()
{
	const FString Path = GetLayoutPath();
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		return;
	}

	TSharedPtr<FJsonObject> Object;
	const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FocusedEditor layout ignored: invalid JSON in %s"), *Path);
		return;
	}

	if (Object->HasField(TEXT("leftVisible")))
	{
		bLeftPanelVisible = Object->GetBoolField(TEXT("leftVisible"));
	}
	if (Object->HasField(TEXT("rightVisible")))
	{
		bRightPanelVisible = Object->GetBoolField(TEXT("rightVisible"));
	}
	if (Object->HasField(TEXT("leftWidth")))
	{
		LeftPanelWidth = FMath::Clamp(
			static_cast<float>(Object->GetNumberField(TEXT("leftWidth"))), 0.12f, 0.45f);
	}
	if (Object->HasField(TEXT("rightWidth")))
	{
		RightPanelWidth = FMath::Clamp(
			static_cast<float>(Object->GetNumberField(TEXT("rightWidth"))), 0.12f, 0.45f);
	}

	RecentSceneIds.Reset();
	if (Object->HasField(TEXT("recentScenes")))
	{
		for (const TSharedPtr<FJsonValue>& Value : Object->GetArrayField(TEXT("recentScenes")))
		{
			if (!Value.IsValid())
			{
				continue;
			}
			FString SceneId;
			if (Value->TryGetString(SceneId) && !SceneId.IsEmpty())
			{
				RecentSceneIds.Add(SceneId);
			}
		}
		if (RecentSceneIds.Num() > 10)
		{
			RecentSceneIds.SetNum(10);
		}
	}

	if (Object->HasField(TEXT("cameraLocation")) && Object->HasField(TEXT("cameraRotation")))
	{
		const TArray<TSharedPtr<FJsonValue>>& LocationValues =
			Object->GetArrayField(TEXT("cameraLocation"));
		const TArray<TSharedPtr<FJsonValue>>& RotationValues =
			Object->GetArrayField(TEXT("cameraRotation"));
		if (LocationValues.Num() == 3 && RotationValues.Num() == 3)
		{
			double X = 0.0, Y = 0.0, Z = 0.0;
			double Pitch = 0.0, Yaw = 0.0, Roll = 0.0;
			if (LocationValues[0]->TryGetNumber(X)
				&& LocationValues[1]->TryGetNumber(Y)
				&& LocationValues[2]->TryGetNumber(Z)
				&& RotationValues[0]->TryGetNumber(Pitch)
				&& RotationValues[1]->TryGetNumber(Yaw)
				&& RotationValues[2]->TryGetNumber(Roll))
			{
				SavedCameraLocation = FVector(X, Y, Z);
				SavedCameraRotation = FRotator(Pitch, Yaw, Roll);
				bHasSavedCamera = true;
			}
		}
	}

	UE_LOG(LogTemp, Display,
		TEXT("FocusedEditor layout loaded: left=%s right=%s leftWidth=%.3f rightWidth=%.3f recent=%d camera=%s"),
		bLeftPanelVisible ? TEXT("true") : TEXT("false"),
		bRightPanelVisible ? TEXT("true") : TEXT("false"),
		LeftPanelWidth,
		RightPanelWidth,
		RecentSceneIds.Num(),
		bHasSavedCamera ? TEXT("true") : TEXT("false"));
}

void SEpicodeFocusedShell::SaveLayout()
{
	TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetBoolField(TEXT("leftVisible"), bLeftPanelVisible);
	Object->SetBoolField(TEXT("rightVisible"), bRightPanelVisible);
	Object->SetNumberField(TEXT("leftWidth"), LeftPanelWidth);
	Object->SetNumberField(TEXT("rightWidth"), RightPanelWidth);

	TArray<TSharedPtr<FJsonValue>> RecentValues;
	for (const FString& SceneId : RecentSceneIds)
	{
		RecentValues.Add(MakeShared<FJsonValueString>(SceneId));
	}
	Object->SetArrayField(TEXT("recentScenes"), RecentValues);

	if (Viewport.IsValid())
	{
		FLevelEditorViewportClient& Client = Viewport->GetLevelViewportClient();
		const FVector Location = Client.GetViewLocation();
		const FRotator Rotation = Client.GetViewRotation();

		TArray<TSharedPtr<FJsonValue>> LocationValues;
		LocationValues.Add(MakeShared<FJsonValueNumber>(Location.X));
		LocationValues.Add(MakeShared<FJsonValueNumber>(Location.Y));
		LocationValues.Add(MakeShared<FJsonValueNumber>(Location.Z));
		Object->SetArrayField(TEXT("cameraLocation"), LocationValues);

		TArray<TSharedPtr<FJsonValue>> RotationValues;
		RotationValues.Add(MakeShared<FJsonValueNumber>(Rotation.Pitch));
		RotationValues.Add(MakeShared<FJsonValueNumber>(Rotation.Yaw));
		RotationValues.Add(MakeShared<FJsonValueNumber>(Rotation.Roll));
		Object->SetArrayField(TEXT("cameraRotation"), RotationValues);
	}

	const FString Path = GetLayoutPath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);

	FString Json;
	const TSharedRef<TJsonWriter<TCHAR>> Writer = TJsonWriterFactory<TCHAR>::Create(&Json);
	if (FJsonSerializer::Serialize(Object.ToSharedRef(), Writer))
	{
		if (FFileHelper::SaveStringToFile(Json, *Path))
		{
			bLayoutDirty = false;
			LastLayoutSaveTime = FPlatformTime::Seconds();
			UE_LOG(LogTemp, Display, TEXT("FocusedEditor layout saved: %s"), *Path);
		}
	}
}

void SEpicodeFocusedShell::MarkRecentScene(const FString& InSceneId)
{
	if (InSceneId.IsEmpty())
	{
		return;
	}
	RecentSceneIds.Remove(InSceneId);
	RecentSceneIds.Insert(InSceneId, 0);
	if (RecentSceneIds.Num() > 10)
	{
		RecentSceneIds.SetNum(10);
	}
	MarkLayoutDirty();
}

void SEpicodeFocusedShell::ApplySavedCamera()
{
	if (!bHasSavedCamera || !Viewport.IsValid())
	{
		return;
	}
	FLevelEditorViewportClient& Client = Viewport->GetLevelViewportClient();
	Client.SetViewLocation(SavedCameraLocation);
	Client.SetViewRotation(SavedCameraRotation);
	Client.Invalidate();
}

void SEpicodeFocusedShell::BindFocusedShortcuts()
{
	if (!Viewport.IsValid())
	{
		return;
	}

	const TSharedPtr<FUICommandList> Commands = Viewport->GetCommandList();
	if (!Commands.IsValid())
	{
		return;
	}

	auto MapCommand = [&Commands](
		const TSharedPtr<FUICommandInfo>& Command,
		FExecuteAction Execute,
		FCanExecuteAction CanExecute)
	{
		if (Command.IsValid())
		{
			Commands->MapAction(Command, MoveTemp(Execute), MoveTemp(CanExecute));
		}
	};

	MapCommand(
		FEditorViewportCommands::Get().SelectMode,
		FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::SetTransformMode, UE::Widget::WM_None),
		FCanExecuteAction());
	MapCommand(
		FEditorViewportCommands::Get().TranslateMode,
		FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::SetTransformMode, UE::Widget::WM_Translate),
		FCanExecuteAction());
	MapCommand(
		FEditorViewportCommands::Get().RotateMode,
		FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::SetTransformMode, UE::Widget::WM_Rotate),
		FCanExecuteAction());
	MapCommand(
		FEditorViewportCommands::Get().ScaleMode,
		FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::SetTransformMode, UE::Widget::WM_Scale),
		FCanExecuteAction());

	MapCommand(
		FEditorViewportCommands::Get().FocusViewportToSelection,
		FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::FocusSelection),
		FCanExecuteAction());

	MapCommand(
		FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::DeleteSelectedEntity),
		FCanExecuteAction::CreateLambda([this]()
		{
			return Model.GetSelectedEntityId().IsValid();
		}));
	MapCommand(
		FGenericCommands::Get().Undo,
		FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::Undo),
		FCanExecuteAction());
	MapCommand(
		FGenericCommands::Get().Redo,
		FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::Redo),
		FCanExecuteAction());
	MapCommand(
		FLevelEditorCommands::Get().Save,
		FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::SaveScene),
		FCanExecuteAction::CreateLambda([this]()
		{
			return Model.HasScene();
		}));

	UE_LOG(LogTemp, Display,
		TEXT("FocusedEditor shortcuts bound: Q/W/E/R/F/Delete/Ctrl+Z/Ctrl+Shift+Z/Ctrl+S"));
}

void SEpicodeFocusedShell::FocusViewport()
{
	if (Viewport.IsValid())
	{
		GCurrentLevelEditingViewportClient = &Viewport->GetLevelViewportClient();
		Viewport->SetKeyboardFocusToThisViewport();
	}
}

void SEpicodeFocusedShell::CreateWorkbenchSceneForSmokeTest()
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("name"), TEXT("Focused Workbench Smoke"));
	const FEpicodeCommandResult Result = Model.Execute(TEXT("scene.new"), Params);
	if (Result.bSuccess)
	{
		MarkRecentScene(Model.GetSceneId());
	}
	RefreshUi();
}

void SEpicodeFocusedShell::SpawnWorkbenchEntitiesForSmokeTest()
{
	CreateLight(TEXT("Point"));
	CreateLight(TEXT("Directional"));
	CreateCamera();
	RefreshUi();
}

void SEpicodeFocusedShell::SelectFirstSceneEntityForSmokeTest()
{
	if (Model.GetEntities().IsEmpty())
	{
		return;
	}
	SelectEntity(Model.GetEntities()[0].Id, true);
}

void SEpicodeFocusedShell::SetPanelsForSmokeTest(bool bLeftVisible, bool bRightVisible)
{
	bLeftPanelVisible = bLeftVisible;
	bRightPanelVisible = bRightVisible;
	Invalidate(EInvalidateWidgetReason::Layout);
	RefreshStatus();
}

void SEpicodeFocusedShell::OpenMenuForSmokeTest(const FString& Label)
{
	if (const TSharedPtr<SComboButton>* Combo = MenuCombos.Find(Label))
	{
		if (Combo->IsValid())
		{
			(*Combo)->SetIsOpen(true);
			UE_LOG(LogTemp, Display,
				TEXT("FocusedEditor menu smoke: label=%s open=%s"),
				*Label,
				(*Combo)->IsOpen() ? TEXT("true") : TEXT("false"));
			return;
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("FocusedEditor menu smoke: menu not found: %s"), *Label);
}

void SEpicodeFocusedShell::CloseMenuForSmokeTest(const FString& Label)
{
	if (const TSharedPtr<SComboButton>* Combo = MenuCombos.Find(Label))
	{
		if (Combo->IsValid())
		{
			(*Combo)->SetIsOpen(false);
		}
	}
}

bool SEpicodeFocusedShell::ProcessShortcutForSmokeTest(
	const FKey& Key, bool bCtrl, bool bShift)
{
	if (!Viewport.IsValid() || !Viewport->GetCommandList().IsValid())
	{
		return false;
	}

	const FModifierKeysState Modifiers(
		bShift, bShift,
		bCtrl, bCtrl,
		false, false,
		false, false,
		false);
	return Viewport->GetCommandList()->ProcessCommandBindings(Key, Modifiers, false);
}

bool SEpicodeFocusedShell::RetryViewportForSmokeTest()
{
	RetryViewport();
	return Viewport.IsValid();
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

	const FGuid EntityId = EpicodeActorFactory::GetEntityId(SelectedActor);
	if (EntityId.IsValid())
	{
		Model.SetSelectedEntityId(EntityId);
		RefreshObjectList();
		RefreshStatus();
	}

	UE_LOG(LogTemp, Display,
		TEXT("FocusedEditor smoke selection: %s entity=%s location=%s widgetMode=%d widgetLocation=%s showWidget=%s"),
		*SelectedActor->GetName(),
		*EntityId.ToString(EGuidFormats::DigitsWithHyphens),
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
		TEXT("FocusedEditor widget mode: %d"), static_cast<int32>(Client.GetWidgetMode()));
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

TSharedRef<SWidget> SEpicodeFocusedShell::MakeMenuCombo(
	const FString& Label, TFunction<TSharedRef<SWidget>()> InBuildMenu)
{
	TSharedRef<SComboButton> Combo = SNew(SComboButton)
		.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
		.ContentPadding(FMargin(8.0f, 3.0f))
		.ToolTipText(FText::FromString(Label))
		.OnGetMenuContent(FOnGetContent::CreateLambda(
			[BuildMenu = MoveTemp(InBuildMenu)]()
			{
				return BuildMenu();
			}))
		.ButtonContent()
		[
			SNew(STextBlock)
				.Text(FText::FromString(Label))
				.Font(FAppStyle::Get().GetFontStyle("NormalFont"))
				.ColorAndOpacity(PanelTextColor())
		];
	MenuCombos.Add(Label, Combo);
	return Combo;
}

TSharedRef<SWidget> SEpicodeFocusedShell::BuildMenuBar()
{
	TSharedRef<SHorizontalBox> MenuRow = SNew(SHorizontalBox);

	MenuRow->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(2.0f, 0.0f, 14.0f, 0.0f))
		[
			SNew(STextBlock)
				.Text(FText::FromString(TEXT("EPICODE")))
				.Font(FAppStyle::Get().GetFontStyle("HeadingExtraSmall"))
				.ColorAndOpacity(AccentTextColor())
		];

	auto AddMenu = [this, &MenuRow](
		const FString& Label, TFunction<void(FMenuBuilder&)> BuildMenu)
	{
		MenuRow->AddSlot()
			.AutoWidth()
			.Padding(FMargin(1.0f, 0.0f))
			[
				MakeMenuCombo(Label, [BuildMenu = MoveTemp(BuildMenu)]()
				{
					FMenuBuilder Builder(true, TSharedPtr<FUICommandList>());
					BuildMenu(Builder);
					return Builder.MakeWidget();
				})
			];
	};

	AddMenu(TEXT("场景"), [this](FMenuBuilder& Builder) { BuildSceneMenu(Builder); });
	AddMenu(TEXT("角色"), [this](FMenuBuilder& Builder) { BuildCharacterMenu(Builder); });
	AddMenu(TEXT("物品"), [this](FMenuBuilder& Builder) { BuildPropMenu(Builder); });
	AddMenu(TEXT("灯光"), [this](FMenuBuilder& Builder) { BuildLightMenu(Builder); });
	AddMenu(TEXT("摄像"), [this](FMenuBuilder& Builder) { BuildCameraMenu(Builder); });
	AddMenu(TEXT("本地库"), [this](FMenuBuilder& Builder) { BuildLibraryMenu(Builder); });
	AddMenu(TEXT("编辑"), [this](FMenuBuilder& Builder) { BuildEditMenu(Builder); });
	AddMenu(TEXT("视图"), [this](FMenuBuilder& Builder) { BuildViewMenu(Builder); });
	AddMenu(TEXT("对话"), [this](FMenuBuilder& Builder) { BuildChatMenu(Builder); });
	AddMenu(TEXT("帮助"), [this](FMenuBuilder& Builder) { BuildHelpMenu(Builder); });

	MenuRow->AddSlot()
		.FillWidth(1.0f)
		[
			SNew(SSpacer)
		];
	MenuRow->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(8.0f, 0.0f, 2.0f, 0.0f))
		[
			SNew(STextBlock)
				.Text(LOCTEXT("FocusedModeLabel", "聚焦工作台 · 2.1"))
				.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
				.ColorAndOpacity(MutedTextColor())
		];

	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
		.Padding(FMargin(8.0f, 4.0f))
		[
			MenuRow
		];
}

void SEpicodeFocusedShell::BuildSceneMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("新建场景")), MenuText(TEXT("创建空白 Epicode 场景")), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::NewScene)));
	MenuBuilder.AddSubMenu(
		MenuText(TEXT("打开场景")), MenuText(TEXT("从本地库打开")),
		FNewMenuDelegate::CreateSP(this, &SEpicodeFocusedShell::BuildOpenSceneMenu));
	MenuBuilder.AddSubMenu(
		MenuText(TEXT("最近场景")), MenuText(TEXT("本地库中最近修改的场景")),
		FNewMenuDelegate::CreateSP(this, &SEpicodeFocusedShell::BuildRecentSceneMenu));
	MenuBuilder.AddMenuSeparator();
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("保存")), MenuText(TEXT("保存当前场景")), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::SaveScene)));
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("另存为")), MenuText(TEXT("保存为本地库中的新场景")), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::SaveSceneAs)));
	MenuBuilder.AddMenuSeparator();
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("关闭场景")), MenuText(TEXT("关闭当前场景，未保存时返回错误")), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::CloseScene)));
}

void SEpicodeFocusedShell::BuildRecentSceneMenu(FMenuBuilder& MenuBuilder)
{
	Model.Refresh();
	const TArray<FEpicodeFocusedSceneRow>& Scenes = Model.GetScenes();
	if (Scenes.IsEmpty())
	{
		MenuBuilder.AddMenuEntry(
			MenuText(TEXT("暂无最近场景")), FText::GetEmpty(), FSlateIcon(), FUIAction());
		return;
	}

	TArray<const FEpicodeFocusedSceneRow*> Ordered;
	for (const FString& SceneId : RecentSceneIds)
	{
		for (const FEpicodeFocusedSceneRow& Row : Scenes)
		{
			if (Row.Id == SceneId && !Ordered.Contains(&Row))
			{
				Ordered.Add(&Row);
				break;
			}
		}
	}
	for (const FEpicodeFocusedSceneRow& Row : Scenes)
	{
		if (!Ordered.Contains(&Row))
		{
			Ordered.Add(&Row);
		}
	}

	const int32 Count = FMath::Min(Ordered.Num(), 5);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FEpicodeFocusedSceneRow& Row = *Ordered[Index];
		FUIAction Action;
		if (Row.bAvailable)
		{
			Action = FUIAction(FExecuteAction::CreateSP(
				this, &SEpicodeFocusedShell::OpenSceneById, Row.Id));
		}
		MenuBuilder.AddMenuEntry(
			FText::FromString(Row.bCurrent
				? FString::Printf(TEXT("%s（当前）"), *Row.Name)
				: Row.Name),
			FText::FromString(Row.Id), FSlateIcon(), Action);
	}
	MenuBuilder.AddMenuSeparator();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FEpicodeFocusedSceneRow& Row = *Ordered[Index];
		if (Row.bCurrent)
		{
			continue;
		}
		MenuBuilder.AddMenuEntry(
			FText::FromString(FString::Printf(TEXT("删除：%s"), *Row.Name)),
			MenuText(TEXT("移到回收站，不能删除当前场景")), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, Id = Row.Id]()
			{
				DeleteSceneById(Id);
			})));
	}
}

void SEpicodeFocusedShell::BuildOpenSceneMenu(FMenuBuilder& MenuBuilder)
{
	Model.Refresh();
	if (Model.GetScenes().IsEmpty())
	{
		MenuBuilder.AddMenuEntry(
			MenuText(TEXT("本地库为空")), FText::GetEmpty(), FSlateIcon(), FUIAction());
		return;
	}

	for (const FEpicodeFocusedSceneRow& Row : Model.GetScenes())
	{
		const FString Label = Row.bCurrent
			? FString::Printf(TEXT("%s（当前）"), *Row.Name)
			: Row.Name;
		const FString Tooltip = Row.bAvailable
			? Row.Id
			: FString::Printf(TEXT("%s · 文件不可用"), *Row.Id);
		FUIAction Action;
		if (Row.bAvailable)
		{
			Action = FUIAction(FExecuteAction::CreateSP(
				this, &SEpicodeFocusedShell::OpenSceneById, Row.Id));
		}
		MenuBuilder.AddMenuEntry(
			FText::FromString(Label), FText::FromString(Tooltip), FSlateIcon(), Action);
	}
}

void SEpicodeFocusedShell::BuildCharacterMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("聚焦选中角色")), FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::FocusSelection)));
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("删除选中角色")), MenuText(TEXT("只能删除当前选中的角色实体")), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]()
		{
			DeleteSelectedEntityOfType(EEpicodeEntityType::Character);
		})));
	MenuBuilder.AddMenuSeparator();
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("放置角色")), MenuText(TEXT("角色资产导入将在任务 4 接入")), FSlateIcon(),
		FUIAction());
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("角色动画")), MenuText(TEXT("动画工作流将在任务 4 接入")), FSlateIcon(),
		FUIAction());
}

void SEpicodeFocusedShell::BuildPropMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("聚焦选中物品")), FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::FocusSelection)));
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("删除选中物品")), MenuText(TEXT("只能删除当前选中的物品实体")), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]()
		{
			DeleteSelectedEntityOfType(EEpicodeEntityType::Prop);
		})));
	MenuBuilder.AddMenuSeparator();
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("放置物品")), MenuText(TEXT("物品资产导入将在任务 4 接入")), FSlateIcon(),
		FUIAction());
}

void SEpicodeFocusedShell::BuildLightMenu(FMenuBuilder& MenuBuilder)
{
	auto AddLight = [this, &MenuBuilder](const TCHAR* Label, const TCHAR* Type)
	{
		MenuBuilder.AddMenuEntry(
			MenuText(Label), FText::FromString(Type), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, Type]()
			{
				CreateLight(Type);
			})));
	};

	AddLight(TEXT("点光源"), TEXT("Point"));
	AddLight(TEXT("聚光源"), TEXT("Spot"));
	AddLight(TEXT("方向光"), TEXT("Directional"));
	AddLight(TEXT("矩形光"), TEXT("Rect"));
	MenuBuilder.AddMenuSeparator();
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("删除选中灯光")), FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]()
		{
			DeleteSelectedEntityOfType(EEpicodeEntityType::Light);
		})));
}

void SEpicodeFocusedShell::BuildCameraMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("创建相机")), FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::CreateCamera)));
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("激活选中相机")), FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::ActivateSelectedCamera)));
	MenuBuilder.AddMenuSeparator();
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("删除选中相机")), FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]()
		{
			DeleteSelectedEntityOfType(EEpicodeEntityType::Camera);
		})));
}

void SEpicodeFocusedShell::BuildLibraryMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("刷新本地库")), FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::RefreshUi)));
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("新建场景")), FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::NewScene)));
	MenuBuilder.AddSubMenu(
		MenuText(TEXT("打开场景")), FText::GetEmpty(),
		FNewMenuDelegate::CreateSP(this, &SEpicodeFocusedShell::BuildOpenSceneMenu));
	MenuBuilder.AddMenuSeparator();
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("导入角色/物品")), MenuText(TEXT("导入流程将在任务 4 接入")), FSlateIcon(),
		FUIAction());
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("重建缩略图")), MenuText(TEXT("缩略图增强将在后续任务接入")), FSlateIcon(),
		FUIAction());
}

void SEpicodeFocusedShell::BuildEditMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("撤销")), FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::Undo)));
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("重做")), FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::Redo)));
	MenuBuilder.AddMenuSeparator();
	MenuBuilder.AddSubMenu(
		MenuText(TEXT("变换")), MenuText(TEXT("切换视口 gizmo")),
		FNewMenuDelegate::CreateSP(this, &SEpicodeFocusedShell::BuildTransformMenu));
	MenuBuilder.AddSubMenu(
		MenuText(TEXT("相机速度")), MenuText(TEXT("调整视口飞行速度")),
		FNewMenuDelegate::CreateSP(this, &SEpicodeFocusedShell::BuildCameraSpeedMenu));
	MenuBuilder.AddMenuSeparator();
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("删除选中")), FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::DeleteSelectedEntity)));
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("全选对象")), FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::SelectAllEntities)));
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("取消选择")), FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::DeselectAll)));
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("聚焦选中")), FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::FocusSelection)));
}

void SEpicodeFocusedShell::BuildTransformMenu(FMenuBuilder& MenuBuilder)
{
	auto AddMode = [this, &MenuBuilder](
		const TCHAR* Label, UE::Widget::EWidgetMode Mode)
	{
		MenuBuilder.AddMenuEntry(
			MenuText(Label), FText::GetEmpty(), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, Mode]()
			{
				SetTransformMode(Mode);
			})));
	};

	AddMode(TEXT("选择"), UE::Widget::WM_None);
	AddMode(TEXT("移动 (W)"), UE::Widget::WM_Translate);
	AddMode(TEXT("旋转 (E)"), UE::Widget::WM_Rotate);
	AddMode(TEXT("缩放 (R)"), UE::Widget::WM_Scale);
}

void SEpicodeFocusedShell::BuildCameraSpeedMenu(FMenuBuilder& MenuBuilder)
{
	for (int32 Speed = 1; Speed <= 8; ++Speed)
	{
		MenuBuilder.AddMenuEntry(
			FText::FromString(FString::Printf(TEXT("速度 %d"), Speed)),
			FText::GetEmpty(), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, Speed]()
				{
					SetCameraSpeed(Speed);
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &SEpicodeFocusedShell::IsCameraSpeed, Speed)),
			NAME_None, EUserInterfaceActionType::RadioButton);
	}
}

void SEpicodeFocusedShell::BuildViewMenu(FMenuBuilder& MenuBuilder)
{
	auto AddViewMode = [this, &MenuBuilder](
		const TCHAR* Label, EViewModeIndex Mode)
	{
		MenuBuilder.AddMenuEntry(
			MenuText(Label), FText::GetEmpty(), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, Mode]()
				{
					SetViewMode(Mode);
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &SEpicodeFocusedShell::IsViewMode, Mode)),
			NAME_None, EUserInterfaceActionType::RadioButton);
	};

	AddViewMode(TEXT("光照"), VMI_Lit);
	AddViewMode(TEXT("无光照"), VMI_Unlit);
	AddViewMode(TEXT("线框"), VMI_Wireframe);
	MenuBuilder.AddMenuSeparator();
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("显示网格")), FText::GetEmpty(), FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::ToggleGrid),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this]()
			{
				return Viewport.IsValid()
					&& Viewport->GetLevelViewportClient().IsSetShowGridChecked();
			})),
		NAME_None, EUserInterfaceActionType::ToggleButton);
	MenuBuilder.AddMenuSeparator();
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("对象列表")), FText::GetEmpty(), FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::ToggleLeftPanel),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &SEpicodeFocusedShell::IsLeftPanelVisible)),
		NAME_None, EUserInterfaceActionType::ToggleButton);
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("对话面板")), FText::GetEmpty(), FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::ToggleRightPanel),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &SEpicodeFocusedShell::IsRightPanelVisible)),
		NAME_None, EUserInterfaceActionType::ToggleButton);
}

void SEpicodeFocusedShell::BuildChatMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("新对话")), FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::NewChat)));
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("清空对话")), FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::ClearChat)));
	MenuBuilder.AddMenuSeparator();
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("显示对话面板")), FText::GetEmpty(), FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &SEpicodeFocusedShell::ToggleRightPanel),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &SEpicodeFocusedShell::IsRightPanelVisible)),
		NAME_None, EUserInterfaceActionType::ToggleButton);
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("对话设置")), MenuText(TEXT("模型与 API Key 设置将在任务 5.9 接入")), FSlateIcon(),
		FUIAction());
}

void SEpicodeFocusedShell::BuildHelpMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("快捷键说明")), FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]()
		{
			AddChatLine(TEXT("帮助"), TEXT("Q 选择，W 移动，E 旋转，R 缩放；Delete 删除；F 聚焦；Ctrl+Z 撤销，Ctrl+Shift+Z 重做。"));
		})));
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("诊断信息")), FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]()
		{
			AddChatLine(TEXT("诊断"), FString::Printf(
				TEXT("视口=%s 场景=%s 对象=%d 库=%d"),
				Viewport.IsValid() ? TEXT("就绪") : TEXT("失败"),
				Model.HasScene() ? *Model.GetSceneName() : TEXT("未打开"),
				Model.GetEntityCount(),
				Model.GetSceneCount()));
		})));
	MenuBuilder.AddMenuSeparator();
	MenuBuilder.AddMenuEntry(
		MenuText(TEXT("关于聚焦工作台")), FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]()
		{
			AddChatLine(TEXT("关于"), TEXT("Epicode 聚焦工作台 PoC。命令、场景和视口复用 EpicodeCore；对话与资产导入尚未接入。"));
		})));
}

TSharedRef<SWidget> SEpicodeFocusedShell::BuildWorkspace()
{
	return SNew(SSplitter)
		.PhysicalSplitterHandleSize(4.0f)
		+ SSplitter::Slot()
			.Value(TAttribute<float>::CreateLambda([this]()
			{
				return bLeftPanelVisible ? LeftPanelWidth : 0.0f;
			}))
			.MinSize(TAttribute<float>::CreateLambda([this]()
			{
				return bLeftPanelVisible ? 240.0f : 0.0f;
			}))
			.OnSlotResized_Lambda([this](float NewValue)
			{
				LeftPanelWidth = FMath::Clamp(NewValue, 0.12f, 0.45f);
				MarkLayoutDirty();
			})
			[
				SNew(SBox)
					.Visibility_Lambda([this]()
					{
						return bLeftPanelVisible ? EVisibility::Visible : EVisibility::Collapsed;
					})
					[
						BuildLeftPanel()
					]
			]
		+ SSplitter::Slot()
			.Value(TAttribute<float>::CreateLambda([this]()
			{
				const float Left = bLeftPanelVisible ? LeftPanelWidth : 0.0f;
				const float Right = bRightPanelVisible ? RightPanelWidth : 0.0f;
				return FMath::Max(0.10f, 1.0f - Left - Right);
			}))
			.MinSize(400.0f)
			[
				SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
					.Padding(FMargin(1.0f))
					[
						SAssignNew(ViewportHost, SBox)
						[
							BuildViewportContent()
						]
					]
			]
		+ SSplitter::Slot()
			.Value(TAttribute<float>::CreateLambda([this]()
			{
				return bRightPanelVisible ? RightPanelWidth : 0.0f;
			}))
			.MinSize(TAttribute<float>::CreateLambda([this]()
			{
				return bRightPanelVisible ? 260.0f : 0.0f;
			}))
			.OnSlotResized_Lambda([this](float NewValue)
			{
				RightPanelWidth = FMath::Clamp(NewValue, 0.12f, 0.45f);
				MarkLayoutDirty();
			})
			[
				SNew(SBox)
					.Visibility_Lambda([this]()
					{
						return bRightPanelVisible ? EVisibility::Visible : EVisibility::Collapsed;
					})
					[
						BuildChatPanel()
					]
			];
}

bool SEpicodeFocusedShell::CreateViewport()
{
	if (Viewport.IsValid())
	{
		return true;
	}

	TSharedPtr<ILevelEditor> LevelEditor = LevelEditorWeak.Pin();
	if (!LevelEditor.IsValid())
	{
		ViewportError = TEXT("LevelEditor instance is not available");
		return false;
	}

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
	ViewportError.Reset();
	return true;
}

void SEpicodeFocusedShell::RetryViewport()
{
	if (Viewport.IsValid())
	{
		return;
	}

	if (!CreateViewport())
	{
		Invalidate(EInvalidateWidgetReason::Layout);
		return;
	}

	if (ViewportHost.IsValid())
	{
		ViewportHost->SetContent(Viewport.ToSharedRef());
	}
	ApplySavedCamera();
	BindFocusedShortcuts();
	FocusViewport();
	RefreshUi();
	Invalidate(EInvalidateWidgetReason::Layout);
	UE_LOG(LogTemp, Display, TEXT("FocusedEditor viewport retry succeeded"));
}

TSharedRef<SWidget> SEpicodeFocusedShell::BuildViewportContent()
{
	if (Viewport.IsValid())
	{
		return Viewport.ToSharedRef();
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
		.Padding(FMargin(24.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
						.Text(FText::FromString(
							FString::Printf(
								TEXT("聚焦视口创建失败。\n%s"),
								ViewportError.IsEmpty() ? TEXT("未知错误") : *ViewportError)))
						.AutoWrapText(true)
				]
			+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 12.0f, 0.0f, 0.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(FMargin(0.0f, 0.0f, 8.0f, 0.0f))
						[
							SNew(SButton)
								.OnClicked_Lambda([this]()
								{
									RetryViewport();
									return FReply::Handled();
								})
								[
									SNew(STextBlock).Text(LOCTEXT("RetryViewport", "重试"))
								]
						]
					+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
								.Text(LOCTEXT("LegacyHint", "仍失败时使用 ./Scripts/Editor.sh --legacy"))
								.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
								.ColorAndOpacity(MutedTextColor())
						]
				]
		];
}

TSharedRef<SWidget> SEpicodeFocusedShell::MakePanelHeader(
	const FString& Title, TSharedRef<SWidget> Extra)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
					.Text(FText::FromString(Title))
					.Font(FAppStyle::Get().GetFontStyle("HeadingExtraSmall"))
					.ColorAndOpacity(PanelTextColor())
			]
		+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SSpacer)
			]
		+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				Extra
			];
}

TSharedRef<SWidget> SEpicodeFocusedShell::MakeFilterButton(
	const FString& Label, const FString& Value)
{
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "Menu.Button")
		.ContentPadding(FMargin(5.0f, 2.0f))
		.ButtonColorAndOpacity_Lambda([this, Value]()
		{
			return TypeFilter == Value
				? FLinearColor(0.18f, 0.32f, 0.48f, 1.0f)
				: FLinearColor(0.12f, 0.12f, 0.12f, 1.0f);
		})
		.OnClicked_Lambda([this, Value]()
		{
			TypeFilter = Value;
			RefreshObjectList();
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
				.Text(FText::FromString(Label))
				.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
				.ColorAndOpacity(PanelTextColor())
		];
}

TSharedRef<SWidget> SEpicodeFocusedShell::BuildLeftPanel()
{
	TSharedRef<SWidget> RefreshButton = SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "Menu.Button")
		.ContentPadding(FMargin(6.0f, 2.0f))
		.OnClicked_Lambda([this]()
		{
			RefreshUi();
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
				.Text(LOCTEXT("Refresh", "刷新"))
				.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
		];

	TSharedRef<SWidget> CollapseButton = SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "Menu.Button")
		.ContentPadding(FMargin(6.0f, 2.0f))
		.ToolTipText(LOCTEXT("CollapseObjects", "收起对象列表"))
		.OnClicked_Lambda([this]()
		{
			ToggleLeftPanel();
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
				.Text(FText::FromString(TEXT("<")))
				.Font(FAppStyle::Get().GetFontStyle("NormalFont"))
		];

	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
		.Padding(FMargin(6.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 0.0f, 0.0f, 4.0f))
				[
					MakePanelHeader(
						TEXT("对象"),
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(FMargin(0.0f, 0.0f, 4.0f, 0.0f))
							[
								SNew(STextBlock)
									.Text_Lambda([this]()
									{
										return FText::FromString(FString::Printf(
											TEXT("%d"), Model.GetEntityCount()));
									})
									.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
									.ColorAndOpacity(MutedTextColor())
							]
						+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								RefreshButton
							]
						+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(FMargin(3.0f, 0.0f, 0.0f, 0.0f))
							[
								CollapseButton
							])
				]
			+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 0.0f, 0.0f, 4.0f))
				[
					SAssignNew(ObjectSearchBox, SEditableTextBox)
						.HintText(LOCTEXT("SearchObjects", "搜索对象"))
						.OnTextChanged_Lambda([this](const FText& NewText)
						{
							ObjectSearch = NewText.ToString();
							RefreshObjectList();
						})
				]
			+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 0.0f, 0.0f, 4.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(FMargin(0.0f, 0.0f, 2.0f, 0.0f))
						[
							MakeFilterButton(TEXT("全部"), TEXT("All"))
						]
					+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(FMargin(2.0f, 0.0f))
						[
							MakeFilterButton(TEXT("角色"), TEXT("Character"))
						]
					+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(FMargin(2.0f, 0.0f))
						[
							MakeFilterButton(TEXT("物品"), TEXT("Prop"))
						]
					+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(FMargin(2.0f, 0.0f))
						[
							MakeFilterButton(TEXT("灯光"), TEXT("Light"))
						]
					+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(FMargin(2.0f, 0.0f))
						[
							MakeFilterButton(TEXT("摄像"), TEXT("Camera"))
						]
				]
			+ SVerticalBox::Slot()
				.FillHeight(0.55f)
				[
					SNew(SBorder)
						.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
						.Padding(FMargin(2.0f))
						[
							SNew(SScrollBox)
							+ SScrollBox::Slot()
								[
									SAssignNew(ObjectListBox, SVerticalBox)
								]
						]
				]
			+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 6.0f, 0.0f, 4.0f))
				[
					MakePanelHeader(
						TEXT("本地库"),
						SNew(STextBlock)
							.Text_Lambda([this]()
							{
								return FText::FromString(FString::Printf(
									TEXT("%d 场景"), Model.GetSceneCount()));
							})
							.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
							.ColorAndOpacity(MutedTextColor()))
				]
			+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 0.0f, 0.0f, 4.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(FMargin(0.0f, 0.0f, 4.0f, 0.0f))
						[
							SAssignNew(SceneNameInput, SEditableTextBox)
								.HintText(LOCTEXT("NewSceneName", "新场景名"))
						]
					+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
								.ButtonStyle(FAppStyle::Get(), "Menu.Button")
								.ContentPadding(FMargin(6.0f, 2.0f))
								.OnClicked_Lambda([this]()
								{
									NewScene();
									return FReply::Handled();
								})
								[
									SNew(STextBlock)
										.Text(LOCTEXT("NewScene", "新建"))
										.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
								]
						]
				]
			+ SVerticalBox::Slot()
				.FillHeight(0.45f)
				[
					SNew(SBorder)
						.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
						.Padding(FMargin(2.0f))
						[
							SNew(SScrollBox)
							+ SScrollBox::Slot()
								[
									SAssignNew(SceneListBox, SVerticalBox)
								]
						]
				]
		];
}

TSharedRef<SWidget> SEpicodeFocusedShell::BuildChatPanel()
{
	TSharedRef<SWidget> ClearButton = SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "Menu.Button")
		.ContentPadding(FMargin(6.0f, 2.0f))
		.OnClicked_Lambda([this]()
		{
			ClearChat();
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
				.Text(LOCTEXT("ClearChat", "清空"))
				.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
		];

	TSharedRef<SWidget> CollapseButton = SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "Menu.Button")
		.ContentPadding(FMargin(6.0f, 2.0f))
		.ToolTipText(LOCTEXT("CollapseChat", "收起对话面板"))
		.OnClicked_Lambda([this]()
		{
			ToggleRightPanel();
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
				.Text(FText::FromString(TEXT(">")))
				.Font(FAppStyle::Get().GetFontStyle("NormalFont"))
		];

	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
		.Padding(FMargin(6.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 0.0f, 0.0f, 4.0f))
				[
					MakePanelHeader(
						TEXT("对话"),
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								ClearButton
							]
						+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(FMargin(3.0f, 0.0f, 0.0f, 0.0f))
							[
								CollapseButton
							])
				]
			+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SBorder)
						.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
						.Padding(FMargin(4.0f))
						[
							SAssignNew(ChatScroll, SScrollBox)
							+ SScrollBox::Slot()
								[
									SAssignNew(ChatHistoryBox, SVerticalBox)
								]
						]
				]
			+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 6.0f, 0.0f, 4.0f))
				[
					SNew(SBox)
						.HeightOverride(64.0f)
						[
							SAssignNew(ChatInput, SMultiLineEditableTextBox)
								.HintText(LOCTEXT("ChatHint", "对话后端未配置"))
								.AutoWrapText(true)
						]
				]
			+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						[
							SAssignNew(ChatStatusText, STextBlock)
								.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
								.ColorAndOpacity(MutedTextColor())
						]
					+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
								.IsEnabled(false)
								.ToolTipText(LOCTEXT("ChatDisabled", "对话后端将在任务 5 接入"))
								[
									SNew(STextBlock)
										.Text(LOCTEXT("Send", "发送（待接入）"))
										.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
								]
						]
				]
		];
}

TSharedRef<SWidget> SEpicodeFocusedShell::BuildEmptyStateBanner()
{
	return SNew(SBorder)
		.Visibility_Lambda([this]()
		{
			return Model.HasScene() ? EVisibility::Collapsed : EVisibility::Visible;
		})
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Header"))
		.Padding(FMargin(14.0f, 12.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
						.Text(LOCTEXT("NoSceneTitle", "未打开 Epicode 场景"))
						.Font(FAppStyle::Get().GetFontStyle("HeadingSmall"))
						.ColorAndOpacity(PanelTextColor())
				]
			+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 3.0f, 0.0f, 8.0f))
				[
					SNew(STextBlock)
						.Text(LOCTEXT("NoSceneBody", "新建场景，或从左侧本地库打开已有场景。"))
						.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
						.ColorAndOpacity(MutedTextColor())
				]
			+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
						[
							SNew(SButton)
								.OnClicked_Lambda([this]()
								{
									NewScene();
									return FReply::Handled();
								})
								[
									SNew(STextBlock).Text(LOCTEXT("CreateScene", "新建场景"))
								]
						]
					+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
								.OnClicked_Lambda([this]()
								{
									if (!bLeftPanelVisible)
									{
										ToggleLeftPanel();
									}
									return FReply::Handled();
								})
								[
									SNew(STextBlock).Text(LOCTEXT("OpenLibrary", "打开本地库"))
								]
						]
				]
		];
}

TSharedRef<SWidget> SEpicodeFocusedShell::BuildStatusBar()
{
	auto MakeStatusText = [](TSharedPtr<STextBlock>& OutText, const FText& Text)
	{
		return SAssignNew(OutText, STextBlock)
			.Text(Text)
			.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
			.ColorAndOpacity(MutedTextColor());
	};

	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
		.Padding(FMargin(8.0f, 3.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					MakeStatusText(StatusSceneText, LOCTEXT("NoScene", "场景：无"))
				]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(10.0f, 0.0f))
				[
					MakeStatusText(StatusSelectionText, LOCTEXT("NoSelection", "选择：无"))
				]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(0.0f, 0.0f, 10.0f, 0.0f))
				[
					MakeStatusText(StatusModeText, LOCTEXT("Mode", "模式：选择"))
				]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(0.0f, 0.0f, 10.0f, 0.0f))
				[
					MakeStatusText(StatusLibraryText, LOCTEXT("Library", "库：0 场景"))
				]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(0.0f, 0.0f, 10.0f, 0.0f))
				[
					MakeStatusText(StatusChatText, LOCTEXT("Chat", "对话：未配置"))
				]
			+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(StatusMessageText, STextBlock)
						.Text(LOCTEXT("Ready", "就绪"))
						.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
						.ColorAndOpacity(MutedTextColor())
				]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(0.0f, 0.0f, 10.0f, 0.0f))
				[
					MakeStatusText(StatusRevisionText, LOCTEXT("Revision", "rev：0"))
				]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(8.0f, 0.0f, 10.0f, 0.0f))
				[
					MakeStatusText(StatusSaveText, LOCTEXT("Saved", "已保存"))
				]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
						.Text(LOCTEXT("ShellStatus", "EpicodeEditorShell / -EpicodeFocusedEditor"))
						.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.42f, 0.42f, 0.42f, 1.0f)))
				]
		];
}

void SEpicodeFocusedShell::RefreshUi()
{
	Model.Refresh();
	SyncSelectionFromViewport();

	const FString Signature = Model.GetSignature();
	if (Signature != LastSignature)
	{
		LastSignature = Signature;
		RefreshObjectList();
		RefreshSceneList();
	}
	RefreshStatus();
}

void SEpicodeFocusedShell::RefreshObjectList()
{
	if (!ObjectListBox.IsValid())
	{
		return;
	}

	ObjectListBox->ClearChildren();

	if (!Model.HasScene())
	{
		ObjectListBox->AddSlot()
			.AutoHeight()
			.Padding(FMargin(6.0f, 8.0f))
			[
				SNew(STextBlock)
					.Text(LOCTEXT("NoSceneObjects", "先新建或打开场景"))
					.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
					.ColorAndOpacity(MutedTextColor())
					.AutoWrapText(true)
			];
		return;
	}

	int32 VisibleCount = 0;
	for (const FEpicodeFocusedEntityRow& Row : Model.GetEntities())
	{
		if (TypeFilter != TEXT("All") && Row.Type != TypeFilter)
		{
			continue;
		}
		if (!ObjectSearch.IsEmpty()
			&& !Row.Name.Contains(ObjectSearch, ESearchCase::IgnoreCase)
			&& !Row.TypeLabel.Contains(ObjectSearch, ESearchCase::IgnoreCase))
		{
			continue;
		}

		++VisibleCount;
		const bool bSelected = Row.Id == Model.GetSelectedEntityId();
		const FString Prefix = bSelected ? TEXT("● ") : TEXT("  ");
		const FString Suffix = Row.bVisible ? FString() : TEXT(" · 隐藏");

		ObjectListBox->AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.0f, 1.0f))
			[
				SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "Menu.Button")
					.ContentPadding(FMargin(6.0f, 3.0f))
					.ButtonColorAndOpacity(bSelected
						? FLinearColor(0.16f, 0.28f, 0.42f, 1.0f)
						: FLinearColor(0.0f, 0.0f, 0.0f, 0.0f))
					.OnClicked_Lambda([this, Id = Row.Id]()
					{
						SelectEntity(Id, false);
						return FReply::Handled();
					})
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
									.Text(FText::FromString(Prefix))
									.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
									.ColorAndOpacity(AccentTextColor())
							]
						+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
									.Text(FText::FromString(
										FString::Printf(TEXT("%s  %s%s"), *Row.Name, *Row.TypeLabel, *Suffix)))
									.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
									.ColorAndOpacity(PanelTextColor())
							]
					]
			];
	}

	if (VisibleCount == 0)
	{
		ObjectListBox->AddSlot()
			.AutoHeight()
			.Padding(FMargin(6.0f, 8.0f))
			[
				SNew(STextBlock)
					.Text(Model.GetEntityCount() == 0
						? LOCTEXT("EmptyScene", "场景为空，可从菜单创建灯光或相机")
						: LOCTEXT("NoMatches", "没有匹配的对象"))
					.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
					.ColorAndOpacity(MutedTextColor())
					.AutoWrapText(true)
			];
	}
}

void SEpicodeFocusedShell::RefreshSceneList()
{
	if (!SceneListBox.IsValid())
	{
		return;
	}

	SceneListBox->ClearChildren();
	if (Model.GetScenes().IsEmpty())
	{
		SceneListBox->AddSlot()
			.AutoHeight()
			.Padding(FMargin(6.0f, 8.0f))
			[
				SNew(STextBlock)
					.Text(LOCTEXT("EmptyLibrary", "本地库为空"))
					.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
					.ColorAndOpacity(MutedTextColor())
			];
		return;
	}

	for (const FEpicodeFocusedSceneRow& Row : Model.GetScenes())
	{
		const FString Label = FString::Printf(
			TEXT("%s %s%s"),
			Row.bCurrent ? TEXT("●") : TEXT("○"),
			*Row.Name,
			Row.bCurrent ? TEXT(" · 当前") : TEXT(""));

		SceneListBox->AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.0f, 1.0f))
			[
				SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "Menu.Button")
					.ContentPadding(FMargin(6.0f, 3.0f))
					.IsEnabled(Row.bAvailable)
					.OnClicked_Lambda([this, Id = Row.Id]()
					{
						OpenSceneById(Id);
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
							.Text(FText::FromString(Label))
							.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
							.ColorAndOpacity(Row.bCurrent ? AccentTextColor() : PanelTextColor())
					]
			];
	}
}

void SEpicodeFocusedShell::RefreshStatus()
{
	if (StatusSceneText.IsValid())
	{
		StatusSceneText->SetText(FText::FromString(
			Model.HasScene()
				? FString::Printf(TEXT("场景：%s%s"), *Model.GetSceneName(), Model.IsDirty() ? TEXT(" *") : TEXT(""))
				: TEXT("场景：无")));
	}
	if (StatusSelectionText.IsValid())
	{
		StatusSelectionText->SetText(FText::FromString(
			FString::Printf(TEXT("选择：%s"), *Model.GetSelectedEntityLabel())));
	}
	if (StatusModeText.IsValid())
	{
		StatusModeText->SetText(FText::FromString(
			FString::Printf(TEXT("模式：%s"), *GetTransformModeLabel())));
	}
	if (StatusLibraryText.IsValid())
	{
		StatusLibraryText->SetText(FText::FromString(
			FString::Printf(TEXT("库：%d 场景"), Model.GetSceneCount())));
	}
	if (StatusChatText.IsValid())
	{
		StatusChatText->SetText(LOCTEXT("ChatNotConfigured", "对话：未配置"));
	}
	if (StatusSaveText.IsValid())
	{
		StatusSaveText->SetText(Model.HasScene()
			? (Model.IsDirty() ? LOCTEXT("Unsaved", "未保存") : LOCTEXT("Saved", "已保存"))
			: LOCTEXT("NoDocument", "无文档"));
		StatusSaveText->SetColorAndOpacity(Model.IsDirty()
			? FSlateColor(FLinearColor(0.95f, 0.66f, 0.35f, 1.0f))
			: MutedTextColor());
	}
	if (StatusMessageText.IsValid())
	{
		StatusMessageText->SetText(FText::FromString(Model.GetLastMessage()));
	}
	if (StatusRevisionText.IsValid())
	{
		StatusRevisionText->SetText(FText::FromString(Model.HasScene()
			? FString::Printf(TEXT("rev：%llu"), Model.GetRevision())
			: TEXT("rev：-")));
	}
	if (ChatStatusText.IsValid())
	{
		ChatStatusText->SetText(LOCTEXT("ChatBackendLater", "未配置 · 任务 5 接入"));
	}
}

void SEpicodeFocusedShell::SyncSelectionFromViewport()
{
	if (!GEditor)
	{
		return;
	}

	AActor* Actor = GEditor->GetSelectedActors()->GetTop<AActor>();
	const FGuid EntityId = Actor ? EpicodeActorFactory::GetEntityId(Actor) : FGuid();
	if (EntityId.IsValid() && Model.FindEntity(EntityId))
	{
		Model.SetSelectedEntityId(EntityId);
	}
	else
	{
		Model.SetSelectedEntityId(FGuid());
	}
}

void SEpicodeFocusedShell::SelectEntity(const FGuid& InId, bool bFocus)
{
	UEpicodeSceneSubsystem* Scene = Model.GetSceneSubsystem();
	AActor* Actor = Scene ? Scene->GetEntityActor(InId) : nullptr;
	if (!Actor)
	{
		Model.SetLastMessage(TEXT("对象没有对应的场景 Actor"));
		RefreshStatus();
		return;
	}

	GEditor->SelectNone(false, true);
	GEditor->SelectActor(Actor, true, true);
	GCurrentLevelEditingViewportClient = Viewport.IsValid()
		? &Viewport->GetLevelViewportClient()
		: GCurrentLevelEditingViewportClient;
	if (Viewport.IsValid())
	{
		Viewport->Invalidate();
	}
	Model.SetSelectedEntityId(InId);
	if (bFocus && Viewport.IsValid())
	{
		Viewport->OnFocusViewportToSelection();
	}
	RefreshObjectList();
	RefreshStatus();
}

void SEpicodeFocusedShell::AddChatLine(const FString& Speaker, const FString& Text)
{
	if (!ChatHistoryBox.IsValid())
	{
		return;
	}

	ChatHistoryBox->AddSlot()
		.AutoHeight()
		.Padding(FMargin(4.0f, 3.0f))
		[
			SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("%s：%s"), *Speaker, *Text)))
				.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
				.ColorAndOpacity(Speaker == TEXT("系统")
					? MutedTextColor()
					: PanelTextColor())
				.AutoWrapText(true)
		];

	if (ChatScroll.IsValid())
	{
		ChatScroll->ScrollToEnd();
	}
}

void SEpicodeFocusedShell::NewChat()
{
	if (ChatHistoryBox.IsValid())
	{
		ChatHistoryBox->ClearChildren();
	}
	AddChatLine(TEXT("系统"), TEXT("新对话。对话后端尚未配置。"));
}

void SEpicodeFocusedShell::ClearChat()
{
	if (ChatHistoryBox.IsValid())
	{
		ChatHistoryBox->ClearChildren();
	}
	AddChatLine(TEXT("系统"), TEXT("对话已清空。"));
}

FString SEpicodeFocusedShell::GetSceneNameInput() const
{
	return SceneNameInput.IsValid()
		? SceneNameInput->GetText().ToString().TrimStartAndEnd()
		: FString();
}

void SEpicodeFocusedShell::NewScene()
{
	FString Name = GetSceneNameInput();
	if (Name.IsEmpty())
	{
		Name = TEXT("Untitled Scene");
	}

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("name"), Name);
	const FEpicodeCommandResult Result = Model.Execute(TEXT("scene.new"), Params);
	if (Result.bSuccess)
	{
		MarkRecentScene(Model.GetSceneId());
	}
	RefreshUi();
}

void SEpicodeFocusedShell::SaveScene()
{
	Model.Execute(TEXT("scene.save"));
	RefreshUi();
}

void SEpicodeFocusedShell::SaveSceneAs()
{
	FString Name = GetSceneNameInput();
	if (Name.IsEmpty())
	{
		Name = Model.HasScene()
			? FString::Printf(TEXT("%s Copy"), *Model.GetSceneName())
			: TEXT("Untitled Scene");
	}

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("name"), Name);
	const FEpicodeCommandResult Result = Model.Execute(TEXT("scene.saveAs"), Params);
	if (Result.bSuccess)
	{
		MarkRecentScene(Model.GetSceneId());
	}
	RefreshUi();
}

void SEpicodeFocusedShell::CloseScene()
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetBoolField(TEXT("discard"), false);
	Model.Execute(TEXT("scene.close"), Params);
	RefreshUi();
}

void SEpicodeFocusedShell::OpenSceneById(FString InSceneId)
{
	if (Model.IsDirty())
	{
		const EAppReturnType::Type Answer = FMessageDialog::Open(
			EAppMsgType::YesNo,
			FText::FromString(TEXT("当前场景有未保存修改，仍要打开其他场景？")));
		if (Answer != EAppReturnType::Yes)
		{
			return;
		}
	}

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("sceneId"), InSceneId);
	const FEpicodeCommandResult Result = Model.Execute(TEXT("scene.open"), Params);
	if (Result.bSuccess)
	{
		MarkRecentScene(InSceneId);
	}
	RefreshUi();
}

void SEpicodeFocusedShell::DeleteSceneById(FString InSceneId)
{
	const EAppReturnType::Type Answer = FMessageDialog::Open(
		EAppMsgType::YesNo,
		FText::FromString(TEXT("将场景移到回收站？")));
	if (Answer != EAppReturnType::Yes)
	{
		return;
	}

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("sceneId"), InSceneId);
	Model.Execute(TEXT("scene.delete"), Params);
	RefreshUi();
}

void SEpicodeFocusedShell::DeleteSelectedEntity()
{
	const FGuid Id = Model.GetSelectedEntityId();
	if (!Id.IsValid())
	{
		Model.SetLastMessage(TEXT("没有选中的 Epicode 对象"));
		RefreshStatus();
		return;
	}

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("id"), Id.ToString(EGuidFormats::DigitsWithHyphens));
	Model.Execute(TEXT("entity.delete"), Params);
	RefreshUi();
}

void SEpicodeFocusedShell::DeleteSelectedEntityOfType(EEpicodeEntityType Type)
{
	const FEpicodeFocusedEntityRow* Row = Model.FindEntity(Model.GetSelectedEntityId());
	if (!Row)
	{
		Model.SetLastMessage(TEXT("没有选中的 Epicode 对象"));
		RefreshStatus();
		return;
	}

	FString Expected;
	switch (Type)
	{
	case EEpicodeEntityType::Character:
		Expected = TEXT("Character");
		break;
	case EEpicodeEntityType::Prop:
		Expected = TEXT("Prop");
		break;
	case EEpicodeEntityType::Light:
		Expected = TEXT("Light");
		break;
	case EEpicodeEntityType::Camera:
		Expected = TEXT("Camera");
		break;
	default:
		break;
	}

	if (!Expected.IsEmpty() && Row->Type != Expected)
	{
		Model.SetLastMessage(FString::Printf(
			TEXT("当前选择不是%s对象"), *Row->TypeLabel));
		RefreshStatus();
		return;
	}

	DeleteSelectedEntity();
}

void SEpicodeFocusedShell::CreateLight(const FString& LightType)
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("lightType"), LightType);
	Params->SetStringField(TEXT("name"), FString::Printf(TEXT("%sLight"), *LightType));
	Model.Execute(TEXT("light.create"), Params);
	RefreshUi();
}

void SEpicodeFocusedShell::CreateCamera()
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("name"), TEXT("Camera"));
	Model.Execute(TEXT("camera.create"), Params);
	RefreshUi();
}

void SEpicodeFocusedShell::ActivateSelectedCamera()
{
	const FEpicodeFocusedEntityRow* Row = Model.FindEntity(Model.GetSelectedEntityId());
	if (!Row || Row->Type != TEXT("Camera"))
	{
		Model.SetLastMessage(TEXT("请先选择一个相机对象"));
		RefreshStatus();
		return;
	}

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("id"), Row->Id.ToString(EGuidFormats::DigitsWithHyphens));
	Model.Execute(TEXT("camera.activate"), Params);
	RefreshUi();
}

void SEpicodeFocusedShell::SetTransformMode(UE::Widget::EWidgetMode Mode)
{
	SetWidgetMode(Mode);
	RefreshStatus();
}

void SEpicodeFocusedShell::SetCameraSpeed(int32 SpeedSetting)
{
	if (!Viewport.IsValid())
	{
		return;
	}
	FEditorViewportClient& Client = Viewport->GetLevelViewportClient();
	FEditorViewportCameraSpeedSettings Settings = Client.GetCameraSpeedSettings();
	Settings.SetRelativeUISpeed(
		static_cast<float>(FMath::Clamp(SpeedSetting, 1, 8) - 1) / 7.0f);
	Client.SetCameraSpeedSettings(Settings);
	Model.SetLastMessage(FString::Printf(TEXT("相机速度设置为 %d"), SpeedSetting));
	RefreshStatus();
}

bool SEpicodeFocusedShell::IsCameraSpeed(int32 SpeedSetting) const
{
	if (!Viewport.IsValid())
	{
		return false;
	}
	const float Expected =
		static_cast<float>(FMath::Clamp(SpeedSetting, 1, 8) - 1) / 7.0f;
	return FMath::IsNearlyEqual(
		Viewport->GetLevelViewportClient().GetCameraSpeedSettings().GetRelativeUISpeed(),
		Expected,
		0.01f);
}

void SEpicodeFocusedShell::SetViewMode(EViewModeIndex Mode)
{
	if (!Viewport.IsValid())
	{
		return;
	}
	Viewport->GetLevelViewportClient().SetViewMode(Mode);
	Viewport->Invalidate();
}

bool SEpicodeFocusedShell::IsViewMode(EViewModeIndex Mode) const
{
	return Viewport.IsValid()
		&& Viewport->GetLevelViewportClient().GetViewMode() == Mode;
}

void SEpicodeFocusedShell::ToggleGrid()
{
	if (!Viewport.IsValid())
	{
		return;
	}
	Viewport->GetLevelViewportClient().SetShowGrid();
	Viewport->Invalidate();
}

void SEpicodeFocusedShell::FocusSelection()
{
	if (Viewport.IsValid())
	{
		Viewport->OnFocusViewportToSelection();
	}
}

void SEpicodeFocusedShell::SelectAllEntities()
{
	UEpicodeSceneSubsystem* Scene = Model.GetSceneSubsystem();
	if (!Scene || !Model.HasScene())
	{
		Model.SetLastMessage(TEXT("没有打开的场景"));
		RefreshStatus();
		return;
	}

	GEditor->SelectNone(false, true);
	for (const FEpicodeFocusedEntityRow& Row : Model.GetEntities())
	{
		if (AActor* Actor = Scene->GetEntityActor(Row.Id))
		{
			GEditor->SelectActor(Actor, true, false);
		}
	}
	GEditor->NoteSelectionChange();
	if (Viewport.IsValid())
	{
		Viewport->Invalidate();
	}
}

void SEpicodeFocusedShell::DeselectAll()
{
	GEditor->SelectNone(false, true);
	if (Viewport.IsValid())
	{
		Viewport->Invalidate();
	}
}

void SEpicodeFocusedShell::Undo()
{
	GEditor->UndoTransaction();
	RefreshUi();
}

void SEpicodeFocusedShell::Redo()
{
	GEditor->RedoTransaction();
	RefreshUi();
}

void SEpicodeFocusedShell::ToggleLeftPanel()
{
	bLeftPanelVisible = !bLeftPanelVisible;
	MarkLayoutDirty();
	Model.SetLastMessage(bLeftPanelVisible ? TEXT("对象列表已展开") : TEXT("对象列表已收起"));
	RefreshStatus();
}

void SEpicodeFocusedShell::ToggleRightPanel()
{
	bRightPanelVisible = !bRightPanelVisible;
	MarkLayoutDirty();
	Model.SetLastMessage(bRightPanelVisible ? TEXT("对话面板已展开") : TEXT("对话面板已收起"));
	RefreshStatus();
}

FString SEpicodeFocusedShell::GetTransformModeLabel() const
{
	if (!Viewport.IsValid())
	{
		return TEXT("无视口");
	}

	switch (Viewport->GetLevelViewportClient().GetWidgetMode())
	{
	case UE::Widget::WM_Translate:
		return TEXT("移动");
	case UE::Widget::WM_Rotate:
		return TEXT("旋转");
	case UE::Widget::WM_Scale:
		return TEXT("缩放");
	default:
		return TEXT("选择");
	}
}

#undef LOCTEXT_NAMESPACE
