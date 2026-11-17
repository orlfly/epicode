#include "Misc/AutomationTest.h"

#include "EpicodeAssistantContext.h"
#include "EpicodeAssistantHistory.h"
#include "EpicodeAssistantOpenAI.h"
#include "EpicodeAssistantSidecar.h"
#include "EpicodeAssistantSecrets.h"
#include "EpicodeAssistantTypes.h"
#include "EpicodeSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
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

	FString DrainSidecarStream(
		const TSharedPtr<IConversationStream>& Stream,
		bool& bOutDone,
		FString& OutErrorCode,
		FString& OutErrorMessage,
		int32 MaxPolls = 600)
	{
		FString Text;
		bOutDone = false;
		for (int32 Poll = 0; Stream.IsValid() && Poll < MaxPolls; ++Poll)
		{
			TArray<FConversationEvent> Events;
			Stream->Poll(Events);
			for (const FConversationEvent& Event : Events)
			{
				if (Event.Type == EConversationEventType::Delta)
				{
					Text += Event.Text;
				}
				else if (Event.Type == EConversationEventType::Done)
				{
					bOutDone = true;
				}
				else if (Event.Type == EConversationEventType::Error
					|| Event.Type == EConversationEventType::Cancelled)
				{
					OutErrorCode = Event.ErrorCode;
					OutErrorMessage = Event.ErrorMessage;
					bOutDone = true;
				}
			}
			if (bOutDone || (Stream.IsValid() && Stream->IsDone()))
			{
				break;
			}
			FPlatformProcess::Sleep(0.01f);
		}
		return Text;
	}

	struct FScopedSidecarTestRoot
	{
		FScopedSidecarTestRoot()
			: Root(FPaths::Combine(
				FPaths::ProjectSavedDir(),
				TEXT("Epicode/Tests/Sidecar"),
				FGuid::NewGuid().ToString(EGuidFormats::Digits)))
			, KeyFile(FPaths::Combine(Root, TEXT("deepseek.env")))
		{
			IFileManager::Get().DeleteDirectory(*Root, false, true);
			IFileManager::Get().MakeDirectory(*Root, true);
			FFileHelper::SaveStringToFile(
				TEXT("DEEPSEEK_API_KEY=test-key-1234567890\n"), *KeyFile);
		}

		~FScopedSidecarTestRoot()
		{
			IFileManager::Get().DeleteDirectory(*Root, false, true);
		}

		FSidecarBackendConfig MakeConfig() const
		{
			FSidecarBackendConfig Config;
			Config.Command = TEXT("python3");
			Config.Script = FPaths::ConvertRelativePathToFull(FPaths::Combine(
				FPaths::ProjectDir(), TEXT("Scripts/Assistant/TestSidecar.py")));
			Config.ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
			Config.ApiKeyFile = KeyFile;
			Config.VenvDir = FPaths::Combine(Root, TEXT("venv"));
			Config.Model = TEXT("test-model");
			Config.BaseUrl = FString();
			Config.RequestTimeoutSeconds = 5.0f;
			return Config;
		}

		FString Root;
		FString KeyFile;
	};
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEpicodeAssistantHttpParserTest,
	"Epicode.Assistant.HttpParser",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeAssistantHttpParserTest::RunTest(const FString& Parameters)
{
	FOpenAICompatibleSseParser Parser;
	TArray<FConversationEvent> Events;

	// Split inside the UTF-8 bytes of Chinese text and inside the SSE line.
	const FString First =
		TEXT("data: {\"choices\":[{\"delta\":{\"content\":\"你好\"}}]}\n");
	const FTCHARToUTF8 FirstUtf8(*First);
	const int32 Split = FMath::Max(1, FirstUtf8.Length() / 2);
	Parser.Append(FirstUtf8.Get(), Split, TEXT("req-1"));
	Parser.TakeEvents(Events);
	TestEqual(TEXT("partial chunk emits nothing"), Events.Num(), 0);
	Parser.Append(
		FirstUtf8.Get() + Split,
		FirstUtf8.Length() - Split,
		TEXT("req-1"));
	Parser.TakeEvents(Events);
	TestEqual(TEXT("delta decoded"), Events.Num(), 1);
	TestEqual(TEXT("delta text"), Events[0].Text, FString(TEXT("你好")));
	TestEqual(TEXT("delta request id"), Events[0].RequestId, FString(TEXT("req-1")));

	Parser.Append(
		TEXT("data: {\"choices\":[{\"delta\":{\"content\":\"，世界\"}}]}\r\n"),
		TEXT("req-1"));
	Parser.Append(TEXT("data: [DONE]\n"), TEXT("req-1"));
	Events.Reset();
	Parser.TakeEvents(Events);
	TestEqual(TEXT("second delta"), Events.Num(), 1);
	TestTrue(TEXT("parser done"), Parser.IsDone());
	TestTrue(TEXT("parser saw output"), Parser.HasOutput());

	FOpenAICompatibleSseParser ErrorParser;
	ErrorParser.Append(
		TEXT("data: {\"error\":{\"code\":\"invalid_key\",\"message\":\"密钥无效\"}}\n"),
		TEXT("req-2"));
	Events.Reset();
	ErrorParser.TakeEvents(Events);
	TestEqual(TEXT("error event"), Events.Num(), 1);
	TestEqual(TEXT("error type"), Events[0].Type, EConversationEventType::Error);
	TestEqual(TEXT("error code"), Events[0].ErrorCode, FString(TEXT("invalid_key")));
	TestTrue(TEXT("error terminal"), ErrorParser.IsDone());

	TestEqual(TEXT("chat url"),
		FOpenAICompatibleChatBackend::MakeChatUrl(TEXT("https://api.deepseek.com/")),
		FString(TEXT("https://api.deepseek.com/chat/completions")));
	FOpenAICompatibleChatBackend MissingKey(TEXT("https://api.deepseek.com"), TEXT("deepseek-chat"), FString());
	FString Reason;
	TestFalse(TEXT("missing key unavailable"), MissingKey.IsAvailable(Reason));
	TestTrue(TEXT("missing key reason"), Reason.Contains(TEXT("DEEPSEEK_API_KEY")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEpicodeAssistantHttpBackendTest,
	"Epicode.Assistant.HttpBackend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeAssistantHttpBackendTest::RunTest(const FString& Parameters)
{
	const FString Script = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectDir(), TEXT("Scripts/Assistant/TestSseServer.py")));
	if (!IFileManager::Get().FileExists(*Script))
	{
		AddWarning(TEXT("local SSE fixture is missing; backend integration skipped"));
		return true;
	}

	const int32 Port = 18721;
	FProcHandle Server = FPlatformProcess::CreateProc(
		TEXT("python3"),
		*FString::Printf(TEXT("\"%s\" %d"), *Script, Port),
		false,
		true,
		true,
		nullptr,
		0,
		*FPaths::GetPath(Script),
		nullptr,
		nullptr);
	if (!Server.IsValid())
	{
		AddWarning(TEXT("python3 unavailable; HTTP backend integration skipped"));
		return true;
	}

	FPlatformProcess::Sleep(0.5f);
	FOpenAICompatibleChatBackend Backend(
		FString::Printf(TEXT("http://127.0.0.1:%d"), Port),
		TEXT("test-model"),
		TEXT("test-key-1234567890"));
	FString Error;
	TestTrue(TEXT("local backend available"), Backend.IsAvailable(Error));

	FConversationRequest Request;
	Request.RequestId = TEXT("http-test-request");
	Request.Prompt = TEXT("你好");
	TSharedPtr<IConversationStream> Stream = Backend.Start(Request, Error);
	TestTrue(TEXT("HTTP stream started"), Stream.IsValid());

	FString Text;
	bool bDone = false;
	for (int32 Attempt = 0; Stream.IsValid() && Attempt < 200 && !bDone; ++Attempt)
	{
		FPlatformProcess::Sleep(0.01f);
		TArray<FConversationEvent> Events;
		Stream->Poll(Events);
		for (const FConversationEvent& Event : Events)
		{
			if (Event.Type == EConversationEventType::Delta)
			{
				Text += Event.Text;
			}
			else if (Event.Type == EConversationEventType::Done)
			{
				bDone = true;
			}
			else if (Event.Type == EConversationEventType::Error)
			{
				AddError(FString::Printf(TEXT("HTTP backend error: %s"), *Event.ErrorMessage));
				bDone = true;
			}
		}
	}
	TestEqual(TEXT("SSE text assembled"), Text, FString(TEXT("你好")));
	TestTrue(TEXT("HTTP stream completed"), bDone);

	if (Stream.IsValid())
	{
		Stream->Cancel();
		Stream.Reset();
	}
	FPlatformProcess::TerminateProc(Server, true);
	FPlatformProcess::WaitForProc(Server);
	FPlatformProcess::CloseProc(Server);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEpicodeAssistantSidecarProtocolTest,
	"Epicode.Assistant.SidecarProtocol",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeAssistantSidecarProtocolTest::RunTest(const FString& Parameters)
{
	FSidecarJsonLineParser Parser;
	TArray<FConversationEvent> Events;

	const FString First =
		TEXT("{\"id\":\"1\",\"event\":\"delta\",\"text\":\"你好\"}\n");
	const FTCHARToUTF8 FirstUtf8(*First);
	const int32 Split = FMath::Max(1, FirstUtf8.Length() / 2);
	Parser.Append(FirstUtf8.Get(), Split);
	Parser.TakeEvents(TEXT("req-1"), Events);
	TestEqual(TEXT("partial line emits nothing"), Events.Num(), 0);
	Parser.Append(FirstUtf8.Get() + Split, FirstUtf8.Length() - Split);
	Parser.TakeEvents(TEXT("req-1"), Events);
	TestEqual(TEXT("delta decoded"), Events.Num(), 1);
	TestEqual(TEXT("delta text"), Events[0].Text, FString(TEXT("你好")));
	TestEqual(TEXT("delta request id"), Events[0].RequestId, FString(TEXT("req-1")));

	Parser.AppendLine(TEXT("{\"event\":\"delta\",\"text\":\"，世界\"}"));
	Parser.AppendLine(TEXT("{\"event\":\"done\"}"));
	Events.Reset();
	Parser.TakeEvents(TEXT("req-1"), Events);
	TestEqual(TEXT("second delta"), Events.Num(), 2);
	TestEqual(TEXT("second delta text"), Events[0].Text, FString(TEXT("，世界")));
	TestEqual(TEXT("done terminal"), Events[1].Type, EConversationEventType::Done);
	TestTrue(TEXT("parser done"), Parser.IsDone());
	TestTrue(TEXT("parser saw output"), Parser.HasOutput());

	FSidecarJsonLineParser ErrorParser;
	ErrorParser.AppendLine(TEXT("{\"event\":\"error\",\"code\":\"bad_key\",\"message\":\"密钥无效\"}"));
	Events.Reset();
	ErrorParser.TakeEvents(TEXT("req-2"), Events);
	TestEqual(TEXT("error event"), Events.Num(), 1);
	TestEqual(TEXT("error type"), Events[0].Type, EConversationEventType::Error);
	TestEqual(TEXT("error code"), Events[0].ErrorCode, FString(TEXT("bad_key")));
	TestTrue(TEXT("error terminal"), ErrorParser.IsDone());

	FSidecarJsonLineParser CancelParser;
	CancelParser.AppendLine(TEXT("{\"event\":\"cancelled\"}"));
	Events.Reset();
	CancelParser.TakeEvents(TEXT("req-3"), Events);
	TestTrue(TEXT("cancel terminal"),
		Events.Num() == 1 && Events[0].Type == EConversationEventType::Cancelled);

	FSidecarJsonLineParser BadParser;
	BadParser.AppendLine(TEXT("{ not json"));
	Events.Reset();
	BadParser.TakeEvents(TEXT("req-4"), Events);
	TestTrue(TEXT("malformed json reports error"),
		Events.Num() == 1 && Events[0].Type == EConversationEventType::Error);
	TestEqual(TEXT("malformed code"), Events[0].ErrorCode, FString(TEXT("bad_json")));

	FSidecarJsonLineParser LongParser;
	const FString Huge = FString::ChrN(1024 * 1024 + 1, TEXT('x'));
	LongParser.AppendLine(Huge);
	Events.Reset();
	LongParser.TakeEvents(TEXT("req-5"), Events);
	TestTrue(TEXT("oversized line rejected"),
		Events.Num() == 1 && Events[0].ErrorCode == TEXT("line_too_long"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEpicodeAssistantSidecarBackendTest,
	"Epicode.Assistant.SidecarBackendFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeAssistantSidecarBackendTest::RunTest(const FString& Parameters)
{
	FScopedSidecarTestRoot Guard;
	const FSidecarBackendConfig Config = Guard.MakeConfig();
	FDeepSeekHarnessSidecarBackend Backend(Config);

	FString Error;
	TestTrue(TEXT("sidecar available"), Backend.IsAvailable(Error));

	FConversationRequest Request;
	Request.RequestId = TEXT("sidecar-normal");
	Request.ConversationId = TEXT("conversation-test");
	Request.Prompt = TEXT("hello");
	TSharedPtr<IConversationStream> Stream = Backend.Start(Request, Error);
	TestTrue(TEXT("stream started"), Stream.IsValid());
	if (!Stream.IsValid())
	{
		AddError(FString::Printf(TEXT("sidecar start failed: %s"), *Error));
	}
	bool bDone = false;
	FString ErrorCode;
	FString ErrorMessage;
	const FString Text = DrainSidecarStream(Stream, bDone, ErrorCode, ErrorMessage);
	TestEqual(TEXT("sidecar text"), Text, FString(TEXT("你好，世界")));
	TestTrue(TEXT("sidecar done"), bDone);
	TestTrue(TEXT("sidecar no error"), ErrorCode.IsEmpty());

	// The process is reused for a second request.
	Request.RequestId = TEXT("sidecar-second");
	Request.Prompt = TEXT("again");
	Stream = Backend.Start(Request, Error);
	TestTrue(TEXT("second stream started"), Stream.IsValid());
	if (!Stream.IsValid())
	{
		AddError(FString::Printf(TEXT("sidecar second start failed: %s"), *Error));
	}
	ErrorCode.Reset();
	ErrorMessage.Reset();
	const FString SecondText = DrainSidecarStream(Stream, bDone, ErrorCode, ErrorMessage);
	TestEqual(TEXT("reused sidecar text"), SecondText, FString(TEXT("你好，世界")));

	// Timeout terminates the process and surfaces a terminal error.
	FSidecarBackendConfig TimeoutConfig = Config;
	TimeoutConfig.RequestTimeoutSeconds = 0.3f;
	FDeepSeekHarnessSidecarBackend TimeoutBackend(TimeoutConfig);
	Request.RequestId = TEXT("sidecar-timeout");
	Request.Prompt = TEXT("timeout");
	Stream = TimeoutBackend.Start(Request, Error);
	TestTrue(TEXT("timeout stream started"), Stream.IsValid());
	if (!Stream.IsValid())
	{
		AddError(FString::Printf(TEXT("sidecar timeout start failed: %s"), *Error));
	}
	ErrorCode.Reset();
	ErrorMessage.Reset();
	DrainSidecarStream(Stream, bDone, ErrorCode, ErrorMessage, 200);
	TestEqual(TEXT("timeout code"), ErrorCode, FString(TEXT("timeout")));

	// Crash is reported without affecting the backend object; the next Start
	// restarts the process.
	FDeepSeekHarnessSidecarBackend CrashBackend(Config);
	Request.RequestId = TEXT("sidecar-crash");
	Request.Prompt = TEXT("crash");
	Stream = CrashBackend.Start(Request, Error);
	TestTrue(TEXT("crash stream started"), Stream.IsValid());
	ErrorCode.Reset();
	ErrorMessage.Reset();
	DrainSidecarStream(Stream, bDone, ErrorCode, ErrorMessage);
	TestEqual(TEXT("crash code"), ErrorCode, FString(TEXT("sidecar_crashed")));

	Request.RequestId = TEXT("sidecar-restart");
	Request.Prompt = TEXT("hello");
	Stream = CrashBackend.Start(Request, Error);
	TestTrue(TEXT("restart stream started"), Stream.IsValid());
	ErrorCode.Reset();
	ErrorMessage.Reset();
	const FString RestartText = DrainSidecarStream(Stream, bDone, ErrorCode, ErrorMessage);
	TestEqual(TEXT("restart text"), RestartText, FString(TEXT("你好，世界")));

	// Cancel is bounded even when the sidecar cannot answer immediately.
	FDeepSeekHarnessSidecarBackend CancelBackend(Config);
	Request.RequestId = TEXT("sidecar-cancel");
	Request.Prompt = TEXT("cancel");
	Stream = CancelBackend.Start(Request, Error);
	TestTrue(TEXT("cancel stream started"), Stream.IsValid());
	if (Stream.IsValid())
	{
		FPlatformProcess::Sleep(0.1f);
		Stream->Cancel();
		ErrorCode.Reset();
		ErrorMessage.Reset();
		DrainSidecarStream(Stream, bDone, ErrorCode, ErrorMessage, 300);
		TestTrue(TEXT("cancel terminal"),
			ErrorCode == TEXT("cancelled") || !ErrorMessage.IsEmpty() || Stream->IsDone());
	}

	FSidecarBackendConfig MissingKey = Config;
	MissingKey.ApiKeyFile = FPaths::Combine(Guard.Root, TEXT("missing.env"));
	FDeepSeekHarnessSidecarBackend MissingKeyBackend(MissingKey);
	TestFalse(TEXT("missing key unavailable"), MissingKeyBackend.IsAvailable(Error));
	TestTrue(TEXT("missing key reason"), Error.Contains(TEXT("API_KEY")));

	const FString EmptyKeyFile = FPaths::Combine(Guard.Root, TEXT("empty.env"));
	FFileHelper::SaveStringToFile(TEXT("DEEPSEEK_API_KEY=\n"), *EmptyKeyFile);
	FSidecarBackendConfig EmptyKey = Config;
	EmptyKey.ApiKeyFile = EmptyKeyFile;
	FDeepSeekHarnessSidecarBackend EmptyKeyBackend(EmptyKey);
	TestFalse(TEXT("empty key unavailable"), EmptyKeyBackend.IsAvailable(Error));

	FSidecarBackendConfig HttpMode = Config;
	HttpMode.Mode = TEXT("http");
	FDeepSeekHarnessSidecarBackend HttpBackend(HttpMode);
	TestFalse(TEXT("http mode unavailable"), HttpBackend.IsAvailable(Error));
	TestTrue(TEXT("http mode reason"), Error.Contains(TEXT("stdio")));

	FSidecarBackendConfig MissingScript = Config;
	MissingScript.Script = FPaths::Combine(Guard.Root, TEXT("missing.py"));
	FDeepSeekHarnessSidecarBackend MissingScriptBackend(MissingScript);
	TestFalse(TEXT("missing script unavailable"), MissingScriptBackend.IsAvailable(Error));
	return true;
}

#endif
