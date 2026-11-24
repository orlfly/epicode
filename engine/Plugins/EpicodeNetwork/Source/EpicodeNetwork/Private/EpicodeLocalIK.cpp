#include "EpicodeLocalIK.h"

#include "EpicodeNetworkModule.h"
#include "EpicodeNetworkSettings.h"
#include "EpicodePerformanceTypes.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "Rig/IKRigDataTypes.h"
#include "Rig/IKRigDefinition.h"
#include "Rig/IKRigProcessor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"

struct FEpicodeLocalIKRuntime
{
	FIKRigProcessor Processor;
	int32 HandBoneIndex = INDEX_NONE;
	int32 FootLeftBoneIndex = INDEX_NONE;
	int32 FootRightBoneIndex = INDEX_NONE;
	int32 LookAtBoneIndex = INDEX_NONE;
	bool bInitialized = false;
	bool bTriedInitialize = false;
	bool bLoggedApplied = false;
};

namespace
{
	const FEpicodeLocalIKSettings& GetSettings()
	{
		return UEpicodeNetworkSettings::Get()->LocalIK;
	}
}

FVector FEpicodeLocalIKRules::ClampWeights(const FVector& InWeights)
{
	return FVector(
		FEpicodePerformanceRules::ClampIKWeight(InWeights.X),
		FEpicodePerformanceRules::ClampIKWeight(InWeights.Y),
		FEpicodePerformanceRules::ClampIKWeight(InWeights.Z));
}

bool FEpicodeLocalIKRules::AreWeightsZero(const FVector& InWeights)
{
	return FMath::IsNearlyZero(InWeights.X) && FMath::IsNearlyZero(InWeights.Y) && FMath::IsNearlyZero(InWeights.Z);
}

bool FEpicodeLocalIKRules::ShouldApplyLocalIK(bool bEnabled, bool bHasAuthority, bool bDedicatedServer,
	bool bLocallyControlled, const FVector& InWeights)
{
	return bEnabled && !bHasAuthority && !bDedicatedServer && bLocallyControlled && !AreWeightsZero(InWeights);
}

FTransform FEpicodeLocalIKRules::BlendBoneTransform(const FTransform& InBase, const FTransform& InSolved, double InWeight)
{
	const double Weight = FMath::Clamp(InWeight, 0.0, 1.0);
	if (Weight <= 0.0)
	{
		return InBase;
	}
	if (Weight >= 1.0)
	{
		return InSolved;
	}
	FTransform Blended = InBase;
	Blended.BlendWith(InSolved, static_cast<float>(Weight));
	return Blended;
}

FVector FEpicodeLocalIKRules::MakeLookAtTarget(const FVector& InHeadLocation, const FVector& InForward, double InDistance)
{
	const FVector Forward = InForward.GetSafeNormal();
	if (Forward.IsNearlyZero() || !FMath::IsFinite(InDistance))
	{
		return InHeadLocation;
	}
	return InHeadLocation + Forward * FMath::Max(0.0, InDistance);
}

UEpicodeLocalIKComponent::UEpicodeLocalIKComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	bWantsInitializeComponent = false;
}

UEpicodeLocalIKComponent::~UEpicodeLocalIKComponent() = default;

void UEpicodeLocalIKComponent::BeginPlay()
{
	Super::BeginPlay();
	bWarnedUnavailable = false;
}

void UEpicodeLocalIKComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Runtime.Reset();
	Super::EndPlay(EndPlayReason);
}

void UEpicodeLocalIKComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const FEpicodeLocalIKSettings& Settings = GetSettings();
	if (!Settings.bEnabled || FEpicodeLocalIKRules::AreWeightsZero(IKWeights))
	{
		return;
	}

	const AActor* Owner = GetOwner();
	const APawn* Pawn = Cast<APawn>(Owner);
	const AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	const UWorld* World = GetWorld();
	const bool bHasAuthority = Owner && Owner->HasAuthority();
	const bool bDedicatedServer = World && World->GetNetMode() == NM_DedicatedServer;
	const bool bLocallyControlled = Controller && Controller->IsLocalController();
	if (!FEpicodeLocalIKRules::ShouldApplyLocalIK(
		Settings.bEnabled, bHasAuthority, bDedicatedServer, bLocallyControlled, IKWeights))
	{
		return;
	}

	if (!EnsureProcessor())
	{
		return;
	}
	SolveAndApply();
}

void UEpicodeLocalIKComponent::SetIKWeights(double InHandWeight, double InFootWeight, double InLookAtWeight)
{
	IKWeights = FEpicodeLocalIKRules::ClampWeights(
		FVector(InHandWeight, InFootWeight, InLookAtWeight));
}

void UEpicodeLocalIKComponent::SetIKWeights(const FVector& InWeights)
{
	IKWeights = FEpicodeLocalIKRules::ClampWeights(InWeights);
}

bool UEpicodeLocalIKComponent::HasActiveLocalIK() const
{
	return GetSettings().bEnabled && !FEpicodeLocalIKRules::AreWeightsZero(IKWeights);
}

USkeletalMeshComponent* UEpicodeLocalIKComponent::GetTargetMesh() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	return Character ? Character->GetMesh() : nullptr;
}

bool UEpicodeLocalIKComponent::EnsureProcessor()
{
	const FEpicodeLocalIKSettings& Settings = GetSettings();
	if (!Settings.bEnabled)
	{
		return false;
	}
	if (!Runtime)
	{
		Runtime = MakeShared<FEpicodeLocalIKRuntime>();
	}
	if (Runtime->bInitialized)
	{
		return true;
	}
	if (Runtime->bTriedInitialize)
	{
		return false;
	}
	Runtime->bTriedInitialize = true;

	if (Settings.IKRigAsset.IsNull())
	{
		WarnUnavailableOnce(TEXT("no IK Rig asset configured"));
		return false;
	}
	UIKRigDefinition* RigAsset = Settings.IKRigAsset.LoadSynchronous();
	if (!RigAsset)
	{
		WarnUnavailableOnce(
			FString::Printf(TEXT("unable to load IK Rig '%s'"), *Settings.IKRigAsset.ToString()));
		return false;
	}
	USkeletalMeshComponent* Mesh = GetTargetMesh();
	if (!Mesh || !Mesh->GetSkeletalMeshAsset())
	{
		WarnUnavailableOnce(TEXT("no skeletal mesh or animation instance"));
		return false;
	}

	Runtime->Processor.Initialize(RigAsset, Mesh->GetSkeletalMeshAsset(), FIKRigGoalContainer());
	if (!Runtime->Processor.IsInitialized())
	{
		WarnUnavailableOnce(TEXT("IK Rig processor initialization failed"));
		return false;
	}

	Runtime->HandBoneIndex = Mesh->GetBoneIndex(Settings.HandBoneName);
	Runtime->FootLeftBoneIndex = Mesh->GetBoneIndex(Settings.FootLeftBoneName);
	Runtime->FootRightBoneIndex = Mesh->GetBoneIndex(Settings.FootRightBoneName);
	Runtime->LookAtBoneIndex = Mesh->GetBoneIndex(Settings.LookAtBoneName);
	if (Runtime->HandBoneIndex == INDEX_NONE
		&& Runtime->FootLeftBoneIndex == INDEX_NONE
		&& Runtime->FootRightBoneIndex == INDEX_NONE
		&& Runtime->LookAtBoneIndex == INDEX_NONE)
	{
		WarnUnavailableOnce(TEXT("none of the configured IK bones exist on the skeletal mesh"));
		return false;
	}

	Runtime->bInitialized = true;
	return true;
}

void UEpicodeLocalIKComponent::SolveAndApply()
{
	USkeletalMeshComponent* Mesh = GetTargetMesh();
	if (!Mesh || !Runtime || !Runtime->bInitialized)
	{
		return;
	}
	const FEpicodeLocalIKSettings& Settings = GetSettings();
	const int32 BoneCount = Mesh->GetNumBones();
	const TArray<FTransform>& ComponentPose = Mesh->GetEditableComponentSpaceTransforms();
	if (BoneCount <= 0 || ComponentPose.Num() < BoneCount)
	{
		return;
	}

	TArray<FTransform> InputPose;
	InputPose.SetNum(BoneCount);
	for (int32 Index = 0; Index < BoneCount; ++Index)
	{
		InputPose[Index] = ComponentPose[Index];
	}
	Runtime->Processor.SetInputPoseGlobal(InputPose);

	int32 GoalCount = 0;
	auto AddGoal = [this, &ComponentPose, &GoalCount](FName GoalName, FName BoneName, int32 BoneIndex,
		const FVector& InOffset, float InWeight)
	{
		if (InWeight <= 0.0f || GoalName.IsNone() || BoneIndex == INDEX_NONE || !ComponentPose.IsValidIndex(BoneIndex))
		{
			return;
		}
		if (!Runtime->Processor.GetGoalBone(GoalName))
		{
			return;
		}
		const FTransform& BoneTransform = ComponentPose[BoneIndex];
		const FVector Target = BoneTransform.GetTranslation() + InOffset;
		Runtime->Processor.SetIKGoal(FIKRigGoal(
			GoalName,
			BoneName,
			Target,
			BoneTransform.GetRotation(),
			InWeight,
			InWeight,
			EIKRigGoalSpace::Component,
			EIKRigGoalSpace::Component,
			true));
		++GoalCount;
	};

	AddGoal(Settings.HandGoalName, Settings.HandBoneName, Runtime->HandBoneIndex,
		Settings.HandTargetOffset, static_cast<float>(IKWeights.X));
	AddGoal(Settings.FootLeftGoalName, Settings.FootLeftBoneName, Runtime->FootLeftBoneIndex,
		Settings.FootTargetOffset, static_cast<float>(IKWeights.Y));
	AddGoal(Settings.FootRightGoalName, Settings.FootRightBoneName, Runtime->FootRightBoneIndex,
		Settings.FootTargetOffset, static_cast<float>(IKWeights.Y));

	if (Runtime->LookAtBoneIndex != INDEX_NONE && ComponentPose.IsValidIndex(Runtime->LookAtBoneIndex)
		&& IKWeights.Z > 0.0 && !Settings.LookAtGoalName.IsNone()
		&& Runtime->Processor.GetGoalBone(Settings.LookAtGoalName))
	{
		const FTransform& Head = ComponentPose[Runtime->LookAtBoneIndex];
		const FVector Target = FEpicodeLocalIKRules::MakeLookAtTarget(
			Head.GetTranslation(), Head.GetRotation().GetForwardVector(), Settings.LookAtDistance);
		Runtime->Processor.SetIKGoal(FIKRigGoal(
			Settings.LookAtGoalName,
			Settings.LookAtBoneName,
			Target,
			Head.GetRotation(),
			static_cast<float>(IKWeights.Z),
			static_cast<float>(IKWeights.Z),
			EIKRigGoalSpace::Component,
			EIKRigGoalSpace::Component,
			true));
		++GoalCount;
	}

	if (GoalCount == 0)
	{
		return;
	}

	Runtime->Processor.Solve(FTransform::Identity);
	TArray<FTransform> OutputPose;
	Runtime->Processor.GetOutputPoseGlobal(OutputPose);
	if (OutputPose.Num() < BoneCount)
	{
		return;
	}

	TArray<FTransform>& EditablePose = Mesh->GetEditableComponentSpaceTransforms();
	auto ApplyBone = [&EditablePose, &OutputPose](int32 BoneIndex, double InWeight)
	{
		if (BoneIndex == INDEX_NONE || InWeight <= 0.0 || !EditablePose.IsValidIndex(BoneIndex) || !OutputPose.IsValidIndex(BoneIndex))
		{
			return;
		}
		const FTransform Blended = FEpicodeLocalIKRules::BlendBoneTransform(
			EditablePose[BoneIndex], OutputPose[BoneIndex], InWeight);
		if (!Blended.ContainsNaN())
		{
			EditablePose[BoneIndex] = Blended;
		}
	};
	ApplyBone(Runtime->HandBoneIndex, IKWeights.X);
	ApplyBone(Runtime->FootLeftBoneIndex, IKWeights.Y);
	ApplyBone(Runtime->FootRightBoneIndex, IKWeights.Y);
	ApplyBone(Runtime->LookAtBoneIndex, IKWeights.Z);
	Mesh->FinalizeBoneTransform();

	if (!Runtime->bLoggedApplied)
	{
		Runtime->bLoggedApplied = true;
		UE_LOG(LogEpicodeNetwork, Log,
			TEXT("Local IK applied character='%s' hand=%.2f foot=%.2f lookAt=%.2f goals=%d"),
			GetOwner() ? *GetOwner()->GetName() : TEXT(""),
			IKWeights.X, IKWeights.Y, IKWeights.Z, GoalCount);
	}
}

void UEpicodeLocalIKComponent::WarnUnavailableOnce(const FString& InReason)
{
	if (bWarnedUnavailable)
	{
		return;
	}
	bWarnedUnavailable = true;
	UE_LOG(LogEpicodeNetwork, Warning,
		TEXT("LOCAL_IK_UNAVAILABLE: character='%s' reason='%s'. Local IK is skipped; server transforms are unchanged."),
		GetOwner() ? *GetOwner()->GetName() : TEXT(""), *InReason);
}
