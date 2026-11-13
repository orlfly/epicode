#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class AActor;
class AExponentialHeightFog;
class ADirectionalLight;
class APostProcessVolume;
class ASkyAtmosphere;
class AVolumetricCloud;
class UWorld;

class EPICODECORE_API FEpicodeEnvironmentController
{
public:
	void Initialize(UWorld* InWorld);
	void Shutdown();

	bool Apply(const TSharedPtr<FJsonObject>& InEnvironment, FString& OutError);
	TSharedPtr<FJsonObject> Capture() const;
	void EnsureActors();

	static TArray<FString> GetPresetNames();
	static TSharedPtr<FJsonObject> MakePreset(const FString& InName);

private:
	void ApplyTimeOfDay(double InTimeOfDay);
	void ApplyPostProcess(double InExposure, double InBloom);

	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<ASkyAtmosphere> SkyActor;
	TWeakObjectPtr<ADirectionalLight> SunActor;
	TWeakObjectPtr<AExponentialHeightFog> FogActor;
	TWeakObjectPtr<AVolumetricCloud> CloudActor;
	TWeakObjectPtr<APostProcessVolume> PostProcessActor;
	TSharedPtr<FJsonObject> Current;
};
