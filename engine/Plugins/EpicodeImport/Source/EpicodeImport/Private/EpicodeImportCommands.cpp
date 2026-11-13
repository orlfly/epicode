#include "EpicodeImportCommands.h"

#include "EpicodeCommand.h"
#include "EpicodeImportClassifier.h"
#include "EpicodeImportService.h"

namespace
{
	FEpicodeCommandResult FailureFrom(const FString& Code, const FString& Message)
	{
		return FEpicodeCommandResult::Failure(Code, Message);
	}

	FEpicodeCommandResult SubmitImport(const TSharedPtr<FJsonObject>& Params, EEpicodeImportKind ForcedKind)
	{
		FEpicodeImportRequest Request;
		FString Error;
		if (!FEpicodeImportRequest::FromJson(Params, Request, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error);
		}
		if (ForcedKind != EEpicodeImportKind::Unknown)
		{
			Request.Kind = ForcedKind;
		}
		if (Request.Kind == EEpicodeImportKind::Unknown)
		{
			Request.Kind = FEpicodeImportClassifier::Classify(Request.FilePath);
		}
		TSharedPtr<FEpicodeImportJob> Job = FEpicodeImportService::Get().Submit(Request, Error);
		if (!Job.IsValid())
		{
			return FailureFrom(TEXT("IMPORT_REJECTED"), Error);
		}
		return FEpicodeCommandResult::Success(Job->ToJson());
	}

	FEpicodeCommandResult RunImportSync(const TSharedPtr<FJsonObject>& Params)
	{
		FEpicodeImportRequest Request;
		FString Error;
		if (!FEpicodeImportRequest::FromJson(Params, Request, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error);
		}
		if (Request.Kind == EEpicodeImportKind::Unknown)
		{
			Request.Kind = FEpicodeImportClassifier::Classify(Request.FilePath);
		}
		FEpicodeImportResult Result;
		const bool bOk = FEpicodeImportService::Get().ImportSynchronously(Request, Result);
		if (!bOk)
		{
			return FailureFrom(Result.ErrorCode.IsEmpty() ? TEXT("IMPORT_FAILED") : Result.ErrorCode, Result.ErrorMessage);
		}
		return FEpicodeCommandResult::Success(Result.ToJson(), Result.EntityIds);
	}
}

void FEpicodeImportCommands::Register(FEpicodeCommandRegistry& Registry)
{
	Registry.Register(TEXT("import.capabilities"), TEXT("List supported import formats."), [](const TSharedPtr<FJsonObject>&, const FEpicodeCommandContext&)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FEpicodeImportCapability& Capability : FEpicodeImportClassifier::Capabilities())
		{
			Values.Add(MakeShared<FJsonValueObject>(Capability.ToJson()));
		}
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("capabilities"), Values);
		return FEpicodeCommandResult::Success(Result);
	}, false);

	Registry.Register(TEXT("import.classify"), TEXT("Classify an import file by extension and name."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString FilePath;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("filePath"), FilePath, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error);
		}
		const EEpicodeImportKind Kind = FEpicodeImportClassifier::Classify(FilePath);
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("kind"), FEpicodeImportClassifier::KindToString(Kind));
		Result->SetBoolField(TEXT("supported"), Kind != EEpicodeImportKind::Unknown);
		Result->SetStringField(TEXT("extension"), FPaths::GetExtension(FilePath).ToLower());
		return FEpicodeCommandResult::Success(Result);
	}, false);

	Registry.Register(TEXT("import.submit"), TEXT("Submit an asynchronous asset import job."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		return SubmitImport(Params, EEpicodeImportKind::Unknown);
	});

	Registry.Register(TEXT("import.file"), TEXT("Run an asset import synchronously."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		return RunImportSync(Params);
	});

	Registry.Register(TEXT("import.run"), TEXT("Run an asset import synchronously (alias of import.file)."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		return RunImportSync(Params);
	});

	Registry.Register(TEXT("scene.import"), TEXT("Import a scene or mesh asset and add it to the current scene."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		return SubmitImport(Params, EEpicodeImportKind::Scene);
	});

	Registry.Register(TEXT("character.import"), TEXT("Import a character asset and add it to the current scene."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		return SubmitImport(Params, EEpicodeImportKind::Character);
	});

	Registry.Register(TEXT("prop.import"), TEXT("Import a prop asset and add it to the current scene."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		return SubmitImport(Params, EEpicodeImportKind::Prop);
	});

	Registry.Register(TEXT("import.status"), TEXT("Return an import job."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString JobId;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("jobId"), JobId, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error);
		}
		TSharedPtr<FEpicodeImportJob> Job = FEpicodeImportService::Get().GetJob(JobId);
		if (!Job.IsValid())
		{
			return FailureFrom(TEXT("JOB_NOT_FOUND"), TEXT("Import job not found."));
		}
		return FEpicodeCommandResult::Success(Job->ToJson());
	}, false);

	Registry.Register(TEXT("import.list"), TEXT("List import jobs."), [](const TSharedPtr<FJsonObject>&, const FEpicodeCommandContext&)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const TSharedPtr<FEpicodeImportJob>& Job : FEpicodeImportService::Get().GetJobs())
		{
			if (Job.IsValid())
			{
				Values.Add(MakeShared<FJsonValueObject>(Job->ToJson()));
			}
		}
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("jobs"), Values);
		return FEpicodeCommandResult::Success(Result);
	}, false);

	Registry.Register(TEXT("import.cancel"), TEXT("Cancel a queued or running import job."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString JobId;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("jobId"), JobId, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error);
		}
		if (!FEpicodeImportService::Get().Cancel(JobId))
		{
			return FailureFrom(TEXT("JOB_NOT_CANCELLABLE"), TEXT("Import job is not running or the job was not found."));
		}
		TSharedPtr<FEpicodeImportJob> Job = FEpicodeImportService::Get().GetJob(JobId);
		return FEpicodeCommandResult::Success(Job.IsValid() ? Job->ToJson() : MakeShared<FJsonObject>());
	});
}
