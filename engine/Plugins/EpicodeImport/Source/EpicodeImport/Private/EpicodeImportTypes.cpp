#include "EpicodeImportTypes.h"

#include "EpicodeCommand.h"
#include "EpicodeImportClassifier.h"

namespace
{
	FString StateToString(EEpicodeImportState State)
	{
		switch (State)
		{
		case EEpicodeImportState::Queued:
			return TEXT("queued");
		case EEpicodeImportState::Running:
			return TEXT("running");
		case EEpicodeImportState::Succeeded:
			return TEXT("succeeded");
		case EEpicodeImportState::Failed:
			return TEXT("failed");
		case EEpicodeImportState::Cancelled:
			return TEXT("cancelled");
		default:
			return TEXT("unknown");
		}
	}
}

TSharedPtr<FJsonObject> FEpicodeImportCapability::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("format"), Format);
	Result->SetStringField(TEXT("kind"), FEpicodeImportClassifier::KindToString(Kind));
	Result->SetBoolField(TEXT("available"), bAvailable);
	Result->SetStringField(TEXT("reason"), Reason);
	return Result;
}

TSharedPtr<FJsonObject> FEpicodeImportRequest::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("filePath"), FilePath);
	Result->SetStringField(TEXT("kind"), FEpicodeImportClassifier::KindToString(Kind));
	Result->SetStringField(TEXT("entityName"), EntityName);
	Result->SetStringField(TEXT("destinationPath"), DestinationPath);
	Result->SetObjectField(TEXT("transform"), Transform.ToJson());
	Result->SetBoolField(TEXT("autoPlace"), bAutoPlace);
	Result->SetBoolField(TEXT("replaceExisting"), bReplaceExisting);
	Result->SetBoolField(TEXT("importMaterials"), bImportMaterials);
	Result->SetNumberField(TEXT("scale"), Scale);
	FEpicodeTransform RotationTransform;
	RotationTransform.Rotation = RotationOffset;
	Result->SetObjectField(TEXT("rotationOffset"), RotationTransform.ToJson());
	return Result;
}

bool FEpicodeImportRequest::FromJson(const TSharedPtr<FJsonObject>& Object, FEpicodeImportRequest& OutRequest, FString& OutError)
{
	if (!EpicodeJson::GetRequiredString(Object, TEXT("filePath"), OutRequest.FilePath, OutError))
	{
		return false;
	}
	FString Kind;
	if (EpicodeJson::GetOptionalString(Object, TEXT("kind"), Kind) && !Kind.IsEmpty())
	{
		OutRequest.Kind = FEpicodeImportClassifier::KindFromString(Kind);
		if (OutRequest.Kind == EEpicodeImportKind::Unknown)
		{
			OutError = TEXT("kind must be scene, character or prop.");
			return false;
		}
	}
	else
	{
		OutRequest.Kind = FEpicodeImportClassifier::Classify(OutRequest.FilePath);
	}
	EpicodeJson::GetOptionalString(Object, TEXT("entityName"), OutRequest.EntityName);
	EpicodeJson::GetOptionalString(Object, TEXT("destinationPath"), OutRequest.DestinationPath);
	if (Object.IsValid() && Object->HasField(TEXT("transform")))
	{
		const TSharedPtr<FJsonObject>* TransformObject = nullptr;
		if (Object->TryGetObjectField(TEXT("transform"), TransformObject) && TransformObject)
		{
			if (!FEpicodeTransform::FromJson(*TransformObject, OutRequest.Transform, OutError))
			{
				return false;
			}
		}
	}
	EpicodeJson::GetOptionalBool(Object, TEXT("autoPlace"), OutRequest.bAutoPlace, true);
	EpicodeJson::GetOptionalBool(Object, TEXT("replaceExisting"), OutRequest.bReplaceExisting, false);
	EpicodeJson::GetOptionalBool(Object, TEXT("importMaterials"), OutRequest.bImportMaterials, true);
	double Scale = 1.0;
	EpicodeJson::GetOptionalNumber(Object, TEXT("scale"), Scale, 1.0);
	OutRequest.Scale = static_cast<float>(Scale);
	if (OutRequest.Scale <= 0.0f)
	{
		OutError = TEXT("scale must be greater than zero.");
		return false;
	}
	FVector RotationOffset = FVector::ZeroVector;
	EpicodeJson::GetVector(Object, TEXT("rotationOffset"), RotationOffset, OutError);
	OutRequest.RotationOffset = FRotator(RotationOffset.X, RotationOffset.Y, RotationOffset.Z);
	return true;
}

TSharedPtr<FJsonObject> FEpicodeImportResult::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bSuccess);
	Result->SetStringField(TEXT("errorCode"), ErrorCode);
	Result->SetStringField(TEXT("errorMessage"), ErrorMessage);
	TArray<TSharedPtr<FJsonValue>> AssetValues;
	for (const FString& Asset : Assets)
	{
		AssetValues.Add(MakeShared<FJsonValueString>(Asset));
	}
	TArray<TSharedPtr<FJsonValue>> EntityValues;
	for (const FString& EntityId : EntityIds)
	{
		EntityValues.Add(MakeShared<FJsonValueString>(EntityId));
	}
	TArray<TSharedPtr<FJsonValue>> WarningValues;
	for (const FString& Warning : Warnings)
	{
		WarningValues.Add(MakeShared<FJsonValueString>(Warning));
	}
	Result->SetArrayField(TEXT("assets"), AssetValues);
	Result->SetArrayField(TEXT("entityIds"), EntityValues);
	Result->SetArrayField(TEXT("warnings"), WarningValues);
	return Result;
}

TSharedPtr<FJsonObject> FEpicodeImportJob::ToJson() const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("jobId"), Id);
	Json->SetStringField(TEXT("state"), StateToString(State));
	Json->SetStringField(TEXT("stage"), Stage);
	Json->SetNumberField(TEXT("progress"), Progress);
	Json->SetStringField(TEXT("createdAt"), CreatedAt.ToIso8601());
	if (FinishedAt != FDateTime())
	{
		Json->SetStringField(TEXT("finishedAt"), FinishedAt.ToIso8601());
	}
	Json->SetObjectField(TEXT("request"), Request.ToJson());
	Json->SetObjectField(TEXT("result"), Result.ToJson());
	return Json;
}
