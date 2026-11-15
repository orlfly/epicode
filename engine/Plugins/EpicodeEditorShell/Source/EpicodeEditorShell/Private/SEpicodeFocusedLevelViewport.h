#pragma once

#include "CoreMinimal.h"
#include "SLevelViewport.h"

/**
 * A standalone level viewport used by the focused shell. It keeps the stock
 * level editor viewport client (selection, transform gizmos, undo) but removes
 * the generic level-editor toolbars from the visible surface.
 */
class SEpicodeFocusedLevelViewport : public SLevelViewport
{
public:
	// SLevelViewport normally decides visibility from a parent viewport layout.
	// The focused shell hosts it in a standalone window, so use the base
	// SEditorViewport visibility rule instead of returning false for no layout.
	virtual bool IsVisible() const override
	{
		return SEditorViewport::IsVisible();
	}

	virtual TSharedPtr<SWidget> BuildViewportToolbar() override
	{
		return TSharedPtr<SWidget>();
	}

	virtual EVisibility GetTransformToolbarVisibility() const override
	{
		return EVisibility::Collapsed;
	}
};
