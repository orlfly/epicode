#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EpicodeSandboxSettings.generated.h"

UCLASS(config = Epicode, defaultconfig, meta = (DisplayName = "Epicode Sandbox"))
class EPICODESANDBOX_API UEpicodeSandboxSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UEpicodeSandboxSettings();

	virtual FName GetContainerName() const override { return TEXT("Project"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("Epicode Sandbox"); }

	static const UEpicodeSandboxSettings* Get() { return GetDefault<UEpicodeSandboxSettings>(); }

	FString GetProductionRootPath() const;
	FString GetProductionTrashRootPath() const;
	FString GetSessionRootPath() const;
	bool Validate(FString& OutError) const;

	UPROPERTY(EditAnywhere, Config, Category = "Runtime")
	bool bEnableSandboxDirector = true;

	UPROPERTY(EditAnywhere, Config, Category = "Runtime")
	bool bEnableReplay = true;

	UPROPERTY(EditAnywhere, Config, Category = "Determinism", meta = (ClampMin = "1", ClampMax = "120"))
	int32 FixedTickRate = 30;

	UPROPERTY(EditAnywhere, Config, Category = "Determinism", meta = (ClampMin = "0"))
	int64 RandomSeed = 0;

	UPROPERTY(EditAnywhere, Config, Category = "Replay", meta = (ClampMin = "1"))
	int32 CheckpointIntervalEvents = 1000;

	UPROPERTY(EditAnywhere, Config, Category = "Replay", meta = (ClampMin = "1"))
	int32 ReplayRetentionHours = 24;

	UPROPERTY(EditAnywhere, Config, Category = "Replay", meta = (ClampMin = "1", ClampMax = "8"))
	int32 MaxConcurrentReplays = 1;

	UPROPERTY(EditAnywhere, Config, Category = "Budget", meta = (ClampMin = "1"))
	int32 MaxEntities = 10000;

	UPROPERTY(EditAnywhere, Config, Category = "Budget", meta = (ClampMin = "1"))
	int32 MaxEventsPerSecond = 1000;

	UPROPERTY(EditAnywhere, Config, Category = "Budget", meta = (ClampMin = "1"))
	int32 MaxDirectorCues = 1000;

	UPROPERTY(EditAnywhere, Config, Category = "Paths")
	FString ProductionRoot = TEXT("Saved/Epicode/Productions");

	UPROPERTY(EditAnywhere, Config, Category = "Paths")
	FString ProductionTrashRoot = TEXT("Saved/Epicode/Trash/Productions");

	UPROPERTY(EditAnywhere, Config, Category = "Paths")
	FString SessionRoot = TEXT("Saved/Epicode/Sessions");

	UPROPERTY(EditAnywhere, Config, Category = "Paths")
	FString RuleSchemaVersion = TEXT("1");
};
