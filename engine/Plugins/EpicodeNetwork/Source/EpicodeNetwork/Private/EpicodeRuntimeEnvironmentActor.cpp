#include "EpicodeRuntimeEnvironmentActor.h"

#include "Dom/JsonObject.h"
#include "Net/UnrealNetwork.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

AEpicodeRuntimeEnvironmentActor::AEpicodeRuntimeEnvironmentActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(false);
}

void AEpicodeRuntimeEnvironmentActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AEpicodeRuntimeEnvironmentActor, EnvironmentJson);
}

void AEpicodeRuntimeEnvironmentActor::BeginPlay()
{
	Super::BeginPlay();
	if (!EnvironmentJson.IsEmpty())
	{
		FString Error;
		ApplyEnvironment(Error);
	}
}

void AEpicodeRuntimeEnvironmentActor::SetEnvironmentJson(const FString& InJson)
{
	EnvironmentJson = InJson;
	if (HasAuthority())
	{
		FString Error;
		ApplyEnvironment(Error);
	}
}

bool AEpicodeRuntimeEnvironmentActor::ApplyEnvironment(FString& OutError)
{
	if (EnvironmentJson.IsEmpty())
	{
		OutError = TEXT("environment document is empty");
		return false;
	}

	TSharedPtr<FJsonObject> Document;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(EnvironmentJson);
	if (!FJsonSerializer::Deserialize(Reader, Document) || !Document.IsValid())
	{
		OutError = TEXT("environment document is not valid JSON");
		return false;
	}

	if (!bEnvironmentInitialized)
	{
		EnvironmentController.Initialize(GetWorld());
		EnvironmentController.EnsureActors();
		bEnvironmentInitialized = true;
	}

	if (!EnvironmentController.Apply(Document, OutError))
	{
		return false;
	}

	return true;
}

void AEpicodeRuntimeEnvironmentActor::OnRep_Environment()
{
	FString Error;
	if (!ApplyEnvironment(Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("Epicode environment replication failed: %s"), *Error);
	}
}
