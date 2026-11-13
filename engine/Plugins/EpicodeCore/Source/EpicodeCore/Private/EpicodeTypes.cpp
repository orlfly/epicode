#include "EpicodeTypes.h"

#include "Dom/JsonObject.h"
#include "Math/UnrealMathUtility.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool FEpicodeTransform::IsFinite() const
{
	return FMath::IsFinite(Location.X) && FMath::IsFinite(Location.Y) && FMath::IsFinite(Location.Z)
		&& FMath::IsFinite(Rotation.Pitch) && FMath::IsFinite(Rotation.Yaw) && FMath::IsFinite(Rotation.Roll)
		&& FMath::IsFinite(Scale.X) && FMath::IsFinite(Scale.Y) && FMath::IsFinite(Scale.Z);
}

FTransform FEpicodeTransform::ToFTransform() const
{
	return FTransform(Rotation, Location, Scale);
}

FEpicodeTransform FEpicodeTransform::FromFTransform(const FTransform& InTransform)
{
	FEpicodeTransform Result;
	Result.Location = InTransform.GetLocation();
	Result.Rotation = InTransform.Rotator();
	Result.Scale = InTransform.GetScale3D();
	return Result;
}

TSharedPtr<FJsonObject> FEpicodeTransform::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("location"), { MakeShared<FJsonValueNumber>(Location.X), MakeShared<FJsonValueNumber>(Location.Y), MakeShared<FJsonValueNumber>(Location.Z) });
	Result->SetArrayField(TEXT("rotation"), { MakeShared<FJsonValueNumber>(Rotation.Pitch), MakeShared<FJsonValueNumber>(Rotation.Yaw), MakeShared<FJsonValueNumber>(Rotation.Roll) });
	Result->SetArrayField(TEXT("scale"), { MakeShared<FJsonValueNumber>(Scale.X), MakeShared<FJsonValueNumber>(Scale.Y), MakeShared<FJsonValueNumber>(Scale.Z) });
	return Result;
}

namespace
{
	bool ReadVector(const TSharedPtr<FJsonObject>& InJson, const TCHAR* Field, FVector& OutValue, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!InJson->TryGetArrayField(Field, Values) || Values == nullptr || Values->Num() != 3)
		{
			OutError = FString::Printf(TEXT("transform.%s must be an array of three numbers"), Field);
			return false;
		}
		OutValue = FVector((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
		return true;
	}

	bool ReadRotator(const TSharedPtr<FJsonObject>& InJson, const TCHAR* Field, FRotator& OutValue, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!InJson->TryGetArrayField(Field, Values) || Values == nullptr || Values->Num() != 3)
		{
			OutError = FString::Printf(TEXT("transform.%s must be an array of three numbers"), Field);
			return false;
		}
		OutValue = FRotator((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
		return true;
	}
}

bool FEpicodeTransform::FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeTransform& OutTransform, FString& OutError)
{
	if (!InJson.IsValid())
	{
		OutError = TEXT("transform must be an object");
		return false;
	}
	if (!ReadVector(InJson, TEXT("location"), OutTransform.Location, OutError)
		|| !ReadRotator(InJson, TEXT("rotation"), OutTransform.Rotation, OutError)
		|| !ReadVector(InJson, TEXT("scale"), OutTransform.Scale, OutError))
	{
		return false;
	}
	if (!OutTransform.IsFinite())
	{
		OutError = TEXT("transform contains a non-finite value");
		return false;
	}
	return true;
}

FEpicodeEntityRecord::FEpicodeEntityRecord()
	: Data(MakeShared<FJsonObject>())
{
}

bool FEpicodeEntityRecord::IsValid() const
{
	return Id.IsValid() && !Name.IsEmpty() && Transform.IsFinite() && Data.IsValid();
}

FString FEpicodeEntityRecord::GetTypeName() const
{
	return LexToString(Type);
}

TSharedPtr<FJsonObject> FEpicodeEntityRecord::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("id"), Id.ToString(EGuidFormats::DigitsWithHyphens));
	Result->SetStringField(TEXT("type"), GetTypeName());
	Result->SetStringField(TEXT("name"), Name);
	if (ParentId.IsValid())
	{
		Result->SetStringField(TEXT("parentId"), ParentId.ToString(EGuidFormats::DigitsWithHyphens));
	}
	Result->SetBoolField(TEXT("visible"), bVisible);
	TArray<TSharedPtr<FJsonValue>> TagValues;
	for (const FString& Tag : Tags)
	{
		TagValues.Add(MakeShared<FJsonValueString>(Tag));
	}
	Result->SetArrayField(TEXT("tags"), TagValues);
	Result->SetObjectField(TEXT("transform"), Transform.ToJson());
	Result->SetObjectField(GetTypeName().ToLower(), Data);
	return Result;
}

bool FEpicodeEntityRecord::FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeEntityRecord& OutRecord, FString& OutError)
{
	if (!InJson.IsValid())
	{
		OutError = TEXT("entity must be an object");
		return false;
	}
	FString IdString;
	if (!InJson->TryGetStringField(TEXT("id"), IdString) || !FGuid::Parse(IdString, OutRecord.Id))
	{
		OutError = TEXT("entity.id must be a valid GUID");
		return false;
	}
	FString TypeString;
	if (!InJson->TryGetStringField(TEXT("type"), TypeString) || !ParseEntityType(TypeString, OutRecord.Type))
	{
		OutError = TEXT("entity.type is invalid");
		return false;
	}
	if (!InJson->TryGetStringField(TEXT("name"), OutRecord.Name) || OutRecord.Name.IsEmpty())
	{
		OutError = TEXT("entity.name is required");
		return false;
	}
	FString ParentString;
	if (InJson->TryGetStringField(TEXT("parentId"), ParentString) && !ParentString.IsEmpty())
	{
		if (!FGuid::Parse(ParentString, OutRecord.ParentId))
		{
			OutError = TEXT("entity.parentId must be a valid GUID");
			return false;
		}
	}
	InJson->TryGetBoolField(TEXT("visible"), OutRecord.bVisible);
	const TArray<TSharedPtr<FJsonValue>>* TagValues = nullptr;
	if (InJson->TryGetArrayField(TEXT("tags"), TagValues) && TagValues)
	{
		for (const TSharedPtr<FJsonValue>& Value : *TagValues)
		{
			OutRecord.Tags.Add(Value->AsString());
		}
	}
	const TSharedPtr<FJsonObject>* TransformJson = nullptr;
	if (!InJson->TryGetObjectField(TEXT("transform"), TransformJson) || !TransformJson)
	{
		OutError = TEXT("entity.transform is required");
		return false;
	}
	if (!FEpicodeTransform::FromJson(*TransformJson, OutRecord.Transform, OutError))
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* Payload = nullptr;
	if (InJson->TryGetObjectField(OutRecord.GetTypeName().ToLower(), Payload) && Payload)
	{
		OutRecord.Data = *Payload;
	}
	else
	{
		OutRecord.Data = MakeShared<FJsonObject>();
	}
	return OutRecord.IsValid();
}

FString LexToString(EEpicodeEntityType Type)
{
	switch (Type)
	{
	case EEpicodeEntityType::Character: return TEXT("character");
	case EEpicodeEntityType::Prop: return TEXT("prop");
	case EEpicodeEntityType::Light: return TEXT("light");
	case EEpicodeEntityType::Camera: return TEXT("camera");
	case EEpicodeEntityType::Group:
	default: return TEXT("group");
	}
}

bool ParseEntityType(const FString& InValue, EEpicodeEntityType& OutType)
{
	if (InValue.Equals(TEXT("group"), ESearchCase::IgnoreCase)) { OutType = EEpicodeEntityType::Group; return true; }
	if (InValue.Equals(TEXT("character"), ESearchCase::IgnoreCase)) { OutType = EEpicodeEntityType::Character; return true; }
	if (InValue.Equals(TEXT("prop"), ESearchCase::IgnoreCase)) { OutType = EEpicodeEntityType::Prop; return true; }
	if (InValue.Equals(TEXT("light"), ESearchCase::IgnoreCase)) { OutType = EEpicodeEntityType::Light; return true; }
	if (InValue.Equals(TEXT("camera"), ESearchCase::IgnoreCase)) { OutType = EEpicodeEntityType::Camera; return true; }
	return false;
}

double FEpicodeCamera::FocalLengthToFieldOfView(double FocalLengthMillimeters, double SensorWidthMillimeters)
{
	if (FocalLengthMillimeters <= 0.0 || SensorWidthMillimeters <= 0.0)
	{
		return 90.0;
	}
	const double FieldOfView = FMath::RadiansToDegrees(2.0 * FMath::Atan(SensorWidthMillimeters / (2.0 * FocalLengthMillimeters)));
	return FMath::IsFinite(FieldOfView) ? FieldOfView : 90.0;
}

double FEpicodeCamera::FieldOfViewToFocalLength(double FieldOfViewDegrees, double SensorWidthMillimeters)
{
	if (FieldOfViewDegrees <= 0.0 || FieldOfViewDegrees >= 180.0 || SensorWidthMillimeters <= 0.0)
	{
		return 0.0;
	}
	const double FocalLength = SensorWidthMillimeters / (2.0 * FMath::Tan(FMath::DegreesToRadians(FieldOfViewDegrees) / 2.0));
	return FMath::IsFinite(FocalLength) ? FocalLength : 0.0;
}

TSharedPtr<FJsonObject> MakeEpicodeDefaultEnvironment()
{
	TSharedPtr<FJsonObject> Environment = MakeShared<FJsonObject>();
	Environment->SetNumberField(TEXT("timeOfDay"), 15.0);
	Environment->SetStringField(TEXT("preset"), TEXT("clear"));
	Environment->SetNumberField(TEXT("fogDensity"), 0.02);
	Environment->SetNumberField(TEXT("cloudCoverage"), 0.30);
	Environment->SetNumberField(TEXT("exposureCompensation"), 0.0);
	Environment->SetNumberField(TEXT("bloomIntensity"), 0.675);
	Environment->SetStringField(TEXT("lut"), TEXT(""));
	return Environment;
}
