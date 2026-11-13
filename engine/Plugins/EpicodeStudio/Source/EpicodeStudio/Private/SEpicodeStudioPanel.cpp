#include "SEpicodeStudioPanel.h"

#include "Dom/JsonObject.h"
#include "EpicodeCommand.h"
#include "EpicodeEngineSubsystem.h"
#include "EpicodeSceneSubsystem.h"
#include "EpicodeSettings.h"
#include "ContentBrowserModule.h"
#include "DesktopPlatformModule.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Input/DragAndDrop.h"
#include "IDesktopPlatform.h"
#include "IContentBrowserSingleton.h"
#include "Modules/ModuleManager.h"
#include "PropertyCustomizationHelpers.h"
#include "SDropTarget.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	class FEpicodeOutlinerDragDropOperation : public FDragDropOperation
	{
	public:
		DRAG_DROP_OPERATOR_TYPE(FEpicodeOutlinerDragDropOperation, FDragDropOperation)
		FString EntityId;
	};

	FString JsonString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, const FString& Default = FString())
	{
		FString Value;
		if (Object.IsValid() && Object->TryGetStringField(Field, Value))
		{
			return Value;
		}
		return Default;
	}

	double JsonNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, double Default = 0.0)
	{
		double Value = Default;
		if (Object.IsValid() && Object->TryGetNumberField(Field, Value))
		{
			return Value;
		}
		return Default;
	}

	TSharedPtr<FJsonObject> MakeParams()
	{
		return MakeShared<FJsonObject>();
	}

	FString Shorten(const FString& Value, int32 MaxLength = 48)
	{
		return Value.Len() <= MaxLength ? Value : Value.Left(MaxLength - 3) + TEXT("...");
	}

	bool JsonVector3(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FVector& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object.IsValid() && Object->TryGetArrayField(Field, Values) && Values && Values->Num() == 3)
		{
			Out = FVector((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
			return true;
		}
		return false;
	}

	FString FormatVector3(const FVector& Value)
	{
		return FString::Printf(TEXT("%.3f, %.3f, %.3f"), Value.X, Value.Y, Value.Z);
	}

	bool ParseVector3(const FString& Text, FVector& Out)
	{
		TArray<FString> Parts;
		Text.ParseIntoArray(Parts, TEXT(","), true);
		if (Parts.Num() != 3)
		{
			return false;
		}
		Out = FVector(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1]), FCString::Atod(*Parts[2]));
		return true;
	}
}

void SEpicodeStudioPanel::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
		.Padding(6.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildToolbar()
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(0.0f, 6.0f)
			[
				SAssignNew(Switcher, SWidgetSwitcher)
				.WidgetIndex_Lambda([this]() { return ViewModel.GetActiveTab(); })
				+ SWidgetSwitcher::Slot()
				[
					BuildScenePanel()
				]
				+ SWidgetSwitcher::Slot()
				[
					BuildOutlinerPanel()
				]
				+ SWidgetSwitcher::Slot()
				[
					BuildCharacterPanel()
				]
				+ SWidgetSwitcher::Slot()
				[
					BuildPropPanel()
				]
				+ SWidgetSwitcher::Slot()
				[
					BuildLightPanel()
				]
				+ SWidgetSwitcher::Slot()
				[
					BuildEnvironmentPanel()
				]
				+ SWidgetSwitcher::Slot()
				[
					BuildImportPanel()
				]
				+ SWidgetSwitcher::Slot()
				[
					BuildRenderPanel()
				]
				+ SWidgetSwitcher::Slot()
				[
					BuildApiPanel()
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildLogPanel()
			]
		]
	];
	RefreshAll();
}

void SEpicodeStudioPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
	if (!Scene)
	{
		return;
	}
	if (ViewModel.NeedsRefresh(Scene->GetDocument().Revision))
	{
		RefreshAll();
	}
}

TSharedRef<SWidget> SEpicodeStudioPanel::BuildToolbar()
{
	auto TabButton = [this](const FString& Label, int32 Index)
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
			.OnClicked_Lambda([this, Index]() { ViewModel.SetActiveTab(Index); return FReply::Handled(); })
			.Content()
			[
				SNew(STextBlock)
				.Text(FText::FromString(Label))
				.ColorAndOpacity_Lambda([this, Index]() { return ViewModel.GetActiveTab() == Index ? FLinearColor(0.3f, 0.8f, 1.0f) : FLinearColor::White; })
			];
	};

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 12.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("EPICODE STUDIO")))
			.Font(FAppStyle::GetFontStyle(TEXT("BoldFont")))
		]
		+ SHorizontalBox::Slot().AutoWidth()[TabButton(TEXT("Scenes"), 0)]
		+ SHorizontalBox::Slot().AutoWidth()[TabButton(TEXT("Outliner"), 1)]
		+ SHorizontalBox::Slot().AutoWidth()[TabButton(TEXT("Characters"), 2)]
		+ SHorizontalBox::Slot().AutoWidth()[TabButton(TEXT("Props"), 3)]
		+ SHorizontalBox::Slot().AutoWidth()[TabButton(TEXT("Lights"), 4)]
		+ SHorizontalBox::Slot().AutoWidth()[TabButton(TEXT("Environment"), 5)]
		+ SHorizontalBox::Slot().AutoWidth()[TabButton(TEXT("Import"), 6)]
		+ SHorizontalBox::Slot().AutoWidth()[TabButton(TEXT("Render"), 7)]
		+ SHorizontalBox::Slot().AutoWidth()[TabButton(TEXT("API"), 8)]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		[
			SAssignNew(StatusText, STextBlock)
			.Text(FText::FromString(TEXT("No scene")))
		];
}

TSharedRef<SWidget> SEpicodeStudioPanel::BuildScenePanel()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			MakeInput(TEXT("New scene"), SceneNameInput, TEXT("Scene name"), TEXT("Untitled"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("New"), [this]()
				{
					TSharedPtr<FJsonObject> Params = MakeParams();
					Params->SetStringField(TEXT("name"), SceneNameInput.IsValid() ? SceneNameInput->GetText().ToString() : TEXT("Untitled"));
					Execute(TEXT("scene.new"), Params);
					RefreshAll();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Save"), [this]()
				{
					Execute(TEXT("scene.save"), MakeParams());
					RefreshAll();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Close"), [this]()
				{
					Execute(TEXT("scene.close"), MakeParams());
					RefreshAll();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				SNew(SBox).WidthOverride(220.0f)
				[
					SAssignNew(SceneSaveAsInput, SEditableTextBox)
					.HintText(FText::FromString(TEXT("Save-as name")))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Save As"), [this]()
				{
					TSharedPtr<FJsonObject> Params = MakeParams();
					Params->SetStringField(TEXT("name"), SceneSaveAsInput.IsValid() ? SceneSaveAsInput->GetText().ToString() : TEXT("Untitled"));
					Execute(TEXT("scene.saveAs"), Params);
					RefreshAll();
				})
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(2.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
			.Padding(4.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(SceneList, SVerticalBox)
				]
			]
	];
}

TSharedRef<SWidget> SEpicodeStudioPanel::BuildOutlinerPanel()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				SNew(SBox).WidthOverride(260.0f)
				[
					SAssignNew(OutlinerSearchInput, SEditableTextBox)
					.HintText(FText::FromString(TEXT("Filter by name, type or id")))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Filter"), [this]() { RefreshOutliner(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Clear"), [this]()
				{
					if (OutlinerSearchInput.IsValid())
					{
						OutlinerSearchInput->SetText(FText::GetEmpty());
					}
					RefreshOutliner();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Delete selected"), [this]()
				{
					const TArray<FString> Ids = ViewModel.GetSelectedEntities().Array();
					for (const FString& Id : Ids)
					{
						TSharedPtr<FJsonObject> Params = MakeParams();
						Params->SetStringField(TEXT("id"), Id);
						Execute(TEXT("entity.delete"), Params);
					}
					ViewModel.ClearSelection();
					RefreshAll();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Clear selection"), [this]()
				{
					ViewModel.ClearSelection();
					RefreshOutliner();
				})
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(2.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(OutlinerList, SVerticalBox)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			SAssignNew(OutlinerSelectionText, STextBlock)
			.Text(FText::FromString(TEXT("No entity selected")))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			SAssignNew(OutlinerInspector, SVerticalBox)
		];
}

TSharedRef<SWidget> SEpicodeStudioPanel::BuildCharacterPanel()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			MakeInput(TEXT("Name"), CharacterNameInput, TEXT("Character"), TEXT("Character"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SBox).WidthOverride(110.0f)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Skeletal mesh")))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(USkeletalMesh::StaticClass())
				.AllowClear(true)
				.ObjectPath_Lambda([this]() { return CharacterMeshPath; })
				.OnObjectChanged_Lambda([this](const FAssetData& Asset)
				{
					CharacterMeshPath = Asset.IsValid() ? Asset.GetSoftObjectPath().ToString() : FString();
				})
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			MakeInput(TEXT("Animation"), CharacterAnimationInput, TEXT("/Game/Path/Anim_Idle"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Spawn character"), [this]() { SpawnCharacter(); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				SNew(SBox).WidthOverride(260.0f)
				[
					SAssignNew(CharacterFilterInput, SEditableTextBox)
					.HintText(FText::FromString(TEXT("Filter by name or mesh")))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Filter"), [this]() { RefreshCharacterList(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Clear"), [this]()
				{
					if (CharacterFilterInput.IsValid())
					{
						CharacterFilterInput->SetText(FText::GetEmpty());
					}
					RefreshCharacterList();
				})
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(2.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
			.Padding(4.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(CharacterList, SVerticalBox)
				]
			]
		];
}

TSharedRef<SWidget> SEpicodeStudioPanel::BuildPropPanel()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			MakeInput(TEXT("Name"), PropNameInput, TEXT("Prop"), TEXT("Prop"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SBox).WidthOverride(110.0f)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Static mesh")))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(UStaticMesh::StaticClass())
				.AllowClear(true)
				.ObjectPath_Lambda([this]() { return PropMeshPath; })
				.OnObjectChanged_Lambda([this](const FAssetData& Asset)
				{
					PropMeshPath = Asset.IsValid() ? Asset.GetSoftObjectPath().ToString() : FString();
				})
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SBox).WidthOverride(110.0f)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Material")))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(UMaterialInterface::StaticClass())
				.AllowClear(true)
				.ObjectPath_Lambda([this]() { return PropMaterialPath; })
				.OnObjectChanged_Lambda([this](const FAssetData& Asset)
				{
					PropMaterialPath = Asset.IsValid() ? Asset.GetSoftObjectPath().ToString() : FString();
				})
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			MakeInput(TEXT("Material slot"), MaterialSlotInput, TEXT("0"), TEXT("0"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			MakeButton(TEXT("Spawn prop"), [this]() { SpawnProp(); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				SNew(SBox).WidthOverride(260.0f)
				[
					SAssignNew(PropFilterInput, SEditableTextBox)
					.HintText(FText::FromString(TEXT("Filter by name or mesh")))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Filter"), [this]() { RefreshPropList(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Clear"), [this]()
				{
					if (PropFilterInput.IsValid())
					{
						PropFilterInput->SetText(FText::GetEmpty());
					}
					RefreshPropList();
				})
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(2.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
			.Padding(4.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(PropList, SVerticalBox)
				]
			]
		];
}

TSharedRef<SWidget> SEpicodeStudioPanel::BuildLightPanel()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			MakeInput(TEXT("Name"), LightNameInput, TEXT("Key Light"), TEXT("Key Light"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			MakeInput(TEXT("Type"), LightTypeInput, TEXT("directional | point | spot | rect"), TEXT("point"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			MakeInput(TEXT("Intensity"), LightIntensityInput, TEXT("1.0"), TEXT("1.0"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			MakeButton(TEXT("Create light"), [this]() { CreateLight(); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				SNew(SBox).WidthOverride(260.0f)
				[
					SAssignNew(LightFilterInput, SEditableTextBox)
					.HintText(FText::FromString(TEXT("Filter by name or type")))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Filter"), [this]() { RefreshLightList(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Clear"), [this]()
				{
					if (LightFilterInput.IsValid())
					{
						LightFilterInput->SetText(FText::GetEmpty());
					}
					RefreshLightList();
				})
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(2.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
			.Padding(4.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(LightList, SVerticalBox)
				]
			]
		];
}

TSharedRef<SWidget> SEpicodeStudioPanel::BuildEnvironmentPanel()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			SAssignNew(EnvironmentSummary, STextBlock)
			.Text(FText::FromString(TEXT("Environment is available once a scene is open.")))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)[MakeButton(TEXT("Clear"), [this]() { TSharedPtr<FJsonObject> P = MakeParams(); P->SetStringField(TEXT("preset"), TEXT("clear")); Execute(TEXT("environment.applyPreset"), P); RefreshAll(); })]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)[MakeButton(TEXT("Overcast"), [this]() { TSharedPtr<FJsonObject> P = MakeParams(); P->SetStringField(TEXT("preset"), TEXT("overcast")); Execute(TEXT("environment.applyPreset"), P); RefreshAll(); })]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)[MakeButton(TEXT("Sunset"), [this]() { TSharedPtr<FJsonObject> P = MakeParams(); P->SetStringField(TEXT("preset"), TEXT("sunset")); Execute(TEXT("environment.applyPreset"), P); RefreshAll(); })]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)[MakeButton(TEXT("Night"), [this]() { TSharedPtr<FJsonObject> P = MakeParams(); P->SetStringField(TEXT("preset"), TEXT("night")); Execute(TEXT("environment.applyPreset"), P); RefreshAll(); })]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)[MakeInput(TEXT("Time of day"), EnvTimeInput, TEXT("0..24"), TEXT("15"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)[MakeInput(TEXT("Fog density"), EnvFogInput, TEXT(">= 0"), TEXT("0.02"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)[MakeInput(TEXT("Cloud coverage"), EnvCloudInput, TEXT("0..1"), TEXT("0.3"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)[MakeInput(TEXT("Exposure"), EnvExposureInput, TEXT("-10..10"), TEXT("0"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)[MakeInput(TEXT("Bloom"), EnvBloomInput, TEXT(">= 0"), TEXT("0.675"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			MakeButton(TEXT("Apply environment"), [this]() { ApplyEnvironment(); })
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(2.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
			.Padding(4.0f)
			[
				SAssignNew(EnvironmentBox, SVerticalBox)
			]
		];
}

TSharedRef<SWidget> SEpicodeStudioPanel::BuildImportPanel()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			MakeInput(TEXT("File"), ImportPathInput, TEXT("/absolute/path/model.fbx"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			MakeInput(TEXT("Kind"), ImportKindInput, TEXT("prop | character | scene"), TEXT("prop"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Browse..."), [this]()
				{
					TArray<FString> Files;
					if (FDesktopPlatformModule::Get() && FDesktopPlatformModule::Get()->OpenFileDialog(
						nullptr,
						TEXT("Select assets to import"),
						FPaths::ProjectDir(),
						FString(),
						TEXT("Importable files (*.fbx;*.gltf;*.glb;*.obj;*.usd;*.usdz)|*.fbx;*.gltf;*.glb;*.obj;*.usd;*.usdz"),
						EFileDialogFlags::Multiple,
						Files) && Files.Num() > 0)
					{
						SelectedImportFiles = Files;
						if (ImportPathInput.IsValid())
						{
							ImportPathInput->SetText(FText::FromString(Files[0]));
						}
						Log(FString::Printf(TEXT("Selected %d import file(s)"), Files.Num()));
					}
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				SAssignNew(ImportAutoPlaceCheck, SCheckBox)
				.IsChecked(true)
				.OnCheckStateChanged_Lambda([this](ECheckBoxState) {})
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Auto place")))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				SAssignNew(ImportReplaceCheck, SCheckBox)
				.IsChecked(false)
				.OnCheckStateChanged_Lambda([this](ECheckBoxState) {})
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Replace existing")))
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Submit import"), [this]()
				{
					TArray<FString> Files = SelectedImportFiles;
					if (Files.Num() == 0 && ImportPathInput.IsValid())
					{
						Files.Add(ImportPathInput->GetText().ToString());
					}
					for (const FString& File : Files)
					{
						TSharedPtr<FJsonObject> Params = MakeParams();
						Params->SetStringField(TEXT("filePath"), File);
						Params->SetStringField(TEXT("kind"), ImportKindInput.IsValid() ? ImportKindInput->GetText().ToString() : TEXT("prop"));
						Params->SetBoolField(TEXT("autoPlace"), !ImportAutoPlaceCheck.IsValid() || ImportAutoPlaceCheck->IsChecked());
						Params->SetBoolField(TEXT("replaceExisting"), ImportReplaceCheck.IsValid() && ImportReplaceCheck->IsChecked());
						Execute(TEXT("import.submit"), Params);
					}
					SelectedImportFiles.Reset();
					RefreshImportList();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Refresh"), [this]() { RefreshImportList(); })
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(2.0f)
		[
			SNew(SDropTarget)
			.OnAllowDrop_Lambda([](TSharedPtr<FDragDropOperation> Operation)
			{
				return Operation.IsValid() && Operation->IsOfType<FExternalDragOperation>() && StaticCastSharedPtr<FExternalDragOperation>(Operation)->HasFiles();
			})
			.OnDropped_Lambda([this](const FGeometry&, const FDragDropEvent& Event)
			{
				TSharedPtr<FDragDropOperation> Operation = Event.GetOperation();
				if (!Operation.IsValid() || !Operation->IsOfType<FExternalDragOperation>())
				{
					return FReply::Unhandled();
				}
				TArray<FString> Files;
				for (const FString& File : StaticCastSharedPtr<FExternalDragOperation>(Operation)->GetFiles())
				{
					const FString Extension = FPaths::GetExtension(File).ToLower();
					if (Extension == TEXT("fbx") || Extension == TEXT("gltf") || Extension == TEXT("glb") || Extension == TEXT("obj") || Extension == TEXT("usd") || Extension == TEXT("usdz"))
					{
						Files.Add(File);
					}
				}
				if (Files.Num() == 0)
				{
					Log(TEXT("Dropped files are not supported import formats."));
					return FReply::Unhandled();
				}
				SelectedImportFiles = Files;
				if (ImportPathInput.IsValid())
				{
					ImportPathInput->SetText(FText::FromString(Files[0]));
				}
				Log(FString::Printf(TEXT("Dropped %d import file(s)"), Files.Num()));
				return FReply::Handled();
			})
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
				.Padding(4.0f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(ImportList, SVerticalBox)
					]
				]
			]
		];
}

TSharedRef<SWidget> SEpicodeStudioPanel::BuildRenderPanel()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Submit 3-frame PNG"), [this]()
				{
					TSharedPtr<FJsonObject> Params = MakeParams();
					Params->SetStringField(TEXT("format"), TEXT("png"));
					Params->SetNumberField(TEXT("width"), 320);
					Params->SetNumberField(TEXT("height"), 180);
					Params->SetNumberField(TEXT("startFrame"), 0);
					Params->SetNumberField(TEXT("endFrame"), 3);
					Params->SetBoolField(TEXT("includeAudio"), false);
					Execute(TEXT("scene.render"), Params);
					RefreshRenderList();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Refresh"), [this]() { RefreshRenderList(); })
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(2.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
			.Padding(4.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(RenderList, SVerticalBox)
				]
			]
		];
}

TSharedRef<SWidget> SEpicodeStudioPanel::BuildApiPanel()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			MakeButton(TEXT("Run health check"), [this]() { RefreshApiPanel(); })
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(2.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
			.Padding(4.0f)
			[
				SAssignNew(ApiSummary, STextBlock)
				.AutoWrapText(true)
				.Text(FText::FromString(TEXT("API status not checked.")))
			]
		];
}

TSharedRef<SWidget> SEpicodeStudioPanel::BuildLogPanel()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
		.Padding(4.0f)
		[
			SNew(SBox)
			.HeightOverride(110.0f)
			[
				SAssignNew(LogScroll, SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(LogText, STextBlock)
					.Text(FText::FromString(TEXT("Ready.")))
				]
			]
		];
}

TSharedRef<SWidget> SEpicodeStudioPanel::MakeInput(const FString& Label, TSharedPtr<SEditableTextBox>& OutInput, const FString& Hint, const FString& Initial)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(110.0f)
			[
				SNew(STextBlock).Text(FText::FromString(Label))
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SAssignNew(OutInput, SEditableTextBox)
			.HintText(FText::FromString(Hint))
			.Text(FText::FromString(Initial))
		];
}

TSharedRef<SWidget> SEpicodeStudioPanel::MakeButton(const FString& Label, TFunction<void()> OnClick)
{
	return SNew(SButton)
		.OnClicked_Lambda([OnClick = MoveTemp(OnClick)]() { OnClick(); return FReply::Handled(); })
		[
			SNew(STextBlock).Text(FText::FromString(Label))
		];
}

void SEpicodeStudioPanel::AddRow(const TSharedPtr<SVerticalBox>& Box, const TSharedRef<SWidget>& Row)
{
	if (Box.IsValid())
	{
		Box->AddSlot().AutoHeight().Padding(2.0f)[Row];
	}
}

void SEpicodeStudioPanel::AddEmptyState(const TSharedPtr<SVerticalBox>& Box, const FString& Text)
{
	AddRow(Box, SNew(STextBlock)
		.Text(FText::FromString(Text))
		.ColorAndOpacity(FLinearColor(0.55f, 0.55f, 0.55f)));
}

FEpicodeCommandResult SEpicodeStudioPanel::Execute(const FString& Method, const TSharedPtr<FJsonObject>& Params)
{
	FEpicodeCommandResult Result;
	UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get();
	if (!EngineSubsystem || !EngineSubsystem->GetCommandRegistry())
	{
		Result = FEpicodeCommandResult::Failure(TEXT("NOT_INITIALIZED"), TEXT("Engine subsystem is not ready."));
		Log(FString::Printf(TEXT("%s failed: %s"), *Method, *Result.ErrorMessage));
		return Result;
	}
	FEpicodeCommandContext Context;
	Context.World = UEpicodeSceneSubsystem::GetPrimaryWorld();
	Context.Source = TEXT("studio");
	FString UnknownMethod;
	if (!EngineSubsystem->GetCommandRegistry()->Execute(Method, Params, Context, Result, UnknownMethod))
	{
		Result = FEpicodeCommandResult::Failure(TEXT("METHOD_NOT_FOUND"), FString::Printf(TEXT("Unknown method: %s"), *Method));
	}
	if (Result.bSuccess)
	{
		Log(FString::Printf(TEXT("%s ok"), *Method));
	}
	else
	{
		Log(FString::Printf(TEXT("%s failed [%s]: %s"), *Method, *Result.ErrorCode, *Result.ErrorMessage));
	}
	return Result;
}

void SEpicodeStudioPanel::Log(const FString& Text)
{
	LogBuffer += Text + TEXT("\n");
	if (LogBuffer.Len() > 8000)
	{
		LogBuffer = LogBuffer.Right(8000);
	}
	if (LogText.IsValid())
	{
		LogText->SetText(FText::FromString(LogBuffer));
	}
	if (LogScroll.IsValid())
	{
		LogScroll->ScrollToEnd();
	}
}

void SEpicodeStudioPanel::RefreshAll()
{
	UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
	if (StatusText.IsValid())
	{
		if (Scene && Scene->HasScene())
		{
			StatusText->SetText(FText::FromString(FString::Printf(
				TEXT("%s%s | rev %llu | %s"),
				*Scene->GetCurrentSceneName(),
				Scene->IsDirty() ? TEXT(" *") : TEXT(""),
				Scene->GetDocument().Revision,
				*Scene->GetCurrentSceneId())));
		}
		else
		{
			StatusText->SetText(FText::FromString(TEXT("No scene")));
		}
	}
	RefreshSceneList();
	RefreshOutliner();
	RefreshCharacterList();
	RefreshPropList();
	RefreshLightList();
	RefreshEnvironment();
	RefreshImportList();
	RefreshRenderList();
	RefreshApiPanel();
	if (Scene)
	{
		ViewModel.MarkRefreshed(Scene->GetDocument().Revision);
	}
}

void SEpicodeStudioPanel::RefreshSceneList()
{
	if (!SceneList.IsValid())
	{
		return;
	}
	SceneList->ClearChildren();
	const FEpicodeCommandResult Result = Execute(TEXT("scene.list"), MakeParams());
	if (!Result.bSuccess || !Result.Result.IsValid())
	{
		return;
	}
	const TArray<TSharedPtr<FJsonValue>> Scenes = EpicodeJson::GetOptionalArray(Result.Result, TEXT("scenes"));
	int32 SceneCount = 0;
	for (const TSharedPtr<FJsonValue>& Value : Scenes)
	{
		const TSharedPtr<FJsonObject> Entry = Value->AsObject();
		if (!Entry.IsValid())
		{
			continue;
		}
		const FString SceneId = JsonString(Entry, TEXT("sceneId"));
		const FString Name = JsonString(Entry, TEXT("name"), TEXT("Untitled"));
		const bool bCurrent = Entry->HasTypedField<EJson::Boolean>(TEXT("current")) && Entry->GetBoolField(TEXT("current"));
		const bool bAvailable = !Entry->HasField(TEXT("available")) || Entry->GetBoolField(TEXT("available"));
		const FString Modified = JsonString(Entry, TEXT("modifiedAt"));
		++SceneCount;
		AddRow(SceneList, SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s%s  %s"), bCurrent ? TEXT("* ") : TEXT(""), *Name, *Shorten(Modified, 20))))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Open"), [this, SceneId]()
				{
					TSharedPtr<FJsonObject> Params = MakeParams();
					Params->SetStringField(TEXT("sceneId"), SceneId);
					Execute(TEXT("scene.open"), Params);
					RefreshAll();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Delete"), [this, SceneId, bCurrent]()
				{
					if (bCurrent)
					{
						Log(TEXT("Cannot delete the open scene."));
						return;
					}
					TSharedPtr<FJsonObject> Params = MakeParams();
					Params->SetStringField(TEXT("sceneId"), SceneId);
					Execute(TEXT("scene.delete"), Params);
					RefreshAll();
				})
		]);
	}
	if (SceneCount == 0)
	{
		AddEmptyState(SceneList, TEXT("No saved scenes. Create one with New."));
	}
}

void SEpicodeStudioPanel::RefreshOutliner()
{
	if (!OutlinerList.IsValid())
	{
		return;
	}
	OutlinerList->ClearChildren();

	UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
	if (!Scene || !Scene->HasScene())
	{
		ViewModel.ClearSelection();
		AddEmptyState(OutlinerList, TEXT("No scene open. Create or open a scene first."));
		RefreshOutlinerInspector();
		return;
	}

	const FEpicodeCommandResult Result = Execute(TEXT("entity.list"), MakeParams());
	if (!Result.bSuccess || !Result.Result.IsValid())
	{
		AddEmptyState(OutlinerList, TEXT("Could not read the scene entities."));
		RefreshOutlinerInspector();
		return;
	}

	const TArray<TSharedPtr<FJsonValue>> Entities = EpicodeJson::GetOptionalArray(Result.Result, TEXT("entities"));
	TMap<FString, FString> ParentById;
	TSet<FString> AllIds;
	TSet<FString> HasChildren;
	for (const TSharedPtr<FJsonValue>& Value : Entities)
	{
		const TSharedPtr<FJsonObject> Entity = Value->AsObject();
		if (!Entity.IsValid())
		{
			continue;
		}
		const FString Id = JsonString(Entity, TEXT("id"));
		const FString ParentId = JsonString(Entity, TEXT("parentId"));
		ParentById.Add(Id, ParentId);
		AllIds.Add(Id);
		if (!ParentId.IsEmpty())
		{
			HasChildren.Add(ParentId);
		}
	}

	const FString Filter = OutlinerSearchInput.IsValid() ? OutlinerSearchInput->GetText().ToString().TrimStartAndEnd() : FString();
	auto DepthOf = [&ParentById](const FString& Id)
	{
		int32 Depth = 0;
		FString Cursor = ParentById.FindRef(Id);
		TSet<FString> Seen;
		while (!Cursor.IsEmpty() && Depth < 16 && !Seen.Contains(Cursor))
		{
			Seen.Add(Cursor);
			++Depth;
			Cursor = ParentById.FindRef(Cursor);
		}
		return Depth;
	};
	auto IsHiddenByCollapsed = [this, &ParentById](const FString& Id)
	{
		FString Cursor = ParentById.FindRef(Id);
		TSet<FString> Seen;
		while (!Cursor.IsEmpty() && !Seen.Contains(Cursor))
		{
			if (OutlinerCollapsedIds.Contains(Cursor))
			{
				return true;
			}
			Seen.Add(Cursor);
			Cursor = ParentById.FindRef(Cursor);
		}
		return false;
	};

	TArray<FString> VisibleIds;
	int32 Count = 0;
	for (const TSharedPtr<FJsonValue>& Value : Entities)
	{
		const TSharedPtr<FJsonObject> Entity = Value->AsObject();
		if (!Entity.IsValid())
		{
			continue;
		}
		const FString Id = JsonString(Entity, TEXT("id"));
		const FString Name = JsonString(Entity, TEXT("name"));
		const FString Type = JsonString(Entity, TEXT("type"));
		if (!Filter.IsEmpty() && !Name.Contains(Filter, ESearchCase::IgnoreCase) && !Type.Contains(Filter, ESearchCase::IgnoreCase) && !Id.Contains(Filter, ESearchCase::IgnoreCase))
		{
			continue;
		}
		if (IsHiddenByCollapsed(Id))
		{
			continue;
		}
		VisibleIds.Add(Id);
		++Count;

		const bool bVisible = !Entity->HasField(TEXT("visible")) || Entity->GetBoolField(TEXT("visible"));
		FString Indent;
		for (int32 Depth = 0; Depth < DepthOf(Id); ++Depth)
		{
			Indent += TEXT("  ");
		}
		AddRow(OutlinerList, SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 2.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this, Id]()
				{
					return ViewModel.IsSelected(Id) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, Id](ECheckBoxState State)
				{
					if (State == ECheckBoxState::Checked)
					{
						ViewModel.AddSelectedEntity(Id);
					}
					else
					{
						ViewModel.RemoveSelectedEntity(Id);
					}
					RefreshOutlinerInspector();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 2.0f, 0.0f)
			[
				MakeButton(HasChildren.Contains(Id) ? (OutlinerCollapsedIds.Contains(Id) ? TEXT("+") : TEXT("-")) : TEXT(" "), [this, Id, HasChildren]()
				{
					if (!HasChildren.Contains(Id))
					{
						return;
					}
					if (OutlinerCollapsedIds.Contains(Id))
					{
						OutlinerCollapsedIds.Remove(Id);
					}
					else
					{
						OutlinerCollapsedIds.Add(Id);
					}
					RefreshOutliner();
				})
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(SButton)
				.OnClicked_Lambda([this, Id]()
				{
					ViewModel.SelectOnly(Id);
					RefreshOutliner();
					return FReply::Handled();
				})
				.OnSlateButtonDragDetected_Lambda([Id](const FGeometry&, const FPointerEvent&)
				{
					const TSharedRef<FEpicodeOutlinerDragDropOperation> Operation = MakeShared<FEpicodeOutlinerDragDropOperation>();
					Operation->EntityId = Id;
					return FReply::Handled().BeginDragDrop(Operation);
				})
				.OnSlateButtonDrop_Lambda([this, Id](const FGeometry&, const FDragDropEvent& Event)
				{
					const TSharedPtr<FDragDropOperation> Operation = Event.GetOperation();
					if (!Operation.IsValid() || !Operation->IsOfType<FEpicodeOutlinerDragDropOperation>())
					{
						return FReply::Unhandled();
					}
					const FString DragId = StaticCastSharedPtr<FEpicodeOutlinerDragDropOperation>(Operation)->EntityId;
					if (DragId != Id)
					{
						TSharedPtr<FJsonObject> Params = MakeParams();
						Params->SetStringField(TEXT("id"), DragId);
						Params->SetStringField(TEXT("parentId"), Id);
						Params->SetBoolField(TEXT("keepWorldTransform"), true);
						Execute(TEXT("entity.reparent"), Params);
						RefreshAll();
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s%s  [%s]"), *Indent, *Name, *Type)))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(bVisible ? TEXT("Hide") : TEXT("Show"), [this, Id, bVisible]()
				{
					TSharedPtr<FJsonObject> Params = MakeParams();
					Params->SetStringField(TEXT("id"), Id);
					Params->SetBoolField(TEXT("visible"), !bVisible);
					Execute(TEXT("entity.setVisible"), Params);
					RefreshAll();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Duplicate"), [this, Id]()
				{
					TSharedPtr<FJsonObject> Params = MakeParams();
					Params->SetStringField(TEXT("id"), Id);
					Execute(TEXT("entity.duplicate"), Params);
					RefreshAll();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Delete"), [this, Id]()
				{
					TSharedPtr<FJsonObject> Params = MakeParams();
					Params->SetStringField(TEXT("id"), Id);
					Execute(TEXT("entity.delete"), Params);
					if (ViewModel.GetSelectedEntityId() == Id)
					{
						ViewModel.RemoveSelectedEntity(Id);
					}
					RefreshAll();
				})
			]);
	}

	if (Count == 0)
	{
		AddEmptyState(OutlinerList, Filter.IsEmpty() ? TEXT("No entities in this scene.") : TEXT("No entities match the filter."));
	}

	ViewModel.RemoveMissingSelection(AllIds);
	if (ViewModel.GetSelectedEntityId().IsEmpty() || !AllIds.Contains(ViewModel.GetSelectedEntityId()))
	{
		ViewModel.SetSelectedEntity(VisibleIds.Num() > 0 ? VisibleIds[0] : FString());
	}
	RefreshOutlinerInspector();
}

void SEpicodeStudioPanel::RefreshOutlinerInspector()
{
	if (!OutlinerInspector.IsValid())
	{
		return;
	}
	const bool bEditing =
		(OutlinerRenameInput.IsValid() && OutlinerRenameInput->HasKeyboardFocus())
		|| (OutlinerParentInput.IsValid() && OutlinerParentInput->HasKeyboardFocus())
		|| (OutlinerLocationInput.IsValid() && OutlinerLocationInput->HasKeyboardFocus())
		|| (OutlinerRotationInput.IsValid() && OutlinerRotationInput->HasKeyboardFocus())
		|| (OutlinerScaleInput.IsValid() && OutlinerScaleInput->HasKeyboardFocus())
		|| (OutlinerAnimationInput.IsValid() && OutlinerAnimationInput->HasKeyboardFocus())
		|| (OutlinerLightTypeInput.IsValid() && OutlinerLightTypeInput->HasKeyboardFocus())
		|| (OutlinerLightIntensityInput.IsValid() && OutlinerLightIntensityInput->HasKeyboardFocus())
		|| (OutlinerCameraFovInput.IsValid() && OutlinerCameraFovInput->HasKeyboardFocus())
		|| (OutlinerMaterialSlotInput.IsValid() && OutlinerMaterialSlotInput->HasKeyboardFocus());
	if (bEditing)
	{
		return;
	}
	OutlinerInspector->ClearChildren();

	UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
	if (!Scene || !Scene->HasScene() || ViewModel.GetSelectedEntityId().IsEmpty())
	{
		if (OutlinerSelectionText.IsValid())
		{
			OutlinerSelectionText->SetText(FText::FromString(TEXT("No entity selected")));
		}
		AddEmptyState(OutlinerInspector, TEXT("Select an entity to edit its name, parent, visibility and transform."));
		return;
	}

	TSharedPtr<FJsonObject> GetParams = MakeParams();
	GetParams->SetStringField(TEXT("id"), ViewModel.GetSelectedEntityId());
	const FEpicodeCommandResult Result = Execute(TEXT("entity.get"), GetParams);
	const TSharedPtr<FJsonObject>* EntityPtr = nullptr;
	if (!Result.bSuccess || !Result.Result.IsValid() || !Result.Result->TryGetObjectField(TEXT("entity"), EntityPtr) || !EntityPtr || !EntityPtr->IsValid())
	{
		if (OutlinerSelectionText.IsValid())
		{
			OutlinerSelectionText->SetText(FText::FromString(TEXT("Selection no longer exists")));
		}
		AddEmptyState(OutlinerInspector, TEXT("The selected entity was removed. Select another entity."));
		ViewModel.ClearSelection();
		return;
	}

	const TSharedPtr<FJsonObject>& Entity = *EntityPtr;
	const FString Name = JsonString(Entity, TEXT("name"));
	const FString Type = JsonString(Entity, TEXT("type"));
	const FString ParentId = JsonString(Entity, TEXT("parentId"));
	const bool bVisible = !Entity->HasField(TEXT("visible")) || Entity->GetBoolField(TEXT("visible"));
	FVector Location = FVector::ZeroVector;
	FVector Rotation = FVector::ZeroVector;
	FVector Scale = FVector::OneVector;
	const TSharedPtr<FJsonObject>* TransformPtr = nullptr;
	if (Entity->TryGetObjectField(TEXT("transform"), TransformPtr) && TransformPtr && TransformPtr->IsValid())
	{
		JsonVector3(*TransformPtr, TEXT("location"), Location);
		JsonVector3(*TransformPtr, TEXT("rotation"), Rotation);
		JsonVector3(*TransformPtr, TEXT("scale"), Scale);
	}

	if (OutlinerSelectionText.IsValid())
	{
		OutlinerSelectionText->SetText(FText::FromString(FString::Printf(TEXT("%s [%s] %s"), *Name, *Type, *ViewModel.GetSelectedEntityId())));
	}

	AddRow(OutlinerInspector, MakeInput(TEXT("Name"), OutlinerRenameInput, TEXT("Entity name"), Name));
	AddRow(OutlinerInspector, MakeInput(TEXT("Parent id"), OutlinerParentInput, TEXT("empty = root"), ParentId));
	AddRow(OutlinerInspector, MakeInput(TEXT("Location"), OutlinerLocationInput, TEXT("x, y, z"), FormatVector3(Location)));
	AddRow(OutlinerInspector, MakeInput(TEXT("Rotation"), OutlinerRotationInput, TEXT("pitch, yaw, roll"), FormatVector3(Rotation)));
	AddRow(OutlinerInspector, MakeInput(TEXT("Scale"), OutlinerScaleInput, TEXT("x, y, z"), FormatVector3(Scale)));

	const FString EntityId = ViewModel.GetSelectedEntityId();
	AddRow(OutlinerInspector, SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
		[
			MakeButton(TEXT("Rename"), [this, EntityId]()
			{
				TSharedPtr<FJsonObject> Params = MakeParams();
				Params->SetStringField(TEXT("id"), EntityId);
				Params->SetStringField(TEXT("name"), OutlinerRenameInput.IsValid() ? OutlinerRenameInput->GetText().ToString() : FString());
				Execute(TEXT("entity.rename"), Params);
				RefreshAll();
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
		[
			MakeButton(TEXT("Reparent"), [this, EntityId]()
			{
				TSharedPtr<FJsonObject> Params = MakeParams();
				Params->SetStringField(TEXT("id"), EntityId);
				const FString Parent = OutlinerParentInput.IsValid() ? OutlinerParentInput->GetText().ToString().TrimStartAndEnd() : FString();
				if (!Parent.IsEmpty())
				{
					Params->SetStringField(TEXT("parentId"), Parent);
				}
				Params->SetBoolField(TEXT("keepWorldTransform"), true);
				Execute(TEXT("entity.reparent"), Params);
				RefreshAll();
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
		[
			MakeButton(bVisible ? TEXT("Hide") : TEXT("Show"), [this, EntityId, bVisible]()
			{
				TSharedPtr<FJsonObject> Params = MakeParams();
				Params->SetStringField(TEXT("id"), EntityId);
				Params->SetBoolField(TEXT("visible"), !bVisible);
				Execute(TEXT("entity.setVisible"), Params);
				RefreshAll();
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
		[
			MakeButton(TEXT("Delete"), [this, EntityId]()
			{
				TSharedPtr<FJsonObject> Params = MakeParams();
				Params->SetStringField(TEXT("id"), EntityId);
				Execute(TEXT("entity.delete"), Params);
				ViewModel.ClearSelection();
				RefreshAll();
			})
		]);

	AddRow(OutlinerInspector, SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
		[
			MakeButton(TEXT("Apply transform"), [this, EntityId]()
			{
				FVector Location;
				FVector Rotation;
				FVector Scale;
				if (!ParseVector3(OutlinerLocationInput.IsValid() ? OutlinerLocationInput->GetText().ToString() : FString(), Location)
					|| !ParseVector3(OutlinerRotationInput.IsValid() ? OutlinerRotationInput->GetText().ToString() : FString(), Rotation)
					|| !ParseVector3(OutlinerScaleInput.IsValid() ? OutlinerScaleInput->GetText().ToString() : FString(), Scale))
				{
					Log(TEXT("Transform fields must contain three comma-separated numbers."));
					return;
				}
				auto SetVector = [](const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, const FVector& Value)
				{
					Object->SetArrayField(Field, {
						MakeShared<FJsonValueNumber>(Value.X),
						MakeShared<FJsonValueNumber>(Value.Y),
						MakeShared<FJsonValueNumber>(Value.Z)
					});
				};
				TSharedPtr<FJsonObject> Transform = MakeParams();
				SetVector(Transform, TEXT("location"), Location);
				SetVector(Transform, TEXT("rotation"), Rotation);
				SetVector(Transform, TEXT("scale"), Scale);
				TSharedPtr<FJsonObject> Params = MakeParams();
				Params->SetStringField(TEXT("id"), EntityId);
				Params->SetObjectField(TEXT("transform"), Transform);
				Execute(TEXT("entity.setTransform"), Params);
				RefreshAll();
			})
		]);

	const TSharedPtr<FJsonObject>* DataPtr = nullptr;
	TSharedPtr<FJsonObject> Data;
	if (Entity->TryGetObjectField(*Type, DataPtr) && DataPtr && DataPtr->IsValid())
	{
		Data = *DataPtr;
	}
	if (Type == TEXT("character") && Data.IsValid())
	{
		const FString Mesh = JsonString(Data, TEXT("skeletalMesh"));
		AddRow(OutlinerInspector, SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("Skeletal mesh: %s"), *Shorten(Mesh, 72)))));
		AddRow(OutlinerInspector, MakeInput(TEXT("Animation"), OutlinerAnimationInput, TEXT("/Game/Path/Anim_Idle"), JsonString(Data, TEXT("animation"))));
		AddRow(OutlinerInspector, MakeButton(TEXT("Play animation"), [this, EntityId]()
		{
			TSharedPtr<FJsonObject> Params = MakeParams();
			Params->SetStringField(TEXT("id"), EntityId);
			Params->SetStringField(TEXT("animation"), OutlinerAnimationInput.IsValid() ? OutlinerAnimationInput->GetText().ToString() : FString());
			Execute(TEXT("character.play"), Params);
			RefreshAll();
		}));
	}
	else if (Type == TEXT("prop") && Data.IsValid())
	{
		OutlinerMaterialPath = JsonString(Data, TEXT("material"));
		AddRow(OutlinerInspector, SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SBox).WidthOverride(110.0f)[SNew(STextBlock).Text(FText::FromString(TEXT("Material")))]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(UMaterialInterface::StaticClass())
				.AllowClear(true)
				.ObjectPath_Lambda([this]() { return OutlinerMaterialPath; })
				.OnObjectChanged_Lambda([this](const FAssetData& Asset)
				{
					OutlinerMaterialPath = Asset.IsValid() ? Asset.GetSoftObjectPath().ToString() : FString();
				})
			]);
		AddRow(OutlinerInspector, MakeInput(TEXT("Material slot"), OutlinerMaterialSlotInput, TEXT("0"), TEXT("0")));
		AddRow(OutlinerInspector, MakeButton(TEXT("Set material"), [this, EntityId]()
		{
			TSharedPtr<FJsonObject> Params = MakeParams();
			Params->SetStringField(TEXT("id"), EntityId);
			Params->SetStringField(TEXT("material"), OutlinerMaterialPath);
			Params->SetNumberField(TEXT("slot"), OutlinerMaterialSlotInput.IsValid() ? FCString::Atoi(*OutlinerMaterialSlotInput->GetText().ToString()) : 0);
			Execute(TEXT("prop.setMaterial"), Params);
			RefreshAll();
		}));
		const bool bSimulate = Data->HasField(TEXT("simulatePhysics")) && Data->GetBoolField(TEXT("simulatePhysics"));
		AddRow(OutlinerInspector, SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("Simulate physics")))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SCheckBox)
				.IsChecked(bSimulate ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
				.OnCheckStateChanged_Lambda([this, EntityId](ECheckBoxState State)
				{
					TSharedPtr<FJsonObject> Params = MakeParams();
					Params->SetStringField(TEXT("id"), EntityId);
					Params->SetBoolField(TEXT("simulate"), State == ECheckBoxState::Checked);
					Execute(TEXT("prop.setPhysics"), Params);
					RefreshAll();
				})
			]);
	}
	else if (Type == TEXT("light") && Data.IsValid())
	{
		const double Intensity = JsonNumber(Data, TEXT("intensity"), 1.0);
		AddRow(OutlinerInspector, MakeInput(TEXT("Light type"), OutlinerLightTypeInput, TEXT("directional | point | spot | rect"), JsonString(Data, TEXT("lightType"), TEXT("point"))));
		AddRow(OutlinerInspector, MakeInput(TEXT("Intensity"), OutlinerLightIntensityInput, TEXT("1.0"), FString::Printf(TEXT("%.2f"), Intensity)));
		AddRow(OutlinerInspector, MakeButton(TEXT("Update light"), [this, EntityId]()
		{
			TSharedPtr<FJsonObject> Params = MakeParams();
			Params->SetStringField(TEXT("id"), EntityId);
			Params->SetStringField(TEXT("lightType"), OutlinerLightTypeInput.IsValid() ? OutlinerLightTypeInput->GetText().ToString() : TEXT("point"));
			Params->SetNumberField(TEXT("intensity"), OutlinerLightIntensityInput.IsValid() ? FCString::Atof(*OutlinerLightIntensityInput->GetText().ToString()) : 1.0);
			Execute(TEXT("light.update"), Params);
			RefreshAll();
		}));
	}
	else if (Type == TEXT("camera") && Data.IsValid())
	{
		const double FieldOfView = JsonNumber(Data, TEXT("fieldOfView"), 35.0);
		AddRow(OutlinerInspector, MakeInput(TEXT("Field of view"), OutlinerCameraFovInput, TEXT("degrees"), FString::Printf(TEXT("%.2f"), FieldOfView)));
		AddRow(OutlinerInspector, MakeButton(TEXT("Update camera"), [this, EntityId]()
		{
			TSharedPtr<FJsonObject> Params = MakeParams();
			Params->SetStringField(TEXT("id"), EntityId);
			Params->SetNumberField(TEXT("fieldOfView"), OutlinerCameraFovInput.IsValid() ? FCString::Atof(*OutlinerCameraFovInput->GetText().ToString()) : 35.0);
			Execute(TEXT("camera.update"), Params);
			RefreshAll();
		}));
	}
}

void SEpicodeStudioPanel::RefreshCharacterList()
{
	if (!CharacterList.IsValid())
	{
		return;
	}
	CharacterList->ClearChildren();
	if (!UEpicodeSceneSubsystem::GetPrimary() || !UEpicodeSceneSubsystem::GetPrimary()->HasScene())
	{
		AddEmptyState(CharacterList, TEXT("No scene open. Create or open a scene first."));
		return;
	}
	const FEpicodeCommandResult Result = Execute(TEXT("entity.list"), MakeParams());
	if (!Result.bSuccess || !Result.Result.IsValid())
	{
		return;
	}
	const TArray<TSharedPtr<FJsonValue>> Entities = EpicodeJson::GetOptionalArray(Result.Result, TEXT("entities"));
	const FString Filter = CharacterFilterInput.IsValid() ? CharacterFilterInput->GetText().ToString().TrimStartAndEnd() : FString();
	int32 CharacterCount = 0;
	for (const TSharedPtr<FJsonValue>& Value : Entities)
	{
		const TSharedPtr<FJsonObject> Entity = Value->AsObject();
		if (!Entity.IsValid() || JsonString(Entity, TEXT("type")) != TEXT("character"))
		{
			continue;
		}
		const FString Id = JsonString(Entity, TEXT("id"));
		const FString Name = JsonString(Entity, TEXT("name"));
		const TSharedPtr<FJsonObject>* DataPtr = nullptr;
		FString Mesh;
		if ((Entity->TryGetObjectField(TEXT("data"), DataPtr) || Entity->TryGetObjectField(TEXT("character"), DataPtr)) && DataPtr)
		{
			Mesh = JsonString(*DataPtr, TEXT("skeletalMesh"));
		}
		if (!Filter.IsEmpty() && !Name.Contains(Filter, ESearchCase::IgnoreCase) && !Mesh.Contains(Filter, ESearchCase::IgnoreCase))
		{
			continue;
		}
		++CharacterCount;
		AddRow(CharacterList, SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s  [%s]"), *Name, *Shorten(Mesh))))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Play"), [this, Id]()
				{
					TSharedPtr<FJsonObject> Params = MakeParams();
					Params->SetStringField(TEXT("id"), Id);
					Params->SetStringField(TEXT("animation"), CharacterAnimationInput.IsValid() ? CharacterAnimationInput->GetText().ToString() : FString());
					Execute(TEXT("character.play"), Params);
					RefreshCharacterList();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Pause"), [this, Id]()
				{
					TSharedPtr<FJsonObject> Params = MakeParams();
					Params->SetStringField(TEXT("id"), Id);
					Params->SetBoolField(TEXT("pause"), true);
					Execute(TEXT("character.pause"), Params);
					RefreshCharacterList();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Delete"), [this, Id]()
				{
					TSharedPtr<FJsonObject> Params = MakeParams();
					Params->SetStringField(TEXT("id"), Id);
					Params->SetBoolField(TEXT("cascade"), true);
					Execute(TEXT("entity.delete"), Params);
					RefreshAll();
				})
		]);
	}
	if (CharacterCount == 0)
	{
		AddEmptyState(CharacterList, Filter.IsEmpty() ? TEXT("No characters in this scene. Spawn or import one.") : TEXT("No characters match the filter."));
	}
}

void SEpicodeStudioPanel::RefreshPropList()
{
	if (!PropList.IsValid())
	{
		return;
	}
	PropList->ClearChildren();
	if (!UEpicodeSceneSubsystem::GetPrimary() || !UEpicodeSceneSubsystem::GetPrimary()->HasScene())
	{
		AddEmptyState(PropList, TEXT("No scene open. Create or open a scene first."));
		return;
	}
	const FEpicodeCommandResult Result = Execute(TEXT("entity.list"), MakeParams());
	if (!Result.bSuccess || !Result.Result.IsValid())
	{
		return;
	}
	const TArray<TSharedPtr<FJsonValue>> Entities = EpicodeJson::GetOptionalArray(Result.Result, TEXT("entities"));
	const FString Filter = PropFilterInput.IsValid() ? PropFilterInput->GetText().ToString().TrimStartAndEnd() : FString();
	int32 PropCount = 0;
	for (const TSharedPtr<FJsonValue>& Value : Entities)
	{
		const TSharedPtr<FJsonObject> Entity = Value->AsObject();
		if (!Entity.IsValid() || JsonString(Entity, TEXT("type")) != TEXT("prop"))
		{
			continue;
		}
		const FString Id = JsonString(Entity, TEXT("id"));
		const FString Name = JsonString(Entity, TEXT("name"));
		const TSharedPtr<FJsonObject>* DataPtr = nullptr;
		FString Mesh;
		if ((Entity->TryGetObjectField(TEXT("data"), DataPtr) || Entity->TryGetObjectField(TEXT("prop"), DataPtr)) && DataPtr)
		{
			Mesh = JsonString(*DataPtr, TEXT("staticMesh"));
		}
		if (!Filter.IsEmpty() && !Name.Contains(Filter, ESearchCase::IgnoreCase) && !Mesh.Contains(Filter, ESearchCase::IgnoreCase))
		{
			continue;
		}
		++PropCount;
		AddRow(PropList, SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s  [%s]"), *Name, *Shorten(Mesh))))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Set material"), [this, Id]()
				{
					TSharedPtr<FJsonObject> Params = MakeParams();
					Params->SetStringField(TEXT("id"), Id);
					Params->SetStringField(TEXT("material"), PropMaterialPath);
					Params->SetNumberField(TEXT("slot"), MaterialSlotInput.IsValid() ? FCString::Atoi(*MaterialSlotInput->GetText().ToString()) : 0);
					Execute(TEXT("prop.setMaterial"), Params);
					RefreshPropList();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Delete"), [this, Id]()
				{
					TSharedPtr<FJsonObject> Params = MakeParams();
					Params->SetStringField(TEXT("id"), Id);
					Params->SetBoolField(TEXT("cascade"), true);
					Execute(TEXT("entity.delete"), Params);
					RefreshAll();
				})
		]);
	}
	if (PropCount == 0)
	{
		AddEmptyState(PropList, Filter.IsEmpty() ? TEXT("No props in this scene. Spawn or import one.") : TEXT("No props match the filter."));
	}
}

void SEpicodeStudioPanel::RefreshLightList()
{
	if (!LightList.IsValid())
	{
		return;
	}
	LightList->ClearChildren();
	if (!UEpicodeSceneSubsystem::GetPrimary() || !UEpicodeSceneSubsystem::GetPrimary()->HasScene())
	{
		AddEmptyState(LightList, TEXT("No scene open. Create or open a scene first."));
		return;
	}
	const FEpicodeCommandResult Result = Execute(TEXT("entity.list"), MakeParams());
	if (!Result.bSuccess || !Result.Result.IsValid())
	{
		return;
	}
	const TArray<TSharedPtr<FJsonValue>> Entities = EpicodeJson::GetOptionalArray(Result.Result, TEXT("entities"));
	const FString Filter = LightFilterInput.IsValid() ? LightFilterInput->GetText().ToString().TrimStartAndEnd() : FString();
	int32 LightCount = 0;
	for (const TSharedPtr<FJsonValue>& Value : Entities)
	{
		const TSharedPtr<FJsonObject> Entity = Value->AsObject();
		if (!Entity.IsValid() || JsonString(Entity, TEXT("type")) != TEXT("light"))
		{
			continue;
		}
		const FString Id = JsonString(Entity, TEXT("id"));
		const FString Name = JsonString(Entity, TEXT("name"));
		const TSharedPtr<FJsonObject>* DataPtr = nullptr;
		FString LightType;
		double Intensity = 0.0;
		if ((Entity->TryGetObjectField(TEXT("data"), DataPtr) || Entity->TryGetObjectField(TEXT("light"), DataPtr)) && DataPtr)
		{
			LightType = JsonString(*DataPtr, TEXT("lightType"));
			Intensity = JsonNumber(*DataPtr, TEXT("intensity"), 0.0);
		}
		if (!Filter.IsEmpty() && !Name.Contains(Filter, ESearchCase::IgnoreCase) && !LightType.Contains(Filter, ESearchCase::IgnoreCase))
		{
			continue;
		}
		++LightCount;
		AddRow(LightList, SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s  %s  %.2f"), *Name, *LightType, Intensity)))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Delete"), [this, Id]()
				{
					TSharedPtr<FJsonObject> Params = MakeParams();
					Params->SetStringField(TEXT("id"), Id);
					Execute(TEXT("light.delete"), Params);
					RefreshAll();
				})
		]);
	}
	if (LightCount == 0)
	{
		AddEmptyState(LightList, Filter.IsEmpty() ? TEXT("No lights in this scene. Create one.") : TEXT("No lights match the filter."));
	}
}

void SEpicodeStudioPanel::RefreshEnvironment()
{
	if (!EnvironmentSummary.IsValid())
	{
		return;
	}
	const FEpicodeCommandResult Result = Execute(TEXT("environment.get"), MakeParams());
	if (!Result.bSuccess || !Result.Result.IsValid())
	{
		EnvironmentSummary->SetText(FText::FromString(TEXT("Environment is available once a scene is open.")));
		return;
	}
	const TSharedPtr<FJsonObject>* EnvPtr = nullptr;
	TSharedPtr<FJsonObject> Env;
	if (Result.Result->TryGetObjectField(TEXT("environment"), EnvPtr) && EnvPtr)
	{
		Env = *EnvPtr;
	}
	if (!Env.IsValid())
	{
		return;
	}
	const double Time = JsonNumber(Env, TEXT("timeOfDay"), 15.0);
	const double Fog = JsonNumber(Env, TEXT("fogDensity"), 0.02);
	const double Cloud = JsonNumber(Env, TEXT("cloudCoverage"), 0.3);
	const double Exposure = JsonNumber(Env, TEXT("exposureCompensation"), 0.0);
	const double Bloom = JsonNumber(Env, TEXT("bloomIntensity"), 0.675);
	EnvironmentSummary->SetText(FText::FromString(FString::Printf(
		TEXT("preset=%s  time=%.2f  fog=%.3f  cloud=%.2f  exposure=%.2f  bloom=%.2f"),
		*JsonString(Env, TEXT("preset"), TEXT("custom")), Time, Fog, Cloud, Exposure, Bloom)));
	if (EnvTimeInput.IsValid()) EnvTimeInput->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), Time)));
	if (EnvFogInput.IsValid()) EnvFogInput->SetText(FText::FromString(FString::Printf(TEXT("%.3f"), Fog)));
	if (EnvCloudInput.IsValid()) EnvCloudInput->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), Cloud)));
	if (EnvExposureInput.IsValid()) EnvExposureInput->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), Exposure)));
	if (EnvBloomInput.IsValid()) EnvBloomInput->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), Bloom)));
}

void SEpicodeStudioPanel::RefreshImportList()
{
	if (!ImportList.IsValid())
	{
		return;
	}
	ImportList->ClearChildren();
	const FEpicodeCommandResult Result = Execute(TEXT("import.list"), MakeParams());
	if (!Result.bSuccess || !Result.Result.IsValid())
	{
		return;
	}
	const TArray<TSharedPtr<FJsonValue>> Jobs = EpicodeJson::GetOptionalArray(Result.Result, TEXT("jobs"));
	int32 ImportCount = 0;
	for (const TSharedPtr<FJsonValue>& Value : Jobs)
	{
		const TSharedPtr<FJsonObject> Job = Value->AsObject();
		if (!Job.IsValid())
		{
			continue;
		}
		const FString JobId = JsonString(Job, TEXT("jobId"));
		++ImportCount;
		const FString State = JsonString(Job, TEXT("state"));
		const double Progress = JsonNumber(Job, TEXT("progress"), 0.0);
		TArray<FString> Assets;
		FString ErrorMessage;
		TArray<FString> Warnings;
		const TSharedPtr<FJsonObject>* ResultObject = nullptr;
		if (Job->TryGetObjectField(TEXT("result"), ResultObject) && ResultObject && ResultObject->IsValid())
		{
			ErrorMessage = JsonString(*ResultObject, TEXT("errorMessage"));
			for (const TSharedPtr<FJsonValue>& AssetValue : EpicodeJson::GetOptionalArray(*ResultObject, TEXT("assets")))
			{
				if (AssetValue.IsValid() && AssetValue->Type == EJson::String)
				{
					Assets.Add(AssetValue->AsString());
				}
			}
			for (const TSharedPtr<FJsonValue>& WarningValue : EpicodeJson::GetOptionalArray(*ResultObject, TEXT("warnings")))
			{
				if (WarningValue.IsValid() && WarningValue->Type == EJson::String)
				{
					Warnings.Add(WarningValue->AsString());
				}
			}
		}
		const FString Detail = !ErrorMessage.IsEmpty() ? ErrorMessage : (Warnings.Num() > 0 ? Warnings[0] : FString());
		AddRow(ImportList, SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s  %s  %.0f%%"), *Shorten(JobId, 38), *State, Progress * 100.0)))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Shorten(Detail, 96)))
					.ColorAndOpacity(FLinearColor(1.0f, 0.55f, 0.35f))
					.Visibility(Detail.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Cancel"), [this, JobId]()
				{
					TSharedPtr<FJsonObject> Params = MakeParams();
					Params->SetStringField(TEXT("jobId"), JobId);
					Execute(TEXT("import.cancel"), Params);
					RefreshImportList();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Reveal"), [Assets]()
				{
					TArray<UObject*> Objects;
					for (const FString& AssetPath : Assets)
					{
						if (UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath))
						{
							Objects.Add(Asset);
						}
					}
					if (Objects.Num() > 0)
					{
						FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
						ContentBrowser.Get().SyncBrowserToAssets(Objects, false, true);
					}
				})
			]);
	}
	if (ImportCount == 0)
	{
		AddEmptyState(ImportList, TEXT("No import jobs. Choose files and submit an import."));
	}
}

void SEpicodeStudioPanel::RefreshRenderList()
{
	if (!RenderList.IsValid())
	{
		return;
	}
	RenderList->ClearChildren();
	const FEpicodeCommandResult Result = Execute(TEXT("render.list"), MakeParams());
	if (!Result.bSuccess || !Result.Result.IsValid())
	{
		return;
	}
	const TArray<TSharedPtr<FJsonValue>> Jobs = EpicodeJson::GetOptionalArray(Result.Result, TEXT("jobs"));
	int32 RenderCount = 0;
	for (const TSharedPtr<FJsonValue>& Value : Jobs)
	{
		const TSharedPtr<FJsonObject> Job = Value->AsObject();
		if (!Job.IsValid())
		{
			continue;
		}
		const FString JobId = JsonString(Job, TEXT("jobId"));
		++RenderCount;
		const FString State = JsonString(Job, TEXT("state"));
		const FString Stage = JsonString(Job, TEXT("stage"));
		const double Progress = JsonNumber(Job, TEXT("progress"), 0.0);
		const bool bCancellable = State == TEXT("queued") || State == TEXT("running");
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s  %s/%s  %.0f%%"), *Shorten(JobId, 34), *State, *Stage, Progress * 100.0)))
			];
		if (bCancellable)
		{
			Row->AddSlot().AutoWidth().Padding(2.0f)
			[
				MakeButton(TEXT("Cancel"), [this, JobId]()
				{
					TSharedPtr<FJsonObject> Params = MakeParams();
					Params->SetStringField(TEXT("jobId"), JobId);
					Execute(TEXT("render.cancel"), Params);
					RefreshRenderList();
				})
			];
		}
		AddRow(RenderList, Row);
	}
	if (RenderCount == 0)
	{
		AddEmptyState(RenderList, TEXT("No render jobs. Submit a three-frame PNG."));
	}
}

void SEpicodeStudioPanel::RefreshApiPanel()
{
	if (!ApiSummary.IsValid())
	{
		return;
	}
	const FEpicodeCommandResult Result = Execute(TEXT("system.health"), MakeParams());
	FString Text;
	if (Result.bSuccess && Result.Result.IsValid())
	{
		Text = FString::Printf(
			TEXT("status: %s\nrender backend: %s\ncommands: %.0f\nscene: %s\nmessages: %.0f"),
			*JsonString(Result.Result, TEXT("status"), TEXT("unknown")),
			*JsonString(Result.Result, TEXT("renderBackend"), TEXT("none")),
			JsonNumber(Result.Result, TEXT("commandCount"), 0.0),
			*JsonString(Result.Result, TEXT("sceneName"), TEXT("none")),
			static_cast<double>(EpicodeJson::GetOptionalArray(Result.Result, TEXT("messages")).Num()));
	}
	else
	{
		Text = FString::Printf(TEXT("health check failed: %s"), *Result.ErrorMessage);
	}
	if (const UEpicodeSettings* Settings = UEpicodeSettings::Get())
	{
		Text += FString::Printf(
			TEXT("\nHTTP: %s:%d\nWS: %s:%d\ntoken file: %s"),
			*Settings->ApiHost,
			Settings->ApiHttpPort,
			*Settings->ApiHost,
			Settings->ApiWsPort,
			*Settings->ApiTokenFile);
	}
	ApiSummary->SetText(FText::FromString(Text));
}

void SEpicodeStudioPanel::SpawnCharacter()
{
	TSharedPtr<FJsonObject> Params = MakeParams();
	Params->SetStringField(TEXT("skeletalMesh"), CharacterMeshPath);
	Params->SetStringField(TEXT("name"), CharacterNameInput.IsValid() ? CharacterNameInput->GetText().ToString() : TEXT("Character"));
	const FString Animation = CharacterAnimationInput.IsValid() ? CharacterAnimationInput->GetText().ToString() : FString();
	if (!Animation.IsEmpty())
	{
		Params->SetStringField(TEXT("animation"), Animation);
	}
	Execute(TEXT("character.spawn"), Params);
	RefreshCharacterList();
}

void SEpicodeStudioPanel::SpawnProp()
{
	TSharedPtr<FJsonObject> Params = MakeParams();
	Params->SetStringField(TEXT("staticMesh"), PropMeshPath);
	Params->SetStringField(TEXT("name"), PropNameInput.IsValid() ? PropNameInput->GetText().ToString() : TEXT("Prop"));
	Execute(TEXT("prop.spawn"), Params);
	RefreshPropList();
}

void SEpicodeStudioPanel::CreateLight()
{
	TSharedPtr<FJsonObject> Params = MakeParams();
	Params->SetStringField(TEXT("lightType"), LightTypeInput.IsValid() ? LightTypeInput->GetText().ToString() : TEXT("point"));
	Params->SetStringField(TEXT("name"), LightNameInput.IsValid() ? LightNameInput->GetText().ToString() : TEXT("Light"));
	Params->SetNumberField(TEXT("intensity"), LightIntensityInput.IsValid() ? FCString::Atof(*LightIntensityInput->GetText().ToString()) : 1.0);
	Execute(TEXT("light.create"), Params);
	RefreshLightList();
}

void SEpicodeStudioPanel::ApplyEnvironment()
{
	TSharedPtr<FJsonObject> Params = MakeParams();
	Params->SetNumberField(TEXT("timeOfDay"), EnvTimeInput.IsValid() ? FCString::Atof(*EnvTimeInput->GetText().ToString()) : 15.0);
	Params->SetNumberField(TEXT("fogDensity"), EnvFogInput.IsValid() ? FCString::Atof(*EnvFogInput->GetText().ToString()) : 0.02);
	Params->SetNumberField(TEXT("cloudCoverage"), EnvCloudInput.IsValid() ? FCString::Atof(*EnvCloudInput->GetText().ToString()) : 0.3);
	Params->SetNumberField(TEXT("exposureCompensation"), EnvExposureInput.IsValid() ? FCString::Atof(*EnvExposureInput->GetText().ToString()) : 0.0);
	Params->SetNumberField(TEXT("bloomIntensity"), EnvBloomInput.IsValid() ? FCString::Atof(*EnvBloomInput->GetText().ToString()) : 0.675);
	Params->SetStringField(TEXT("preset"), TEXT("custom"));
	Execute(TEXT("environment.set"), Params);
	RefreshEnvironment();
}
