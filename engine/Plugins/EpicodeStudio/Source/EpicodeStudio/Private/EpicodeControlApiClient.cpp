#include "EpicodeControlApiClient.h"

#include "EpicodeSettings.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Containers/Ticker.h"
#include "Misc/Guid.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	const TCHAR* DefaultControlApiUrl = TEXT("http://127.0.0.1:8710");

	FString ReadErrorCode(const TSharedPtr<FJsonObject>& InError)
	{
		FString Code;
		if (InError->TryGetStringField(TEXT("code"), Code))
		{
			return Code;
		}
		double NumericCode = 0.0;
		if (InError->TryGetNumberField(TEXT("code"), NumericCode))
		{
			return FString::Printf(TEXT("%.0f"), NumericCode);
		}
		return TEXT("RPC_ERROR");
	}
}

struct FEpicodePendingRpcRequest
{
	bool bCompleted = false;
	double DeadlineSeconds = 0.0;
	FTSTicker::FDelegateHandle TimeoutHandle;
	TFunction<void(const FEpicodeControlApiResponse&)> OnComplete;
};

FEpicodeControlApiClient::FEpicodeControlApiClient()
	: BaseUrl(DefaultControlApiUrl)
{
}

void FEpicodeControlApiClient::SetBaseUrl(const FString& InBaseUrl)
{
	BaseUrl = NormalizeBaseUrl(InBaseUrl);
}

void FEpicodeControlApiClient::SetToken(const FString& InToken)
{
	Token = InToken.TrimStartAndEnd();
	MaskedToken = MaskToken(Token);
}

void FEpicodeControlApiClient::SetTimeoutSeconds(double InTimeoutSeconds)
{
	TimeoutSeconds = FMath::Clamp(InTimeoutSeconds, 0.5, 30.0);
}

bool FEpicodeControlApiClient::IsConfigured() const
{
	return IsValidBaseUrl(BaseUrl);
}

FString FEpicodeControlApiClient::NormalizeBaseUrl(const FString& InBaseUrl)
{
	FString Url = InBaseUrl.TrimStartAndEnd();
	if (Url.IsEmpty())
	{
		return DefaultControlApiUrl;
	}
	if (!Url.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase)
		&& !Url.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase))
	{
		Url = FString(TEXT("http://")) + Url;
	}
	while (Url.EndsWith(TEXT("/")))
	{
		Url.LeftChopInline(1);
	}
	if (Url.EndsWith(TEXT("/api/v1/rpc"), ESearchCase::IgnoreCase))
	{
		Url.LeftChopInline(11);
	}
	else if (Url.EndsWith(TEXT("/api/v1"), ESearchCase::IgnoreCase))
	{
		Url.LeftChopInline(7);
	}
	while (Url.EndsWith(TEXT("/")))
	{
		Url.LeftChopInline(1);
	}
	return Url;
}

bool FEpicodeControlApiClient::IsValidBaseUrl(const FString& InBaseUrl)
{
	const FString Trimmed = InBaseUrl.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return true;
	}
	if (Trimmed.Contains(TEXT("://"))
		&& !Trimmed.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase)
		&& !Trimmed.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase))
	{
		return false;
	}
	const FString Url = NormalizeBaseUrl(Trimmed);
	if (Url.Contains(TEXT(" ")) || Url.Contains(TEXT("\t")))
	{
		return false;
	}
	FString Rest;
	if (Url.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase))
	{
		Rest = Url.Mid(7);
	}
	else if (Url.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase))
	{
		Rest = Url.Mid(8);
	}
	else
	{
		return false;
	}
	const int32 SlashIndex = Rest.Find(TEXT("/"));
	const FString HostPort = SlashIndex == INDEX_NONE ? Rest : Rest.Left(SlashIndex);
	return !HostPort.IsEmpty() && !HostPort.StartsWith(TEXT(":"));
}

FString FEpicodeControlApiClient::MaskToken(const FString& InToken)
{
	const FString Trimmed = InToken.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return FString();
	}
	if (Trimmed.Len() <= 8)
	{
		return TEXT("****");
	}
	return Trimmed.Left(4) + TEXT("...") + Trimmed.Right(4);
}

TSharedPtr<FJsonObject> FEpicodeControlApiClient::BuildRpcPayload(
	const FString& Method, const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	Payload->SetStringField(TEXT("id"), FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens));
	Payload->SetStringField(TEXT("method"), Method);
	Payload->SetObjectField(TEXT("params"), Params.IsValid() ? Params : MakeShared<FJsonObject>());
	return Payload;
}

FEpicodeControlApiResponse FEpicodeControlApiClient::ParseRpcResponse(
	int32 HttpStatus, const FString& Body, const FString& TransportError, bool bConnected)
{
	FEpicodeControlApiResponse Response;
	Response.HttpStatus = HttpStatus;
	Response.RawBody = Body;

	if (!bConnected || !TransportError.IsEmpty())
	{
		Response.bConnectionError = true;
		Response.bTimedOut = TransportError.Contains(TEXT("TIMEOUT"), ESearchCase::IgnoreCase);
		Response.ErrorCode = Response.bTimedOut ? TEXT("TIMEOUT") : TEXT("CONNECTION_FAILED");
		Response.ErrorMessage = TransportError.IsEmpty()
			? TEXT("Unable to connect to the control API.")
			: TransportError;
		return Response;
	}
	if (HttpStatus == 401)
	{
		Response.bUnauthorized = true;
		Response.ErrorCode = TEXT("UNAUTHORIZED");
		Response.ErrorMessage = TEXT("The control API rejected the bearer token.");
		return Response;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		Response.ErrorCode = HttpStatus >= 400 ? TEXT("HTTP_ERROR") : TEXT("INVALID_RESPONSE");
		Response.ErrorMessage = HttpStatus >= 400
			? FString::Printf(TEXT("Control API returned HTTP %d."), HttpStatus)
			: TEXT("Control API returned malformed JSON.");
		return Response;
	}

	const TSharedPtr<FJsonObject>* ErrorObject = nullptr;
	if (Root->TryGetObjectField(TEXT("error"), ErrorObject) && ErrorObject && ErrorObject->IsValid())
	{
		Response.ErrorCode = ReadErrorCode(*ErrorObject);
		(*ErrorObject)->TryGetStringField(TEXT("message"), Response.ErrorMessage);
		const TSharedPtr<FJsonObject>* DataObject = nullptr;
		if ((*ErrorObject)->TryGetObjectField(TEXT("data"), DataObject) && DataObject && DataObject->IsValid())
		{
			FString DomainCode;
			if ((*DataObject)->TryGetStringField(TEXT("domainCode"), DomainCode) && !DomainCode.IsEmpty())
			{
				Response.ErrorCode = DomainCode;
			}
			if (Response.ErrorMessage.IsEmpty())
			{
				(*DataObject)->TryGetStringField(TEXT("message"), Response.ErrorMessage);
			}
		}
		if (Response.ErrorMessage.IsEmpty())
		{
			Response.ErrorMessage = TEXT("Control API RPC request failed.");
		}
		return Response;
	}

	const TSharedPtr<FJsonObject>* ResultObject = nullptr;
	if (Root->TryGetObjectField(TEXT("result"), ResultObject) && ResultObject && ResultObject->IsValid())
	{
		Response.Result = *ResultObject;
		bool bCommandOk = true;
		if (Response.Result->TryGetBoolField(TEXT("ok"), bCommandOk) && !bCommandOk)
		{
			const TSharedPtr<FJsonObject>* CommandError = nullptr;
			if (Response.Result->TryGetObjectField(TEXT("error"), CommandError) && CommandError && CommandError->IsValid())
			{
				Response.ErrorCode = ReadErrorCode(*CommandError);
				(*CommandError)->TryGetStringField(TEXT("message"), Response.ErrorMessage);
			}
			else
			{
				Response.ErrorCode = TEXT("COMMAND_FAILED");
			}
			return Response;
		}
		Response.bSuccess = true;
		return Response;
	}

	Response.ErrorCode = HttpStatus >= 400 ? TEXT("HTTP_ERROR") : TEXT("INVALID_RESPONSE");
	Response.ErrorMessage = HttpStatus >= 400
		? FString::Printf(TEXT("Control API returned HTTP %d."), HttpStatus)
		: TEXT("Control API response did not contain a JSON-RPC result.");
	return Response;
}

void FEpicodeControlApiClient::Request(const FString& Method, const TSharedPtr<FJsonObject>& Params,
	TFunction<void(const FEpicodeControlApiResponse&)> OnComplete)
{
	if (!IsConfigured())
	{
		FEpicodeControlApiResponse Response;
		Response.ErrorCode = TEXT("INVALID_URL");
		Response.ErrorMessage = TEXT("Control API address is empty or invalid.");
		OnComplete(Response);
		return;
	}
	if (Token.IsEmpty())
	{
		SetToken(UEpicodeSettings::Get()->GetApiToken());
	}
	if (Token.IsEmpty())
	{
		FEpicodeControlApiResponse Response;
		Response.bUnauthorized = true;
		Response.ErrorCode = TEXT("UNAUTHORIZED");
		Response.ErrorMessage = TEXT("No control API token is configured.");
		OnComplete(Response);
		return;
	}
	MaskedToken = MaskToken(Token);
	RequestWithTransport(Method, Params, MoveTemp(OnComplete));
}

void FEpicodeControlApiClient::SetTransportForTesting(FEpicodeControlApiTransport InTransport)
{
	Transport = MoveTemp(InTransport);
}

void FEpicodeControlApiClient::RequestWithTransport(const FString& Method, const TSharedPtr<FJsonObject>& Params,
	TFunction<void(const FEpicodeControlApiResponse&)> OnComplete)
{
	const TSharedPtr<FJsonObject> Payload = BuildRpcPayload(Method, Params);
	FString Body;
	const TSharedRef<TJsonWriter<TCHAR>> Writer = TJsonWriterFactory<TCHAR>::Create(&Body);
	if (!FJsonSerializer::Serialize(Payload.ToSharedRef(), Writer))
	{
		FEpicodeControlApiResponse Response;
		Response.ErrorCode = TEXT("SERIALIZE_FAILED");
		Response.ErrorMessage = TEXT("Unable to serialize the control API request.");
		OnComplete(Response);
		return;
	}

	TMap<FString, FString> Headers;
	Headers.Add(TEXT("Content-Type"), TEXT("application/json"));
	Headers.Add(TEXT("Accept"), TEXT("application/json"));
	Headers.Add(TEXT("Authorization"), FString(TEXT("Bearer ")) + Token);
	const FString Url = BaseUrl + TEXT("/api/v1/rpc");

	if (Transport)
	{
		Transport(Url, Body, Headers,
			[OnComplete](bool bConnected, int32 StatusCode, const FString& ResponseBody, const FString& TransportError)
			{
				OnComplete(ParseRpcResponse(StatusCode, ResponseBody, TransportError, bConnected));
			});
		return;
	}

	FHttpRequestPtr HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(Url);
	HttpRequest->SetVerb(TEXT("POST"));
	for (const TPair<FString, FString>& Header : Headers)
	{
		HttpRequest->SetHeader(Header.Key, Header.Value);
	}
	HttpRequest->SetTimeout(TimeoutSeconds);
	HttpRequest->SetContentAsString(Body);

	TSharedPtr<FEpicodePendingRpcRequest> Pending = MakeShared<FEpicodePendingRpcRequest>();
	Pending->DeadlineSeconds = FPlatformTime::Seconds() + TimeoutSeconds;
	Pending->OnComplete = MoveTemp(OnComplete);
	auto Complete = [Pending](bool bConnected, int32 StatusCode, const FString& ResponseBody,
		const FString& TransportError)
	{
		if (Pending->bCompleted)
		{
			return;
		}
		Pending->bCompleted = true;
		if (Pending->TimeoutHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(Pending->TimeoutHandle);
			Pending->TimeoutHandle.Reset();
		}
		Pending->OnComplete(ParseRpcResponse(StatusCode, ResponseBody, TransportError, bConnected));
	};

	HttpRequest->OnProcessRequestComplete().BindLambda(
		[Complete](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			const int32 StatusCode = Response.IsValid() ? Response->GetResponseCode() : 0;
			const FString ResponseBody = Response.IsValid() ? Response->GetContentAsString() : FString();
			const FString TransportError = bConnected ? FString() : TEXT("CONNECTION_FAILED");
			Complete(bConnected, StatusCode, ResponseBody, TransportError);
		});
	Pending->TimeoutHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([Pending](float) -> bool
		{
			if (Pending->bCompleted || FPlatformTime::Seconds() < Pending->DeadlineSeconds)
			{
				return !Pending->bCompleted;
			}
			Pending->bCompleted = true;
			Pending->OnComplete(ParseRpcResponse(0, FString(), TEXT("TIMEOUT"), false));
			return false;
		}));
	if (!HttpRequest->ProcessRequest())
	{
		Complete(false, 0, FString(), TEXT("REQUEST_START_FAILED"));
	}
}

FString EpicodeControlApiStatusText(const FEpicodeControlApiResponse& Response)
{
	if (Response.bSuccess)
	{
		return TEXT("OK");
	}
	if (Response.ErrorMessage.IsEmpty())
	{
		return Response.ErrorCode.IsEmpty() ? TEXT("Control API request failed.") : Response.ErrorCode;
	}
	return Response.ErrorCode.IsEmpty()
		? Response.ErrorMessage
		: FString::Printf(TEXT("%s: %s"), *Response.ErrorCode, *Response.ErrorMessage);
}
