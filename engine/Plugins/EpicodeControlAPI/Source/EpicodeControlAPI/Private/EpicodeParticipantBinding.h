#pragma once

#include "CoreMinimal.h"
#include "EpicodeCommand.h"

/**
 * Binds an Agent participant token to a command context and rejects attempts to
 * act as another participant. Kept independent of the HTTP transport so the
 * same rules apply to HTTP, WebSocket, batch and RPC entry points.
 */
namespace EpicodeParticipantBinding
{
	using FTokenResolver = TFunction<bool(const FString& InToken, FString& OutPlayerId, FString& OutError)>;

	EPICODECONTROLAPI_API bool BindWithResolver(
		const FString& InToken,
		const TSharedPtr<FJsonObject>& InParams,
		FEpicodeCommandContext& InOutContext,
		FString& OutErrorCode,
		FString& OutErrorMessage,
		const FTokenResolver& InResolver);

	EPICODECONTROLAPI_API bool Bind(
		const FString& InToken,
		const TSharedPtr<FJsonObject>& InParams,
		FEpicodeCommandContext& InOutContext,
		FString& OutErrorCode,
		FString& OutErrorMessage);
}
