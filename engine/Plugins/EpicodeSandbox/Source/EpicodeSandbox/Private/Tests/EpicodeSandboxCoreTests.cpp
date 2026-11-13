#include "Misc/AutomationTest.h"

#include "EpicodeProductionDocument.h"
#include "EpicodeSandboxClock.h"
#include "EpicodeSandboxProjection.h"
#include "EpicodeSandboxRandom.h"
#include "EpicodeSandboxSession.h"
#include "EpicodeSandboxWorld.h"
#include "EpicodeSceneDocument.h"
#include "EpicodeTypes.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	struct FSandboxTestScene
	{
		FEpicodeSceneDocument Scene;
		FGuid HeroId;
		FGuid PropId;
		FGuid LightId;
	};

	FSandboxTestScene MakeSandboxScene()
	{
		FSandboxTestScene Result;
		Result.Scene = FEpicodeSceneDocument::CreateDefault(TEXT("Sandbox Test Scene"));
		Result.HeroId = FGuid::NewGuid();
		Result.PropId = FGuid::NewGuid();
		Result.LightId = FGuid::NewGuid();

		FEpicodeEntityRecord Hero;
		Hero.Id = Result.HeroId;
		Hero.Type = EEpicodeEntityType::Character;
		Hero.Name = TEXT("Hero");
		Hero.Transform.Location = FVector(10, 20, 30);
		Result.Scene.Entities.Add(Hero);

		FEpicodeEntityRecord Prop;
		Prop.Id = Result.PropId;
		Prop.Type = EEpicodeEntityType::Prop;
		Prop.Name = TEXT("Crate");
		Prop.Data = MakeShared<FJsonObject>();
		Prop.Data->SetStringField(TEXT("archetypeId"), TEXT("Crate"));
		Result.Scene.Entities.Add(Prop);

		FEpicodeEntityRecord Light;
		Light.Id = Result.LightId;
		Light.Type = EEpicodeEntityType::Light;
		Light.Name = TEXT("KeyLight");
		Result.Scene.Entities.Add(Light);
		return Result;
	}

	FEpicodeProductionDocument MakeSandboxProduction(const FSandboxTestScene& InScene)
	{
		FEpicodeProductionDocument Production = FEpicodeProductionDocument::CreateDefault(InScene.Scene.SceneId, TEXT("Sandbox Production"));
		FEpicodeProductionCastMember Member;
		Member.Slot = TEXT("hero");
		Member.EntityId = InScene.HeroId.ToString(EGuidFormats::DigitsWithHyphens);
		Member.ParticipantKind = TEXT("performer");
		Production.Cast.Add(Member);
		Production.Narrative->SetStringField(TEXT("entryBeatId"), TEXT("beat-1"));
		TSharedPtr<FJsonObject> Variables = Production.Narrative->GetObjectField(TEXT("variables"));
		Variables->SetNumberField(TEXT("alarmLevel"), 0);
		Variables->SetBoolField(TEXT("hasKeycard"), false);
		return Production;
	}

	FEpicodeSandboxEntity MakeSandboxEntity(const FString& InId, EEpicodeSandboxEntityKind InKind, bool bInVisible = true)
	{
		FEpicodeSandboxEntity Entity;
		Entity.Id = InId;
		Entity.Kind = InKind;
		Entity.ArchetypeId = LexToString(InKind);
		Entity.bVisible = bInVisible;
		return Entity;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeSandboxWorldTest, "Epicode.Sandbox.World", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeSandboxWorldTest::RunTest(const FString& Parameters)
{
	FEpicodeSandboxWorld World = FEpicodeSandboxWorld::Create(TEXT("session-1"), TEXT("production-1"), TEXT("scene-1"), 42);
	TestTrue(TEXT("world id"), !World.WorldId.IsEmpty());

	FEpicodeSandboxEntity EntityB = MakeSandboxEntity(TEXT("entity-b"), EEpicodeSandboxEntityKind::Prop);
	EntityB.Location = FVector(1, 2, 3);
	FString Error;
	TestTrue(TEXT("add entity b"), World.AddEntity(EntityB, Error));
	TestFalse(TEXT("duplicate entity"), World.AddEntity(EntityB, Error));
	TestTrue(TEXT("duplicate code"), Error.Contains(TEXT("ENTITY_EXISTS")));
	FEpicodeSandboxEntity EntityA = MakeSandboxEntity(TEXT("entity-a"), EEpicodeSandboxEntityKind::Character);
	TestTrue(TEXT("add entity a"), World.AddEntity(EntityA, Error));

	FEpicodeSandboxParticipant Participant;
	Participant.Id = TEXT("hero");
	Participant.Kind = EEpicodeSandboxParticipantKind::Performer;
	Participant.CharacterEntityId = EntityA.Id;
	TestTrue(TEXT("add participant"), World.AddParticipant(Participant, Error));
	TestFalse(TEXT("duplicate participant"), World.AddParticipant(Participant, Error));

	TestNotNull(TEXT("find entity"), World.FindEntity(TEXT("entity-a")));
	TestNotNull(TEXT("find participant"), World.FindParticipant(TEXT("hero")));
	TArray<FString> Ids = World.GetSortedEntityIds();
	TestEqual(TEXT("sorted ids"), Ids.Num(), 2);
	TestEqual(TEXT("sorted first"), Ids[0], FString(TEXT("entity-a")));
	TestEqual(TEXT("sorted second"), Ids[1], FString(TEXT("entity-b")));

	World.FindEntity(TEXT("entity-b"))->SetItemCount(TEXT("metal"), 3);
	TestEqual(TEXT("inventory"), World.FindEntity(TEXT("entity-b"))->GetItemCount(TEXT("metal")), 3);
	TestTrue(TEXT("add item"), World.FindEntity(TEXT("entity-b"))->AddItem(TEXT("metal"), 2, Error));
	TestEqual(TEXT("inventory after add"), World.FindEntity(TEXT("entity-b"))->GetItemCount(TEXT("metal")), 5);
	TestFalse(TEXT("negative inventory"), World.FindEntity(TEXT("entity-b"))->AddItem(TEXT("metal"), -6, Error));
	TestTrue(TEXT("negative error"), Error.Contains(TEXT("INVENTORY_NEGATIVE")));
	TestEqual(TEXT("inventory preserved"), World.FindEntity(TEXT("entity-b"))->GetItemCount(TEXT("metal")), 5);

	World.Narrative.SetVariable(TEXT("alarmLevel"), FEpicodeSandboxValue::MakeInt(2));
	World.Narrative.Objectives.Add(TEXT("escape"), true);
	World.Narrative.CompletedEvents.Add(TEXT("door-opened"));
	World.UnknownFields->SetStringField(TEXT("futureWorldField"), TEXT("kept"));

	TSharedPtr<FJsonObject> Json = World.ToJson();
	FEpicodeSandboxWorld Restored;
	TestTrue(TEXT("world roundtrip"), FEpicodeSandboxWorld::FromJson(Json, Restored, Error));
	TestEqual(TEXT("restored entities"), Restored.Entities.Num(), 2);
	TestEqual(TEXT("restored participant"), Restored.Participants.Num(), 1);
	TestEqual(TEXT("restored variable"), Restored.Narrative.FindVariable(TEXT("alarmLevel"))->AsInt(), static_cast<int64>(2));
	TestEqual(TEXT("restored unknown"), Restored.UnknownFields->GetStringField(TEXT("futureWorldField")), FString(TEXT("kept")));
	TestEqual(TEXT("digest stable"), FEpicodeSandboxDigest::ComputeWorldDigest(World), FEpicodeSandboxDigest::ComputeWorldDigest(Restored));

	TestTrue(TEXT("remove entity"), World.RemoveEntity(TEXT("entity-b"), Error));
	TestFalse(TEXT("remove missing"), World.RemoveEntity(TEXT("entity-b"), Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeSandboxTickTest, "Epicode.Sandbox.Tick", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeSandboxTickTest::RunTest(const FString& Parameters)
{
	FEpicodeSandboxSimulationClock Clock;
	Clock.Configure(30);
	TestEqual(TEXT("tick rate"), Clock.GetTickRate(), 30);
	TestEqual(TEXT("fixed delta"), Clock.GetFixedDeltaSeconds(), 1.0 / 30.0);

	TestEqual(TEXT("one tick"), Clock.Advance(1.0 / 30.0), 1);
	TestEqual(TEXT("tick value"), Clock.GetCurrentTick(), static_cast<int64>(1));
	TestEqual(TEXT("thirty ticks"), Clock.Advance(1.0, 30), 30);
	TestEqual(TEXT("tick monotonic"), Clock.GetCurrentTick(), static_cast<int64>(31));

	FEpicodeSandboxSimulationClock Backlog;
	Backlog.Configure(30);
	for (int32 Index = 0; Index < 5; ++Index)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetNumberField(TEXT("input"), Index);
		Backlog.Enqueue(EEpicodeSandboxCommandCategory::Input, Payload);
	}
	TestEqual(TEXT("pending inputs"), Backlog.GetPendingCount(), 5);
	TestEqual(TEXT("backlog tick"), Backlog.Advance(1.0 / 30.0), 1);
	TestEqual(TEXT("backlog drained"), Backlog.GetPendingCount(), 0);
	TestEqual(TEXT("backlog processed"), Backlog.GetLastTickCommands().Num(), 5);

	FEpicodeSandboxSimulationClock Ordered;
	Ordered.Configure(30);
	Ordered.Enqueue(EEpicodeSandboxCommandCategory::System);
	Ordered.Enqueue(EEpicodeSandboxCommandCategory::Projection);
	Ordered.Enqueue(EEpicodeSandboxCommandCategory::Input);
	Ordered.Enqueue(EEpicodeSandboxCommandCategory::Rule);
	Ordered.Advance(1.0 / 30.0);
	const TArray<FEpicodeSandboxQueuedCommand>& Commands = Ordered.GetLastTickCommands();
	TestEqual(TEXT("ordered count"), Commands.Num(), 4);
	TestEqual(TEXT("input first"), Commands[0].Category, EEpicodeSandboxCommandCategory::Input);
	TestEqual(TEXT("rule second"), Commands[1].Category, EEpicodeSandboxCommandCategory::Rule);
	TestEqual(TEXT("projection third"), Commands[2].Category, EEpicodeSandboxCommandCategory::Projection);
	TestEqual(TEXT("system fourth"), Commands[3].Category, EEpicodeSandboxCommandCategory::System);

	Ordered.Enqueue(EEpicodeSandboxCommandCategory::Input, nullptr, Ordered.GetCurrentTick() + 2);
	Ordered.Advance(1.0 / 30.0);
	TestEqual(TEXT("future command waits"), Ordered.GetLastTickCommands().Num(), 0);
	TestEqual(TEXT("future pending"), Ordered.GetPendingCount(), 1);
	Ordered.Advance(1.0 / 30.0);
	TestEqual(TEXT("future processed"), Ordered.GetLastTickCommands().Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeSandboxDeterminismTest, "Epicode.Sandbox.Determinism", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeSandboxDeterminismTest::RunTest(const FString& Parameters)
{
	FEpicodeSandboxRandomStreams First;
	First.Initialize(1234);
	TArray<int32> FirstValues;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		FirstValues.Add(First.GetInt(TEXT("loot"), 0, 1000000));
	}
	FEpicodeSandboxRandomStreams Second;
	Second.Initialize(1234);
	TArray<int32> SecondValues;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		SecondValues.Add(Second.GetInt(TEXT("loot"), 0, 1000000));
	}
	TestEqual(TEXT("same seed same values"), FirstValues, SecondValues);

	FEpicodeSandboxRandomStreams DifferentSeed;
	DifferentSeed.Initialize(4321);
	TArray<int32> DifferentValues;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		DifferentValues.Add(DifferentSeed.GetInt(TEXT("loot"), 0, 1000000));
	}
	TestNotEqual(TEXT("different seed differs"), FirstValues, DifferentValues);

	FEpicodeSandboxRandomStreams DomainA;
	DomainA.Initialize(7);
	const int32 A1 = DomainA.GetInt(TEXT("loot"), 0, 1000000);
	const int32 B1 = DomainA.GetInt(TEXT("npc"), 0, 1000000);
	FEpicodeSandboxRandomStreams DomainB;
	DomainB.Initialize(7);
	const int32 B2 = DomainB.GetInt(TEXT("npc"), 0, 1000000);
	const int32 A2 = DomainB.GetInt(TEXT("loot"), 0, 1000000);
	TestEqual(TEXT("loot domain independent"), A1, A2);
	TestEqual(TEXT("npc domain independent"), B1, B2);
	TestNotEqual(TEXT("domains isolated"), A1, B1);
	TestEqual(TEXT("draws recorded"), DomainA.GetDraws().Num(), 2);
	TestTrue(TEXT("draws json"), DomainA.GetDrawsJson()->GetArrayField(TEXT("draws")).Num() == 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeSandboxDigestTest, "Epicode.Sandbox.Digest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeSandboxDigestTest::RunTest(const FString& Parameters)
{
	FEpicodeSandboxWorld First = FEpicodeSandboxWorld::Create(TEXT("session"), TEXT("production"), TEXT("scene"), 9);
	FEpicodeSandboxWorld Second = First;
	FString Error;
	TestTrue(TEXT("add first"), First.AddEntity(MakeSandboxEntity(TEXT("crate"), EEpicodeSandboxEntityKind::Prop), Error));
	TestTrue(TEXT("add second"), Second.AddEntity(MakeSandboxEntity(TEXT("crate"), EEpicodeSandboxEntityKind::Prop), Error));
	TestEqual(TEXT("same state same digest"), FEpicodeSandboxDigest::ComputeWorldDigest(First), FEpicodeSandboxDigest::ComputeWorldDigest(Second));

	First.FindEntity(TEXT("crate"))->Location.X = 99.0;
	TestNotEqual(TEXT("transform changes digest"), FEpicodeSandboxDigest::ComputeWorldDigest(First), FEpicodeSandboxDigest::ComputeWorldDigest(Second));
	Second.FindEntity(TEXT("crate"))->Location.X = 99.0;
	TestEqual(TEXT("same transform"), FEpicodeSandboxDigest::ComputeWorldDigest(First), FEpicodeSandboxDigest::ComputeWorldDigest(Second));

	First.Narrative.SetVariable(TEXT("alarmLevel"), FEpicodeSandboxValue::MakeInt(1));
	TestNotEqual(TEXT("narrative changes digest"), FEpicodeSandboxDigest::ComputeWorldDigest(First), FEpicodeSandboxDigest::ComputeWorldDigest(Second));

	TArray<FEpicodeSandboxEvent> Events;
	FEpicodeSandboxEvent A;
	A.Sequence = 1;
	A.Tick = 1;
	A.Type = TEXT("sandbox.event");
	FEpicodeSandboxEvent B;
	B.Sequence = 2;
	B.Tick = 1;
	B.Type = TEXT("sandbox.event");
	Events.Add(A);
	Events.Add(B);
	TArray<FEpicodeSandboxEvent> Reversed;
	Reversed.Add(B);
	Reversed.Add(A);
	TestEqual(TEXT("same event order same digest"), FEpicodeSandboxDigest::ComputeEventDigest(Events), FEpicodeSandboxDigest::ComputeEventDigest(Events));
	TestNotEqual(TEXT("event order changes digest"), FEpicodeSandboxDigest::ComputeEventDigest(Events), FEpicodeSandboxDigest::ComputeEventDigest(Reversed));
	TestTrue(TEXT("combined digest"), !FEpicodeSandboxDigest::ComputeCombinedDigest(First, Events).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeSandboxProjectionBoundaryTest, "Epicode.Sandbox.ProjectionBoundary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeSandboxProjectionBoundaryTest::RunTest(const FString& Parameters)
{
	FEpicodeSandboxWorld World = FEpicodeSandboxWorld::Create(TEXT("session"), TEXT("production"), TEXT("scene"), 1);
	FString Error;
	World.AddEntity(MakeSandboxEntity(TEXT("hero"), EEpicodeSandboxEntityKind::Character), Error);
	World.AddEntity(MakeSandboxEntity(TEXT("trigger"), EEpicodeSandboxEntityKind::Trigger), Error);
	World.AddEntity(MakeSandboxEntity(TEXT("hidden"), EEpicodeSandboxEntityKind::Prop, false), Error);
	World.AddEntity(MakeSandboxEntity(TEXT("light"), EEpicodeSandboxEntityKind::Light), Error);

	TArray<FEpicodeSandboxProjectionCommand> Commands = FEpicodeSandboxProjection::Build(World);
	TestEqual(TEXT("projection count"), Commands.Num(), 2);
	TestEqual(TEXT("projection first"), Commands[0].EntityId, FString(TEXT("hero")));
	TestEqual(TEXT("projection second"), Commands[1].EntityId, FString(TEXT("light")));
	TestFalse(TEXT("trigger not projectable"), FEpicodeSandboxProjection::IsProjectable(EEpicodeSandboxEntityKind::Trigger));
	TestTrue(TEXT("light projectable"), FEpicodeSandboxProjection::IsProjectable(EEpicodeSandboxEntityKind::Light));

	const FString SourceRoot = FPaths::Combine(FPaths::ProjectDir(), TEXT("Plugins/EpicodeSandbox/Source/EpicodeSandbox"));
	const TArray<FString> CoreFiles = {
		TEXT("Public/EpicodeSandboxTypes.h"),
		TEXT("Public/EpicodeSandboxWorld.h"),
		TEXT("Public/EpicodeSandboxClock.h"),
		TEXT("Public/EpicodeSandboxRandom.h"),
		TEXT("Public/EpicodeSandboxProjection.h"),
		TEXT("Private/EpicodeSandboxTypes.cpp"),
		TEXT("Private/EpicodeSandboxWorld.cpp"),
		TEXT("Private/EpicodeSandboxClock.cpp"),
		TEXT("Private/EpicodeSandboxRandom.cpp"),
		TEXT("Private/EpicodeSandboxProjection.cpp")
	};
	for (const FString& File : CoreFiles)
	{
		FString Contents;
		const FString Path = FPaths::Combine(SourceRoot, File);
		TestTrue(TEXT("core source exists"), FFileHelper::LoadFileToString(Contents, *Path));
		TestFalse(TEXT("no Actor include"), Contents.Contains(TEXT("GameFramework/Actor.h")));
		TestFalse(TEXT("no Actor class"), Contents.Contains(TEXT("AActor")));
		TestFalse(TEXT("no Slate include"), Contents.Contains(TEXT("Widgets/")));
		TestFalse(TEXT("no Slate type"), Contents.Contains(TEXT("SCompoundWidget")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeSandboxLifecycleTest, "Epicode.Sandbox.Lifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeSandboxLifecycleTest::RunTest(const FString& Parameters)
{
	const FSandboxTestScene TestScene = MakeSandboxScene();
	const FEpicodeProductionDocument Production = MakeSandboxProduction(TestScene);
	FEpicodeSandboxSessionConfig Config;
	Config.SessionId = TEXT("session-lifecycle");
	Config.Seed = 123;
	Config.TickRate = 30;

	FEpicodeSandboxSession Session;
	FString Error;
	TestTrue(TEXT("load"), Session.Load(Production, TestScene.Scene, Config, Error));
	TestEqual(TEXT("ready state"), Session.GetState(), EEpicodeSandboxSessionState::Ready);
	TestEqual(TEXT("world entities"), Session.GetWorld().Entities.Num(), 3);
	TestEqual(TEXT("world participants"), Session.GetWorld().Participants.Num(), 1);
	TestEqual(TEXT("current beat"), Session.GetWorld().Narrative.CurrentBeatId, FString(TEXT("beat-1")));
	TestEqual(TEXT("alarm variable"), Session.GetWorld().Narrative.FindVariable(TEXT("alarmLevel"))->AsInt(), static_cast<int64>(0));
	TestEqual(TEXT("loaded event"), Session.GetEvents().GetLastSequence(), static_cast<int64>(1));
	TestEqual(TEXT("loaded event type"), Session.GetEvents().GetEvents()[0].Type, FString(TEXT("sandbox.loaded")));
	TestTrue(TEXT("digest"), !Session.GetDigest().IsEmpty());
	TestTrue(TEXT("projection"), FEpicodeSandboxProjection::Build(Session.GetWorld()).Num() == 3);

	TestTrue(TEXT("start"), Session.Start(Error));
	TestTrue(TEXT("running"), Session.IsRunning());
	TestEqual(TEXT("tick one"), Session.Tick(1.0 / 30.0), 1);
	TestEqual(TEXT("world tick"), Session.GetWorld().Tick, static_cast<int64>(1));
	TestEqual(TEXT("tick many"), Session.Tick(1.0), 8);
	TestTrue(TEXT("tick monotonic"), Session.GetWorld().Tick > 1);
	TestTrue(TEXT("stop"), Session.Stop(Error));
	TestFalse(TEXT("not running"), Session.IsRunning());

	TestTrue(TEXT("reset"), Session.Reset(Error));
	TestEqual(TEXT("reset tick"), Session.GetWorld().Tick, static_cast<int64>(0));
	TestEqual(TEXT("reset state"), Session.GetState(), EEpicodeSandboxSessionState::Ready);
	TestEqual(TEXT("reset events"), Session.GetEvents().GetLastSequence(), static_cast<int64>(2));
	TestEqual(TEXT("reset event type"), Session.GetEvents().GetEvents().Last().Type, FString(TEXT("sandbox.reset")));

	TestTrue(TEXT("close"), Session.Close(Error));
	TestEqual(TEXT("closed state"), Session.GetState(), EEpicodeSandboxSessionState::Closed);
	TestTrue(TEXT("close idempotent"), Session.Close(Error));
	TestFalse(TEXT("start closed"), Session.Start(Error));

	FEpicodeProductionDocument Invalid = Production;
	Invalid.Cast[0].EntityId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	FEpicodeSandboxSession InvalidSession;
	TestFalse(TEXT("invalid load"), InvalidSession.Load(Invalid, TestScene.Scene, Config, Error));
	TestEqual(TEXT("invalid state"), InvalidSession.GetState(), EEpicodeSandboxSessionState::Failed);
	TestTrue(TEXT("invalid error"), Error.Contains(TEXT("PRODUCTION_REFERENCE_NOT_FOUND")));

	FEpicodeSandboxSession LegacySession;
	TestTrue(TEXT("legacy load"), LegacySession.LoadLegacy(TestScene.Scene, Config, Error));
	TestEqual(TEXT("legacy entities"), LegacySession.GetWorld().Entities.Num(), 3);
	TestEqual(TEXT("legacy participants"), LegacySession.GetWorld().Participants.Num(), 1);
	return true;
}
