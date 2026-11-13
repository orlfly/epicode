#include "EpicodeCommand.h"

#include "EpicodeTypes.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

FEpicodeCommandResult FEpicodeCommandResult::Success(const TSharedPtr<FJsonObject>& InResult, const TArray<FString>& InAffectedIds)
{
	FEpicodeCommandResult Result;
	Result.bSuccess = true;
	Result.Result = InResult;
	Result.AffectedIds = InAffectedIds;
	return Result;
}

FEpicodeCommandResult FEpicodeCommandResult::Failure(const FString& InCode, const FString& InMessage, const FString& InDetail, bool bInRetryable)
{
	FEpicodeCommandResult Result;
	Result.bSuccess = false;
	Result.ErrorCode = InCode;
	Result.ErrorMessage = InMessage;
	Result.ErrorDetail = InDetail;
	Result.bRetryable = bInRetryable;
	return Result;
}

TSharedPtr<FJsonObject> FEpicodeCommandResult::ToJson() const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetBoolField(TEXT("success"), bSuccess);
	if (bSuccess)
	{
		Json->SetObjectField(TEXT("result"), Result.IsValid() ? Result : MakeShared<FJsonObject>());
	}
	else
	{
		Json->SetObjectField(TEXT("error"), EpicodeJson::MakeErrorData(ErrorCode, ErrorDetail, bRetryable));
		Json->GetObjectField(TEXT("error"))->SetStringField(TEXT("message"), ErrorMessage);
	}
	if (AffectedIds.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& Id : AffectedIds)
		{
			Values.Add(MakeShared<FJsonValueString>(Id));
		}
		Json->SetArrayField(TEXT("affectedIds"), Values);
	}
	if (Warnings.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& Warning : Warnings)
		{
			Values.Add(MakeShared<FJsonValueString>(Warning));
		}
		Json->SetArrayField(TEXT("warnings"), Values);
	}
	return Json;
}

void FEpicodeCommandRegistry::Register(const FString& InName, const FString& InDescription, FEpicodeCommandHandler InHandler, bool bInMutating, bool bInAsync, const FString& InRequiredParticipantKind, bool bInRequiresLease)
{
	FScopeLock ScopeLock(&Lock);
	FEpicodeCommandDefinition Definition;
	Definition.Name = InName;
	Definition.Description = InDescription;
	Definition.bMutating = bInMutating;
	Definition.bAsync = bInAsync;
	Definition.RequiredParticipantKind = InRequiredParticipantKind;
	Definition.bRequiresLease = bInRequiresLease;
	Definition.Handler = MoveTemp(InHandler);
	Definitions.Add(InName, MoveTemp(Definition));
}

void FEpicodeCommandRegistry::Unregister(const FString& InName)
{
	FScopeLock ScopeLock(&Lock);
	Definitions.Remove(InName);
}

bool FEpicodeCommandRegistry::Has(const FString& InName) const
{
	FScopeLock ScopeLock(&Lock);
	return Definitions.Contains(InName);
}

bool FEpicodeCommandRegistry::Execute(const FString& InName, const TSharedPtr<FJsonObject>& InParams, const FEpicodeCommandContext& InContext, FEpicodeCommandResult& OutResult, FString& OutError) const
{
	FEpicodeCommandDefinition Definition;
	{
		FScopeLock ScopeLock(&Lock);
		const FEpicodeCommandDefinition* Found = Definitions.Find(InName);
		if (!Found)
		{
			OutError = InName;
			return false;
		}
		Definition = *Found;
	}
	OutResult = Definition.Handler(InParams.IsValid() ? InParams : MakeShared<FJsonObject>(), InContext);
	return true;
}

TArray<FString> FEpicodeCommandRegistry::GetMethodNames() const
{
	FScopeLock ScopeLock(&Lock);
	TArray<FString> Names;
	Definitions.GetKeys(Names);
	Names.Sort();
	return Names;
}

TSharedPtr<FJsonObject> FEpicodeCommandRegistry::DescribeMethods() const
{
	FScopeLock ScopeLock(&Lock);
	TArray<TSharedPtr<FJsonValue>> Methods;
	for (const TPair<FString, FEpicodeCommandDefinition>& Pair : Definitions)
	{
		TSharedPtr<FJsonObject> Method = MakeShared<FJsonObject>();
		Method->SetStringField(TEXT("name"), Pair.Value.Name);
		Method->SetStringField(TEXT("description"), Pair.Value.Description);
		Method->SetBoolField(TEXT("mutating"), Pair.Value.bMutating);
		Method->SetBoolField(TEXT("async"), Pair.Value.bAsync);
		if (!Pair.Value.RequiredParticipantKind.IsEmpty())
		{
			Method->SetStringField(TEXT("requiredParticipantKind"), Pair.Value.RequiredParticipantKind);
		}
		if (Pair.Value.bRequiresLease)
		{
			Method->SetBoolField(TEXT("requiresLease"), true);
		}
		Methods.Add(MakeShared<FJsonValueObject>(Method));
	}
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("methods"), Methods);
	return Result;
}

TMap<FName, TFunction<void(FEpicodeCommandRegistry&)>>& FEpicodeCommandProviderRegistry::GetProviders()
{
	static TMap<FName, TFunction<void(FEpicodeCommandRegistry&)>> Providers;
	return Providers;
}

void FEpicodeCommandProviderRegistry::AddProvider(FName Name, TFunction<void(FEpicodeCommandRegistry&)> Registration)
{
	GetProviders().Add(Name, MoveTemp(Registration));
}

void FEpicodeCommandProviderRegistry::RemoveProvider(FName Name)
{
	GetProviders().Remove(Name);
}

void FEpicodeCommandProviderRegistry::RegisterAll(FEpicodeCommandRegistry& Registry)
{
	for (const TPair<FName, TFunction<void(FEpicodeCommandRegistry&)>>& Pair : GetProviders())
	{
		if (Pair.Value)
		{
			Pair.Value(Registry);
		}
	}
}

namespace EpicodeJson
{
	bool GetRequiredString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FString& OutValue, FString& OutError)
	{
		if (!Object.IsValid() || !Object->TryGetStringField(Field, OutValue) || OutValue.IsEmpty())
		{
			OutError = FString::Printf(TEXT("%s is required and must be a non-empty string"), Field);
			return false;
		}
		return true;
	}

	bool GetOptionalString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FString& OutValue, const FString& DefaultValue)
	{
		if (!Object.IsValid() || !Object->TryGetStringField(Field, OutValue))
		{
			OutValue = DefaultValue;
			return false;
		}
		return true;
	}

	bool GetOptionalNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, double& OutValue, double DefaultValue)
	{
		if (!Object.IsValid() || !Object->TryGetNumberField(Field, OutValue))
		{
			OutValue = DefaultValue;
			return false;
		}
		return true;
	}

	bool GetOptionalBool(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, bool& OutValue, bool DefaultValue)
	{
		if (!Object.IsValid() || !Object->TryGetBoolField(Field, OutValue))
		{
			OutValue = DefaultValue;
			return false;
		}
		return true;
	}

	TSharedPtr<FJsonObject> GetOptionalObject(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* Value = nullptr;
		if (Object.IsValid() && Object->TryGetObjectField(Field, Value) && Value)
		{
			return *Value;
		}
		return nullptr;
	}

	TArray<TSharedPtr<FJsonValue>> GetOptionalArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* Value = nullptr;
		if (Object.IsValid() && Object->TryGetArrayField(Field, Value) && Value)
		{
			return *Value;
		}
		return {};
	}

	bool GetVector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FVector& OutValue, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>> Values = GetOptionalArray(Object, Field);
		if (Values.Num() != 3)
		{
			OutError = FString::Printf(TEXT("%s must be an array of three numbers"), Field);
			return false;
		}
		OutValue = FVector(Values[0]->AsNumber(), Values[1]->AsNumber(), Values[2]->AsNumber());
		if (!FMath::IsFinite(OutValue.X) || !FMath::IsFinite(OutValue.Y) || !FMath::IsFinite(OutValue.Z))
		{
			OutError = FString::Printf(TEXT("%s contains a non-finite value"), Field);
			return false;
		}
		return true;
	}

	bool GetRotator(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FRotator& OutValue, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>> Values = GetOptionalArray(Object, Field);
		if (Values.Num() != 3)
		{
			OutError = FString::Printf(TEXT("%s must be an array of three numbers"), Field);
			return false;
		}
		OutValue = FRotator(Values[0]->AsNumber(), Values[1]->AsNumber(), Values[2]->AsNumber());
		if (!FMath::IsFinite(OutValue.Pitch) || !FMath::IsFinite(OutValue.Yaw) || !FMath::IsFinite(OutValue.Roll))
		{
			OutError = FString::Printf(TEXT("%s contains a non-finite value"), Field);
			return false;
		}
		return true;
	}

	TSharedPtr<FJsonObject> TransformToJson(const FEpicodeTransform& Transform)
	{
		return Transform.ToJson();
	}

	bool TransformFromJson(const TSharedPtr<FJsonObject>& Object, FEpicodeTransform& OutTransform, FString& OutError)
	{
		return FEpicodeTransform::FromJson(Object, OutTransform, OutError);
	}

	TSharedPtr<FJsonObject> EntityToJson(const FEpicodeEntityRecord& Entity)
	{
		return Entity.ToJson();
	}

	TSharedPtr<FJsonObject> MakeResult(const TArray<FString>& AffectedIds, const TArray<FString>& Warnings)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Ids;
		for (const FString& Id : AffectedIds)
		{
			Ids.Add(MakeShared<FJsonValueString>(Id));
		}
		Result->SetArrayField(TEXT("affectedIds"), Ids);
		if (Warnings.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const FString& Warning : Warnings)
			{
				Values.Add(MakeShared<FJsonValueString>(Warning));
			}
			Result->SetArrayField(TEXT("warnings"), Values);
		}
		return Result;
	}

	TSharedPtr<FJsonObject> MakeErrorData(const FString& DomainCode, const FString& Detail, bool bRetryable)
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("domainCode"), DomainCode);
		if (!Detail.IsEmpty())
		{
			Data->SetStringField(TEXT("detail"), Detail);
		}
		Data->SetBoolField(TEXT("retryable"), bRetryable);
		return Data;
	}
}
