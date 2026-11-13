#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "EpicodePerformanceTypes.generated.h"

UENUM(BlueprintType)
enum class EEpicodeVisemeSource : uint8
{
	None,
	Audio,
	LiveLink,
	Preset
};

EPICODENETWORK_API FString LexToString(EEpicodeVisemeSource Source);
EPICODENETWORK_API bool ParseVisemeSource(const FString& Value, EEpicodeVisemeSource& OutSource);

/** Replicated performance state owned by the authoritative server. */
USTRUCT(BlueprintType)
struct EPICODENETWORK_API FEpicodePerformanceState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Epicode|Performance")
	FString ActionTag;

	UPROPERTY(BlueprintReadOnly, Category = "Epicode|Performance")
	TArray<FString> ActionQueue;

	UPROPERTY(BlueprintReadOnly, Category = "Epicode|Performance")
	FString Emotion = TEXT("neutral");

	UPROPERTY(BlueprintReadOnly, Category = "Epicode|Performance", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ExpressionIntensity = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Epicode|Performance")
	bool bSpeaking = false;

	UPROPERTY(BlueprintReadOnly, Category = "Epicode|Performance")
	EEpicodeVisemeSource VisemeSource = EEpicodeVisemeSource::None;

	UPROPERTY(BlueprintReadOnly, Category = "Epicode|Performance")
	bool bMotionMatchingActive = false;

	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodePerformanceState& OutState, FString& OutError);

	static bool IsValidActionTag(const FString& InTag);
	static bool IsValidEmotion(const FString& InEmotion);
	static bool IsValidExpressionIntensity(double InIntensity);
};

/** Pure rules for local-only IK and Motion Matching fallback. */
struct EPICODENETWORK_API FEpicodePerformanceRules
{
	static float ClampIKWeight(double InWeight);
	static bool CanUseMotionMatching(bool bRequested, bool bMotionDataAvailable);
	/** Returns whether Motion Matching is active and the warning used when requested data is missing. */
	static bool ResolveMotionMatching(bool bRequested, bool bMotionDataAvailable, FString& OutWarning);
	static bool ApplyAction(FEpicodePerformanceState& InOutState, const FString& InActionTag, bool bInterrupt,
		double InDurationSeconds, double& InOutRemainingSeconds, FString& OutError);
	static bool ResolveVisemeSource(EEpicodeVisemeSource Requested, bool bLiveLinkAvailable, bool bAudioAvailable,
		EEpicodeVisemeSource& OutSource, FString& OutWarning);
};
