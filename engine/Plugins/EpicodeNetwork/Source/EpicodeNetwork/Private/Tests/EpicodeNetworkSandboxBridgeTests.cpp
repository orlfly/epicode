#include "Misc/AutomationTest.h"

#include "EpicodeNetworkSandboxBridge.h"
#include "EpicodeProductionDocument.h"
#include "EpicodeSandboxSettings.h"
#include "EpicodeSandboxTypes.h"
#include "EpicodeSceneDocument.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FEpicodeSceneDocument MakeBridgeScene()
	{
		FEpicodeSceneDocument Scene = FEpicodeSceneDocument::CreateDefault(TEXT("Bridge Test Scene"));
		FEpicodeEntityRecord Character;
		Character.Id = FGuid::NewGuid();
		Character.Type = EEpicodeEntityType::Character;
		Character.Name = TEXT("Bridge Hero");
		Character.Transform.Location = FVector(10.0, 20.0, 0.0);
		Character.Data = MakeShared<FJsonObject>();
		Scene.Entities.Add(Character);

		FEpicodeEntityRecord Prop;
		Prop.Id = FGuid::NewGuid();
		Prop.Type = EEpicodeEntityType::Prop;
		Prop.Name = TEXT("Bridge Crate");
		Prop.Transform.Location = FVector(100.0, 0.0, 0.0);
		Prop.Data = MakeShared<FJsonObject>();
		Scene.Entities.Add(Prop);

		FEpicodeEntityRecord Camera;
		Camera.Id = FGuid::NewGuid();
		Camera.Type = EEpicodeEntityType::Camera;
		Camera.Name = TEXT("Bridge Camera");
		Camera.Data = MakeShared<FJsonObject>();
		Scene.Entities.Add(Camera);

		return Scene;
	}

	FEpicodeSandboxSessionConfig MakeBridgeConfig()
	{
		FEpicodeSandboxSessionConfig Config;
		Config.SessionId = TEXT("bridge-session");
		Config.Seed = 99;
		Config.TickRate = 30;
		return Config;
	}

	const FEpicodeSandboxEntity* FindEntityByKind(const FEpicodeSandboxWorld& World, EEpicodeSandboxEntityKind Kind)
	{
		for (const FString& EntityId : World.GetSortedEntityIds())
		{
			const FEpicodeSandboxEntity* Entity = World.Entities.Find(EntityId);
			if (Entity && Entity->Kind == Kind)
			{
				return Entity;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkSandboxIntegrationTest, "Epicode.Network.SandboxIntegration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkSandboxIntegrationTest::RunTest(const FString& Parameters)
{
	const FEpicodeSceneDocument Scene = MakeBridgeScene();
	const FEpicodeProductionDocument Production = FEpicodeProductionDocument::CreateDefault(Scene.SceneId, TEXT("Bridge Production"));
	FEpicodeNetworkSandboxBridge Bridge;
	FString Error;
	TestTrue(TEXT("bridge creates from production"), Bridge.Create(Production, Scene, MakeBridgeConfig(), Error));
	TestEqual(TEXT("bridge is running"), Bridge.GetState(), EEpicodeSandboxSessionState::Running);
	TestTrue(TEXT("bridge is active"), Bridge.IsActive());
	TestFalse(TEXT("production session is not legacy"), Bridge.IsLegacy());
	TestTrue(TEXT("production session is replayable"), Bridge.IsReplayable());
	TestFalse(TEXT("digest is populated"), Bridge.GetDigest().IsEmpty());

	FEpicodeNetworkSandboxBridge Legacy;
	TestTrue(TEXT("legacy bridge creates"), Legacy.CreateLegacy(Scene, MakeBridgeConfig(), Error));
	TestTrue(TEXT("legacy session is marked legacy"), Legacy.IsLegacy());
	TestFalse(TEXT("legacy session is not replayable"), Legacy.IsReplayable());
	TestTrue(TEXT("legacy bridge closes"), Legacy.Close(Error));
	TestFalse(TEXT("second close is rejected"), Legacy.Close(Error));
	TestTrue(TEXT("second close reports inactive"), Error.Contains(TEXT("SANDBOX_NOT_CREATED")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkCastBindingTest, "Epicode.Network.CastBinding", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkCastBindingTest::RunTest(const FString& Parameters)
{
	const FEpicodeSceneDocument Scene = MakeBridgeScene();
	const FEpicodeProductionDocument Production = FEpicodeProductionDocument::CreateDefault(Scene.SceneId, TEXT("Bridge Production"));
	FEpicodeNetworkSandboxBridge Bridge;
	FString Error;
	if (!TestTrue(TEXT("bridge creates"), Bridge.Create(Production, Scene, MakeBridgeConfig(), Error)))
	{
		return false;
	}
	const FEpicodeSandboxEntity* Character = FindEntityByKind(Bridge.GetWorld(), EEpicodeSandboxEntityKind::Character);
	const FEpicodeSandboxEntity* Prop = FindEntityByKind(Bridge.GetWorld(), EEpicodeSandboxEntityKind::Prop);
	if (!TestNotNull(TEXT("scene has a character"), Character) || !TestNotNull(TEXT("scene has a prop"), Prop))
	{
		return false;
	}

	TestTrue(TEXT("owner binds"), Bridge.BindPlayer(TEXT("owner"), EEpicodeSandboxParticipantKind::Owner, FString(), Error));
	TestTrue(TEXT("performer binds"), Bridge.BindPlayer(TEXT("performer"), EEpicodeSandboxParticipantKind::Performer, Character->Id, Error));
	TestTrue(TEXT("viewer binds"), Bridge.BindPlayer(TEXT("viewer"), EEpicodeSandboxParticipantKind::Viewer, FString(), Error));
	TestTrue(TEXT("agent binds"), Bridge.BindPlayer(TEXT("agent"), EEpicodeSandboxParticipantKind::Agent, FString(), Error));
	TestTrue(TEXT("agent can interact"), Bridge.CanSubmit(TEXT("agent"), EEpicodeSessionAction::WorldInteract, Error));

	TestTrue(TEXT("owner can manage"), Bridge.CanSubmit(TEXT("owner"), EEpicodeSessionAction::ManageSession, Error));
	TestTrue(TEXT("performer can interact"), Bridge.CanSubmit(TEXT("performer"), EEpicodeSessionAction::WorldInteract, Error));
	TestFalse(TEXT("viewer cannot interact"), Bridge.CanSubmit(TEXT("viewer"), EEpicodeSessionAction::WorldInteract, Error));
	TestTrue(TEXT("viewer rejection is explicit"), Error.Contains(TEXT("VIEWER_READ_ONLY")));
	TestFalse(TEXT("second performer cannot take the character"), Bridge.BindPlayer(TEXT("performer-2"), EEpicodeSandboxParticipantKind::Performer, Character->Id, Error));
	TestTrue(TEXT("slot conflict is explicit"), Error.Contains(TEXT("CHARACTER_SLOT_TAKEN")));

	TestFalse(TEXT("unknown character is rejected"), Bridge.BindPlayer(TEXT("ghost"), EEpicodeSandboxParticipantKind::Performer, TEXT("missing-entity"), Error));
	TestTrue(TEXT("missing character is explicit"), Error.Contains(TEXT("CHARACTER_NOT_FOUND")));

	TestTrue(TEXT("performer reconnects"), Bridge.SetPlayerConnected(TEXT("performer"), false, Error));
	const FEpicodeSandboxParticipant* Performer = Bridge.GetWorld().FindParticipant(TEXT("performer"));
	TestNotNull(TEXT("participant survives disconnect"), Performer);
	if (Performer)
	{
		TestFalse(TEXT("participant is disconnected"), Performer->bConnected);
	}
	TestTrue(TEXT("performer binds again"), Bridge.BindPlayer(TEXT("performer"), EEpicodeSandboxParticipantKind::Performer, Prop->Id, Error));
	Performer = Bridge.GetWorld().FindParticipant(TEXT("performer"));
	if (Performer)
	{
		TestTrue(TEXT("participant is connected again"), Performer->bConnected);
		TestEqual(TEXT("character assignment updates"), Performer->CharacterEntityId, Prop->Id);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkProjectionTest, "Epicode.Network.Projection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkProjectionTest::RunTest(const FString& Parameters)
{
	const FEpicodeSceneDocument Scene = MakeBridgeScene();
	const FEpicodeProductionDocument Production = FEpicodeProductionDocument::CreateDefault(Scene.SceneId, TEXT("Bridge Production"));
	FEpicodeNetworkSandboxBridge Bridge;
	FString Error;
	if (!TestTrue(TEXT("bridge creates"), Bridge.Create(Production, Scene, MakeBridgeConfig(), Error)))
	{
		return false;
	}
	const FEpicodeSandboxEntity* Prop = FindEntityByKind(Bridge.GetWorld(), EEpicodeSandboxEntityKind::Prop);
	if (!TestNotNull(TEXT("scene has a prop"), Prop))
	{
		return false;
	}
	const FString PropId = Prop->Id;
	FEpicodeSandboxEntity Trigger;
	Trigger.Id = TEXT("bridge-trigger");
	Trigger.Kind = EEpicodeSandboxEntityKind::Trigger;
	Trigger.Data = MakeShared<FJsonObject>();
	TestTrue(TEXT("sandbox trigger can be added"), Bridge.GetMutableWorld().AddEntity(Trigger, Error));
	for (const EEpicodeSandboxEntityKind Kind : { EEpicodeSandboxEntityKind::Npc, EEpicodeSandboxEntityKind::Item, EEpicodeSandboxEntityKind::Build })
	{
		FEpicodeSandboxEntity Entity;
		Entity.Id = FString::Printf(TEXT("bridge-%d"), static_cast<int32>(Kind));
		Entity.Kind = Kind;
		Entity.Data = MakeShared<FJsonObject>();
		TestTrue(TEXT("sandbox projection entity can be added"), Bridge.GetMutableWorld().AddEntity(Entity, Error));
	}

	TArray<FEpicodeSandboxProjectionCommand> Initial = Bridge.BuildProjection();
	TestTrue(TEXT("projection emits upserts"), Initial.Num() > 0);
	bool bSawProp = false;
	bool bSawTrigger = false;
	TSet<EEpicodeSandboxEntityKind> UpsertedKinds;
	for (const FEpicodeSandboxProjectionCommand& Command : Initial)
	{
		bSawProp |= Command.EntityId == PropId && Command.Operation == TEXT("upsert");
		bSawTrigger |= Command.Kind == EEpicodeSandboxEntityKind::Trigger;
		UpsertedKinds.Add(Command.Kind);
	}
	TestTrue(TEXT("prop is projected"), bSawProp);
	TestFalse(TEXT("trigger is not projected"), bSawTrigger);
	TestTrue(TEXT("npc is projected"), UpsertedKinds.Contains(EEpicodeSandboxEntityKind::Npc));
	TestTrue(TEXT("item is projected"), UpsertedKinds.Contains(EEpicodeSandboxEntityKind::Item));
	TestTrue(TEXT("build is projected"), UpsertedKinds.Contains(EEpicodeSandboxEntityKind::Build));

	TestTrue(TEXT("prop removal succeeds"), Bridge.GetMutableWorld().RemoveEntity(PropId, Error));
	TArray<FEpicodeSandboxProjectionCommand> AfterRemoval = Bridge.BuildProjection();
	bool bSawRemove = false;
	for (const FEpicodeSandboxProjectionCommand& Command : AfterRemoval)
	{
		bSawRemove |= Command.EntityId == PropId && Command.Operation == TEXT("remove");
	}
	TestTrue(TEXT("removed prop is cleaned up"), bSawRemove);
	TestTrue(TEXT("projection sequence advances"), Bridge.GetLastProjectedSequence() >= 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkSandboxReplicationTest, "Epicode.Network.SandboxReplication", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkSandboxReplicationTest::RunTest(const FString& Parameters)
{
	const FEpicodeSceneDocument Scene = MakeBridgeScene();
	FEpicodeNetworkSandboxBridge Bridge;
	FString Error;
	if (!TestTrue(TEXT("legacy bridge creates"), Bridge.CreateLegacy(Scene, MakeBridgeConfig(), Error)))
	{
		return false;
	}
	const FEpicodeSandboxEntity* Character = FindEntityByKind(Bridge.GetWorld(), EEpicodeSandboxEntityKind::Character);
	if (!TestNotNull(TEXT("scene has a character"), Character))
	{
		return false;
	}
	TestTrue(TEXT("performer binds"), Bridge.BindPlayer(TEXT("player-1"), EEpicodeSandboxParticipantKind::Performer, Character->Id, Error));
	TestTrue(TEXT("viewer binds"), Bridge.BindPlayer(TEXT("viewer-1"), EEpicodeSandboxParticipantKind::Viewer, FString(), Error));
	TestFalse(TEXT("viewer cannot submit"), Bridge.CanSubmit(TEXT("viewer-1"), EEpicodeSessionAction::WorldInteract, Error));
	TestTrue(TEXT("viewer rejection is explicit"), Error.Contains(TEXT("VIEWER_READ_ONLY")));
	TestTrue(TEXT("transform is recorded"), Bridge.RecordPlayerTransform(TEXT("player-1"), FVector(5.0, 6.0, 7.0), FRotator(0.0, 90.0, 0.0), Error));

	const TArray<FEpicodeNetworkEvent> Events = Bridge.CollectEventsSince(0);
	TestTrue(TEXT("sandbox event is mirrored"), Events.Num() > 0);
	bool bSawMoved = false;
	for (const FEpicodeNetworkEvent& Event : Events)
	{
		bSawMoved |= Event.Type == TEXT("sandbox.playerMoved") && Event.PlayerId == TEXT("player-1");
	}
	TestTrue(TEXT("player movement event is present"), bSawMoved);

	TSharedPtr<FJsonObject> Snapshot = Bridge.GetReplicationSnapshot();
	TestTrue(TEXT("snapshot is valid"), Snapshot.IsValid());
	if (Snapshot.IsValid())
	{
		TestFalse(TEXT("legacy snapshot is not replayable"), Snapshot->GetBoolField(TEXT("replayable")));
		TestTrue(TEXT("legacy snapshot is flagged"), Snapshot->GetBoolField(TEXT("legacy")));
		TestFalse(TEXT("snapshot includes digest"), Snapshot->GetStringField(TEXT("digest")).IsEmpty());
		TestTrue(TEXT("snapshot sequence advances"), Snapshot->GetNumberField(TEXT("eventSequence")) > 0.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeNetworkLegacySessionTest, "Epicode.Network.LegacySession", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeNetworkLegacySessionTest::RunTest(const FString& Parameters)
{
	const FEpicodeSceneDocument Scene = MakeBridgeScene();
	FEpicodeNetworkSandboxBridge Bridge;
	FString Error;
	if (!TestTrue(TEXT("legacy bridge creates"), Bridge.CreateLegacy(Scene, MakeBridgeConfig(), Error)))
	{
		return false;
	}
	TestTrue(TEXT("legacy session is running"), Bridge.GetState() == EEpicodeSandboxSessionState::Running);
	TestTrue(TEXT("legacy session is flagged"), Bridge.IsLegacy());
	TestFalse(TEXT("legacy session is not replayable"), Bridge.IsReplayable());
	const TSharedPtr<FJsonObject> Snapshot = Bridge.GetReplicationSnapshot();
	if (TestTrue(TEXT("legacy snapshot is valid"), Snapshot.IsValid()))
	{
		TestFalse(TEXT("legacy snapshot downgrades replayable"), Snapshot->GetBoolField(TEXT("replayable")));
		TestTrue(TEXT("legacy snapshot exposes legacy flag"), Snapshot->GetBoolField(TEXT("legacy")));
		TestTrue(TEXT("legacy snapshot exposes state"), Snapshot->GetStringField(TEXT("state")) == TEXT("running"));
	}
	TestTrue(TEXT("legacy session closes"), Bridge.Close(Error));
	TestTrue(TEXT("legacy session is closed"), Bridge.GetState() == EEpicodeSandboxSessionState::Closed);
	TestFalse(TEXT("closed legacy session cannot restart"), Bridge.Start(Error));
	TestTrue(TEXT("restart error is explicit"), Error.Contains(TEXT("SANDBOX_NOT_CREATED")));

	UEpicodeSandboxSettings* Settings = GetMutableDefault<UEpicodeSandboxSettings>();
	const bool bWasEnabled = Settings->bEnableSandboxDirector;
	Settings->bEnableSandboxDirector = false;
	FEpicodeNetworkSandboxBridge DisabledBridge;
	TestFalse(TEXT("feature switch disables the bridge"), DisabledBridge.CreateLegacy(Scene, MakeBridgeConfig(), Error));
	TestTrue(TEXT("disabled switch is explicit"), Error.Contains(TEXT("SANDBOX_DISABLED")));
	Settings->bEnableSandboxDirector = bWasEnabled;
	return true;
}

#endif
