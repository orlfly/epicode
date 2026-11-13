#include "EpicodeWebSocketServer.h"

#include "Common/TcpSocketBuilder.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTLS.h"
#include "Misc/Base64.h"
#include "Misc/SecureHash.h"
#include "SocketSubsystem.h"

namespace
{
	const TCHAR* WebSocketGuid = TEXT("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
	constexpr int32 MaxFramePayload = 8 * 1024 * 1024;

	uint16 ReadUInt16BE(const uint8* Data)
	{
		return static_cast<uint16>(Data[0]) << 8 | static_cast<uint16>(Data[1]);
	}

	uint64 ReadUInt64BE(const uint8* Data)
	{
		uint64 Value = 0;
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Value = (Value << 8) | Data[Index];
		}
		return Value;
	}

	bool GetHttpHeader(const FString& Handshake, const FString& Name, FString& OutValue)
	{
		TArray<FString> Lines;
		Handshake.ParseIntoArray(Lines, TEXT("\r\n"), false);
		for (const FString& Line : Lines)
		{
			int32 ColonIndex = INDEX_NONE;
			if (Line.FindChar(TEXT(':'), ColonIndex))
			{
				if (Line.Left(ColonIndex).TrimStartAndEnd().Equals(Name, ESearchCase::IgnoreCase))
				{
					OutValue = Line.Mid(ColonIndex + 1).TrimStartAndEnd();
					return true;
				}
			}
		}
		return false;
	}
}

FEpicodeWebSocketConnection::FEpicodeWebSocketConnection(FSocket* InSocket, const FString& InPath, FEpicodeWebSocketServer* InServer)
	: Socket(InSocket)
	, Path(InPath)
	, Server(InServer)
{
	static TAtomic<uint64> NextId { 1 };
	Id = NextId++;
}

FEpicodeWebSocketConnection::~FEpicodeWebSocketConnection()
{
	if (Thread)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
	if (Socket)
	{
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
}

void FEpicodeWebSocketConnection::Start()
{
	if (Thread || !Socket)
	{
		return;
	}
	Thread = FRunnableThread::Create(this, TEXT("EpicodeWebSocketClient"), 256 * 1024, TPri_Normal);
}

void FEpicodeWebSocketConnection::Stop()
{
	bStop = true;
	if (Socket)
	{
		Socket->Shutdown(ESocketShutdownMode::ReadWrite);
	}
}

uint32 FEpicodeWebSocketConnection::Run()
{
	if (!PerformHandshake())
	{
		NotifyClosed();
		return 0;
	}
	Server->OnConnectionOpened(AsShared());
	Socket->SetNonBlocking(true);
	while (!bStop)
	{
		int32 BytesRead = 0;
		uint8 Buffer[8192];
		const bool bReceived = Socket->Recv(Buffer, sizeof(Buffer), BytesRead);
		if (bReceived && BytesRead > 0)
		{
			ReceiveBuffer.Append(Buffer, BytesRead);
			if (!ProcessFrames())
			{
				break;
			}
		}
		else if (!bReceived && BytesRead == 0)
		{
			FPlatformProcess::Sleep(0.002f);
		}
		else if (!bReceived)
		{
			const ESocketErrors Error = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLastErrorCode();
			if (Error != SE_EWOULDBLOCK && Error != SE_NO_ERROR)
			{
				break;
			}
			FPlatformProcess::Sleep(0.002f);
		}
	}
	NotifyClosed();
	return 0;
}

bool FEpicodeWebSocketConnection::PerformHandshake()
{
	Socket->SetNoDelay(true);
	Socket->SetNonBlocking(true);
	TArray<uint8> Buffer;
	int32 HeaderEnd = INDEX_NONE;
	const double Deadline = FPlatformTime::Seconds() + 5.0;
	while (!bStop && FPlatformTime::Seconds() < Deadline && HeaderEnd == INDEX_NONE)
	{
		int32 BytesRead = 0;
		uint8 Chunk[2048];
		if (Socket->Recv(Chunk, sizeof(Chunk), BytesRead) && BytesRead > 0)
		{
			Buffer.Append(Chunk, BytesRead);
			for (int32 Index = 0; Index + 3 < Buffer.Num(); ++Index)
			{
				if (Buffer[Index] == '\r' && Buffer[Index + 1] == '\n' && Buffer[Index + 2] == '\r' && Buffer[Index + 3] == '\n')
				{
					HeaderEnd = Index + 4;
					break;
				}
			}
		}
		else
		{
			FPlatformProcess::Sleep(0.002f);
		}
	}
	if (HeaderEnd == INDEX_NONE || HeaderEnd > 16384)
	{
		return false;
	}
	const FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Buffer.GetData()), HeaderEnd);
	const FString Handshake(Converter.Length(), Converter.Get());
	ReceiveBuffer.Append(Buffer.GetData() + HeaderEnd, Buffer.Num() - HeaderEnd);

	TArray<FString> RequestLines;
	Handshake.ParseIntoArray(RequestLines, TEXT("\r\n"), false);
	if (RequestLines.Num() == 0)
	{
		return false;
	}
	TArray<FString> RequestParts;
	RequestLines[0].ParseIntoArray(RequestParts, TEXT(" "), true);
	if (RequestParts.Num() < 2 || !RequestParts[0].Equals(TEXT("GET"), ESearchCase::IgnoreCase))
	{
		SendAll(reinterpret_cast<const uint8*>("HTTP/1.1 400 Bad Request\r\n\r\n"), 28);
		return false;
	}
	Path = RequestParts[1];
	if (Server && !Server->ValidatePath(Path))
	{
		const char* Response = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
		SendAll(reinterpret_cast<const uint8*>(Response), FCStringAnsi::Strlen(Response));
		return false;
	}
	FString Key;
	if (!GetHttpHeader(Handshake, TEXT("Sec-WebSocket-Key"), Key) || Key.IsEmpty())
	{
		const char* Response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
		SendAll(reinterpret_cast<const uint8*>(Response), FCStringAnsi::Strlen(Response));
		return false;
	}
	const FString AcceptSource = Key + WebSocketGuid;
	FTCHARToUTF8 AcceptUtf8(*AcceptSource);
	uint8 Digest[20];
	FSHA1::HashBuffer(AcceptUtf8.Get(), AcceptUtf8.Length(), Digest);
	const FString Accept = FBase64::Encode(Digest, 20);
	const FString Response = FString::Printf(
		TEXT("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n"),
		*Accept);
	FTCHARToUTF8 ResponseUtf8(*Response);
	return SendAll(reinterpret_cast<const uint8*>(ResponseUtf8.Get()), ResponseUtf8.Length());
}

bool FEpicodeWebSocketConnection::ProcessFrames()
{
	int32 Offset = 0;
	while (ReceiveBuffer.Num() - Offset >= 2)
	{
		const uint8* Data = ReceiveBuffer.GetData() + Offset;
		const bool bFin = (Data[0] & 0x80) != 0;
		const uint8 Opcode = Data[0] & 0x0F;
		const bool bMasked = (Data[1] & 0x80) != 0;
		uint64 PayloadLength = Data[1] & 0x7F;
		int32 HeaderLength = 2;
		if (PayloadLength == 126)
		{
			if (ReceiveBuffer.Num() - Offset < 4)
			{
				break;
			}
			PayloadLength = ReadUInt16BE(Data + 2);
			HeaderLength = 4;
		}
		else if (PayloadLength == 127)
		{
			if (ReceiveBuffer.Num() - Offset < 10)
			{
				break;
			}
			PayloadLength = ReadUInt64BE(Data + 2);
			HeaderLength = 10;
		}
		if (PayloadLength > static_cast<uint64>(MaxFramePayload))
		{
			return false;
		}
		if (bMasked)
		{
			HeaderLength += 4;
		}
		if (ReceiveBuffer.Num() - Offset < HeaderLength + static_cast<int32>(PayloadLength))
		{
			break;
		}
		const uint8* Mask = bMasked ? Data + HeaderLength - 4 : nullptr;
		const uint8* Payload = Data + HeaderLength;
		TArray<uint8> Unmasked;
		Unmasked.SetNumUninitialized(static_cast<int32>(PayloadLength));
		for (int32 Index = 0; Index < static_cast<int32>(PayloadLength); ++Index)
		{
			Unmasked[Index] = Payload[Index] ^ (Mask ? Mask[Index % 4] : 0);
		}
		Offset += HeaderLength + static_cast<int32>(PayloadLength);

		if (Opcode == 0x8)
		{
			SendFrame(0x8, nullptr, 0);
			bStop = true;
			break;
		}
		if (Opcode == 0x9)
		{
			SendFrame(0xA, Unmasked.GetData(), Unmasked.Num());
			continue;
		}
		if (Opcode == 0xA)
		{
			continue;
		}
		if (Opcode == 0x1 || Opcode == 0x0)
		{
			if (Opcode == 0x1)
			{
				FragmentBuffer.Reset();
				FragmentOpcode = Opcode;
			}
			FragmentBuffer.Append(Unmasked);
			if (bFin)
			{
				const FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(FragmentBuffer.GetData()), FragmentBuffer.Num());
				const FString Message(Converter.Length(), Converter.Get());
				Server->OnMessageReceived(AsShared(), Message);
				FragmentBuffer.Reset();
			}
		}
	}
	if (Offset > 0)
	{
		ReceiveBuffer.RemoveAt(0, Offset, EAllowShrinking::No);
	}
	return true;
}

bool FEpicodeWebSocketConnection::SendText(const FString& Message)
{
	const FTCHARToUTF8 Converter(*Message);
	return SendFrame(0x1, reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
}

void FEpicodeWebSocketConnection::SetEventFilter(const FEpicodeEventFilter& InFilter)
{
	FScopeLock ScopeLock(&FilterLock);
	EventFilter = InFilter;
}

FEpicodeEventFilter FEpicodeWebSocketConnection::GetEventFilter() const
{
	FScopeLock ScopeLock(&FilterLock);
	return EventFilter;
}

bool FEpicodeWebSocketConnection::SendFrame(uint8 Opcode, const uint8* Payload, int32 PayloadLength)
{
	FScopeLock ScopeLock(&SendLock);
	if (!Socket || bStop)
	{
		return false;
	}
	TArray<uint8> Frame;
	Frame.Add(0x80 | Opcode);
	if (PayloadLength <= 125)
	{
		Frame.Add(static_cast<uint8>(PayloadLength));
	}
	else if (PayloadLength <= 65535)
	{
		Frame.Add(126);
		Frame.Add(static_cast<uint8>((PayloadLength >> 8) & 0xFF));
		Frame.Add(static_cast<uint8>(PayloadLength & 0xFF));
	}
	else
	{
		Frame.Add(127);
		for (int32 Shift = 56; Shift >= 0; Shift -= 8)
		{
			Frame.Add(static_cast<uint8>((static_cast<uint64>(PayloadLength) >> Shift) & 0xFF));
		}
	}
	if (PayloadLength > 0 && Payload)
	{
		Frame.Append(Payload, PayloadLength);
	}
	return SendAll(Frame.GetData(), Frame.Num());
}

bool FEpicodeWebSocketConnection::SendAll(const uint8* Data, int32 Length)
{
	int32 Sent = 0;
	while (Sent < Length)
	{
		int32 ThisSend = 0;
		if (!Socket->Send(Data + Sent, Length - Sent, ThisSend) || ThisSend <= 0)
		{
			return false;
		}
		Sent += ThisSend;
	}
	return true;
}

void FEpicodeWebSocketConnection::NotifyClosed()
{
	if (bClosed.Exchange(true))
	{
		return;
	}
	if (Server)
	{
		Server->OnConnectionClosed(AsShared());
	}
}

FEpicodeWebSocketServer::FEpicodeWebSocketServer() = default;

FEpicodeWebSocketServer::~FEpicodeWebSocketServer()
{
	Stop();
}

bool FEpicodeWebSocketServer::Start(uint32 InPort, bool bInAllowRemote, FPathValidator InValidator, FConnectedHandler InConnectedHandler, FMessageHandler InMessageHandler, FDisconnectHandler InDisconnectHandler)
{
	Stop();
	Port = InPort;
	bAllowRemote = bInAllowRemote;
	Validator = MoveTemp(InValidator);
	ConnectedHandler = MoveTemp(InConnectedHandler);
	MessageHandler = MoveTemp(InMessageHandler);
	DisconnectHandler = MoveTemp(InDisconnectHandler);
	FTcpSocketBuilder Builder(TEXT("EpicodeWebSocketListener"));
	Builder.AsReusable();
	if (!bAllowRemote)
	{
		Builder.BoundToAddress(FIPv4Address::InternalLoopback);
	}
	Listener = Builder.BoundToPort(static_cast<uint16>(Port)).Listening(8);
	if (!Listener)
	{
		return false;
	}
	bStop = false;
	Thread = FRunnableThread::Create(this, TEXT("EpicodeWebSocketListener"), 256 * 1024, TPri_Normal);
	if (!Thread)
	{
		Listener->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Listener);
		Listener = nullptr;
		return false;
	}
	return true;
}

void FEpicodeWebSocketServer::Stop()
{
	if (bStop && !Listener && !Thread)
	{
		return;
	}
	bStop = true;
	if (Listener)
	{
		Listener->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Listener);
		Listener = nullptr;
	}
	if (Thread)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
	TArray<TSharedPtr<FEpicodeWebSocketConnection>> Snapshot;
	{
		FScopeLock ScopeLock(&ClientsLock);
		Snapshot = Clients;
		Clients.Reset();
	}
	for (const TSharedPtr<FEpicodeWebSocketConnection>& Client : Snapshot)
	{
		if (Client.IsValid())
		{
			Client->Stop();
		}
	}
}

uint32 FEpicodeWebSocketServer::Run()
{
	while (!bStop)
	{
		bool bPending = false;
		if (Listener && Listener->HasPendingConnection(bPending) && bPending)
		{
			FSocket* ClientSocket = Listener->Accept(TEXT("EpicodeWebSocketClient"));
			if (ClientSocket)
			{
				TSharedPtr<FEpicodeWebSocketConnection> Connection = MakeShared<FEpicodeWebSocketConnection>(ClientSocket, FString(), this);
				{
					FScopeLock ScopeLock(&ClientsLock);
					Clients.Add(Connection);
				}
				Connection->Start();
			}
		}
		FPlatformProcess::Sleep(0.01f);
	}
	return 0;
}

void FEpicodeWebSocketServer::Broadcast(const FString& Message)
{
	BroadcastFiltered(Message, [](const FEpicodeWebSocketConnection&) { return true; });
}

void FEpicodeWebSocketServer::BroadcastFiltered(const FString& Message, const FConnectionFilter& Filter)
{
	TArray<TSharedPtr<FEpicodeWebSocketConnection>> Snapshot;
	{
		FScopeLock ScopeLock(&ClientsLock);
		Clients.RemoveAll([](const TSharedPtr<FEpicodeWebSocketConnection>& Client)
		{
			return !Client.IsValid() || Client->IsClosed();
		});
		Snapshot = Clients;
	}
	for (const TSharedPtr<FEpicodeWebSocketConnection>& Client : Snapshot)
	{
		if (Client.IsValid() && (!Filter || Filter(*Client)))
		{
			Client->SendText(Message);
		}
	}
}

int32 FEpicodeWebSocketServer::GetClientCount() const
{
	FScopeLock ScopeLock(&ClientsLock);
	return Clients.Num();
}

bool FEpicodeWebSocketServer::ValidatePath(const FString& Path) const
{
	return Validator ? Validator(Path) : true;
}

void FEpicodeWebSocketServer::OnConnectionOpened(const TSharedRef<FEpicodeWebSocketConnection>& Connection)
{
	if (ConnectedHandler)
	{
		ConnectedHandler(Connection);
	}
}

void FEpicodeWebSocketServer::OnConnectionClosed(const TSharedRef<FEpicodeWebSocketConnection>& Connection)
{
	if (DisconnectHandler)
	{
		DisconnectHandler(Connection);
	}
}

void FEpicodeWebSocketServer::OnMessageReceived(const TSharedRef<FEpicodeWebSocketConnection>& Connection, const FString& Message)
{
	if (MessageHandler)
	{
		MessageHandler(Connection, Message);
	}
}
