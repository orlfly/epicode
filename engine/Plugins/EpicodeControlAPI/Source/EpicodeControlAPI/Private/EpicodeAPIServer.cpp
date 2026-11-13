#include "EpicodeAPIServer.h"

#include "Dom/JsonObject.h"
#include "EpicodeCommand.h"
#include "EpicodeControlAPIModule.h"
#include "EpicodeDiagnostics.h"
#include "EpicodeEngineSubsystem.h"
#include "EpicodeNetworkSubsystem.h"
#include "EpicodeEventBus.h"
#include "EpicodeParticipantBinding.h"
#include "EpicodeCommandEnvelope.h"
#include "EpicodeAgentAuditLog.h"
#include "EpicodeSceneSubsystem.h"
#include "EpicodeSettings.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "Misc/App.h"
#include "HAL/PlatformTime.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	constexpr int32 MaxBatchCommands = 64;
	constexpr int32 MaxIdempotencyEntries = 10000;
	constexpr double IdempotencyTtlSeconds = 86400.0;
	constexpr double TicketTtlSeconds = 30.0;

	TSharedPtr<FJsonObject> ParseJsonObject(const FString& Text, FString& OutError)
	{
		TSharedPtr<FJsonObject> Json;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		{
			OutError = TEXT("Request body must be a valid JSON object.");
			return nullptr;
		}
		return Json;
	}

	bool ParseJsonValue(const FString& Text, TSharedPtr<FJsonValue>& OutValue, FString& OutError)
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, OutValue) || !OutValue.IsValid())
		{
			OutError = TEXT("Request body must be valid JSON.");
			return false;
		}
		return true;
	}

	FString SerializeJson(const TSharedPtr<FJsonObject>& Json)
	{
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(Json.ToSharedRef(), Writer);
		return Text;
	}

	FString SerializeJsonArray(const TArray<TSharedPtr<FJsonValue>>& Values)
	{
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(Values, Writer);
		return Text;
	}

	bool GetHeader(const FHttpServerRequest& Request, const FString& Name, FString& OutValue)
	{
		for (const TPair<FString, TArray<FString>>& Pair : Request.Headers)
		{
			if (Pair.Key.Equals(Name, ESearchCase::IgnoreCase) && Pair.Value.Num() > 0)
			{
				OutValue = Pair.Value[0];
				return true;
			}
		}
		return false;
	}

	bool ConstantTimeEquals(const FString& A, const FString& B)
	{
		if (A.Len() != B.Len())
		{
			return false;
		}
		uint32 Diff = 0;
		for (int32 Index = 0; Index < A.Len(); ++Index)
		{
			Diff |= static_cast<uint32>(A[Index]) ^ static_cast<uint32>(B[Index]);
		}
		return Diff == 0;
	}

	FString MakeRequestId()
	{
		return FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	}

	int32 HttpStatusForResult(const FEpicodeCommandResult& Result)
	{
		if (Result.bSuccess)
		{
			return 200;
		}
		const FString& Code = Result.ErrorCode;
		if (Code == TEXT("METHOD_NOT_FOUND") || Code.EndsWith(TEXT("_NOT_FOUND")) || Code == TEXT("JOB_NOT_FOUND"))
		{
			return 404;
		}
		if (Code == TEXT("NOT_INITIALIZED"))
		{
			return 503;
		}
		if (Code == TEXT("NO_SCENE") || Code == TEXT("UNSAVED_CHANGES")
			|| Code == TEXT("SCENE_CREATE_FAILED") || Code == TEXT("SCENE_OPEN_FAILED")
			|| Code == TEXT("SCENE_SWITCH_FAILED") || Code == TEXT("SCENE_DELETE_FAILED")
			|| Code == TEXT("SCENE_CLOSE_FAILED") || Code == TEXT("ENTITY_DELETE_FAILED")
			|| Code == TEXT("JOB_NOT_CANCELLABLE") || Code == TEXT("TRANSACTION_FAILED"))
		{
			return 409;
		}
		if (Code == TEXT("SCENE_SAVE_FAILED") || Code == TEXT("INTERNAL_ERROR"))
		{
			return 500;
		}
		if (Code.StartsWith(TEXT("INVALID_")) || Code == TEXT("CREATE_FAILED") || Code == TEXT("UPDATE_FAILED")
			|| Code == TEXT("ATTACH_FAILED") || Code == TEXT("DETACH_FAILED") || Code == TEXT("MATERIAL_FAILED")
			|| Code == TEXT("ANIMATION_FAILED") || Code.StartsWith(TEXT("IMPORT_")) || Code == TEXT("IMPORT_REJECTED"))
		{
			return 422;
		}
		return 400;
	}

	TUniquePtr<FHttpServerResponse> MakeJsonResponse(int32 Status, const TSharedPtr<FJsonObject>& Body)
	{
		const FString Text = SerializeJson(Body);
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(Text, TEXT("application/json"));
		Response->Code = static_cast<EHttpServerResponseCodes>(Status);
		if (UEpicodeSettings::Get()->bEnableCors)
		{
			Response->Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("*") });
			Response->Headers.Add(TEXT("Access-Control-Allow-Headers"), { TEXT("Authorization, Content-Type, Idempotency-Key, X-Request-Id, X-Epicode-Token") });
			Response->Headers.Add(TEXT("Access-Control-Allow-Methods"), { TEXT("GET, POST, OPTIONS") });
		}
		Response->Headers.Add(TEXT("Cache-Control"), { TEXT("no-store") });
		return Response;
	}

	TUniquePtr<FHttpServerResponse> MakeRawJsonResponse(int32 Status, const FString& Text)
	{
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(Text, TEXT("application/json"));
		Response->Code = static_cast<EHttpServerResponseCodes>(Status);
		if (UEpicodeSettings::Get()->bEnableCors)
		{
			Response->Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("*") });
			Response->Headers.Add(TEXT("Access-Control-Allow-Headers"), { TEXT("Authorization, Content-Type, Idempotency-Key, X-Request-Id, X-Epicode-Token") });
			Response->Headers.Add(TEXT("Access-Control-Allow-Methods"), { TEXT("GET, POST, OPTIONS") });
		}
		Response->Headers.Add(TEXT("Cache-Control"), { TEXT("no-store") });
		return Response;
	}

	TSharedPtr<FJsonObject> MakeRpcError(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message, const FString& DomainCode = FString(), const FString& Detail = FString())
	{
		TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
		Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
		if (Id.IsValid())
		{
			Response->SetField(TEXT("id"), Id);
		}
		else
		{
			Response->SetField(TEXT("id"), MakeShared<FJsonValueNull>());
		}
		TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetNumberField(TEXT("code"), Code);
		Error->SetStringField(TEXT("message"), Message);
		if (!DomainCode.IsEmpty() || !Detail.IsEmpty())
		{
			TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetStringField(TEXT("domainCode"), DomainCode);
			Data->SetStringField(TEXT("detail"), Detail);
			Error->SetObjectField(TEXT("data"), Data);
		}
		Response->SetObjectField(TEXT("error"), Error);
		return Response;
	}

	TUniquePtr<FHttpServerResponse> MakeCorsResponse()
	{
		if (!UEpicodeSettings::Get()->bEnableCors)
		{
			TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(FString(TEXT("CORS is disabled.")), FString(TEXT("text/plain")));
			Response->Code = EHttpServerResponseCodes::Forbidden;
			return MoveTemp(Response);
		}
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(FString(), TEXT("text/plain"));
		Response->Code = EHttpServerResponseCodes::NoContent;
		Response->Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("*") });
		Response->Headers.Add(TEXT("Access-Control-Allow-Headers"), { TEXT("Authorization, Content-Type, Idempotency-Key, X-Request-Id, X-Epicode-Token") });
		Response->Headers.Add(TEXT("Access-Control-Allow-Methods"), { TEXT("GET, POST, OPTIONS") });
		Response->Headers.Add(TEXT("Access-Control-Max-Age"), { TEXT("600") });
		return Response;
	}

	FString ExtractQueryParam(const FString& Path, const FString& Name)
	{
		const int32 QuestionIndex = Path.Find(TEXT("?"));
		if (QuestionIndex == INDEX_NONE)
		{
			return FString();
		}
		const FString Query = Path.Mid(QuestionIndex + 1);
		TArray<FString> Parts;
		Query.ParseIntoArray(Parts, TEXT("&"), true);
		for (const FString& Part : Parts)
		{
			int32 EqualsIndex = INDEX_NONE;
			if (Part.FindChar(TEXT('='), EqualsIndex))
			{
				const FString Key = Part.Left(EqualsIndex);
				if (Key.Equals(Name, ESearchCase::IgnoreCase))
				{
					return Part.Mid(EqualsIndex + 1);
				}
			}
		}
		return FString();
	}
}

FEpicodeAPIServer& FEpicodeAPIServer::Get()
{
	static FEpicodeAPIServer Instance;
	return Instance;
}

bool FEpicodeAPIServer::Start(FString& OutError)
{
	if (bRunning)
	{
		return true;
	}
	const UEpicodeSettings* Settings = UEpicodeSettings::Get();
	if (!Settings->bApiEnabled)
	{
		OutError = TEXT("API is disabled by settings.");
		return false;
	}
	if (!Settings->Validate(OutError))
	{
		return false;
	}
	HttpRouter = FHttpServerModule::Get().GetHttpRouter(static_cast<uint32>(Settings->ApiHttpPort), true);
	if (!HttpRouter.IsValid())
	{
		OutError = FString::Printf(TEXT("Unable to bind HTTP port %d."), Settings->ApiHttpPort);
		return false;
	}

	auto Bind = [this](const TCHAR* Path, EHttpServerRequestVerbs Verbs, FHttpRequestHandler Handler)
	{
		const FHttpRouteHandle Handle = HttpRouter->BindRoute(FHttpPath(Path), Verbs, MoveTemp(Handler));
		if (Handle.IsValid())
		{
			RouteHandles.Add(Handle);
		}
		else
		{
			UE_LOG(LogEpicodeAPI, Warning, TEXT("Unable to bind route %s"), Path);
		}
	};

	Bind(TEXT("/api/v1/health"), EHttpServerRequestVerbs::VERB_GET, FHttpRequestHandler::CreateRaw(this, &FEpicodeAPIServer::HandleHealth));
	Bind(TEXT("/api/v1/methods"), EHttpServerRequestVerbs::VERB_GET, FHttpRequestHandler::CreateRaw(this, &FEpicodeAPIServer::HandleMethods));
	Bind(TEXT("/api/v1/command"), EHttpServerRequestVerbs::VERB_POST, FHttpRequestHandler::CreateRaw(this, &FEpicodeAPIServer::HandleCommand));
	Bind(TEXT("/api/v1/rpc"), EHttpServerRequestVerbs::VERB_POST, FHttpRequestHandler::CreateRaw(this, &FEpicodeAPIServer::HandleRpc));
	Bind(TEXT("/api/v1/commands/:method"), EHttpServerRequestVerbs::VERB_POST, FHttpRequestHandler::CreateRaw(this, &FEpicodeAPIServer::HandleCommand));
	Bind(TEXT("/api/v1/commands/:method"), EHttpServerRequestVerbs::VERB_GET, FHttpRequestHandler::CreateRaw(this, &FEpicodeAPIServer::HandleCommand));
	Bind(TEXT("/api/v1/batch"), EHttpServerRequestVerbs::VERB_POST, FHttpRequestHandler::CreateRaw(this, &FEpicodeAPIServer::HandleBatch));
	Bind(TEXT("/api/v1/ws-ticket"), EHttpServerRequestVerbs::VERB_POST, FHttpRequestHandler::CreateRaw(this, &FEpicodeAPIServer::HandleWsTicket));
	Bind(TEXT("/api/v1/:path"), EHttpServerRequestVerbs::VERB_OPTIONS, FHttpRequestHandler::CreateRaw(this, &FEpicodeAPIServer::HandleOptions));
	Bind(TEXT("/api/v1/rpc"), EHttpServerRequestVerbs::VERB_OPTIONS, FHttpRequestHandler::CreateRaw(this, &FEpicodeAPIServer::HandleOptions));
	Bind(TEXT("/api/v1/commands/:method"), EHttpServerRequestVerbs::VERB_OPTIONS, FHttpRequestHandler::CreateRaw(this, &FEpicodeAPIServer::HandleOptions));
	FHttpServerModule::Get().StartAllListeners();

	WebSocketServer = MakeShared<FEpicodeWebSocketServer>();
	if (WebSocketServer->Start(
		static_cast<uint32>(Settings->ApiWsPort),
		Settings->bAllowRemote,
		[this](const FString& Path)
		{
			if (!UEpicodeSettings::Get()->bRequireAuth)
			{
				return true;
			}
			return ConsumeTicket(ExtractQueryParam(Path, TEXT("ticket")));
		},
		[this](TSharedRef<FEpicodeWebSocketConnection> Connection) { HandleWsConnected(Connection); },
		[this](TSharedRef<FEpicodeWebSocketConnection> Connection, const FString& Message) { HandleWsMessage(Connection, Message); },
		[this](TSharedRef<FEpicodeWebSocketConnection> Connection) { HandleWsDisconnected(Connection); }))
	{
		UE_LOG(LogEpicodeAPI, Log, TEXT("WebSocket listening on port %d"), Settings->ApiWsPort);
	}
	else
	{
		UE_LOG(LogEpicodeAPI, Warning, TEXT("Unable to start WebSocket listener on port %d"), Settings->ApiWsPort);
		WebSocketServer.Reset();
	}

	StartedAt = FDateTime::UtcNow();
	LoadIdempotencyCache();
	if (UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get())
	{
		if (FEpicodeEventBus* EventBus = EngineSubsystem->GetEventBus())
		{
			LastBroadcastSequence = EventBus->GetLatestSequence();
		}
	}
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FEpicodeAPIServer::Tick), 0.05f);
	bRunning = true;
	UE_LOG(LogEpicodeAPI, Log, TEXT("Epicode API listening on %s (WebSocket %s)"), *Settings->GetHttpBaseUrl(), *Settings->GetWebSocketUrl());
	return true;
}

void FEpicodeAPIServer::Stop()
{
	if (!bRunning)
	{
		return;
	}
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	SaveIdempotencyCache();
	{
		FScopeLock ScopeLock(&Lock);
		WsTickets.Reset();
		IdempotencyCache.Reset();
	}
	{
		FScopeLock RateScopeLock(&RateLock);
		RateWindows.Reset();
	}
	if (WebSocketServer.IsValid())
	{
		WebSocketServer->Stop();
		WebSocketServer.Reset();
	}
	if (HttpRouter.IsValid())
	{
		for (const FHttpRouteHandle& Handle : RouteHandles)
		{
			HttpRouter->UnbindRoute(Handle);
		}
		RouteHandles.Reset();
		HttpRouter.Reset();
	}
	if (FHttpServerModule::IsAvailable())
	{
		FHttpServerModule::Get().StopAllListeners();
	}
	bRunning = false;
	UE_LOG(LogEpicodeAPI, Log, TEXT("Epicode API stopped"));
}

bool FEpicodeAPIServer::Tick(float DeltaSeconds)
{
	BroadcastEvents();
	CleanupTickets();
	const FDateTime Now = FDateTime::UtcNow();
	bool bShouldSave = false;
	{
		FScopeLock ScopeLock(&Lock);
		for (auto It = IdempotencyCache.CreateIterator(); It; ++It)
		{
			if (It.Value().ExpiresAt <= Now)
			{
				It.RemoveCurrent();
				bIdempotencyDirty = true;
			}
		}
		bShouldSave = bIdempotencyDirty && FPlatformTime::Seconds() - LastIdempotencySaveSeconds >= 1.0;
	}
	if (bShouldSave)
	{
		SaveIdempotencyCache();
	}
	return true;
}

void FEpicodeAPIServer::LoadIdempotencyCache()
{
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Epicode/idempotency.json"));
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		return;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return;
	}
	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (!Root->TryGetArrayField(TEXT("entries"), Entries) || !Entries)
	{
		return;
	}
	const FDateTime Now = FDateTime::UtcNow();
	FScopeLock ScopeLock(&Lock);
	for (const TSharedPtr<FJsonValue>& Value : *Entries)
	{
		const TSharedPtr<FJsonObject> EntryJson = Value.IsValid() ? Value->AsObject() : nullptr;
		if (!EntryJson.IsValid())
		{
			continue;
		}
		FString Key;
		FString Signature;
		FString ExpiresText;
		if (!EntryJson->TryGetStringField(TEXT("key"), Key) || Key.IsEmpty()
			|| !EntryJson->TryGetStringField(TEXT("signature"), Signature)
			|| !EntryJson->TryGetStringField(TEXT("expiresAt"), ExpiresText))
		{
			continue;
		}
		FDateTime ExpiresAt;
		if (!FDateTime::ParseIso8601(*ExpiresText, ExpiresAt) || ExpiresAt <= Now)
		{
			continue;
		}
		const TSharedPtr<FJsonObject>* Command = nullptr;
		if (!EntryJson->TryGetObjectField(TEXT("command"), Command) || !Command)
		{
			continue;
		}
		FIdempotentEntry Entry;
		Entry.Signature = Signature;
		Entry.ExpiresAt = ExpiresAt;
		(*Command)->TryGetBoolField(TEXT("success"), Entry.Result.bSuccess);
		if (Entry.Result.bSuccess)
		{
			const TSharedPtr<FJsonObject>* ResultObject = nullptr;
			if ((*Command)->TryGetObjectField(TEXT("result"), ResultObject) && ResultObject)
			{
				Entry.Result.Result = *ResultObject;
			}
		}
		else
		{
			const TSharedPtr<FJsonObject>* ErrorObject = nullptr;
			if ((*Command)->TryGetObjectField(TEXT("error"), ErrorObject) && ErrorObject)
			{
				(*ErrorObject)->TryGetStringField(TEXT("code"), Entry.Result.ErrorCode);
				(*ErrorObject)->TryGetStringField(TEXT("message"), Entry.Result.ErrorMessage);
				(*ErrorObject)->TryGetStringField(TEXT("detail"), Entry.Result.ErrorDetail);
				(*ErrorObject)->TryGetBoolField(TEXT("retryable"), Entry.Result.bRetryable);
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* AffectedIds = nullptr;
		if ((*Command)->TryGetArrayField(TEXT("affectedIds"), AffectedIds) && AffectedIds)
		{
			for (const TSharedPtr<FJsonValue>& IdValue : *AffectedIds)
			{
				if (IdValue.IsValid() && IdValue->Type == EJson::String)
				{
					Entry.Result.AffectedIds.Add(IdValue->AsString());
				}
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Warnings = nullptr;
		if ((*Command)->TryGetArrayField(TEXT("warnings"), Warnings) && Warnings)
		{
			for (const TSharedPtr<FJsonValue>& WarningValue : *Warnings)
			{
				if (WarningValue.IsValid() && WarningValue->Type == EJson::String)
				{
					Entry.Result.Warnings.Add(WarningValue->AsString());
				}
			}
		}
		if (IdempotencyCache.Num() < MaxIdempotencyEntries)
		{
			IdempotencyCache.Add(Key, Entry);
		}
	}
}

void FEpicodeAPIServer::SaveIdempotencyCache()
{
	TArray<TSharedPtr<FJsonValue>> Entries;
	{
		FScopeLock ScopeLock(&Lock);
		if (!bIdempotencyDirty)
		{
			return;
		}
		bIdempotencyDirty = false;
		LastIdempotencySaveSeconds = FPlatformTime::Seconds();
		for (const TPair<FString, FIdempotentEntry>& Pair : IdempotencyCache)
		{
			TSharedPtr<FJsonObject> EntryJson = MakeShared<FJsonObject>();
			EntryJson->SetStringField(TEXT("key"), Pair.Key);
			EntryJson->SetStringField(TEXT("signature"), Pair.Value.Signature);
			EntryJson->SetStringField(TEXT("expiresAt"), Pair.Value.ExpiresAt.ToIso8601());
			EntryJson->SetObjectField(TEXT("command"), Pair.Value.Result.ToJson());
			Entries.Add(MakeShared<FJsonValueObject>(EntryJson));
		}
	}
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), 1);
	Root->SetArrayField(TEXT("entries"), Entries);
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Epicode/idempotency.json"));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	FFileHelper::SaveStringToFile(SerializeJson(Root), *Path);
}

void FEpicodeAPIServer::CleanupTickets()
{
	const FDateTime Now = FDateTime::UtcNow();
	FScopeLock ScopeLock(&Lock);
	for (auto It = WsTickets.CreateIterator(); It; ++It)
	{
		if (It.Value() <= Now)
		{
			It.RemoveCurrent();
		}
	}
}

FString FEpicodeAPIServer::IssueTicket()
{
	const FDateTime ExpiresAt = FDateTime::UtcNow() + FTimespan::FromSeconds(TicketTtlSeconds);
	const FString Ticket = FGuid::NewGuid().ToString(EGuidFormats::Digits) + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	FScopeLock ScopeLock(&Lock);
	WsTickets.Add(Ticket, ExpiresAt);
	return Ticket;
}

bool FEpicodeAPIServer::ConsumeTicket(const FString& Ticket)
{
	FScopeLock ScopeLock(&Lock);
	FDateTime* ExpiresAt = WsTickets.Find(Ticket);
	if (!ExpiresAt || *ExpiresAt <= FDateTime::UtcNow())
	{
		WsTickets.Remove(Ticket);
		return false;
	}
	WsTickets.Remove(Ticket);
	return true;
}

bool FEpicodeAPIServer::CheckAuth(const FHttpServerRequest& Request) const
{
	const UEpicodeSettings* Settings = UEpicodeSettings::Get();
	if (!Settings->bRequireAuth)
	{
		return true;
	}
	FString Provided;
	if (!GetHeader(Request, TEXT("Authorization"), Provided) && !GetHeader(Request, TEXT("X-Epicode-Token"), Provided))
	{
		return false;
	}
	if (Provided.StartsWith(TEXT("Bearer "), ESearchCase::IgnoreCase))
	{
		Provided = Provided.RightChop(7);
	}
	return ConstantTimeEquals(Provided, Settings->GetApiToken());
}

bool FEpicodeAPIServer::CheckRemote(const FHttpServerRequest& Request) const
{
	if (UEpicodeSettings::Get()->bAllowRemote)
	{
		return true;
	}
	if (!Request.PeerAddress.IsValid())
	{
		return false;
	}
	const FString Address = Request.PeerAddress->ToString(false);
	return Address.StartsWith(TEXT("127.")) || Address == TEXT("::1") || Address == TEXT("0:0:0:0:0:0:0:1");
}

bool FEpicodeAPIServer::CheckRateLimit(const FHttpServerRequest& Request) const
{
	const int32 MaxRequests = UEpicodeSettings::Get()->MaxRequestsPerSecond;
	if (MaxRequests <= 0 || !Request.PeerAddress.IsValid())
	{
		return true;
	}
	const FString Key = Request.PeerAddress->ToString(false);
	const double Now = FPlatformTime::Seconds();
	FScopeLock ScopeLock(&RateLock);
	if (RateWindows.Num() > 4096)
	{
		RateWindows.Reset();
	}
	FRateWindow& Window = RateWindows.FindOrAdd(Key);
	if (Window.WindowStart <= 0.0 || Now - Window.WindowStart >= 1.0)
	{
		Window.WindowStart = Now;
		Window.Count = 0;
	}
	++Window.Count;
	return Window.Count <= MaxRequests;
}

bool FEpicodeAPIServer::ParseBody(const FHttpServerRequest& Request, TSharedPtr<FJsonObject>& OutJson, FString& OutError) const
{
	if (Request.Body.Num() > UEpicodeSettings::Get()->MaxRequestBytes)
	{
		OutError = TEXT("Request body exceeds the configured limit.");
		return false;
	}
	if (Request.Body.Num() == 0)
	{
		OutJson = MakeShared<FJsonObject>();
		return true;
	}
	const FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
	const FString Text(Converter.Length(), Converter.Get());
	OutJson = ParseJsonObject(Text, OutError);
	return OutJson.IsValid();
}

TSharedPtr<FJsonObject> FEpicodeAPIServer::BuildEnvelope(const FString& Method, const FString& RequestId, const FEpicodeCommandResult& Result, int32 Status, bool bReplay) const
{
	uint64 EventSequence = 0;
	if (UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get())
	{
		if (FEpicodeEventBus* EventBus = EngineSubsystem->GetEventBus())
		{
			EventSequence = EventBus->GetLatestSequence();
		}
	}
	return EpicodeCommandEnvelope::Build(Method, RequestId, Result, Status, bReplay, EventSequence);
}

TSharedPtr<FJsonObject> FEpicodeAPIServer::BuildErrorEnvelope(const FString& Method, const FString& RequestId, const FString& Code, const FString& Message) const
{
	FEpicodeCommandResult Result = FEpicodeCommandResult::Failure(Code, Message);
	return BuildEnvelope(Method, RequestId, Result, HttpStatusForResult(Result), false);
}

TSharedPtr<FJsonObject> FEpicodeAPIServer::ExecuteCommand(const FString& Method, const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context, FEpicodeCommandResult& OutResult, int32& OutStatus) const
{
	UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get();
	if (!EngineSubsystem || !EngineSubsystem->GetCommandRegistry())
	{
		OutResult = FEpicodeCommandResult::Failure(TEXT("NOT_INITIALIZED"), TEXT("Epicode engine subsystem is unavailable."));
		OutStatus = HttpStatusForResult(OutResult);
		return BuildEnvelope(Method, Context.RequestId, OutResult, OutStatus, false);
	}
	FString UnknownMethod;
	if (!EngineSubsystem->GetCommandRegistry()->Execute(Method, Params, Context, OutResult, UnknownMethod))
	{
		OutResult = FEpicodeCommandResult::Failure(TEXT("METHOD_NOT_FOUND"), FString::Printf(TEXT("Unknown method: %s"), *Method));
		OutStatus = HttpStatusForResult(OutResult);
		return BuildEnvelope(Method, Context.RequestId, OutResult, OutStatus, false);
	}
	OutStatus = HttpStatusForResult(OutResult);
	return BuildEnvelope(Method, Context.RequestId, OutResult, OutStatus, false);
}

bool FEpicodeAPIServer::HandleHealth(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	if (!CheckRemote(Request))
	{
		OnComplete(MakeJsonResponse(403, BuildErrorEnvelope(TEXT("health"), MakeRequestId(), TEXT("REMOTE_FORBIDDEN"), TEXT("Remote access is disabled."))));
		return true;
	}
	if (!CheckRateLimit(Request))
	{
		OnComplete(MakeJsonResponse(429, BuildErrorEnvelope(TEXT("health"), MakeRequestId(), TEXT("RATE_LIMITED"), TEXT("Too many requests."))));
		return true;
	}
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	const FEpicodeDiagnosticsResult Diagnostics = FEpicodeDiagnostics::Run();
	Json->SetStringField(TEXT("status"), Diagnostics.bOk ? TEXT("ok") : TEXT("degraded"));
	Json->SetBoolField(TEXT("ok"), Diagnostics.bOk);
	Json->SetBoolField(TEXT("degraded"), Diagnostics.bDegraded);
	Json->SetStringField(TEXT("engineVersion"), Diagnostics.EngineVersion);
	Json->SetStringField(TEXT("projectVersion"), Diagnostics.ProjectVersion);
	Json->SetStringField(TEXT("renderBackend"), Diagnostics.RenderBackend);
	TArray<TSharedPtr<FJsonValue>> Messages;
	for (const FString& Message : Diagnostics.Messages)
	{
		Messages.Add(MakeShared<FJsonValueString>(Message));
	}
	Json->SetArrayField(TEXT("messages"), Messages);
	UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
	Json->SetBoolField(TEXT("sceneOpen"), Scene && Scene->HasScene());
	Json->SetStringField(TEXT("sceneId"), Scene ? Scene->GetCurrentSceneId() : FString());
	Json->SetBoolField(TEXT("sceneDirty"), Scene ? Scene->IsDirty() : false);
	const UEpicodeSettings* Settings = UEpicodeSettings::Get();
	Json->SetNumberField(TEXT("httpPort"), Settings->ApiHttpPort);
	Json->SetNumberField(TEXT("wsPort"), Settings->ApiWsPort);
	Json->SetNumberField(TEXT("uptimeSeconds"), (FDateTime::UtcNow() - StartedAt).GetTotalSeconds());
	if (UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get())
	{
		if (FEpicodeCommandRegistry* Registry = EngineSubsystem->GetCommandRegistry())
		{
			Json->SetNumberField(TEXT("commandCount"), Registry->GetMethodNames().Num());
		}
		if (FEpicodeEventBus* EventBus = EngineSubsystem->GetEventBus())
		{
			Json->SetNumberField(TEXT("eventSequence"), static_cast<double>(EventBus->GetLatestSequence()));
		}
	}
	if (const TSharedPtr<FJsonObject> Network = FEpicodeDiagnostics::GetNetworkStatus())
	{
		Json->SetObjectField(TEXT("network"), Network);
	}
	OnComplete(MakeJsonResponse(200, Json));
	return true;
}

bool FEpicodeAPIServer::HandleMethods(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	if (!CheckRemote(Request))
	{
		OnComplete(MakeJsonResponse(403, BuildErrorEnvelope(TEXT("methods"), MakeRequestId(), TEXT("REMOTE_FORBIDDEN"), TEXT("Remote access is disabled."))));
		return true;
	}
	if (!CheckRateLimit(Request))
	{
		OnComplete(MakeJsonResponse(429, BuildErrorEnvelope(TEXT("methods"), MakeRequestId(), TEXT("RATE_LIMITED"), TEXT("Too many requests."))));
		return true;
	}
	if (!CheckAuth(Request))
	{
		OnComplete(MakeJsonResponse(401, BuildErrorEnvelope(TEXT("methods"), MakeRequestId(), TEXT("UNAUTHORIZED"), TEXT("A valid bearer token is required."))));
		return true;
	}
	UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get();
	if (!EngineSubsystem || !EngineSubsystem->GetCommandRegistry())
	{
		OnComplete(MakeJsonResponse(503, BuildErrorEnvelope(TEXT("methods"), MakeRequestId(), TEXT("NOT_INITIALIZED"), TEXT("Engine subsystem is not ready."))));
		return true;
	}
	TSharedPtr<FJsonObject> Json = EngineSubsystem->GetCommandRegistry()->DescribeMethods();
	Json->SetNumberField(TEXT("count"), EngineSubsystem->GetCommandRegistry()->GetMethodNames().Num());
	OnComplete(MakeJsonResponse(200, Json));
	return true;
}

bool FEpicodeAPIServer::HandleCommand(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const FString RequestIdHeader = [&Request]()
	{
		FString Value;
		GetHeader(Request, TEXT("X-Request-Id"), Value);
		return Value;
	}();
	const FString RequestId = RequestIdHeader.IsEmpty() ? MakeRequestId() : RequestIdHeader.Left(128);
	if (!CheckRemote(Request))
	{
		OnComplete(MakeJsonResponse(403, BuildErrorEnvelope(TEXT("unknown"), RequestId, TEXT("REMOTE_FORBIDDEN"), TEXT("Remote access is disabled."))));
		return true;
	}
	if (!CheckRateLimit(Request))
	{
		OnComplete(MakeJsonResponse(429, BuildErrorEnvelope(TEXT("unknown"), RequestId, TEXT("RATE_LIMITED"), TEXT("Too many requests."))));
		return true;
	}
	if (!CheckAuth(Request))
	{
		OnComplete(MakeJsonResponse(401, BuildErrorEnvelope(TEXT("unknown"), RequestId, TEXT("UNAUTHORIZED"), TEXT("A valid bearer token is required."))));
		return true;
	}
	FString ContentType;
	GetHeader(Request, TEXT("Content-Type"), ContentType);
	if (!ContentType.IsEmpty() && !ContentType.StartsWith(TEXT("application/json"), ESearchCase::IgnoreCase))
	{
		OnComplete(MakeJsonResponse(415, BuildErrorEnvelope(TEXT("unknown"), RequestId, TEXT("UNSUPPORTED_MEDIA_TYPE"), TEXT("Content-Type must be application/json."))));
		return true;
	}
	if (Request.Body.Num() > UEpicodeSettings::Get()->MaxRequestBytes)
	{
		OnComplete(MakeJsonResponse(413, BuildErrorEnvelope(TEXT("unknown"), RequestId, TEXT("BODY_TOO_LARGE"), TEXT("Request body exceeds the configured limit."))));
		return true;
	}
	FString BodyError;
	TSharedPtr<FJsonObject> Body;
	if (!ParseBody(Request, Body, BodyError))
	{
		OnComplete(MakeJsonResponse(400, BuildErrorEnvelope(TEXT("unknown"), RequestId, TEXT("INVALID_BODY"), BodyError)));
		return true;
	}
	FString Method;
	if (!Request.PathParams.Contains(TEXT("method")))
	{
		EpicodeJson::GetOptionalString(Body, TEXT("method"), Method);
	}
	else
	{
		Method = Request.PathParams.FindRef(TEXT("method"));
	}
	if (Method.IsEmpty())
	{
		OnComplete(MakeJsonResponse(400, BuildErrorEnvelope(TEXT("unknown"), RequestId, TEXT("INVALID_PARAMS"), TEXT("method is required."))));
		return true;
	}
	TSharedPtr<FJsonObject> Params;
	const TSharedPtr<FJsonObject>* ParamsObject = nullptr;
	if (Body->TryGetObjectField(TEXT("params"), ParamsObject) && ParamsObject)
	{
		Params = *ParamsObject;
	}
	else if (Request.PathParams.Contains(TEXT("method")))
	{
		Params = Body;
	}
	else
	{
		Params = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& Pair : Request.QueryParams)
		{
			Params->SetStringField(Pair.Key, Pair.Value);
		}
	}
	FString IdempotencyKey;
	GetHeader(Request, TEXT("Idempotency-Key"), IdempotencyKey);
	if (IdempotencyKey.IsEmpty())
	{
		EpicodeJson::GetOptionalString(Body, TEXT("idempotencyKey"), IdempotencyKey);
	}
	FString ParticipantToken;
	GetHeader(Request, TEXT("X-Epicode-Participant"), ParticipantToken);
	if (ParticipantToken.IsEmpty())
	{
		EpicodeJson::GetOptionalString(Body, TEXT("participantToken"), ParticipantToken);
	}
	FEpicodeCommandContext Context;
	Context.World = UEpicodeSceneSubsystem::GetPrimaryWorld();
	Context.RequestId = RequestId;
	Context.Source = TEXT("http");
	Context.IdempotencyKey = IdempotencyKey;
	double ExpectedStateVersion = -1.0;
	EpicodeJson::GetOptionalNumber(Body, TEXT("expectedStateVersion"), ExpectedStateVersion, -1.0);
	Context.ExpectedStateVersion = ExpectedStateVersion;
	if (!ParticipantToken.IsEmpty())
	{
		FString BindingCode;
		FString BindingMessage;
		if (!EpicodeParticipantBinding::Bind(ParticipantToken, Params, Context, BindingCode, BindingMessage))
		{
			OnComplete(MakeJsonResponse(403, BuildErrorEnvelope(Method, RequestId, BindingCode, BindingMessage)));
			return true;
		}
	}
	const FString CacheKey = Method + TEXT("|") + ParticipantToken + TEXT("|") + IdempotencyKey;
	const FString RequestSignature = Method + TEXT("|") + SerializeJson(Params.IsValid() ? Params : MakeShared<FJsonObject>());
	if (!IdempotencyKey.IsEmpty())
	{
		FScopeLock ScopeLock(&Lock);
		if (const FIdempotentEntry* Entry = IdempotencyCache.Find(CacheKey))
		{
			if (Entry->ExpiresAt > FDateTime::UtcNow())
			{
				if (Entry->Signature != RequestSignature)
				{
					OnComplete(MakeJsonResponse(409, BuildErrorEnvelope(Method, RequestId, TEXT("IDEMPOTENCY_CONFLICT"), TEXT("The idempotency key was already used with different parameters."))));
					return true;
				}
				OnComplete(MakeJsonResponse(200, BuildEnvelope(Method, RequestId, Entry->Result, 200, true)));
				return true;
			}
		}
	}
	if (Context.ExpectedStateVersion >= 0.0)
	{
		UEpicodeNetworkSubsystem* Network = UEpicodeNetworkSubsystem::Get();
		const FEpicodeNetworkSession* Session = Network ? Network->GetSession() : nullptr;
		if (Session && !EpicodeCommandEnvelope::MatchesStateVersion(Context.ExpectedStateVersion, Session->GetWorldVersion()))
		{
			const FEpicodeCommandResult Stale = FEpicodeCommandResult::Failure(
				TEXT("STALE_STATE"),
				FString::Printf(TEXT("expectedStateVersion %llu does not match the current state version %llu."),
					static_cast<unsigned long long>(Context.ExpectedStateVersion),
					static_cast<unsigned long long>(Session->GetWorldVersion())),
				FString(),
				true);
			OnComplete(MakeJsonResponse(409, BuildEnvelope(Method, RequestId, Stale, 409, false)));
			return true;
		}
	}
	FEpicodeCommandResult Result;
	int32 Status = 200;
	TSharedPtr<FJsonObject> Envelope = ExecuteCommand(Method, Params, Context, Result, Status);
	if (!ParticipantToken.IsEmpty())
	{
		FEpicodeAgentAuditEntry Audit;
		Audit.Method = Method;
		Audit.RequestId = RequestId;
		EpicodeJson::GetOptionalString(Params, TEXT("playerId"), Audit.ParticipantId);
		Audit.Source = Context.Source;
		Audit.bOk = Result.bSuccess;
		Audit.HttpStatus = Status;
		Audit.ErrorCode = Result.ErrorCode;
		if (Result.Result.IsValid())
		{
			Result.Result->TryGetNumberField(TEXT("stateVersion"), Audit.StateVersion);
		}
		double AuditEventSequence = 0.0;
		if (Envelope.IsValid() && Envelope->TryGetNumberField(TEXT("eventSequence"), AuditEventSequence))
		{
			Audit.EventSequence = static_cast<uint64>(AuditEventSequence);
		}
		FEpicodeAgentAuditLog::Get().Record(Audit);
	}
	if (!IdempotencyKey.IsEmpty() && Result.bSuccess)
	{
		FScopeLock ScopeLock(&Lock);
		if (IdempotencyCache.Num() >= MaxIdempotencyEntries)
		{
			IdempotencyCache.Empty();
		}
		FIdempotentEntry Entry;
		Entry.Result = Result;
		Entry.ExpiresAt = FDateTime::UtcNow() + FTimespan::FromSeconds(IdempotencyTtlSeconds);
		Entry.Signature = RequestSignature;
		IdempotencyCache.Add(CacheKey, Entry);
		bIdempotencyDirty = true;
	}
	OnComplete(MakeJsonResponse(Status, Envelope));
	return true;
}

bool FEpicodeAPIServer::HandleBatch(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const FString RequestId = MakeRequestId();
	if (!CheckRemote(Request))
	{
		OnComplete(MakeJsonResponse(403, BuildErrorEnvelope(TEXT("batch"), RequestId, TEXT("REMOTE_FORBIDDEN"), TEXT("Remote access is disabled."))));
		return true;
	}
	if (!CheckRateLimit(Request))
	{
		OnComplete(MakeJsonResponse(429, BuildErrorEnvelope(TEXT("batch"), RequestId, TEXT("RATE_LIMITED"), TEXT("Too many requests."))));
		return true;
	}
	if (!CheckAuth(Request))
	{
		OnComplete(MakeJsonResponse(401, BuildErrorEnvelope(TEXT("batch"), RequestId, TEXT("UNAUTHORIZED"), TEXT("A valid bearer token is required."))));
		return true;
	}
	FString ContentType;
	GetHeader(Request, TEXT("Content-Type"), ContentType);
	if (!ContentType.IsEmpty() && !ContentType.StartsWith(TEXT("application/json"), ESearchCase::IgnoreCase))
	{
		OnComplete(MakeJsonResponse(415, BuildErrorEnvelope(TEXT("batch"), RequestId, TEXT("UNSUPPORTED_MEDIA_TYPE"), TEXT("Content-Type must be application/json."))));
		return true;
	}
	if (Request.Body.Num() > UEpicodeSettings::Get()->MaxRequestBytes)
	{
		OnComplete(MakeJsonResponse(413, BuildErrorEnvelope(TEXT("batch"), RequestId, TEXT("BODY_TOO_LARGE"), TEXT("Request body exceeds the configured limit."))));
		return true;
	}
	FString BodyError;
	TSharedPtr<FJsonObject> Body;
	if (!ParseBody(Request, Body, BodyError))
	{
		OnComplete(MakeJsonResponse(400, BuildErrorEnvelope(TEXT("batch"), RequestId, TEXT("INVALID_BODY"), BodyError)));
		return true;
	}
	const TArray<TSharedPtr<FJsonValue>> Commands = EpicodeJson::GetOptionalArray(Body, TEXT("commands"));
	if (Commands.Num() == 0 || Commands.Num() > MaxBatchCommands)
	{
		OnComplete(MakeJsonResponse(400, BuildErrorEnvelope(TEXT("batch"), RequestId, TEXT("INVALID_PARAMS"), FString::Printf(TEXT("commands must contain 1..%d entries."), MaxBatchCommands))));
		return true;
	}
	TArray<TSharedPtr<FJsonValue>> Results;
	bool bAllOk = true;
	int32 LastStatus = 200;
	for (const TSharedPtr<FJsonValue>& Value : Commands)
	{
		const TSharedPtr<FJsonObject> Command = Value->AsObject();
		if (!Command.IsValid())
		{
			bAllOk = false;
			LastStatus = 400;
			Results.Add(MakeShared<FJsonValueObject>(BuildErrorEnvelope(TEXT("batch"), RequestId, TEXT("INVALID_PARAMS"), TEXT("Each command must be an object."))));
			continue;
		}
		FString Method;
		EpicodeJson::GetOptionalString(Command, TEXT("method"), Method);
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		const TSharedPtr<FJsonObject>* ParamsObject = nullptr;
		if (Command->TryGetObjectField(TEXT("params"), ParamsObject) && ParamsObject)
		{
			Params = *ParamsObject;
		}
		FEpicodeCommandContext Context;
		Context.World = UEpicodeSceneSubsystem::GetPrimaryWorld();
		Context.RequestId = FString::Printf(TEXT("%s-%d"), *RequestId, Results.Num());
		Context.Source = TEXT("http-batch");
		FEpicodeCommandResult Result;
		int32 Status = 200;
		TSharedPtr<FJsonObject> Envelope = ExecuteCommand(Method, Params, Context, Result, Status);
		if (!Result.bSuccess)
		{
			bAllOk = false;
			LastStatus = Status;
		}
		Results.Add(MakeShared<FJsonValueObject>(Envelope));
	}
	TSharedPtr<FJsonObject> Envelope = MakeShared<FJsonObject>();
	Envelope->SetBoolField(TEXT("ok"), bAllOk);
	Envelope->SetStringField(TEXT("requestId"), RequestId);
	Envelope->SetArrayField(TEXT("results"), Results);
	OnComplete(MakeJsonResponse(bAllOk ? 200 : LastStatus, Envelope));
	return true;
}

bool FEpicodeAPIServer::HandleRpc(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	if (!CheckRemote(Request))
	{
		OnComplete(MakeRawJsonResponse(403, SerializeJson(MakeRpcError(nullptr, -32001, TEXT("Remote access is disabled."), TEXT("REMOTE_FORBIDDEN")))));
		return true;
	}
	if (!CheckRateLimit(Request))
	{
		OnComplete(MakeRawJsonResponse(429, SerializeJson(MakeRpcError(nullptr, -32000, TEXT("Too many requests."), TEXT("RATE_LIMITED")))));
		return true;
	}
	if (!CheckAuth(Request))
	{
		OnComplete(MakeRawJsonResponse(401, SerializeJson(MakeRpcError(nullptr, -32001, TEXT("A valid bearer token is required."), TEXT("UNAUTHORIZED")))));
		return true;
	}
	if (Request.Body.Num() > UEpicodeSettings::Get()->MaxRequestBytes)
	{
		OnComplete(MakeRawJsonResponse(413, SerializeJson(MakeRpcError(nullptr, -32600, TEXT("Request body exceeds the configured limit."), TEXT("INVALID_REQUEST")))));
		return true;
	}
	FString Body;
	if (Request.Body.Num() > 0)
	{
		const FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
		Body = FString(Converter.Length(), Converter.Get());
	}
	TSharedPtr<FJsonValue> Root;
	FString ParseError;
	if (!ParseJsonValue(Body, Root, ParseError))
	{
		OnComplete(MakeRawJsonResponse(200, SerializeJson(MakeRpcError(nullptr, -32700, ParseError, TEXT("PARSE_ERROR")))));
		return true;
	}

	auto ProcessRequest = [this](const TSharedPtr<FJsonObject>& RpcRequest, TSharedPtr<FJsonObject>& OutResponse) -> bool
	{
		TSharedPtr<FJsonValue> IdValue = RpcRequest->TryGetField(TEXT("id"));
		const bool bHasId = IdValue.IsValid();
		FString Version;
		RpcRequest->TryGetStringField(TEXT("jsonrpc"), Version);
		if (!Version.IsEmpty() && Version != TEXT("2.0"))
		{
			OutResponse = MakeRpcError(IdValue, -32600, TEXT("jsonrpc must be \"2.0\"."), TEXT("INVALID_REQUEST"));
			return true;
		}
		FString Method;
		if (!RpcRequest->TryGetStringField(TEXT("method"), Method) || Method.IsEmpty())
		{
			OutResponse = MakeRpcError(IdValue, -32600, TEXT("method is required."), TEXT("INVALID_REQUEST"));
			return true;
		}
		TSharedPtr<FJsonValue> ParamsValue = RpcRequest->TryGetField(TEXT("params"));
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		if (ParamsValue.IsValid())
		{
			if (ParamsValue->Type != EJson::Object)
			{
				OutResponse = MakeRpcError(IdValue, -32602, TEXT("params must be an object."), TEXT("INVALID_PARAMS"));
				return true;
			}
			Params = ParamsValue->AsObject();
		}
		FEpicodeCommandContext Context;
		Context.World = UEpicodeSceneSubsystem::GetPrimaryWorld();
		Context.RequestId = IdValue.IsValid() && IdValue->Type == EJson::String ? IdValue->AsString() : MakeRequestId();
		Context.Source = TEXT("jsonrpc");
		FEpicodeCommandResult Result;
		int32 Status = 200;
		ExecuteCommand(Method, Params, Context, Result, Status);
		if (!bHasId)
		{
			OutResponse.Reset();
			return false;
		}
		OutResponse = MakeShared<FJsonObject>();
		OutResponse->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
		OutResponse->SetField(TEXT("id"), IdValue.IsValid() ? IdValue : MakeShared<FJsonValueNull>());
		if (Result.bSuccess)
		{
			OutResponse->SetObjectField(TEXT("result"), Result.Result.IsValid() ? Result.Result : MakeShared<FJsonObject>());
			TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> Affected;
			for (const FString& AffectedId : Result.AffectedIds)
			{
				Affected.Add(MakeShared<FJsonValueString>(AffectedId));
			}
			Meta->SetArrayField(TEXT("affected"), Affected);
			TArray<TSharedPtr<FJsonValue>> Warnings;
			for (const FString& Warning : Result.Warnings)
			{
				Warnings.Add(MakeShared<FJsonValueString>(Warning));
			}
			Meta->SetArrayField(TEXT("warnings"), Warnings);
			if (UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get())
			{
				if (FEpicodeEventBus* EventBus = EngineSubsystem->GetEventBus())
				{
					Meta->SetNumberField(TEXT("eventSequence"), static_cast<double>(EventBus->GetLatestSequence()));
				}
			}
			OutResponse->SetObjectField(TEXT("_meta"), Meta);
		}
		else
		{
			int32 Code = -32000;
			if (Result.ErrorCode == TEXT("METHOD_NOT_FOUND"))
			{
				Code = -32601;
			}
			else if (Result.ErrorCode.StartsWith(TEXT("INVALID_")))
			{
				Code = -32602;
			}
			else if (Result.ErrorCode == TEXT("NOT_INITIALIZED") || Result.ErrorCode == TEXT("INTERNAL_ERROR"))
			{
				Code = -32603;
			}
			TSharedPtr<FJsonObject> Error = MakeRpcError(IdValue, Code, Result.ErrorMessage, Result.ErrorCode, Result.ErrorDetail);
			OutResponse = Error;
		}
		return true;
	};

	if (Root->Type == EJson::Array)
	{
		const TArray<TSharedPtr<FJsonValue>>& Batch = Root->AsArray();
		if (Batch.Num() == 0 || Batch.Num() > MaxBatchCommands)
		{
			OnComplete(MakeRawJsonResponse(200, SerializeJson(MakeRpcError(nullptr, -32600, FString::Printf(TEXT("Batch must contain 1..%d requests."), MaxBatchCommands), TEXT("INVALID_REQUEST")))));
			return true;
		}
		TArray<TSharedPtr<FJsonValue>> Responses;
		for (const TSharedPtr<FJsonValue>& Value : Batch)
		{
			TSharedPtr<FJsonObject> Response;
			if (!Value.IsValid() || Value->Type != EJson::Object)
			{
				Responses.Add(MakeShared<FJsonValueObject>(MakeRpcError(nullptr, -32600, TEXT("Each batch entry must be an object."), TEXT("INVALID_REQUEST"))));
				continue;
			}
			if (ProcessRequest(Value->AsObject(), Response) && Response.IsValid())
			{
				Responses.Add(MakeShared<FJsonValueObject>(Response));
			}
		}
		if (Responses.Num() == 0)
		{
			OnComplete(MakeRawJsonResponse(204, FString()));
			return true;
		}
		OnComplete(MakeRawJsonResponse(200, SerializeJsonArray(Responses)));
		return true;
	}
	if (Root->Type != EJson::Object)
	{
		OnComplete(MakeRawJsonResponse(200, SerializeJson(MakeRpcError(nullptr, -32600, TEXT("Request must be an object or an array."), TEXT("INVALID_REQUEST")))));
		return true;
	}
	TSharedPtr<FJsonObject> Response;
	if (!ProcessRequest(Root->AsObject(), Response) || !Response.IsValid())
	{
		OnComplete(MakeRawJsonResponse(204, FString()));
		return true;
	}
	OnComplete(MakeRawJsonResponse(200, SerializeJson(Response)));
	return true;
}

bool FEpicodeAPIServer::HandleWsTicket(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const FString RequestId = MakeRequestId();
	if (!CheckRemote(Request))
	{
		OnComplete(MakeJsonResponse(403, BuildErrorEnvelope(TEXT("ws-ticket"), RequestId, TEXT("REMOTE_FORBIDDEN"), TEXT("Remote access is disabled."))));
		return true;
	}
	if (!CheckRateLimit(Request))
	{
		OnComplete(MakeJsonResponse(429, BuildErrorEnvelope(TEXT("ws-ticket"), RequestId, TEXT("RATE_LIMITED"), TEXT("Too many requests."))));
		return true;
	}
	if (!CheckAuth(Request))
	{
		OnComplete(MakeJsonResponse(401, BuildErrorEnvelope(TEXT("ws-ticket"), RequestId, TEXT("UNAUTHORIZED"), TEXT("A valid bearer token is required."))));
		return true;
	}
	const FString Ticket = IssueTicket();
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("ticket"), Ticket);
	Json->SetStringField(TEXT("url"), UEpicodeSettings::Get()->GetWebSocketUrl());
	Json->SetStringField(TEXT("expiresAt"), (FDateTime::UtcNow() + FTimespan::FromSeconds(TicketTtlSeconds)).ToIso8601());
	Json->SetNumberField(TEXT("expiresInSeconds"), TicketTtlSeconds);
	OnComplete(MakeJsonResponse(200, Json));
	return true;
}

bool FEpicodeAPIServer::HandleOptions(const FHttpServerRequest&, const FHttpResultCallback& OnComplete)
{
	OnComplete(MakeCorsResponse());
	return true;
}

void FEpicodeAPIServer::HandleWsConnected(TSharedRef<FEpicodeWebSocketConnection> Connection)
{
	const FString Path = Connection->GetPath();
	Connection->SetEventFilter(FEpicodeEventFilter::FromQueryString(Path));
	TSharedPtr<FJsonObject> Welcome = MakeShared<FJsonObject>();
	Welcome->SetStringField(TEXT("type"), TEXT("welcome"));
	Welcome->SetStringField(TEXT("apiVersion"), TEXT("v1"));
	Welcome->SetNumberField(TEXT("ticketExpiresInSeconds"), 0);
	if (UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get())
	{
		if (FEpicodeEventBus* EventBus = EngineSubsystem->GetEventBus())
		{
			Welcome->SetNumberField(TEXT("sequence"), static_cast<double>(EventBus->GetLatestSequence()));
		}
	}
	Connection->SendText(SerializeJson(Welcome));

	FString SinceText = ExtractQueryParam(Path, TEXT("since"));
	if (!SinceText.IsEmpty())
	{
		const uint64 Since = FCString::Strtoui64(*SinceText, nullptr, 10);
		if (UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get())
		{
			if (FEpicodeEventBus* EventBus = EngineSubsystem->GetEventBus())
			{
				const uint64 Oldest = EventBus->GetOldestSequence();
				if (FEpicodeEventFilter::NeedsGap(Since, Oldest))
				{
					TSharedPtr<FJsonObject> Gap = MakeShared<FJsonObject>();
					Gap->SetStringField(TEXT("type"), TEXT("events.gap"));
					Gap->SetNumberField(TEXT("requestedSince"), static_cast<double>(Since));
					Gap->SetNumberField(TEXT("oldestSequence"), static_cast<double>(Oldest));
					Gap->SetNumberField(TEXT("latestSequence"), static_cast<double>(EventBus->GetLatestSequence()));
					Connection->SendText(SerializeJson(Gap));
				}
				for (const FEpicodeEvent& Event : EventBus->GetSince(Since, 256))
				{
					if (!Connection->GetEventFilter().Matches(Event))
					{
						continue;
					}
					TSharedPtr<FJsonObject> Envelope = MakeShared<FJsonObject>();
					Envelope->SetStringField(TEXT("type"), TEXT("event"));
					Envelope->SetObjectField(TEXT("event"), Event.ToJson());
					Connection->SendText(SerializeJson(Envelope));
				}
			}
		}
	}
}

void FEpicodeAPIServer::HandleWsMessage(TSharedRef<FEpicodeWebSocketConnection> Connection, const FString& Message)
{
	FString Error;
	TSharedPtr<FJsonObject> Json = ParseJsonObject(Message, Error);
	if (!Json.IsValid())
	{
		TSharedPtr<FJsonObject> Envelope = MakeShared<FJsonObject>();
		Envelope->SetStringField(TEXT("type"), TEXT("response"));
		Envelope->SetBoolField(TEXT("ok"), false);
		TSharedPtr<FJsonObject> ErrorObject = MakeShared<FJsonObject>();
		ErrorObject->SetStringField(TEXT("code"), TEXT("INVALID_BODY"));
		ErrorObject->SetStringField(TEXT("message"), Error);
		Envelope->SetObjectField(TEXT("error"), ErrorObject);
		Connection->SendText(SerializeJson(Envelope));
		return;
	}
	FString Type;
	EpicodeJson::GetOptionalString(Json, TEXT("type"), Type);
	if (Type.Equals(TEXT("ping"), ESearchCase::IgnoreCase))
	{
		TSharedPtr<FJsonObject> Pong = MakeShared<FJsonObject>();
		Pong->SetStringField(TEXT("type"), TEXT("pong"));
		if (UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get())
		{
			if (FEpicodeEventBus* EventBus = EngineSubsystem->GetEventBus())
			{
				Pong->SetNumberField(TEXT("sequence"), static_cast<double>(EventBus->GetLatestSequence()));
			}
		}
		Connection->SendText(SerializeJson(Pong));
		return;
	}
	HandleWsCommand(Connection, Json);
}

void FEpicodeAPIServer::HandleWsCommand(TSharedRef<FEpicodeWebSocketConnection> Connection, const TSharedPtr<FJsonObject>& Message)
{
	FString Method;
	EpicodeJson::GetOptionalString(Message, TEXT("method"), Method);
	FString Id;
	EpicodeJson::GetOptionalString(Message, TEXT("id"), Id);
	if (Method.IsEmpty())
	{
		TSharedPtr<FJsonObject> Envelope = MakeShared<FJsonObject>();
		Envelope->SetStringField(TEXT("type"), TEXT("response"));
		Envelope->SetStringField(TEXT("id"), Id);
		Envelope->SetBoolField(TEXT("ok"), false);
		TSharedPtr<FJsonObject> ErrorObject = MakeShared<FJsonObject>();
		ErrorObject->SetStringField(TEXT("code"), TEXT("INVALID_PARAMS"));
		ErrorObject->SetStringField(TEXT("message"), TEXT("method is required."));
		Envelope->SetObjectField(TEXT("error"), ErrorObject);
		Connection->SendText(SerializeJson(Envelope));
		return;
	}
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	const TSharedPtr<FJsonObject>* ParamsObject = nullptr;
	if (Message->TryGetObjectField(TEXT("params"), ParamsObject) && ParamsObject)
	{
		Params = *ParamsObject;
	}
	FString IdempotencyKey;
	EpicodeJson::GetOptionalString(Message, TEXT("idempotencyKey"), IdempotencyKey);
	FEpicodeCommandContext Context;
	Context.World = UEpicodeSceneSubsystem::GetPrimaryWorld();
	Context.RequestId = Id.IsEmpty() ? MakeRequestId() : Id;
	Context.Source = TEXT("websocket");
	Context.IdempotencyKey = IdempotencyKey;
	FString ParticipantToken;
	EpicodeJson::GetOptionalString(Message, TEXT("participantToken"), ParticipantToken);
	double ExpectedStateVersion = -1.0;
	EpicodeJson::GetOptionalNumber(Message, TEXT("expectedStateVersion"), ExpectedStateVersion, -1.0);
	Context.ExpectedStateVersion = ExpectedStateVersion;
	if (!ParticipantToken.IsEmpty())
	{
		FString BindingCode;
		FString BindingMessage;
		if (!EpicodeParticipantBinding::Bind(ParticipantToken, Params, Context, BindingCode, BindingMessage))
		{
			TSharedPtr<FJsonObject> Envelope = BuildErrorEnvelope(Method, Context.RequestId, BindingCode, BindingMessage);
			Envelope->SetStringField(TEXT("type"), TEXT("response"));
			if (!Id.IsEmpty())
			{
				Envelope->SetStringField(TEXT("id"), Id);
			}
			Connection->SendText(SerializeJson(Envelope));
			return;
		}
	}
	FEpicodeCommandResult Result;
	int32 Status = 200;
	TSharedPtr<FJsonObject> Envelope = ExecuteCommand(Method, Params, Context, Result, Status);
	Envelope->SetStringField(TEXT("type"), TEXT("response"));
	if (!Id.IsEmpty())
	{
		Envelope->SetStringField(TEXT("id"), Id);
	}
	Connection->SendText(SerializeJson(Envelope));
}

void FEpicodeAPIServer::HandleWsDisconnected(TSharedRef<FEpicodeWebSocketConnection> Connection)
{
	UE_LOG(LogEpicodeAPI, Verbose, TEXT("WebSocket client %llu disconnected"), Connection->GetId());
}

void FEpicodeAPIServer::BroadcastEvents()
{
	UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get();
	if (!EngineSubsystem)
	{
		return;
	}
	FEpicodeEventBus* EventBus = EngineSubsystem->GetEventBus();
	if (!EventBus)
	{
		return;
	}
	const TArray<FEpicodeEvent> Events = EventBus->GetSince(LastBroadcastSequence, 256);
	if (Events.Num() == 0)
	{
		return;
	}
	LastBroadcastSequence = Events.Last().Sequence;
	if (!WebSocketServer.IsValid())
	{
		return;
	}
	for (const FEpicodeEvent& Event : Events)
	{
		TSharedPtr<FJsonObject> Envelope = MakeShared<FJsonObject>();
		Envelope->SetStringField(TEXT("type"), TEXT("event"));
		Envelope->SetObjectField(TEXT("event"), Event.ToJson());
		const FString Message = SerializeJson(Envelope);
		WebSocketServer->BroadcastFiltered(Message, [&Event](const FEpicodeWebSocketConnection& Connection)
		{
			return Connection.GetEventFilter().Matches(Event);
		});
	}
}
