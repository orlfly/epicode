#include "EpicodeFocusedShellModel.h"

#include "EpicodeEngineSubsystem.h"
#include "EpicodeSceneSubsystem.h"
#include "EpicodeTypes.h"

namespace
{
	FString TypeLabel(EEpicodeEntityType Type)
	{
		switch (Type)
		{
		case EEpicodeEntityType::Character:
			return TEXT("角色");
		case EEpicodeEntityType::Prop:
			return TEXT("物品");
		case EEpicodeEntityType::Light:
			return TEXT("灯光");
		case EEpicodeEntityType::Camera:
			return TEXT("摄像");
		default:
			return TEXT("分组");
		}
	}
}

UEpicodeSceneSubsystem* FEpicodeFocusedShellModel::GetSceneSubsystem() const
{
	return UEpicodeSceneSubsystem::GetPrimary();
}

void FEpicodeFocusedShellModel::Refresh()
{
	Entities.Reset();
	Scenes.Reset();
	bHasScene = false;
	bDirty = false;
	Revision = 0;
	SceneName.Reset();
	SceneId.Reset();

	UEpicodeSceneSubsystem* Scene = GetSceneSubsystem();
	if (!Scene)
	{
		SelectedEntityId.Invalidate();
		return;
	}

	for (const FEpicodeSceneLibraryEntry& Entry : Scene->ListScenes())
	{
		FEpicodeFocusedSceneRow Row;
		Row.Id = Entry.SceneId;
		Row.Name = Entry.Name;
		Row.bCurrent = Entry.bCurrent;
		Row.bAvailable = Entry.bAvailable;
		Scenes.Add(MoveTemp(Row));
	}

	if (!Scene->HasScene())
	{
		SelectedEntityId.Invalidate();
		return;
	}

	bHasScene = true;
	bDirty = Scene->IsDirty();
	Revision = Scene->GetDocument().Revision;
	SceneName = Scene->GetCurrentSceneName();
	SceneId = Scene->GetCurrentSceneId();

	for (const FEpicodeEntityRecord& Record : Scene->GetDocument().Entities)
	{
		FEpicodeFocusedEntityRow Row;
		Row.Id = Record.Id;
		Row.Name = Record.Name;
		Row.Type = Record.GetTypeName();
		Row.TypeLabel = TypeLabel(Record.Type);
		Row.bVisible = Record.bVisible;
		Entities.Add(MoveTemp(Row));
	}

	if (SelectedEntityId.IsValid() && !FindEntity(SelectedEntityId))
	{
		SelectedEntityId.Invalidate();
	}
}

FEpicodeCommandResult FEpicodeFocusedShellModel::Execute(
	const FString& Method, const TSharedPtr<FJsonObject>& Params)
{
	FEpicodeCommandResult Result;
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	if (!Engine || !Engine->GetCommandRegistry())
	{
		Result = FEpicodeCommandResult::Failure(
			TEXT("NOT_INITIALIZED"), TEXT("Epicode engine subsystem is not ready."));
	}
	else
	{
		FEpicodeCommandContext Context;
		Context.World = UEpicodeSceneSubsystem::GetPrimaryWorld();
		Context.Source = TEXT("focused");
		Result = Engine->ExecuteCommand(Method, Params, Context);
	}

	if (Result.bSuccess)
	{
		LastMessage = FString::Printf(TEXT("%s 完成"), *Method);
	}
	else
	{
		LastMessage = FString::Printf(
			TEXT("%s 失败：%s"), *Method,
			Result.ErrorMessage.IsEmpty() ? *Result.ErrorCode : *Result.ErrorMessage);
	}

	Refresh();
	return Result;
}

const FEpicodeFocusedEntityRow* FEpicodeFocusedShellModel::FindEntity(const FGuid& InId) const
{
	return Entities.FindByPredicate([&InId](const FEpicodeFocusedEntityRow& Row)
	{
		return Row.Id == InId;
	});
}

FString FEpicodeFocusedShellModel::GetSelectedEntityLabel() const
{
	const FEpicodeFocusedEntityRow* Row = FindEntity(SelectedEntityId);
	if (!Row)
	{
		return TEXT("无选择");
	}
	return FString::Printf(TEXT("%s · %s"), *Row->TypeLabel, *Row->Name);
}

FString FEpicodeFocusedShellModel::GetSignature() const
{
	return FString::Printf(
		TEXT("%s|%llu|%d|%d|%s|%d"),
		*SceneId,
		Revision,
		bHasScene ? 1 : 0,
		bDirty ? 1 : 0,
		*SelectedEntityId.ToString(EGuidFormats::Digits),
		Scenes.Num());
}
