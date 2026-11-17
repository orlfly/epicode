#pragma once

#include "CoreMinimal.h"

/** Who produced one conversation message. */
enum class EConversationRole : uint8
{
	User,
	Assistant,
	System,
};

/** One persisted or in-flight conversation message. */
struct EPICODEASSISTANT_API FConversationMessage
{
	FString Id;
	EConversationRole Role = EConversationRole::User;
	FString Content;
	FDateTime Timestamp = FDateTime::UtcNow();

	static FString RoleToString(EConversationRole Role);
	static EConversationRole RoleFromString(const FString& Value);
};

/** Request sent to a conversation backend. */
struct EPICODEASSISTANT_API FConversationRequest
{
	FString RequestId;
	FString ConversationId;
	FString Prompt;
	FString Context;
	TArray<FConversationMessage> History;
};

enum class EConversationEventType : uint8
{
	Delta,
	Done,
	Error,
	Cancelled,
};

/** One streamed event. Delta events carry text; terminal events carry status. */
struct EPICODEASSISTANT_API FConversationEvent
{
	EConversationEventType Type = EConversationEventType::Delta;
	FString RequestId;
	FString Text;
	FString ErrorCode;
	FString ErrorMessage;
};

/** A pollable stream. Backends must make Poll cheap and non-blocking. */
class EPICODEASSISTANT_API IConversationStream
{
public:
	virtual ~IConversationStream() = default;

	virtual void Cancel() = 0;
	virtual bool IsDone() const = 0;

	/** Appends zero or more events produced since the previous poll. */
	virtual void Poll(TArray<FConversationEvent>& OutEvents) = 0;
};

/** Pluggable conversation transport (mock, sidecar, or future HTTP backend). */
class EPICODEASSISTANT_API IConversationBackend
{
public:
	virtual ~IConversationBackend() = default;

	virtual FString GetName() const = 0;

	/** Returns false with a user-facing reason when the backend cannot start. */
	virtual bool IsAvailable(FString& OutReason) const
	{
		OutReason.Reset();
		return true;
	}

	/** Returns null and fills OutError when the request cannot start. */
	virtual TSharedPtr<IConversationStream> Start(
		const FConversationRequest& Request,
		FString& OutError) = 0;
};

/** Backend used when no real transport is configured. */
class EPICODEASSISTANT_API FUnavailableConversationBackend final : public IConversationBackend
{
public:
	explicit FUnavailableConversationBackend(FString InReason);

	virtual FString GetName() const override { return TEXT("unavailable"); }
	virtual bool IsAvailable(FString& OutReason) const override;
	virtual TSharedPtr<IConversationStream> Start(
		const FConversationRequest& Request,
		FString& OutError) override;

private:
	FString Reason;
};

/** Deterministic backend for automation and offline UI demos. */
class EPICODEASSISTANT_API FMockConversationBackend final : public IConversationBackend
{
public:
	TArray<FString> ResponseChunks;
	FString SendError;
	FString StreamErrorCode;
	FString StreamErrorMessage;
	bool bCancelOnFirstPoll = false;
	bool bErrorAfterChunks = false;
	bool bTimeout = false;
	bool bCrash = false;
	bool bDoneWithoutChunks = false;
	int32 PollsBeforeChunks = 0;

	virtual FString GetName() const override { return TEXT("mock"); }
	virtual bool IsAvailable(FString& OutReason) const override;
	virtual TSharedPtr<IConversationStream> Start(
		const FConversationRequest& Request,
		FString& OutError) override;
};
