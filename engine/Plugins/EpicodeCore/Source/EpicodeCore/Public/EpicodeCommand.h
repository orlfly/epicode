#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class UWorld;
struct FEpicodeTransform;
struct FEpicodeEntityRecord;

struct EPICODECORE_API FEpicodeCommandContext
{
	UWorld* World = nullptr;
	FString RequestId;
	FString Source = TEXT("internal");
	FString IdempotencyKey;
	FString ParticipantToken;
	double ExpectedStateVersion = -1.0;
	bool bUndoable = true;
};

struct EPICODECORE_API FEpicodeCommandResult
{
	bool bSuccess = false;
	FString ErrorCode;
	FString ErrorMessage;
	FString ErrorDetail;
	bool bRetryable = false;
	TSharedPtr<FJsonObject> Result;
	TArray<FString> AffectedIds;
	TArray<FString> Warnings;

	static FEpicodeCommandResult Success(const TSharedPtr<FJsonObject>& InResult = nullptr, const TArray<FString>& InAffectedIds = {});
	static FEpicodeCommandResult Failure(const FString& InCode, const FString& InMessage, const FString& InDetail = FString(), bool bInRetryable = false);

	TSharedPtr<FJsonObject> ToJson() const;
};

using FEpicodeCommandHandler = TFunction<FEpicodeCommandResult(const TSharedPtr<FJsonObject>&, const FEpicodeCommandContext&)>;

struct EPICODECORE_API FEpicodeCommandDefinition
{
	FString Name;
	FString Description;
	bool bMutating = true;
	bool bAsync = false;
	FString RequiredParticipantKind;
	bool bRequiresLease = false;
	FEpicodeCommandHandler Handler;
};

class EPICODECORE_API IEpicodeTransactionSink
{
public:
	virtual ~IEpicodeTransactionSink() = default;
	virtual void BeginTransaction(const FString& Description) = 0;
	virtual void CommitTransaction() = 0;
	virtual void CancelTransaction() = 0;
	virtual void MarkModified(UObject* Object) = 0;
};

class EPICODECORE_API FEpicodeCommandRegistry
{
public:
	void Register(const FString& InName, const FString& InDescription, FEpicodeCommandHandler InHandler, bool bInMutating = true, bool bInAsync = false, const FString& InRequiredParticipantKind = FString(), bool bInRequiresLease = false);
	void Unregister(const FString& InName);
	bool Has(const FString& InName) const;
	bool Execute(const FString& InName, const TSharedPtr<FJsonObject>& InParams, const FEpicodeCommandContext& InContext, FEpicodeCommandResult& OutResult, FString& OutError) const;
	TArray<FString> GetMethodNames() const;
	TSharedPtr<FJsonObject> DescribeMethods() const;

private:
	mutable FCriticalSection Lock;
	TMap<FString, FEpicodeCommandDefinition> Definitions;
};

class EPICODECORE_API IEpicodeCommandProvider
{
public:
	virtual ~IEpicodeCommandProvider() = default;
	virtual void RegisterCommands(FEpicodeCommandRegistry& Registry) = 0;
};

class EPICODECORE_API FEpicodeCommandProviderRegistry
{
public:
	static void AddProvider(FName Name, TFunction<void(FEpicodeCommandRegistry&)> Registration);
	static void RemoveProvider(FName Name);
	static void RegisterAll(FEpicodeCommandRegistry& Registry);

private:
	static TMap<FName, TFunction<void(FEpicodeCommandRegistry&)>>& GetProviders();
};

namespace EpicodeJson
{
	EPICODECORE_API bool GetRequiredString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FString& OutValue, FString& OutError);
	EPICODECORE_API bool GetOptionalString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FString& OutValue, const FString& DefaultValue = FString());
	EPICODECORE_API bool GetOptionalNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, double& OutValue, double DefaultValue = 0.0);
	EPICODECORE_API bool GetOptionalBool(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, bool& OutValue, bool DefaultValue = false);
	EPICODECORE_API TSharedPtr<FJsonObject> GetOptionalObject(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field);
	EPICODECORE_API TArray<TSharedPtr<FJsonValue>> GetOptionalArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field);
	EPICODECORE_API bool GetVector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FVector& OutValue, FString& OutError);
	EPICODECORE_API bool GetRotator(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FRotator& OutValue, FString& OutError);
	EPICODECORE_API TSharedPtr<FJsonObject> TransformToJson(const FEpicodeTransform& Transform);
	EPICODECORE_API bool TransformFromJson(const TSharedPtr<FJsonObject>& Object, FEpicodeTransform& OutTransform, FString& OutError);
	EPICODECORE_API TSharedPtr<FJsonObject> EntityToJson(const FEpicodeEntityRecord& Entity);
	EPICODECORE_API TSharedPtr<FJsonObject> MakeResult(const TArray<FString>& AffectedIds, const TArray<FString>& Warnings = {});
	EPICODECORE_API TSharedPtr<FJsonObject> MakeErrorData(const FString& DomainCode, const FString& Detail, bool bRetryable);
}
