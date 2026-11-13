#include "EpicodeCommandEnvelope.h"

namespace EpicodeCommandEnvelope
{
	bool MatchesStateVersion(double ExpectedStateVersion, uint64 CurrentStateVersion)
	{
		return ExpectedStateVersion < 0.0 || static_cast<uint64>(ExpectedStateVersion) == CurrentStateVersion;
	}

	TSharedPtr<FJsonObject> Build(
		const FString& Method,
		const FString& RequestId,
		const FEpicodeCommandResult& Result,
		int32 Status,
		bool bReplay,
		uint64 EventSequence)
	{
		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetBoolField(TEXT("ok"), Result.bSuccess);
		Json->SetStringField(TEXT("requestId"), RequestId.IsEmpty() ? TEXT("unknown") : RequestId);
		Json->SetStringField(TEXT("method"), Method);
		Json->SetNumberField(TEXT("httpStatus"), Status);
		Json->SetBoolField(TEXT("idempotentReplay"), bReplay);
		if (Result.bSuccess && Result.Result.IsValid())
		{
			double StateVersion = 0.0;
			if (Result.Result->TryGetNumberField(TEXT("stateVersion"), StateVersion))
			{
				Json->SetNumberField(TEXT("stateVersion"), StateVersion);
			}
			Json->SetObjectField(TEXT("result"), Result.Result);
		}
		else if (Result.bSuccess)
		{
			Json->SetObjectField(TEXT("result"), MakeShared<FJsonObject>());
		}
		else
		{
			TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
			Error->SetStringField(TEXT("code"), Result.ErrorCode);
			Error->SetStringField(TEXT("message"), Result.ErrorMessage);
			Error->SetStringField(TEXT("detail"), Result.ErrorDetail);
			Error->SetBoolField(TEXT("retryable"), Result.bRetryable);
			Json->SetObjectField(TEXT("error"), Error);
		}
		TArray<TSharedPtr<FJsonValue>> Affected;
		for (const FString& Id : Result.AffectedIds)
		{
			Affected.Add(MakeShared<FJsonValueString>(Id));
		}
		Json->SetArrayField(TEXT("affected"), Affected);
		TArray<TSharedPtr<FJsonValue>> Warnings;
		for (const FString& Warning : Result.Warnings)
		{
			Warnings.Add(MakeShared<FJsonValueString>(Warning));
		}
		Json->SetArrayField(TEXT("warnings"), Warnings);
		Json->SetObjectField(TEXT("command"), Result.ToJson());
		Json->SetNumberField(TEXT("eventSequence"), static_cast<double>(EventSequence));
		return Json;
	}
}
