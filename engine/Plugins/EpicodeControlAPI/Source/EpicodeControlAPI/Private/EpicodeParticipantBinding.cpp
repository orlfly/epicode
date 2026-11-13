#include "EpicodeParticipantBinding.h"

#include "EpicodeNetworkSubsystem.h"

namespace EpicodeParticipantBinding
{
	bool Bind(
		const FString& InToken,
		const TSharedPtr<FJsonObject>& InParams,
		FEpicodeCommandContext& InOutContext,
		FString& OutErrorCode,
		FString& OutErrorMessage)
	{
		return BindWithResolver(
			InToken,
			InParams,
			InOutContext,
			OutErrorCode,
			OutErrorMessage,
			[](const FString& Token, FString& OutPlayerId, FString& OutError)
			{
				UEpicodeNetworkSubsystem* Network = UEpicodeNetworkSubsystem::Get();
				return Network && Network->ResolveAgentToken(Token, OutPlayerId, OutError);
			});
	}

	bool BindWithResolver(
		const FString& InToken,
		const TSharedPtr<FJsonObject>& InParams,
		FEpicodeCommandContext& InOutContext,
		FString& OutErrorCode,
		FString& OutErrorMessage,
		const FTokenResolver& InResolver)
	{
		OutErrorCode.Reset();
		OutErrorMessage.Reset();
		if (InToken.IsEmpty())
		{
			OutErrorCode = TEXT("PARTICIPANT_NOT_BOUND");
			OutErrorMessage = TEXT("An Agent participant token is required.");
			return false;
		}

		FString BoundPlayerId;
		if (!InResolver || !InResolver(InToken, BoundPlayerId, OutErrorMessage))
		{
			OutErrorCode = OutErrorMessage.StartsWith(TEXT("FORBIDDEN")) ? TEXT("FORBIDDEN") : TEXT("PARTICIPANT_NOT_BOUND");
			if (OutErrorMessage.IsEmpty())
			{
				OutErrorMessage = TEXT("Participant token is invalid.");
			}
			return false;
		}

		if (InParams.IsValid())
		{
			FString RequestedPlayerId;
			EpicodeJson::GetOptionalString(InParams, TEXT("playerId"), RequestedPlayerId);
			if (RequestedPlayerId.IsEmpty())
			{
				EpicodeJson::GetOptionalString(InParams, TEXT("requesterId"), RequestedPlayerId);
			}
			if (!RequestedPlayerId.IsEmpty() && RequestedPlayerId != BoundPlayerId)
			{
				OutErrorCode = TEXT("FORBIDDEN");
				OutErrorMessage = TEXT("Participant token does not match the requested player.");
				return false;
			}
			InParams->SetStringField(TEXT("playerId"), BoundPlayerId);
			if (!InParams->HasField(TEXT("requesterId")))
			{
				InParams->SetStringField(TEXT("requesterId"), BoundPlayerId);
			}
		}

		InOutContext.ParticipantToken = InToken;
		return true;
	}
}
