#include "SEpicodeDirectorWorkspace.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

void SEpicodeDirectorWorkspace::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("Sandbox / Director Workspace")))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
		[
			BuildTabBar()
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
		[
			SAssignNew(HeaderText, STextBlock).Text(FText::FromString(TEXT("No production loaded")))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("Filter")))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SAssignNew(FilterInput, SEditableTextBox)
				.OnTextChanged_Lambda([this](const FText& InText)
				{
					Model.SetEventFilter(InText.ToString());
					Refresh();
				})
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(4.0f)
		[
			BuildBody()
		]
	];
	Refresh();
}

TSharedRef<SWidget> SEpicodeDirectorWorkspace::BuildTabBar()
{
	struct FTab
	{
		const TCHAR* Label;
		EEpicodeDirectorTab Tab;
	};
	const FTab Tabs[] = {
		{ TEXT("Overview"), EEpicodeDirectorTab::Overview },
		{ TEXT("Timeline"), EEpicodeDirectorTab::Timeline },
		{ TEXT("Beats"), EEpicodeDirectorTab::Beats },
		{ TEXT("Cues"), EEpicodeDirectorTab::Cues },
		{ TEXT("Controls"), EEpicodeDirectorTab::Controls }
	};
	TSharedRef<SHorizontalBox> Bar = SNew(SHorizontalBox);
	for (const FTab& Tab : Tabs)
	{
		const EEpicodeDirectorTab Captured = Tab.Tab;
		Bar->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(SButton)
			.Text(FText::FromString(Tab.Label))
			.OnClicked_Lambda([this, Captured]()
			{
				SelectTab(Captured);
				return FReply::Handled();
			})
		];
	}
	return Bar;
}

TSharedRef<SWidget> SEpicodeDirectorWorkspace::BuildBody()
{
	return SNew(SBox)
		[
			SAssignNew(BodyText, STextBlock)
			.AutoWrapText(true)
			.Text(FText::FromString(TEXT("Open a production to inspect sandbox state.")))
		];
}

void SEpicodeDirectorWorkspace::SelectTab(EEpicodeDirectorTab InTab)
{
	Model.SetActiveTab(InTab);
	Refresh();
}

void SEpicodeDirectorWorkspace::SetSandboxSnapshot(const TSharedPtr<FJsonObject>& InSnapshot)
{
	Model.ApplySandboxSnapshot(InSnapshot);
	Refresh();
}

void SEpicodeDirectorWorkspace::SetDirectorSnapshot(const TSharedPtr<FJsonObject>& InSnapshot)
{
	Model.ApplyDirectorSnapshot(InSnapshot);
	Refresh();
}

void SEpicodeDirectorWorkspace::SetRole(const FString& InRole)
{
	Model.SetRole(InRole);
	Refresh();
}

void SEpicodeDirectorWorkspace::Refresh()
{
	if (HeaderText.IsValid())
	{
		if (Model.HasProduction())
		{
			HeaderText->SetText(FText::FromString(FString::Printf(TEXT("%s | %s | %s"),
				*Model.GetProductionId(), *Model.GetCurrentBeatId(), *Model.GetMode())));
		}
		else
		{
			HeaderText->SetText(FText::FromString(TEXT("No production loaded")));
		}
	}
	if (!BodyText.IsValid())
	{
		return;
	}
	TArray<FString> Lines;
	if (!Model.GetLastError().IsEmpty())
	{
		Lines.Add(Model.GetLastError());
	}
	switch (Model.GetActiveTab())
	{
	case EEpicodeDirectorTab::Overview:
		Lines.Add(FString::Printf(TEXT("Sandbox: %s"), Model.IsSandboxActive() ? TEXT("running") : TEXT("idle")));
		Lines.Add(FString::Printf(TEXT("Production: %s"), *Model.GetProductionId()));
		for (const FString& Budget : Model.GetBudgetLines())
		{
			Lines.Add(Budget);
		}
		break;
	case EEpicodeDirectorTab::Timeline:
		for (const FEpicodeStudioEventRow& Event : Model.GetFilteredEvents())
		{
			Lines.Add(FString::Printf(TEXT("%lld  %s  %s"), static_cast<long long>(Event.Sequence), *Event.Type, *Event.ActorId));
		}
		break;
	case EEpicodeDirectorTab::Beats:
		Lines.Add(FString::Printf(TEXT("Beat: %s"), *Model.GetCurrentBeatId()));
		Lines.Add(FString::Printf(TEXT("Objective: %s"), *Model.GetCurrentObjectiveId()));
		break;
	case EEpicodeDirectorTab::Cues:
		for (const FString& Cue : Model.GetCueLines())
		{
			Lines.Add(Cue);
		}
		break;
	case EEpicodeDirectorTab::Controls:
		Lines.Add(FString::Printf(TEXT("Role: %s"), *Model.GetRole()));
		Lines.Add(FString::Printf(TEXT("Take: %s v%d"), *Model.GetSelectedTakeId(), Model.GetSelectedTakeVersion()));
		Lines.Add(FString::Printf(TEXT("Mode: %s"), *Model.GetMode()));
		Lines.Add(FString::Printf(TEXT("Frame budget: %.1f ms"), Model.GetFrameBudgetMs()));
		break;
	default:
		break;
	}
	BodyText->SetText(FText::FromString(FString::Join(Lines, TEXT("\n"))));
}
