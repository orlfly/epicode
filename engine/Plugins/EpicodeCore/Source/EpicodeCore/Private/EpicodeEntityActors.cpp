#include "EpicodeEntityActors.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/LightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/RectLight.h"
#include "Engine/SpotLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Animation/AnimSequenceBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

AEpicodeEntityActor::AEpicodeEntityActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;
	EntityLink = CreateDefaultSubobject<UEpicodeEntityLinkComponent>(TEXT("EntityLink"));
}

void AEpicodeEntityActor::InitializeEntity(const FEpicodeEntityRecord& InRecord)
{
	if (EntityLink)
	{
		EntityLink->EntityId = InRecord.Id;
		EntityLink->EntityType = InRecord.Type;
	}
	ApplyRecord(InRecord);
}

void AEpicodeEntityActor::ApplyRecord(const FEpicodeEntityRecord& InRecord)
{
	SetActorTransform(InRecord.Transform.ToFTransform());
	SetActorHiddenInGame(!InRecord.bVisible);
	SetActorEnableCollision(true);
	if (EntityLink)
	{
		EntityLink->EntityId = InRecord.Id;
		EntityLink->EntityType = InRecord.Type;
	}
}

FGuid AEpicodeEntityActor::GetEntityId() const
{
	return EntityLink ? EntityLink->EntityId : FGuid();
}

AEpicodeCharacterActor::AEpicodeCharacterActor()
{
	MeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("SkeletalMesh"));
	MeshComponent->SetupAttachment(SceneRoot);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
}

void AEpicodeCharacterActor::ApplyRecord(const FEpicodeEntityRecord& InRecord)
{
	Super::ApplyRecord(InRecord);
	FString MeshPath;
	if (InRecord.Data.IsValid())
	{
		InRecord.Data->TryGetStringField(TEXT("skeletalMesh"), MeshPath);
	}
	if (!MeshPath.IsEmpty())
	{
		FString Error;
		SetSkeletalMeshPath(MeshPath, Error);
	}
	FString AnimationPath;
	bool bLoop = true;
	float Rate = 1.0f;
	if (InRecord.Data.IsValid())
	{
		InRecord.Data->TryGetStringField(TEXT("animation"), AnimationPath);
		InRecord.Data->TryGetBoolField(TEXT("loop"), bLoop);
		double Number = 1.0;
		if (InRecord.Data->TryGetNumberField(TEXT("playRate"), Number))
		{
			Rate = static_cast<float>(Number);
		}
	}
	if (!AnimationPath.IsEmpty())
	{
		FString Error;
		PlayAnimationPath(AnimationPath, bLoop, Rate, Error);
	}
}

bool AEpicodeCharacterActor::SetSkeletalMeshPath(const FString& InPath, FString& OutError)
{
	if (!MeshComponent)
	{
		OutError = TEXT("Skeletal mesh component is missing.");
		return false;
	}
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *InPath);
	if (!Mesh)
	{
		OutError = FString::Printf(TEXT("Unable to load skeletal mesh: %s"), *InPath);
		return false;
	}
	MeshComponent->SetSkeletalMeshAsset(Mesh);
	CaptureDefaultMaterials();
	return true;
}

bool AEpicodeCharacterActor::PlayAnimationPath(const FString& InPath, bool bLoop, float InRate, FString& OutError)
{
	if (!MeshComponent)
	{
		OutError = TEXT("Skeletal mesh component is missing.");
		return false;
	}
	UAnimationAsset* Animation = LoadObject<UAnimationAsset>(nullptr, *InPath);
	if (!Animation)
	{
		OutError = FString::Printf(TEXT("Unable to load animation: %s"), *InPath);
		return false;
	}
	MeshComponent->PlayAnimation(Animation, bLoop);
	MeshComponent->SetPlayRate(InRate);
	return true;
}

void AEpicodeCharacterActor::PauseAnimation(bool bPause)
{
	if (MeshComponent)
	{
		MeshComponent->bPauseAnims = bPause;
	}
}

bool AEpicodeCharacterActor::SetAnimationTime(float InSeconds, FString& OutError)
{
	if (!MeshComponent)
	{
		OutError = TEXT("Skeletal mesh component is missing.");
		return false;
	}
	MeshComponent->SetPosition(InSeconds);
	return true;
}

void AEpicodeCharacterActor::SetAnimationRate(float InRate)
{
	if (MeshComponent)
	{
		MeshComponent->SetPlayRate(InRate);
	}
}

bool AEpicodeCharacterActor::SetMaterialOverride(int32 InSlot, const FString& InMaterialPath, FString& OutError)
{
	if (!MeshComponent)
	{
		OutError = TEXT("Skeletal mesh component is missing.");
		return false;
	}
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *InMaterialPath);
	if (!Material)
	{
		OutError = FString::Printf(TEXT("Unable to load material: %s"), *InMaterialPath);
		return false;
	}
	UMaterialInstanceDynamic* Dynamic = UMaterialInstanceDynamic::Create(Material, this);
	if (!Dynamic)
	{
		OutError = TEXT("Unable to create dynamic material instance.");
		return false;
	}
	if (OverrideMaterials.Num() <= InSlot)
	{
		OverrideMaterials.SetNum(InSlot + 1);
	}
	OverrideMaterials[InSlot] = Dynamic;
	MeshComponent->SetMaterial(InSlot, Dynamic);
	return true;
}

void AEpicodeCharacterActor::ClearMaterialOverride(int32 InSlot)
{
	if (MeshComponent && DefaultMaterials.IsValidIndex(InSlot))
	{
		MeshComponent->SetMaterial(InSlot, DefaultMaterials[InSlot]);
	}
	if (OverrideMaterials.IsValidIndex(InSlot))
	{
		OverrideMaterials[InSlot] = nullptr;
	}
}

void AEpicodeCharacterActor::CaptureDefaultMaterials()
{
	if (!MeshComponent)
	{
		return;
	}
	const int32 NumMaterials = MeshComponent->GetNumMaterials();
	DefaultMaterials.Reset();
	DefaultMaterials.Reserve(NumMaterials);
	for (int32 Index = 0; Index < NumMaterials; ++Index)
	{
		DefaultMaterials.Add(MeshComponent->GetMaterial(Index));
	}
	OverrideMaterials.Reset();
	OverrideMaterials.SetNum(NumMaterials);
}

AEpicodePropActor::AEpicodePropActor()
{
	PrimaryActorTick.bCanEverTick = false;
	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMesh"));
	MeshComponent->SetupAttachment(SceneRoot);
	MeshComponent->SetCollisionProfileName(TEXT("BlockAll"));
}

void AEpicodePropActor::ApplyRecord(const FEpicodeEntityRecord& InRecord)
{
	Super::ApplyRecord(InRecord);
	FString MeshPath;
	if (InRecord.Data.IsValid())
	{
		InRecord.Data->TryGetStringField(TEXT("staticMesh"), MeshPath);
	}
	if (!MeshPath.IsEmpty())
	{
		FString Error;
		SetStaticMeshPath(MeshPath, Error);
	}
	if (InRecord.Data.IsValid())
	{
		bool bSimulate = false;
		InRecord.Data->TryGetBoolField(TEXT("simulatePhysics"), bSimulate);
		SetSimulatePhysics(bSimulate);
		FString CollisionProfile;
		if (InRecord.Data->TryGetStringField(TEXT("collisionProfile"), CollisionProfile))
		{
			SetCollisionProfile(CollisionProfile);
		}
		const TArray<TSharedPtr<FJsonValue>>* Overrides = nullptr;
		if (InRecord.Data->TryGetArrayField(TEXT("materialOverrides"), Overrides) && Overrides)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Overrides)
			{
				const TSharedPtr<FJsonObject> Entry = Value->AsObject();
				if (!Entry.IsValid())
				{
					continue;
				}
				int32 Slot = 0;
				Entry->TryGetNumberField(TEXT("slot"), Slot);
				FString MaterialPath;
				if (Entry->TryGetStringField(TEXT("material"), MaterialPath))
				{
					FString Error;
					SetMaterialOverride(Slot, MaterialPath, Error);
				}
			}
		}
	}
}

bool AEpicodePropActor::SetStaticMeshPath(const FString& InPath, FString& OutError)
{
	if (!MeshComponent)
	{
		OutError = TEXT("Static mesh component is missing.");
		return false;
	}
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *InPath);
	if (!Mesh)
	{
		OutError = FString::Printf(TEXT("Unable to load static mesh: %s"), *InPath);
		return false;
	}
	MeshComponent->SetStaticMesh(Mesh);
	CaptureDefaultMaterials();
	return true;
}

bool AEpicodePropActor::SetMaterialOverride(int32 InSlot, const FString& InMaterialPath, FString& OutError)
{
	if (!MeshComponent)
	{
		OutError = TEXT("Static mesh component is missing.");
		return false;
	}
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *InMaterialPath);
	if (!Material)
	{
		OutError = FString::Printf(TEXT("Unable to load material: %s"), *InMaterialPath);
		return false;
	}
	UMaterialInstanceDynamic* Dynamic = UMaterialInstanceDynamic::Create(Material, this);
	if (!Dynamic)
	{
		OutError = TEXT("Unable to create dynamic material instance.");
		return false;
	}
	if (OverrideMaterials.Num() <= InSlot)
	{
		OverrideMaterials.SetNum(InSlot + 1);
	}
	OverrideMaterials[InSlot] = Dynamic;
	MeshComponent->SetMaterial(InSlot, Dynamic);
	return true;
}

void AEpicodePropActor::ClearMaterialOverride(int32 InSlot)
{
	if (MeshComponent && DefaultMaterials.IsValidIndex(InSlot))
	{
		MeshComponent->SetMaterial(InSlot, DefaultMaterials[InSlot]);
	}
	if (OverrideMaterials.IsValidIndex(InSlot))
	{
		OverrideMaterials[InSlot] = nullptr;
	}
}

void AEpicodePropActor::SetSimulatePhysics(bool bEnabled)
{
	if (MeshComponent)
	{
		MeshComponent->SetSimulatePhysics(bEnabled);
	}
}

void AEpicodePropActor::SetCollisionProfile(const FString& InProfile)
{
	if (MeshComponent)
	{
		MeshComponent->SetCollisionProfileName(*InProfile);
	}
}

void AEpicodePropActor::CaptureDefaultMaterials()
{
	if (!MeshComponent)
	{
		return;
	}
	const int32 NumMaterials = MeshComponent->GetNumMaterials();
	DefaultMaterials.Reset();
	DefaultMaterials.Reserve(NumMaterials);
	for (int32 Index = 0; Index < NumMaterials; ++Index)
	{
		DefaultMaterials.Add(MeshComponent->GetMaterial(Index));
	}
	OverrideMaterials.Reset();
	OverrideMaterials.SetNum(NumMaterials);
}

namespace
{
	FLinearColor ReadColor(const TSharedPtr<FJsonObject>& Data, const TCHAR* Field, const FLinearColor& DefaultValue)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Data.IsValid() && Data->TryGetArrayField(Field, Values) && Values && Values->Num() >= 3)
		{
			return FLinearColor((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber(), Values->Num() > 3 ? (*Values)[3]->AsNumber() : 1.0);
		}
		return DefaultValue;
	}

	void ApplyLightRecord(AActor* Actor, const FEpicodeEntityRecord& Record)
	{
		ULightComponent* Light = Actor ? Actor->FindComponentByClass<ULightComponent>() : nullptr;
		if (!Light || !Record.Data.IsValid())
		{
			return;
		}
		double Number = 0.0;
		if (Record.Data->TryGetNumberField(TEXT("intensity"), Number))
		{
			Light->SetIntensity(static_cast<float>(Number));
		}
		Light->SetLightColor(ReadColor(Record.Data, TEXT("color"), FLinearColor::White));
		bool bUseTemperature = false;
		Record.Data->TryGetBoolField(TEXT("useTemperature"), bUseTemperature);
		Light->SetUseTemperature(bUseTemperature);
		if (Record.Data->TryGetNumberField(TEXT("temperature"), Number))
		{
			Light->SetTemperature(static_cast<float>(Number));
		}
		bool bCastShadows = true;
		Record.Data->TryGetBoolField(TEXT("castShadows"), bCastShadows);
		Light->SetCastShadows(bCastShadows);
		if (ULocalLightComponent* LocalLight = Cast<ULocalLightComponent>(Light))
		{
			if (Record.Data->TryGetNumberField(TEXT("attenuationRadius"), Number))
			{
				LocalLight->SetAttenuationRadius(static_cast<float>(Number));
			}
		}
		if (USpotLightComponent* Spot = Cast<USpotLightComponent>(Light))
		{
			if (Record.Data->TryGetNumberField(TEXT("innerConeAngle"), Number))
			{
				Spot->SetInnerConeAngle(static_cast<float>(Number));
			}
			if (Record.Data->TryGetNumberField(TEXT("outerConeAngle"), Number))
			{
				Spot->SetOuterConeAngle(static_cast<float>(Number));
			}
		}
		if (URectLightComponent* Rect = Cast<URectLightComponent>(Light))
		{
			if (Record.Data->TryGetNumberField(TEXT("sourceWidth"), Number))
			{
				Rect->SetSourceWidth(static_cast<float>(Number));
			}
			if (Record.Data->TryGetNumberField(TEXT("sourceHeight"), Number))
			{
				Rect->SetSourceHeight(static_cast<float>(Number));
			}
		}
	}

	void ApplyCameraRecord(AActor* Actor, const FEpicodeEntityRecord& Record)
	{
		ACameraActor* Camera = Cast<ACameraActor>(Actor);
		if (!Camera || !Camera->GetCameraComponent() || !Record.Data.IsValid())
		{
			return;
		}
		double Number = 0.0;
		if (Record.Data->TryGetNumberField(TEXT("fieldOfView"), Number))
		{
			Camera->GetCameraComponent()->SetFieldOfView(static_cast<float>(Number));
		}
		else if (Record.Data->TryGetNumberField(TEXT("focalLength"), Number) && Number > 0.0)
		{
			Camera->GetCameraComponent()->SetFieldOfView(static_cast<float>(FEpicodeCamera::FocalLengthToFieldOfView(Number)));
		}
	}
}

namespace EpicodeActorFactory
{
	AActor* SpawnForRecord(UWorld* InWorld, const FEpicodeEntityRecord& InRecord)
	{
		if (!InWorld)
		{
			return nullptr;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = nullptr;
		const FTransform Transform = InRecord.Transform.ToFTransform();
		switch (InRecord.Type)
		{
		case EEpicodeEntityType::Character:
			Actor = InWorld->SpawnActor<AEpicodeCharacterActor>(AEpicodeCharacterActor::StaticClass(), Transform, Params);
			break;
		case EEpicodeEntityType::Prop:
			Actor = InWorld->SpawnActor<AEpicodePropActor>(AEpicodePropActor::StaticClass(), Transform, Params);
			break;
		case EEpicodeEntityType::Light:
		{
			FString LightType;
			InRecord.Data->TryGetStringField(TEXT("lightType"), LightType);
			if (LightType.Equals(TEXT("point"), ESearchCase::IgnoreCase))
			{
				Actor = InWorld->SpawnActor<APointLight>(APointLight::StaticClass(), Transform, Params);
			}
			else if (LightType.Equals(TEXT("spot"), ESearchCase::IgnoreCase))
			{
				Actor = InWorld->SpawnActor<ASpotLight>(ASpotLight::StaticClass(), Transform, Params);
			}
			else if (LightType.Equals(TEXT("rect"), ESearchCase::IgnoreCase))
			{
				Actor = InWorld->SpawnActor<ARectLight>(ARectLight::StaticClass(), Transform, Params);
			}
			else
			{
				Actor = InWorld->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), Transform, Params);
			}
			break;
		}
		case EEpicodeEntityType::Camera:
			Actor = InWorld->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Transform, Params);
			break;
		case EEpicodeEntityType::Group:
		default:
			Actor = InWorld->SpawnActor<AEpicodeGroupActor>(AEpicodeGroupActor::StaticClass(), Transform, Params);
			break;
		}
		if (Actor)
		{
			if (AEpicodeEntityActor* CustomActor = Cast<AEpicodeEntityActor>(Actor))
			{
				CustomActor->InitializeEntity(InRecord);
			}
			else
			{
				SetEntityId(Actor, InRecord.Id, InRecord.Type);
				ApplyRecord(Actor, InRecord);
			}
		}
		return Actor;
	}

	void ApplyRecord(AActor* InActor, const FEpicodeEntityRecord& InRecord)
	{
		if (!InActor)
		{
			return;
		}
		InActor->SetActorTransform(InRecord.Transform.ToFTransform());
		InActor->SetActorHiddenInGame(!InRecord.bVisible);
		SetEntityId(InActor, InRecord.Id, InRecord.Type);
		if (AEpicodeEntityActor* CustomActor = Cast<AEpicodeEntityActor>(InActor))
		{
			CustomActor->ApplyRecord(InRecord);
		}
		else if (InRecord.Type == EEpicodeEntityType::Light)
		{
			ApplyLightRecord(InActor, InRecord);
		}
		else if (InRecord.Type == EEpicodeEntityType::Camera)
		{
			ApplyCameraRecord(InActor, InRecord);
		}
	}

	FGuid GetEntityId(const AActor* InActor)
	{
		if (!InActor)
		{
			return FGuid();
		}
		if (const UEpicodeEntityLinkComponent* Link = InActor->FindComponentByClass<UEpicodeEntityLinkComponent>())
		{
			return Link->EntityId;
		}
		return FGuid();
	}

	void SetEntityId(AActor* InActor, const FGuid& InId, EEpicodeEntityType InType)
	{
		if (!InActor)
		{
			return;
		}
		UEpicodeEntityLinkComponent* Link = InActor->FindComponentByClass<UEpicodeEntityLinkComponent>();
		if (!Link)
		{
			Link = NewObject<UEpicodeEntityLinkComponent>(InActor, TEXT("EpicodeEntityLink"));
			Link->RegisterComponent();
		}
		Link->EntityId = InId;
		Link->EntityType = InType;
	}
}
