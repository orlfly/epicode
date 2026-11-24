#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct EPICODESTUDIO_API FEpicodeControlApiResponse
{
	bool bSuccess = false;
	bool bUnauthorized = false;
	bool bTimedOut = false;
	bool bConnectionError = false;
	int32 HttpStatus = 0;
	FString ErrorCode;
	FString ErrorMessage;
	FString RawBody;
	TSharedPtr<FJsonObject> Result;
};

using FEpicodeControlApiTransport = TFunction<void(
	const FString& Url,
	const FString& Body,
	const TMap<FString, FString>& Headers,
	TFunction<void(bool bConnected, int32 StatusCode, const FString& ResponseBody, const FString& TransportError)> OnComplete)>;

/** Async JSON-RPC client for a local or remote Epicode control API. */
class EPICODESTUDIO_API FEpicodeControlApiClient : public TSharedFromThis<FEpicodeControlApiClient>
{
public:
	FEpicodeControlApiClient();

	void SetBaseUrl(const FString& InBaseUrl);
	const FString& GetBaseUrl() const { return BaseUrl; }
	void SetToken(const FString& InToken);
	void SetTimeoutSeconds(double InTimeoutSeconds);
	const FString& GetMaskedToken() const { return MaskedToken; }
	bool IsConfigured() const;

	void Request(const FString& Method, const TSharedPtr<FJsonObject>& Params,
		TFunction<void(const FEpicodeControlApiResponse&)> OnComplete);

	/** Test seam: replaces the HTTP transport with an in-process fake. */
	void SetTransportForTesting(FEpicodeControlApiTransport InTransport);

	static FString NormalizeBaseUrl(const FString& InBaseUrl);
	static bool IsValidBaseUrl(const FString& InBaseUrl);
	static FString MaskToken(const FString& InToken);
	static TSharedPtr<FJsonObject> BuildRpcPayload(const FString& Method, const TSharedPtr<FJsonObject>& Params);
	static FEpicodeControlApiResponse ParseRpcResponse(int32 HttpStatus, const FString& Body,
		const FString& TransportError, bool bConnected);

private:
	void RequestWithTransport(const FString& Method, const TSharedPtr<FJsonObject>& Params,
		TFunction<void(const FEpicodeControlApiResponse&)> OnComplete);

	FString BaseUrl;
	FString Token;
	FString MaskedToken;
	double TimeoutSeconds = 3.0;
	FEpicodeControlApiTransport Transport;
};

/** Renders a control API response as a concise, token-free status string. */
EPICODESTUDIO_API FString EpicodeControlApiStatusText(const FEpicodeControlApiResponse& Response);
