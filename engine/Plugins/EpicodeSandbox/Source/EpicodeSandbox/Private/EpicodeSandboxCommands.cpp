#include "EpicodeSandboxCommands.h"

#include "EpicodeCommand.h"
#include "EpicodeProductionDocument.h"
#include "EpicodeProductionLibrary.h"
#include "EpicodeProductionValidation.h"
#include "EpicodeSandboxSettings.h"
#include "EpicodeSettings.h"

namespace
{
	FString ErrorCode(const FString& InError)
	{
		int32 Colon = INDEX_NONE;
		return InError.FindChar(TEXT(':'), Colon) ? InError.Left(Colon) : InError;
	}

	FEpicodeCommandResult ValidateProduction(const TSharedPtr<FJsonObject>& InParams, const FEpicodeCommandContext& InContext)
	{
		if (!InParams.IsValid())
		{
			return FEpicodeCommandResult::Failure(TEXT("INVALID_PARAMS"), TEXT("production.validate requires parameters"));
		}

		FEpicodeProductionDocument Document;
		if (const TSharedPtr<FJsonObject>* DocumentJson = nullptr; InParams->TryGetObjectField(TEXT("document"), DocumentJson) && DocumentJson && (*DocumentJson).IsValid())
		{
			FString Error;
			if (!FEpicodeProductionDocument::FromJson(*DocumentJson, Document, Error))
			{
				return FEpicodeCommandResult::Failure(ErrorCode(Error), Error);
			}
		}
		else
		{
			FString ProductionId;
			if (!InParams->TryGetStringField(TEXT("productionId"), ProductionId) || ProductionId.IsEmpty())
			{
				return FEpicodeCommandResult::Failure(TEXT("INVALID_PARAMS"), TEXT("production.validate requires productionId or document"));
			}
			const UEpicodeSandboxSettings* Settings = UEpicodeSandboxSettings::Get();
			const FString Root = Settings ? Settings->GetProductionRootPath() : FString();
			FString Error;
			const FString Path = FEpicodeProductionLibrary::GetDocumentPath(Root, ProductionId);
			if (!FEpicodeProductionLibrary::Load(Path, Document, Error))
			{
				return FEpicodeCommandResult::Failure(ErrorCode(Error), Error);
			}
		}

		FString SceneRoot;
		if (!InParams->TryGetStringField(TEXT("sceneRoot"), SceneRoot) || SceneRoot.IsEmpty())
		{
			const UEpicodeSettings* Settings = UEpicodeSettings::Get();
			SceneRoot = Settings ? Settings->GetSceneRootPath() : FString();
		}

		FEpicodeProductionValidationResult Validation;
		FEpicodeProductionValidator::ValidateBinding(Document, SceneRoot, Validation);
		TSharedPtr<FJsonObject> Result = Validation.ToJson();
		Result->SetStringField(TEXT("productionId"), Document.ProductionId);
		Result->SetStringField(TEXT("name"), Document.Name);
		if (!Validation.bSuccess)
		{
			FEpicodeCommandResult Failure = FEpicodeCommandResult::Failure(Validation.GetFirstCode(), Validation.GetFirstError());
			Failure.Result = Result;
			return Failure;
		}
		return FEpicodeCommandResult::Success(Result);
	}
}

void FEpicodeSandboxCommands::Register(FEpicodeCommandRegistry& InRegistry)
{
	InRegistry.Register(
		TEXT("production.validate"),
		TEXT("Validate a production package against its scene."),
		&ValidateProduction,
		false);
}
