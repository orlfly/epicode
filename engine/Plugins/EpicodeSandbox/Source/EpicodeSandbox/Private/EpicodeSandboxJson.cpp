#include "EpicodeSandboxJson.h"

#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

TSharedPtr<FJsonValue> FEpicodeSandboxJson::CloneValue(const TSharedPtr<FJsonValue>& InValue)
{
	if (!InValue.IsValid())
	{
		return MakeShared<FJsonValueNull>();
	}
	switch (InValue->Type)
	{
	case EJson::Object:
		return MakeShared<FJsonValueObject>(CloneObject(InValue->AsObject()));
	case EJson::Array:
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const TSharedPtr<FJsonValue>& Value : InValue->AsArray())
		{
			Values.Add(CloneValue(Value));
		}
		return MakeShared<FJsonValueArray>(Values);
	}
	case EJson::String:
		return MakeShared<FJsonValueString>(InValue->AsString());
	case EJson::Boolean:
		return MakeShared<FJsonValueBoolean>(InValue->AsBool());
	case EJson::Number:
		return MakeShared<FJsonValueNumber>(InValue->AsNumber());
	case EJson::Null:
	default:
		return MakeShared<FJsonValueNull>();
	}
}

TSharedPtr<FJsonObject> FEpicodeSandboxJson::CloneObject(const TSharedPtr<FJsonObject>& InObject)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	if (!InObject.IsValid())
	{
		return Result;
	}
	for (const auto& Pair : InObject->Values)
	{
		Result->SetField(FString(*Pair.Key), CloneValue(Pair.Value));
	}
	return Result;
}

FString FEpicodeSandboxJson::EscapeString(const FString& InValue)
{
	FString Result;
	Result.Reserve(InValue.Len() + 2);
	Result += TEXT("\"");
	for (const TCHAR Character : InValue)
	{
		switch (Character)
		{
		case TEXT('"'): Result += TEXT("\\\""); break;
		case TEXT('\\'): Result += TEXT("\\\\"); break;
		case TEXT('\b'): Result += TEXT("\\b"); break;
		case TEXT('\f'): Result += TEXT("\\f"); break;
		case TEXT('\n'): Result += TEXT("\\n"); break;
		case TEXT('\r'): Result += TEXT("\\r"); break;
		case TEXT('\t'): Result += TEXT("\\t"); break;
		default: Result.AppendChar(Character); break;
		}
	}
	Result += TEXT("\"");
	return Result;
}

FString FEpicodeSandboxJson::CanonicalString(const TSharedPtr<FJsonValue>& InValue)
{
	if (!InValue.IsValid())
	{
		return TEXT("null");
	}
	switch (InValue->Type)
	{
	case EJson::Object:
		return CanonicalString(InValue->AsObject());
	case EJson::Array:
	{
		TArray<FString> Parts;
		for (const TSharedPtr<FJsonValue>& Value : InValue->AsArray())
		{
			Parts.Add(CanonicalString(Value));
		}
		return TEXT("[") + FString::Join(Parts, TEXT(",")) + TEXT("]");
	}
	case EJson::String:
		return EscapeString(InValue->AsString());
	case EJson::Boolean:
		return InValue->AsBool() ? TEXT("true") : TEXT("false");
	case EJson::Number:
	{
		const double Number = InValue->AsNumber();
		if (FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number)))
		{
			return FString::Printf(TEXT("%lld"), static_cast<long long>(FMath::RoundToDouble(Number)));
		}
		return FString::Printf(TEXT("%.17g"), Number);
	}
	case EJson::Null:
	default:
		return TEXT("null");
	}
}

FString FEpicodeSandboxJson::CanonicalString(const TSharedPtr<FJsonObject>& InObject)
{
	if (!InObject.IsValid())
	{
		return TEXT("{}");
	}
	TArray<FString> Keys;
	TMap<FString, TSharedPtr<FJsonValue>> Values;
	for (const auto& Pair : InObject->Values)
	{
		const FString Key(*Pair.Key);
		Keys.Add(Key);
		Values.Add(Key, Pair.Value);
	}
	Keys.Sort();
	TArray<FString> Parts;
	for (const FString& Key : Keys)
	{
		const TSharedPtr<FJsonValue>* const Value = Values.Find(Key);
		Parts.Add(EscapeString(Key) + TEXT(":") + CanonicalString(Value ? *Value : MakeShared<FJsonValueNull>()));
	}
	return TEXT("{") + FString::Join(Parts, TEXT(",")) + TEXT("}");
}

FString FEpicodeSandboxJson::Serialize(const TSharedPtr<FJsonObject>& InObject, bool bPretty)
{
	FString Output;
	if (!InObject.IsValid())
	{
		return Output;
	}
	if (bPretty)
	{
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
		FJsonSerializer::Serialize(InObject.ToSharedRef(), Writer);
	}
	else
	{
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
		FJsonSerializer::Serialize(InObject.ToSharedRef(), Writer);
	}
	return Output;
}
