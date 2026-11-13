#pragma once

#include "CoreMinimal.h"
#include "EpicodeStudioDirectorModel.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class STextBlock;

class EPICODESTUDIO_API SEpicodeDirectorWorkspace : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEpicodeDirectorWorkspace) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetSandboxSnapshot(const TSharedPtr<FJsonObject>& InSnapshot);
	void SetDirectorSnapshot(const TSharedPtr<FJsonObject>& InSnapshot);
	void SetRole(const FString& InRole);
	const FEpicodeStudioDirectorModel& GetModel() const { return Model; }
	FEpicodeStudioDirectorModel& GetMutableModel() { return Model; }

private:
	TSharedRef<SWidget> BuildTabBar();
	TSharedRef<SWidget> BuildBody();
	void SelectTab(EEpicodeDirectorTab InTab);
	void Refresh();

	FEpicodeStudioDirectorModel Model;
	TSharedPtr<STextBlock> BodyText;
	TSharedPtr<STextBlock> HeaderText;
	TSharedPtr<SEditableTextBox> FilterInput;
};
