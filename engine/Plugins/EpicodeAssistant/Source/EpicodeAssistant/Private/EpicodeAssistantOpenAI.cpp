#include "EpicodeAssistantOpenAI.h"

#include "Dom/JsonObject.h"
#include "HAL/ThreadSafeBool.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include <atomic>

namespace
{
	bool TryReadDeltaContent(
		const TSharedPtr<FJsonObject>& Choice,
		FString& OutContent)
	{
		if (!Choice.IsValid())
		{
			return false;
		}

		const TSharedPtr<FJsonObject>* Delta = nullptr;
		if (Choice->TryGetObjectField(TEXT("delta"), Delta)
			&& Delta != nullptr && Delta->IsValid()
			&& (*Delta)->TryGetStringField(TEXT("content"), OutContent))
		{
			return !OutContent.IsEmpty();
		}

		const TSharedPtr<FJsonObject>* Message = nullptr;
		if (Choice->TryGetObjectField(TEXT("message"), Message)
			&& Message != nullptr && Message->IsValid()
			&& (*Message)->TryGetStringField(TEXT("content"), OutContent))
		{
			return !OutContent.IsEmpty();
		}
		return false;
	}

	void ParseOpenAIJson(
		const FString& Payload,
		const FString& RequestId,
		TArray<FConversationEvent>& OutEvents,
		bool& bOutDone,
		bool& bOutHasOutput)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Payload);
		if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
		{
			return;
		}

		const TSharedPtr<FJsonObject>* Error = nullptr;
		if (Object->TryGetObjectField(TEXT("error"), Error)
			&& Error != nullptr && Error->IsValid())
		{
			FConversationEvent Event;
			Event.Type = EConversationEventType::Error;
			Event.RequestId = RequestId;
			(*Error)->TryGetStringField(TEXT("code"), Event.ErrorCode);
			(*Error)->TryGetStringField(TEXT("message"), Event.ErrorMessage);
			if (Event.ErrorMessage.IsEmpty())
			{
				Event.ErrorMessage = TEXT("DeepSeek/OpenAI 兼容接口返回错误");
			}
			OutEvents.Add(MoveTemp(Event));
			bOutDone = true;
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
		if (!Object->TryGetArrayField(TEXT("choices"), Choices)
			|| Choices == nullptr || Choices->Num() == 0)
		{
			return;
		}

		const TSharedPtr<FJsonObject> Choice = (*Choices)[0]->AsObject();
		FString Content;
		if (TryReadDeltaContent(Choice, Content))
		{
			FConversationEvent Event;
			Event.Type = EConversationEventType::Delta;
			Event.RequestId = RequestId;
			Event.Text = MoveTemp(Content);
			OutEvents.Add(MoveTemp(Event));
			bOutHasOutput = true;
		}
		else if (Choice.IsValid())
		{
			FString FinishReason;
			if (Choice->TryGetStringField(TEXT("finish_reason"), FinishReason)
				&& !FinishReason.IsEmpty())
			{
				bOutDone = true;
			}
		}
	}

	class FOpenAICompatibleStream final : public IConversationStream
		, public TSharedFromThis<FOpenAICompatibleStream, ESPMode::ThreadSafe>
	{
	public:
		explicit FOpenAICompatibleStream(FString InRequestId)
			: RequestId(MoveTemp(InRequestId))
		{
		}

		~FOpenAICompatibleStream() override
		{
			Cancel();
		}

		void SetRequest(FHttpRequestPtr InRequest)
		{
			Request = MoveTemp(InRequest);
		}

		void OnBody(void* Data, int64& Length)
		{
			if (bDone || Length <= 0 || Data == nullptr)
			{
				return;
			}
			FScopeLock Lock(&RawLock);
			RawBytes.Append(static_cast<const uint8*>(Data), static_cast<int32>(Length));
		}

		void OnComplete(FHttpResponsePtr Response, bool bRequestSuccess)
		{
			bComplete = true;
			bSuccess = bRequestSuccess;
			ResponseCode.store(Response.IsValid() ? Response->GetResponseCode() : 0);
		}

		virtual void Cancel() override
		{
			bCancelled = true;
			if (Request.IsValid())
			{
				Request->CancelRequest();
			}
		}

		virtual bool IsDone() const override
		{
			return bDone;
		}

		virtual void Poll(TArray<FConversationEvent>& OutEvents) override
		{
			if (bDone)
			{
				return;
			}

			if (bCancelled)
			{
				FConversationEvent Event;
				Event.Type = EConversationEventType::Cancelled;
				Event.RequestId = RequestId;
				Event.ErrorMessage = TEXT("已停止生成");
				OutEvents.Add(MoveTemp(Event));
				bDone = true;
				return;
			}

			TArray<uint8> LocalBytes;
			{
				FScopeLock Lock(&RawLock);
				LocalBytes = MoveTemp(RawBytes);
				RawBytes.Reset();
			}
			if (LocalBytes.Num() > 0)
			{
				Parser.Append(LocalBytes.GetData(), LocalBytes.Num(), RequestId);
			}

			TArray<FConversationEvent> Parsed;
			Parser.TakeEvents(Parsed);
			bool bTerminal = false;
			for (const FConversationEvent& Event : Parsed)
			{
				if (Event.Type == EConversationEventType::Delta)
				{
					bHadOutput = true;
				}
				else if (Event.Type == EConversationEventType::Error)
				{
					bTerminal = true;
				}
			}
			OutEvents.Append(MoveTemp(Parsed));

			if (Parser.IsDone() && !bDone)
			{
				if (!bTerminal)
				{
					FConversationEvent Done;
					Done.Type = EConversationEventType::Done;
					Done.RequestId = RequestId;
					OutEvents.Add(MoveTemp(Done));
				}
				bDone = true;
				return;
			}

			if (bComplete && !bDone)
			{
				const int32 Code = ResponseCode.load();
				if (!bSuccess || Code < 200 || Code >= 300)
				{
					FConversationEvent Event;
					Event.Type = EConversationEventType::Error;
					Event.RequestId = RequestId;
					Event.ErrorCode = FString::Printf(TEXT("http_%d"), Code);
					Event.ErrorMessage = FString::Printf(
						TEXT("DeepSeek/OpenAI 兼容接口返回 HTTP %d"), Code);
					OutEvents.Add(MoveTemp(Event));
				}
				else if (!bHadOutput && !Parser.HasOutput())
				{
					FConversationEvent Event;
					Event.Type = EConversationEventType::Error;
					Event.RequestId = RequestId;
					Event.ErrorCode = TEXT("empty_response");
					Event.ErrorMessage = TEXT("对话接口返回空回复");
					OutEvents.Add(MoveTemp(Event));
				}
				else
				{
					FConversationEvent Done;
					Done.Type = EConversationEventType::Done;
					Done.RequestId = RequestId;
					OutEvents.Add(MoveTemp(Done));
				}
				bDone = true;
			}
		}

	private:
		FString RequestId;
		FHttpRequestPtr Request;
		FOpenAICompatibleSseParser Parser;
		FCriticalSection RawLock;
		TArray<uint8> RawBytes;
		FThreadSafeBool bCancelled = false;
		FThreadSafeBool bComplete = false;
		FThreadSafeBool bSuccess = false;
		FThreadSafeBool bDone = false;
		std::atomic<int32> ResponseCode{ 0 };
		bool bHadOutput = false;
	};
}

void FOpenAICompatibleSseParser::Append(
	const void* Data, int64 Length, const FString& RequestId)
{
	if (Data == nullptr || Length <= 0)
	{
		return;
	}
	PendingBytes.Append(static_cast<const uint8*>(Data), static_cast<int32>(Length));

	int32 LineStart = 0;
	for (;;)
	{
		int32 NewlineIndex = INDEX_NONE;
		for (int32 Index = LineStart; Index < PendingBytes.Num(); ++Index)
		{
			if (PendingBytes[Index] == static_cast<uint8>('\n'))
			{
				NewlineIndex = Index;
				break;
			}
		}
		if (NewlineIndex == INDEX_NONE)
		{
			break;
		}

		TArray<uint8> LineBytes;
		LineBytes.Append(PendingBytes.GetData() + LineStart, NewlineIndex - LineStart);
		if (LineBytes.Num() > 0 && LineBytes.Last() == static_cast<uint8>('\r'))
		{
			LineBytes.Pop();
		}
		if (LineBytes.Num() > 0)
		{
			const FUTF8ToTCHAR Converter(
				reinterpret_cast<const ANSICHAR*>(LineBytes.GetData()),
				LineBytes.Num());
			ProcessLine(FString(Converter.Length(), Converter.Get()), RequestId);
		}
		LineStart = NewlineIndex + 1;
	}
	if (LineStart > 0)
	{
		PendingBytes.RemoveAt(0, LineStart);
	}
}

void FOpenAICompatibleSseParser::Append(const FString& Text, const FString& RequestId)
{
	const FTCHARToUTF8 Converter(*Text);
	Append(Converter.Get(), Converter.Length(), RequestId);
}

void FOpenAICompatibleSseParser::TakeEvents(TArray<FConversationEvent>& OutEvents)
{
	OutEvents.Append(MoveTemp(Events));
	Events.Reset();
}

void FOpenAICompatibleSseParser::Reset()
{
	PendingBytes.Reset();
	Events.Reset();
	bDone = false;
	bHasOutput = false;
}

void FOpenAICompatibleSseParser::ProcessLine(
	const FString& Line, const FString& RequestId)
{
	if (Line.IsEmpty())
	{
		return;
	}

	FString Payload;
	if (Line.StartsWith(TEXT("data:")))
	{
		Payload = Line.Mid(5).TrimStartAndEnd();
	}
	else if (Line.StartsWith(TEXT("{")))
	{
		Payload = Line.TrimStartAndEnd();
	}
	else
	{
		return;
	}

	if (Payload == TEXT("[DONE]"))
	{
		bDone = true;
		return;
	}
	if (Payload.IsEmpty())
	{
		return;
	}

	TArray<FConversationEvent> Parsed;
	bool bParsedDone = false;
	bool bParsedOutput = false;
	ParseOpenAIJson(Payload, RequestId, Parsed, bParsedDone, bParsedOutput);
	if (Parsed.Num() > 0)
	{
		bHasOutput |= bParsedOutput;
		Events.Append(MoveTemp(Parsed));
	}
	bDone |= bParsedDone;
}

FOpenAICompatibleChatBackend::FOpenAICompatibleChatBackend(
	FString InEndpoint,
	FString InModel,
	FString InApiKey,
	float InTimeoutSeconds)
	: Endpoint(MoveTemp(InEndpoint))
	, Model(MoveTemp(InModel))
	, ApiKey(MoveTemp(InApiKey))
	, TimeoutSeconds(InTimeoutSeconds)
{
}

FOpenAICompatibleChatBackend::~FOpenAICompatibleChatBackend()
{
	// Do not keep a secret in memory longer than the backend instance.
	ApiKey.Empty();
}

bool FOpenAICompatibleChatBackend::IsAvailable(FString& OutReason) const
{
	if (Endpoint.IsEmpty()
		|| (!Endpoint.StartsWith(TEXT("https://")) && !Endpoint.StartsWith(TEXT("http://"))))
	{
		OutReason = TEXT("对话 Endpoint 必须是 http:// 或 https:// 地址");
		return false;
	}
	if (ApiKey.Len() < 8)
	{
		OutReason = TEXT("未配置 DEEPSEEK_API_KEY 或 deepseek.env");
		return false;
	}
	OutReason.Reset();
	return true;
}

FString FOpenAICompatibleChatBackend::MakeChatUrl(const FString& Endpoint)
{
	FString Base = Endpoint.TrimStartAndEnd();
	while (Base.EndsWith(TEXT("/")))
	{
		Base.LeftChopInline(1);
	}
	if (Base.EndsWith(TEXT("/chat/completions")))
	{
		return Base;
	}
	return Base + TEXT("/chat/completions");
}

TSharedPtr<IConversationStream> FOpenAICompatibleChatBackend::Start(
	const FConversationRequest& Request,
	FString& OutError)
{
	if (!IsAvailable(OutError))
	{
		return nullptr;
	}
	if (Request.Prompt.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("对话消息为空");
		return nullptr;
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("model"), Model.IsEmpty() ? TEXT("deepseek-chat") : Model);
	Root->SetBoolField(TEXT("stream"), true);

	TArray<TSharedPtr<FJsonValue>> Messages;
	auto AddMessage = [&Messages](const FString& Role, const FString& Content)
	{
		if (Content.IsEmpty())
		{
			return;
		}
		TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
		Message->SetStringField(TEXT("role"), Role);
		Message->SetStringField(TEXT("content"), Content);
		Messages.Add(MakeShared<FJsonValueObject>(Message));
	};

	AddMessage(TEXT("system"), Request.Context);
	for (const FConversationMessage& History : Request.History)
	{
		if (History.Role != EConversationRole::System && !History.Content.IsEmpty())
		{
			AddMessage(FConversationMessage::RoleToString(History.Role), History.Content);
		}
	}
	AddMessage(TEXT("user"), Request.Prompt);
	Root->SetArrayField(TEXT("messages"), Messages);

	FString Payload;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Payload);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		OutError = TEXT("无法序列化对话请求");
		return nullptr;
	}

	FString RequestId = Request.RequestId;
	if (RequestId.IsEmpty())
	{
		RequestId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	}
	TSharedRef<FOpenAICompatibleStream, ESPMode::ThreadSafe> Stream =
		MakeShared<FOpenAICompatibleStream, ESPMode::ThreadSafe>(RequestId);

	FHttpRequestPtr HttpRequest = FHttpModule::Get().CreateRequest();
	Stream->SetRequest(HttpRequest);
	HttpRequest->SetURL(MakeChatUrl(Endpoint));
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("Accept"), TEXT("text/event-stream"));
	HttpRequest->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + ApiKey);
	HttpRequest->SetTimeout(FMath::Max(5.0f, TimeoutSeconds));
	HttpRequest->SetContentAsString(Payload);

	TWeakPtr<FOpenAICompatibleStream, ESPMode::ThreadSafe> WeakStream = Stream;
	if (!HttpRequest->SetResponseBodyReceiveStreamDelegateV2(
		FHttpRequestStreamDelegateV2::CreateLambda(
			[WeakStream](void* Data, int64& Length)
			{
				if (const TSharedPtr<FOpenAICompatibleStream, ESPMode::ThreadSafe> Pinned =
					WeakStream.Pin())
				{
					Pinned->OnBody(Data, Length);
				}
				else
				{
					Length = 0;
				}
			})))
	{
		OutError = TEXT("HTTP 客户端不支持流式响应");
		return nullptr;
	}

	HttpRequest->OnProcessRequestComplete().BindLambda(
		[WeakStream](FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess)
		{
			if (const TSharedPtr<FOpenAICompatibleStream, ESPMode::ThreadSafe> Pinned =
				WeakStream.Pin())
			{
				Pinned->OnComplete(Response, bSuccess);
			}
		});

	if (!HttpRequest->ProcessRequest())
	{
		OutError = TEXT("HTTP 对话请求启动失败");
		return nullptr;
	}
	return Stream;
}
