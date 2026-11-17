#include "EpicodeStudioModule.h"

#include "Editor.h"
#include "EpicodeCommand.h"
#include "EpicodeEngineSubsystem.h"
#include "EpicodeSceneSubsystem.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "SEpicodeStudioPanel.h"
#include "SEpicodeMultiplayerPanel.h"
#include "SEpicodeDirectorWorkspace.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

DEFINE_LOG_CATEGORY(LogEpicodeStudio);

namespace
{
	class FEpicodeEditorTransactionSink final : public IEpicodeTransactionSink
	{
	public:
		virtual void BeginTransaction(const FString& Description) override
		{
			if (GEditor && TransactionIndex == INDEX_NONE)
			{
				TransactionIndex = GEditor->BeginTransaction(FText::FromString(Description));
				if (UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary())
				{
					if (UEpicodeUndoRecord* Record = Scene->GetUndoRecord())
					{
						Record->Modify();
					}
				}
			}
		}

		virtual void CommitTransaction() override
		{
			if (GEditor && TransactionIndex != INDEX_NONE)
			{
				GEditor->EndTransaction();
				TransactionIndex = INDEX_NONE;
			}
		}

		virtual void CancelTransaction() override
		{
			if (GEditor && TransactionIndex != INDEX_NONE)
			{
				GEditor->CancelTransaction(TransactionIndex);
				TransactionIndex = INDEX_NONE;
				if (UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary())
				{
					Scene->SyncUndoSnapshot();
				}
			}
		}

		virtual void MarkModified(UObject* Object) override
		{
			if (Object)
			{
				Object->Modify();
			}
		}

	private:
		int32 TransactionIndex = INDEX_NONE;
	};
}

void FEpicodeStudioModule::StartupModule()
{
	const bool bFocusedEditor = FParse::Param(
		FCommandLine::Get(), TEXT("EpicodeFocusedEditor"));
	if (bFocusedEditor)
	{
		UE_LOG(LogEpicodeStudio, Log,
			TEXT("Focused editor mode: legacy Epicode tabs are not registered"));
	}
	else
	{
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		TEXT("EpicodeStudio"),
		FOnSpawnTab::CreateRaw(this, &FEpicodeStudioModule::SpawnStudioTab))
		.SetDisplayName(FText::FromString(TEXT("Epicode Studio")))
		.SetTooltipText(FText::FromString(TEXT("Scene, character, prop, light and environment managers.")))
		.SetMenuType(ETabSpawnerMenuType::Enabled)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("LevelEditor.Tabs.Viewports")));
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		TEXT("EpicodeMultiplayer"),
		FOnSpawnTab::CreateRaw(this, &FEpicodeStudioModule::SpawnMultiplayerTab))
		.SetDisplayName(FText::FromString(TEXT("Epicode Multiplayer")))
		.SetTooltipText(FText::FromString(TEXT("Session, players, performer HUD and director console.")))
		.SetMenuType(ETabSpawnerMenuType::Enabled)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("LevelEditor.Tabs.Viewports")));
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		TEXT("EpicodeDirectorWorkspace"),
		FOnSpawnTab::CreateRaw(this, &FEpicodeStudioModule::SpawnDirectorTab))
		.SetDisplayName(FText::FromString(TEXT("Epicode Director Workspace")))
		.SetTooltipText(FText::FromString(TEXT("Sandbox, director, timeline and replay workspace.")))
		.SetMenuType(ETabSpawnerMenuType::Enabled)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("LevelEditor.Tabs.Viewports")));

	}

	if (!bFocusedEditor)
	{
		if (UToolMenus* Menus = UToolMenus::Get())
		{
			FToolMenuOwnerScoped OwnerScoped(this);
			if (UToolMenu* WindowMenu = Menus->ExtendMenu(TEXT("LevelEditor.MainMenu.Window")))
			{
				FToolMenuSection& Section = WindowMenu->FindOrAddSection(TEXT("WindowLayout"));
				Section.AddMenuEntry(
					TEXT("EpicodeStudio"),
					FText::FromString(TEXT("Epicode Studio")),
					FText::FromString(TEXT("Open the Epicode production workspace.")),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("LevelEditor.Tabs.Viewports")),
					FUIAction(FExecuteAction::CreateLambda([]()
					{
						FGlobalTabmanager::Get()->TryInvokeTab(FTabId(TEXT("EpicodeStudio")));
					})));
				Section.AddMenuEntry(
					TEXT("EpicodeDirectorWorkspace"),
					FText::FromString(TEXT("Epicode Director Workspace")),
					FText::FromString(TEXT("Open sandbox, director, timeline and replay controls.")),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("LevelEditor.Tabs.Viewports")),
					FUIAction(FExecuteAction::CreateLambda([]()
					{
						FGlobalTabmanager::Get()->TryInvokeTab(FTabId(TEXT("EpicodeDirectorWorkspace")));
					})));
			}
			if (UToolMenu* ToolbarMenu = Menus->ExtendMenu(TEXT("LevelEditor.LevelEditorToolBar.User")))
			{
				FToolMenuSection& Section = ToolbarMenu->FindOrAddSection(TEXT("Epicode"));
				Section.AddEntry(FToolMenuEntry::InitToolBarButton(
					TEXT("EpicodeStudio"),
					FUIAction(FExecuteAction::CreateLambda([]()
					{
						FGlobalTabmanager::Get()->TryInvokeTab(FTabId(TEXT("EpicodeStudio")));
					})),
					FText::FromString(TEXT("Epicode Studio")),
					FText::FromString(TEXT("Open the Epicode production workspace.")),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("LevelEditor.Tabs.Viewports"))));
			}
			if (UToolMenu* WindowMenu = Menus->ExtendMenu(TEXT("LevelEditor.MainMenu.Window")))
			{
				FToolMenuSection& Section = WindowMenu->FindOrAddSection(TEXT("WindowLayout"));
				Section.AddMenuEntry(
					TEXT("EpicodeMultiplayer"),
					FText::FromString(TEXT("Epicode Multiplayer")),
					FText::FromString(TEXT("Open the Epicode multiplayer panel.")),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("LevelEditor.Tabs.Viewports")),
					FUIAction(FExecuteAction::CreateLambda([]()
					{
						FGlobalTabmanager::Get()->TryInvokeTab(FTabId(TEXT("EpicodeMultiplayer")));
					})));
			}
		}
	}
	UE_LOG(LogEpicodeStudio, Log, TEXT("EpicodeStudio module started"));

	TransactionSink = MakeShared<FEpicodeEditorTransactionSink>();
	PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FEpicodeStudioModule::RegisterTransactionSink);
	PostUndoRedoHandle = FEditorDelegates::PostUndoRedo.AddRaw(this, &FEpicodeStudioModule::HandlePostUndoRedo);
	RegisterTransactionSink();
}

void FEpicodeStudioModule::ShutdownModule()
{
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}
	if (PostUndoRedoHandle.IsValid())
	{
		FEditorDelegates::PostUndoRedo.Remove(PostUndoRedoHandle);
		PostUndoRedoHandle.Reset();
	}
	UnregisterTransactionSink();
	if (!FParse::Param(FCommandLine::Get(), TEXT("EpicodeFocusedEditor")))
	{
		UToolMenus::UnregisterOwner(this);
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TEXT("EpicodeStudio"));
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TEXT("EpicodeMultiplayer"));
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TEXT("EpicodeDirectorWorkspace"));
	}
	UE_LOG(LogEpicodeStudio, Log, TEXT("EpicodeStudio module stopped"));
}

void FEpicodeStudioModule::HandlePostUndoRedo()
{
	UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
	if (!Scene || !Scene->HasScene())
	{
		return;
	}
	UEpicodeUndoRecord* Record = Scene->GetUndoRecord();
	if (!Record)
	{
		return;
	}
	FString Error;
	if (!Scene->ApplyDocumentJson(Record->SceneJson, Error))
	{
		UE_LOG(LogEpicodeStudio, Warning, TEXT("Could not restore scene after undo/redo: %s"), *Error);
	}
}

void FEpicodeStudioModule::RegisterTransactionSink()
{
	if (UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get())
	{
		EngineSubsystem->SetTransactionSink(TransactionSink.Get());
	}
}

void FEpicodeStudioModule::UnregisterTransactionSink()
{
	if (UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get())
	{
		if (EngineSubsystem->GetTransactionSink() == TransactionSink.Get())
		{
			EngineSubsystem->SetTransactionSink(nullptr);
		}
	}
	TransactionSink.Reset();
}

TSharedRef<SDockTab> FEpicodeStudioModule::SpawnStudioTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SEpicodeStudioPanel)
		];
}

TSharedRef<SDockTab> FEpicodeStudioModule::SpawnMultiplayerTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SEpicodeMultiplayerPanel)
		];
}

TSharedRef<SDockTab> FEpicodeStudioModule::SpawnDirectorTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SEpicodeDirectorWorkspace)
		];
}

IMPLEMENT_MODULE(FEpicodeStudioModule, EpicodeStudio);
