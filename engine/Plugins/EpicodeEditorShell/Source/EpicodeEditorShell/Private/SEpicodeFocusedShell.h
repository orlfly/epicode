#pragma once

#include "CoreMinimal.h"
#include "UnrealWidgetFwd.h"
#include "Widgets/SCompoundWidget.h"

class AActor;
class ILevelEditor;
class SEpicodeFocusedLevelViewport;

class SEpicodeFocusedShell : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEpicodeFocusedShell)
		{}
		SLATE_ARGUMENT(TSharedPtr<ILevelEditor>, LevelEditor)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	bool IsViewportReady() const { return Viewport.IsValid(); }
	const FString& GetViewportError() const { return ViewportError; }
	void FocusViewport();
	void SelectFirstActorForSmokeTest();
	void SetWidgetMode(UE::Widget::EWidgetMode Mode);
	void MoveSelectedActorForUndoSmokeTest();

private:
	TSharedRef<SWidget> BuildMenuBar();
	TSharedRef<SWidget> BuildStatusBar();
	TSharedRef<SWidget> MakeMenuButton(const FString& Label);

	TSharedPtr<SEpicodeFocusedLevelViewport> Viewport;
	TWeakObjectPtr<AActor> SmokeActor;
	FString ViewportError;
};
