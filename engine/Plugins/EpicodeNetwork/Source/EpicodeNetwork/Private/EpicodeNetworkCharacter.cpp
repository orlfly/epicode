#include "EpicodeNetworkCharacter.h"

#include "EpicodeLocalIK.h"
#include "EpicodeNetworkModule.h"
#include "EpicodeNetworkSubsystem.h"
#include "Animation/AnimInstance.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

AEpicodeNetworkCharacter::AEpicodeNetworkCharacter()
{
	bReplicates = true;
	SetReplicateMovement(true);
	NetUpdateFrequency = 30.0f;
	MinNetUpdateFrequency = 10.0f;
	NetPriority = 2.0f;
	NetCullDistanceSquared = 100000000.0f;
	bAlwaysRelevant = false;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->SetIsReplicated(true);
	Movement->NetworkSmoothingMode = ENetworkSmoothingMode::Exponential;
	Movement->bAllowPhysicsRotationDuringAnimRootMotion = false;
	Movement->MaxWalkSpeed = 400.0f;
	Movement->MaxFlySpeed = 400.0f;
	Movement->MaxCustomMovementSpeed = 400.0f;
	Movement->MaxAcceleration = 2048.0f;
	Movement->BrakingDecelerationWalking = 2048.0f;
	Movement->bOrientRotationToMovement = true;
	Movement->bUseControllerDesiredRotation = false;
	Movement->RotationRate = FRotator(0.0, 540.0, 0.0);
	Movement->JumpZVelocity = 420.0f;
	Movement->AirControl = 0.2f;
	Movement->NetworkSimulatedSmoothLocationTime = 0.10f;
	Movement->NetworkSimulatedSmoothRotationTime = 0.05f;
	Movement->bIgnoreBaseRotation = false;
	PrimaryActorTick.bCanEverTick = true;

	LocalIKComponent = CreateDefaultSubobject<UEpicodeLocalIKComponent>(TEXT("LocalIK"));
	LocalIKComponent->SetIsReplicated(false);
}

void AEpicodeNetworkCharacter::BeginPlay()
{
	Super::BeginPlay();
	if (USkeletalMeshComponent* SkeletalMesh = GetMesh())
	{
		if (UAnimInstance* AnimInstance = SkeletalMesh->GetAnimInstance())
		{
			AnimInstance->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
		}
	}
	if (HasAuthority())
	{
		SetReplicateMovement(true);
	}
	else
	{
		const UCharacterMovementComponent* Movement = GetCharacterMovement();
		UE_LOG(LogEpicodeNetwork, Log,
			TEXT("Remote interpolation character='%s' smoothing=Exponential locationTime=%.2f rotationTime=%.2f rootMotion=Ignore"),
			*CharacterId,
			Movement ? Movement->NetworkSimulatedSmoothLocationTime : 0.0f,
			Movement ? Movement->NetworkSimulatedSmoothRotationTime : 0.0f);
	}
}

void AEpicodeNetworkCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() && !PerformanceState.ActionTag.IsEmpty())
	{
		ActionRemainingSeconds -= DeltaSeconds;
		if (ActionRemainingSeconds <= 0.0)
		{
			if (PerformanceState.ActionQueue.Num() > 0)
			{
				PerformanceState.ActionTag = PerformanceState.ActionQueue[0];
				PerformanceState.ActionQueue.RemoveAt(0);
				ActionRemainingSeconds = 1.0;
			}
			else
			{
				PerformanceState.ActionTag.Reset();
				ActionRemainingSeconds = 0.0;
			}
		}
	}
	if (!HasAuthority() || PlayerId.IsEmpty())
	{
		return;
	}
	TransformRecordAccumulator += DeltaSeconds;
	if (TransformRecordAccumulator < 1.0f)
	{
		return;
	}
	TransformRecordAccumulator = 0.0f;
	if (UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get())
	{
		FString Error;
		Subsystem->RecordPlayerTransform(PlayerId, GetActorLocation(), GetActorRotation(), Error);
	}
}

void AEpicodeNetworkCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AEpicodeNetworkCharacter, PlayerId);
	DOREPLIFETIME(AEpicodeNetworkCharacter, CharacterId);
	DOREPLIFETIME(AEpicodeNetworkCharacter, AppearanceMeshPath);
	DOREPLIFETIME(AEpicodeNetworkCharacter, PerformanceState);
}

void AEpicodeNetworkCharacter::SetNetworkIdentity(const FString& InPlayerId, const FString& InCharacterId)
{
	if (!HasAuthority())
	{
		return;
	}
	PlayerId = InPlayerId;
	CharacterId = InCharacterId;
}

bool AEpicodeNetworkCharacter::ApplyAppearance(const FString& InMeshPath)
{
	if (InMeshPath.IsEmpty())
	{
		return false;
	}
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *InMeshPath);
	if (!Mesh)
	{
		UE_LOG(LogEpicodeNetwork, Warning, TEXT("Unable to load character mesh '%s'"), *InMeshPath);
		return false;
	}
	GetMesh()->SetSkeletalMesh(Mesh);
	return true;
}

bool AEpicodeNetworkCharacter::SetAppearance(const FString& InMeshPath)
{
	if (!HasAuthority())
	{
		return false;
	}
	const bool bApplied = ApplyAppearance(InMeshPath);
	if (bApplied)
	{
		AppearanceMeshPath = InMeshPath;
	}
	return bApplied;
}

void AEpicodeNetworkCharacter::OnRep_Appearance()
{
	ApplyAppearance(AppearanceMeshPath);
}

void AEpicodeNetworkCharacter::OnRep_Performance()
{
	UE_LOG(LogEpicodeNetwork, Verbose, TEXT("Performance state action='%s' emotion='%s' intensity=%.2f speaking=%d viseme=%s"),
		*PerformanceState.ActionTag, *PerformanceState.Emotion, PerformanceState.ExpressionIntensity,
		PerformanceState.bSpeaking ? 1 : 0, *LexToString(PerformanceState.VisemeSource));
}

bool AEpicodeNetworkCharacter::RequestAction(const FString& InActionTag, bool bInterrupt, double InDurationSeconds, FString& OutError)
{
	if (!HasAuthority())
	{
		OutError = TEXT("FORBIDDEN: actions must be requested on the authoritative server.");
		return false;
	}
	const double Duration = FMath::Clamp(InDurationSeconds <= 0.0 ? 1.0 : InDurationSeconds, 0.1, 10.0);
	if (!FEpicodePerformanceRules::ApplyAction(PerformanceState, InActionTag, bInterrupt, InDurationSeconds, ActionRemainingSeconds, OutError))
	{
		return false;
	}
	if (UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get())
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("action"), InActionTag);
		Payload->SetBoolField(TEXT("interrupt"), bInterrupt);
		Payload->SetBoolField(TEXT("queued"), PerformanceState.ActionTag != InActionTag);
		Payload->SetNumberField(TEXT("duration"), Duration);
		Subsystem->PublishPerformanceEvent(PlayerId, CharacterId, TEXT("performance.action"), Payload);
	}
	return true;
}

bool AEpicodeNetworkCharacter::SetExpression(const FString& InEmotion, double InIntensity, FString& OutError)
{
	if (!HasAuthority())
	{
		OutError = TEXT("FORBIDDEN: expressions must be set on the authoritative server.");
		return false;
	}
	if (!FEpicodePerformanceState::IsValidEmotion(InEmotion) || !FEpicodePerformanceState::IsValidExpressionIntensity(InIntensity))
	{
		OutError = TEXT("INVALID_PARAMS: emotion must be non-empty and intensity must be between 0 and 1.");
		return false;
	}
	PerformanceState.Emotion = InEmotion;
	PerformanceState.ExpressionIntensity = static_cast<float>(InIntensity);
	if (UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get())
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("emotion"), InEmotion);
		Payload->SetNumberField(TEXT("intensity"), InIntensity);
		Subsystem->PublishPerformanceEvent(PlayerId, CharacterId, TEXT("performance.expression"), Payload);
	}
	return true;
}

bool AEpicodeNetworkCharacter::SetVoice(bool bInSpeaking, EEpicodeVisemeSource InSource, FString& OutWarning, FString& OutError)
{
	if (!HasAuthority())
	{
		OutError = TEXT("FORBIDDEN: voice state must be set on the authoritative server.");
		return false;
	}
	EEpicodeVisemeSource Resolved = InSource;
	if (!FEpicodePerformanceRules::ResolveVisemeSource(InSource, false, false, Resolved, OutWarning))
	{
		OutError = TEXT("INVALID_PARAMS: unsupported viseme source.");
		return false;
	}
	PerformanceState.bSpeaking = bInSpeaking;
	PerformanceState.VisemeSource = Resolved;
	if (UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get())
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetBoolField(TEXT("speaking"), bInSpeaking);
		Payload->SetStringField(TEXT("requestedSource"), LexToString(InSource));
		Payload->SetStringField(TEXT("resolvedSource"), LexToString(Resolved));
		if (!OutWarning.IsEmpty())
		{
			Payload->SetStringField(TEXT("warning"), OutWarning);
		}
		Subsystem->PublishPerformanceEvent(PlayerId, CharacterId, TEXT("performance.voice"), Payload);
	}
	return true;
}

bool AEpicodeNetworkCharacter::SetMotionMatching(bool bEnabled, bool bMotionDataAvailable, FString& OutError)
{
	if (!HasAuthority())
	{
		OutError = TEXT("FORBIDDEN: Motion Matching is server-authoritative.");
		return false;
	}
	PerformanceState.bMotionMatchingActive = FEpicodePerformanceRules::ResolveMotionMatching(bEnabled, bMotionDataAvailable, OutError);
	return true;
}

void AEpicodeNetworkCharacter::PlayLocalActionPrediction(const FString& InActionTag)
{
	if (HasAuthority() || !FEpicodePerformanceState::IsValidActionTag(InActionTag))
	{
		return;
	}
	UE_LOG(LogEpicodeNetwork, Verbose, TEXT("Local action prediction '%s' for character '%s'"), *InActionTag, *CharacterId);
}

void AEpicodeNetworkCharacter::ApplyLocalMicroExpression(double InIntensity)
{
	if (!FEpicodePerformanceState::IsValidExpressionIntensity(InIntensity))
	{
		return;
	}
	LocalMicroExpressionIntensity = static_cast<float>(InIntensity);
}

void AEpicodeNetworkCharacter::ApplyLocalIKCorrections(double InHandWeight, double InFootWeight, double InLookAtWeight)
{
	LocalIKWeights = FVector(
		FEpicodePerformanceRules::ClampIKWeight(InHandWeight),
		FEpicodePerformanceRules::ClampIKWeight(InFootWeight),
		FEpicodePerformanceRules::ClampIKWeight(InLookAtWeight));
	if (LocalIKComponent)
	{
		LocalIKComponent->SetIKWeights(LocalIKWeights);
	}
}

void AEpicodeNetworkCharacter::ServerRequestAction_Implementation(const FString& InActionTag, bool bInterrupt, float InDurationSeconds)
{
	FString Error;
	RequestAction(InActionTag, bInterrupt, InDurationSeconds, Error);
}

void AEpicodeNetworkCharacter::ServerSetExpression_Implementation(const FString& InEmotion, float InIntensity)
{
	FString Error;
	SetExpression(InEmotion, InIntensity, Error);
}

void AEpicodeNetworkCharacter::ServerSetVoice_Implementation(bool bInSpeaking, EEpicodeVisemeSource InSource)
{
	FString Warning;
	FString Error;
	SetVoice(bInSpeaking, InSource, Warning, Error);
}

bool AEpicodeNetworkCharacter::IsTeleportDistanceAllowed(double InDistance, double InMaxDistance)
{
	return InDistance >= 0.0 && InDistance <= InMaxDistance;
}

bool AEpicodeNetworkCharacter::IsMoveDistanceAllowed(double InDeltaSeconds, double InDistance, double InMaxSpeed, double InTolerance)
{
	if (InDeltaSeconds < 0.0 || InDistance < 0.0 || InMaxSpeed <= 0.0 || InTolerance <= 0.0)
	{
		return false;
	}
	const double Budget = InMaxSpeed * InDeltaSeconds * InTolerance;
	return InDistance <= Budget;
}

FVector AEpicodeNetworkCharacter::ComputeAuthoritativeCorrection(const FVector& InServerLocation, const FVector& InClientLocation, double InMaxCorrectionDistance)
{
	if (InMaxCorrectionDistance < 0.0)
	{
		return InServerLocation;
	}
	return FVector::DistSquared(InServerLocation, InClientLocation) > FMath::Square(InMaxCorrectionDistance)
		? InServerLocation
		: InClientLocation;
}

FVector AEpicodeNetworkCharacter::MakeCapsuleSpawnLocation(const FVector& InSceneLocation, double InCapsuleHalfHeight)
{
	return InSceneLocation + FVector(0.0, 0.0, FMath::Max(0.0, InCapsuleHalfHeight));
}

void AEpicodeNetworkCharacter::ServerRequestTeleport_Implementation(const FVector& TargetLocation, const FRotator& TargetRotation)
{
	if (!HasAuthority())
	{
		return;
	}
	const double Distance = FVector::Dist(GetActorLocation(), TargetLocation);
	if (!IsTeleportDistanceAllowed(Distance, MaxTeleportDistance))
	{
		UE_LOG(LogEpicodeNetwork, Warning, TEXT("Rejected teleport for character '%s': distance %.1f exceeds %.1f"),
			*CharacterId, Distance, MaxTeleportDistance);
		return;
	}
	SetActorLocationAndRotation(TargetLocation, TargetRotation, false, nullptr, ETeleportType::TeleportPhysics);
}

void AEpicodeNetworkCharacter::OnRep_PlayerId()
{
	UE_LOG(LogEpicodeNetwork, Verbose, TEXT("Character %s now belongs to player %s"), *GetName(), *PlayerId);
}

void AEpicodeNetworkCharacter::OnRep_CharacterId()
{
	UE_LOG(LogEpicodeNetwork, Verbose, TEXT("Character %s received runtime id %s"), *GetName(), *CharacterId);
}
