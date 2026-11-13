#include "Misc/AutomationTest.h"

#include "EpicodeProductionDocument.h"
#include "EpicodeProductionLibrary.h"
#include "EpicodeSandboxGameplay.h"
#include "EpicodeSandboxPersistence.h"
#include "EpicodeSandboxRules.h"
#include "EpicodeSandboxSession.h"
#include "EpicodeSceneDocument.h"
#include "EpicodeSceneLibrary.h"
#include "EpicodeTypes.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	TSharedPtr<FJsonObject> BuildGameplayRulesJson()
	{
		TSharedPtr<FJsonObject> Rules = MakeShared<FJsonObject>();

		TSharedPtr<FJsonObject> CrateInteraction = MakeShared<FJsonObject>();
		CrateInteraction->SetStringField(TEXT("id"), TEXT("crate-use"));
		CrateInteraction->SetStringField(TEXT("entityId"), TEXT("crate"));
		CrateInteraction->SetArrayField(TEXT("verbs"), {MakeShared<FJsonValueString>(TEXT("use"))});
		CrateInteraction->SetNumberField(TEXT("maxDistance"), 150);
		CrateInteraction->SetNumberField(TEXT("cooldownSeconds"), 1);
		CrateInteraction->SetArrayField(TEXT("outcomes"), {
			MakeShared<FJsonValueString>(TEXT("setVariable:alarmLevel=1")),
			MakeShared<FJsonValueString>(TEXT("addItem:keycard=1"))
		});

		TSharedPtr<FJsonObject> LockedInteraction = MakeShared<FJsonObject>();
		LockedInteraction->SetStringField(TEXT("id"), TEXT("locked-use"));
		LockedInteraction->SetStringField(TEXT("entityId"), TEXT("locked"));
		LockedInteraction->SetArrayField(TEXT("verbs"), {MakeShared<FJsonValueString>(TEXT("use"))});
		LockedInteraction->SetNumberField(TEXT("maxDistance"), 150);
		LockedInteraction->SetArrayField(TEXT("conditions"), {MakeShared<FJsonValueString>(TEXT("alarmLevel == 1"))});
		LockedInteraction->SetArrayField(TEXT("outcomes"), {MakeShared<FJsonValueString>(TEXT("addItem:keycard=1"))});
		Rules->SetArrayField(TEXT("interactionPoints"), {
			MakeShared<FJsonValueObject>(CrateInteraction),
			MakeShared<FJsonValueObject>(LockedInteraction)
		});

		TSharedPtr<FJsonObject> Zone = MakeShared<FJsonObject>();
		Zone->SetStringField(TEXT("id"), TEXT("roof"));
		Zone->SetStringField(TEXT("name"), TEXT("Roof"));
		Zone->SetNumberField(TEXT("maxEntities"), 5);
		Zone->SetArrayField(TEXT("center"), {MakeShared<FJsonValueNumber>(0), MakeShared<FJsonValueNumber>(0), MakeShared<FJsonValueNumber>(0)});
		Zone->SetArrayField(TEXT("extent"), {MakeShared<FJsonValueNumber>(100), MakeShared<FJsonValueNumber>(100), MakeShared<FJsonValueNumber>(10)});
		Rules->SetArrayField(TEXT("zones"), {MakeShared<FJsonValueObject>(Zone)});

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("entityType"), TEXT("Prop"));
		Entry->SetStringField(TEXT("archetypeId"), TEXT("LootCrate"));
		Entry->SetNumberField(TEXT("weight"), 1);
		TSharedPtr<FJsonObject> Table = MakeShared<FJsonObject>();
		Table->SetStringField(TEXT("id"), TEXT("loot-table"));
		Table->SetArrayField(TEXT("entries"), {MakeShared<FJsonValueObject>(Entry)});
		Rules->SetArrayField(TEXT("spawnTables"), {MakeShared<FJsonValueObject>(Table)});

		TSharedPtr<FJsonObject> Spawner = MakeShared<FJsonObject>();
		Spawner->SetStringField(TEXT("id"), TEXT("loot-spawner"));
		Spawner->SetStringField(TEXT("zoneId"), TEXT("roof"));
		Spawner->SetStringField(TEXT("spawnTableId"), TEXT("loot-table"));
		Spawner->SetNumberField(TEXT("intervalSeconds"), 1);
		Spawner->SetNumberField(TEXT("maxAlive"), 1);
		Rules->SetArrayField(TEXT("spawners"), {MakeShared<FJsonValueObject>(Spawner)});

		TSharedPtr<FJsonObject> Resource = MakeShared<FJsonObject>();
		Resource->SetStringField(TEXT("id"), TEXT("metal-node"));
		Resource->SetStringField(TEXT("entityId"), TEXT("node"));
		Resource->SetStringField(TEXT("itemId"), TEXT("metal"));
		Resource->SetNumberField(TEXT("capacity"), 2);
		Resource->SetNumberField(TEXT("respawnSeconds"), 1);
		Rules->SetArrayField(TEXT("resources"), {MakeShared<FJsonValueObject>(Resource)});

		TSharedPtr<FJsonObject> Profile = MakeShared<FJsonObject>();
		Profile->SetStringField(TEXT("id"), TEXT("guard"));
		Profile->SetStringField(TEXT("behavior"), TEXT("patrol"));
		Profile->SetNumberField(TEXT("perceptionRadius"), 300);
		Profile->SetNumberField(TEXT("speed"), 100);
		Profile->SetArrayField(TEXT("patrolPoints"), {
			MakeShared<FJsonValueObject>([]()
			{
				TSharedPtr<FJsonObject> Point = MakeShared<FJsonObject>();
				Point->SetArrayField(TEXT("location"), {MakeShared<FJsonValueNumber>(0), MakeShared<FJsonValueNumber>(0), MakeShared<FJsonValueNumber>(0)});
				return Point;
			}())
		});
		Rules->SetArrayField(TEXT("npcProfiles"), {MakeShared<FJsonValueObject>(Profile)});

		TSharedPtr<FJsonObject> Recipe = MakeShared<FJsonObject>();
		Recipe->SetStringField(TEXT("id"), TEXT("make-key"));
		TSharedPtr<FJsonObject> Inputs = MakeShared<FJsonObject>();
		Inputs->SetNumberField(TEXT("metal"), 2);
		Recipe->SetObjectField(TEXT("inputs"), Inputs);
		TSharedPtr<FJsonObject> Outputs = MakeShared<FJsonObject>();
		Outputs->SetNumberField(TEXT("key"), 1);
		Recipe->SetObjectField(TEXT("outputs"), Outputs);
		Rules->SetArrayField(TEXT("recipes"), {MakeShared<FJsonValueObject>(Recipe)});

		TSharedPtr<FJsonObject> Budgets = MakeShared<FJsonObject>();
		Budgets->SetNumberField(TEXT("maxEntities"), 10);
		Budgets->SetNumberField(TEXT("maxEventsPerSecond"), 1000);
		Budgets->SetNumberField(TEXT("maxSpawnPerSecond"), 10);
		Budgets->SetNumberField(TEXT("maxDirectorCues"), 100);
		Rules->SetObjectField(TEXT("budgets"), Budgets);
		return Rules;
	}

	struct FGameplayFixture
	{
		FEpicodeSandboxWorld World;
		FEpicodeSandboxRandomStreams Random;
		FEpicodeSandboxEventStream Events;
		FEpicodeSandboxRules Rules;
		FEpicodeSandboxGameplay Gameplay;

		bool Init(int32 InSeed, FString& OutError)
		{
			World = FEpicodeSandboxWorld::Create(TEXT("gameplay-session"), TEXT("gameplay-production"), TEXT("gameplay-scene"), InSeed);
			Random.Initialize(InSeed);
			Events.Reset(1);

			FEpicodeSandboxEntity Hero;
			Hero.Id = TEXT("hero");
			Hero.Kind = EEpicodeSandboxEntityKind::Character;
			Hero.Location = FVector::ZeroVector;
			Hero.Data = MakeShared<FJsonObject>();
			Hero.Data->SetArrayField(TEXT("tags"), {MakeShared<FJsonValueString>(TEXT("hero"))});
			if (!World.AddEntity(Hero, OutError)) return false;

			FEpicodeSandboxEntity Crate;
			Crate.Id = TEXT("crate");
			Crate.Kind = EEpicodeSandboxEntityKind::Prop;
			Crate.Location = FVector(100, 0, 0);
			if (!World.AddEntity(Crate, OutError)) return false;

			FEpicodeSandboxEntity Locked;
			Locked.Id = TEXT("locked");
			Locked.Kind = EEpicodeSandboxEntityKind::Prop;
			Locked.Location = FVector(100, 0, 0);
			if (!World.AddEntity(Locked, OutError)) return false;

			FEpicodeSandboxEntity Node;
			Node.Id = TEXT("node");
			Node.Kind = EEpicodeSandboxEntityKind::Prop;
			Node.Location = FVector(50, 0, 0);
			if (!World.AddEntity(Node, OutError)) return false;

			FEpicodeSandboxEntity Npc;
			Npc.Id = TEXT("npc");
			Npc.Kind = EEpicodeSandboxEntityKind::Npc;
			Npc.Location = FVector(200, 0, 0);
			Npc.Data = MakeShared<FJsonObject>();
			Npc.Data->SetStringField(TEXT("npcProfileId"), TEXT("guard"));
			if (!World.AddEntity(Npc, OutError)) return false;

			FEpicodeSandboxParticipant HeroParticipant;
			HeroParticipant.Id = TEXT("hero");
			HeroParticipant.Kind = EEpicodeSandboxParticipantKind::Performer;
			HeroParticipant.CharacterEntityId = TEXT("hero");
			if (!World.AddParticipant(HeroParticipant, OutError)) return false;

			FEpicodeSandboxParticipant Viewer;
			Viewer.Id = TEXT("watcher");
			Viewer.Kind = EEpicodeSandboxParticipantKind::Viewer;
			Viewer.CharacterEntityId = TEXT("hero");
			if (!World.AddParticipant(Viewer, OutError)) return false;

			FEpicodeSandboxParticipant Owner;
			Owner.Id = TEXT("owner");
			Owner.Kind = EEpicodeSandboxParticipantKind::Owner;
			if (!World.AddParticipant(Owner, OutError)) return false;

			FEpicodeSandboxParticipant NpcParticipant;
			NpcParticipant.Id = TEXT("npc-owner");
			NpcParticipant.Kind = EEpicodeSandboxParticipantKind::Npc;
			NpcParticipant.CharacterEntityId = TEXT("npc");
			if (!World.AddParticipant(NpcParticipant, OutError)) return false;

			if (!FEpicodeSandboxRules::Parse(BuildGameplayRulesJson(), Rules, OutError))
			{
				return false;
			}
			return Gameplay.Initialize(Rules, World, Random, Events, 30, OutError);
		}
	};

	struct FSessionFixture
	{
		FEpicodeSceneDocument Scene;
		FEpicodeProductionDocument Production;
		FGuid HeroId;

		FSessionFixture()
		{
			Scene = FEpicodeSceneDocument::CreateDefault(TEXT("Persistence Scene"));
			HeroId = FGuid::NewGuid();
			FEpicodeEntityRecord Hero;
			Hero.Id = HeroId;
			Hero.Type = EEpicodeEntityType::Character;
			Hero.Name = TEXT("Hero");
			Scene.Entities.Add(Hero);
			Production = FEpicodeProductionDocument::CreateDefault(Scene.SceneId, TEXT("Persistence Production"));
			FEpicodeProductionCastMember Member;
			Member.Slot = TEXT("hero");
			Member.EntityId = HeroId.ToString(EGuidFormats::DigitsWithHyphens);
			Member.ParticipantKind = TEXT("performer");
			Production.Cast.Add(Member);
		}
	};

	FString MakeGameplayTempDirectory(const FString& InName)
	{
		const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Epicode"), TEXT("GameplayTests"), InName + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
		IFileManager::Get().MakeDirectory(*Directory, true);
		return Directory;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeSandboxRulesTest, "Epicode.Sandbox.Rules", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeSandboxRulesTest::RunTest(const FString& Parameters)
{
	FGameplayFixture Fixture;
	FString Error;
	TestTrue(TEXT("fixture"), Fixture.Init(1, Error));
	TestEqual(TEXT("interactions parsed"), Fixture.Rules.Interactions.Num(), 2);
	TestEqual(TEXT("zones parsed"), Fixture.Rules.Zones.Num(), 1);
	TestEqual(TEXT("spawners parsed"), Fixture.Rules.Spawners.Num(), 1);
	TestEqual(TEXT("resources parsed"), Fixture.Rules.Resources.Num(), 1);
	TestEqual(TEXT("profiles parsed"), Fixture.Rules.NpcProfiles.Num(), 1);
	TestEqual(TEXT("recipes parsed"), Fixture.Rules.Recipes.Num(), 1);
	TestTrue(TEXT("rules validate"), Fixture.Rules.Validate(&Fixture.World, Error));

	FEpicodeSandboxCondition Condition;
	TestTrue(TEXT("condition parse"), FEpicodeSandboxCondition::Parse(TEXT("alarmLevel == 1"), Condition, Error));
	FEpicodeSandboxNarrativeState Narrative;
	Narrative.SetVariable(TEXT("alarmLevel"), FEpicodeSandboxValue::MakeInt(1));
	TestTrue(TEXT("condition true"), Condition.Evaluate(Narrative));
	Narrative.SetVariable(TEXT("alarmLevel"), FEpicodeSandboxValue::MakeInt(0));
	TestFalse(TEXT("condition false"), Condition.Evaluate(Narrative));

	FEpicodeSandboxOutcome Outcome;
	TestTrue(TEXT("outcome parse"), FEpicodeSandboxOutcome::Parse(TEXT("addItem:metal=2"), Outcome, Error));
	TestEqual(TEXT("outcome action"), Outcome.Action, FString(TEXT("addItem")));
	TestEqual(TEXT("outcome key"), Outcome.Key, FString(TEXT("metal")));
	TestEqual(TEXT("outcome value"), Outcome.Value.AsInt(), static_cast<int64>(2));

	TSharedPtr<FJsonObject> DuplicateJson = BuildGameplayRulesJson();
	TSharedPtr<FJsonObject> DuplicateZone = MakeShared<FJsonObject>();
	DuplicateZone->SetStringField(TEXT("id"), TEXT("crate-use"));
	DuplicateJson->SetArrayField(TEXT("zones"), {MakeShared<FJsonValueObject>(DuplicateZone)});
	FEpicodeSandboxRules DuplicateRules;
	TestTrue(TEXT("duplicate parses"), FEpicodeSandboxRules::Parse(DuplicateJson, DuplicateRules, Error));
	TestFalse(TEXT("duplicate rejected"), DuplicateRules.Validate(nullptr, Error));
	TestTrue(TEXT("duplicate code"), Error.Contains(TEXT("DUPLICATE_RULE_ID")));

	TSharedPtr<FJsonObject> PathJson = BuildGameplayRulesJson();
	PathJson->GetArrayField(TEXT("spawnTables"))[0]->AsObject()->GetArrayField(TEXT("entries"))[0]->AsObject()->SetStringField(TEXT("archetypeId"), TEXT("../escape"));
	FEpicodeSandboxRules PathRules;
	TestTrue(TEXT("path parses"), FEpicodeSandboxRules::Parse(PathJson, PathRules, Error));
	TestFalse(TEXT("path rejected"), PathRules.Validate(nullptr, Error));
	TestTrue(TEXT("path code"), Error.Contains(TEXT("PATH_NOT_ALLOWED")));

	TSharedPtr<FJsonObject> BudgetJson = BuildGameplayRulesJson();
	BudgetJson->GetObjectField(TEXT("budgets"))->SetNumberField(TEXT("maxEntities"), 0);
	FEpicodeSandboxRules BudgetRules;
	TestTrue(TEXT("budget parses"), FEpicodeSandboxRules::Parse(BudgetJson, BudgetRules, Error));
	TestFalse(TEXT("bad budget rejected"), BudgetRules.Validate(nullptr, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeSandboxInteractionsTest, "Epicode.Sandbox.Interactions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeSandboxInteractionsTest::RunTest(const FString& Parameters)
{
	FGameplayFixture Fixture;
	FString Error;
	TestTrue(TEXT("fixture"), Fixture.Init(2, Error));
	TestTrue(TEXT("interaction"), Fixture.Gameplay.ExecuteInteraction(TEXT("hero"), TEXT("crate"), TEXT("use"), Error));
	TestEqual(TEXT("variable set"), Fixture.World.Narrative.FindVariable(TEXT("alarmLevel"))->AsInt(), static_cast<int64>(1));
	TestEqual(TEXT("item added"), Fixture.World.FindEntity(TEXT("hero"))->GetItemCount(TEXT("keycard")), 1);
	TestEqual(TEXT("event"), Fixture.Events.GetEvents().Last().Type, FString(TEXT("sandbox.interaction")));

	TestFalse(TEXT("cooldown rejected"), Fixture.Gameplay.ExecuteInteraction(TEXT("hero"), TEXT("crate"), TEXT("use"), Error));
	TestTrue(TEXT("cooldown code"), Error.Contains(TEXT("INTERACTION_COOLDOWN")));
	Fixture.World.Tick = 30;
	TestTrue(TEXT("cooldown expires"), Fixture.Gameplay.ExecuteInteraction(TEXT("hero"), TEXT("crate"), TEXT("use"), Error));
	TestEqual(TEXT("item added twice"), Fixture.World.FindEntity(TEXT("hero"))->GetItemCount(TEXT("keycard")), 2);

	TestFalse(TEXT("unknown verb"), Fixture.Gameplay.ExecuteInteraction(TEXT("hero"), TEXT("crate"), TEXT("open"), Error));
	TestTrue(TEXT("unknown verb code"), Error.Contains(TEXT("INTERACTION_NOT_ALLOWED")));

	Fixture.World.FindEntity(TEXT("crate"))->Location = FVector(300, 0, 0);
	TestFalse(TEXT("out of range"), Fixture.Gameplay.ExecuteInteraction(TEXT("hero"), TEXT("crate"), TEXT("use"), Error));
	TestTrue(TEXT("range code"), Error.Contains(TEXT("INTERACTION_OUT_OF_RANGE")));

	Fixture.World.Narrative.SetVariable(TEXT("alarmLevel"), FEpicodeSandboxValue::MakeInt(0));
	TestFalse(TEXT("condition fails"), Fixture.Gameplay.ExecuteInteraction(TEXT("hero"), TEXT("locked"), TEXT("use"), Error));
	TestTrue(TEXT("condition code"), Error.Contains(TEXT("INTERACTION_CONDITION_FAILED")));

	TestFalse(TEXT("viewer read only"), Fixture.Gameplay.ExecuteInteraction(TEXT("watcher"), TEXT("crate"), TEXT("use"), Error));
	TestTrue(TEXT("viewer code"), Error.Contains(TEXT("VIEWER_READ_ONLY")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeSandboxSpawnersTest, "Epicode.Sandbox.Spawners", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeSandboxSpawnersTest::RunTest(const FString& Parameters)
{
	FGameplayFixture Fixture;
	FString Error;
	TestTrue(TEXT("fixture"), Fixture.Init(3, Error));
	Fixture.World.Tick = 30;
	TestEqual(TEXT("spawned"), Fixture.Gameplay.TickSpawners(30, Error), 1);
	TestEqual(TEXT("budget report"), Fixture.Gameplay.GetBudgetReport()->GetIntegerField(TEXT("entityCount")), Fixture.World.Entities.Num());
	TestEqual(TEXT("spawner alive"), Fixture.Gameplay.GetBudgetReport()->GetArrayField(TEXT("spawners"))[0]->AsObject()->GetIntegerField(TEXT("alive")), 1);

	FString BudgetError;
	TestEqual(TEXT("max alive blocks"), Fixture.Gameplay.TickSpawners(60, BudgetError), 0);
	TestTrue(TEXT("max alive code"), BudgetError.Contains(TEXT("SPAWN_BUDGET_EXCEEDED")));
	TestTrue(TEXT("spawn budget event"), Fixture.Events.GetEvents().Last().Type == FString(TEXT("sandbox.spawnBudgetExceeded")));

	TestFalse(TEXT("global budget"), Fixture.Gameplay.CanSpawn(100, BudgetError));
	TestTrue(TEXT("global budget code"), BudgetError.Contains(TEXT("SPAWN_BUDGET_EXCEEDED")));

	FGameplayFixture Other;
	TestTrue(TEXT("other fixture"), Other.Init(3, Error));
	Other.World.Tick = 30;
	TestEqual(TEXT("other spawned"), Other.Gameplay.TickSpawners(30, Error), 1);
	const FString FirstSpawnId = Fixture.World.GetSortedEntityIds().Last();
	const FString SecondSpawnId = Other.World.GetSortedEntityIds().Last();
	TestEqual(TEXT("deterministic spawn id"), FirstSpawnId, SecondSpawnId);
	const FEpicodeSandboxEntity* FirstSpawn = Fixture.World.FindEntity(FirstSpawnId);
	const FEpicodeSandboxEntity* SecondSpawn = Other.World.FindEntity(SecondSpawnId);
	TestTrue(TEXT("deterministic spawn location"), FirstSpawn->Location.Equals(SecondSpawn->Location, 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeSandboxNpcTest, "Epicode.Sandbox.NPC", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeSandboxNpcTest::RunTest(const FString& Parameters)
{
	FGameplayFixture Fixture;
	FString Error;
	TestTrue(TEXT("fixture"), Fixture.Init(4, Error));
	TestEqual(TEXT("npc action"), Fixture.Gameplay.TickNpcs(1, 1.0, Error), 1);
	FEpicodeSandboxEntity* Npc = Fixture.World.FindEntity(TEXT("npc"));
	TestEqual(TEXT("npc moved"), Npc->Location.X, 100.0);
	FString Target;
	TestTrue(TEXT("npc target"), Npc->Data->TryGetStringField(TEXT("npcTargetId"), Target));
	TestEqual(TEXT("npc target id"), Target, FString(TEXT("hero")));

	Fixture.World.FindEntity(TEXT("hero"))->Location = FVector(1000, 0, 0);
	Fixture.Gameplay.TickNpcs(2, 1.0, Error);
	TestFalse(TEXT("target lost"), Fixture.World.FindEntity(TEXT("npc"))->Data->HasField(TEXT("npcTargetId")));
	bool bTargetLostEvent = false;
	for (const FEpicodeSandboxEvent& Event : Fixture.Events.GetEvents())
	{
		if (Event.Type == TEXT("sandbox.npcTargetLost"))
		{
			bTargetLostEvent = true;
		}
	}
	TestTrue(TEXT("target lost event"), bTargetLostEvent);

	FEpicodeSandboxEntity* TakenNpc = Fixture.World.FindEntity(TEXT("npc"));
	TakenNpc->Location = FVector(200, 0, 0);
	Fixture.World.FindParticipant(TEXT("npc-owner"))->bTakeover = true;
	TestEqual(TEXT("takeover pauses npc"), Fixture.Gameplay.TickNpcs(3, 1.0, Error), 0);
	TestEqual(TEXT("takeover location"), TakenNpc->Location.X, 200.0);
	Fixture.World.FindParticipant(TEXT("npc-owner"))->bTakeover = false;
	TestEqual(TEXT("takeover released"), Fixture.Gameplay.TickNpcs(4, 1.0, Error), 1);

	FEpicodeSandboxRules NoProfiles = Fixture.Rules;
	NoProfiles.NpcProfiles.Reset();
	FEpicodeSandboxGameplay Fallback;
	TestTrue(TEXT("fallback init"), Fallback.Initialize(NoProfiles, Fixture.World, Fixture.Random, Fixture.Events, 30, Error));
	TestEqual(TEXT("fallback idle"), Fallback.TickNpcs(3, 1.0, Error), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeSandboxResourcesTest, "Epicode.Sandbox.Resources", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeSandboxResourcesTest::RunTest(const FString& Parameters)
{
	FGameplayFixture Fixture;
	FString Error;
	TestTrue(TEXT("fixture"), Fixture.Init(5, Error));
	TestTrue(TEXT("collect one"), Fixture.Gameplay.Collect(TEXT("hero"), TEXT("node"), 1, Error));
	TestEqual(TEXT("metal one"), Fixture.World.FindEntity(TEXT("hero"))->GetItemCount(TEXT("metal")), 1);
	TestTrue(TEXT("collect two"), Fixture.Gameplay.Collect(TEXT("hero"), TEXT("node"), 1, Error));
	TestEqual(TEXT("metal two"), Fixture.World.FindEntity(TEXT("hero"))->GetItemCount(TEXT("metal")), 2);
	TestEqual(TEXT("remaining zero"), Fixture.Gameplay.GetResourceStates().FindRef(TEXT("metal-node")).Remaining, 0);
	TestFalse(TEXT("depleted"), Fixture.Gameplay.Collect(TEXT("hero"), TEXT("node"), 1, Error));
	TestTrue(TEXT("depleted code"), Error.Contains(TEXT("RESOURCE_DEPLETED")));
	TestEqual(TEXT("metal unchanged"), Fixture.World.FindEntity(TEXT("hero"))->GetItemCount(TEXT("metal")), 2);

	TestTrue(TEXT("craft"), Fixture.Gameplay.Craft(TEXT("hero"), TEXT("make-key"), Error));
	TestEqual(TEXT("metal consumed"), Fixture.World.FindEntity(TEXT("hero"))->GetItemCount(TEXT("metal")), 0);
	TestEqual(TEXT("key crafted"), Fixture.World.FindEntity(TEXT("hero"))->GetItemCount(TEXT("key")), 1);
	TestFalse(TEXT("craft insufficient"), Fixture.Gameplay.Craft(TEXT("hero"), TEXT("make-key"), Error));
	TestTrue(TEXT("craft code"), Error.Contains(TEXT("INSUFFICIENT_RESOURCES")));
	TestEqual(TEXT("key unchanged"), Fixture.World.FindEntity(TEXT("hero"))->GetItemCount(TEXT("key")), 1);
	TestEqual(TEXT("metal unchanged"), Fixture.World.FindEntity(TEXT("hero"))->GetItemCount(TEXT("metal")), 0);

	TestTrue(TEXT("consume"), Fixture.Gameplay.ConsumeItem(TEXT("hero"), TEXT("key"), 1, Error));
	TestEqual(TEXT("key consumed"), Fixture.World.FindEntity(TEXT("hero"))->GetItemCount(TEXT("key")), 0);
	TestFalse(TEXT("consume insufficient"), Fixture.Gameplay.ConsumeItem(TEXT("hero"), TEXT("key"), 1, Error));
	TestTrue(TEXT("consume code"), Error.Contains(TEXT("INSUFFICIENT_RESOURCES")));

	Fixture.Gameplay.TickResources(30, Error);
	TestEqual(TEXT("resource respawn"), Fixture.Gameplay.GetResourceStates().FindRef(TEXT("metal-node")).Remaining, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeSandboxPermissionsTest, "Epicode.Sandbox.Permissions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeSandboxPermissionsTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("viewer cannot interact"), FEpicodeSandboxGameplay::CanPerform(EEpicodeSandboxParticipantKind::Viewer, TEXT("interact")));
	TestTrue(TEXT("performer can interact"), FEpicodeSandboxGameplay::CanPerform(EEpicodeSandboxParticipantKind::Performer, TEXT("interact")));
	TestTrue(TEXT("director can spawn"), FEpicodeSandboxGameplay::CanPerform(EEpicodeSandboxParticipantKind::Director, TEXT("spawn")));
	TestFalse(TEXT("director cannot manage ownership"), FEpicodeSandboxGameplay::CanPerform(EEpicodeSandboxParticipantKind::Director, TEXT("manageOwnership")));
	TestTrue(TEXT("owner can manage ownership"), FEpicodeSandboxGameplay::CanPerform(EEpicodeSandboxParticipantKind::Owner, TEXT("manageOwnership")));
	TestTrue(TEXT("viewer can observe"), FEpicodeSandboxGameplay::CanPerform(EEpicodeSandboxParticipantKind::Viewer, TEXT("observe")));

	FGameplayFixture Fixture;
	FString Error;
	TestTrue(TEXT("fixture"), Fixture.Init(6, Error));
	TestFalse(TEXT("viewer permission"), Fixture.Gameplay.CheckPermission(TEXT("watcher"), TEXT("interact"), Error));
	TestTrue(TEXT("viewer code"), Error.Contains(TEXT("VIEWER_READ_ONLY")));
	TestFalse(TEXT("performer ownership"), Fixture.Gameplay.CheckPermission(TEXT("hero"), TEXT("manageOwnership"), Error));
	TestTrue(TEXT("ownership forbidden"), Error.Contains(TEXT("FORBIDDEN")));
	TestTrue(TEXT("owner ownership"), Fixture.Gameplay.CheckPermission(TEXT("owner"), TEXT("manageOwnership"), Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeSandboxBudgetTest, "Epicode.Sandbox.Budget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeSandboxBudgetTest::RunTest(const FString& Parameters)
{
	FGameplayFixture Fixture;
	FString Error;
	TestTrue(TEXT("fixture"), Fixture.Init(7, Error));
	TestTrue(TEXT("budget report"), Fixture.Gameplay.GetBudgetReport()->GetIntegerField(TEXT("maxEntities")) == 10);
	TestFalse(TEXT("entity budget"), Fixture.Gameplay.CanSpawn(100, Error));
	TestTrue(TEXT("entity budget code"), Error.Contains(TEXT("SPAWN_BUDGET_EXCEEDED")));

	FEpicodeSandboxRules Tight = Fixture.Rules;
	Tight.Budget.MaxEventsPerSecond = 30;
	for (FEpicodeSandboxInteractionPoint& Interaction : Tight.Interactions)
	{
		Interaction.CooldownSeconds = 0.0;
	}
	FEpicodeSandboxGameplay Limited;
	TestTrue(TEXT("limited init"), Limited.Initialize(Tight, Fixture.World, Fixture.Random, Fixture.Events, 30, Error));
	TestTrue(TEXT("first event"), Limited.ExecuteInteraction(TEXT("hero"), TEXT("crate"), TEXT("use"), Error));
	const int32 KeycardsAfterFirst = Fixture.World.FindEntity(TEXT("hero"))->GetItemCount(TEXT("keycard"));
	TestFalse(TEXT("event budget"), Limited.ExecuteInteraction(TEXT("hero"), TEXT("crate"), TEXT("use"), Error));
	TestTrue(TEXT("event budget code"), Error.Contains(TEXT("EVENT_BUDGET_EXCEEDED")));
	TestEqual(TEXT("interaction rolled back"), Fixture.World.FindEntity(TEXT("hero"))->GetItemCount(TEXT("keycard")), KeycardsAfterFirst);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeSandboxPersistenceTest, "Epicode.Sandbox.Persistence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeSandboxPersistenceTest::RunTest(const FString& Parameters)
{
	const FSessionFixture Fixture;
	FEpicodeSandboxSessionConfig Config;
	Config.SessionId = TEXT("persist-session");
	Config.Seed = 77;
	Config.TickRate = 30;

	FEpicodeSandboxSession Session;
	FString Error;
	TestTrue(TEXT("load"), Session.Load(Fixture.Production, Fixture.Scene, Config, Error));
	Session.GetMutableWorld().FindEntity(Fixture.HeroId.ToString(EGuidFormats::DigitsWithHyphens))->Location = FVector(42, 0, 0);
	const FString SavedDigest = Session.GetDigest();
	const FString Root = MakeGameplayTempDirectory(TEXT("persist"));
	TestTrue(TEXT("save snapshot"), Session.SaveSnapshot(Root, Error));
	TestTrue(TEXT("snapshot file"), FPaths::FileExists(FEpicodeSandboxPersistence::GetSnapshotPath(Root)));

	FEpicodeSandboxSession Restored;
	TestTrue(TEXT("restore load"), Restored.Load(Fixture.Production, Fixture.Scene, Config, Error));
	TestTrue(TEXT("restore snapshot"), Restored.RestoreSnapshot(Root, Error));
	TestEqual(TEXT("restored digest"), Restored.GetDigest(), SavedDigest);
	TestEqual(TEXT("restored location"), Restored.GetWorld().FindEntity(Fixture.HeroId.ToString(EGuidFormats::DigitsWithHyphens))->Location.X, 42.0);
	TestEqual(TEXT("restored state"), Restored.GetState(), EEpicodeSandboxSessionState::Ready);

	FEpicodeSandboxSession Fresh;
	TestTrue(TEXT("fresh restore"), Fresh.RestoreSnapshot(Root, Error));
	TestEqual(TEXT("fresh digest"), Fresh.GetDigest(), SavedDigest);

	const FString CorruptRoot = MakeGameplayTempDirectory(TEXT("corrupt"));
	FFileHelper::SaveStringToFile(TEXT("{\"schemaVersion\":1,\"world\":{},\"events\":{},\"digest\":\"bad\"}"), *FEpicodeSandboxPersistence::GetSnapshotPath(CorruptRoot));
	FEpicodeSandboxSession Corrupt;
	TestFalse(TEXT("corrupt rejected"), Corrupt.RestoreSnapshot(CorruptRoot, Error));
	TestTrue(TEXT("corrupt code"), Error.Contains(TEXT("SESSION_RESTORE_FAILED")));

	IFileManager::Get().DeleteDirectory(*Root, false, true);
	IFileManager::Get().DeleteDirectory(*CorruptRoot, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeSandboxExampleSessionTest, "Epicode.Sandbox.ExampleSession", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeSandboxExampleSessionTest::RunTest(const FString& Parameters)
{
	const FString ExamplesRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Docs/Examples")));
	FEpicodeProductionDocument Production;
	FString Error;
	TestTrue(TEXT("example production"), FEpicodeProductionLibrary::Load(FPaths::Combine(ExamplesRoot, TEXT("Production/production.json")), Production, Error));
	FEpicodeSceneDocument Scene;
	TestTrue(TEXT("example scene"), FEpicodeSceneLibrary::Load(FPaths::Combine(ExamplesRoot, TEXT("Scenes/11111111-1111-4111-8111-111111111111/scene.json")), Scene, Error));

	FEpicodeSandboxSessionConfig Config;
	Config.SessionId = TEXT("example-session");
	Config.Seed = 2024;
	FEpicodeSandboxSession Session;
	TestTrue(TEXT("example session"), Session.Load(Production, Scene, Config, Error));
	TestTrue(TEXT("example entities"), Session.GetWorld().Entities.Num() >= 3);

	FEpicodeSandboxRules Rules;
	TestTrue(TEXT("example rules"), FEpicodeSandboxRules::Parse(Production.WorldRules, Rules, Error));
	TestTrue(TEXT("example rules validate"), Rules.Validate(&Session.GetWorld(), Error));
	FEpicodeSandboxRandomStreams Random;
	Random.Initialize(Config.Seed);
	FEpicodeSandboxEventStream Events;
	Events.Reset(1);
	FEpicodeSandboxGameplay Gameplay;
	TestTrue(TEXT("example gameplay"), Gameplay.Initialize(Rules, Session.GetMutableWorld(), Random, Events, 30, Error));
	TestEqual(TEXT("example spawner"), Rules.Spawners.Num(), 1);
	return true;
}
