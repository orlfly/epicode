#include "Misc/AutomationTest.h"

#include "EpicodeCommand.h"
#include "EpicodeAgentController.h"
#include "EpicodeDiagnostics.h"
#include "EpicodeEntityActors.h"
#include "EpicodeLocalIK.h"
#include "EpicodeEngineSubsystem.h"
#include "EpicodeNetworkCharacter.h"
#include "EpicodeNetworkCommands.h"
#include "EpicodeNetworkGameMode.h"
#include "EpicodeNetworkModule.h"
#include "EpicodeNetworkPlayerController.h"
#include "EpicodeNetworkSceneLoader.h"
#include "EpicodeNetworkSession.h"
#include "EpicodeNetworkSettings.h"
#include "EpicodeNetworkSubsystem.h"
#include "EpicodeNetworkTypes.h"
#include "EpicodeRuntimeEnvironmentActor.h"
#include "EpicodeRuntimeMode.h"
#include "EpicodeViewerPlayerController.h"
#include "EpicodeViewerSpectatorPawn.h"
#include "Camera/CameraActor.h"
#include "Engine/PointLight.h"
#include "EpicodePerformanceTypes.h"
#include "EpicodeWorldState.h"
#include "EpicodeSceneDocument.h"
#include "EpicodeSceneLibrary.h"
#include "EpicodeSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

#if PLATFORM_LINUX
#include <sys/stat.h>
#endif

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FString MakeSceneFixture(FString& OutSceneId, FString& OutDocumentPath, bool bIncludeCharacter = false, bool bIncludeWorldPresentation = false)
	{
		const FString Root = UEpicodeSettings::Get()->GetSceneRootPath();
		FEpicodeSceneDocument Document = FEpicodeSceneDocument::CreateDefault(TEXT("Network Test Scene"));
		OutSceneId = Document.SceneId;
		FEpicodeEntityRecord Entity;
		Entity.Id = FGuid::NewGuid();
		Entity.Type = EEpicodeEntityType::Prop;
		Entity.Name = TEXT("Network Test Prop");
		Document.Entities.Add(Entity);
		if (bIncludeCharacter)
		{
			FEpicodeEntityRecord Character;
			Character.Id = FGuid::NewGuid();
			Character.Type = EEpicodeEntityType::Character;
			Character.Name = TEXT("Network Test Character");
			Character.Transform.Location = FVector(100.0, 200.0, 0.0);
			Character.Data->SetStringField(TEXT("skeletalMesh"), TEXT("/Engine/EngineMeshes/SkeletalCube"));
			Document.Entities.Add(Character);
		}
		if (bIncludeWorldPresentation)
		{
			FEpicodeEntityRecord Light;
			Light.Id = FGuid::NewGuid();
			Light.Type = EEpicodeEntityType::Light;
			Light.Name = TEXT("Test Key Light");
			Light.Transform.Location = FVector(0.0, 0.0, 300.0);
			Light.Data->SetStringField(TEXT("lightType"), TEXT("point"));
			Document.Entities.Add(Light);

			FEpicodeEntityRecord Camera;
			Camera.Id = FGuid::NewGuid();
			Camera.Type = EEpicodeEntityType::Camera;
			Camera.Name = TEXT("Test Camera");
			Camera.Transform.Location = FVector(0.0, 0.0, 150.0);
			Camera.Data->SetStringField(TEXT("focalLength"), TEXT("35"));
			Document.Entities.Add(Camera);

			Document.Environment = MakeEpicodeDefaultEnvironment();
			Document.Environment->SetNumberField(TEXT("timeOfDay"), 14.5);
			Document.Runtime.Viewer.DefaultCameraId = Camera.Id.ToString(EGuidFormats::DigitsWithHyphens);
			Document.Runtime.Viewer.CameraIds.Add(Camera.Id.ToString(EGuidFormats::DigitsWithHyphens));
			Document.Runtime.Viewer.SpawnTransform = Camera.Transform;
		}
		Document.MarkModified();
		FString Error;
		if (!FEpicodeSceneLibrary::Save(Document, Root, OutDocumentPath, Error))
		{
			return FString();
		}
		return Root;
	}

	void CleanupScene(const FString& InRoot, const FString& InSceneId)
	{
		if (!InRoot.IsEmpty() && !InSceneId.IsEmpty())
		{
			IFileManager::Get().DeleteDirectory(*FPaths::Combine(InRoot, InSceneId), false, true);
		}
	}

	FString MakeTempDirectory(const FString& InName)
	{
		const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Epicode"), TEXT("NetworkTests"), InName + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
		IFileManager::Get().MakeDirectory(*Directory, true);
		return Directory;
	}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkSessionLifecycleTest, "Epicode.Network.SessionLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkSessionLifecycleTest::RunTest(const FString& Parameters)
{
	FEpicodeNetworkSession Session;
	FString Error;

	FString Joined;
	TestFalse(TEXT("join before create rejected"), Session.Join(TEXT("p1"), TEXT("P1"), EEpicodeSessionRole::Performer, Joined, Error));
	TestTrue(TEXT("create"), Session.Create(TEXT("scene-1"), TEXT("owner-1"), TEXT("Owner"), 4, 30.0, Error));
	TestEqual(TEXT("created state"), LexToString(Session.GetState()), FString(TEXT("created")));
	TestTrue(TEXT("start"), Session.Start(Error));
	TestEqual(TEXT("active state"), LexToString(Session.GetState()), FString(TEXT("active")));
	TestTrue(TEXT("start is idempotent"), Session.Start(Error));

	TestTrue(TEXT("join performer"), Session.Join(TEXT("p1"), TEXT("P1"), EEpicodeSessionRole::Performer, Joined, Error));
	TestEqual(TEXT("join returns player id"), Joined, FString(TEXT("p1")));
	TestTrue(TEXT("leave"), Session.Leave(TEXT("p1"), Error));
	TestFalse(TEXT("leave twice rejected"), Session.Leave(TEXT("p1"), Error));
	TestTrue(TEXT("end"), Session.End(Error));
	TestEqual(TEXT("ended state"), LexToString(Session.GetState()), FString(TEXT("ended")));
	TestFalse(TEXT("start after end rejected"), Session.Start(Error));
	TestFalse(TEXT("join after end rejected"), Session.Join(TEXT("p2"), TEXT("P2"), EEpicodeSessionRole::Performer, Joined, Error));

	FEpicodeNetworkSession Small;
	TestTrue(TEXT("small create"), Small.Create(TEXT("scene-small"), TEXT("owner-small"), TEXT("Owner"), 2, 30.0, Error));
	TestTrue(TEXT("small start"), Small.Start(Error));
	TestTrue(TEXT("first slot"), Small.Join(TEXT("small-1"), TEXT("One"), EEpicodeSessionRole::Performer, Joined, Error));
	TestFalse(TEXT("session full rejected"), Small.Join(TEXT("small-2"), TEXT("Two"), EEpicodeSessionRole::Performer, Joined, Error));
	TestTrue(TEXT("session full error code"), Error.Contains(TEXT("SESSION_FULL")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkReconnectTest, "Epicode.Network.Reconnect", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkReconnectTest::RunTest(const FString& Parameters)
{
	FEpicodeNetworkSession Session;
	FString Error;
	TestTrue(TEXT("create"), Session.Create(TEXT("scene-reconnect"), TEXT("owner-reconnect"), TEXT("Owner"), 4, 30.0, Error));
	TestTrue(TEXT("start"), Session.Start(Error));
	FString Joined;
	TestTrue(TEXT("join"), Session.Join(TEXT("performer-1"), TEXT("Performer"), EEpicodeSessionRole::Performer, Joined, Error));
	TestTrue(TEXT("assign character"), Session.AssignCharacter(TEXT("owner-reconnect"), TEXT("performer-1"), TEXT("character-1"), Error));
	TestTrue(TEXT("record runtime transform"),
		Session.RecordPlayerTransform(TEXT("performer-1"), FVector(500.0, 600.0, 88.0), FRotator(0.0, 45.0, 0.0), Error));
	const double Now = FPlatformTime::Seconds();
	TestTrue(TEXT("leave"), Session.Leave(TEXT("performer-1"), Error, Now));

	const FEpicodeSessionPlayer* Disconnected = Session.FindPlayer(TEXT("performer-1"));
	TestNotNull(TEXT("player retained while disconnected"), Disconnected);
	TestEqual(TEXT("character retained during grace"), Disconnected->CharacterId, FString(TEXT("character-1")));
	TestTrue(TEXT("reconnect within grace"), Session.Reconnect(TEXT("performer-1"), Error, Now + 1.0));
	TestEqual(TEXT("character restored"), Session.FindPlayer(TEXT("performer-1"))->CharacterId, FString(TEXT("character-1")));
	TestTrue(TEXT("runtime transform retained"), Session.FindPlayer(TEXT("performer-1"))->bHasRuntimeTransform);
	TestTrue(TEXT("runtime location retained"), Session.FindPlayer(TEXT("performer-1"))->RuntimeLocation.Equals(FVector(500.0, 600.0, 88.0)));
	TestTrue(TEXT("runtime rotation retained"), Session.FindPlayer(TEXT("performer-1"))->RuntimeRotation.Equals(FRotator(0.0, 45.0, 0.0)));
	TestFalse(TEXT("unknown player transform rejected"), Session.RecordPlayerTransform(TEXT("missing"), FVector::ZeroVector, FRotator::ZeroRotator, Error));
	TestTrue(TEXT("unknown player transform error"), Error.Contains(TEXT("PLAYER_NOT_FOUND")));
	TestFalse(TEXT("NaN transform rejected"), Session.RecordPlayerTransform(TEXT("performer-1"), FVector(FMath::Sqrt(-1.0), 0.0, 0.0), FRotator::ZeroRotator, Error));

	TestTrue(TEXT("leave again"), Session.Leave(TEXT("performer-1"), Error, Now + 2.0));
	Session.Tick(Now + 120.0);
	TestEqual(TEXT("character released after grace"), Session.FindPlayer(TEXT("performer-1"))->CharacterId, FString());
	TestFalse(TEXT("reconnect after grace rejected"), Session.Reconnect(TEXT("performer-1"), Error, Now + 120.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkPermissionsTest, "Epicode.Network.Permissions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkPermissionsTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("owner can manage"), FEpicodeSessionPolicy::CanPerform(EEpicodeSessionRole::Owner, EEpicodeSessionAction::ManageSession));
	TestFalse(TEXT("director cannot manage"), FEpicodeSessionPolicy::CanPerform(EEpicodeSessionRole::Director, EEpicodeSessionAction::ManageSession));
	TestTrue(TEXT("director can control scene"), FEpicodeSessionPolicy::CanPerform(EEpicodeSessionRole::Director, EEpicodeSessionAction::ControlScene));
	TestFalse(TEXT("director cannot interact with world"), FEpicodeSessionPolicy::CanPerform(EEpicodeSessionRole::Director, EEpicodeSessionAction::WorldInteract));
	TestTrue(TEXT("performer can interact with world"), FEpicodeSessionPolicy::CanPerform(EEpicodeSessionRole::Performer, EEpicodeSessionAction::WorldInteract));
	TestFalse(TEXT("observer cannot control scene"), FEpicodeSessionPolicy::CanPerform(EEpicodeSessionRole::Observer, EEpicodeSessionAction::ControlScene));
	TestTrue(TEXT("observer can query"), FEpicodeSessionPolicy::CanPerform(EEpicodeSessionRole::Observer, EEpicodeSessionAction::QuerySession));

	FEpicodeNetworkSession Session;
	FString Error;
	TestTrue(TEXT("create"), Session.Create(TEXT("scene-permissions"), TEXT("owner"), TEXT("Owner"), 4, 30.0, Error));
	TestTrue(TEXT("start"), Session.Start(Error));
	FString Joined;
	TestTrue(TEXT("join director"), Session.Join(TEXT("director"), TEXT("Director"), EEpicodeSessionRole::Director, Joined, Error));
	TestTrue(TEXT("join performer"), Session.Join(TEXT("performer"), TEXT("Performer"), EEpicodeSessionRole::Performer, Joined, Error));
	TestTrue(TEXT("join observer"), Session.Join(TEXT("observer"), TEXT("Observer"), EEpicodeSessionRole::Observer, Joined, Error));

	TestTrue(TEXT("director scene"), Session.CanPerform(TEXT("director"), EEpicodeSessionAction::ControlScene, Error));
	TestFalse(TEXT("director world rejected"), Session.CanPerform(TEXT("director"), EEpicodeSessionAction::WorldInteract, Error));
	TestTrue(TEXT("performer world"), Session.CanPerform(TEXT("performer"), EEpicodeSessionAction::WorldInteract, Error));
	TestFalse(TEXT("observer scene rejected"), Session.CanPerform(TEXT("observer"), EEpicodeSessionAction::ControlScene, Error));

	TestTrue(TEXT("owner changes role"), Session.SetRole(TEXT("owner"), TEXT("performer"), EEpicodeSessionRole::Observer, Error));
	TestFalse(TEXT("observer world rejected after role change"), Session.CanPerform(TEXT("performer"), EEpicodeSessionAction::WorldInteract, Error));
	TestFalse(TEXT("performer cannot change roles"), Session.SetRole(TEXT("director"), TEXT("observer"), EEpicodeSessionRole::Director, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkParticipantsTest, "Epicode.Network.Participants", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkParticipantsTest::RunTest(const FString& Parameters)
{
	FEpicodeNetworkSession Session;
	FString Error;
	TestTrue(TEXT("create"), Session.Create(TEXT("scene-participants"), TEXT("owner"), TEXT("Owner"), 8, 30.0, Error, 4));
	TestTrue(TEXT("start"), Session.Start(Error));
	FString Joined;
	TestTrue(TEXT("join agent"), Session.Join(TEXT("agent-1"), TEXT("Agent"), EEpicodeSessionRole::Performer, Joined, Error, EEpicodeParticipantKind::Agent));
	TestEqual(TEXT("agent kind"), LexToString(Session.FindPlayer(TEXT("agent-1"))->ParticipantKind), FString(TEXT("agent")));
	TestTrue(TEXT("join viewer"), Session.Join(TEXT("viewer-1"), TEXT("Viewer"), EEpicodeSessionRole::Performer, Joined, Error, EEpicodeParticipantKind::Viewer));
	TestEqual(TEXT("viewer kind"), LexToString(Session.FindPlayer(TEXT("viewer-1"))->ParticipantKind), FString(TEXT("viewer")));
	TestEqual(TEXT("viewer role forced observer"), LexToString(Session.FindPlayer(TEXT("viewer-1"))->Role), FString(TEXT("observer")));
	TestFalse(TEXT("kind mismatch rejected"), Session.Join(TEXT("agent-1"), TEXT("Agent"), EEpicodeSessionRole::Performer, Joined, Error, EEpicodeParticipantKind::Viewer));
	TestTrue(TEXT("kind mismatch error"), Error.Contains(TEXT("PARTICIPANT_KIND_MISMATCH")));

	const TSharedPtr<FJsonObject> Snapshot = Session.ToJson();
	TestEqual(TEXT("max viewers snapshot"), Snapshot->GetIntegerField(TEXT("maxViewers")), 4);
	TestEqual(TEXT("performer count"), Snapshot->GetIntegerField(TEXT("performerCount")), 2);
	TestEqual(TEXT("viewer count"), Snapshot->GetIntegerField(TEXT("viewerCount")), 1);
	const TArray<TSharedPtr<FJsonValue>> Players = Snapshot->GetArrayField(TEXT("players"));
	TestTrue(TEXT("participant kind in snapshot"), Players.Num() == 3 && Players[2]->AsObject()->GetStringField(TEXT("participantKind")) == TEXT("viewer"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkViewerSlotsTest, "Epicode.Network.ViewerSlots", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkViewerSlotsTest::RunTest(const FString& Parameters)
{
	FEpicodeNetworkSession Session;
	FString Error;
	TestTrue(TEXT("create"), Session.Create(TEXT("scene-viewer-slots"), TEXT("owner"), TEXT("Owner"), 2, 30.0, Error, 1));
	TestTrue(TEXT("start"), Session.Start(Error));
	FString Joined;
	TestTrue(TEXT("performer joins"), Session.Join(TEXT("performer-1"), TEXT("Performer"), EEpicodeSessionRole::Performer, Joined, Error));
	TestTrue(TEXT("viewer joins with performer full"), Session.Join(TEXT("viewer-1"), TEXT("Viewer"), EEpicodeSessionRole::Observer, Joined, Error, EEpicodeParticipantKind::Viewer));
	TestFalse(TEXT("second viewer rejected"), Session.Join(TEXT("viewer-2"), TEXT("Viewer 2"), EEpicodeSessionRole::Observer, Joined, Error, EEpicodeParticipantKind::Viewer));
	TestTrue(TEXT("viewer slot error"), Error.Contains(TEXT("VIEWER_SLOTS_FULL")));
	TestTrue(TEXT("viewer leaves"), Session.Leave(TEXT("viewer-1"), Error));
	TestTrue(TEXT("viewer slot released"), Session.Join(TEXT("viewer-2"), TEXT("Viewer 2"), EEpicodeSessionRole::Observer, Joined, Error, EEpicodeParticipantKind::Viewer));
	TestFalse(TEXT("performers still full"), Session.Join(TEXT("performer-2"), TEXT("Performer 2"), EEpicodeSessionRole::Performer, Joined, Error));
	TestTrue(TEXT("performer full error"), Error.Contains(TEXT("SESSION_FULL")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkViewerPermissionsTest, "Epicode.Network.ViewerPermissions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkViewerPermissionsTest::RunTest(const FString& Parameters)
{
	FEpicodeNetworkSession Session;
	FString Error;
	TestTrue(TEXT("create"), Session.Create(TEXT("scene-viewer-permissions"), TEXT("owner"), TEXT("Owner"), 4, 30.0, Error));
	TestTrue(TEXT("start"), Session.Start(Error));
	FString Joined;
	TestTrue(TEXT("join viewer"), Session.Join(TEXT("viewer"), TEXT("Viewer"), EEpicodeSessionRole::Observer, Joined, Error, EEpicodeParticipantKind::Viewer));
	TestTrue(TEXT("viewer can query"), Session.CanPerform(TEXT("viewer"), EEpicodeSessionAction::QuerySession, Error));
	TestFalse(TEXT("viewer cannot interact"), Session.CanPerform(TEXT("viewer"), EEpicodeSessionAction::WorldInteract, Error));
	TestTrue(TEXT("viewer interact error"), Error.Contains(TEXT("VIEWER_READ_ONLY")));
	TestFalse(TEXT("viewer cannot manage"), Session.CanPerform(TEXT("viewer"), EEpicodeSessionAction::ManageSession, Error));
	TestTrue(TEXT("viewer manage error"), Error.Contains(TEXT("VIEWER_READ_ONLY")));
	TestFalse(TEXT("viewer cannot control character"), Session.CanControlCharacter(TEXT("viewer"), TEXT("character-1"), Error));
	TestTrue(TEXT("viewer character error"), Error.Contains(TEXT("VIEWER_READ_ONLY")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkAgentLeaseTest, "Epicode.Network.AgentLease", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkAgentLeaseTest::RunTest(const FString& Parameters)
{
	FEpicodeNetworkSession Session;
	FString Error;
	TestTrue(TEXT("create"), Session.Create(TEXT("scene-agent-lease"), TEXT("owner"), TEXT("Owner"), 4, 30.0, Error));
	TestTrue(TEXT("start"), Session.Start(Error));
	FString Joined;
	TestTrue(TEXT("join agent"), Session.Join(TEXT("agent-a"), TEXT("Agent A"), EEpicodeSessionRole::Performer, Joined, Error, EEpicodeParticipantKind::Agent));
	FString LeaseId;
	double ExpiresAt = 0.0;
	TestTrue(TEXT("acquire lease"), Session.AcquireLease(TEXT("agent-a"), TEXT("character-a"), 5.0, LeaseId, ExpiresAt, Error));
	TestFalse(TEXT("lease id present"), LeaseId.IsEmpty());
	TestTrue(TEXT("lease expires"), ExpiresAt > 0.0);
	TestEqual(TEXT("lease character"), Session.FindPlayer(TEXT("agent-a"))->CharacterId, FString(TEXT("character-a")));
	TestTrue(TEXT("lease state version"), Session.FindPlayer(TEXT("agent-a"))->StateVersion > 0);

	const double FirstExpiry = ExpiresAt;
	FString RenewedLease;
	double RenewedExpiry = 0.0;
	TestTrue(TEXT("heartbeat"), Session.HeartbeatLease(TEXT("agent-a"), RenewedLease, RenewedExpiry, Error));
	TestEqual(TEXT("heartbeat lease id"), RenewedLease, LeaseId);
	TestTrue(TEXT("heartbeat extends"), RenewedExpiry >= FirstExpiry);

	TestTrue(TEXT("join agent b"), Session.Join(TEXT("agent-b"), TEXT("Agent B"), EEpicodeSessionRole::Performer, Joined, Error, EEpicodeParticipantKind::Agent));
	FString OtherLease;
	double OtherExpiry = 0.0;
	TestFalse(TEXT("conflicting lease rejected"), Session.AcquireLease(TEXT("agent-b"), TEXT("character-a"), 5.0, OtherLease, OtherExpiry, Error));
	TestTrue(TEXT("conflict error"), Error.Contains(TEXT("CHARACTER_ALREADY_OWNED")));

	TestTrue(TEXT("release lease"), Session.ReleaseLease(TEXT("agent-a"), Error));
	TestTrue(TEXT("character released"), Session.FindPlayer(TEXT("agent-a"))->CharacterId.IsEmpty());
	TestTrue(TEXT("lease cleared"), Session.FindPlayer(TEXT("agent-a"))->LeaseId.IsEmpty());

	TestTrue(TEXT("acquire expiring lease"), Session.AcquireLease(TEXT("agent-b"), TEXT("character-b"), 1.0, OtherLease, OtherExpiry, Error));
	Session.Tick(OtherExpiry + 1.0);
	TestTrue(TEXT("expired character released"), Session.FindPlayer(TEXT("agent-b"))->CharacterId.IsEmpty());
	TestTrue(TEXT("expired lease cleared"), Session.FindPlayer(TEXT("agent-b"))->LeaseId.IsEmpty());
	bool bFoundExpiryEvent = false;
	for (const FEpicodeNetworkEvent& Event : Session.GetEvents())
	{
		if (Event.Type == TEXT("agent.lease.expired"))
		{
			bFoundExpiryEvent = true;
		}
	}
	TestTrue(TEXT("lease expiry event"), bFoundExpiryEvent);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkSnapshotParticipantsTest, "Epicode.Network.SnapshotParticipants", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkSnapshotParticipantsTest::RunTest(const FString& Parameters)
{
	FEpicodeNetworkSession Session;
	FString Error;
	TestTrue(TEXT("create"), Session.Create(TEXT("scene-snapshot-participants"), TEXT("owner"), TEXT("Owner"), 3, 30.0, Error, 1));
	TestTrue(TEXT("start"), Session.Start(Error));
	FString Joined;
	TestTrue(TEXT("join performer"), Session.Join(TEXT("performer-1"), TEXT("Performer"), EEpicodeSessionRole::Performer, Joined, Error, EEpicodeParticipantKind::Performer));
	TestTrue(TEXT("join agent"), Session.Join(TEXT("agent-1"), TEXT("Agent"), EEpicodeSessionRole::Performer, Joined, Error, EEpicodeParticipantKind::Agent));
	TestTrue(TEXT("join viewer"), Session.Join(TEXT("viewer-1"), TEXT("Viewer"), EEpicodeSessionRole::Observer, Joined, Error, EEpicodeParticipantKind::Viewer));

	FString LeaseId;
	double LeaseExpiresAt = 0.0;
	TestTrue(TEXT("agent lease"), Session.AcquireLease(TEXT("agent-1"), TEXT("character-1"), 30.0, LeaseId, LeaseExpiresAt, Error));
	FEpicodeSessionPlayer* Viewer = Session.FindPlayer(TEXT("viewer-1"));
	TestNotNull(TEXT("viewer player"), Viewer);
	if (Viewer)
	{
		Viewer->ViewerCameraMode = TEXT("fixed");
		Viewer->ViewerCameraId = TEXT("cam-a");
		Viewer->StateVersion = 3;
	}

	const TSharedPtr<FJsonObject> Snapshot = Session.ToJson();
	TestEqual(TEXT("max viewers"), Snapshot->GetNumberField(TEXT("maxViewers")), 1.0);
	TestEqual(TEXT("performer count"), Snapshot->GetNumberField(TEXT("performerCount")), 3.0);
	TestEqual(TEXT("viewer count"), Snapshot->GetNumberField(TEXT("viewerCount")), 1.0);
	TestTrue(TEXT("event sequence"), Snapshot->GetNumberField(TEXT("eventSequence")) > 0.0);

	const TArray<TSharedPtr<FJsonValue>> Players = Snapshot->GetArrayField(TEXT("players"));
	TestEqual(TEXT("player count"), Players.Num(), 4);
	bool bViewerFound = false;
	for (const TSharedPtr<FJsonValue>& Value : Players)
	{
		const TSharedPtr<FJsonObject> Player = Value->AsObject();
		if (Player.IsValid() && Player->GetStringField(TEXT("playerId")) == TEXT("viewer-1"))
		{
			bViewerFound = true;
			TestEqual(TEXT("viewer kind"), Player->GetStringField(TEXT("participantKind")), FString(TEXT("viewer")));
			TestEqual(TEXT("viewer camera mode"), Player->GetStringField(TEXT("viewerCameraMode")), FString(TEXT("fixed")));
			TestEqual(TEXT("viewer camera id"), Player->GetStringField(TEXT("viewerCameraId")), FString(TEXT("cam-a")));
		}
		if (Player.IsValid() && Player->GetStringField(TEXT("playerId")) == TEXT("agent-1"))
		{
			TestEqual(TEXT("agent kind"), Player->GetStringField(TEXT("participantKind")), FString(TEXT("agent")));
			TestEqual(TEXT("agent lease"), Player->GetStringField(TEXT("leaseId")), LeaseId);
		}
	}
	TestTrue(TEXT("viewer payload"), bViewerFound);

	const TArray<TSharedPtr<FJsonValue>> Leases = Snapshot->GetArrayField(TEXT("agentLeases"));
	TestEqual(TEXT("lease count"), Leases.Num(), 1);
	if (Leases.Num() == 1)
	{
		const TSharedPtr<FJsonObject> Lease = Leases[0]->AsObject();
		TestEqual(TEXT("lease player"), Lease->GetStringField(TEXT("playerId")), FString(TEXT("agent-1")));
		TestEqual(TEXT("lease character"), Lease->GetStringField(TEXT("characterId")), FString(TEXT("character-1")));
		TestEqual(TEXT("lease id"), Lease->GetStringField(TEXT("leaseId")), LeaseId);
	}

	const TArray<TSharedPtr<FJsonValue>> Cameras = Snapshot->GetArrayField(TEXT("viewerCameras"));
	TestEqual(TEXT("viewer camera count"), Cameras.Num(), 1);
	if (Cameras.Num() == 1)
	{
		const TSharedPtr<FJsonObject> Camera = Cameras[0]->AsObject();
		TestEqual(TEXT("camera player"), Camera->GetStringField(TEXT("playerId")), FString(TEXT("viewer-1")));
		TestEqual(TEXT("camera mode"), Camera->GetStringField(TEXT("mode")), FString(TEXT("fixed")));
		TestEqual(TEXT("camera id"), Camera->GetStringField(TEXT("cameraId")), FString(TEXT("cam-a")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkAgentControllerTest, "Epicode.Network.AgentController", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkAgentControllerTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, FName(*FString::Printf(TEXT("EpicodeAgentControllerTest_%s"), *FGuid::NewGuid().ToString())));
	TestNotNull(TEXT("test world"), World);
	if (!World)
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	AEpicodeNetworkCharacter* Character = World->SpawnActor<AEpicodeNetworkCharacter>(AEpicodeNetworkCharacter::StaticClass(), FVector(10.0, 20.0, 30.0), FRotator::ZeroRotator);
	TestNotNull(TEXT("character spawned"), Character);
	AEpicodeAgentController* Agent = World->SpawnActor<AEpicodeAgentController>(AEpicodeAgentController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	TestNotNull(TEXT("agent controller spawned"), Agent);
	if (Character && Agent)
	{
		Agent->Configure(TEXT("agent-world"), TEXT("character-world"));
		Agent->Possess(Character);
		TestTrue(TEXT("controlled pawn"), Agent->GetPawn() == Character);
		TestEqual(TEXT("agent player"), Agent->GetAgentPlayerId(), FString(TEXT("agent-world")));
		TestEqual(TEXT("agent character"), Agent->GetAgentCharacterId(), FString(TEXT("character-world")));

		FString MoveError;
		const bool bMoved = Agent->MoveToAgentLocation(FVector(500.0, 500.0, 30.0), 50.0f, MoveError, false);
		TestFalse(TEXT("move without nav data rejected"), bMoved);
		TestTrue(TEXT("navigation error"), MoveError.Contains(TEXT("NAVIGATION_UNAVAILABLE")));

		Agent->LookAtAgentTarget(FVector(10.0, 120.0, 30.0));
		TestTrue(TEXT("look rotation"), Agent->GetControlRotation().Yaw > 0.0f);
		Agent->StopAgent();
		TestTrue(TEXT("stop accepted"), true);
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkAgentMovementTest, "Epicode.Network.AgentMovement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkAgentMovementTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, FName(*FString::Printf(TEXT("EpicodeAgentMovement_%s"), *FGuid::NewGuid().ToString())));
	TestNotNull(TEXT("test world"), World);
	if (!World)
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	AEpicodeNetworkCharacter* Character = World->SpawnActor<AEpicodeNetworkCharacter>(AEpicodeNetworkCharacter::StaticClass(), FVector(0.0, 0.0, 88.0), FRotator::ZeroRotator);
	AEpicodeAgentController* Agent = World->SpawnActor<AEpicodeAgentController>(AEpicodeAgentController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	TestNotNull(TEXT("character"), Character);
	TestNotNull(TEXT("agent"), Agent);
	if (Character && Agent)
	{
		Agent->Configure(TEXT("move-agent"), TEXT("move-character"));
		Agent->Possess(Character);
		const FVector Start = Character->GetActorLocation();
		FString Error;
		TestFalse(TEXT("without fallback rejected"), Agent->MoveToAgentLocation(FVector(600.0, 0.0, 88.0), 20.0f, Error, false));
		TestTrue(TEXT("unavailable code"), Error.Contains(TEXT("NAVIGATION_UNAVAILABLE")));
		TestTrue(TEXT("direct move accepted"), Agent->MoveToAgentLocation(FVector(600.0, 0.0, 88.0), 20.0f, Error, true));
		for (int32 Step = 0; Step < 20; ++Step)
		{
			Agent->AdvanceDirectMovement();
		}
		TestTrue(TEXT("direct move updated position"), Character->GetActorLocation().X > Start.X);
		Agent->StopAgent();
		const FVector Stopped = Character->GetActorLocation();
		for (int32 Step = 0; Step < 5; ++Step)
		{
			Agent->AdvanceDirectMovement();
		}
		TestTrue(TEXT("stop halts movement"), FVector::Dist(Stopped, Character->GetActorLocation()) < 1.0);
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkViewerAuthTest, "Epicode.Network.ViewerAuth", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkViewerAuthTest::RunTest(const FString& Parameters)
{
	FString Error;
	TestTrue(TEXT("viewer token available"), UEpicodeNetworkSettings::Get()->EnsureViewerToken(Error));
	const FString Token = UEpicodeNetworkSettings::Get()->GetViewerToken();
	TestFalse(TEXT("viewer token non-empty"), Token.IsEmpty());
	TestTrue(TEXT("valid viewer token"), AEpicodeViewerPlayerController::AuthorizeViewer(Token, false, Error));

	TestFalse(TEXT("missing token rejected"), AEpicodeViewerPlayerController::AuthorizeViewer(FString(), false, Error));
	TestTrue(TEXT("missing token code"), Error.Contains(TEXT("VIEWER_NOT_AUTHORIZED")));
	TestFalse(TEXT("wrong token rejected"), AEpicodeViewerPlayerController::AuthorizeViewer(TEXT("wrong-token"), false, Error));
	TestTrue(TEXT("wrong token code"), Error.Contains(TEXT("VIEWER_NOT_AUTHORIZED")));

	const bool bRemoteAllowed = UEpicodeNetworkSettings::Get()->bAllowRemoteViewers;
	TestEqual(TEXT("remote policy"), AEpicodeViewerPlayerController::AuthorizeViewer(Token, true, Error), bRemoteAllowed);
	if (!bRemoteAllowed)
	{
		TestTrue(TEXT("remote disabled code"), Error.Contains(TEXT("remote viewers are disabled")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeViewerCameraModesTest, "Epicode.Viewer.CameraModes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeViewerCameraModesTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, FName(*FString::Printf(TEXT("EpicodeViewerCameraModes_%s"), *FGuid::NewGuid().ToString())));
	TestNotNull(TEXT("test world"), World);
	if (!World)
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	AEpicodeViewerSpectatorPawn* Spectator = World->SpawnActor<AEpicodeViewerSpectatorPawn>(
		AEpicodeViewerSpectatorPawn::StaticClass(), FVector(0.0, 0.0, 180.0), FRotator::ZeroRotator);
	TestNotNull(TEXT("spectator spawned"), Spectator);
	if (Spectator)
	{
		TestNotNull(TEXT("viewer camera component"), Spectator->ViewerCamera.Get());
		TestEqual(TEXT("default camera"), Spectator->GetCameraMode(), FString(TEXT("free")));
		Spectator->SetCameraMode(TEXT("follow"), TEXT(""), TEXT("character-1"));
		TestEqual(TEXT("follow camera"), Spectator->GetCameraMode(), FString(TEXT("follow")));
		TestEqual(TEXT("follow target"), Spectator->GetFollowTargetId(), FString(TEXT("character-1")));
		Spectator->SetCameraMode(TEXT("fixed"), TEXT("cam-a"));
		TestEqual(TEXT("fixed camera"), Spectator->GetCameraMode(), FString(TEXT("fixed")));
		TestEqual(TEXT("preset camera id"), Spectator->GetCameraId(), FString(TEXT("cam-a")));
		Spectator->SetCameraMode(TEXT("preset"), TEXT("cam-b"));
		TestEqual(TEXT("preset camera"), Spectator->GetCameraMode(), FString(TEXT("preset")));
		Spectator->SetCameraMode(TEXT("free"));
		TestEqual(TEXT("free camera"), Spectator->GetCameraMode(), FString(TEXT("free")));
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeViewerOverlayTest, "Epicode.Viewer.Overlay", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeViewerOverlayTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, FName(*FString::Printf(TEXT("EpicodeViewerOverlay_%s"), *FGuid::NewGuid().ToString())));
	TestNotNull(TEXT("test world"), World);
	if (!World)
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	AEpicodeViewerPlayerController* Controller = World->SpawnActor<AEpicodeViewerPlayerController>();
	TestNotNull(TEXT("viewer controller"), Controller);
	if (Controller)
	{
		TestTrue(TEXT("overlay visible by default"), Controller->IsOverlayVisible());
		Controller->ViewerId = TEXT("overlay-viewer");
		Controller->ServerSetOverlayVisible_Implementation(false);
		TestFalse(TEXT("overlay hidden"), Controller->IsOverlayVisible());
		Controller->ServerSetOverlayVisible_Implementation(true);
		TestTrue(TEXT("overlay restored"), Controller->IsOverlayVisible());
		Controller->ViewerId.Reset();
		Controller->ServerSetOverlayVisible_Implementation(false);
		TestTrue(TEXT("unregistered viewer ignored"), Controller->IsOverlayVisible());
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkViewerSpawnTest, "Epicode.Network.ViewerSpawn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkViewerSpawnTest::RunTest(const FString& Parameters)
{
	FEpicodeNetworkSession Session;
	FString Error;
	TestTrue(TEXT("create"), Session.Create(TEXT("scene-viewer-spawn"), TEXT("owner"), TEXT("Owner"), 2, 30.0, Error, 2));
	TestTrue(TEXT("start"), Session.Start(Error));
	const FEpicodeSessionPlayer* OwnerBefore = Session.FindPlayer(TEXT("owner"));
	TestNotNull(TEXT("owner before"), OwnerBefore);
	const int32 PerformersBefore = static_cast<int32>(Session.ToJson()->GetNumberField(TEXT("performerCount")));
	FString Joined;
	TestTrue(TEXT("viewer join"), Session.Join(TEXT("viewer-spawn"), TEXT("Viewer"), EEpicodeSessionRole::Observer, Joined, Error, EEpicodeParticipantKind::Viewer));
	const FEpicodeSessionPlayer* Viewer = Session.FindPlayer(TEXT("viewer-spawn"));
	TestNotNull(TEXT("viewer player"), Viewer);
	if (Viewer)
	{
		TestTrue(TEXT("viewer has no character"), Viewer->CharacterId.IsEmpty());
		TestEqual(TEXT("viewer kind"), LexToString(Viewer->ParticipantKind), FString(TEXT("viewer")));
		TestEqual(TEXT("viewer role"), LexToString(Viewer->Role), FString(TEXT("observer")));
	}
	TestEqual(TEXT("performer slots unchanged"), static_cast<int32>(Session.ToJson()->GetNumberField(TEXT("performerCount"))), PerformersBefore);
	TestTrue(TEXT("owner unchanged"), Session.FindPlayer(TEXT("owner")) == OwnerBefore);
	TestFalse(TEXT("viewer cannot control character"), FEpicodeSessionPolicy::CanControlCharacter(EEpicodeSessionRole::Observer));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeViewerSetupTest, "Epicode.Viewer.Setup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeViewerSetupTest::RunTest(const FString& Parameters)
{
	FString SceneId;
	FString DocumentPath;
	const FString Root = MakeSceneFixture(SceneId, DocumentPath, true, true);
	TestFalse(TEXT("rich fixture"), Root.IsEmpty());
	if (Root.IsEmpty())
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, FName(*FString::Printf(TEXT("EpicodeViewerSetup_%s"), *FGuid::NewGuid().ToString())));
	TestNotNull(TEXT("test world"), World);
	if (!World)
	{
		CleanupScene(Root, SceneId);
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
	FString Error;
	TestTrue(TEXT("create session"), Subsystem->CreateSession(SceneId, TEXT("viewer-owner"), TEXT("Owner"), 4, 30.0, Error, 2));
	AEpicodeViewerPlayerController* Controller = World->SpawnActor<AEpicodeViewerPlayerController>();
	TestNotNull(TEXT("viewer controller"), Controller);
	if (Controller)
	{
		FString TokenError;
		TestTrue(TEXT("viewer token"), UEpicodeNetworkSettings::Get()->EnsureViewerToken(TokenError));
		Controller->ServerRegisterViewer_Implementation(TEXT("Viewer"), UEpicodeNetworkSettings::Get()->GetViewerToken(), TEXT("fixed"));
		TestTrue(TEXT("viewer registered"), Controller->IsViewerRegistered());
		TestEqual(TEXT("viewer camera mode"), Controller->GetViewerCameraMode(), FString(TEXT("fixed")));
		TestTrue(TEXT("viewer has spectator pawn"), Cast<AEpicodeViewerSpectatorPawn>(Controller->GetPawn()) != nullptr);
		TestTrue(TEXT("setup has viewer id"), Controller->GetSetupJson().Contains(TEXT("\"viewerId\"")));
		TestTrue(TEXT("setup has session"), Controller->GetSetupJson().Contains(TEXT("\"sessionId\"")));
		TestTrue(TEXT("setup has cameras"), Controller->GetSetupJson().Contains(TEXT("\"cameras\"")));
		TestTrue(TEXT("setup has participants"), Controller->GetSetupJson().Contains(TEXT("\"participants\"")));
		TestTrue(TEXT("setup has default camera"), Controller->GetSetupJson().Contains(TEXT("\"defaultCameraId\"")));

		Controller->ServerSetCameraMode_Implementation(TEXT("follow"), TEXT(""), TEXT("character-1"));
		TestEqual(TEXT("follow mode applied"), Controller->GetViewerCameraMode(), FString(TEXT("follow")));
		TestEqual(TEXT("follow target applied"), Controller->FollowTargetId, FString(TEXT("character-1")));
		Controller->ServerSetOverlayVisible_Implementation(false);
		TestFalse(TEXT("overlay hidden"), Controller->IsOverlayVisible());
		Controller->ServerSetOverlayVisible_Implementation(true);
		TestTrue(TEXT("overlay shown"), Controller->IsOverlayVisible());
	}
	Subsystem->EndSession(Error);
	Subsystem->ClearEndedSession(Error);
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	CleanupScene(Root, SceneId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeViewerFollowTargetTest, "Epicode.Viewer.FollowTarget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeViewerFollowTargetTest::RunTest(const FString& Parameters)
{
	FString SceneId;
	FString DocumentPath;
	const FString Root = MakeSceneFixture(SceneId, DocumentPath, true);
	TestFalse(TEXT("fixture"), Root.IsEmpty());
	if (Root.IsEmpty())
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, FName(*FString::Printf(TEXT("EpicodeViewerFollowTarget_%s"), *FGuid::NewGuid().ToString())));
	TestNotNull(TEXT("test world"), World);
	if (!World)
	{
		CleanupScene(Root, SceneId);
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
	FString Error;
	TestTrue(TEXT("create session"), Subsystem->CreateSession(SceneId, TEXT("follow-owner"), TEXT("Owner"), 4, 30.0, Error, 2));
	FString Joined;
	TestTrue(TEXT("join agent"), Subsystem->JoinSession(TEXT("follow-agent"), TEXT("Agent"), EEpicodeSessionRole::Performer, Joined, Error, EEpicodeParticipantKind::Agent));
	TestTrue(TEXT("assign first character"), Subsystem->AssignCharacter(TEXT("follow-owner"), TEXT("follow-agent"), TEXT("character-a"), Error));
	AEpicodeViewerPlayerController* Controller = World->SpawnActor<AEpicodeViewerPlayerController>();
	TestNotNull(TEXT("viewer controller"), Controller);
	if (Controller)
	{
		FString TokenError;
		TestTrue(TEXT("viewer token"), UEpicodeNetworkSettings::Get()->EnsureViewerToken(TokenError));
		Controller->ServerRegisterViewer_Implementation(TEXT("FollowViewer"), UEpicodeNetworkSettings::Get()->GetViewerToken(), TEXT("follow"));

		Controller->ServerSetCameraMode_Implementation(TEXT("follow"), TEXT(""), TEXT(""));
		TestEqual(TEXT("target selected"), Controller->FollowTargetId, FString(TEXT("character-a")));
		TestTrue(TEXT("release character"), Subsystem->ReleaseCharacter(TEXT("follow-owner"), TEXT("follow-agent"), Error));
		Controller->ServerSetCameraMode_Implementation(TEXT("follow"), TEXT(""), TEXT(""));
		TestEqual(TEXT("target held after leave"), Controller->FollowTargetId, FString(TEXT("character-a")));
		TestTrue(TEXT("assign second character"), Subsystem->AssignCharacter(TEXT("follow-owner"), TEXT("follow-agent"), TEXT("character-b"), Error));
		Controller->ServerSetCameraMode_Implementation(TEXT("follow"), TEXT(""), TEXT(""));
		TestEqual(TEXT("target reselected"), Controller->FollowTargetId, FString(TEXT("character-b")));
	}
	Subsystem->EndSession(Error);
	Subsystem->ClearEndedSession(Error);
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	CleanupScene(Root, SceneId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkOwnershipTest, "Epicode.Network.Ownership", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkOwnershipTest::RunTest(const FString& Parameters)
{
	FEpicodeNetworkSession Session;
	FString Error;
	TestTrue(TEXT("create"), Session.Create(TEXT("scene-ownership"), TEXT("owner"), TEXT("Owner"), 4, 30.0, Error));
	TestTrue(TEXT("start"), Session.Start(Error));
	FString Joined;
	TestTrue(TEXT("join performer"), Session.Join(TEXT("performer-a"), TEXT("A"), EEpicodeSessionRole::Performer, Joined, Error));
	TestTrue(TEXT("join performer b"), Session.Join(TEXT("performer-b"), TEXT("B"), EEpicodeSessionRole::Performer, Joined, Error));

	TestTrue(TEXT("assign to a"), Session.AssignCharacter(TEXT("owner"), TEXT("performer-a"), TEXT("character-1"), Error));
	TestFalse(TEXT("duplicate character rejected"), Session.AssignCharacter(TEXT("owner"), TEXT("performer-b"), TEXT("character-1"), Error));
	TestFalse(TEXT("second character rejected"), Session.AssignCharacter(TEXT("owner"), TEXT("performer-a"), TEXT("character-2"), Error));
	TestTrue(TEXT("release a"), Session.ReleaseCharacter(TEXT("owner"), TEXT("performer-a"), Error));
	TestTrue(TEXT("assign to b after release"), Session.AssignCharacter(TEXT("owner"), TEXT("performer-b"), TEXT("character-1"), Error));
	TestTrue(TEXT("b controls character"), Session.CanControlCharacter(TEXT("performer-b"), TEXT("character-1"), Error));
	TestFalse(TEXT("owner does not control b's character"), Session.CanControlCharacter(TEXT("owner"), TEXT("character-1"), Error));
	TestTrue(TEXT("transfer to a"), Session.TransferCharacter(TEXT("owner"), TEXT("performer-b"), TEXT("performer-a"), TEXT("character-1"), Error));
	TestEqual(TEXT("source released after transfer"), Session.FindPlayer(TEXT("performer-b"))->CharacterId, FString());
	TestEqual(TEXT("target owns after transfer"), Session.FindPlayer(TEXT("performer-a"))->CharacterId, FString(TEXT("character-1")));
	TestTrue(TEXT("owner demotes a to observer"), Session.SetRole(TEXT("owner"), TEXT("performer-a"), EEpicodeSessionRole::Observer, Error));
	TestTrue(TEXT("role change released character"), Session.FindPlayer(TEXT("performer-a"))->CharacterId.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkSnapshotTest, "Epicode.Network.Snapshot", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkSnapshotTest::RunTest(const FString& Parameters)
{
	FEpicodeNetworkSession Session;
	FString Error;
	TestTrue(TEXT("create"), Session.Create(TEXT("scene-snapshot"), TEXT("owner"), TEXT("Owner"), 4, 30.0, Error));
	TestTrue(TEXT("start"), Session.Start(Error));
	FString Joined;
	TestTrue(TEXT("join"), Session.Join(TEXT("p1"), TEXT("P1"), EEpicodeSessionRole::Performer, Joined, Error));
	TestTrue(TEXT("assign"), Session.AssignCharacter(TEXT("owner"), TEXT("p1"), TEXT("character-snapshot"), Error));

	const int64 Before = Session.GetEventSequence();
	TestTrue(TEXT("publish event"), Session.PublishEvent(TEXT("test.event"), TEXT("p1"), TEXT("character-snapshot")));
	TestTrue(TEXT("sequence monotonic"), Session.GetEventSequence() == Before + 1);

	bool bNeedsSnapshot = false;
	const TArray<FEpicodeNetworkEvent> Incremental = Session.GetEventsSince(2, 100, bNeedsSnapshot);
	TestFalse(TEXT("incremental does not need snapshot"), bNeedsSnapshot);
	TestTrue(TEXT("incremental events returned"), Incremental.Num() > 0);
	for (const FEpicodeNetworkEvent& Event : Incremental)
	{
		TestTrue(TEXT("event sequence greater than requested"), Event.Sequence > 2);
	}

	bool bNeedsFull = false;
	Session.GetEventsSince(-1, 100, bNeedsFull);
	TestTrue(TEXT("old cursor requires full snapshot"), bNeedsFull);

	const TSharedPtr<FJsonObject> Snapshot = Session.ToJson();
	TestEqual(TEXT("snapshot session id"), Snapshot->GetStringField(TEXT("sessionId")), Session.GetSessionId());
	TestEqual(TEXT("snapshot state"), Snapshot->GetStringField(TEXT("state")), FString(TEXT("active")));
	TestEqual(TEXT("snapshot player count"), Snapshot->GetArrayField(TEXT("players")).Num(), 2);
	TestEqual(TEXT("snapshot character count"), Snapshot->GetArrayField(TEXT("characters")).Num(), 1);
	const TSharedPtr<FJsonObject> SnapshotCharacter = Snapshot->GetArrayField(TEXT("characters"))[0]->AsObject();
	TestEqual(TEXT("snapshot character replication class"), SnapshotCharacter->GetStringField(TEXT("replicationClass")), FString(TEXT("high")));
	TestEqual(TEXT("snapshot character update frequency"), SnapshotCharacter->GetNumberField(TEXT("netUpdateFrequency")), 30.0);

	const FString Directory = MakeTempDirectory(TEXT("snapshot"));
	FString SessionPath;
	FString EventPath;
	TestTrue(TEXT("export"), Session.Export(Directory, SessionPath, EventPath, Error));
	TestTrue(TEXT("session file exists"), IFileManager::Get().FileExists(*SessionPath));
	TestTrue(TEXT("event file exists"), IFileManager::Get().FileExists(*EventPath));
	IFileManager::Get().DeleteDirectory(*Directory, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkSceneLoadTest, "Epicode.Network.SceneLoad", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkSceneLoadTest::RunTest(const FString& Parameters)
{
	FString SceneId;
	FString DocumentPath;
	const FString Root = MakeSceneFixture(SceneId, DocumentPath, true);
	TestFalse(TEXT("scene fixture created"), Root.IsEmpty());
	if (Root.IsEmpty())
	{
		return false;
	}

	FEpicodeNetworkSceneSummary Summary;
	FString Error;
	TestTrue(TEXT("scene loads"), FEpicodeNetworkSceneLoader::Load(SceneId, Summary, Error));
	TestEqual(TEXT("scene id matches"), Summary.SceneId, SceneId);
	TestEqual(TEXT("entity count"), Summary.EntityCount, 2);
	TestEqual(TEXT("character count"), Summary.Characters.Num(), 1);
	if (Summary.Characters.Num() == 1)
	{
		TestEqual(TEXT("character mesh"), Summary.Characters[0].MeshPath, FString(TEXT("/Engine/EngineMeshes/SkeletalCube")));
		TestTrue(TEXT("character transform"), Summary.Characters[0].Transform.Location.Equals(FVector(100.0, 200.0, 0.0)));
	}
	TestEqual(TEXT("schema version"), Summary.SchemaVersion, FEpicodeSceneDocument::CurrentSchemaVersion);

	FString JsonText;
	TestTrue(TEXT("scene file readable"), FFileHelper::LoadFileToString(JsonText, *DocumentPath));
	TSharedPtr<FJsonObject> Json;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	TestTrue(TEXT("scene json parses"), FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid());
	Json->SetNumberField(TEXT("schemaVersion"), 99);
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	TestTrue(TEXT("future scene serializes"), FJsonSerializer::Serialize(Json, Writer));
	TestTrue(TEXT("future scene writes"), FFileHelper::SaveStringToFile(JsonText, *DocumentPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	TestFalse(TEXT("future scene rejected"), FEpicodeNetworkSceneLoader::Load(SceneId, Summary, Error));
	TestTrue(TEXT("future scene error code"), Error.Contains(TEXT("SCENE_VERSION_UNSUPPORTED")));

	CleanupScene(Root, SceneId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkSceneIsolationTest, "Epicode.Network.SceneIsolation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkSceneIsolationTest::RunTest(const FString& Parameters)
{
	FString SceneId;
	FString DocumentPath;
	const FString Root = MakeSceneFixture(SceneId, DocumentPath, true);
	TestFalse(TEXT("scene fixture created"), Root.IsEmpty());
	if (Root.IsEmpty())
	{
		return false;
	}

	TArray<uint8> Before;
	TestTrue(TEXT("read scene bytes"), FFileHelper::LoadFileToArray(Before, *DocumentPath));
	const FDateTime BeforeTime = IFileManager::Get().GetTimeStamp(*DocumentPath);

	FEpicodeNetworkSceneSummary Summary;
	FString Error;
	TestTrue(TEXT("scene loads"), FEpicodeNetworkSceneLoader::Load(SceneId, Summary, Error));

	TArray<uint8> After;
	TestTrue(TEXT("read scene bytes after load"), FFileHelper::LoadFileToArray(After, *DocumentPath));
	const FDateTime AfterTime = IFileManager::Get().GetTimeStamp(*DocumentPath);
	TestEqual(TEXT("scene bytes unchanged"), After.Num(), Before.Num());
	TestTrue(TEXT("scene content unchanged"), After == Before);
	TestTrue(TEXT("scene mtime unchanged"), AfterTime == BeforeTime);

	CleanupScene(Root, SceneId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkSessionExportTest, "Epicode.Network.SessionExport", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkSessionExportTest::RunTest(const FString& Parameters)
{
	FString SceneId;
	FString DocumentPath;
	const FString Root = MakeSceneFixture(SceneId, DocumentPath, true);
	TestFalse(TEXT("scene fixture created"), Root.IsEmpty());
	if (Root.IsEmpty())
	{
		return false;
	}
	TArray<uint8> SceneBefore;
	TestTrue(TEXT("read scene before export"), FFileHelper::LoadFileToArray(SceneBefore, *DocumentPath));

	FEpicodeNetworkSession Session;
	FString Error;
	TestTrue(TEXT("create"), Session.Create(SceneId, TEXT("owner-export"), TEXT("Owner"), 4, 30.0, Error));
	TestTrue(TEXT("start"), Session.Start(Error));
	FString Joined;
	TestTrue(TEXT("join"), Session.Join(TEXT("player-export"), TEXT("Player"), EEpicodeSessionRole::Performer, Joined, Error));
	TestTrue(TEXT("assign"), Session.AssignCharacter(TEXT("owner-export"), TEXT("player-export"), TEXT("character-export"), Error));
	TestTrue(TEXT("end"), Session.End(Error));

	const FString Directory = MakeTempDirectory(TEXT("export"));
	FString SessionPath;
	FString EventPath;
	TestTrue(TEXT("export"), Session.Export(Directory, SessionPath, EventPath, Error));

	FString SessionText;
	TestTrue(TEXT("read exported session"), FFileHelper::LoadFileToString(SessionText, *SessionPath));
	TSharedPtr<FJsonObject> Exported;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SessionText);
	TestTrue(TEXT("exported session parses"), FJsonSerializer::Deserialize(Reader, Exported) && Exported.IsValid());
	TestEqual(TEXT("exported player count"), Exported->GetArrayField(TEXT("players")).Num(), 2);
	TestEqual(TEXT("exported character count"), Exported->GetArrayField(TEXT("characters")).Num(), 1);
	TestEqual(TEXT("exported ended state"), Exported->GetStringField(TEXT("state")), FString(TEXT("ended")));
	const TArray<TSharedPtr<FJsonValue>> ExportedCharacters = Exported->GetArrayField(TEXT("characters"));
	TestEqual(TEXT("exported character id"), ExportedCharacters[0]->AsObject()->GetStringField(TEXT("entityId")), FString(TEXT("character-export")));

	TArray<uint8> SceneAfter;
	TestTrue(TEXT("read scene after export"), FFileHelper::LoadFileToArray(SceneAfter, *DocumentPath));
	TestTrue(TEXT("scene unchanged by export"), SceneAfter == SceneBefore);

	IFileManager::Get().DeleteDirectory(*Directory, false, true);
	CleanupScene(Root, SceneId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkCommandsTest, "Epicode.Network.Commands", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkCommandsTest::RunTest(const FString& Parameters)
{
	UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
	TestNotNull(TEXT("network subsystem available"), Subsystem);
	if (!Subsystem)
	{
		return false;
	}

	FEpicodeCommandRegistry Registry;
	FEpicodeNetworkCommands::Register(Registry);

	const TArray<FString> Expected = {
		TEXT("session.create"),
		TEXT("session.start"),
		TEXT("session.join"),
		TEXT("session.leave"),
		TEXT("session.status"),
		TEXT("session.snapshot"),
		TEXT("session.export"),
		TEXT("session.end"),
		TEXT("session.kick"),
		TEXT("session.setRole"),
		TEXT("session.assignCharacter"),
		TEXT("session.releaseCharacter"),
		TEXT("session.transferCharacter"),
		TEXT("network.status")
	};
	for (const FString& Method : Expected)
	{
		TestTrue(FString::Printf(TEXT("registered %s"), *Method), Registry.Has(Method));
	}

	FEpicodeCommandContext Context;
	FEpicodeCommandResult Result;
	FString Error;
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Registry.Execute(TEXT("session.create"), Params, Context, Result, Error);
	TestFalse(TEXT("create without scene fails"), Result.bSuccess);
	TestEqual(TEXT("create error code"), Result.ErrorCode, FString(TEXT("INVALID_PARAMS")));

	Registry.Execute(TEXT("session.snapshot"), Params, Context, Result, Error);
	TestFalse(TEXT("snapshot without session fails"), Result.bSuccess);
	TestEqual(TEXT("snapshot without session code"), Result.ErrorCode, FString(TEXT("SESSION_NOT_FOUND")));

	FString SceneId;
	FString DocumentPath;
	const FString Root = MakeSceneFixture(SceneId, DocumentPath, true);
	TestFalse(TEXT("scene fixture created"), Root.IsEmpty());
	if (Root.IsEmpty())
	{
		return false;
	}
	const FString OwnerId = TEXT("command-owner");
	Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("sceneId"), SceneId);
	Params->SetStringField(TEXT("playerId"), OwnerId);
	Registry.Execute(TEXT("session.create"), Params, Context, Result, Error);
	TestTrue(TEXT("create succeeds"), Result.bSuccess);
	TestTrue(TEXT("create returns session"), Result.Result.IsValid() && Result.Result->HasField(TEXT("sessionId")));
	const FString SessionId = Result.Result.IsValid() ? Result.Result->GetStringField(TEXT("sessionId")) : FString();
	FString FreeCharacterId;
	FString FreeCharacterMesh;
	FTransform FreeTransform = FTransform::Identity;
	TestTrue(TEXT("scene character available"), Subsystem->GetNextFreeCharacter(FreeCharacterId, FreeCharacterMesh, FreeTransform));
	TestEqual(TEXT("scene character mesh"), FreeCharacterMesh, FString(TEXT("/Engine/EngineMeshes/SkeletalCube")));
	TestTrue(TEXT("scene character transform"), FreeTransform.GetLocation().Equals(FVector(100.0, 200.0, 0.0)));

	Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("playerId"), TEXT("command-performer"));
	Params->SetStringField(TEXT("displayName"), TEXT("Performer"));
	Registry.Execute(TEXT("session.join"), Params, Context, Result, Error);
	TestTrue(TEXT("join succeeds"), Result.bSuccess);

	Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("playerId"), TEXT("command-viewer"));
	Params->SetStringField(TEXT("displayName"), TEXT("Viewer"));
	Params->SetStringField(TEXT("role"), TEXT("performer"));
	Params->SetStringField(TEXT("participantKind"), TEXT("viewer"));
	Registry.Execute(TEXT("session.join"), Params, Context, Result, Error);
	TestTrue(TEXT("viewer join succeeds"), Result.bSuccess);
	if (Result.bSuccess && Result.Result.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>> Players = Result.Result->GetArrayField(TEXT("players"));
		bool bViewerFound = false;
		for (const TSharedPtr<FJsonValue>& Value : Players)
		{
			const TSharedPtr<FJsonObject> Player = Value->AsObject();
			if (Player.IsValid() && Player->GetStringField(TEXT("playerId")) == TEXT("command-viewer"))
			{
				bViewerFound = true;
				TestEqual(TEXT("viewer kind"), Player->GetStringField(TEXT("participantKind")), FString(TEXT("viewer")));
				TestEqual(TEXT("viewer role"), Player->GetStringField(TEXT("role")), FString(TEXT("observer")));
			}
		}
		TestTrue(TEXT("viewer in snapshot"), bViewerFound);
	}

	Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("playerId"), TEXT("command-performer"));
	Registry.Execute(TEXT("session.snapshot"), Params, Context, Result, Error);
	TestTrue(TEXT("snapshot succeeds"), Result.bSuccess);

	Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("playerId"), TEXT("command-unknown"));
	Registry.Execute(TEXT("session.snapshot"), Params, Context, Result, Error);
	TestFalse(TEXT("unknown player rejected"), Result.bSuccess);
	TestEqual(TEXT("unknown player code"), Result.ErrorCode, FString(TEXT("PLAYER_NOT_FOUND")));

	Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("requesterId"), TEXT("command-performer"));
	Params->SetStringField(TEXT("targetPlayerId"), OwnerId);
	Params->SetStringField(TEXT("role"), TEXT("observer"));
	Registry.Execute(TEXT("session.setRole"), Params, Context, Result, Error);
	TestFalse(TEXT("performer cannot change role"), Result.bSuccess);
	TestEqual(TEXT("role denial code"), Result.ErrorCode, FString(TEXT("FORBIDDEN")));

	Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("playerId"), TEXT("command-performer"));
	Registry.Execute(TEXT("session.export"), Params, Context, Result, Error);
	TestFalse(TEXT("performer cannot export"), Result.bSuccess);
	TestEqual(TEXT("export denial code"), Result.ErrorCode, FString(TEXT("FORBIDDEN")));

	Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("playerId"), OwnerId);
	Registry.Execute(TEXT("session.end"), Params, Context, Result, Error);
	TestTrue(TEXT("owner ends session"), Result.bSuccess);
	TestEqual(TEXT("ended session state"), Result.Result.IsValid() ? Result.Result->GetStringField(TEXT("state")) : FString(), FString(TEXT("ended")));

	CleanupScene(Root, SceneId);
	if (!SessionId.IsEmpty())
	{
		IFileManager::Get().DeleteDirectory(*FPaths::Combine(UEpicodeNetworkSettings::Get()->GetSessionRootPath(), SessionId), false, true);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeAPIAgentMethodsTest, "Epicode.API.AgentMethods", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeAPIAgentMethodsTest::RunTest(const FString& Parameters)
{
	UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
	TestNotNull(TEXT("network subsystem available"), Subsystem);
	if (!Subsystem)
	{
		return false;
	}

	FEpicodeCommandRegistry Registry;
	FEpicodeNetworkCommands::Register(Registry);
	const TArray<FString> AgentMethods = {
		TEXT("session.heartbeat"),
		TEXT("character.moveTo"),
		TEXT("character.lookAt"),
		TEXT("character.stop"),
		TEXT("character.state")
	};
	for (const FString& Method : AgentMethods)
	{
		TestTrue(FString::Printf(TEXT("registered %s"), *Method), Registry.Has(Method));
	}

	const TSharedPtr<FJsonObject> Directory = Registry.DescribeMethods();
	TestTrue(TEXT("method directory"), Directory.IsValid());
	if (Directory.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>> Methods = Directory->GetArrayField(TEXT("methods"));
		for (const FString& Name : AgentMethods)
		{
			bool bFound = false;
			for (const TSharedPtr<FJsonValue>& Value : Methods)
			{
				const TSharedPtr<FJsonObject> Method = Value->AsObject();
				if (!Method.IsValid() || Method->GetStringField(TEXT("name")) != Name)
				{
					continue;
				}
				bFound = true;
				TestEqual(FString::Printf(TEXT("%s participant kind"), *Name), Method->GetStringField(TEXT("requiredParticipantKind")), FString(TEXT("agent")));
				const bool bExpectedLease = Name != TEXT("character.state");
				TestEqual(FString::Printf(TEXT("%s lease requirement"), *Name), Method->HasTypedField<EJson::Boolean>(TEXT("requiresLease")), bExpectedLease);
			}
			TestTrue(FString::Printf(TEXT("directory contains %s"), *Name), bFound);
		}
	}

	FString SceneId;
	FString DocumentPath;
	const FString Root = MakeSceneFixture(SceneId, DocumentPath, true);
	TestFalse(TEXT("scene fixture created"), Root.IsEmpty());
	if (Root.IsEmpty())
	{
		return false;
	}

	FEpicodeCommandContext Context;
	FEpicodeCommandResult Result;
	FString Error;
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("sceneId"), SceneId);
	Params->SetStringField(TEXT("playerId"), TEXT("agent-owner"));
	Registry.Execute(TEXT("session.create"), Params, Context, Result, Error);
	TestTrue(TEXT("session created"), Result.bSuccess);

	Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("playerId"), TEXT("agent-api"));
	Params->SetStringField(TEXT("displayName"), TEXT("Agent API"));
	Params->SetStringField(TEXT("participantKind"), TEXT("agent"));
	Registry.Execute(TEXT("session.join"), Params, Context, Result, Error);
	TestTrue(TEXT("agent joined"), Result.bSuccess);
	FString ParticipantToken;
	if (Result.bSuccess && Result.Result.IsValid())
	{
		Result.Result->TryGetStringField(TEXT("participantToken"), ParticipantToken);
	}
	TestFalse(TEXT("participant token issued"), ParticipantToken.IsEmpty());

	FString CharacterId;
	FString CharacterMesh;
	FTransform CharacterTransform = FTransform::Identity;
	TestTrue(TEXT("free character"), Subsystem->GetNextFreeCharacter(CharacterId, CharacterMesh, CharacterTransform));

	Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("requesterId"), TEXT("agent-api"));
	Params->SetStringField(TEXT("targetPlayerId"), TEXT("agent-api"));
	Params->SetStringField(TEXT("characterId"), CharacterId);
	Registry.Execute(TEXT("session.assignCharacter"), Params, Context, Result, Error);
	TestTrue(TEXT("agent assigned"), Result.bSuccess);

	Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("playerId"), TEXT("agent-api"));
	Registry.Execute(TEXT("session.heartbeat"), Params, Context, Result, Error);
	TestTrue(TEXT("heartbeat succeeds"), Result.bSuccess);
	TestTrue(TEXT("heartbeat lease id"), Result.Result.IsValid() && Result.Result->HasField(TEXT("leaseId")));

	Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("playerId"), TEXT("agent-api"));
	Registry.Execute(TEXT("character.state"), Params, Context, Result, Error);
	TestTrue(TEXT("state succeeds"), Result.bSuccess);
	TestEqual(TEXT("state character"), Result.Result.IsValid() ? Result.Result->GetStringField(TEXT("characterId")) : FString(), CharacterId);
	TestTrue(TEXT("state lease"), Result.Result.IsValid() && Result.Result->HasField(TEXT("leaseId")));

	Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("playerId"), TEXT("agent-api"));
	Registry.Execute(TEXT("character.moveTo"), Params, Context, Result, Error);
	TestFalse(TEXT("move without target rejected"), Result.bSuccess);
	TestEqual(TEXT("move params code"), Result.ErrorCode, FString(TEXT("INVALID_PARAMS")));

	Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("requesterId"), TEXT("agent-api"));
	Registry.Execute(TEXT("session.releaseCharacter"), Params, Context, Result, Error);
	TestTrue(TEXT("agent released"), Result.bSuccess);

	Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("playerId"), TEXT("agent-owner"));
	Registry.Execute(TEXT("session.end"), Params, Context, Result, Error);
	TestTrue(TEXT("session ended"), Result.bSuccess);
	TestTrue(TEXT("ended session cleared"), Subsystem->ClearEndedSession(Error));
	CleanupScene(Root, SceneId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeAPIAgentErrorsTest, "Epicode.API.AgentErrors", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeAPIAgentErrorsTest::RunTest(const FString& Parameters)
{
	UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
	TestNotNull(TEXT("network subsystem available"), Subsystem);
	if (!Subsystem)
	{
		return false;
	}
	FString SceneId;
	FString DocumentPath;
	const FString Root = MakeSceneFixture(SceneId, DocumentPath, true);
	TestFalse(TEXT("scene fixture created"), Root.IsEmpty());
	if (Root.IsEmpty())
	{
		return false;
	}

	FEpicodeCommandRegistry Registry;
	FEpicodeNetworkCommands::Register(Registry);
	FEpicodeCommandContext Context;
	FEpicodeCommandResult Result;
	FString Error;
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("sceneId"), SceneId);
	Params->SetStringField(TEXT("playerId"), TEXT("error-owner"));
	Registry.Execute(TEXT("session.create"), Params, Context, Result, Error);
	TestTrue(TEXT("session created"), Result.bSuccess);

	Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("playerId"), TEXT("error-agent"));
	Params->SetStringField(TEXT("displayName"), TEXT("Error Agent"));
	Params->SetStringField(TEXT("participantKind"), TEXT("agent"));
	Registry.Execute(TEXT("session.join"), Params, Context, Result, Error);
	TestTrue(TEXT("agent joined"), Result.bSuccess);
	FString ParticipantToken;
	if (Result.Result.IsValid())
	{
		Result.Result->TryGetStringField(TEXT("participantToken"), ParticipantToken);
	}
	TestFalse(TEXT("token issued"), ParticipantToken.IsEmpty());

	Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("playerId"), TEXT("error-agent"));
	Registry.Execute(TEXT("character.stop"), Params, Context, Result, Error);
	TestFalse(TEXT("unassigned stop rejected"), Result.bSuccess);
	TestEqual(TEXT("unassigned code"), Result.ErrorCode, FString(TEXT("CHARACTER_NOT_ASSIGNED")));

	FString CharacterId;
	FString CharacterMesh;
	FTransform CharacterTransform = FTransform::Identity;
	TestTrue(TEXT("free character"), Subsystem->GetNextFreeCharacter(CharacterId, CharacterMesh, CharacterTransform));
	Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("requesterId"), TEXT("error-agent"));
	Params->SetStringField(TEXT("targetPlayerId"), TEXT("error-agent"));
	Params->SetStringField(TEXT("characterId"), CharacterId);
	Registry.Execute(TEXT("session.assignCharacter"), Params, Context, Result, Error);
	TestTrue(TEXT("agent assigned"), Result.bSuccess);

	Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("playerId"), TEXT("error-agent"));
	Registry.Execute(TEXT("character.stop"), Params, Context, Result, Error);
	TestFalse(TEXT("missing controller rejected"), Result.bSuccess);
	TestEqual(TEXT("controller code"), Result.ErrorCode, FString(TEXT("NOT_CONTROLLER")));

	FString LeaseId;
	double LeaseExpiresAt = 0.0;
	TestTrue(TEXT("short lease"), Subsystem->GetSession()->AcquireLease(TEXT("error-agent"), CharacterId, 1.0, LeaseId, LeaseExpiresAt, Error));
	FPlatformProcess::Sleep(1.2f);
	Registry.Execute(TEXT("character.lookAt"), [&]()
	{
		TSharedPtr<FJsonObject> Look = MakeShared<FJsonObject>();
		Look->SetStringField(TEXT("playerId"), TEXT("error-agent"));
		const TArray<TSharedPtr<FJsonValue>> Target = {
			MakeShared<FJsonValueNumber>(0.0),
			MakeShared<FJsonValueNumber>(0.0),
			MakeShared<FJsonValueNumber>(0.0)
		};
		Look->SetArrayField(TEXT("target"), Target);
		return Look;
	}(), Context, Result, Error);
	TestFalse(TEXT("expired lease rejected"), Result.bSuccess);
	TestEqual(TEXT("expired code"), Result.ErrorCode, FString(TEXT("LEASE_EXPIRED")));

	const int32 Limit = UEpicodeNetworkSettings::Get()->AgentCommandsPerSecond;
	for (int32 Index = 0; Index < Limit; ++Index)
	{
		TestTrue(FString::Printf(TEXT("rate call %d"), Index), Subsystem->CheckAgentCommandRate(TEXT("rate-agent"), Error));
	}
	TestFalse(TEXT("rate limited"), Subsystem->CheckAgentCommandRate(TEXT("rate-agent"), Error));
	TestTrue(TEXT("rate code"), Error.Contains(TEXT("RATE_LIMITED")));

	Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("playerId"), TEXT("error-owner"));
	Registry.Execute(TEXT("session.end"), Params, Context, Result, Error);
	Subsystem->ClearEndedSession(Error);
	CleanupScene(Root, SceneId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkSceneActorsTest, "Epicode.Network.SceneActors", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkSceneActorsTest::RunTest(const FString& Parameters)
{
	UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
	TestNotNull(TEXT("network subsystem"), Subsystem);
	if (!Subsystem)
	{
		return false;
	}
	FString SceneId;
	FString DocumentPath;
	const FString Root = MakeSceneFixture(SceneId, DocumentPath, true, true);
	TestFalse(TEXT("rich fixture"), Root.IsEmpty());
	if (Root.IsEmpty())
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, FName(*FString::Printf(TEXT("EpicodeSceneActors_%s"), *FGuid::NewGuid().ToString())));
	TestNotNull(TEXT("test world"), World);
	if (!World)
	{
		CleanupScene(Root, SceneId);
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	FString Error;
	TestTrue(TEXT("create session"), Subsystem->CreateSession(SceneId, TEXT("actor-owner"), TEXT("Owner"), 4, 30.0, Error, 2));
	int32 PropCount = 0;
	int32 LightCount = 0;
	int32 CameraCount = 0;
	for (TActorIterator<AEpicodePropActor> It(World); It; ++It)
	{
		++PropCount;
		TestTrue(TEXT("prop replicates"), It->GetIsReplicated());
	}
	for (TActorIterator<APointLight> It(World); It; ++It)
	{
		++LightCount;
		TestTrue(TEXT("light replicates"), It->GetIsReplicated());
	}
	for (TActorIterator<ACameraActor> It(World); It; ++It)
	{
		++CameraCount;
		TestTrue(TEXT("camera replicates"), It->GetIsReplicated());
	}
	TestEqual(TEXT("runtime prop actors"), PropCount, 1);
	TestEqual(TEXT("runtime light actors"), LightCount, 1);
	TestEqual(TEXT("runtime camera actors"), CameraCount, 1);
	int32 EnvironmentCount = 0;
	for (TActorIterator<AEpicodeRuntimeEnvironmentActor> It(World); It; ++It)
	{
		++EnvironmentCount;
		TestTrue(TEXT("environment actor replicates"), It->GetIsReplicated());
		TestTrue(TEXT("environment json has timeOfDay"), It->GetEnvironmentJson().Contains(TEXT("timeOfDay")));
	}
	TestEqual(TEXT("runtime environment actors"), EnvironmentCount, 1);
	Subsystem->EndSession(Error);
	Subsystem->ClearEndedSession(Error);
	int32 RemainingEnvironmentCount = 0;
	for (TActorIterator<AEpicodeRuntimeEnvironmentActor> It(World); It; ++It)
	{
		++RemainingEnvironmentCount;
	}
	TestEqual(TEXT("environment actor destroyed with session"), RemainingEnvironmentCount, 0);
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	CleanupScene(Root, SceneId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkWorldActorsTest, "Epicode.Network.WorldActors", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkWorldActorsTest::RunTest(const FString& Parameters)
{
	FString SceneId;
	FString DocumentPath;
	const FString Root = MakeSceneFixture(SceneId, DocumentPath, true, true);
	TestFalse(TEXT("rich fixture"), Root.IsEmpty());
	if (Root.IsEmpty())
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, FName(*FString::Printf(TEXT("EpicodeWorldActors_%s"), *FGuid::NewGuid().ToString())));
	TestNotNull(TEXT("test world"), World);
	if (!World)
	{
		CleanupScene(Root, SceneId);
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
	FString Error;
	TestTrue(TEXT("create session"), Subsystem->CreateSession(SceneId, TEXT("world-owner"), TEXT("Owner"), 4, 30.0, Error, 2));
	const int32 PropsBefore = [&]()
	{
		int32 Count = 0;
		for (TActorIterator<AEpicodePropActor> It(World); It; ++It)
		{
			++Count;
		}
		return Count;
	}();

	FString ItemId;
	TestTrue(TEXT("spawn item"), Subsystem->GetWorldState().SpawnItem(TEXT("crate"), TEXT("Crate"), FVector(200.0, 0.0, 50.0), 1.0, 1, ItemId, Error));
	Subsystem->SyncRuntimeWorldActors();
	int32 PropsAfterItem = 0;
	bool bItemReplicates = false;
	for (TActorIterator<AEpicodePropActor> It(World); It; ++It)
	{
		++PropsAfterItem;
		if (It->GetActorLocation().Y == 0.0 && It->GetActorLocation().X >= 199.0)
		{
			bItemReplicates = It->GetIsReplicated();
		}
	}
	TestEqual(TEXT("item actor spawned"), PropsAfterItem, PropsBefore + 1);
	TestTrue(TEXT("item actor replicates"), bItemReplicates);
	TestTrue(TEXT("pickup item"), Subsystem->GetWorldState().PickupItem(TEXT("world-owner"), ItemId, 0.0, 1000.0, Error));
	Subsystem->SyncRuntimeWorldActors();
	for (TActorIterator<AEpicodePropActor> It(World); It; ++It)
	{
		if (It->GetActorLocation().X >= 199.0 && It->GetActorLocation().Y == 0.0)
		{
			TestTrue(TEXT("held item actor hidden"), It->IsHidden());
		}
	}

	Subsystem->GetWorldState().GrantMaterials(TEXT("world-owner"), 4);
	FString BuildId;
	TestTrue(TEXT("confirm build"), Subsystem->GetWorldState().ConfirmBuild(TEXT("world-owner"), TEXT("wall"), FVector(0.0, 0.0, 0.0), FVector(400.0, 0.0, 50.0), FRotator::ZeroRotator, 1, 1000.0, BuildId, Error));
	Subsystem->SyncRuntimeWorldActors();
	int32 PropsAfterBuild = 0;
	bool bBuildReplicates = false;
	for (TActorIterator<AEpicodePropActor> It(World); It; ++It)
	{
		++PropsAfterBuild;
		if (It->GetActorLocation().X == 400.0)
		{
			bBuildReplicates = It->GetIsReplicated();
		}
	}
	TestEqual(TEXT("build actor spawned"), PropsAfterBuild, PropsAfterItem + 1);
	TestTrue(TEXT("build actor replicates"), bBuildReplicates);

	TestTrue(TEXT("undo build"), Subsystem->GetWorldState().UndoBuild(TEXT("world-owner"), BuildId, Error));
	Subsystem->SyncRuntimeWorldActors();
	int32 PropsAfterUndo = 0;
	for (TActorIterator<AEpicodePropActor> It(World); It; ++It)
	{
		++PropsAfterUndo;
	}
	TestEqual(TEXT("build actor removed"), PropsAfterUndo, PropsAfterItem);

	Subsystem->EndSession(Error);
	Subsystem->ClearEndedSession(Error);
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	CleanupScene(Root, SceneId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkEnvironmentReplicationTest, "Epicode.Network.EnvironmentReplication", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkEnvironmentReplicationTest::RunTest(const FString& Parameters)
{
	FString SceneId;
	FString DocumentPath;
	const FString Root = MakeSceneFixture(SceneId, DocumentPath, true, true);
	TestFalse(TEXT("rich fixture"), Root.IsEmpty());
	if (Root.IsEmpty())
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, FName(*FString::Printf(TEXT("EpicodeEnvironmentReplication_%s"), *FGuid::NewGuid().ToString())));
	TestNotNull(TEXT("test world"), World);
	if (!World)
	{
		CleanupScene(Root, SceneId);
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
	FString Error;
	TestTrue(TEXT("create session"), Subsystem->CreateSession(SceneId, TEXT("env-owner"), TEXT("Owner"), 4, 30.0, Error, 2));
	AEpicodeRuntimeEnvironmentActor* EnvironmentActor = nullptr;
	for (TActorIterator<AEpicodeRuntimeEnvironmentActor> It(World); It; ++It)
	{
		EnvironmentActor = *It;
	}
	TestNotNull(TEXT("environment actor"), EnvironmentActor);
	if (EnvironmentActor)
	{
		TestTrue(TEXT("environment replicates"), EnvironmentActor->GetIsReplicated());
		const FString DefaultJson = EnvironmentActor->GetEnvironmentJson();
		TestTrue(TEXT("default timeOfDay"), DefaultJson.Contains(TEXT("timeOfDay")));
		TestTrue(TEXT("default fog"), DefaultJson.Contains(TEXT("fogDensity")));
		TestTrue(TEXT("default clouds"), DefaultJson.Contains(TEXT("cloudCoverage")));
		TestTrue(TEXT("default exposure"), DefaultJson.Contains(TEXT("exposureCompensation")));
		TestTrue(TEXT("default preset"), DefaultJson.Contains(TEXT("preset")));
		TestTrue(TEXT("apply modified environment"), EnvironmentActor->ApplyEnvironment(Error));

		const FString Modified = TEXT("{\"timeOfDay\":6.5,\"fogDensity\":0.05,\"cloudCoverage\":0.2,\"exposureCompensation\":1.5,\"preset\":\"dawn\"}");
		EnvironmentActor->SetEnvironmentJson(Modified);
		TestEqual(TEXT("modified json retained"), EnvironmentActor->GetEnvironmentJson(), Modified);
	}
	Subsystem->EndSession(Error);
	Subsystem->ClearEndedSession(Error);
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	CleanupScene(Root, SceneId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkViewerLateJoinTest, "Epicode.Network.ViewerLateJoin", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkViewerLateJoinTest::RunTest(const FString& Parameters)
{
	FString SceneId;
	FString DocumentPath;
	const FString Root = MakeSceneFixture(SceneId, DocumentPath, true, true);
	TestFalse(TEXT("rich fixture"), Root.IsEmpty());
	if (Root.IsEmpty())
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, FName(*FString::Printf(TEXT("EpicodeViewerLateJoin_%s"), *FGuid::NewGuid().ToString())));
	TestNotNull(TEXT("test world"), World);
	if (!World)
	{
		CleanupScene(Root, SceneId);
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
	FString Error;
	TestTrue(TEXT("create session"), Subsystem->CreateSession(SceneId, TEXT("late-owner"), TEXT("Owner"), 4, 30.0, Error, 2));
	FString ItemId;
	TestTrue(TEXT("late item"), Subsystem->GetWorldState().SpawnItem(TEXT("crate"), TEXT("Late Crate"), FVector(10.0, 20.0, 30.0), 1.0, 1, ItemId, Error));
	Subsystem->GetWorldState().GrantMaterials(TEXT("late-owner"), 2);
	FString BuildId;
	TestTrue(TEXT("late build"), Subsystem->GetWorldState().ConfirmBuild(TEXT("late-owner"), TEXT("wall"), FVector::ZeroVector, FVector(100.0, 0.0, 0.0), FRotator::ZeroRotator, 1, 1000.0, BuildId, Error));
	Subsystem->SyncRuntimeWorldActors();
	const uint64 Version = Subsystem->GetSession()->GetWorldVersion();
	Subsystem->GetSession()->PublishEvent(TEXT("item.spawned"), TEXT("late-owner"), ItemId);

	UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get();
	TestNotNull(TEXT("engine subsystem"), EngineSubsystem);
	FEpicodeCommandResult Result;
	FEpicodeCommandContext Context;
	EngineSubsystem->GetCommandRegistry()->Execute(TEXT("session.snapshot"), MakeShared<FJsonObject>(), Context, Result, Error);
	TestTrue(TEXT("late snapshot succeeds"), Result.bSuccess);
	if (Result.bSuccess && Result.Result.IsValid())
	{
		TestTrue(TEXT("snapshot has items"), Result.Result->HasTypedField<EJson::Array>(TEXT("items")));
		TestEqual(TEXT("snapshot item count"), Result.Result->GetArrayField(TEXT("items")).Num(), 1);
		TestTrue(TEXT("snapshot has builds"), Result.Result->HasTypedField<EJson::Array>(TEXT("builds")));
		TestEqual(TEXT("snapshot build count"), Result.Result->GetArrayField(TEXT("builds")).Num(), 1);
		TestTrue(TEXT("snapshot has environment"), Result.Result->HasTypedField<EJson::Object>(TEXT("environment")));
		TestTrue(TEXT("snapshot has scene"), Result.Result->HasTypedField<EJson::Object>(TEXT("scene")));
		TestTrue(TEXT("snapshot has world version"), Result.Result->GetIntegerField(TEXT("worldVersion")) >= static_cast<int64>(Version));
	}

	bool bNeedsSnapshot = false;
	const TArray<FEpicodeNetworkEvent> Events = Subsystem->GetSession()->GetEventsSince(-1, 100, bNeedsSnapshot);
	TestTrue(TEXT("late cursor needs snapshot"), bNeedsSnapshot);
	TestTrue(TEXT("late snapshot events available"), Events.Num() > 0);

	Subsystem->EndSession(Error);
	Subsystem->ClearEndedSession(Error);
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	CleanupScene(Root, SceneId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkSceneSummaryTest, "Epicode.Network.SceneSummary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkSceneSummaryTest::RunTest(const FString& Parameters)
{
	FString SceneId;
	FString DocumentPath;
	const FString Root = MakeSceneFixture(SceneId, DocumentPath, true, true);
	TestFalse(TEXT("rich fixture created"), Root.IsEmpty());
	if (Root.IsEmpty())
	{
		return false;
	}
	FEpicodeNetworkSceneSummary Summary;
	FString Error;
	TestTrue(TEXT("load summary"), FEpicodeNetworkSceneLoader::Load(SceneId, Summary, Error));
	TestEqual(TEXT("characters"), Summary.Characters.Num(), 1);
	TestEqual(TEXT("props"), Summary.Props.Num(), 1);
	TestEqual(TEXT("lights"), Summary.Lights.Num(), 1);
	TestEqual(TEXT("cameras"), Summary.Cameras.Num(), 1);
	TestEqual(TEXT("entity count"), Summary.EntityCount, 4);
	TestTrue(TEXT("environment"), Summary.Environment.IsValid());
	if (Summary.Environment.IsValid())
	{
		TestEqual(TEXT("environment time"), Summary.Environment->GetNumberField(TEXT("timeOfDay")), 14.5);
	}
	TestTrue(TEXT("runtime metadata"), Summary.Runtime.IsValid());
	if (Summary.Runtime.IsValid())
	{
		const TSharedPtr<FJsonObject>* Viewer = nullptr;
		TestTrue(TEXT("viewer runtime"), Summary.Runtime->TryGetObjectField(TEXT("viewer"), Viewer) && Viewer && Viewer->IsValid());
		if (Viewer && Viewer->IsValid())
		{
			TestFalse(TEXT("default camera"), (*Viewer)->GetStringField(TEXT("defaultCameraId")).IsEmpty());
		}
	}
	const TSharedPtr<FJsonObject> Json = Summary.ToJson();
	TestEqual(TEXT("json props"), Json->GetArrayField(TEXT("props")).Num(), 1);
	TestEqual(TEXT("json lights"), Json->GetArrayField(TEXT("lights")).Num(), 1);
	TestEqual(TEXT("json cameras"), Json->GetArrayField(TEXT("cameras")).Num(), 1);
	TestTrue(TEXT("json environment"), Json->HasField(TEXT("environment")));
	TestTrue(TEXT("json runtime"), Json->HasField(TEXT("runtime")));
	if (Json->GetArrayField(TEXT("lights")).Num() == 1)
	{
		const TSharedPtr<FJsonObject> Light = Json->GetArrayField(TEXT("lights"))[0]->AsObject();
		TestEqual(TEXT("light type"), Light->GetStringField(TEXT("type")), FString(TEXT("light")));
		TestEqual(TEXT("light data type"), Light->GetObjectField(TEXT("data"))->GetStringField(TEXT("lightType")), FString(TEXT("point")));
	}
	CleanupScene(Root, SceneId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkAgentObservationTest, "Epicode.Network.AgentObservation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkAgentObservationTest::RunTest(const FString& Parameters)
{
	UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
	TestNotNull(TEXT("network subsystem"), Subsystem);
	if (!Subsystem)
	{
		return false;
	}
	FString SceneId;
	FString DocumentPath;
	const FString Root = MakeSceneFixture(SceneId, DocumentPath, true);
	FString Error;
	TestTrue(TEXT("create"), Subsystem->CreateSession(SceneId, TEXT("owner-observation"), TEXT("Owner"), 8, 30.0, Error, 4));
	FString Joined;
	TestTrue(TEXT("agent a"), Subsystem->JoinSession(TEXT("observation-a"), TEXT("Agent A"), EEpicodeSessionRole::Performer, Joined, Error, EEpicodeParticipantKind::Agent));
	TestTrue(TEXT("agent b"), Subsystem->JoinSession(TEXT("observation-b"), TEXT("Agent B"), EEpicodeSessionRole::Performer, Joined, Error, EEpicodeParticipantKind::Agent));
	FString CharacterId;
	FString MeshPath;
	FTransform SceneTransform = FTransform::Identity;
	TestTrue(TEXT("free character"), Subsystem->GetNextFreeCharacter(CharacterId, MeshPath, SceneTransform));
	TestTrue(TEXT("assign a"), Subsystem->AssignCharacter(TEXT("owner-observation"), TEXT("observation-a"), CharacterId, Error));
	FString LeaseId;
	double LeaseExpiresAt = 0.0;
	TestTrue(TEXT("lease a"), Subsystem->AcquireAgentLease(TEXT("observation-a"), CharacterId, LeaseId, LeaseExpiresAt, Error));

	const TSharedPtr<FJsonObject> StateA = Subsystem->GetAgentState(TEXT("observation-a"), Error);
	TestTrue(TEXT("state a"), StateA.IsValid());
	if (StateA.IsValid())
	{
		TestEqual(TEXT("state a character"), StateA->GetStringField(TEXT("characterId")), CharacterId);
		TestEqual(TEXT("state a kind"), StateA->GetStringField(TEXT("participantKind")), FString(TEXT("agent")));
		TestEqual(TEXT("state a lease"), StateA->GetStringField(TEXT("leaseId")), LeaseId);
		TestTrue(TEXT("state a version"), StateA->GetNumberField(TEXT("stateVersion")) > 0.0);
		TestTrue(TEXT("state a inventory"), StateA->HasField(TEXT("inventory")));
		if (StateA->HasField(TEXT("inventory")))
		{
			TestEqual(TEXT("inventory owner"), StateA->GetObjectField(TEXT("inventory"))->GetStringField(TEXT("playerId")), FString(TEXT("observation-a")));
		}
	}
	const TSharedPtr<FJsonObject> StateB = Subsystem->GetAgentState(TEXT("observation-b"), Error);
	TestTrue(TEXT("state b"), StateB.IsValid());
	if (StateB.IsValid())
	{
		TestTrue(TEXT("state b no character"), StateB->GetStringField(TEXT("characterId")).IsEmpty());
		TestTrue(TEXT("state b no lease"), StateB->GetStringField(TEXT("leaseId")).IsEmpty());
	}
	TestFalse(TEXT("unknown agent rejected"), Subsystem->GetAgentState(TEXT("missing-agent"), Error).IsValid());
	TestTrue(TEXT("unknown agent error"), Error.Contains(TEXT("PLAYER_NOT_FOUND")));
	Subsystem->EndSession(Error);
	Subsystem->ClearEndedSession(Error);
	CleanupScene(Root, SceneId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkAgentLeaseExpiryTest, "Epicode.Network.AgentLeaseExpiry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkAgentLeaseExpiryTest::RunTest(const FString& Parameters)
{
	FEpicodeNetworkSession Session;
	FString Error;
	TestTrue(TEXT("create"), Session.Create(TEXT("scene-agent-expiry"), TEXT("owner"), TEXT("Owner"), 4, 30.0, Error, 2));
	TestTrue(TEXT("start"), Session.Start(Error));
	FString Joined;
	TestTrue(TEXT("join"), Session.Join(TEXT("expiry-agent"), TEXT("Expiry"), EEpicodeSessionRole::Performer, Joined, Error, EEpicodeParticipantKind::Agent));
	FString LeaseId;
	double ExpiresAt = 0.0;
	TestTrue(TEXT("lease"), Session.AcquireLease(TEXT("expiry-agent"), TEXT("expiry-character"), 1.0, LeaseId, ExpiresAt, Error));
	Session.Tick(ExpiresAt + 1.0);
	TestTrue(TEXT("character cleared"), Session.FindPlayer(TEXT("expiry-agent"))->CharacterId.IsEmpty());
	TestTrue(TEXT("lease cleared"), Session.FindPlayer(TEXT("expiry-agent"))->LeaseId.IsEmpty());
	TestTrue(TEXT("reacquire after expiry"), Session.AcquireLease(TEXT("expiry-agent"), TEXT("expiry-character"), 30.0, LeaseId, ExpiresAt, Error));
	TestTrue(TEXT("leave"), Session.Leave(TEXT("expiry-agent"), Error));
	TestTrue(TEXT("reconnect"), Session.Reconnect(TEXT("expiry-agent"), Error));
	TestEqual(TEXT("kind preserved"), LexToString(Session.FindPlayer(TEXT("expiry-agent"))->ParticipantKind), FString(TEXT("agent")));
	TestTrue(TEXT("reacquire after reconnect"), Session.AcquireLease(TEXT("expiry-agent"), TEXT("expiry-character"), 30.0, LeaseId, ExpiresAt, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkAgentLimitsTest, "Epicode.Network.AgentLimits", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkAgentLimitsTest::RunTest(const FString& Parameters)
{
	UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
	TestNotNull(TEXT("network subsystem"), Subsystem);
	if (!Subsystem)
	{
		return false;
	}
	FString Error;
	const int32 Limit = UEpicodeNetworkSettings::Get()->AgentCommandsPerSecond;
	for (int32 Index = 0; Index < Limit; ++Index)
	{
		TestTrue(FString::Printf(TEXT("command %d allowed"), Index), Subsystem->CheckAgentCommandRate(TEXT("limits-agent"), Error));
	}
	TestFalse(TEXT("over limit rejected"), Subsystem->CheckAgentCommandRate(TEXT("limits-agent"), Error));
	TestTrue(TEXT("rate error"), Error.Contains(TEXT("RATE_LIMITED")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkViewerLifecycleTest, "Epicode.Network.ViewerLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkViewerLifecycleTest::RunTest(const FString& Parameters)
{
	FEpicodeNetworkSession Session;
	FString Error;
	FString Joined;
	TestTrue(TEXT("create"), Session.Create(TEXT("scene-viewer-lifecycle"), TEXT("owner"), TEXT("Owner"), 2, 30.0, Error, 2));
	TestTrue(TEXT("start"), Session.Start(Error));
	TestTrue(TEXT("join viewer"), Session.Join(TEXT("viewer-life"), TEXT("Viewer"), EEpicodeSessionRole::Observer, Joined, Error, EEpicodeParticipantKind::Viewer));
	TestTrue(TEXT("leave viewer"), Session.Leave(TEXT("viewer-life"), Error));
	TestFalse(TEXT("viewer disconnected"), Session.FindPlayer(TEXT("viewer-life"))->bConnected);
	TestEqual(TEXT("viewer slots after leave"), static_cast<int32>(Session.ToJson()->GetNumberField(TEXT("viewerCount"))), 0);
	TestEqual(TEXT("viewer camera summaries after leave"), Session.ToJson()->GetArrayField(TEXT("viewerCameras")).Num(), 0);
	TestTrue(TEXT("reconnect viewer"), Session.Reconnect(TEXT("viewer-life"), Error));
	TestTrue(TEXT("viewer connected"), Session.FindPlayer(TEXT("viewer-life"))->bConnected);
	TestEqual(TEXT("kind after reconnect"), LexToString(Session.FindPlayer(TEXT("viewer-life"))->ParticipantKind), FString(TEXT("viewer")));
	TestTrue(TEXT("viewer still has no character"), Session.FindPlayer(TEXT("viewer-life"))->CharacterId.IsEmpty());
	TestEqual(TEXT("viewer count after reconnect"), static_cast<int32>(Session.ToJson()->GetNumberField(TEXT("viewerCount"))), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkViewerReadOnlyTest, "Epicode.Network.ViewerReadOnly", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkViewerReadOnlyTest::RunTest(const FString& Parameters)
{
	FEpicodeNetworkSession Session;
	FString Error;
	FString Joined;
	TestTrue(TEXT("create"), Session.Create(TEXT("scene-viewer-readonly"), TEXT("owner"), TEXT("Owner"), 2, 30.0, Error, 2));
	TestTrue(TEXT("start"), Session.Start(Error));
	TestTrue(TEXT("join viewer"), Session.Join(TEXT("viewer-readonly"), TEXT("Viewer"), EEpicodeSessionRole::Observer, Joined, Error, EEpicodeParticipantKind::Viewer));
	TestFalse(TEXT("viewer cannot assign"), Session.AssignCharacter(TEXT("viewer-readonly"), TEXT("viewer-readonly"), TEXT("character-x"), Error));
	TestTrue(TEXT("viewer read only error"), Error.Contains(TEXT("VIEWER_READ_ONLY")));
	TestFalse(TEXT("viewer cannot manage"), Session.CanPerform(TEXT("viewer-readonly"), EEpicodeSessionAction::ManageSession, Error));
	TestFalse(TEXT("viewer cannot control"), Session.CanControlCharacter(TEXT("viewer-readonly"), TEXT("character-x"), Error));
	TestEqual(TEXT("viewer has no character"), Session.FindPlayer(TEXT("viewer-readonly"))->CharacterId, FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkMixedParticipantsTest, "Epicode.Network.MixedParticipants", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkMixedParticipantsTest::RunTest(const FString& Parameters)
{
	FEpicodeNetworkSession Session;
	FString Error;
	FString Joined;
	TestTrue(TEXT("create"), Session.Create(TEXT("scene-mixed"), TEXT("owner"), TEXT("Owner"), 3, 30.0, Error, 2));
	TestTrue(TEXT("start"), Session.Start(Error));
	TestTrue(TEXT("join performer"), Session.Join(TEXT("mixed-performer"), TEXT("Performer"), EEpicodeSessionRole::Performer, Joined, Error, EEpicodeParticipantKind::Performer));
	TestTrue(TEXT("join agent"), Session.Join(TEXT("mixed-agent"), TEXT("Agent"), EEpicodeSessionRole::Performer, Joined, Error, EEpicodeParticipantKind::Agent));
	TestTrue(TEXT("join viewer"), Session.Join(TEXT("mixed-viewer"), TEXT("Viewer"), EEpicodeSessionRole::Observer, Joined, Error, EEpicodeParticipantKind::Viewer));
	const TSharedPtr<FJsonObject> Snapshot = Session.ToJson();
	TestEqual(TEXT("performer summary"), static_cast<int32>(Snapshot->GetNumberField(TEXT("performerCount"))), 3);
	TestEqual(TEXT("viewer summary"), static_cast<int32>(Snapshot->GetNumberField(TEXT("viewerCount"))), 1);
	TestEqual(TEXT("player summary"), Snapshot->GetArrayField(TEXT("players")).Num(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeViewerSceneCamerasTest, "Epicode.Viewer.SceneCameras", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeViewerSceneCamerasTest::RunTest(const FString& Parameters)
{
	FString SceneId;
	FString DocumentPath;
	const FString Root = MakeSceneFixture(SceneId, DocumentPath, true, true);
	TestFalse(TEXT("rich fixture"), Root.IsEmpty());
	if (Root.IsEmpty())
	{
		return false;
	}
	FEpicodeNetworkSceneSummary Summary;
	FString Error;
	TestTrue(TEXT("load"), FEpicodeNetworkSceneLoader::Load(SceneId, Summary, Error));
	TestEqual(TEXT("camera count"), Summary.Cameras.Num(), 1);
	TestTrue(TEXT("camera transform"), Summary.Cameras.Num() == 1 && Summary.Cameras[0].Transform.Location.Z > 0.0);
	TestTrue(TEXT("runtime viewer"), Summary.Runtime.IsValid());
	if (Summary.Runtime.IsValid())
	{
		const TSharedPtr<FJsonObject>* Viewer = nullptr;
		TestTrue(TEXT("viewer runtime"), Summary.Runtime->TryGetObjectField(TEXT("viewer"), Viewer) && Viewer && Viewer->IsValid());
		if (Viewer && Viewer->IsValid())
		{
			TestFalse(TEXT("default camera configured"), (*Viewer)->GetStringField(TEXT("defaultCameraId")).IsEmpty());
			TestEqual(TEXT("camera ids"), (*Viewer)->GetArrayField(TEXT("cameraIds")).Num(), 1);
		}
	}
	CleanupScene(Root, SceneId);

	FString FallbackSceneId;
	FString FallbackDocumentPath;
	const FString FallbackRoot = MakeSceneFixture(FallbackSceneId, FallbackDocumentPath, false);
	if (!FallbackRoot.IsEmpty())
	{
		FEpicodeNetworkSceneSummary FallbackSummary;
		TestTrue(TEXT("fallback load"), FEpicodeNetworkSceneLoader::Load(FallbackSceneId, FallbackSummary, Error));
		TestEqual(TEXT("fallback cameras empty"), FallbackSummary.Cameras.Num(), 0);
		CleanupScene(FallbackRoot, FallbackSceneId);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkHealthTest, "Epicode.Network.Health", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkHealthTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> ProviderStatus = FEpicodeDiagnostics::GetNetworkStatus();
	TestNotNull(TEXT("network health provider registered"), ProviderStatus.Get());
	if (ProviderStatus.IsValid())
	{
		TestEqual(TEXT("provider module"), ProviderStatus->GetStringField(TEXT("module")), FString(TEXT("EpicodeNetwork")));
		TestTrue(TEXT("provider online subsystem"), ProviderStatus->HasField(TEXT("onlineSubsystem")));
		TestTrue(TEXT("provider module loaded"), ProviderStatus->GetBoolField(TEXT("moduleLoaded")));
	}

	UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get();
	TestNotNull(TEXT("engine subsystem available"), EngineSubsystem);
	if (!EngineSubsystem || !EngineSubsystem->GetCommandRegistry())
	{
		return false;
	}
	FEpicodeCommandContext Context;
	FEpicodeCommandResult Result;
	FString Error;
	EngineSubsystem->GetCommandRegistry()->Execute(TEXT("system.health"), MakeShared<FJsonObject>(), Context, Result, Error);
	TestTrue(TEXT("system.health succeeds"), Result.bSuccess);
	if (!Result.bSuccess || !Result.Result.IsValid())
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* Network = nullptr;
	TestTrue(TEXT("system.health includes network"), Result.Result->TryGetObjectField(TEXT("network"), Network) && Network && Network->IsValid());
	if (Network && Network->IsValid())
	{
		TestTrue(TEXT("health network module loaded"), (*Network)->GetBoolField(TEXT("moduleLoaded")));
		TestEqual(TEXT("health online subsystem"), (*Network)->GetStringField(TEXT("onlineSubsystem")), FString(TEXT("LAN")));
		TestTrue(TEXT("health network port"), (*Network)->GetIntegerField(TEXT("port")) > 0);
		TestTrue(TEXT("health server mode"), (*Network)->HasField(TEXT("serverMode")));
		TestTrue(TEXT("health server target support"), (*Network)->HasField(TEXT("serverTargetSupported")));
		TestTrue(TEXT("health fallback reason"), (*Network)->HasField(TEXT("fallbackReason")));
		TestTrue(TEXT("health server binary"), (*Network)->HasField(TEXT("serverBinary")));
		TestTrue(TEXT("health client mode"), (*Network)->HasField(TEXT("clientMode")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkServerFallbackTest, "Epicode.Network.ServerFallback", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkServerFallbackTest::RunTest(const FString& Parameters)
{
	const TCHAR* Names[] = {
		TEXT("EPICODE_SERVER_MODE"),
		TEXT("EPICODE_SERVER_TARGET_SUPPORTED"),
		TEXT("EPICODE_SERVER_FALLBACK_REASON"),
		TEXT("EPICODE_SERVER_BINARY"),
		TEXT("EPICODE_CLIENT_MODE")
	};
	TMap<FString, FString> Previous;
	for (const TCHAR* Name : Names)
	{
		Previous.Add(Name, FPlatformMisc::GetEnvironmentVariable(Name));
	}
	auto Restore = [&Previous, &Names]()
	{
		for (const TCHAR* Name : Names)
		{
			FPlatformMisc::SetEnvironmentVar(Name, *Previous.FindRef(Name));
		}
	};

	TestEqual(TEXT("server command resolves to editor fallback"),
		FEpicodeRuntimeMode::ResolveServerMode(TEXT("-server"), FString(), FString()),
		FString(TEXT("editor-fallback")));
	TestEqual(TEXT("explicit server target wins"),
		FEpicodeRuntimeMode::ResolveServerMode(TEXT("-server"), TEXT("server-target"), FString()),
		FString(TEXT("server-target")));
	TestEqual(TEXT("no server flag resolves to none"),
		FEpicodeRuntimeMode::ResolveServerMode(TEXT(""), FString(), FString()),
		FString(TEXT("none")));
	TestTrue(TEXT("bool true"), FEpicodeRuntimeMode::ParseBoolEnvironment(TEXT("true"), false));
	TestTrue(TEXT("bool one"), FEpicodeRuntimeMode::ParseBoolEnvironment(TEXT("1"), false));
	TestFalse(TEXT("bool false"), FEpicodeRuntimeMode::ParseBoolEnvironment(TEXT("false"), true));
	TestFalse(TEXT("bool zero"), FEpicodeRuntimeMode::ParseBoolEnvironment(TEXT("0"), true));
	TestTrue(TEXT("bool default"), FEpicodeRuntimeMode::ParseBoolEnvironment(TEXT("unknown"), true));

	FPlatformMisc::SetEnvironmentVar(TEXT("EPICODE_SERVER_MODE"), TEXT("server-target"));
	FPlatformMisc::SetEnvironmentVar(TEXT("EPICODE_SERVER_TARGET_SUPPORTED"), TEXT("1"));
	FPlatformMisc::SetEnvironmentVar(TEXT("EPICODE_SERVER_FALLBACK_REASON"), TEXT(""));
	FPlatformMisc::SetEnvironmentVar(TEXT("EPICODE_SERVER_BINARY"), TEXT("/tmp/EpicodeVideoServer"));
	FPlatformMisc::SetEnvironmentVar(TEXT("EPICODE_CLIENT_MODE"), TEXT("game-binary"));
	FEpicodeRuntimeMode TargetMode = FEpicodeRuntimeMode::Resolve();
	TestEqual(TEXT("target mode"), TargetMode.ServerMode, FString(TEXT("server-target")));
	TestTrue(TEXT("target supported"), TargetMode.bServerTargetSupported);
	TestEqual(TEXT("target binary"), TargetMode.ServerBinary, FString(TEXT("/tmp/EpicodeVideoServer")));
	TestEqual(TEXT("client mode"), TargetMode.ClientMode, FString(TEXT("game-binary")));

	FPlatformMisc::SetEnvironmentVar(TEXT("EPICODE_SERVER_MODE"), TEXT("editor-fallback"));
	FPlatformMisc::SetEnvironmentVar(TEXT("EPICODE_SERVER_TARGET_SUPPORTED"), TEXT("0"));
	FPlatformMisc::SetEnvironmentVar(TEXT("EPICODE_SERVER_FALLBACK_REASON"), TEXT("Server targets are not supported by this engine distribution."));
	FPlatformMisc::SetEnvironmentVar(TEXT("EPICODE_SERVER_BINARY"), TEXT("/usr/local/unreal/Engine/Binaries/Linux/UnrealEditor-Cmd"));
	FPlatformMisc::SetEnvironmentVar(TEXT("EPICODE_CLIENT_MODE"), TEXT("editor-game"));
	FEpicodeRuntimeMode FallbackMode = FEpicodeRuntimeMode::Resolve();
	TestEqual(TEXT("fallback mode"), FallbackMode.ServerMode, FString(TEXT("editor-fallback")));
	TestFalse(TEXT("fallback target unsupported"), FallbackMode.bServerTargetSupported);
	TestTrue(TEXT("fallback reason"), FallbackMode.ServerFallbackReason.Contains(TEXT("not supported")));
	TestEqual(TEXT("fallback binary"), FallbackMode.ServerBinary,
		FString(TEXT("/usr/local/unreal/Engine/Binaries/Linux/UnrealEditor-Cmd")));
	TestEqual(TEXT("fallback client mode"), FallbackMode.ClientMode, FString(TEXT("editor-game")));
	const TSharedPtr<FJsonObject> ModeJson = FallbackMode.ToJson();
	TestEqual(TEXT("mode JSON server mode"), ModeJson->GetStringField(TEXT("serverMode")), FString(TEXT("editor-fallback")));
	TestFalse(TEXT("mode JSON support"), ModeJson->GetBoolField(TEXT("serverTargetSupported")));
	TestTrue(TEXT("mode JSON reason"), ModeJson->GetStringField(TEXT("fallbackReason")).Contains(TEXT("not supported")));
	TestTrue(TEXT("mode JSON client mode"), ModeJson->HasField(TEXT("clientMode")));

	Restore();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkLevelTravelTest, "Epicode.Network.LevelTravel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkLevelTravelTest::RunTest(const FString& Parameters)
{
	FString SceneId;
	FString DocumentPath;
	const FString Root = MakeSceneFixture(SceneId, DocumentPath, true, true);
	TestFalse(TEXT("rich fixture"), Root.IsEmpty());
	if (Root.IsEmpty())
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, FName(*FString::Printf(TEXT("EpicodeLevelTravel_%s"), *FGuid::NewGuid().ToString())));
	TestNotNull(TEXT("test world"), World);
	if (!World)
	{
		CleanupScene(Root, SceneId);
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	TestFalse(TEXT("empty level path does not travel"), UEpicodeNetworkSubsystem::ShouldTravelToLevel(World, FString()));
	TestTrue(TEXT("different existing level travels"), UEpicodeNetworkSubsystem::ShouldTravelToLevel(World, TEXT("/Engine/Maps/Entry")));
	TestFalse(TEXT("same level does not travel"), UEpicodeNetworkSubsystem::ShouldTravelToLevel(World, TEXT("/Game/Maps/") + World->GetMapName()));
	TestFalse(TEXT("missing level does not travel"), UEpicodeNetworkSubsystem::ShouldTravelToLevel(World, TEXT("/Game/Missing/MissingLevel")));

	UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
	FString Error;
	TestTrue(TEXT("create session"), Subsystem->CreateSession(SceneId, TEXT("travel-owner"), TEXT("Owner"), 4, 30.0, Error, 2));
	TestTrue(TEXT("active session"), Subsystem->HasActiveSession());
	FEpicodeNetworkSession* Session = Subsystem->GetSession();
	Subsystem->AttachToWorld();
	TestTrue(TEXT("attach keeps session"), Subsystem->GetSession() == Session);
	TestTrue(TEXT("attach keeps active"), Subsystem->HasActiveSession());
	Subsystem->EndSession(Error);
	Subsystem->ClearEndedSession(Error);
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	CleanupScene(Root, SceneId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeFoundationProcessRolesTest, "Epicode.Foundation.ProcessRoles", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeFoundationProcessRolesTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("editor role"), FEpicodeNetworkModule::ResolveProcessRole(TEXT("UnrealEditor"), true), FString(TEXT("Editor")));
	TestEqual(TEXT("headless role"), FEpicodeNetworkModule::ResolveProcessRole(TEXT("UnrealEditor -game -nullrhi"), false), FString(TEXT("Headless")));
	TestEqual(TEXT("server role"), FEpicodeNetworkModule::ResolveProcessRole(TEXT("UnrealEditor -server -nullrhi"), false), FString(TEXT("Server")));
	TestEqual(TEXT("viewer role"), FEpicodeNetworkModule::ResolveProcessRole(TEXT("UnrealEditor -game -EpicodeViewer"), false), FString(TEXT("Viewer")));
	TestEqual(TEXT("viewer value role"), FEpicodeNetworkModule::ResolveProcessRole(TEXT("UnrealEditor EpicodeViewer=1"), true), FString(TEXT("Viewer")));
	TestEqual(TEXT("agent role"), FEpicodeNetworkModule::ResolveProcessRole(TEXT("UnrealEditor -EpicodeAgent"), false), FString(TEXT("Agent")));
	TestEqual(TEXT("director role"), FEpicodeNetworkModule::ResolveProcessRole(TEXT("UnrealEditor -EpicodeDirector"), false), FString(TEXT("Director")));
	TestEqual(TEXT("replay role"), FEpicodeNetworkModule::ResolveProcessRole(TEXT("UnrealEditor -EpicodeReplay"), false), FString(TEXT("Replay")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkRuntimeDefaultsTest, "Epicode.Network.RuntimeDefaults", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkRuntimeDefaultsTest::RunTest(const FString& Parameters)
{
	const AEpicodeNetworkCharacter* Character = GetDefault<AEpicodeNetworkCharacter>();
	TestNotNull(TEXT("character CDO"), Character);
	if (!Character)
	{
		return false;
	}
	TestTrue(TEXT("character replicates"), Character->GetIsReplicated());
	TestTrue(TEXT("net update frequency"), Character->GetNetUpdateFrequency() >= 20.0f);
	TestFalse(TEXT("controller yaw disabled"), Character->bUseControllerRotationYaw);
	TestFalse(TEXT("empty appearance rejected"), const_cast<AEpicodeNetworkCharacter*>(Character)->ApplyAppearance(FString()));
	TestTrue(TEXT("scene spawn location offset"), AEpicodeNetworkCharacter::MakeCapsuleSpawnLocation(FVector(100.0, 200.0, 0.0), 88.0).Equals(FVector(100.0, 200.0, 88.0)));
	TestTrue(TEXT("scene spawn negative offset clamped"), AEpicodeNetworkCharacter::MakeCapsuleSpawnLocation(FVector(0.0, 0.0, 0.0), -10.0).Equals(FVector::ZeroVector));
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	TestNotNull(TEXT("character movement"), Movement);
	if (Movement)
	{
		TestEqual(TEXT("walk speed"), Movement->MaxWalkSpeed, 400.0f);
		TestTrue(TEXT("orient to movement"), Movement->bOrientRotationToMovement);
		TestTrue(TEXT("exponential smoothing"), Movement->NetworkSmoothingMode == ENetworkSmoothingMode::Exponential);
		TestFalse(TEXT("root motion rotation disabled"), Movement->bAllowPhysicsRotationDuringAnimRootMotion);
	}

	TestTrue(TEXT("teleport at origin"), AEpicodeNetworkCharacter::IsTeleportDistanceAllowed(0.0, 500.0));
	TestTrue(TEXT("teleport at limit"), AEpicodeNetworkCharacter::IsTeleportDistanceAllowed(500.0, 500.0));
	TestFalse(TEXT("teleport over limit"), AEpicodeNetworkCharacter::IsTeleportDistanceAllowed(500.1, 500.0));
	TestFalse(TEXT("negative teleport distance"), AEpicodeNetworkCharacter::IsTeleportDistanceAllowed(-1.0, 500.0));
	TestTrue(TEXT("move inside speed budget"), AEpicodeNetworkCharacter::IsMoveDistanceAllowed(0.1, 40.0, 400.0, 1.15));
	TestFalse(TEXT("move over speed budget"), AEpicodeNetworkCharacter::IsMoveDistanceAllowed(0.1, 80.0, 400.0, 1.15));
	TestFalse(TEXT("negative move delta"), AEpicodeNetworkCharacter::IsMoveDistanceAllowed(-0.1, 1.0, 400.0));
	TestFalse(TEXT("zero max speed"), AEpicodeNetworkCharacter::IsMoveDistanceAllowed(0.1, 1.0, 0.0));
	TestTrue(TEXT("small client drift accepted"),
		AEpicodeNetworkCharacter::ComputeAuthoritativeCorrection(FVector::ZeroVector, FVector(10.0, 0.0, 0.0), 50.0).Equals(FVector(10.0, 0.0, 0.0)));
	TestTrue(TEXT("large client drift corrected"),
		AEpicodeNetworkCharacter::ComputeAuthoritativeCorrection(FVector::ZeroVector, FVector(60.0, 0.0, 0.0), 50.0).Equals(FVector::ZeroVector));
	TestTrue(TEXT("negative correction threshold always corrects"),
		AEpicodeNetworkCharacter::ComputeAuthoritativeCorrection(FVector(1.0, 2.0, 3.0), FVector(1.0, 2.0, 3.0), -1.0).Equals(FVector(1.0, 2.0, 3.0)));

	TestEqual(TEXT("safe player id spaces"), AEpicodeNetworkPlayerController::MakeSafePlayerId(TEXT("  Alice Smith  ")), FString(TEXT("Alice-Smith")));
	TestEqual(TEXT("safe player id slashes"), AEpicodeNetworkPlayerController::MakeSafePlayerId(TEXT("a/b\\c:d")), FString(TEXT("a-b-c-d")));
	TestEqual(TEXT("empty player id fallback"), AEpicodeNetworkPlayerController::MakeSafePlayerId(TEXT("   ")), FString(TEXT("Player")));
	FString LongName;
	for (int32 Index = 0; Index < 200; ++Index)
	{
		LongName.AppendChar(TEXT('a'));
	}
	TestTrue(TEXT("player id length capped"), AEpicodeNetworkPlayerController::MakeSafePlayerId(LongName).Len() <= 64);

	const AEpicodeNetworkGameMode* GameMode = GetDefault<AEpicodeNetworkGameMode>();
	TestNotNull(TEXT("game mode CDO"), GameMode);
	if (GameMode)
	{
		TestTrue(TEXT("default pawn class"), GameMode->DefaultPawnClass == AEpicodeNetworkCharacter::StaticClass());
		TestTrue(TEXT("player controller class"), GameMode->PlayerControllerClass == AEpicodeNetworkPlayerController::StaticClass());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkSettingsTest, "Epicode.Network.Settings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkSettingsTest::RunTest(const FString& Parameters)
{
	const UEpicodeNetworkSettings* Settings = UEpicodeNetworkSettings::Get();
	TestNotNull(TEXT("network settings"), Settings);
	if (!Settings)
	{
		return false;
	}
	TestTrue(TEXT("max viewers"), Settings->MaxViewers > 0);
	TestTrue(TEXT("agent lease"), Settings->AgentLeaseSeconds > 0.0);
	TestTrue(TEXT("agent heartbeat"), Settings->AgentHeartbeatSeconds > 0.0);
	TestTrue(TEXT("agent command timeout"), Settings->AgentCommandTimeoutSeconds > 0.0);
	TestFalse(TEXT("remote viewers default off"), Settings->bAllowRemoteViewers);
	TestFalse(TEXT("remote agents default off"), Settings->bAllowRemoteAgents);
	TestFalse(TEXT("viewer token file configured"), Settings->ViewerTokenFile.IsEmpty());

	FString Error;
	TestTrue(TEXT("viewer token ensured"), Settings->EnsureViewerToken(Error));
	const FString TokenPath = Settings->GetViewerTokenFilePath();
	TestTrue(TEXT("viewer token file exists"), IFileManager::Get().FileExists(*TokenPath));
	TestFalse(TEXT("viewer token readable"), Settings->GetViewerToken().IsEmpty());
#if PLATFORM_LINUX
	struct stat FileStat;
	if (stat(TCHAR_TO_UTF8(*TokenPath), &FileStat) == 0)
	{
		TestEqual(TEXT("viewer token mode"), static_cast<int32>(FileStat.st_mode & 0777), 0600);
	}
	else
	{
		AddError(TEXT("Unable to stat viewer token file."));
	}
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkMovementTest, "Epicode.Network.Movement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkMovementTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("predicted move accepted"), AEpicodeNetworkCharacter::IsMoveDistanceAllowed(0.05, 10.0, 400.0));
	TestTrue(TEXT("tolerance accepted"), AEpicodeNetworkCharacter::IsMoveDistanceAllowed(0.05, 22.0, 400.0));
	TestFalse(TEXT("predicted move rejected"), AEpicodeNetworkCharacter::IsMoveDistanceAllowed(0.05, 30.0, 400.0));
	TestFalse(TEXT("teleport beyond limit rejected"), AEpicodeNetworkCharacter::IsTeleportDistanceAllowed(600.0, 500.0));
	TestTrue(TEXT("teleport inside limit allowed"), AEpicodeNetworkCharacter::IsTeleportDistanceAllowed(499.0, 500.0));
	TestTrue(TEXT("correction keeps server authority"),
		AEpicodeNetworkCharacter::ComputeAuthoritativeCorrection(FVector(100.0, 0.0, 0.0), FVector(130.0, 0.0, 0.0), 20.0).Equals(FVector(100.0, 0.0, 0.0)));
	TestTrue(TEXT("prediction kept under threshold"),
		AEpicodeNetworkCharacter::ComputeAuthoritativeCorrection(FVector(100.0, 0.0, 0.0), FVector(110.0, 0.0, 0.0), 20.0).Equals(FVector(110.0, 0.0, 0.0)));
	const AEpicodeNetworkCharacter* Character = GetDefault<AEpicodeNetworkCharacter>();
	TestNotNull(TEXT("movement character CDO"), Character);
	if (Character)
	{
		TestEqual(TEXT("authoritative walk speed"), Character->GetCharacterMovement()->MaxWalkSpeed, 400.0f);
		TestTrue(TEXT("movement replicated"), Character->GetCharacterMovement()->GetIsReplicated());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkInterpolationTest, "Epicode.Network.Interpolation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkInterpolationTest::RunTest(const FString& Parameters)
{
	const AEpicodeNetworkCharacter* Character = GetDefault<AEpicodeNetworkCharacter>();
	TestNotNull(TEXT("interpolation character CDO"), Character);
	if (!Character)
	{
		return false;
	}
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	TestEqual(TEXT("exponential smoothing"), Movement->NetworkSmoothingMode, ENetworkSmoothingMode::Exponential);
	TestTrue(TEXT("simulated location smoothing"), Movement->NetworkSimulatedSmoothLocationTime > 0.0f);
	TestTrue(TEXT("simulated rotation smoothing"), Movement->NetworkSimulatedSmoothRotationTime > 0.0f);
	TestFalse(TEXT("anim root motion does not drive physics"), Movement->bAllowPhysicsRotationDuringAnimRootMotion);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkReplicationPriorityTest, "Epicode.Network.ReplicationPriority", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkReplicationPriorityTest::RunTest(const FString& Parameters)
{
	const AEpicodeNetworkCharacter* Character = GetDefault<AEpicodeNetworkCharacter>();
	TestNotNull(TEXT("replication character CDO"), Character);
	if (!Character)
	{
		return false;
	}
	TestEqual(TEXT("character net priority"), Character->NetPriority, 2.0f);
	TestEqual(TEXT("character update frequency"), Character->GetNetUpdateFrequency(), 30.0f);
	TestEqual(TEXT("character minimum update frequency"), Character->GetMinNetUpdateFrequency(), 10.0f);
	TestTrue(TEXT("character cull distance configured"), Character->NetCullDistanceSquared > 0.0f);
	TestFalse(TEXT("character not always relevant"), Character->bAlwaysRelevant);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkPerformanceStateTest, "Epicode.Network.PerformanceState", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkPerformanceStateTest::RunTest(const FString& Parameters)
{
	FEpicodePerformanceState State;
	TestTrue(TEXT("default action empty"), State.ActionTag.IsEmpty());
	TestEqual(TEXT("default emotion"), State.Emotion, FString(TEXT("neutral")));
	TestEqual(TEXT("default intensity"), State.ExpressionIntensity, 0.0f);
	TestFalse(TEXT("default speaking"), State.bSpeaking);
	TestTrue(TEXT("default viseme none"), State.VisemeSource == EEpicodeVisemeSource::None);
	TestFalse(TEXT("default motion matching"), State.bMotionMatchingActive);

	State.ActionTag = TEXT("wave");
	State.ActionQueue.Add(TEXT("bow"));
	State.Emotion = TEXT("happy");
	State.ExpressionIntensity = 0.75f;
	State.bSpeaking = true;
	State.VisemeSource = EEpicodeVisemeSource::LiveLink;

	const TSharedPtr<FJsonObject> Json = State.ToJson();
	TestEqual(TEXT("serialized action"), Json->GetStringField(TEXT("actionTag")), FString(TEXT("wave")));
	TestEqual(TEXT("serialized queue"), Json->GetArrayField(TEXT("actionQueue")).Num(), 1);
	TestEqual(TEXT("serialized emotion"), Json->GetStringField(TEXT("emotion")), FString(TEXT("happy")));
	TestEqual(TEXT("serialized viseme"), Json->GetStringField(TEXT("visemeSource")), FString(TEXT("livelink")));

	FEpicodePerformanceState RoundTrip;
	FString Error;
	TestTrue(TEXT("state deserializes"), FEpicodePerformanceState::FromJson(Json, RoundTrip, Error));
	TestEqual(TEXT("round trip action"), RoundTrip.ActionTag, FString(TEXT("wave")));
	TestEqual(TEXT("round trip intensity"), RoundTrip.ExpressionIntensity, 0.75f);
	TestTrue(TEXT("round trip source"), RoundTrip.VisemeSource == EEpicodeVisemeSource::LiveLink);

	TSharedPtr<FJsonObject> Invalid = MakeShared<FJsonObject>();
	Invalid->SetNumberField(TEXT("expressionIntensity"), 1.5);
	TestFalse(TEXT("intensity above one rejected"), FEpicodePerformanceState::FromJson(Invalid, RoundTrip, Error));
	Invalid->SetNumberField(TEXT("expressionIntensity"), -0.1);
	TestFalse(TEXT("negative intensity rejected"), FEpicodePerformanceState::FromJson(Invalid, RoundTrip, Error));
	TestFalse(TEXT("non-finite intensity rejected"), FEpicodePerformanceState::IsValidExpressionIntensity(FMath::Sqrt(-1.0)));
	TestFalse(TEXT("empty action rejected"), FEpicodePerformanceState::IsValidActionTag(TEXT("")));
	TestFalse(TEXT("spaced action rejected"), FEpicodePerformanceState::IsValidActionTag(TEXT("wave now")));
	TestTrue(TEXT("dotted action accepted"), FEpicodePerformanceState::IsValidActionTag(TEXT("wave.alt_1")));
	TestTrue(TEXT("emotion accepted"), FEpicodePerformanceState::IsValidEmotion(TEXT("angry")));
	TestFalse(TEXT("empty emotion rejected"), FEpicodePerformanceState::IsValidEmotion(TEXT("")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkActionsTest, "Epicode.Network.Actions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkActionsTest::RunTest(const FString& Parameters)
{
	FEpicodePerformanceState State;
	double Remaining = 0.0;
	FString Error;
	TestTrue(TEXT("first action plays"), FEpicodePerformanceRules::ApplyAction(State, TEXT("wave"), false, 2.0, Remaining, Error));
	TestEqual(TEXT("first action tag"), State.ActionTag, FString(TEXT("wave")));
	TestEqual(TEXT("first action duration"), Remaining, 2.0);
	TestTrue(TEXT("second action queues"), FEpicodePerformanceRules::ApplyAction(State, TEXT("bow"), false, 1.0, Remaining, Error));
	TestEqual(TEXT("active action remains"), State.ActionTag, FString(TEXT("wave")));
	TestEqual(TEXT("queued action count"), State.ActionQueue.Num(), 1);
	TestEqual(TEXT("queued action tag"), State.ActionQueue[0], FString(TEXT("bow")));
	TestTrue(TEXT("interrupt action plays"), FEpicodePerformanceRules::ApplyAction(State, TEXT("jump"), true, 3.0, Remaining, Error));
	TestEqual(TEXT("interrupted action tag"), State.ActionTag, FString(TEXT("jump")));
	TestEqual(TEXT("interrupt clears queue"), State.ActionQueue.Num(), 0);
	TestEqual(TEXT("interrupt duration"), Remaining, 3.0);
	TestFalse(TEXT("invalid action rejected"), FEpicodePerformanceRules::ApplyAction(State, TEXT("bad action"), false, 1.0, Remaining, Error));
	TestTrue(TEXT("invalid action error"), Error.Contains(TEXT("INVALID_PARAMS")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkExpressionTest, "Epicode.Network.Expression", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkExpressionTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("zero intensity valid"), FEpicodePerformanceState::IsValidExpressionIntensity(0.0));
	TestTrue(TEXT("one intensity valid"), FEpicodePerformanceState::IsValidExpressionIntensity(1.0));
	TestFalse(TEXT("over one rejected"), FEpicodePerformanceState::IsValidExpressionIntensity(1.01));
	TestFalse(TEXT("negative rejected"), FEpicodePerformanceState::IsValidExpressionIntensity(-0.01));

	FEpicodePerformanceState State;
	State.Emotion = TEXT("sad");
	State.ExpressionIntensity = 0.5f;
	const TSharedPtr<FJsonObject> Json = State.ToJson();
	TestEqual(TEXT("expression emotion"), Json->GetStringField(TEXT("emotion")), FString(TEXT("sad")));
	TestEqual(TEXT("expression intensity"), Json->GetNumberField(TEXT("expressionIntensity")), 0.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkVoiceDriveTest, "Epicode.Network.VoiceDrive", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkVoiceDriveTest::RunTest(const FString& Parameters)
{
	EEpicodeVisemeSource Source = EEpicodeVisemeSource::None;
	FString Warning;
	TestTrue(TEXT("none resolves"), FEpicodePerformanceRules::ResolveVisemeSource(EEpicodeVisemeSource::None, false, false, Source, Warning));
	TestTrue(TEXT("none stays none"), Source == EEpicodeVisemeSource::None);
	TestTrue(TEXT("preset resolves"), FEpicodePerformanceRules::ResolveVisemeSource(EEpicodeVisemeSource::Preset, false, false, Source, Warning));
	TestTrue(TEXT("preset stays preset"), Source == EEpicodeVisemeSource::Preset);
	TestTrue(TEXT("livelink with source"), FEpicodePerformanceRules::ResolveVisemeSource(EEpicodeVisemeSource::LiveLink, true, false, Source, Warning));
	TestTrue(TEXT("livelink stays livelink"), Source == EEpicodeVisemeSource::LiveLink);
	TestTrue(TEXT("livelink fallback allowed"), FEpicodePerformanceRules::ResolveVisemeSource(EEpicodeVisemeSource::LiveLink, false, false, Source, Warning));
	TestTrue(TEXT("livelink falls back to preset"), Source == EEpicodeVisemeSource::Preset);
	TestTrue(TEXT("livelink warning"), Warning.Contains(TEXT("LIVELINK_UNAVAILABLE")));
	TestTrue(TEXT("audio fallback allowed"), FEpicodePerformanceRules::ResolveVisemeSource(EEpicodeVisemeSource::Audio, false, false, Source, Warning));
	TestTrue(TEXT("audio falls back to preset"), Source == EEpicodeVisemeSource::Preset);
	TestTrue(TEXT("audio warning"), Warning.Contains(TEXT("AUDIO_UNAVAILABLE")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkMotionMatchingTest, "Epicode.Network.MotionMatching", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkMotionMatchingTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("enabled with data"), FEpicodePerformanceRules::CanUseMotionMatching(true, true));
	TestFalse(TEXT("enabled without data falls back"), FEpicodePerformanceRules::CanUseMotionMatching(true, false));
	TestFalse(TEXT("disabled with data does not activate"), FEpicodePerformanceRules::CanUseMotionMatching(false, true));
	TestFalse(TEXT("disabled without data"), FEpicodePerformanceRules::CanUseMotionMatching(false, false));
	FString Warning;
	TestTrue(TEXT("resolver enables with data"), FEpicodePerformanceRules::ResolveMotionMatching(true, true, Warning));
	TestTrue(TEXT("resolver has no warning with data"), Warning.IsEmpty());
	TestFalse(TEXT("resolver falls back without data"), FEpicodePerformanceRules::ResolveMotionMatching(true, false, Warning));
	TestTrue(TEXT("resolver warning names missing data"), Warning.Contains(TEXT("MOTION_DATA_UNAVAILABLE")));
	TestFalse(TEXT("resolver leaves disabled inactive"), FEpicodePerformanceRules::ResolveMotionMatching(false, true, Warning));
	TestTrue(TEXT("resolver disabled has no warning"), Warning.IsEmpty());
	const AEpicodeNetworkCharacter* Character = GetDefault<AEpicodeNetworkCharacter>();
	TestNotNull(TEXT("motion character CDO"), Character);
	if (Character)
	{
		TestFalse(TEXT("default motion matching inactive"), Character->PerformanceState.bMotionMatchingActive);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkIKTest, "Epicode.Network.IK", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkIKTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("negative IK clamp"), FEpicodePerformanceRules::ClampIKWeight(-1.0), 0.0f);
	TestEqual(TEXT("over IK clamp"), FEpicodePerformanceRules::ClampIKWeight(2.0), 1.0f);
	TestEqual(TEXT("mid IK clamp"), FEpicodePerformanceRules::ClampIKWeight(0.5), 0.5f);
	TestEqual(TEXT("non-finite IK clamp"), FEpicodePerformanceRules::ClampIKWeight(FMath::Sqrt(-1.0)), 0.0f);

	const FVector Clamped = FEpicodeLocalIKRules::ClampWeights(FVector(-1.0, 2.0, 0.25));
	TestEqual(TEXT("local IK hand clamp"), Clamped.X, 0.0);
	TestEqual(TEXT("local IK foot clamp"), Clamped.Y, 1.0);
	TestEqual(TEXT("local IK look clamp"), Clamped.Z, 0.25);
	TestTrue(TEXT("zero weights detected"), FEpicodeLocalIKRules::AreWeightsZero(FVector::ZeroVector));
	TestFalse(TEXT("non-zero weights detected"), FEpicodeLocalIKRules::AreWeightsZero(FVector(0.0, 0.0, 0.1)));

	TestTrue(TEXT("local owner client applies IK"),
		FEpicodeLocalIKRules::ShouldApplyLocalIK(true, false, false, true, FVector(1.0, 1.0, 1.0)));
	TestFalse(TEXT("server skips IK"),
		FEpicodeLocalIKRules::ShouldApplyLocalIK(true, true, false, true, FVector(1.0, 1.0, 1.0)));
	TestFalse(TEXT("dedicated server skips IK"),
		FEpicodeLocalIKRules::ShouldApplyLocalIK(true, false, true, true, FVector(1.0, 1.0, 1.0)));
	TestFalse(TEXT("remote proxy skips IK"),
		FEpicodeLocalIKRules::ShouldApplyLocalIK(true, false, false, false, FVector(1.0, 1.0, 1.0)));
	TestFalse(TEXT("zero weights skip IK"),
		FEpicodeLocalIKRules::ShouldApplyLocalIK(true, false, false, true, FVector::ZeroVector));
	TestFalse(TEXT("disabled settings skip IK"),
		FEpicodeLocalIKRules::ShouldApplyLocalIK(false, false, false, true, FVector(1.0, 1.0, 1.0)));

	const FTransform Base(FRotator::ZeroRotator, FVector(0.0, 0.0, 0.0));
	const FTransform Solved(FRotator(0.0, 90.0, 0.0), FVector(100.0, 0.0, 0.0));
	const FTransform Half = FEpicodeLocalIKRules::BlendBoneTransform(Base, Solved, 0.5);
	TestEqual(TEXT("half blend location"), Half.GetTranslation().X, 50.0);
	TestEqual(TEXT("zero blend keeps base"), FEpicodeLocalIKRules::BlendBoneTransform(Base, Solved, 0.0).GetTranslation().X, 0.0);
	TestEqual(TEXT("full blend uses solved"), FEpicodeLocalIKRules::BlendBoneTransform(Base, Solved, 1.0).GetTranslation().X, 100.0);
	TestTrue(TEXT("half blend rotates"), Half.Rotator().Yaw > 0.0);

	const FVector LookAt = FEpicodeLocalIKRules::MakeLookAtTarget(
		FVector(0.0, 0.0, 100.0), FVector(1.0, 0.0, 0.0), 500.0);
	TestTrue(TEXT("look-at target forward"), LookAt.Equals(FVector(500.0, 0.0, 100.0)));
	TestTrue(TEXT("look-at zero forward keeps head"),
		FEpicodeLocalIKRules::MakeLookAtTarget(FVector(0.0, 0.0, 100.0), FVector::ZeroVector, 500.0)
			.Equals(FVector(0.0, 0.0, 100.0)));

	UEpicodeLocalIKComponent* Component = NewObject<UEpicodeLocalIKComponent>();
	TestNotNull(TEXT("local IK component"), Component);
	if (Component)
	{
		TestTrue(TEXT("component starts with zero weights"),
			FEpicodeLocalIKRules::AreWeightsZero(Component->GetIKWeights()));
		Component->SetIKWeights(-1.0, 2.0, 0.5);
		TestEqual(TEXT("component hand weight"), Component->GetIKWeights().X, 0.0);
		TestEqual(TEXT("component foot weight"), Component->GetIKWeights().Y, 1.0);
		TestEqual(TEXT("component look weight"), Component->GetIKWeights().Z, 0.5);
	}

	const AEpicodeNetworkCharacter* Character = GetDefault<AEpicodeNetworkCharacter>();
	TestNotNull(TEXT("network character CDO"), Character);
	if (Character)
	{
		TestNotNull(TEXT("local IK component attached"), Character->GetLocalIKComponent());
	}
	FEpicodePerformanceState PerformanceState;
	const TSharedPtr<FJsonObject> StateJson = PerformanceState.ToJson();
	TestFalse(TEXT("performance snapshot excludes local IK weights"), StateJson->HasField(TEXT("localIKWeights")));
	AddInfo(FString::Printf(
		TEXT("LOCAL_IK_BONE_DELTA baseX=%.1f halfX=%.1f halfYaw=%.1f snapshotHasLocalIK=%s"),
		Base.GetTranslation().X,
		Half.GetTranslation().X,
		Half.Rotator().Yaw,
		StateJson->HasField(TEXT("localIKWeights")) ? TEXT("true") : TEXT("false")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkPerformanceEventsTest, "Epicode.Network.PerformanceEvents", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkPerformanceEventsTest::RunTest(const FString& Parameters)
{
	FEpicodeNetworkSession Session;
	FString Error;
	TestTrue(TEXT("create"), Session.Create(TEXT("scene-performance"), TEXT("owner-perf"), TEXT("Owner"), 4, 30.0, Error));
	TestTrue(TEXT("start"), Session.Start(Error));
	FString Joined;
	TestTrue(TEXT("join"), Session.Join(TEXT("perf-1"), TEXT("Performer"), EEpicodeSessionRole::Performer, Joined, Error));
	TestTrue(TEXT("assign character"), Session.AssignCharacter(TEXT("owner-perf"), TEXT("perf-1"), TEXT("character-perf"), Error));

	const int64 Before = Session.GetEventSequence();
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("action"), TEXT("wave"));
	TestTrue(TEXT("publish performance event"), Session.PublishEvent(TEXT("performance.action"), TEXT("perf-1"), TEXT("character-perf"), Payload));
	TestTrue(TEXT("event sequence incremented"), Session.GetEventSequence() == Before + 1);
	bool bNeedsSnapshot = false;
	const TArray<FEpicodeNetworkEvent> Events = Session.GetEventsSince(Before, 10, bNeedsSnapshot);
	TestEqual(TEXT("event count"), Events.Num(), 1);
	TestEqual(TEXT("event type"), Events[0].Type, FString(TEXT("performance.action")));
	TestTrue(TEXT("event timestamp"), Events[0].Timestamp > 0.0);
	TestEqual(TEXT("event player"), Events[0].PlayerId, FString(TEXT("perf-1")));
	TestEqual(TEXT("event entity"), Events[0].EntityId, FString(TEXT("character-perf")));

	const TSharedPtr<FJsonObject> Snapshot = Session.ToJson();
	TestEqual(TEXT("snapshot performance events"), Snapshot->GetArrayField(TEXT("performanceEvents")).Num(), 1);
	TestEqual(TEXT("snapshot performance sequence"), Snapshot->GetArrayField(TEXT("performanceEvents"))[0]->AsObject()->GetNumberField(TEXT("sequence")), static_cast<double>(Before + 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkItemsTest, "Epicode.Network.Items", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkItemsTest::RunTest(const FString& Parameters)
{
	FEpicodeWorldState World;
	FString ItemId;
	FString Error;
	TestTrue(TEXT("spawn item"), World.SpawnItem(TEXT("scrap"), TEXT("Scrap Metal"), FVector(100.0, 0.0, 0.0), 1.0, 2, ItemId, Error));
	const FEpicodeItemRecord* Item = World.FindItem(ItemId);
	TestNotNull(TEXT("item exists"), Item);
	if (Item)
	{
		TestEqual(TEXT("item type"), Item->ItemType, FString(TEXT("scrap")));
		TestEqual(TEXT("item quantity"), Item->Quantity, 2);
		TestTrue(TEXT("item replicates"), Item->bReplicates);
		TestEqual(TEXT("item replication class"), Item->ReplicationClass, FString(TEXT("owner")));
	}
	TestFalse(TEXT("invalid durability rejected"), World.SpawnItem(TEXT("scrap"), TEXT("Bad"), FVector::ZeroVector, -1.0, 1, ItemId, Error));
	TestFalse(TEXT("empty type rejected"), World.SpawnItem(TEXT(""), TEXT("Bad"), FVector::ZeroVector, 1.0, 1, ItemId, Error));

	TestTrue(TEXT("pickup owns item"), World.PickupItem(TEXT("alice"), ItemId, 10.0, 200.0, Error));
	TestTrue(TEXT("owner updates state"), World.SetItemState(TEXT("alice"), ItemId, 0.5, 1, Error));
	TestFalse(TEXT("other player forgery rejected"), World.SetItemState(TEXT("bob"), ItemId, 1.0, 5, Error));
	TestTrue(TEXT("forgery error"), Error.Contains(TEXT("FORBIDDEN")));
	TestFalse(TEXT("invalid state rejected"), World.SetItemState(TEXT("alice"), ItemId, 2.0, 0, Error));
	TestEqual(TEXT("item json id"), World.FindItem(ItemId)->ToJson()->GetStringField(TEXT("itemId")), ItemId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkItemInteractionTest, "Epicode.Network.ItemInteraction", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkItemInteractionTest::RunTest(const FString& Parameters)
{
	FEpicodeWorldState World;
	FString ItemId;
	FString Error;
	TestTrue(TEXT("spawn"), World.SpawnItem(TEXT("tool"), TEXT("Wrench"), FVector::ZeroVector, 1.0, 1, ItemId, Error));
	TestFalse(TEXT("distance too far rejected"), World.PickupItem(TEXT("alice"), ItemId, 300.0, 200.0, Error));
	TestTrue(TEXT("distance error"), Error.Contains(TEXT("DISTANCE_TOO_FAR")));
	TestTrue(TEXT("pickup"), World.PickupItem(TEXT("alice"), ItemId, 10.0, 200.0, Error));
	TestFalse(TEXT("double pickup by bob rejected"), World.PickupItem(TEXT("bob"), ItemId, 0.0, 200.0, Error));
	TestTrue(TEXT("already held error"), Error.Contains(TEXT("ITEM_ALREADY_HELD")));
	TestTrue(TEXT("hold slot"), World.HoldItem(TEXT("alice"), ItemId, TEXT("left"), Error));
	TestEqual(TEXT("slot applied"), World.FindItem(ItemId)->Slot, FString(TEXT("left")));
	TestTrue(TEXT("release"), World.ReleaseItem(TEXT("alice"), ItemId, Error));
	TestTrue(TEXT("holder cleared"), World.FindItem(ItemId)->HolderPlayerId.IsEmpty());
	TestFalse(TEXT("release non-held rejected"), World.ReleaseItem(TEXT("alice"), ItemId, Error));
	TestTrue(TEXT("repickup"), World.PickupItem(TEXT("alice"), ItemId, 0.0, 200.0, Error));
	TestTrue(TEXT("drop"), World.DropItem(TEXT("alice"), ItemId, Error));
	TestTrue(TEXT("drop clears owner"), World.FindItem(ItemId)->OwnerPlayerId.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkInventoryTest, "Epicode.Network.Inventory", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkInventoryTest::RunTest(const FString& Parameters)
{
	FEpicodeWorldState World;
	FString Error;
	FEpicodeInventoryRecord Inventory;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		FString ItemId;
		TestTrue(TEXT("spawn inventory item"), World.SpawnItem(TEXT("scrap"), TEXT("Scrap"), FVector::ZeroVector, 1.0, 1, ItemId, Error));
		TestTrue(TEXT("pickup inventory item"), World.PickupItem(TEXT("alice"), ItemId, 0.0, 200.0, Error));
	}
	TestTrue(TEXT("inventory readable by owner"), World.GetInventory(TEXT("alice"), TEXT("alice"), Inventory, Error));
	TestEqual(TEXT("inventory count"), Inventory.ItemIds.Num(), 8);
	TestEqual(TEXT("inventory capacity"), Inventory.Capacity, 8);
	TestFalse(TEXT("inventory private"), World.GetInventory(TEXT("bob"), TEXT("alice"), Inventory, Error));
	TestTrue(TEXT("privacy error"), Error.Contains(TEXT("FORBIDDEN")));
	FString OverflowId;
	TestTrue(TEXT("spawn overflow item"), World.SpawnItem(TEXT("scrap"), TEXT("Overflow"), FVector::ZeroVector, 1.0, 1, OverflowId, Error));
	TestFalse(TEXT("inventory full rejected"), World.PickupItem(TEXT("alice"), OverflowId, 0.0, 200.0, Error));
	TestTrue(TEXT("full error"), Error.Contains(TEXT("INVENTORY_FULL")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkCraftingTest, "Epicode.Network.Crafting", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkCraftingTest::RunTest(const FString& Parameters)
{
	FEpicodeWorldState World;
	FEpicodeRecipe Recipe;
	Recipe.RecipeId = TEXT("craft.tool");
	Recipe.DisplayName = TEXT("Tool");
	Recipe.InputItemTypes = { TEXT("scrap") };
	Recipe.OutputItemType = TEXT("tool");
	Recipe.Duration = 2.0;
	FString Error;
	TestTrue(TEXT("add recipe"), World.AddRecipe(Recipe, Error));
	TestFalse(TEXT("duplicate recipe rejected"), World.AddRecipe(Recipe, Error));

	FString JobId;
	TestFalse(TEXT("missing materials rejected"), World.StartCraft(TEXT("alice"), TEXT("craft.tool"), 0.0, JobId, Error));
	TestTrue(TEXT("missing materials error"), Error.Contains(TEXT("MISSING_MATERIALS")));

	FString ScrapId;
	if (!World.SpawnItem(TEXT("scrap"), TEXT("Scrap"), FVector::ZeroVector, 1.0, 1, ScrapId, Error))
	{
		AddError(FString::Printf(TEXT("spawn scrap failed: %s"), *Error));
		return false;
	}
	if (!World.PickupItem(TEXT("alice"), ScrapId, 0.0, 200.0, Error))
	{
		AddError(FString::Printf(TEXT("pickup scrap failed: %s"), *Error));
		return false;
	}
	if (!World.StartCraft(TEXT("alice"), TEXT("craft.tool"), 10.0, JobId, Error))
	{
		AddError(FString::Printf(TEXT("start craft failed: %s"), *Error));
		return false;
	}
	TSharedPtr<FJsonObject> Status;
	TestTrue(TEXT("craft status"), World.GetCraftStatus(TEXT("alice"), JobId, 11.0, Status, Error));
	if (!Status.IsValid())
	{
		AddError(FString::Printf(TEXT("craft status unavailable: %s job=%s"), *Error, *JobId));
		return false;
	}
	TestEqual(TEXT("craft progress"), Status->GetNumberField(TEXT("progress")), 0.5);
	TestFalse(TEXT("other player status rejected"), World.GetCraftStatus(TEXT("bob"), JobId, 11.0, Status, Error));

	World.Tick(13.0);
	TestFalse(TEXT("completed craft inactive"), World.GetCrafts()[0].bActive);
	FEpicodeInventoryRecord Inventory;
	TestTrue(TEXT("crafted inventory"), World.GetInventory(TEXT("alice"), TEXT("alice"), Inventory, Error));
	TestEqual(TEXT("crafted item added"), Inventory.ItemIds.Num(), 1);
	const FEpicodeItemRecord* Crafted = World.FindItem(Inventory.ItemIds[0]);
	TestNotNull(TEXT("crafted item exists"), Crafted);
	if (Crafted)
	{
		TestEqual(TEXT("crafted type"), Crafted->ItemType, FString(TEXT("tool")));
	}

	FString CancelJob;
	FString CancelScrap;
	TestTrue(TEXT("spawn cancel scrap"), World.SpawnItem(TEXT("scrap"), TEXT("Scrap"), FVector::ZeroVector, 1.0, 1, CancelScrap, Error));
	TestTrue(TEXT("pickup cancel scrap"), World.PickupItem(TEXT("alice"), CancelScrap, 0.0, 200.0, Error));
	TestTrue(TEXT("start cancel craft"), World.StartCraft(TEXT("alice"), TEXT("craft.tool"), 20.0, CancelJob, Error));
	TestTrue(TEXT("cancel craft"), World.CancelCraft(TEXT("alice"), CancelJob, Error));
	TestFalse(TEXT("cancel completed rejected"), World.CancelCraft(TEXT("alice"), CancelJob, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkBuildingTest, "Epicode.Network.Building", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkBuildingTest::RunTest(const FString& Parameters)
{
	FEpicodeWorldState World;
	FString Error;
	World.GrantMaterials(TEXT("alice"), 10);
	FString BuildId;
	TestTrue(TEXT("valid build"), World.ConfirmBuild(TEXT("alice"), TEXT("wall"), FVector::ZeroVector, FVector(100.0, 100.0, 0.0), FRotator::ZeroRotator, 3, 500.0, BuildId, Error));
	TestEqual(TEXT("materials deducted"), World.GetMaterials(TEXT("alice")), 7);
	TestEqual(TEXT("build count"), World.GetBuilds().Num(), 1);
	TestFalse(TEXT("collision rejected"), World.ConfirmBuild(TEXT("alice"), TEXT("wall"), FVector::ZeroVector, FVector(100.0, 100.0, 0.0), FRotator::ZeroRotator, 3, 500.0, BuildId, Error));
	TestTrue(TEXT("collision error"), Error.Contains(TEXT("BUILD_COLLISION")));
	TestFalse(TEXT("out of bounds rejected"), World.ConfirmBuild(TEXT("alice"), TEXT("wall"), FVector::ZeroVector, FVector(20000.0, 0.0, 0.0), FRotator::ZeroRotator, 1, 50000.0, BuildId, Error));
	TestFalse(TEXT("too far rejected"), World.ConfirmBuild(TEXT("alice"), TEXT("wall"), FVector::ZeroVector, FVector(500.0, 0.0, 0.0), FRotator::ZeroRotator, 1, 100.0, BuildId, Error));
	TestFalse(TEXT("missing materials rejected"), World.ConfirmBuild(TEXT("alice"), TEXT("wall"), FVector::ZeroVector, FVector(300.0, 0.0, 0.0), FRotator::ZeroRotator, 50, 500.0, BuildId, Error));
	TestTrue(TEXT("undo build"), World.UndoBuild(TEXT("alice"), World.GetBuilds()[0].BuildId, Error));
	TestEqual(TEXT("materials refunded"), World.GetMaterials(TEXT("alice")), 10);
	TestEqual(TEXT("builds after undo"), World.GetBuilds().Num(), 0);
	TestFalse(TEXT("undo unknown rejected"), World.UndoBuild(TEXT("alice"), TEXT("missing"), Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkWorldEventsTest, "Epicode.Network.WorldEvents", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkWorldEventsTest::RunTest(const FString& Parameters)
{
	FEpicodeNetworkSession Session;
	FString Error;
	TestTrue(TEXT("create"), Session.Create(TEXT("scene-world"), TEXT("owner-world"), TEXT("Owner"), 4, 30.0, Error));
	TestTrue(TEXT("start"), Session.Start(Error));
	FString Joined;
	TestTrue(TEXT("join"), Session.Join(TEXT("world-1"), TEXT("Performer"), EEpicodeSessionRole::Performer, Joined, Error));
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("itemId"), TEXT("item-1"));
	TestTrue(TEXT("item event"), Session.PublishEvent(TEXT("item.spawned"), TEXT("world-1"), TEXT("item-1"), Payload));
	TestTrue(TEXT("craft event"), Session.PublishEvent(TEXT("craft.started"), TEXT("world-1"), TEXT("job-1"), Payload));
	TestTrue(TEXT("build event"), Session.PublishEvent(TEXT("build.confirmed"), TEXT("world-1"), TEXT("build-1"), Payload));
	const TSharedPtr<FJsonObject> Snapshot = Session.ToJson();
	TestEqual(TEXT("world events in snapshot"), Snapshot->GetArrayField(TEXT("worldEvents")).Num(), 3);
	TestEqual(TEXT("world event type"), Snapshot->GetArrayField(TEXT("worldEvents"))[0]->AsObject()->GetStringField(TEXT("type")), FString(TEXT("item.spawned")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkWorldPermissionsTest, "Epicode.Network.WorldPermissions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkWorldPermissionsTest::RunTest(const FString& Parameters)
{
	FString Error;
	TestFalse(TEXT("observer cannot interact"), FEpicodeSessionPolicy::CanPerform(EEpicodeSessionRole::Observer, EEpicodeSessionAction::WorldInteract));
	TestTrue(TEXT("performer can interact"), FEpicodeSessionPolicy::CanPerform(EEpicodeSessionRole::Performer, EEpicodeSessionAction::WorldInteract));
	TestTrue(TEXT("owner can interact"), FEpicodeSessionPolicy::CanPerform(EEpicodeSessionRole::Owner, EEpicodeSessionAction::WorldInteract));
	TestFalse(TEXT("performer cannot manage session"), FEpicodeSessionPolicy::CanPerform(EEpicodeSessionRole::Performer, EEpicodeSessionAction::ManageSession));

	FEpicodeWorldState World;
	FEpicodeRecipe Recipe;
	Recipe.RecipeId = TEXT("craft.tool");
	Recipe.InputItemTypes = { TEXT("scrap") };
	Recipe.OutputItemType = TEXT("tool");
	Recipe.Duration = 5.0;
	TestTrue(TEXT("add recipe"), World.AddRecipe(Recipe, Error));
	FString ScrapId;
	TestTrue(TEXT("spawn scrap"), World.SpawnItem(TEXT("scrap"), TEXT("Scrap"), FVector::ZeroVector, 1.0, 1, ScrapId, Error));
	TestTrue(TEXT("pickup scrap"), World.PickupItem(TEXT("alice"), ScrapId, 0.0, 200.0, Error));
	FString JobId;
	TestTrue(TEXT("start craft"), World.StartCraft(TEXT("alice"), TEXT("craft.tool"), 0.0, JobId, Error));
	World.TransferCrafts(TEXT("alice"), TEXT("owner"));
	TSharedPtr<FJsonObject> Status;
	TestTrue(TEXT("owner receives craft"), World.GetCraftStatus(TEXT("owner"), JobId, 1.0, Status, Error));
	TestFalse(TEXT("original craft owner lost access"), World.GetCraftStatus(TEXT("alice"), JobId, 1.0, Status, Error));
	return true;
}

#endif
