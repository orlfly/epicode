#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "EpicodeTypes.h"
#include "EpicodeEntityActors.generated.h"

class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

UCLASS(ClassGroup = (Epicode), meta = (BlueprintSpawnableComponent))
class EPICODECORE_API UEpicodeEntityLinkComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, Category = "Epicode")
	FGuid EntityId;

	UPROPERTY(VisibleAnywhere, Category = "Epicode")
	EEpicodeEntityType EntityType = EEpicodeEntityType::Group;
};

UCLASS(Abstract)
class EPICODECORE_API AEpicodeEntityActor : public AActor
{
	GENERATED_BODY()

public:
	AEpicodeEntityActor();

	UPROPERTY(VisibleAnywhere, Category = "Epicode")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Epicode")
	TObjectPtr<UEpicodeEntityLinkComponent> EntityLink;

	virtual void InitializeEntity(const FEpicodeEntityRecord& InRecord);
	virtual void ApplyRecord(const FEpicodeEntityRecord& InRecord);
	FGuid GetEntityId() const;
};

UCLASS()
class EPICODECORE_API AEpicodeGroupActor : public AEpicodeEntityActor
{
	GENERATED_BODY()
};

UCLASS()
class EPICODECORE_API AEpicodeCharacterActor : public AEpicodeEntityActor
{
	GENERATED_BODY()

public:
	AEpicodeCharacterActor();

	UPROPERTY(VisibleAnywhere, Category = "Epicode")
	TObjectPtr<USkeletalMeshComponent> MeshComponent;

	virtual void ApplyRecord(const FEpicodeEntityRecord& InRecord) override;

	bool SetSkeletalMeshPath(const FString& InPath, FString& OutError);
	bool PlayAnimationPath(const FString& InPath, bool bLoop, float InRate, FString& OutError);
	void PauseAnimation(bool bPause);
	bool SetAnimationTime(float InSeconds, FString& OutError);
	void SetAnimationRate(float InRate);
	bool SetMaterialOverride(int32 InSlot, const FString& InMaterialPath, FString& OutError);
	void ClearMaterialOverride(int32 InSlot);
	void CaptureDefaultMaterials();

private:
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> DefaultMaterials;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> OverrideMaterials;
};

UCLASS()
class EPICODECORE_API AEpicodePropActor : public AEpicodeEntityActor
{
	GENERATED_BODY()

public:
	AEpicodePropActor();

	UPROPERTY(VisibleAnywhere, Category = "Epicode")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	virtual void ApplyRecord(const FEpicodeEntityRecord& InRecord) override;

	bool SetStaticMeshPath(const FString& InPath, FString& OutError);
	bool SetMaterialOverride(int32 InSlot, const FString& InMaterialPath, FString& OutError);
	void ClearMaterialOverride(int32 InSlot);
	void SetSimulatePhysics(bool bEnabled);
	void SetCollisionProfile(const FString& InProfile);
	void CaptureDefaultMaterials();

private:
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> DefaultMaterials;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> OverrideMaterials;
};

namespace EpicodeActorFactory
{
	EPICODECORE_API AActor* SpawnForRecord(UWorld* InWorld, const FEpicodeEntityRecord& InRecord);
	EPICODECORE_API void ApplyRecord(AActor* InActor, const FEpicodeEntityRecord& InRecord);
	EPICODECORE_API FGuid GetEntityId(const AActor* InActor);
	EPICODECORE_API void SetEntityId(AActor* InActor, const FGuid& InId, EEpicodeEntityType InType);
}
