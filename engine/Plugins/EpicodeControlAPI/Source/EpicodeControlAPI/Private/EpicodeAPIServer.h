#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "EpicodeCommand.h"
#include "HttpResultCallback.h"
#include "HttpRouteHandle.h"
#include "HttpServerRequest.h"
#include "EpicodeWebSocketServer.h"

class IHttpRouter;

class EPICODECONTROLAPI_API FEpicodeAPIServer
{
public:
	static FEpicodeAPIServer& Get();

	bool Start(FString& OutError);
	void Stop();
	bool IsRunning() const { return bRunning; }

private:
	bool Tick(float DeltaSeconds);
	void BroadcastEvents();
	void CleanupTickets();

	bool HandleHealth(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleMethods(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleCommand(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleBatch(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleRpc(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleWsTicket(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleOptions(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	void HandleWsConnected(TSharedRef<FEpicodeWebSocketConnection> Connection);
	void HandleWsMessage(TSharedRef<FEpicodeWebSocketConnection> Connection, const FString& Message);
	void HandleWsDisconnected(TSharedRef<FEpicodeWebSocketConnection> Connection);
	void HandleWsCommand(TSharedRef<FEpicodeWebSocketConnection> Connection, const TSharedPtr<FJsonObject>& Message);

	FString IssueTicket();
	bool ConsumeTicket(const FString& Ticket);
	bool CheckAuth(const FHttpServerRequest& Request) const;
	bool CheckRemote(const FHttpServerRequest& Request) const;
	bool CheckRateLimit(const FHttpServerRequest& Request) const;
	bool ParseBody(const FHttpServerRequest& Request, TSharedPtr<FJsonObject>& OutJson, FString& OutError) const;
	TSharedPtr<FJsonObject> ExecuteCommand(const FString& Method, const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context, FEpicodeCommandResult& OutResult, int32& OutStatus) const;
	TSharedPtr<FJsonObject> BuildEnvelope(const FString& Method, const FString& RequestId, const FEpicodeCommandResult& Result, int32 Status, bool bReplay) const;
	TSharedPtr<FJsonObject> BuildErrorEnvelope(const FString& Method, const FString& RequestId, const FString& Code, const FString& Message) const;
	void LoadIdempotencyCache();
	void SaveIdempotencyCache();

	struct FIdempotentEntry
	{
		FEpicodeCommandResult Result;
		FDateTime ExpiresAt;
		FString Signature;
	};

	struct FRateWindow
	{
		double WindowStart = 0.0;
		int32 Count = 0;
	};

	TSharedPtr<IHttpRouter> HttpRouter;
	TArray<FHttpRouteHandle> RouteHandles;
	TSharedPtr<FEpicodeWebSocketServer> WebSocketServer;
	TMap<FString, FDateTime> WsTickets;
	TMap<FString, FIdempotentEntry> IdempotencyCache;
	mutable TMap<FString, FRateWindow> RateWindows;
	mutable FCriticalSection RateLock;
	uint64 LastBroadcastSequence = 0;
	FDateTime StartedAt;
	FTSTicker::FDelegateHandle TickerHandle;
	mutable FCriticalSection Lock;
	bool bRunning = false;
	bool bIdempotencyDirty = false;
	double LastIdempotencySaveSeconds = 0.0;
};
