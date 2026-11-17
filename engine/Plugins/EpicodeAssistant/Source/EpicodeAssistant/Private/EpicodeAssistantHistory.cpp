#include "EpicodeAssistantHistory.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	FString HistoryRootOverride;

	constexpr int32 HistorySchemaVersion = 1;
	constexpr int32 MaxPersistedMessages = 200;

	FString HistoryFilePath(const FString& ConversationId)
	{
		return FPaths::Combine(
			FConversationHistoryStore::GetHistoryRoot(),
			FConversationHistoryStore::SanitizeId(ConversationId) + TEXT(".json"));
	}

	bool ParseHistoryObject(
		const TSharedPtr<FJsonObject>& Root,
		TArray<FConversationMessage>& OutMessages,
		FString& OutError)
	{
		if (!Root.IsValid())
		{
			OutError = TEXT("对话历史为空或不是 JSON 对象");
			return false;
		}

		int32 SchemaVersion = 0;
		if (!Root->TryGetNumberField(TEXT("schemaVersion"), SchemaVersion)
			|| SchemaVersion != HistorySchemaVersion)
		{
			OutError = TEXT("不支持的对话历史版本");
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* MessageValues = nullptr;
		if (!Root->TryGetArrayField(TEXT("messages"), MessageValues) || MessageValues == nullptr)
		{
			OutError = TEXT("对话历史缺少 messages 数组");
			return false;
		}

		OutMessages.Reset();
		for (const TSharedPtr<FJsonValue>& Value : *MessageValues)
		{
			const TSharedPtr<FJsonObject>* MessageObject = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(MessageObject) || MessageObject == nullptr)
			{
				continue;
			}

			FString Role;
			FString Content;
			if (!(*MessageObject)->TryGetStringField(TEXT("role"), Role)
				|| !(*MessageObject)->TryGetStringField(TEXT("content"), Content))
			{
				continue;
			}

			FConversationMessage Message;
			Message.Role = FConversationMessage::RoleFromString(Role);
			Message.Content = Content;
			(*MessageObject)->TryGetStringField(TEXT("id"), Message.Id);
			if (Message.Id.IsEmpty())
			{
				Message.Id = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
			}

			FString Timestamp;
			if ((*MessageObject)->TryGetStringField(TEXT("timestamp"), Timestamp))
			{
				FDateTime Parsed;
				if (FDateTime::ParseIso8601(*Timestamp, Parsed))
				{
					Message.Timestamp = Parsed;
				}
			}

			OutMessages.Add(MoveTemp(Message));
		}

		OutError.Reset();
		return true;
	}
}

FConversationHistoryStore::FScopedRootOverride::FScopedRootOverride(const FString& InRoot)
	: PreviousRoot(HistoryRootOverride)
{
	HistoryRootOverride = FPaths::ConvertRelativePathToFull(InRoot);
}

FConversationHistoryStore::FScopedRootOverride::~FScopedRootOverride()
{
	HistoryRootOverride = PreviousRoot;
}

FString FConversationHistoryStore::GetHistoryRoot()
{
	if (!HistoryRootOverride.IsEmpty())
	{
		return HistoryRootOverride;
	}
	return FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Epicode"), TEXT("Chat")));
}

FString FConversationHistoryStore::NewConversationId()
{
	return FString::Printf(
		TEXT("chat-%s-%s"),
		*FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8).ToLower());
}

FString FConversationHistoryStore::SanitizeId(const FString& Id)
{
	FString Clean;
	Clean.Reserve(FMath::Min(Id.Len(), 96));
	for (const TCHAR Char : Id)
	{
		if (FChar::IsAlnum(Char) || Char == TEXT('-') || Char == TEXT('_') || Char == TEXT('.'))
		{
			Clean.AppendChar(Char);
		}
		if (Clean.Len() >= 96)
		{
			break;
		}
	}
	return Clean.IsEmpty() ? TEXT("conversation") : Clean;
}

bool FConversationHistoryStore::Save(
	const FString& ConversationId,
	const TArray<FConversationMessage>& Messages,
	FString& OutError)
{
	const FString SafeId = SanitizeId(ConversationId);
	const FString Root = GetHistoryRoot();
	if (!IFileManager::Get().MakeDirectory(*Root, true))
	{
		OutError = FString::Printf(TEXT("无法创建对话历史目录：%s"), *Root);
		return false;
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetNumberField(TEXT("schemaVersion"), HistorySchemaVersion);
	RootObject->SetStringField(TEXT("conversationId"), SafeId);
	RootObject->SetStringField(TEXT("updatedAt"), FDateTime::UtcNow().ToIso8601());

	TArray<TSharedPtr<FJsonValue>> MessageValues;
	int32 Persisted = 0;
	for (int32 Index = Messages.Num() - 1; Index >= 0 && Persisted < MaxPersistedMessages; --Index)
	{
		const FConversationMessage& Message = Messages[Index];
		if (Message.Role == EConversationRole::System || Message.Content.IsEmpty())
		{
			continue;
		}

		TSharedRef<FJsonObject> MessageObject = MakeShared<FJsonObject>();
		MessageObject->SetStringField(TEXT("id"), Message.Id);
		MessageObject->SetStringField(TEXT("role"), FConversationMessage::RoleToString(Message.Role));
		MessageObject->SetStringField(TEXT("content"), Message.Content);
		MessageObject->SetStringField(TEXT("timestamp"), Message.Timestamp.ToIso8601());
		MessageValues.Add(MakeShared<FJsonValueObject>(MessageObject));
		++Persisted;
	}
	Algo::Reverse(MessageValues);
	RootObject->SetArrayField(TEXT("messages"), MessageValues);

	FString Serialized;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
	if (!FJsonSerializer::Serialize(RootObject, Writer))
	{
		OutError = TEXT("无法序列化对话历史");
		return false;
	}

	const FString Path = HistoryFilePath(SafeId);
	const FString TempPath = Path + TEXT(".tmp");
	if (!FFileHelper::SaveStringToFile(Serialized, *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("无法写入对话历史：%s"), *TempPath);
		return false;
	}

	if (!IFileManager::Get().Move(*Path, *TempPath, true, true))
	{
		IFileManager::Get().Delete(*TempPath, false, true, true);
		OutError = FString::Printf(TEXT("无法替换对话历史：%s"), *Path);
		return false;
	}

	OutError.Reset();
	return true;
}

bool FConversationHistoryStore::Load(
	const FString& ConversationId,
	TArray<FConversationMessage>& OutMessages,
	FString& OutError)
{
	const FString Path = HistoryFilePath(ConversationId);
	FString Serialized;
	if (!FFileHelper::LoadFileToString(Serialized, *Path))
	{
		OutError = FString::Printf(TEXT("对话历史不存在：%s"), *Path);
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Serialized);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("对话历史不是有效 JSON");
		return false;
	}
	return ParseHistoryObject(Root, OutMessages, OutError);
}

TArray<FString> FConversationHistoryStore::ListIds(FString& OutError)
{
	TArray<FString> Result;
	const FString Root = GetHistoryRoot();
	if (!IFileManager::Get().DirectoryExists(*Root))
	{
		OutError.Reset();
		return Result;
	}

	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(Root, TEXT("chat-*.json")), true, false);
	Files.Sort([&Root](const FString& Left, const FString& Right)
	{
		const FDateTime LeftTime = IFileManager::Get().GetTimeStamp(*FPaths::Combine(Root, Left));
		const FDateTime RightTime = IFileManager::Get().GetTimeStamp(*FPaths::Combine(Root, Right));
		if (LeftTime == RightTime)
		{
			return Left > Right;
		}
		return LeftTime > RightTime;
	});

	for (const FString& File : Files)
	{
		Result.Add(FPaths::GetBaseFilename(File));
	}
	OutError.Reset();
	return Result;
}

bool FConversationHistoryStore::Delete(const FString& ConversationId, FString& OutError)
{
	const FString Path = HistoryFilePath(ConversationId);
	if (!IFileManager::Get().FileExists(*Path))
	{
		OutError = TEXT("对话历史不存在");
		return false;
	}
	if (!IFileManager::Get().Delete(*Path, false, true, true))
	{
		OutError = FString::Printf(TEXT("无法删除对话历史：%s"), *Path);
		return false;
	}
	OutError.Reset();
	return true;
}
