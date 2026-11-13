#include "EpicodeEnvironmentController.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "EpicodeTypes.h"

namespace
{
	double ReadNumber(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field, double DefaultValue)
	{
		double Value = DefaultValue;
		if (Json.IsValid())
		{
			Json->TryGetNumberField(Field, Value);
		}
		return Value;
	}
}

void FEpicodeEnvironmentController::Initialize(UWorld* InWorld)
{
	World = InWorld;
	Current = MakeEpicodeDefaultEnvironment();
	EnsureActors();
}

void FEpicodeEnvironmentController::Shutdown()
{
	auto DestroyActor = [](TWeakObjectPtr<AActor> Actor)
	{
		if (Actor.IsValid())
		{
			Actor->Destroy();
		}
	};
	if (SkyActor.IsValid()) { SkyActor->Destroy(); }
	if (SunActor.IsValid()) { SunActor->Destroy(); }
	if (FogActor.IsValid()) { FogActor->Destroy(); }
	if (CloudActor.IsValid()) { CloudActor->Destroy(); }
	if (PostProcessActor.IsValid()) { PostProcessActor->Destroy(); }
	SkyActor.Reset();
	SunActor.Reset();
	FogActor.Reset();
	CloudActor.Reset();
	PostProcessActor.Reset();
	World.Reset();
	Current.Reset();
}

void FEpicodeEnvironmentController::EnsureActors()
{
	UWorld* InWorld = World.Get();
	if (!InWorld)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (!SkyActor.IsValid())
	{
		SkyActor = InWorld->SpawnActor<ASkyAtmosphere>(ASkyAtmosphere::StaticClass(), FTransform::Identity, Params);
		if (SkyActor.IsValid())
		{
#if WITH_EDITOR
			SkyActor->SetActorLabel(TEXT("Epicode Sky Atmosphere"));
#endif
		}
	}
	if (!SunActor.IsValid())
	{
		SunActor = InWorld->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform::Identity, Params);
		if (SunActor.IsValid())
		{
#if WITH_EDITOR
			SunActor->SetActorLabel(TEXT("Epicode Sun"));
#endif
			if (ULightComponent* Light = SunActor->GetLightComponent())
			{
				Light->SetMobility(EComponentMobility::Movable);
				Light->SetIntensity(6.0f);
			}
		}
	}
	if (!FogActor.IsValid())
	{
		FogActor = InWorld->SpawnActor<AExponentialHeightFog>(AExponentialHeightFog::StaticClass(), FTransform::Identity, Params);
		if (FogActor.IsValid())
		{
#if WITH_EDITOR
			FogActor->SetActorLabel(TEXT("Epicode Height Fog"));
#endif
		}
	}
	if (!CloudActor.IsValid())
	{
		CloudActor = InWorld->SpawnActor<AVolumetricCloud>(AVolumetricCloud::StaticClass(), FTransform::Identity, Params);
		if (CloudActor.IsValid())
		{
#if WITH_EDITOR
			CloudActor->SetActorLabel(TEXT("Epicode Volumetric Clouds"));
#endif
		}
	}
	if (!PostProcessActor.IsValid())
	{
		PostProcessActor = InWorld->SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass(), FTransform::Identity, Params);
		if (PostProcessActor.IsValid())
		{
#if WITH_EDITOR
			PostProcessActor->SetActorLabel(TEXT("Epicode Post Process"));
#endif
			PostProcessActor->bUnbound = true;
			PostProcessActor->BlendWeight = 1.0f;
		}
	}
}

bool FEpicodeEnvironmentController::Apply(const TSharedPtr<FJsonObject>& InEnvironment, FString& OutError)
{
	if (!InEnvironment.IsValid())
	{
		OutError = TEXT("environment must be an object");
		return false;
	}
	const double TimeOfDay = ReadNumber(InEnvironment, TEXT("timeOfDay"), 15.0);
	const double FogDensity = ReadNumber(InEnvironment, TEXT("fogDensity"), 0.02);
	const double CloudCoverage = ReadNumber(InEnvironment, TEXT("cloudCoverage"), 0.3);
	const double Exposure = ReadNumber(InEnvironment, TEXT("exposureCompensation"), 0.0);
	const double Bloom = ReadNumber(InEnvironment, TEXT("bloomIntensity"), 0.675);
	if (TimeOfDay < 0.0 || TimeOfDay > 24.0)
	{
		OutError = TEXT("environment.timeOfDay must be between 0 and 24");
		return false;
	}
	if (FogDensity < 0.0)
	{
		OutError = TEXT("environment.fogDensity must not be negative");
		return false;
	}
	if (CloudCoverage < 0.0 || CloudCoverage > 1.0)
	{
		OutError = TEXT("environment.cloudCoverage must be between 0 and 1");
		return false;
	}
	if (Exposure < -10.0 || Exposure > 10.0)
	{
		OutError = TEXT("environment.exposureCompensation is out of range");
		return false;
	}
	if (Bloom < 0.0)
	{
		OutError = TEXT("environment.bloomIntensity must not be negative");
		return false;
	}

	EnsureActors();
	if (!SunActor.IsValid() || !FogActor.IsValid() || !CloudActor.IsValid() || !PostProcessActor.IsValid())
	{
		OutError = TEXT("environment actors are not available in the current world");
		return false;
	}

	ApplyTimeOfDay(TimeOfDay);
	if (UExponentialHeightFogComponent* Fog = FogActor->GetComponent())
	{
		Fog->SetFogDensity(static_cast<float>(FogDensity));
	}
	if (UVolumetricCloudComponent* Cloud = CloudActor->FindComponentByClass<UVolumetricCloudComponent>())
	{
		Cloud->SetLayerBottomAltitude(2000.0f);
		Cloud->SetLayerHeight(500.0f + static_cast<float>(CloudCoverage) * 3500.0f);
	}
	ApplyPostProcess(Exposure, Bloom);
	Current = InEnvironment;
	return true;
}

TSharedPtr<FJsonObject> FEpicodeEnvironmentController::Capture() const
{
	return Current.IsValid() ? Current : MakeEpicodeDefaultEnvironment();
}

void FEpicodeEnvironmentController::ApplyTimeOfDay(double InTimeOfDay)
{
	if (SunActor.IsValid())
	{
		const double Pitch = -(InTimeOfDay - 6.0) * 15.0;
		SunActor->SetActorRotation(FRotator(Pitch, -90.0, 0.0));
		if (ULightComponent* Light = SunActor->GetLightComponent())
		{
			const double Solar = FMath::Clamp(FMath::Sin((InTimeOfDay - 6.0) / 12.0 * PI), 0.0, 1.0);
			Light->SetIntensity(static_cast<float>(FMath::Lerp(0.05, 10.0, Solar)));
			Light->SetLightColor(FLinearColor::LerpUsingHSV(FLinearColor(1.0f, 0.42f, 0.18f), FLinearColor(1.0f, 0.96f, 0.88f), static_cast<float>(Solar)));
		}
	}
	if (SkyActor.IsValid())
	{
		if (USkyAtmosphereComponent* Sky = SkyActor->GetComponent())
		{
			const double Daylight = FMath::Clamp(FMath::Sin((InTimeOfDay - 6.0) / 12.0 * PI), 0.0, 1.0);
			Sky->SetSkyLuminanceFactor(FLinearColor::LerpUsingHSV(FLinearColor(0.02f, 0.03f, 0.08f), FLinearColor(1.0f, 1.0f, 1.0f), static_cast<float>(Daylight)));
		}
	}
}

void FEpicodeEnvironmentController::ApplyPostProcess(double InExposure, double InBloom)
{
	if (!PostProcessActor.IsValid())
	{
		return;
	}
	FPostProcessSettings& Settings = PostProcessActor->Settings;
	Settings.bOverride_AutoExposureBias = true;
	Settings.AutoExposureBias = static_cast<float>(InExposure);
	Settings.bOverride_BloomIntensity = true;
	Settings.BloomIntensity = static_cast<float>(InBloom);
}

TArray<FString> FEpicodeEnvironmentController::GetPresetNames()
{
	return { TEXT("clear"), TEXT("overcast"), TEXT("sunset"), TEXT("night") };
}

TSharedPtr<FJsonObject> FEpicodeEnvironmentController::MakePreset(const FString& InName)
{
	TSharedPtr<FJsonObject> Preset = MakeEpicodeDefaultEnvironment();
	Preset->SetStringField(TEXT("preset"), InName);
	if (InName.Equals(TEXT("overcast"), ESearchCase::IgnoreCase))
	{
		Preset->SetNumberField(TEXT("timeOfDay"), 12.0);
		Preset->SetNumberField(TEXT("fogDensity"), 0.08);
		Preset->SetNumberField(TEXT("cloudCoverage"), 0.85);
		Preset->SetNumberField(TEXT("exposureCompensation"), -0.5);
		Preset->SetNumberField(TEXT("bloomIntensity"), 0.4);
	}
	else if (InName.Equals(TEXT("sunset"), ESearchCase::IgnoreCase))
	{
		Preset->SetNumberField(TEXT("timeOfDay"), 18.5);
		Preset->SetNumberField(TEXT("fogDensity"), 0.05);
		Preset->SetNumberField(TEXT("cloudCoverage"), 0.5);
		Preset->SetNumberField(TEXT("exposureCompensation"), 0.2);
		Preset->SetNumberField(TEXT("bloomIntensity"), 0.9);
	}
	else if (InName.Equals(TEXT("night"), ESearchCase::IgnoreCase))
	{
		Preset->SetNumberField(TEXT("timeOfDay"), 23.0);
		Preset->SetNumberField(TEXT("fogDensity"), 0.1);
		Preset->SetNumberField(TEXT("cloudCoverage"), 0.2);
		Preset->SetNumberField(TEXT("exposureCompensation"), 0.8);
		Preset->SetNumberField(TEXT("bloomIntensity"), 1.1);
	}
	else
	{
		Preset->SetStringField(TEXT("preset"), TEXT("clear"));
	}
	return Preset;
}
