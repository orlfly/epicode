#include "EpicodeAssistantSidecar.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/ThreadSafeBool.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogEpicodeAssistantSidecar, Log, All);

constexpr int32 MaxSidecarLineBytes = 1024 * 1024;
constexpr int32 MaxStderrLines = 40;

	static FString QuoteArgument(const FString& Value)
	{
		FString Escaped = Value;
		Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
		return FString::Printf(TEXT("\"%s\""), *Escaped);
	}

	static FString ResolvePath(const FString& ProjectDir, const FString& Path)
	{
		if (Path.IsEmpty())
		{
			return Path;
		}
		if (FPaths::IsRelative(Path))
		{
			return FPaths::ConvertRelativePathToFull(FPaths::Combine(ProjectDir, Path));
		}
		return FPaths::ConvertRelativePathToFull(Path);
	}

	static bool HasApiKey(const FString& KeyFile)
	{
		const FString EnvironmentKey = FPlatformMisc::GetEnvironmentVariable(TEXT("DEEPSEEK_API_KEY")).TrimStartAndEnd();
		if (EnvironmentKey.Len() >= 8 && !EnvironmentKey.Contains(TEXT(" ")))
		{
			return true;
		}

		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *KeyFile))
		{
			return false;
		}

		TArray<FString> Lines;
		Contents.ParseIntoArrayLines(Lines, false);
		for (FString Line : Lines)
		{
			Line.TrimStartAndEndInline();
			if (Line.IsEmpty() || Line.StartsWith(TEXT("#")))
			{
				continue;
			}
			if (Line.StartsWith(TEXT("export ")))
			{
				Line = Line.Mid(7).TrimStartAndEnd();
			}
			FString Value = Line;
			int32 EqualsIndex = INDEX_NONE;
			if (Line.FindChar(TEXT('='), EqualsIndex))
			{
				const FString Name = Line.Left(EqualsIndex).TrimStartAndEnd();
				if (Name != TEXT("DEEPSEEK_API_KEY") && Name != TEXT("EPICODE_DEEPSEEK_API_KEY"))
				{
					continue;
				}
				Value = Line.Mid(EqualsIndex + 1).TrimStartAndEnd();
			}
			if (Value.Len() >= 2
				&& (Value[0] == TEXT('"') || Value[0] == TEXT('\''))
				&& Value[Value.Len() - 1] == Value[0])
			{
				Value = Value.Mid(1, Value.Len() - 2).TrimStartAndEnd();
			}
			if (Value.Len() >= 8 && !Value.Contains(TEXT(" ")))
			{
				return true;
			}
		}
		return false;
	}

	static bool WriteAllLines(
		const TArray<uint8>& Bytes,
		int32& InOutStart,
		TArray<FString>& OutLines)
	{
		int32 LineStart = InOutStart;
		for (;;)
		{
			int32 NewlineIndex = INDEX_NONE;
			for (int32 Index = LineStart; Index < Bytes.Num(); ++Index)
			{
				if (Bytes[Index] == static_cast<uint8>('\n'))
				{
					NewlineIndex = Index;
					break;
				}
			}
			if (NewlineIndex == INDEX_NONE)
			{
				break;
			}

			int32 LineLength = NewlineIndex - LineStart;
			if (LineLength > 0 && Bytes[LineStart + LineLength - 1] == static_cast<uint8>('\r'))
			{
				--LineLength;
			}
			if (LineLength > 0)
			{
				const FUTF8ToTCHAR Converter(
					reinterpret_cast<const ANSICHAR*>(Bytes.GetData() + LineStart),
					LineLength);
				OutLines.Add(FString(Converter.Length(), Converter.Get()));
			}
			LineStart = NewlineIndex + 1;
		}
		InOutStart = LineStart;
		return true;
	}

	class FSidecarReader final : public FRunnable
	{
	public:
		explicit FSidecarReader(FSidecarProcess& InOwner)
			: Owner(InOwner)
		{
		}

		virtual uint32 Run() override;

	private:
		FSidecarProcess& Owner;
	};

	struct FSidecarProcess
	{
		explicit FSidecarProcess(FSidecarBackendConfig InConfig)
			: Config(MoveTemp(InConfig))
		{
			ProjectDir = Config.ProjectDir.IsEmpty()
				? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir())
				: FPaths::ConvertRelativePathToFull(Config.ProjectDir);
			ScriptPath = ResolvePath(ProjectDir, Config.Script);
			ApiKeyFile = ResolvePath(
				ProjectDir,
				Config.ApiKeyFile.IsEmpty()
					? FString(TEXT("Saved/Epicode/deepseek.env"))
					: Config.ApiKeyFile);
			DshHome = ResolvePath(
				ProjectDir,
				Config.DshHome.IsEmpty()
					? FString(TEXT("Saved/Epicode/Assistant/dsh-home"))
					: Config.DshHome);
			VenvDir = ResolvePath(
				ProjectDir,
				Config.VenvDir.IsEmpty()
					? FString(TEXT("Saved/Epicode/Assistant/venv"))
					: Config.VenvDir);
		}

		~FSidecarProcess()
		{
			Stop();
		}

		bool EnsureRunning(FString& OutError)
		{
			if (IsRunning())
			{
				return true;
			}
			Stop();
			return Start(OutError);
		}

		bool Start(FString& OutError)
		{
			if (Config.Command.TrimStartAndEnd().IsEmpty())
			{
				OutError = TEXT("AssistantSidecarCommand 为空");
				return false;
			}
			if (!IFileManager::Get().FileExists(*ScriptPath))
			{
				OutError = FString::Printf(TEXT("找不到 sidecar 脚本：%s"), *ScriptPath);
				return false;
			}
			if (!IFileManager::Get().DirectoryExists(*ProjectDir))
			{
				OutError = FString::Printf(TEXT("项目目录不存在：%s"), *ProjectDir);
				return false;
			}

			if (!FPlatformProcess::CreatePipe(OutRead, OutWrite)
				|| !FPlatformProcess::CreatePipe(InRead, InWrite)
				|| !FPlatformProcess::CreatePipe(ErrRead, ErrWrite))
			{
				ClosePipes();
				OutError = TEXT("无法创建 sidecar 管道");
				return false;
			}

			FString Arguments = FString::Printf(
				TEXT("%s --project-dir %s --model %s --base-url %s --api-key-file %s --dsh-home %s --venv %s --idle-seconds %d --request-timeout-seconds %s"),
				*QuoteArgument(ScriptPath),
				*QuoteArgument(ProjectDir),
				*QuoteArgument(Config.Model),
				*QuoteArgument(Config.BaseUrl),
				*QuoteArgument(ApiKeyFile),
				*QuoteArgument(DshHome),
				*QuoteArgument(VenvDir),
				Config.IdleSeconds,
				*FString::SanitizeFloat(FMath::Max(5.0f, Config.RequestTimeoutSeconds)));

			Proc = FPlatformProcess::CreateProc(
				*Config.Command,
				*Arguments,
				false,
				true,
				true,
				nullptr,
				0,
				*ProjectDir,
				OutWrite,
				InRead,
				ErrWrite);
			if (!Proc.IsValid())
			{
				ClosePipes();
				OutError = FString::Printf(
					TEXT("无法启动 sidecar：%s %s"), *Config.Command, *Arguments);
				return false;
			}

			bStop = false;
			bStarted = true;
			Reader = MakeUnique<FSidecarReader>(*this);
			ReaderThread = TUniquePtr<FRunnableThread>(
				FRunnableThread::Create(Reader.Get(), TEXT("EpicodeAssistantSidecar"), 0, TPri_BelowNormal));
			if (!ReaderThread.IsValid())
			{
				Stop();
				OutError = TEXT("无法启动 sidecar 读取线程");
				return false;
			}

			UE_LOG(LogEpicodeAssistantSidecar, Log, TEXT("started %s"), *ScriptPath);
			return true;
		}

		void Stop()
		{
			if (!bStarted && !Proc.IsValid())
			{
				ClosePipes();
				return;
			}

			bStop = true;
			if (Proc.IsValid())
			{
				if (FPlatformProcess::IsProcRunning(Proc))
				{
					FString Ignored;
					SendLineUnchecked(TEXT("{\"op\":\"shutdown\"}"), Ignored);
					for (int32 Wait = 0; Wait < 30 && FPlatformProcess::IsProcRunning(Proc); ++Wait)
					{
						FPlatformProcess::Sleep(0.01f);
					}
				}
				if (FPlatformProcess::IsProcRunning(Proc))
				{
					FPlatformProcess::TerminateProc(Proc, true);
				}
				FPlatformProcess::WaitForProc(Proc);
				FPlatformProcess::CloseProc(Proc);
			}
			Proc.Reset();
			if (ReaderThread.IsValid())
			{
				ReaderThread->WaitForCompletion();
				ReaderThread.Reset();
			}
			Reader.Reset();
			ClosePipes();
			bStarted = false;
			UE_LOG(LogEpicodeAssistantSidecar, Log, TEXT("stopped"));
		}

		bool IsRunning()
		{
			return Proc.IsValid() && FPlatformProcess::IsProcRunning(Proc);
		}

		bool SendLine(const FString& Line, FString& OutError)
		{
			if (!IsRunning())
			{
				OutError = TEXT("sidecar 未运行");
				return false;
			}
			return SendLineUnchecked(Line, OutError);
		}

		bool SendLineUnchecked(const FString& Line, FString& OutError)
		{
			if (InWrite == nullptr)
			{
				OutError = TEXT("sidecar 输入管道不可用");
				return false;
			}
			const FTCHARToUTF8 Converter(*Line);
			TArray<uint8> Bytes;
			Bytes.Append(
				reinterpret_cast<const uint8*>(Converter.Get()),
				Converter.Length());
			Bytes.Add(static_cast<uint8>('\n'));
			int32 Written = 0;
			if (!FPlatformProcess::WritePipe(
					InWrite,
					Bytes.GetData(),
					Bytes.Num(),
					&Written)
				|| Written != Bytes.Num())
			{
				OutError = TEXT("写入 sidecar 输入管道失败");
				return false;
			}
			return true;
		}

		void DrainLines(TArray<FString>& OutLines)
		{
			FScopeLock Lock(&LineLock);
			OutLines = MoveTemp(Lines);
			Lines.Reset();
		}

		FString GetStderrTail() const
		{
			FScopeLock Lock(&LineLock);
			return FString::Join(StderrTail, TEXT("\n"));
		}

		void ReceiveStdout(const TArray<uint8>& Bytes)
		{
			StdoutPending.Append(Bytes);
			TArray<FString> NewLines;
			int32 Start = 0;
			WriteAllLines(StdoutPending, Start, NewLines);
			if (Start > 0)
			{
				StdoutPending.RemoveAt(0, Start);
			}
			if (NewLines.Num() == 0)
			{
				return;
			}
			FScopeLock Lock(&LineLock);
			Lines.Append(MoveTemp(NewLines));
		}

		void ReceiveStderr(const TArray<uint8>& Bytes)
		{
			StderrPending.Append(Bytes);
			TArray<FString> NewLines;
			int32 Start = 0;
			WriteAllLines(StderrPending, Start, NewLines);
			if (Start > 0)
			{
				StderrPending.RemoveAt(0, Start);
			}
			if (NewLines.Num() == 0)
			{
				return;
			}
			FScopeLock Lock(&LineLock);
			StderrTail.Append(MoveTemp(NewLines));
			const int32 Excess = StderrTail.Num() - MaxStderrLines;
			if (Excess > 0)
			{
				StderrTail.RemoveAt(0, Excess);
			}
		}

		void ClosePipes()
		{
			if (OutRead != nullptr || OutWrite != nullptr)
			{
				FPlatformProcess::ClosePipe(OutRead, OutWrite);
				OutRead = nullptr;
				OutWrite = nullptr;
			}
			if (InRead != nullptr || InWrite != nullptr)
			{
				FPlatformProcess::ClosePipe(InRead, InWrite);
				InRead = nullptr;
				InWrite = nullptr;
			}
			if (ErrRead != nullptr || ErrWrite != nullptr)
			{
				FPlatformProcess::ClosePipe(ErrRead, ErrWrite);
				ErrRead = nullptr;
				ErrWrite = nullptr;
			}
		}

		FSidecarBackendConfig Config;
		FString ProjectDir;
		FString ScriptPath;
		FString ApiKeyFile;
		FString DshHome;
		FString VenvDir;
		FProcHandle Proc;
		void* OutRead = nullptr;
		void* OutWrite = nullptr;
		void* InRead = nullptr;
		void* InWrite = nullptr;
		void* ErrRead = nullptr;
		void* ErrWrite = nullptr;
		TUniquePtr<FSidecarReader> Reader;
		TUniquePtr<FRunnableThread> ReaderThread;
		FThreadSafeBool bStop = false;
		FThreadSafeBool bStarted = false;
		TArray<uint8> StdoutPending;
		TArray<uint8> StderrPending;
		mutable FCriticalSection LineLock;
		TArray<FString> Lines;
		TArray<FString> StderrTail;
	};

	uint32 FSidecarReader::Run()
	{
		while (!Owner.bStop)
		{
			bool bReadAnything = false;
			TArray<uint8> StdoutBytes;
			if (FPlatformProcess::ReadPipeToArray(Owner.OutRead, StdoutBytes))
			{
				bReadAnything = true;
				Owner.ReceiveStdout(StdoutBytes);
			}
			TArray<uint8> StderrBytes;
			if (FPlatformProcess::ReadPipeToArray(Owner.ErrRead, StderrBytes))
			{
				bReadAnything = true;
				Owner.ReceiveStderr(StderrBytes);
			}
			if (!bReadAnything)
			{
				FPlatformProcess::Sleep(0.01f);
			}
		}
		return 0;
	}

	class FSidecarConversationStream final : public IConversationStream
		, public TSharedFromThis<FSidecarConversationStream, ESPMode::ThreadSafe>
	{
	public:
		FSidecarConversationStream(
			TSharedPtr<FSidecarProcess> InProcess,
			FString InRequestId,
			float InTimeoutSeconds)
			: Process(MoveTemp(InProcess))
			, RequestId(MoveTemp(InRequestId))
			, TimeoutSeconds(FMath::Max(0.1f, InTimeoutSeconds))
			, StartedAt(FPlatformTime::Seconds())
		{
		}

		virtual void Cancel() override
		{
			if (bDone || bCancelRequested)
			{
				return;
			}
			bCancelRequested = true;
			CancelRequestedAt = FPlatformTime::Seconds();
			if (Process.IsValid())
			{
				TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
				Root->SetStringField(TEXT("id"), RequestId);
				Root->SetStringField(TEXT("op"), TEXT("cancel"));
				FString Payload;
				const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
					TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Payload);
				FJsonSerializer::Serialize(Root, Writer);
				FString Ignored;
				Process->SendLine(Payload, Ignored);
			}
		}

		virtual bool IsDone() const override
		{
			return bDone;
		}

		virtual void Poll(TArray<FConversationEvent>& OutEvents) override
		{
			if (bDone)
			{
				return;
			}

			const double Now = FPlatformTime::Seconds();
			if (!bCancelRequested && Now - StartedAt > TimeoutSeconds)
			{
				FConversationEvent Event;
				Event.Type = EConversationEventType::Error;
				Event.RequestId = RequestId;
				Event.ErrorCode = TEXT("timeout");
				Event.ErrorMessage = TEXT("sidecar 响应超时");
				OutEvents.Add(MoveTemp(Event));
				bDone = true;
				if (Process.IsValid())
				{
					Process->Stop();
				}
				return;
			}
			if (bCancelRequested && Now - CancelRequestedAt > 2.0)
			{
				FConversationEvent Event;
				Event.Type = EConversationEventType::Cancelled;
				Event.RequestId = RequestId;
				Event.ErrorMessage = TEXT("已停止生成");
				OutEvents.Add(MoveTemp(Event));
				bDone = true;
				if (Process.IsValid())
				{
					Process->Stop();
				}
				return;
			}

			if (Process.IsValid())
			{
				TArray<FString> Lines;
				Process->DrainLines(Lines);
				for (const FString& Line : Lines)
				{
					Parser.AppendLine(Line);
				}
			}

			TArray<FConversationEvent> Parsed;
			Parser.TakeEvents(RequestId, Parsed);
			bool bSawDone = false;
			for (const FConversationEvent& Event : Parsed)
			{
				if (Event.Type == EConversationEventType::Delta)
				{
					bHadOutput = true;
				}
				else if (Event.Type == EConversationEventType::Done)
				{
					bSawDone = true;
				}
			}
			if (bSawDone && !bHadOutput && Parsed.Num() > 0)
			{
				for (FConversationEvent& Event : Parsed)
				{
					if (Event.Type == EConversationEventType::Done)
					{
						Event.Type = EConversationEventType::Error;
						Event.ErrorCode = TEXT("empty_response");
						Event.ErrorMessage = TEXT("sidecar 返回空回复");
					}
				}
			}
			OutEvents.Append(MoveTemp(Parsed));

			if (Parser.IsDone())
			{
				bDone = true;
				return;
			}

			if (Process.IsValid() && !Process->IsRunning())
			{
				FConversationEvent Event;
				Event.Type = EConversationEventType::Error;
				Event.RequestId = RequestId;
				Event.ErrorCode = TEXT("sidecar_crashed");
				const FString StderrTail = Process->GetStderrTail();
				Event.ErrorMessage = StderrTail.IsEmpty()
					? TEXT("sidecar 进程已退出")
					: FString::Printf(TEXT("sidecar 进程已退出：%s"), *StderrTail);
				OutEvents.Add(MoveTemp(Event));
				bDone = true;
			}
		}

	private:
		TSharedPtr<FSidecarProcess> Process;
		FString RequestId;
		FSidecarJsonLineParser Parser;
		float TimeoutSeconds = 120.0f;
		double StartedAt = 0.0;
		double CancelRequestedAt = 0.0;
		bool bDone = false;
		bool bHadOutput = false;
		bool bCancelRequested = false;
	};

void FSidecarJsonLineParser::Append(const void* Data, int64 Length)
{
	if (Data == nullptr || Length <= 0)
	{
		return;
	}
	PendingBytes.Append(static_cast<const uint8*>(Data), static_cast<int32>(Length));

	int32 LineStart = 0;
	for (;;)
	{
		int32 NewlineIndex = INDEX_NONE;
		for (int32 Index = LineStart; Index < PendingBytes.Num(); ++Index)
		{
			if (PendingBytes[Index] == static_cast<uint8>('\n'))
			{
				NewlineIndex = Index;
				break;
			}
		}
		if (NewlineIndex == INDEX_NONE)
		{
			break;
		}

		int32 LineLength = NewlineIndex - LineStart;
		if (LineLength > 0 && PendingBytes[LineStart + LineLength - 1] == static_cast<uint8>('\r'))
		{
			--LineLength;
		}
		if (LineLength > 0)
		{
			if (LineLength > MaxSidecarLineBytes)
			{
				FConversationEvent Event;
				Event.Type = EConversationEventType::Error;
				Event.ErrorCode = TEXT("line_too_long");
				Event.ErrorMessage = TEXT("sidecar 响应行超过 1 MiB");
				AppendEvent(MoveTemp(Event));
				bProtocolError = true;
				bDone = true;
				PendingBytes.Reset();
				return;
			}
			const FUTF8ToTCHAR Converter(
				reinterpret_cast<const ANSICHAR*>(PendingBytes.GetData() + LineStart),
				LineLength);
			ProcessLine(FString(Converter.Length(), Converter.Get()));
		}
		LineStart = NewlineIndex + 1;
	}
	if (LineStart > 0)
	{
		PendingBytes.RemoveAt(0, LineStart);
	}
	if (PendingBytes.Num() > MaxSidecarLineBytes)
	{
		FConversationEvent Event;
		Event.Type = EConversationEventType::Error;
		Event.ErrorCode = TEXT("line_too_long");
		Event.ErrorMessage = TEXT("sidecar 响应行超过 1 MiB");
		AppendEvent(MoveTemp(Event));
		bProtocolError = true;
		bDone = true;
		PendingBytes.Reset();
	}
}

void FSidecarJsonLineParser::Append(const FString& Text)
{
	const FTCHARToUTF8 Converter(*Text);
	Append(Converter.Get(), Converter.Length());
}

void FSidecarJsonLineParser::AppendLine(const FString& Line)
{
	if (Line.Len() > MaxSidecarLineBytes)
	{
		FConversationEvent Event;
		Event.Type = EConversationEventType::Error;
		Event.ErrorCode = TEXT("line_too_long");
		Event.ErrorMessage = TEXT("sidecar 响应行超过 1 MiB");
		AppendEvent(MoveTemp(Event));
		bProtocolError = true;
		bDone = true;
		return;
	}
	ProcessLine(Line);
}

void FSidecarJsonLineParser::TakeEvents(
	const FString& RequestId,
	TArray<FConversationEvent>& OutEvents)
{
	for (FConversationEvent& Event : Events)
	{
		if (Event.RequestId.IsEmpty())
		{
			Event.RequestId = RequestId;
		}
	}
	OutEvents.Append(MoveTemp(Events));
	Events.Reset();
}

void FSidecarJsonLineParser::Reset()
{
	PendingBytes.Reset();
	Events.Reset();
	bDone = false;
	bHasOutput = false;
	bProtocolError = false;
}

void FSidecarJsonLineParser::ProcessLine(const FString& Line)
{
	if (Line.TrimStartAndEnd().IsEmpty())
	{
		return;
	}

	TSharedPtr<FJsonObject> Object;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
	if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
	{
		FConversationEvent Event;
		Event.Type = EConversationEventType::Error;
		Event.ErrorCode = TEXT("bad_json");
		Event.ErrorMessage = TEXT("sidecar 返回了非法 JSON");
		AppendEvent(MoveTemp(Event));
		bProtocolError = true;
		bDone = true;
		return;
	}

	FString EventName;
	if (!Object->TryGetStringField(TEXT("event"), EventName))
	{
		return;
	}

	if (EventName == TEXT("delta"))
	{
		FString Text;
		if (Object->TryGetStringField(TEXT("text"), Text) && !Text.IsEmpty())
		{
			FConversationEvent Event;
			Event.Type = EConversationEventType::Delta;
			Event.Text = MoveTemp(Text);
			AppendEvent(MoveTemp(Event));
			bHasOutput = true;
		}
	}
	else if (EventName == TEXT("done"))
	{
		FConversationEvent Event;
		Event.Type = EConversationEventType::Done;
		AppendEvent(MoveTemp(Event));
		bDone = true;
	}
	else if (EventName == TEXT("error"))
	{
		FConversationEvent Event;
		Event.Type = EConversationEventType::Error;
		Object->TryGetStringField(TEXT("code"), Event.ErrorCode);
		Object->TryGetStringField(TEXT("message"), Event.ErrorMessage);
		if (Event.ErrorMessage.IsEmpty())
		{
			Event.ErrorMessage = TEXT("sidecar 返回错误");
		}
		AppendEvent(MoveTemp(Event));
		bDone = true;
	}
	else if (EventName == TEXT("cancelled"))
	{
		FConversationEvent Event;
		Event.Type = EConversationEventType::Cancelled;
		Object->TryGetStringField(TEXT("message"), Event.ErrorMessage);
		if (Event.ErrorMessage.IsEmpty())
		{
			Event.ErrorMessage = TEXT("已停止生成");
		}
		AppendEvent(MoveTemp(Event));
		bDone = true;
	}
}

void FSidecarJsonLineParser::AppendEvent(FConversationEvent Event)
{
	Events.Add(MoveTemp(Event));
}

FDeepSeekHarnessSidecarBackend::FDeepSeekHarnessSidecarBackend(FSidecarBackendConfig InConfig)
	: Config(MoveTemp(InConfig))
{
}

FDeepSeekHarnessSidecarBackend::~FDeepSeekHarnessSidecarBackend()
{
	if (Process.IsValid())
	{
		Process->Stop();
	}
}

bool FDeepSeekHarnessSidecarBackend::IsAvailable(FString& OutReason) const
{
	if (!Config.Mode.Equals(TEXT("stdio"), ESearchCase::IgnoreCase))
	{
		OutReason = FString::Printf(
			TEXT("sidecar 模式 %s 尚未启用，请使用 stdio"),
			*Config.Mode);
		return false;
	}
	if (Config.Command.TrimStartAndEnd().IsEmpty())
	{
		OutReason = TEXT("AssistantSidecarCommand 为空");
		return false;
	}

	const FString ProjectDir = Config.ProjectDir.IsEmpty()
		? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir())
		: FPaths::ConvertRelativePathToFull(Config.ProjectDir);
	const FString ScriptPath = ResolvePath(ProjectDir, Config.Script);
	if (!IFileManager::Get().FileExists(*ScriptPath))
	{
		OutReason = FString::Printf(TEXT("找不到 sidecar 脚本：%s"), *ScriptPath);
		return false;
	}

	const FString KeyFile = ResolvePath(
		ProjectDir,
		Config.ApiKeyFile.IsEmpty()
			? FString(TEXT("Saved/Epicode/deepseek.env"))
			: Config.ApiKeyFile);
	if (!HasApiKey(KeyFile))
	{
		OutReason = FString::Printf(
			TEXT("未配置 DEEPSEEK_API_KEY，也未找到 key 文件 %s"),
			*KeyFile);
		return false;
	}

	OutReason.Reset();
	return true;
}

TSharedPtr<IConversationStream> FDeepSeekHarnessSidecarBackend::Start(
	const FConversationRequest& Request,
	FString& OutError)
{
	if (!IsAvailable(OutError))
	{
		return nullptr;
	}
	if (Request.Prompt.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("对话消息为空");
		return nullptr;
	}
	if (const TSharedPtr<IConversationStream> Active = ActiveStream.Pin();
		Active.IsValid() && !Active->IsDone())
	{
		OutError = TEXT("上一条对话仍在生成");
		return nullptr;
	}

	if (!Process.IsValid())
	{
		Process = MakeShared<FSidecarProcess>(Config);
	}
	if (!Process->EnsureRunning(OutError))
	{
		return nullptr;
	}
	FPlatformProcess::Sleep(0.05f);
	if (!Process->IsRunning())
	{
		const FString StderrTail = Process->GetStderrTail();
		OutError = StderrTail.IsEmpty()
			? TEXT("sidecar 启动后立即退出")
			: FString::Printf(TEXT("sidecar 启动后立即退出：%s"), *StderrTail);
		Process->Stop();
		return nullptr;
	}

	FString RequestId = Request.RequestId;
	if (RequestId.IsEmpty())
	{
		RequestId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("id"), RequestId);
	Root->SetStringField(TEXT("op"), TEXT("chat"));
	Root->SetStringField(TEXT("prompt"), Request.Prompt);
	Root->SetStringField(TEXT("context"), Request.Context);
	Root->SetStringField(TEXT("conversationId"), Request.ConversationId);
	Root->SetStringField(TEXT("model"), Config.Model);

	TArray<TSharedPtr<FJsonValue>> History;
	for (const FConversationMessage& Message : Request.History)
	{
		if (Message.Content.IsEmpty())
		{
			continue;
		}
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("role"), FConversationMessage::RoleToString(Message.Role));
		Item->SetStringField(TEXT("content"), Message.Content);
		History.Add(MakeShared<FJsonValueObject>(Item));
	}
	Root->SetArrayField(TEXT("history"), History);

	FString Payload;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Payload);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		OutError = TEXT("无法序列化 sidecar 请求");
		return nullptr;
	}
	if (!Process->SendLine(Payload, OutError))
	{
		const FString StderrTail = Process->GetStderrTail();
		if (!StderrTail.IsEmpty())
		{
			OutError += TEXT("；") + StderrTail;
		}
		return nullptr;
	}

	TSharedRef<FSidecarConversationStream, ESPMode::ThreadSafe> Stream =
		MakeShared<FSidecarConversationStream, ESPMode::ThreadSafe>(
			Process,
			RequestId,
			Config.RequestTimeoutSeconds);
	ActiveStream = Stream;
	return Stream;
}
