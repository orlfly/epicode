#include "EpicodePerformanceTypes.h"

FString LexToString(EEpicodeVisemeSource Source)
{
	switch (Source)
	{
	case EEpicodeVisemeSource::Audio: return TEXT("audio");
	case EEpicodeVisemeSource::LiveLink: return TEXT("livelink");
	case EEpicodeVisemeSource::Preset: return TEXT("preset");
	case EEpicodeVisemeSource::None:
	default:
		return TEXT("none");
	}
}

bool ParseVisemeSource(const FString& Value, EEpicodeVisemeSource& OutSource)
{
	const FString Lower = Value.ToLower();
	if (Lower == TEXT("none"))
	{
		OutSource = EEpicodeVisemeSource::None;
		return true;
	}
	if (Lower == TEXT("audio"))
	{
		OutSource = EEpicodeVisemeSource::Audio;
		return true;
	}
	if (Lower == TEXT("livelink") || Lower == TEXT("live-link") || Lower == TEXT("live_link"))
	{
		OutSource = EEpicodeVisemeSource::LiveLink;
		return true;
	}
	if (Lower == TEXT("preset"))
	{
		OutSource = EEpicodeVisemeSource::Preset;
		return true;
	}
	return false;
}

TSharedPtr<FJsonObject> FEpicodePerformanceState::ToJson() const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("actionTag"), ActionTag);
	TArray<TSharedPtr<FJsonValue>> QueueValues;
	for (const FString& Entry : ActionQueue)
	{
		QueueValues.Add(MakeShared<FJsonValueString>(Entry));
	}
	Json->SetArrayField(TEXT("actionQueue"), QueueValues);
	Json->SetStringField(TEXT("emotion"), Emotion);
	Json->SetNumberField(TEXT("expressionIntensity"), ExpressionIntensity);
	Json->SetBoolField(TEXT("speaking"), bSpeaking);
	Json->SetStringField(TEXT("visemeSource"), LexToString(VisemeSource));
	Json->SetBoolField(TEXT("motionMatchingActive"), bMotionMatchingActive);
	return Json;
}

bool FEpicodePerformanceState::FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodePerformanceState& OutState, FString& OutError)
{
	if (!InJson.IsValid())
	{
		OutError = TEXT("Performance state must be an object.");
		return false;
	}
	InJson->TryGetStringField(TEXT("actionTag"), OutState.ActionTag);
	InJson->TryGetStringField(TEXT("emotion"), OutState.Emotion);
	const TArray<TSharedPtr<FJsonValue>>* QueueValues = nullptr;
	if (InJson->TryGetArrayField(TEXT("actionQueue"), QueueValues) && QueueValues)
	{
		OutState.ActionQueue.Reset();
		for (const TSharedPtr<FJsonValue>& Value : *QueueValues)
		{
			OutState.ActionQueue.Add(Value->AsString());
		}
	}
	double Intensity = OutState.ExpressionIntensity;
	if (InJson->TryGetNumberField(TEXT("expressionIntensity"), Intensity))
	{
		if (!IsValidExpressionIntensity(Intensity))
		{
			OutError = TEXT("INVALID_PARAMS: expressionIntensity must be between 0 and 1.");
			return false;
		}
		OutState.ExpressionIntensity = static_cast<float>(Intensity);
	}
	InJson->TryGetBoolField(TEXT("speaking"), OutState.bSpeaking);
	InJson->TryGetBoolField(TEXT("motionMatchingActive"), OutState.bMotionMatchingActive);
	FString SourceText;
	if (InJson->TryGetStringField(TEXT("visemeSource"), SourceText) && !ParseVisemeSource(SourceText, OutState.VisemeSource))
	{
		OutError = FString::Printf(TEXT("INVALID_PARAMS: unknown viseme source '%s'."), *SourceText);
		return false;
	}
	return true;
}

bool FEpicodePerformanceState::IsValidActionTag(const FString& InTag)
{
	if (InTag.IsEmpty() || InTag.Len() > 64)
	{
		return false;
	}
	for (const TCHAR Character : InTag)
	{
		if (!FChar::IsAlnum(Character) && Character != TEXT('_') && Character != TEXT('-') && Character != TEXT('.'))
		{
			return false;
		}
	}
	return true;
}

bool FEpicodePerformanceState::IsValidEmotion(const FString& InEmotion)
{
	return !InEmotion.IsEmpty() && InEmotion.Len() <= 32;
}

bool FEpicodePerformanceState::IsValidExpressionIntensity(double InIntensity)
{
	return FMath::IsFinite(InIntensity) && InIntensity >= 0.0 && InIntensity <= 1.0;
}

float FEpicodePerformanceRules::ClampIKWeight(double InWeight)
{
	if (!FMath::IsFinite(InWeight))
	{
		return 0.0f;
	}
	return static_cast<float>(FMath::Clamp(InWeight, 0.0, 1.0));
}

bool FEpicodePerformanceRules::CanUseMotionMatching(bool bRequested, bool bMotionDataAvailable)
{
	return bRequested && bMotionDataAvailable;
}

bool FEpicodePerformanceRules::ResolveMotionMatching(bool bRequested, bool bMotionDataAvailable, FString& OutWarning)
{
	OutWarning.Reset();
	const bool bActive = CanUseMotionMatching(bRequested, bMotionDataAvailable);
	if (bRequested && !bMotionDataAvailable)
	{
		OutWarning = TEXT("MOTION_DATA_UNAVAILABLE: falling back to standard movement.");
	}
	return bActive;
}

bool FEpicodePerformanceRules::ApplyAction(FEpicodePerformanceState& InOutState, const FString& InActionTag, bool bInterrupt,
	double InDurationSeconds, double& InOutRemainingSeconds, FString& OutError)
{
	if (!FEpicodePerformanceState::IsValidActionTag(InActionTag))
	{
		OutError = FString::Printf(TEXT("INVALID_PARAMS: invalid action tag '%s'."), *InActionTag);
		return false;
	}
	const double Duration = FMath::Clamp(InDurationSeconds <= 0.0 ? 1.0 : InDurationSeconds, 0.1, 10.0);
	if (bInterrupt)
	{
		InOutState.ActionQueue.Reset();
	}
	if (!InOutState.ActionTag.IsEmpty() && !bInterrupt)
	{
		InOutState.ActionQueue.Add(InActionTag);
	}
	else
	{
		InOutState.ActionTag = InActionTag;
		InOutRemainingSeconds = Duration;
	}
	return true;
}

bool FEpicodePerformanceRules::ResolveVisemeSource(EEpicodeVisemeSource Requested, bool bLiveLinkAvailable, bool bAudioAvailable,
	EEpicodeVisemeSource& OutSource, FString& OutWarning)
{
	OutWarning.Reset();
	switch (Requested)
	{
	case EEpicodeVisemeSource::LiveLink:
		if (!bLiveLinkAvailable)
		{
			OutSource = EEpicodeVisemeSource::Preset;
			OutWarning = TEXT("LIVELINK_UNAVAILABLE: falling back to preset visemes.");
			return true;
		}
		OutSource = Requested;
		return true;
	case EEpicodeVisemeSource::Audio:
		if (!bAudioAvailable)
		{
			OutSource = EEpicodeVisemeSource::Preset;
			OutWarning = TEXT("AUDIO_UNAVAILABLE: falling back to preset visemes.");
			return true;
		}
		OutSource = Requested;
		return true;
	case EEpicodeVisemeSource::Preset:
	case EEpicodeVisemeSource::None:
	default:
		OutSource = Requested;
		return true;
	}
}
