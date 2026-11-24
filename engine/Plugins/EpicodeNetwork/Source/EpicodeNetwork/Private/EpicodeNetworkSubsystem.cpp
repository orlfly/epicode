#include "EpicodeNetworkSubsystem.h"

#include "EpicodeRuntimeMode.h"
#include "Engine/Engine.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EpicodeAgentController.h"
#include "EpicodeEntityActors.h"
#include "EpicodeNetworkCharacter.h"
#include "EpicodeNetworkModule.h"
#include "EpicodeNetworkSandboxBridge.h"
#include "EpicodeNetworkSettings.h"
#include "EpicodeProductionLibrary.h"
#include "EpicodeRuntimeEnvironmentActor.h"
#include "EpicodeSandboxSettings.h"
#include "EpicodeSceneLibrary.h"
#include "EpicodeSettings.h"
#include "HAL/PlatformTime.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	EEpicodeSandboxParticipantKind MapSandboxParticipantKind(const FEpicodeSessionPlayer& InPlayer)
	{
		if (InPlayer.ParticipantKind == EEpicodeParticipantKind::Viewer || InPlayer.Role == EEpicodeSessionRole::Observer)
		{
			return EEpicodeSandboxParticipantKind::Viewer;
		}
		if (InPlayer.Role == EEpicodeSessionRole::Owner)
		{
			return EEpicodeSandboxParticipantKind::Owner;
		}
		if (InPlayer.Role == EEpicodeSessionRole::Director)
		{
			return EEpicodeSandboxParticipantKind::Director;
		}
		if (InPlayer.ParticipantKind == EEpicodeParticipantKind::Agent)
		{
			return EEpicodeSandboxParticipantKind::Agent;
		}
		return EEpicodeSandboxParticipantKind::Performer;
	}

	AEpicodeNetworkCharacter* FindCharacterById(UWorld* World, const FString& InCharacterId)
	{
		if (!World || InCharacterId.IsEmpty())
		{
			return nullptr;
		}
		for (TActorIterator<AEpicodeNetworkCharacter> It(World); It; ++It)
		{
			if (It->CharacterId == InCharacterId)
			{
				return *It;
			}
		}
		return nullptr;
	}
}

bool UEpicodeNetworkSubsystem::CreateSandboxBridge(const FEpicodeNetworkSceneSummary& InSummary)
{
	SandboxBridge.Reset();
	const UEpicodeSandboxSettings* SandboxSettings = UEpicodeSandboxSettings::Get();
	if (!SandboxSettings->bEnableSandboxDirector)
	{
		return true;
	}

	FEpicodeSceneDocument Scene;
	FString SceneError;
	if (!FEpicodeSceneLibrary::Load(InSummary.DocumentPath, Scene, SceneError))
	{
		UE_LOG(LogEpicodeNetwork, Warning, TEXT("Sandbox bridge skipped: scene load failed: %s"), *SceneError);
		return false;
	}

	FEpicodeProductionDocument Production;
	bool bHasProduction = false;
	for (const FEpicodeProductionLibraryEntry& Entry : FEpicodeProductionLibrary::Scan(SandboxSettings->GetProductionRootPath()))
	{
		FEpicodeProductionDocument Candidate;
		FString LoadError;
		if (FEpicodeProductionLibrary::Load(Entry.DocumentPath, Candidate, LoadError) && Candidate.SceneId == InSummary.SceneId)
		{
			Production = Candidate;
			bHasProduction = true;
			break;
		}
	}

	FEpicodeSandboxSessionConfig Config;
	Config.SessionId = Session.IsValid() ? Session->GetSessionId() : FString();
	Config.TickRate = FMath::Max(1, SandboxSettings->FixedTickRate);
	Config.Seed = static_cast<int32>(SandboxSettings->RandomSeed);

	TSharedPtr<FEpicodeNetworkSandboxBridge> Bridge = MakeShared<FEpicodeNetworkSandboxBridge>();
	FString BridgeError;
	const bool bCreated = bHasProduction
		? Bridge->Create(Production, Scene, Config, BridgeError)
		: Bridge->CreateLegacy(Scene, Config, BridgeError);
	if (!bCreated)
	{
		UE_LOG(LogEpicodeNetwork, Warning, TEXT("Sandbox bridge skipped: %s"), *BridgeError);
		return false;
	}
	SandboxBridge = Bridge;
	if (Session.IsValid())
	{
		SyncSandboxPlayer(Session->GetOwnerPlayerId());
	}
	return true;
}

void UEpicodeNetworkSubsystem::SyncSandboxPlayer(const FString& InPlayerId)
{
	if (!SandboxBridge.IsValid() || !Session.IsValid() || InPlayerId.IsEmpty())
	{
		return;
	}
	const FEpicodeSessionPlayer* Player = Session->FindPlayer(InPlayerId);
	if (!Player)
	{
		return;
	}
	FString BridgeError;
	if (!SandboxBridge->BindPlayer(Player->PlayerId, MapSandboxParticipantKind(*Player), Player->CharacterId, BridgeError))
	{
		UE_LOG(LogEpicodeNetwork, Warning, TEXT("Sandbox participant bind failed for %s: %s"), *InPlayerId, *BridgeError);
	}
}

void UEpicodeNetworkSubsystem::CloseSandboxBridge()
{
	if (!SandboxBridge.IsValid())
	{
		return;
	}
	if (SandboxBridge->IsActive())
	{
		for (const FString& ParticipantId : SandboxBridge->GetWorld().GetSortedParticipantIds())
		{
			FString DisconnectError;
			SandboxBridge->SetPlayerConnected(ParticipantId, false, DisconnectError);
		}
		FString CloseError;
		SandboxBridge->Close(CloseError);
	}
	SandboxBridge.Reset();
}

void UEpicodeNetworkSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogEpicodeNetwork, Log, TEXT("EpicodeNetwork subsystem initialized (port=%d, maxPlayers=%d, onlineSubsystem=%s)"),
		UEpicodeNetworkSettings::Get()->GamePort,
		UEpicodeNetworkSettings::Get()->MaxPlayers,
		*UEpicodeNetworkSettings::Get()->OnlineSubsystem);
	if (IsRunningDedicatedServer())
	{
		FString ViewerTokenError;
		if (!UEpicodeNetworkSettings::Get()->EnsureViewerToken(ViewerTokenError))
		{
			UE_LOG(LogEpicodeNetwork, Warning, TEXT("Viewer token unavailable: %s"), *ViewerTokenError);
		}
		else
		{
			UE_LOG(LogEpicodeNetwork, Log, TEXT("Viewer join token ready at %s"), *UEpicodeNetworkSettings::Get()->GetViewerTokenFilePath());
		}
	}
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float)
	{
		Sync();
		return true;
	}), 0.5f);
}

void UEpicodeNetworkSubsystem::Deinitialize()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	Session.Reset();
	Super::Deinitialize();
}

UEpicodeNetworkSubsystem* UEpicodeNetworkSubsystem::Get()
{
	return GEngine ? GEngine->GetEngineSubsystem<UEpicodeNetworkSubsystem>() : nullptr;
}

void UEpicodeNetworkSubsystem::Sync()
{
	WorldState.Tick(FPlatformTime::Seconds());
	if (!Session.IsValid())
	{
		DestroyRuntimeWorldActors();
		return;
	}
	Session->Tick(FPlatformTime::Seconds());
	if (SandboxBridge.IsValid())
	{
		SandboxBridge->Tick(FApp::GetDeltaTime());
	}
	SyncRuntimeWorldActors();
	for (auto It = AgentControllers.CreateIterator(); It; ++It)
	{
		AEpicodeAgentController* Controller = It.Value().Get();
		const FEpicodeSessionPlayer* Player = Session->FindPlayer(It.Key());
		if (!Controller || !Player || Player->CharacterId.IsEmpty() || Player->LeaseId.IsEmpty())
		{
			if (Controller)
			{
				Controller->StopMovement();
				Controller->UnPossess();
				Controller->Destroy();
			}
			It.RemoveCurrent();
		}
	}
	MaybeExport();
}

void UEpicodeNetworkSubsystem::DestroyRuntimeSceneActors()
{
	for (const TWeakObjectPtr<AActor>& WeakActor : RuntimeSceneActors)
	{
		if (AActor* Actor = WeakActor.Get())
		{
			Actor->Destroy();
		}
	}
	RuntimeSceneActors.Reset();
	if (AEpicodeRuntimeEnvironmentActor* EnvironmentActor = RuntimeEnvironmentActor.Get())
	{
		EnvironmentActor->Destroy();
	}
	RuntimeEnvironmentActor.Reset();
}

bool UEpicodeNetworkSubsystem::HasActiveSession() const
{
	return Session.IsValid() && Session->GetState() != EEpicodeSessionState::Ended;
}

void UEpicodeNetworkSubsystem::AttachToWorld()
{
	if (!HasActiveSession())
	{
		return;
	}
	SpawnRuntimeSceneActors();
	SyncRuntimeWorldActors();
	Session->PublishEvent(TEXT("scene.attached"), Session->GetOwnerPlayerId(), FString());
}

bool UEpicodeNetworkSubsystem::ShouldTravelToLevel(const UWorld* InWorld, const FString& LevelPath)
{
	if (!InWorld || LevelPath.IsEmpty())
	{
		return false;
	}
	FString CurrentMap = InWorld->GetMapName();
	CurrentMap.RemoveFromStart(InWorld->StreamingLevelsPrefix);
	const FString TargetMap = FPackageName::GetShortName(LevelPath);
	if (CurrentMap.Equals(TargetMap, ESearchCase::IgnoreCase))
	{
		return false;
	}
	return FPackageName::DoesPackageExist(LevelPath);
}

void UEpicodeNetworkSubsystem::SpawnRuntimeSceneActors()
{
	DestroyRuntimeSceneActors();
	UWorld* World = FindGameWorld();
	if (!World)
	{
		return;
	}
	auto SpawnEntity = [this, World](const FEpicodeNetworkSceneEntity& Entity)
	{
		FEpicodeEntityRecord Record;
		FGuid::Parse(Entity.EntityId, Record.Id);
		ParseEntityType(Entity.Type, Record.Type);
		Record.Name = Entity.Name;
		Record.bVisible = Entity.bVisible;
		Record.Transform = Entity.Transform;
		Record.Data = Entity.Data;
		AActor* Actor = EpicodeActorFactory::SpawnForRecord(World, Record);
		if (Actor)
		{
			Actor->SetReplicates(true);
			Actor->SetActorHiddenInGame(!Entity.bVisible);
			RuntimeSceneActors.Add(Actor);
#if WITH_EDITOR
			Actor->SetActorLabel(Entity.Name.IsEmpty() ? Entity.EntityId : Entity.Name);
#endif
		}
	};
	for (const FEpicodeNetworkSceneEntity& Prop : SceneSummary.Props)
	{
		SpawnEntity(Prop);
	}
	for (const FEpicodeNetworkSceneEntity& Light : SceneSummary.Lights)
	{
		SpawnEntity(Light);
	}
	for (const FEpicodeNetworkSceneEntity& Camera : SceneSummary.Cameras)
	{
		SpawnEntity(Camera);
	}

	if (SceneSummary.Environment.IsValid())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AEpicodeRuntimeEnvironmentActor* EnvironmentActor = World->SpawnActor<AEpicodeRuntimeEnvironmentActor>(
			AEpicodeRuntimeEnvironmentActor::StaticClass(), FTransform::Identity, Params);
		if (EnvironmentActor)
		{
			FString EnvironmentJson;
			const TSharedRef<TJsonWriter<>>& Writer = TJsonWriterFactory<>::Create(&EnvironmentJson);
			FJsonSerializer::Serialize(SceneSummary.Environment.ToSharedRef(), Writer);
			EnvironmentActor->SetReplicates(true);
			EnvironmentActor->SetEnvironmentJson(EnvironmentJson);
			RuntimeEnvironmentActor = EnvironmentActor;
		}
	}
}

void UEpicodeNetworkSubsystem::DestroyRuntimeWorldActors()
{
	for (const TPair<FString, TWeakObjectPtr<AActor>>& Pair : RuntimeItemActors)
	{
		if (AActor* Actor = Pair.Value.Get())
		{
			Actor->Destroy();
		}
	}
	RuntimeItemActors.Reset();
	for (const TPair<FString, TWeakObjectPtr<AActor>>& Pair : RuntimeBuildActors)
	{
		if (AActor* Actor = Pair.Value.Get())
		{
			Actor->Destroy();
		}
	}
	RuntimeBuildActors.Reset();
}

void UEpicodeNetworkSubsystem::SyncRuntimeWorldActors()
{
	UWorld* World = FindGameWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}

	const FString CubeMesh = TEXT("/Engine/BasicShapes/Cube.Cube");
	TSet<FString> LiveItems;
	for (const FEpicodeItemRecord& Item : WorldState.GetItems())
	{
		LiveItems.Add(Item.ItemId);
		AActor* Actor = RuntimeItemActors.FindRef(Item.ItemId).Get();
		if (!Actor)
		{
			FEpicodeEntityRecord Record;
			FGuid::Parse(Item.ItemId, Record.Id);
			if (!Record.Id.IsValid())
			{
				Record.Id = FGuid::NewGuid();
			}
			Record.Type = EEpicodeEntityType::Prop;
			Record.Name = Item.DisplayName.IsEmpty() ? Item.ItemType : Item.DisplayName;
			Record.Transform.Location = Item.Anchor;
			Record.Data = MakeShared<FJsonObject>();
			Record.Data->SetStringField(TEXT("staticMesh"), CubeMesh);
			Actor = EpicodeActorFactory::SpawnForRecord(World, Record);
			if (Actor)
			{
				Actor->SetReplicates(true);
				RuntimeItemActors.Add(Item.ItemId, Actor);
			}
		}
		if (Actor)
		{
			Actor->SetActorLocation(Item.Anchor);
			Actor->SetActorHiddenInGame(!Item.HolderPlayerId.IsEmpty());
		}
	}
	for (auto It = RuntimeItemActors.CreateIterator(); It; ++It)
	{
		if (!LiveItems.Contains(It.Key()))
		{
			if (AActor* Actor = It.Value().Get())
			{
				Actor->Destroy();
			}
			It.RemoveCurrent();
		}
	}

	TSet<FString> LiveBuilds;
	for (const FEpicodeBuildRecord& Build : WorldState.GetBuilds())
	{
		LiveBuilds.Add(Build.BuildId);
		AActor* Actor = RuntimeBuildActors.FindRef(Build.BuildId).Get();
		if (!Actor)
		{
			FEpicodeEntityRecord Record;
			FGuid::Parse(Build.BuildId, Record.Id);
			if (!Record.Id.IsValid())
			{
				Record.Id = FGuid::NewGuid();
			}
			Record.Type = EEpicodeEntityType::Prop;
			Record.Name = Build.EntityType;
			Record.Transform.Location = Build.Location;
			Record.Transform.Rotation = Build.Rotation;
			Record.Data = MakeShared<FJsonObject>();
			Record.Data->SetStringField(TEXT("staticMesh"), CubeMesh);
			Actor = EpicodeActorFactory::SpawnForRecord(World, Record);
			if (Actor)
			{
				Actor->SetReplicates(true);
				RuntimeBuildActors.Add(Build.BuildId, Actor);
			}
		}
		if (Actor)
		{
			Actor->SetActorLocationAndRotation(Build.Location, Build.Rotation);
		}
	}
	for (auto It = RuntimeBuildActors.CreateIterator(); It; ++It)
	{
		if (!LiveBuilds.Contains(It.Key()))
		{
			if (AActor* Actor = It.Value().Get())
			{
				Actor->Destroy();
			}
			It.RemoveCurrent();
		}
	}
}

bool UEpicodeNetworkSubsystem::CreateSession(const FString& InSceneId, const FString& InOwnerPlayerId, const FString& InOwnerDisplayName, int32 InMaxPlayers, double InReconnectGraceSeconds, FString& OutError, int32 InMaxViewers)
{
	LastError.Reset();
	if (Session.IsValid() && Session->GetState() != EEpicodeSessionState::Ended)
	{
		OutError = TEXT("SESSION_ALREADY_EXISTS: end the current session before creating another.");
		LastError = OutError;
		return false;
	}

	FEpicodeNetworkSceneSummary Summary;
	if (!LoadScene(InSceneId, Summary, OutError))
	{
		LastError = OutError;
		return false;
	}

	const UEpicodeNetworkSettings* Settings = UEpicodeNetworkSettings::Get();
	WorldState.Reset();
	FEpicodeRecipe ToolRecipe;
	ToolRecipe.RecipeId = TEXT("craft.tool");
	ToolRecipe.DisplayName = TEXT("Craft Tool");
	ToolRecipe.InputItemTypes = { TEXT("scrap") };
	ToolRecipe.OutputItemType = TEXT("tool");
	ToolRecipe.Duration = 2.0;
	FString RecipeError;
	WorldState.AddRecipe(ToolRecipe, RecipeError);
	TSharedPtr<FEpicodeNetworkSession> NewSession = MakeShared<FEpicodeNetworkSession>();
	if (!NewSession->Create(
		InSceneId,
		InOwnerPlayerId,
		InOwnerDisplayName,
		InMaxPlayers > 0 ? InMaxPlayers : Settings->MaxPlayers,
		InReconnectGraceSeconds >= 0.0 ? InReconnectGraceSeconds : Settings->ReconnectGraceSeconds,
		OutError,
		InMaxViewers > 0 ? InMaxViewers : Settings->MaxViewers))
	{
		LastError = OutError;
		return false;
	}
	NewSession->SetSceneVersion(Summary.SchemaVersion);
	if (!NewSession->Start(OutError))
	{
		LastError = OutError;
		return false;
	}

	Session = NewSession;
	SceneSummary = Summary;
	CreateSandboxBridge(Summary);
	SpawnRuntimeSceneActors();
	LastExportedEventSequence = -1;
	LastExportedWorldVersion = 0;
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("sceneId"), Summary.SceneId);
	Payload->SetNumberField(TEXT("entityCount"), Summary.EntityCount);
	Session->PublishEvent(TEXT("scene.loaded"), InOwnerPlayerId, FString(), Payload);
	MaybeExport();
	if (UWorld* TravelWorld = FindGameWorld(); ShouldTravelToLevel(TravelWorld, Summary.LevelPath))
	{
		const FString TravelUrl = Summary.LevelPath + TEXT("?EpicodeRestoreSession=1");
		UE_LOG(LogEpicodeNetwork, Log, TEXT("Travelling to runtime level '%s'"), *TravelUrl);
		TravelWorld->ServerTravel(TravelUrl, false, false);
	}
	return true;
}

bool UEpicodeNetworkSubsystem::StartSession(FString& OutError)
{
	LastError.Reset();
	if (!Session.IsValid())
	{
		OutError = TEXT("SESSION_NOT_FOUND: create a session first.");
		LastError = OutError;
		return false;
	}
	const bool bResult = Session->Start(OutError);
	if (!bResult)
	{
		LastError = OutError;
		return false;
	}
	MaybeExport();
	return true;
}

bool UEpicodeNetworkSubsystem::JoinSession(const FString& InPlayerId, const FString& InDisplayName, EEpicodeSessionRole InRole, FString& OutPlayerId, FString& OutError, EEpicodeParticipantKind InParticipantKind)
{
	LastError.Reset();
	if (!Session.IsValid())
	{
		OutError = TEXT("SESSION_NOT_FOUND: create a session first.");
		LastError = OutError;
		return false;
	}
	const bool bResult = Session->Join(InPlayerId, InDisplayName, InRole, OutPlayerId, OutError, InParticipantKind);
	if (!bResult)
	{
		LastError = OutError;
		return false;
	}
	Session->BumpWorldVersion();
	if (Session->GetOwnerPlayerId() != InPlayerId)
	{
		WorldState.TransferCrafts(InPlayerId, Session->GetOwnerPlayerId());
	}
	SyncSandboxPlayer(OutPlayerId);
	MaybeExport();
	return true;
}

bool UEpicodeNetworkSubsystem::LeaveSession(const FString& InPlayerId, FString& OutError)
{
	LastError.Reset();
	if (!Session.IsValid())
	{
		OutError = TEXT("SESSION_NOT_FOUND: create a session first.");
		LastError = OutError;
		return false;
	}
	const bool bResult = Session->Leave(InPlayerId, OutError);
	if (!bResult)
	{
		LastError = OutError;
		return false;
	}
	Session->BumpWorldVersion();
	if (SandboxBridge.IsValid())
	{
		FString BridgeError;
		SandboxBridge->SetPlayerConnected(InPlayerId, false, BridgeError);
	}
	RevokeAgentToken(InPlayerId);
	MaybeExport();
	return true;
}

bool UEpicodeNetworkSubsystem::ReconnectSession(const FString& InPlayerId, FString& OutError)
{
	LastError.Reset();
	if (!Session.IsValid())
	{
		OutError = TEXT("SESSION_NOT_FOUND: create a session first.");
		LastError = OutError;
		return false;
	}
	const bool bResult = Session->Reconnect(InPlayerId, OutError);
	if (!bResult)
	{
		LastError = OutError;
		return false;
	}
	for (auto It = AgentControllers.CreateIterator(); It; ++It)
	{
		if (AEpicodeAgentController* Controller = It.Value().Get())
		{
			Controller->StopMovement();
			Controller->UnPossess();
			Controller->Destroy();
		}
		It.RemoveCurrent();
	}
	AgentTokens.Reset();
	Session->BumpWorldVersion();
	SyncSandboxPlayer(InPlayerId);
	MaybeExport();
	return true;
}

bool UEpicodeNetworkSubsystem::AcquireAgentLease(const FString& InPlayerId, const FString& InCharacterId, FString& OutLeaseId, double& OutExpiresAt, FString& OutError)
{
	if (!Session.IsValid())
	{
		OutError = TEXT("SESSION_NOT_FOUND: create a session first.");
		return false;
	}
	if (!Session->AcquireLease(InPlayerId, InCharacterId, UEpicodeNetworkSettings::Get()->AgentLeaseSeconds, OutLeaseId, OutExpiresAt, OutError))
	{
		return false;
	}
	UWorld* World = FindGameWorld();
	if (World)
	{
		AEpicodeNetworkCharacter* Character = FindCharacterById(World, InCharacterId);
		if (!Character)
		{
			FString MeshPath;
			FTransform SpawnTransform = FTransform::Identity;
			if (GetCharacterInfo(InCharacterId, MeshPath, SpawnTransform))
			{
				const AEpicodeNetworkCharacter* CharacterDefaults = GetDefault<AEpicodeNetworkCharacter>();
				const double CapsuleHalfHeight = CharacterDefaults && CharacterDefaults->GetCapsuleComponent()
					? CharacterDefaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
					: 88.0;
				const FVector Location = AEpicodeNetworkCharacter::MakeCapsuleSpawnLocation(SpawnTransform.GetLocation(), CapsuleHalfHeight);
				FActorSpawnParameters SpawnParams;
				SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				Character = World->SpawnActor<AEpicodeNetworkCharacter>(
					AEpicodeNetworkCharacter::StaticClass(), Location, SpawnTransform.Rotator(), SpawnParams);
				if (Character)
				{
					Character->SetReplicates(true);
					Character->SetNetworkIdentity(InPlayerId, InCharacterId);
					Character->SetAppearance(MeshPath);
				}
			}
		}
		if (Character)
		{
			if (AEpicodeAgentController* Existing = AgentControllers.FindRef(InPlayerId).Get())
			{
				Existing->Destroy();
			}
			AEpicodeAgentController* Controller = World->SpawnActor<AEpicodeAgentController>();
			if (Controller)
			{
				Controller->Configure(InPlayerId, InCharacterId);
				Controller->Possess(Character);
				AgentControllers.Add(InPlayerId, Controller);
			}
		}
	}
	MaybeExport();
	return true;
}

bool UEpicodeNetworkSubsystem::HeartbeatAgentLease(const FString& InPlayerId, FString& OutLeaseId, double& OutExpiresAt, FString& OutError)
{
	return Session.IsValid() && Session->HeartbeatLease(InPlayerId, OutLeaseId, OutExpiresAt, OutError);
}

bool UEpicodeNetworkSubsystem::ReleaseAgentLease(const FString& InPlayerId, FString& OutError)
{
	if (!Session.IsValid())
	{
		OutError = TEXT("SESSION_NOT_FOUND: create a session first.");
		return false;
	}
	if (AEpicodeAgentController* Controller = AgentControllers.FindRef(InPlayerId).Get())
	{
		Controller->StopMovement();
		Controller->UnPossess();
		Controller->Destroy();
		AgentControllers.Remove(InPlayerId);
	}
	const bool bResult = Session->ReleaseLease(InPlayerId, OutError);
	if (bResult)
	{
		MaybeExport();
	}
	return bResult;
}

bool UEpicodeNetworkSubsystem::MoveAgent(const FString& InPlayerId, const FVector& InTarget, FString& OutError)
{
	const FEpicodeSessionPlayer* Player = Session.IsValid() ? Session->FindPlayer(InPlayerId) : nullptr;
	if (!Player)
	{
		OutError = FString::Printf(TEXT("PLAYER_NOT_FOUND: player '%s' is not part of the session."), *InPlayerId);
		return false;
	}
	if (Player->CharacterId.IsEmpty())
	{
		OutError = TEXT("CHARACTER_NOT_ASSIGNED: acquire a character lease first.");
		return false;
	}
	if (Player->LeaseId.IsEmpty() || Player->LeaseExpiresAt <= FPlatformTime::Seconds())
	{
		OutError = TEXT("LEASE_EXPIRED: acquire a character lease again.");
		return false;
	}
	AEpicodeAgentController* Controller = AgentControllers.FindRef(InPlayerId).Get();
	if (!Controller)
	{
		OutError = TEXT("NOT_CONTROLLER: the server has no Agent controller for this participant.");
		return false;
	}
	const bool bResult = Controller->MoveToAgentLocation(InTarget, 50.0f, OutError);
	if (bResult)
	{
		Session->BumpWorldVersion();
	}
	return bResult;
}

bool UEpicodeNetworkSubsystem::LookAgent(const FString& InPlayerId, const FVector& InTarget, FString& OutError)
{
	const FEpicodeSessionPlayer* Player = Session.IsValid() ? Session->FindPlayer(InPlayerId) : nullptr;
	if (!Player)
	{
		OutError = FString::Printf(TEXT("PLAYER_NOT_FOUND: player '%s' is not part of the session."), *InPlayerId);
		return false;
	}
	if (Player->CharacterId.IsEmpty())
	{
		OutError = TEXT("CHARACTER_NOT_ASSIGNED: acquire a character lease first.");
		return false;
	}
	if (Player->LeaseId.IsEmpty() || Player->LeaseExpiresAt <= FPlatformTime::Seconds())
	{
		OutError = TEXT("LEASE_EXPIRED: acquire a character lease again.");
		return false;
	}
	AEpicodeAgentController* Controller = AgentControllers.FindRef(InPlayerId).Get();
	if (!Controller)
	{
		OutError = TEXT("NOT_CONTROLLER: the server has no Agent controller for this participant.");
		return false;
	}
	Controller->LookAtAgentTarget(InTarget);
	return true;
}

bool UEpicodeNetworkSubsystem::StopAgent(const FString& InPlayerId, FString& OutError)
{
	const FEpicodeSessionPlayer* Player = Session.IsValid() ? Session->FindPlayer(InPlayerId) : nullptr;
	if (!Player)
	{
		OutError = FString::Printf(TEXT("PLAYER_NOT_FOUND: player '%s' is not part of the session."), *InPlayerId);
		return false;
	}
	if (Player->CharacterId.IsEmpty())
	{
		OutError = TEXT("CHARACTER_NOT_ASSIGNED: acquire a character lease first.");
		return false;
	}
	if (Player->LeaseId.IsEmpty() || Player->LeaseExpiresAt <= FPlatformTime::Seconds())
	{
		OutError = TEXT("LEASE_EXPIRED: acquire a character lease again.");
		return false;
	}
	AEpicodeAgentController* Controller = AgentControllers.FindRef(InPlayerId).Get();
	if (!Controller)
	{
		OutError = TEXT("NOT_CONTROLLER: the server has no Agent controller for this participant.");
		return false;
	}
	Controller->StopAgent();
	return true;
}

TSharedPtr<FJsonObject> UEpicodeNetworkSubsystem::GetAgentState(const FString& InPlayerId, FString& OutError) const
{
	const FEpicodeSessionPlayer* Player = Session.IsValid() ? Session->FindPlayer(InPlayerId) : nullptr;
	if (!Player)
	{
		OutError = FString::Printf(TEXT("PLAYER_NOT_FOUND: player '%s' is not part of the session."), *InPlayerId);
		return nullptr;
	}
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("playerId"), Player->PlayerId);
	Result->SetStringField(TEXT("characterId"), Player->CharacterId);
	Result->SetStringField(TEXT("participantKind"), LexToString(Player->ParticipantKind));
	Result->SetStringField(TEXT("leaseId"), Player->LeaseId);
	Result->SetNumberField(TEXT("leaseExpiresAt"), Player->LeaseExpiresAt);
	Result->SetNumberField(TEXT("stateVersion"), static_cast<double>(Player->StateVersion));
	Result->SetNumberField(TEXT("worldVersion"), static_cast<double>(Session->GetWorldVersion()));
	FEpicodeInventoryRecord Inventory;
	FString InventoryError;
	if (WorldState.GetInventory(InPlayerId, InPlayerId, Inventory, InventoryError))
	{
		Result->SetObjectField(TEXT("inventory"), Inventory.ToJson());
	}
	if (UWorld* World = FindGameWorld())
	{
		if (const AEpicodeNetworkCharacter* Character = FindCharacterById(World, Player->CharacterId))
		{
			Result->SetStringField(TEXT("location"), Character->GetActorLocation().ToString());
			Result->SetStringField(TEXT("rotation"), Character->GetActorRotation().ToString());
			Result->SetStringField(TEXT("velocity"), Character->GetVelocity().ToString());
			Result->SetStringField(TEXT("action"), Character->PerformanceState.ActionTag);
			Result->SetStringField(TEXT("emotion"), Character->PerformanceState.Emotion);
			Result->SetNumberField(TEXT("expressionIntensity"), Character->PerformanceState.ExpressionIntensity);
			Result->SetBoolField(TEXT("speaking"), Character->PerformanceState.bSpeaking);
		}
	}
	return Result;
}

FString UEpicodeNetworkSubsystem::IssueAgentToken(const FString& InPlayerId)
{
	RevokeAgentToken(InPlayerId);
	const FString Token = UEpicodeSettings::GenerateToken();
	AgentTokens.Add(Token, InPlayerId);
	return Token;
}

bool UEpicodeNetworkSubsystem::ResolveAgentToken(const FString& InToken, FString& OutPlayerId, FString& OutError) const
{
	const FString* Found = AgentTokens.Find(InToken);
	if (!Found || Found->IsEmpty())
	{
		OutError = TEXT("PARTICIPANT_NOT_BOUND: participant token is missing or invalid.");
		return false;
	}
	const FEpicodeSessionPlayer* Player = Session.IsValid() ? Session->FindPlayer(*Found) : nullptr;
	if (!Player || Player->ParticipantKind != EEpicodeParticipantKind::Agent || !Player->bConnected)
	{
		OutError = TEXT("FORBIDDEN: participant token is not bound to a connected agent.");
		return false;
	}
	OutPlayerId = *Found;
	return true;
}

bool UEpicodeNetworkSubsystem::CheckAgentCommandRate(const FString& InPlayerId, FString& OutError)
{
	if (InPlayerId.IsEmpty())
	{
		OutError = TEXT("PARTICIPANT_NOT_BOUND: a participant is required for rate limiting.");
		return false;
	}
	const int32 Limit = FMath::Max(1, UEpicodeNetworkSettings::Get()->AgentCommandsPerSecond);
	const double Now = FPlatformTime::Seconds();
	FScopeLock ScopeLock(&AgentRateLock);
	if (AgentRateWindows.Num() > 4096)
	{
		AgentRateWindows.Reset();
	}
	FAgentRateWindow& Window = AgentRateWindows.FindOrAdd(InPlayerId);
	if (Window.WindowStart <= 0.0 || Now - Window.WindowStart >= 1.0)
	{
		Window.WindowStart = Now;
		Window.Count = 0;
	}
	if (Window.Count >= Limit)
	{
		OutError = FString::Printf(TEXT("RATE_LIMITED: agent '%s' exceeded %d commands per second."), *InPlayerId, Limit);
		return false;
	}
	++Window.Count;
	return true;
}

void UEpicodeNetworkSubsystem::RevokeAgentToken(const FString& InPlayerId)
{
	for (auto It = AgentTokens.CreateIterator(); It; ++It)
	{
		if (It.Value() == InPlayerId)
		{
			It.RemoveCurrent();
		}
	}
}

bool UEpicodeNetworkSubsystem::EndSession(FString& OutError)
{
	LastError.Reset();
	if (!Session.IsValid())
	{
		OutError = TEXT("SESSION_NOT_FOUND: create a session first.");
		LastError = OutError;
		return false;
	}
	const bool bResult = Session->End(OutError);
	if (!bResult)
	{
		LastError = OutError;
		return false;
	}
	Session->BumpWorldVersion();
	CloseSandboxBridge();
	MaybeExport();
	return true;
}

bool UEpicodeNetworkSubsystem::ClearEndedSession(FString& OutError)
{
	LastError.Reset();
	if (!Session.IsValid())
	{
		OutError = TEXT("SESSION_NOT_FOUND: create a session first.");
		LastError = OutError;
		return false;
	}
	if (Session->GetState() != EEpicodeSessionState::Ended)
	{
		OutError = TEXT("SESSION_NOT_ENDED: end the session before clearing it.");
		LastError = OutError;
		return false;
	}
	for (auto It = AgentControllers.CreateIterator(); It; ++It)
	{
		if (AEpicodeAgentController* Controller = It.Value().Get())
		{
			Controller->StopMovement();
			Controller->UnPossess();
			Controller->Destroy();
		}
		It.RemoveCurrent();
	}
	AgentTokens.Reset();
	CloseSandboxBridge();
	DestroyRuntimeSceneActors();
	DestroyRuntimeWorldActors();
	Session.Reset();
	SceneSummary = FEpicodeNetworkSceneSummary();
	WorldState.Reset();
	return true;
}

bool UEpicodeNetworkSubsystem::KickPlayer(const FString& InRequesterId, const FString& InTargetId, FString& OutError)
{
	LastError.Reset();
	if (!Session.IsValid())
	{
		OutError = TEXT("SESSION_NOT_FOUND: create a session first.");
		LastError = OutError;
		return false;
	}
	const bool bResult = Session->Kick(InRequesterId, InTargetId, OutError);
	if (!bResult)
	{
		LastError = OutError;
		return false;
	}
	Session->BumpWorldVersion();
	if (SandboxBridge.IsValid())
	{
		FString BridgeError;
		SandboxBridge->SetPlayerConnected(InTargetId, false, BridgeError);
	}
	MaybeExport();
	return true;
}

bool UEpicodeNetworkSubsystem::SetPlayerRole(const FString& InRequesterId, const FString& InTargetId, EEpicodeSessionRole InRole, FString& OutError)
{
	LastError.Reset();
	if (!Session.IsValid())
	{
		OutError = TEXT("SESSION_NOT_FOUND: create a session first.");
		LastError = OutError;
		return false;
	}
	const bool bResult = Session->SetRole(InRequesterId, InTargetId, InRole, OutError);
	if (!bResult)
	{
		LastError = OutError;
		return false;
	}
	Session->BumpWorldVersion();
	SyncSandboxPlayer(InTargetId);
	MaybeExport();
	return true;
}

bool UEpicodeNetworkSubsystem::AssignCharacter(const FString& InRequesterId, const FString& InTargetPlayerId, const FString& InCharacterId, FString& OutError)
{
	LastError.Reset();
	if (!Session.IsValid())
	{
		OutError = TEXT("SESSION_NOT_FOUND: create a session first.");
		LastError = OutError;
		return false;
	}
	const bool bResult = Session->AssignCharacter(InRequesterId, InTargetPlayerId, InCharacterId, OutError);
	if (!bResult)
	{
		LastError = OutError;
		return false;
	}
	SyncSandboxPlayer(InTargetPlayerId);
	MaybeExport();
	return true;
}

bool UEpicodeNetworkSubsystem::ReleaseCharacter(const FString& InRequesterId, const FString& InTargetPlayerId, FString& OutError)
{
	LastError.Reset();
	if (!Session.IsValid())
	{
		OutError = TEXT("SESSION_NOT_FOUND: create a session first.");
		LastError = OutError;
		return false;
	}
	const bool bResult = Session->ReleaseCharacter(InRequesterId, InTargetPlayerId, OutError);
	if (!bResult)
	{
		LastError = OutError;
		return false;
	}
	SyncSandboxPlayer(InTargetPlayerId);
	MaybeExport();
	return true;
}

bool UEpicodeNetworkSubsystem::TransferCharacter(const FString& InRequesterId, const FString& InFromPlayerId, const FString& InToPlayerId, const FString& InCharacterId, FString& OutError)
{
	LastError.Reset();
	if (!Session.IsValid())
	{
		OutError = TEXT("SESSION_NOT_FOUND: create a session first.");
		LastError = OutError;
		return false;
	}
	if (!Session->TransferCharacter(InRequesterId, InFromPlayerId, InToPlayerId, InCharacterId, OutError))
	{
		LastError = OutError;
		return false;
	}
	SyncSandboxPlayer(InToPlayerId);
	SyncSandboxPlayer(InFromPlayerId);
	MaybeExport();
	return true;
}

bool UEpicodeNetworkSubsystem::RecordPlayerTransform(const FString& InPlayerId, const FVector& InLocation, const FRotator& InRotation, FString& OutError)
{
	if (!Session.IsValid())
	{
		OutError = TEXT("SESSION_NOT_FOUND: create a session first.");
		return false;
	}
	if (!Session->RecordPlayerTransform(InPlayerId, InLocation, InRotation, OutError))
	{
		return false;
	}
	if (SandboxBridge.IsValid())
	{
		FString BridgeError;
		if (!SandboxBridge->RecordPlayerTransform(InPlayerId, InLocation, InRotation, BridgeError))
		{
			UE_LOG(LogEpicodeNetwork, Verbose, TEXT("Sandbox transform not recorded: %s"), *BridgeError);
		}
	}
	return true;
}

bool UEpicodeNetworkSubsystem::PublishPerformanceEvent(const FString& InPlayerId, const FString& InCharacterId, const FString& InType, const TSharedPtr<FJsonObject>& InPayload)
{
	if (!Session.IsValid())
	{
		LastError = TEXT("SESSION_NOT_FOUND: create a session first.");
		return false;
	}
	if (!Session->PublishEvent(InType, InPlayerId, InCharacterId, InPayload))
	{
		LastError = TEXT("EVENT_REJECTED: performance event was rejected.");
		return false;
	}
	MaybeExport();
	return true;
}

bool UEpicodeNetworkSubsystem::PublishWorldEvent(const FString& InPlayerId, const FString& InEntityId, const FString& InType, const TSharedPtr<FJsonObject>& InPayload)
{
	if (!Session.IsValid())
	{
		LastError = TEXT("SESSION_NOT_FOUND: create a session first.");
		return false;
	}
	if (!Session->PublishEvent(InType, InPlayerId, InEntityId, InPayload))
	{
		LastError = TEXT("EVENT_REJECTED: world event was rejected.");
		return false;
	}
	MaybeExport();
	return true;
}

bool UEpicodeNetworkSubsystem::ExportSession(FString& OutSessionPath, FString& OutEventPath, FString& OutError)
{
	if (!Session.IsValid() || Session->GetSessionId().IsEmpty())
	{
		OutError = TEXT("SESSION_NOT_FOUND: create a session first.");
		LastError = OutError;
		return false;
	}
	const FString Directory = FPaths::Combine(UEpicodeNetworkSettings::Get()->GetSessionRootPath(), Session->GetSessionId());
	if (!Session->Export(Directory, OutSessionPath, OutEventPath, OutError))
	{
		LastError = OutError;
		return false;
	}
	LastExportedEventSequence = Session->GetEventSequence();
	LastExportedWorldVersion = Session->GetWorldVersion();
	return true;
}

bool UEpicodeNetworkSubsystem::LoadScene(const FString& InSceneId, FEpicodeNetworkSceneSummary& OutSummary, FString& OutError) const
{
	return FEpicodeNetworkSceneLoader::Load(InSceneId, OutSummary, OutError);
}

bool UEpicodeNetworkSubsystem::GetNextFreeCharacter(FString& OutEntityId, FString& OutMeshPath, FTransform& OutTransform) const
{
	if (!Session.IsValid())
	{
		return false;
	}
	TSet<FString> Used;
	for (const FEpicodeSessionPlayer& Player : Session->GetPlayers())
	{
		if (!Player.CharacterId.IsEmpty())
		{
			Used.Add(Player.CharacterId);
		}
	}
	for (const FEpicodeNetworkSceneCharacter& Character : SceneSummary.Characters)
	{
		if (Used.Contains(Character.EntityId))
		{
			continue;
		}
		OutEntityId = Character.EntityId;
		OutMeshPath = Character.MeshPath;
		OutTransform = Character.Transform.ToFTransform();
		return true;
	}
	return false;
}

bool UEpicodeNetworkSubsystem::GetCharacterInfo(const FString& InEntityId, FString& OutMeshPath, FTransform& OutTransform) const
{
	for (const FEpicodeNetworkSceneCharacter& Character : SceneSummary.Characters)
	{
		if (Character.EntityId == InEntityId)
		{
			OutMeshPath = Character.MeshPath;
			OutTransform = Character.Transform.ToFTransform();
			return true;
		}
	}
	return false;
}

UWorld* UEpicodeNetworkSubsystem::FindGameWorld() const
{
	if (!GEngine)
	{
		return nullptr;
	}
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.World() && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE))
		{
			return Context.World();
		}
	}
	return nullptr;
}

void UEpicodeNetworkSubsystem::MaybeExport()
{
	const UEpicodeNetworkSettings* Settings = UEpicodeNetworkSettings::Get();
	if (!Settings->bAutoExportSession || !Session.IsValid() || Session->GetSessionId().IsEmpty())
	{
		return;
	}
	if (Session->GetEventSequence() == LastExportedEventSequence && Session->GetWorldVersion() == LastExportedWorldVersion)
	{
		return;
	}
	FString SessionPath;
	FString EventPath;
	FString Error;
	if (!ExportSession(SessionPath, EventPath, Error))
	{
		UE_LOG(LogEpicodeNetwork, Warning, TEXT("Session auto-export failed: %s"), *Error);
	}
}

TSharedPtr<FJsonObject> UEpicodeNetworkSubsystem::GetStatusJson() const
{
	const UEpicodeNetworkSettings* Settings = UEpicodeNetworkSettings::Get();
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("module"), TEXT("EpicodeNetwork"));
	Json->SetStringField(TEXT("onlineSubsystem"), Settings->OnlineSubsystem);
	Json->SetNumberField(TEXT("port"), Settings->GamePort);
	Json->SetNumberField(TEXT("maxPlayers"), Settings->MaxPlayers);
	Json->SetNumberField(TEXT("maxViewers"), Settings->MaxViewers);
	Json->SetStringField(TEXT("sessionRoot"), Settings->GetSessionRootPath());
	Json->SetNumberField(TEXT("reconnectGraceSeconds"), Settings->ReconnectGraceSeconds);
	Json->SetStringField(TEXT("lastError"), LastError);
	FEpicodeRuntimeMode::Resolve().ApplyToJson(Json);

	if (!Session.IsValid())
	{
		Json->SetStringField(TEXT("state"), TEXT("idle"));
		Json->SetStringField(TEXT("sessionId"), FString());
		Json->SetStringField(TEXT("sceneId"), FString());
		Json->SetNumberField(TEXT("playerCount"), 0);
		Json->SetNumberField(TEXT("viewerCount"), 0);
		Json->SetNumberField(TEXT("connectedPlayers"), 0);
		return Json;
	}

	Json->SetStringField(TEXT("state"), LexToString(Session->GetState()));
	Json->SetStringField(TEXT("sessionId"), Session->GetSessionId());
	Json->SetStringField(TEXT("sceneId"), Session->GetSceneId());
	Json->SetNumberField(TEXT("sceneVersion"), Session->GetSceneVersion());
	Json->SetNumberField(TEXT("playerCount"), Session->GetPlayers().Num());
	int32 Connected = 0;
	for (const FEpicodeSessionPlayer& Player : Session->GetPlayers())
	{
		Connected += Player.bConnected ? 1 : 0;
	}
	Json->SetNumberField(TEXT("connectedPlayers"), Connected);
	Json->SetNumberField(TEXT("eventSequence"), static_cast<double>(Session->GetEventSequence()));
	Json->SetNumberField(TEXT("worldVersion"), static_cast<double>(Session->GetWorldVersion()));
	if (SandboxBridge.IsValid())
	{
		Json->SetObjectField(TEXT("sandbox"), SandboxBridge->GetReplicationSnapshot());
	}
	return Json;
}
