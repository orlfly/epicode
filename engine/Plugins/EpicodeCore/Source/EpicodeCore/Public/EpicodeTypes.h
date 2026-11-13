#pragma once

#include "CoreMinimal.h"
#include "EpicodeTypes.generated.h"

UENUM(BlueprintType)
enum class EEpicodeEntityType : uint8
{
	Group,
	Character,
	Prop,
	Light,
	Camera
};

USTRUCT(BlueprintType)
struct EPICODECORE_API FEpicodeTransform
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Epicode")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Epicode")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Epicode")
	FVector Scale = FVector::OneVector;

	bool IsFinite() const;
	FTransform ToFTransform() const;
	static FEpicodeTransform FromFTransform(const FTransform& InTransform);
	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeTransform& OutTransform, FString& OutError);
};

struct EPICODECORE_API FEpicodeEntityRecord
{
	FGuid Id;
	EEpicodeEntityType Type = EEpicodeEntityType::Group;
	FString Name;
	FGuid ParentId;
	bool bVisible = true;
	TArray<FString> Tags;
	FEpicodeTransform Transform;
	TSharedPtr<FJsonObject> Data;

	FEpicodeEntityRecord();
	bool IsValid() const;
	FString GetTypeName() const;
	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeEntityRecord& OutRecord, FString& OutError);
};

struct EPICODECORE_API FEpicodeCamera
{
	static constexpr double DefaultSensorWidthMillimeters = 36.0;

	// Converts a 35mm-equivalent focal length to a horizontal field of view in degrees.
	static double FocalLengthToFieldOfView(double FocalLengthMillimeters, double SensorWidthMillimeters = DefaultSensorWidthMillimeters);
	static double FieldOfViewToFocalLength(double FieldOfViewDegrees, double SensorWidthMillimeters = DefaultSensorWidthMillimeters);
};

EPICODECORE_API FString LexToString(EEpicodeEntityType Type);
EPICODECORE_API bool ParseEntityType(const FString& InValue, EEpicodeEntityType& OutType);
EPICODECORE_API TSharedPtr<FJsonObject> MakeEpicodeDefaultEnvironment();
