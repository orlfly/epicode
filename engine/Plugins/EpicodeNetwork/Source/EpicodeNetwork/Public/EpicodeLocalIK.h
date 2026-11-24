#pragma once

#include "CoreMinimal.h"
#include "EpicodeLocalIK.generated.h"

class UIKRigDefinition;
class USkeletalMeshComponent;

/** Local-only IK configuration. None of these values are replicated. */
USTRUCT(BlueprintType)
struct EPICODENETWORK_API FEpicodeLocalIKSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Config, Category = "Local IK")
	bool bEnabled = false;

	/** Optional IK Rig asset. When empty, local IK logs a single degradation warning. */
	UPROPERTY(EditAnywhere, Config, Category = "Local IK")
	TSoftObjectPtr<UIKRigDefinition> IKRigAsset;

	UPROPERTY(EditAnywhere, Config, Category = "Local IK")
	FName HandGoalName = TEXT("HandRight");

	UPROPERTY(EditAnywhere, Config, Category = "Local IK")
	FName FootLeftGoalName = TEXT("FootLeft");

	UPROPERTY(EditAnywhere, Config, Category = "Local IK")
	FName FootRightGoalName = TEXT("FootRight");

	UPROPERTY(EditAnywhere, Config, Category = "Local IK")
	FName LookAtGoalName = TEXT("LookAt");

	UPROPERTY(EditAnywhere, Config, Category = "Local IK")
	FName HandBoneName = TEXT("hand_r");

	UPROPERTY(EditAnywhere, Config, Category = "Local IK")
	FName FootLeftBoneName = TEXT("foot_l");

	UPROPERTY(EditAnywhere, Config, Category = "Local IK")
	FName FootRightBoneName = TEXT("foot_r");

	UPROPERTY(EditAnywhere, Config, Category = "Local IK")
	FName LookAtBoneName = TEXT("head");

	UPROPERTY(EditAnywhere, Config, Category = "Local IK")
	FVector HandTargetOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Config, Category = "Local IK")
	FVector FootTargetOffset = FVector(0.0, 0.0, 2.0);

	UPROPERTY(EditAnywhere, Config, Category = "Local IK")
	float LookAtDistance = 500.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Local IK")
	bool bDrawDebug = false;
};

/** Pure local-IK rules shared by the component and automation tests. */
struct EPICODENETWORK_API FEpicodeLocalIKRules
{
	static FVector ClampWeights(const FVector& InWeights);
	static bool AreWeightsZero(const FVector& InWeights);
	/** Server, dedicated server and remote proxies never solve local IK. */
	static bool ShouldApplyLocalIK(bool bEnabled, bool bHasAuthority, bool bDedicatedServer,
		bool bLocallyControlled, const FVector& InWeights);
	static FTransform BlendBoneTransform(const FTransform& InBase, const FTransform& InSolved, double InWeight);
	static FVector MakeLookAtTarget(const FVector& InHeadLocation, const FVector& InForward, double InDistance);
};

/** Applies hand, foot and look-at IK to the local mesh only. */
UCLASS(ClassGroup = (Epicode), meta = (BlueprintSpawnableComponent))
class EPICODENETWORK_API UEpicodeLocalIKComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEpicodeLocalIKComponent();
	virtual ~UEpicodeLocalIKComponent() override;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void SetIKWeights(double InHandWeight, double InFootWeight, double InLookAtWeight);
	void SetIKWeights(const FVector& InWeights);
	const FVector& GetIKWeights() const { return IKWeights; }
	bool HasActiveLocalIK() const;

private:
	bool EnsureProcessor();
	void SolveAndApply();
	void WarnUnavailableOnce(const FString& InReason);
	USkeletalMeshComponent* GetTargetMesh() const;

	FVector IKWeights = FVector::ZeroVector;
	TSharedPtr<struct FEpicodeLocalIKRuntime> Runtime;
	bool bWarnedUnavailable = false;
};
