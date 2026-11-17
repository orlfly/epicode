#include "EpicodeAssistantContext.h"

namespace
{
	FString MessageLine(const FConversationMessage& Message)
	{
		const TCHAR* Speaker = TEXT("用户");
		if (Message.Role == EConversationRole::Assistant)
		{
			Speaker = TEXT("助手");
		}
		else if (Message.Role == EConversationRole::System)
		{
			Speaker = TEXT("系统");
		}

		FString Content = Message.Content.TrimStartAndEnd();
		if (Content.Len() > 2000)
		{
			Content = Content.Left(2000) + TEXT("…");
		}
		return FString::Printf(TEXT("%s：%s"), Speaker, *Content);
	}
}

int32 FConversationContextBuilder::EstimateTokens(const FString& Text)
{
	// A conservative approximation for mixed Chinese/ASCII text. It is used only
	// for budgeting, never for billing or model limits.
	return FMath::Max(1, FMath::CeilToInt(static_cast<float>(Text.Len()) / 3.0f));
}

FString FConversationContextBuilder::Build(const FConversationContextInput& Input, int32 MaxChars)
{
	const int32 Budget = FMath::Max(1, MaxChars);
	FString Core;
	Core += TEXT("你是 Epicode 场景编辑助手。只根据给出的场景、选择、资产库和命令能力回答；")
		+ FString(TEXT("不确定时明确说明，不要编造不存在的对象、资产或命令。\n"));
	Core += FString::Printf(
		TEXT("场景：%s（%s）\n"),
		Input.SceneName.IsEmpty() ? TEXT("未打开") : *Input.SceneName,
		Input.SceneId.IsEmpty() ? TEXT("无 ID") : *Input.SceneId);
	Core += FString::Printf(TEXT("场景对象数：%d\n"), Input.EntityCount);
	Core += FString::Printf(
		TEXT("当前选择：%s\n"),
		Input.SelectionName.IsEmpty() ? TEXT("无") : *Input.SelectionName);
	if (!Input.SelectionType.IsEmpty())
	{
		Core += FString::Printf(TEXT("选择类型：%s\n"), *Input.SelectionType);
	}
	if (!Input.LibrarySummary.IsEmpty())
	{
		Core += FString::Printf(TEXT("本地资产库：%s\n"), *Input.LibrarySummary);
	}
	if (!Input.CommandSummary.IsEmpty())
	{
		Core += FString::Printf(TEXT("可用命令：%s\n"), *Input.CommandSummary);
	}
	Core += TEXT("回答使用简体中文，保持简洁；涉及操作时给出可执行的步骤。\n");

	const int32 CoreCap = FMath::Max(1, Budget - 96);
	if (Core.Len() > CoreCap)
	{
		Core = Core.Left(CoreCap) + TEXT("…\n");
	}

	TArray<FString> HistoryLines;
	int32 HistoryChars = 0;
	for (int32 Index = Input.History.Num() - 1; Index >= 0; --Index)
	{
		const FConversationMessage& Message = Input.History[Index];
		if (Message.Role == EConversationRole::System || Message.Content.TrimStartAndEnd().IsEmpty())
		{
			continue;
		}
		const FString Line = MessageLine(Message);
		const int32 Added = Line.Len() + 1;
		if (Core.Len() + HistoryChars + Added + 24 > Budget)
		{
			break;
		}
		HistoryLines.Insert(Line, 0);
		HistoryChars += Added;
	}

	FString Result = Core;
	if (HistoryLines.Num() > 0)
	{
		Result += TEXT("最近对话：\n");
		for (const FString& Line : HistoryLines)
		{
			Result += Line + TEXT("\n");
		}
	}
	return Result.Left(Budget);
}
