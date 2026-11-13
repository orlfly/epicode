#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "EpicodeEventFilter.h"
#include "Sockets.h"

class FEpicodeWebSocketConnection;
class FEpicodeWebSocketServer;

class FEpicodeWebSocketConnection : public FRunnable, public TSharedFromThis<FEpicodeWebSocketConnection>
{
public:
	FEpicodeWebSocketConnection(FSocket* InSocket, const FString& InPath, FEpicodeWebSocketServer* InServer);
	virtual ~FEpicodeWebSocketConnection() override;

	void Start();
	void Stop();
	bool SendText(const FString& Message);
	FString GetPath() const { return Path; }
	uint64 GetId() const { return Id; }
	bool IsClosed() const { return bClosed; }
	void SetEventFilter(const FEpicodeEventFilter& InFilter);
	FEpicodeEventFilter GetEventFilter() const;

	virtual uint32 Run() override;
	virtual void StopThread() { Stop(); }

private:
	bool PerformHandshake();
	bool ProcessFrames();
	bool SendFrame(uint8 Opcode, const uint8* Payload, int32 PayloadLength);
	bool SendAll(const uint8* Data, int32 Length);
	void NotifyClosed();

	FSocket* Socket = nullptr;
	FString Path;
	FEpicodeWebSocketServer* Server = nullptr;
	FRunnableThread* Thread = nullptr;
	TAtomic<bool> bStop { false };
	TAtomic<bool> bClosed { false };
	FCriticalSection SendLock;
	mutable FCriticalSection FilterLock;
	FEpicodeEventFilter EventFilter;
	TArray<uint8> ReceiveBuffer;
	TArray<uint8> FragmentBuffer;
	uint8 FragmentOpcode = 0;
	uint64 Id = 0;
};

class FEpicodeWebSocketServer : public FRunnable
{
public:
	using FPathValidator = TFunction<bool(const FString& Path)>;
	using FConnectedHandler = TFunction<void(TSharedRef<FEpicodeWebSocketConnection>)>;
	using FMessageHandler = TFunction<void(TSharedRef<FEpicodeWebSocketConnection>, const FString& Message)>;
	using FDisconnectHandler = TFunction<void(TSharedRef<FEpicodeWebSocketConnection>)>;
	using FConnectionFilter = TFunction<bool(const FEpicodeWebSocketConnection&)>;

	FEpicodeWebSocketServer();
	~FEpicodeWebSocketServer();

	bool Start(uint32 InPort, bool bInAllowRemote, FPathValidator InValidator, FConnectedHandler InConnectedHandler, FMessageHandler InMessageHandler, FDisconnectHandler InDisconnectHandler);
	void Stop();
	void Broadcast(const FString& Message);
	void BroadcastFiltered(const FString& Message, const FConnectionFilter& Filter);
	int32 GetClientCount() const;

	virtual uint32 Run() override;
	void OnConnectionOpened(const TSharedRef<FEpicodeWebSocketConnection>& Connection);
	void OnConnectionClosed(const TSharedRef<FEpicodeWebSocketConnection>& Connection);
	void OnMessageReceived(const TSharedRef<FEpicodeWebSocketConnection>& Connection, const FString& Message);
	bool ValidatePath(const FString& Path) const;

private:
	FSocket* Listener = nullptr;
	FRunnableThread* Thread = nullptr;
	TAtomic<bool> bStop { false };
	uint32 Port = 0;
	bool bAllowRemote = false;
	FPathValidator Validator;
	FConnectedHandler ConnectedHandler;
	FMessageHandler MessageHandler;
	FDisconnectHandler DisconnectHandler;
	mutable FCriticalSection ClientsLock;
	TArray<TSharedPtr<FEpicodeWebSocketConnection>> Clients;
};
