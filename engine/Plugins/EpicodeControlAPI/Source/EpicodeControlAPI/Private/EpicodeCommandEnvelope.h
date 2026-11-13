#pragma once

#include "CoreMinimal.h"
#include "EpicodeCommand.h"

namespace EpicodeCommandEnvelope
{
	/** True when the caller did not pin a version, or the pinned version is current. */
	EPICODECONTROLAPI_API bool MatchesStateVersion(double ExpectedStateVersion, uint64 CurrentStateVersion);

	/**
	 * Builds the stable HTTP/WebSocket command response envelope. Command
	 * handlers only return result data; transport metadata lives here so HTTP,
	 * batch, RPC and WebSocket responses stay identical.
	 */
	EPICODECONTROLAPI_API TSharedPtr<FJsonObject> Build(
		const FString& Method,
		const FString& RequestId,
		const FEpicodeCommandResult& Result,
		int32 Status,
		bool bReplay,
		uint64 EventSequence);
}
