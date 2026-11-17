#include "Misc/AutomationTest.h"

#include "EpicodeAssistantContext.h"
#include "EpicodeAssistantHistory.h"
#include "EpicodeAssistantSecrets.h"
#include "EpicodeAssistantTypes.h"
#include "EpicodeSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	struct FScopedAssistantTestRoot
	{
		FScopedAssistantTestRoot()
			: Root(FPaths::Combine(
				FPaths::ProjectSavedDir(),
				TEXT("Epicode/Tests/AssistantChat"),
				FGuid::NewGuid().ToString(EGuidFormats::Digits)))
			, Override(Root)
		{
			IFileManager::Get().DeleteDirectory(*Root, false, true);
			IFileManager::Get().MakeDirectory(*Root, true);
		}

		~FScopedAssistantTestRoot()
		{
			IFileManager::Get().DeleteDirectory(*Root, false, true);
		}

		FString Root;
		FConversationHistoryStore::FScopedRootOverride Override;
	};

	FConversationMessage MakeMessage(
		EConversationRole Role, const FString& Content, const FString& Id)
	{
		FConversationMessage Message;
		Message.Id = Id;
		Message.Role = Role;
		Message.Content = Content;
		return Message;
	}

	void DrainMockStream(
		const TSharedPtr<IConversationStream>& Stream,
		TArray<FConversationEvent>& OutEvents,
		int32 MaxPolls = 16)
	{
		for (int32 Poll = 0; Stream.IsValid() && !Stream->IsDone() && Poll < MaxPolls; ++Poll)
		{
			Stream->Poll(OutEvents);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEpicodeAssistantHistoryTest,
	"Epicode.Assistant.History",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeAssistantHistoryTest::RunTest(const FString& Parameters)
{
	FScopedAssistantTestRoot Guard;
	const FString ConversationId = FConversationHistoryStore::NewConversationId();
	TestTrue(TEXT("id is filesystem safe"),
		FConversationHistoryStore::SanitizeId(ConversationId) == ConversationId);

	TArray<FConversationMessage> Messages;
	Messages.Add(MakeMessage(EConversationRole::User, TEXT("第一问"), TEXT("m1")));
	Messages.Add(MakeMessage(EConversationRole::Assistant, TEXT("第一答"), TEXT("m2")));
	Messages.Add(MakeMessage(EConversationRole::System, TEXT("系统提示不落盘"), TEXT("m3")));

	FString Error;
	TestTrue(TEXT("history saved"), FConversationHistoryStore::Save(ConversationId, Messages, Error));
	TestTrue(TEXT("save has no error"), Error.IsEmpty());

	TArray<FConversationMessage> Loaded;
	TestTrue(TEXT("history loaded"), FConversationHistoryStore::Load(ConversationId, Loaded, Error));
	TestEqual(TEXT("system lines filtered"), Loaded.Num(), 2);
	TestEqual(TEXT("first role"), Loaded[0].Role, EConversationRole::User);
	TestEqual(TEXT("first content"), Loaded[0].Content, FString(TEXT("第一问")));
	TestEqual(TEXT("second role"), Loaded[1].Role, EConversationRole::Assistant);

	FString ListError;
	const TArray<FString> Ids = FConversationHistoryStore::ListIds(ListError);
	TestTrue(TEXT("conversation listed"), Ids.Contains(ConversationId));

	// Malformed JSON must fail without touching other conversations.
	const FString BadPath = FPaths::Combine(Guard.Root, TEXT("chat-bad.json"));
	FFileHelper::SaveStringToFile(TEXT("{ not json"), *BadPath);
	TArray<FConversationMessage> BadMessages;
	TestFalse(TEXT("malformed history rejected"),
		FConversationHistoryStore::Load(TEXT("chat-bad"), BadMessages, Error));

	TestTrue(TEXT("history deleted"), FConversationHistoryStore::Delete(ConversationId, Error));
	TestFalse(TEXT("deleted history cannot load"),
		FConversationHistoryStore::Load(ConversationId, Loaded, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEpicodeAssistantContextTest,
	"Epicode.Assistant.ContextBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeAssistantContextTest::RunTest(const FString& Parameters)
{
	FConversationContextInput Input;
	Input.SceneName = TEXT("上下文测试场景");
	Input.SceneId = TEXT("scene-ctx");
	Input.EntityCount = 7;
	Input.SelectionName = TEXT("主灯");
	Input.SelectionType = TEXT("Light");
	Input.LibrarySummary = TEXT("3 个资产：Cube、Sphere、Chair");
	Input.CommandSummary = TEXT("scene.new、entity.create、entity.setTransform");

	for (int32 Index = 0; Index < 200; ++Index)
	{
		Input.History.Add(MakeMessage(
			Index % 2 == 0 ? EConversationRole::User : EConversationRole::Assistant,
			FString::Printf(TEXT("历史消息 %d：%s"), Index, *FString::ChrN(300, TEXT('长'))),
			FString::Printf(TEXT("h%d"), Index)));
	}

	const FString Context = FConversationContextBuilder::Build(Input, 1200);
	TestTrue(TEXT("context respects character budget"), Context.Len() <= 1200);
	TestTrue(TEXT("context keeps scene facts"), Context.Contains(TEXT("上下文测试场景")));
	TestTrue(TEXT("context keeps selection"), Context.Contains(TEXT("主灯")));
	TestTrue(TEXT("context keeps commands"), Context.Contains(TEXT("entity.setTransform")));
	TestEqual(TEXT("token estimate is positive"),
		FConversationContextBuilder::EstimateTokens(TEXT("你好")) > 0, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEpicodeAssistantSecretTest,
	"Epicode.Assistant.Secrets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeAssistantSecretTest::RunTest(const FString& Parameters)
{
	FScopedAssistantTestRoot Guard;
	TestEqual(TEXT("mask hides short secrets"),
		FConversationSecretStore::Mask(TEXT("short")), FString(TEXT("********")));
	TestEqual(TEXT("mask keeps only edges"),
		FConversationSecretStore::Mask(TEXT("sk-1234567890")), FString(TEXT("sk-1…7890")));
	TestEqual(TEXT("empty mask"),
		FConversationSecretStore::Mask(FString()), FString(TEXT("未设置")));

	const FString Variable = FConversationSecretStore::GetEnvironmentVariableName();
	TestEqual(TEXT("environment variable name"), Variable, FString(TEXT("DEEPSEEK_API_KEY")));

	const FString OldValue = FPlatformMisc::GetEnvironmentVariable(*Variable);
	const bool bHadValue = !OldValue.IsEmpty();
	FPlatformMisc::SetEnvironmentVar(*Variable, TEXT("test-key-1234567890"));
	FString Key;
	FString Source;
	FString Error;
	TestTrue(TEXT("environment key read"), FConversationSecretStore::Read(Key, Source, Error));
	TestEqual(TEXT("environment source"), Source, FString(TEXT("environment")));
	TestEqual(TEXT("key preserved"), Key, FString(TEXT("test-key-1234567890")));
	TestFalse(TEXT("masked key does not contain full secret"),
		FConversationSecretStore::Mask(Key).Contains(Key));

	FPlatformMisc::SetEnvironmentVar(*Variable, TEXT(""));
	UEpicodeSettings* Settings = GetMutableDefault<UEpicodeSettings>();
	const FString OldKeyFile = Settings->AssistantApiKeyFile;
	Settings->AssistantApiKeyFile = FPaths::Combine(
		Guard.Root, TEXT("keys/deepseek.env"));
	FString TemplatePath;
	TestTrue(TEXT("key template created"),
		FConversationSecretStore::EnsureTemplateFile(TemplatePath, Error));
	TestTrue(TEXT("key template exists"), IFileManager::Get().FileExists(*TemplatePath));
	TestTrue(TEXT("key file written"),
		FFileHelper::SaveStringToFile(
			TEXT("DEEPSEEK_API_KEY=file-key-1234567890\n"), *TemplatePath));
	TestTrue(TEXT("file key read"), FConversationSecretStore::Read(Key, Source, Error));
	TestEqual(TEXT("file source"), Source, FString(TEXT("file")));
	TestEqual(TEXT("file key preserved"), Key, FString(TEXT("file-key-1234567890")));
	Settings->AssistantApiKeyFile = OldKeyFile;

	if (bHadValue)
	{
		FPlatformMisc::SetEnvironmentVar(*Variable, *OldValue);
	}
	else
	{
		FPlatformMisc::SetEnvironmentVar(*Variable, TEXT(""));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEpicodeAssistantMockBackendTest,
	"Epicode.Assistant.MockBackend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeAssistantMockBackendTest::RunTest(const FString& Parameters)
{
	FMockConversationBackend Backend;
	Backend.ResponseChunks = { TEXT("你好"), TEXT("，世界") };
	FString Error;
	FConversationRequest Request;
	Request.Prompt = TEXT("hi");
	TSharedPtr<IConversationStream> Stream = Backend.Start(Request, Error);
	TestTrue(TEXT("stream started"), Stream.IsValid());
	if (!Stream.IsValid())
	{
		return false;
	}

	TArray<FConversationEvent> Events;
	DrainMockStream(Stream, Events);
	TestEqual(TEXT("three events"), Events.Num(), 3);
	TestEqual(TEXT("first is delta"), Events[0].Type, EConversationEventType::Delta);
	TestEqual(TEXT("assembled text"),
		Events[0].Text + Events[1].Text, FString(TEXT("你好，世界")));
	TestEqual(TEXT("terminal done"), Events[2].Type, EConversationEventType::Done);

	FMockConversationBackend Failing;
	Failing.ResponseChunks = { TEXT("部分") };
	Failing.bErrorAfterChunks = true;
	Failing.StreamErrorCode = TEXT("test_error");
	Failing.StreamErrorMessage = TEXT("模拟失败");
	Stream = Failing.Start(FConversationRequest(), Error);
	Events.Reset();
	DrainMockStream(Stream, Events);
	TestTrue(TEXT("terminal error emitted"),
		Events.Num() > 0 && Events.Last().Type == EConversationEventType::Error);
	TestEqual(TEXT("error code preserved"), Events.Last().ErrorCode, FString(TEXT("test_error")));

	FMockConversationBackend Timeout;
	Timeout.ResponseChunks = { TEXT("等待") };
	Timeout.bTimeout = true;
	Stream = Timeout.Start(FConversationRequest(), Error);
	Events.Reset();
	DrainMockStream(Stream, Events);
	TestTrue(TEXT("timeout terminal"),
		Events.Num() > 0 && Events.Last().Type == EConversationEventType::Error);
	TestEqual(TEXT("timeout code"), Events.Last().ErrorCode, FString(TEXT("timeout")));

	FMockConversationBackend Crash;
	Crash.ResponseChunks = { TEXT("崩溃前") };
	Crash.bCrash = true;
	Stream = Crash.Start(FConversationRequest(), Error);
	Events.Reset();
	DrainMockStream(Stream, Events);
	TestTrue(TEXT("crash terminal"),
		Events.Num() > 0 && Events.Last().Type == EConversationEventType::Error);
	TestEqual(TEXT("crash code"), Events.Last().ErrorCode, FString(TEXT("backend_crashed")));

	FMockConversationBackend Cancelled;
	Cancelled.ResponseChunks = { TEXT("不会到达") };
	Stream = Cancelled.Start(FConversationRequest(), Error);
	Stream->Cancel();
	Events.Reset();
	Stream->Poll(Events);
	TestTrue(TEXT("cancel emits cancelled"),
		Events.Num() == 1 && Events[0].Type == EConversationEventType::Cancelled);
	TestTrue(TEXT("cancelled stream done"), Stream->IsDone());

	FMockConversationBackend SendFailure;
	SendFailure.SendError = TEXT("网络不可用");
	TestFalse(TEXT("unavailable backend"), SendFailure.IsAvailable(Error));
	TestTrue(TEXT("start returns null"), !SendFailure.Start(FConversationRequest(), Error));
	TestEqual(TEXT("start error"), Error, FString(TEXT("网络不可用")));

	FUnavailableConversationBackend Unavailable(TEXT("未配置"));
	TestFalse(TEXT("unavailable reports reason"), Unavailable.IsAvailable(Error));
	TestEqual(TEXT("unavailable reason"), Error, FString(TEXT("未配置")));
	return true;
}

#endif
