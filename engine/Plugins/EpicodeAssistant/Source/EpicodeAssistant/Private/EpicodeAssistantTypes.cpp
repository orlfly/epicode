#include "EpicodeAssistantTypes.h"

FString FConversationMessage::RoleToString(EConversationRole Role)
{
	switch (Role)
	{
	case EConversationRole::Assistant:
		return TEXT("assistant");
	case EConversationRole::System:
		return TEXT("system");
	case EConversationRole::User:
	default:
		return TEXT("user");
	}
}

EConversationRole FConversationMessage::RoleFromString(const FString& Value)
{
	const FString Lower = Value.ToLower();
	if (Lower == TEXT("assistant"))
	{
		return EConversationRole::Assistant;
	}
	if (Lower == TEXT("system"))
	{
		return EConversationRole::System;
	}
	return EConversationRole::User;
}

FUnavailableConversationBackend::FUnavailableConversationBackend(FString InReason)
	: Reason(MoveTemp(InReason))
{
}

bool FUnavailableConversationBackend::IsAvailable(FString& OutReason) const
{
	OutReason = Reason.IsEmpty() ? TEXT("对话后端未配置") : Reason;
	return false;
}

TSharedPtr<IConversationStream> FUnavailableConversationBackend::Start(
	const FConversationRequest& Request,
	FString& OutError)
{
	OutError = Reason.IsEmpty() ? TEXT("对话后端未配置") : Reason;
	return nullptr;
}

namespace
{
	class FMockConversationStream final : public IConversationStream
	{
	public:
		FMockConversationStream(
			FString InRequestId,
			TArray<FString> InChunks,
			const FMockConversationBackend& Config)
			: RequestId(MoveTemp(InRequestId))
			, Chunks(MoveTemp(InChunks))
			, ErrorCode(Config.StreamErrorCode)
			, ErrorMessage(Config.StreamErrorMessage)
			, RemainingPolls(Config.PollsBeforeChunks)
			, bCancelOnFirstPoll(Config.bCancelOnFirstPoll)
			, bErrorAfterChunks(Config.bErrorAfterChunks)
			, bTimeout(Config.bTimeout)
			, bCrash(Config.bCrash)
		{
		}

		virtual void Cancel() override
		{
			bCancelled = true;
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

			if (bCancelOnFirstPoll && NextChunk == 0)
			{
				Cancel();
				return;
			}

			if (RemainingPolls > 0)
			{
				--RemainingPolls;
				return;
			}

			if (Chunks.IsValidIndex(NextChunk))
			{
				FConversationEvent Event;
				Event.Type = EConversationEventType::Delta;
				Event.RequestId = RequestId;
				Event.Text = Chunks[NextChunk++];
				OutEvents.Add(MoveTemp(Event));
				return;
			}

			FConversationEvent Event;
			Event.RequestId = RequestId;
			if (bTimeout)
			{
				Event.Type = EConversationEventType::Error;
				Event.ErrorCode = TEXT("timeout");
				Event.ErrorMessage = TEXT("模拟请求超时");
			}
			else if (bCrash)
			{
				Event.Type = EConversationEventType::Error;
				Event.ErrorCode = TEXT("backend_crashed");
				Event.ErrorMessage = TEXT("模拟后端崩溃");
			}
			else if (bErrorAfterChunks)
			{
				Event.Type = EConversationEventType::Error;
				Event.ErrorCode = ErrorCode.IsEmpty() ? TEXT("mock_stream_error") : ErrorCode;
				Event.ErrorMessage = ErrorMessage.IsEmpty()
					? TEXT("模拟对话流失败")
					: ErrorMessage;
			}
			else
			{
				Event.Type = EConversationEventType::Done;
			}
			OutEvents.Add(MoveTemp(Event));
			bDone = true;
		}

	private:
		FString RequestId;
		TArray<FString> Chunks;
		FString ErrorCode;
		FString ErrorMessage;
		int32 RemainingPolls = 0;
		int32 NextChunk = 0;
		bool bCancelOnFirstPoll = false;
		bool bErrorAfterChunks = false;
		bool bTimeout = false;
		bool bCrash = false;
		bool bCancelled = false;
		bool bDone = false;
	};
}

bool FMockConversationBackend::IsAvailable(FString& OutReason) const
{
	if (!SendError.IsEmpty())
	{
		OutReason = SendError;
		return false;
	}
	OutReason.Reset();
	return true;
}

TSharedPtr<IConversationStream> FMockConversationBackend::Start(
	const FConversationRequest& Request,
	FString& OutError)
{
	if (!SendError.IsEmpty())
	{
		OutError = SendError;
		return nullptr;
	}

	FString RequestId = Request.RequestId;
	if (RequestId.IsEmpty())
	{
		RequestId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	}
	return MakeShared<FMockConversationStream>(RequestId, ResponseChunks, *this);
}
