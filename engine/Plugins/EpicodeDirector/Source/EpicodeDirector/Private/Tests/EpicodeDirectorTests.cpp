#include "Misc/AutomationTest.h"

#include "EpicodeDirector.h"
#include "EpicodeReplayCheckpoint.h"
#include "EpicodeReplayJournal.h"
#include "EpicodeSandboxJson.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	FString MakeDirectorTempDirectory(const FString& InName)
	{
		const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Epicode"), TEXT("DirectorTests"), InName + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
		IFileManager::Get().MakeDirectory(*Directory, true);
		return Directory;
	}

	TSharedPtr<FJsonObject> MakeNarrativeJson()
	{
		TSharedPtr<FJsonObject> Narrative = MakeShared<FJsonObject>();
		Narrative->SetStringField(TEXT("entryBeatId"), TEXT("intro"));
		TSharedPtr<FJsonObject> Variables = MakeShared<FJsonObject>();
		Variables->SetNumberField(TEXT("alarmLevel"), 0);
		Variables->SetBoolField(TEXT("hasKeycard"), false);
		Narrative->SetObjectField(TEXT("variables"), Variables);

		TSharedPtr<FJsonObject> Objective = MakeShared<FJsonObject>();
		Objective->SetStringField(TEXT("id"), TEXT("steal"));
		Objective->SetStringField(TEXT("description"), TEXT("Steal the keycard"));
		Objective->SetStringField(TEXT("condition"), TEXT("hasKeycard == true"));
		Narrative->SetArrayField(TEXT("objectives"), {MakeShared<FJsonValueObject>(Objective)});

		TSharedPtr<FJsonObject> Intro = MakeShared<FJsonObject>();
		Intro->SetStringField(TEXT("id"), TEXT("intro"));
		Intro->SetStringField(TEXT("name"), TEXT("Intro"));
		Intro->SetArrayField(TEXT("exitConditions"), {MakeShared<FJsonValueString>(TEXT("alarmLevel >= 1"))});
		Intro->SetArrayField(TEXT("next"), {MakeShared<FJsonValueString>(TEXT("escape"))});
		Intro->SetArrayField(TEXT("cues"), {MakeShared<FJsonValueString>(TEXT("cue-beat"))});
		TSharedPtr<FJsonObject> Escape = MakeShared<FJsonObject>();
		Escape->SetStringField(TEXT("id"), TEXT("escape"));
		Escape->SetStringField(TEXT("name"), TEXT("Escape"));
		Escape->SetArrayField(TEXT("entryConditions"), {MakeShared<FJsonValueString>(TEXT("alarmLevel >= 1"))});
		Escape->SetArrayField(TEXT("cues"), {MakeShared<FJsonValueString>(TEXT("cue-enter"))});
		Narrative->SetArrayField(TEXT("beats"), {MakeShared<FJsonValueObject>(Intro), MakeShared<FJsonValueObject>(Escape)});

		TSharedPtr<FJsonObject> TimeCue = MakeShared<FJsonObject>();
		TimeCue->SetStringField(TEXT("id"), TEXT("cue-time"));
		TimeCue->SetStringField(TEXT("trigger"), TEXT("time"));
		TimeCue->SetNumberField(TEXT("time"), 1.0);
		TimeCue->SetArrayField(TEXT("actions"), {MakeShared<FJsonValueString>(TEXT("message:hello")), MakeShared<FJsonValueString>(TEXT("camera:camera-1"))});
		TSharedPtr<FJsonObject> EventCue = MakeShared<FJsonObject>();
		EventCue->SetStringField(TEXT("id"), TEXT("cue-event"));
		EventCue->SetStringField(TEXT("trigger"), TEXT("event"));
		EventCue->SetStringField(TEXT("event"), TEXT("sandbox.interaction"));
		EventCue->SetArrayField(TEXT("actions"), {MakeShared<FJsonValueString>(TEXT("message:event"))});
		TSharedPtr<FJsonObject> BeatCue = MakeShared<FJsonObject>();
		BeatCue->SetStringField(TEXT("id"), TEXT("cue-beat"));
		BeatCue->SetStringField(TEXT("trigger"), TEXT("beat"));
		BeatCue->SetStringField(TEXT("beatId"), TEXT("intro"));
		BeatCue->SetArrayField(TEXT("actions"), {MakeShared<FJsonValueString>(TEXT("message:beat"))});
		TSharedPtr<FJsonObject> ConditionCue = MakeShared<FJsonObject>();
		ConditionCue->SetStringField(TEXT("id"), TEXT("cue-condition"));
		ConditionCue->SetStringField(TEXT("trigger"), TEXT("condition"));
		ConditionCue->SetStringField(TEXT("condition"), TEXT("alarmLevel >= 1"));
		ConditionCue->SetArrayField(TEXT("actions"), {MakeShared<FJsonValueString>(TEXT("setObjective:steal"))});
		TSharedPtr<FJsonObject> EnterCue = MakeShared<FJsonObject>();
		EnterCue->SetStringField(TEXT("id"), TEXT("cue-enter"));
		EnterCue->SetStringField(TEXT("trigger"), TEXT("beat"));
		EnterCue->SetStringField(TEXT("beatId"), TEXT("escape"));
		EnterCue->SetArrayField(TEXT("actions"), {MakeShared<FJsonValueString>(TEXT("message:escape"))});
		TSharedPtr<FJsonObject> FailCue = MakeShared<FJsonObject>();
		FailCue->SetStringField(TEXT("id"), TEXT("cue-fail"));
		FailCue->SetStringField(TEXT("trigger"), TEXT("time"));
		FailCue->SetNumberField(TEXT("time"), 0.05);
		FailCue->SetArrayField(TEXT("actions"), {MakeShared<FJsonValueString>(TEXT("camera:missing-camera"))});
		Narrative->SetArrayField(TEXT("cues"), {
			MakeShared<FJsonValueObject>(TimeCue),
			MakeShared<FJsonValueObject>(EventCue),
			MakeShared<FJsonValueObject>(BeatCue),
			MakeShared<FJsonValueObject>(ConditionCue),
			MakeShared<FJsonValueObject>(EnterCue),
			MakeShared<FJsonValueObject>(FailCue)
		});
		return Narrative;
	}

	FEpicodeSandboxEntity MakeEntity(const FString& InId, EEpicodeSandboxEntityKind InKind, const FVector& InLocation)
	{
		FEpicodeSandboxEntity Entity;
		Entity.Id = InId;
		Entity.Kind = InKind;
		Entity.Location = InLocation;
		Entity.Data = MakeShared<FJsonObject>();
		return Entity;
	}

	bool AddParticipant(FEpicodeSandboxWorld& InWorld, const FString& InId, EEpicodeSandboxParticipantKind InKind, const FString& InCharacterId, FString& OutError)
	{
		FEpicodeSandboxParticipant Participant;
		Participant.Id = InId;
		Participant.Kind = InKind;
		Participant.CharacterEntityId = InCharacterId;
		return InWorld.AddParticipant(Participant, OutError);
	}

	struct FDirectorFixture
	{
		FEpicodeSandboxWorld World;
		FEpicodeSandboxEventStream Events;
		TArray<FEpicodeProductionCameraPreset> Presets;
		FEpicodeDirectorController Controller;
		FEpicodeBeatGraph Graph;

		bool Init(FString& OutError)
		{
			World = FEpicodeSandboxWorld::Create(TEXT("director-session"), TEXT("director-production"), TEXT("director-scene"), 11);
			FEpicodeSandboxEntity Hero = MakeEntity(TEXT("hero"), EEpicodeSandboxEntityKind::Character, FVector::ZeroVector);
			Hero.Data->SetArrayField(TEXT("tags"), {MakeShared<FJsonValueString>(TEXT("hero"))});
			FEpicodeSandboxEntity Npc = MakeEntity(TEXT("npc"), EEpicodeSandboxEntityKind::Npc, FVector(100, 0, 0));
			Npc.Data->SetStringField(TEXT("npcProfileId"), TEXT("guard"));
			FEpicodeSandboxEntity Camera = MakeEntity(TEXT("camera-1"), EEpicodeSandboxEntityKind::Camera, FVector::ZeroVector);
			FEpicodeSandboxEntity Crate = MakeEntity(TEXT("crate"), EEpicodeSandboxEntityKind::Prop, FVector(50, 0, 0));
			for (const FEpicodeSandboxEntity& Entity : {Hero, Npc, Camera, Crate})
			{
				if (!World.AddEntity(Entity, OutError)) return false;
			}
			if (!AddParticipant(World, TEXT("director-1"), EEpicodeSandboxParticipantKind::Director, FString(), OutError)) return false;
			if (!AddParticipant(World, TEXT("director-2"), EEpicodeSandboxParticipantKind::Director, FString(), OutError)) return false;
			if (!AddParticipant(World, TEXT("owner-1"), EEpicodeSandboxParticipantKind::Owner, FString(), OutError)) return false;
			if (!AddParticipant(World, TEXT("performer-1"), EEpicodeSandboxParticipantKind::Performer, TEXT("hero"), OutError)) return false;
			if (!AddParticipant(World, TEXT("npc-participant"), EEpicodeSandboxParticipantKind::Npc, TEXT("npc"), OutError)) return false;
			if (!AddParticipant(World, TEXT("viewer-1"), EEpicodeSandboxParticipantKind::Viewer, TEXT("hero"), OutError)) return false;
			FEpicodeProductionCameraPreset Preset;
			Preset.Id = TEXT("cam-preset");
			Preset.Name = TEXT("Main");
			Presets.Add(Preset);
			Events.Reset(1);
			if (!FEpicodeBeatGraph::Parse(MakeNarrativeJson(), Graph, OutError)) return false;
			return Controller.Initialize(Graph, World, &Events, Presets, OutError);
		}
	};

	FString MakeReplayRoot(const FEpicodeSandboxWorld& InWorld, FString& OutError)
	{
		const FString Root = MakeDirectorTempDirectory(TEXT("replay"));
		FEpicodeReplayCheckpoint Checkpoint;
		Checkpoint.Sequence = 0;
		Checkpoint.Tick = 0;
		Checkpoint.TickRate = 30;
		Checkpoint.Seed = 11;
		Checkpoint.ContentDigest = TEXT("content-v1");
		Checkpoint.World = InWorld;
		Checkpoint.Random.Initialize(11);
		if (!FEpicodeReplayCheckpoints::Save(Root, Checkpoint, OutError)) return FString();
		FEpicodeReplayJournalHeader Header;
		Header.SessionId = TEXT("director-replay");
		Header.ContentDigest = TEXT("content-v1");
		Header.Seed = 11;
		FEpicodeReplayJournal Journal;
		if (!Journal.Create(Root, Header, OutError)) return FString();
		FEpicodeSandboxWorld ReplayWorld = InWorld;
		FEpicodeSandboxEvent Event;
		Event.Sequence = 1;
		Event.Tick = 10;
		Event.Type = TEXT("sandbox.director.location");
		Event.ActorId = TEXT("director-1");
		Event.Payload = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> Change = MakeShared<FJsonObject>();
		Change->SetStringField(TEXT("op"), TEXT("entity.location"));
		Change->SetStringField(TEXT("id"), TEXT("hero"));
		Change->SetArrayField(TEXT("location"), {MakeShared<FJsonValueNumber>(5), MakeShared<FJsonValueNumber>(0), MakeShared<FJsonValueNumber>(0)});
		Event.Payload->SetArrayField(TEXT("changes"), {MakeShared<FJsonValueObject>(Change)});
		if (!FEpicodeReplayState::ApplyEvent(ReplayWorld, Event, OutError)) return FString();
		if (!Journal.Append(Event, FEpicodeSandboxDigest::ComputeWorldDigest(ReplayWorld), TEXT("2026-01-01T00:00:00Z"), OutError)) return FString();
		if (!Journal.Finalize(OutError)) return FString();
		return Root;
	}

	int32 CountEvents(const FEpicodeSandboxEventStream& InEvents, const FString& InType)
	{
		int32 Count = 0;
		for (const FEpicodeSandboxEvent& Event : InEvents.GetEvents())
		{
			if (Event.Type == InType) ++Count;
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeDirectorBeatsTest, "Epicode.Director.Beats", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeDirectorBeatsTest::RunTest(const FString& Parameters)
{
	FString Error;
	FEpicodeBeatGraph Graph;
	TestTrue(TEXT("parse"), FEpicodeBeatGraph::Parse(MakeNarrativeJson(), Graph, Error));
	TestTrue(TEXT("validate"), Graph.Validate(Error));
	TestEqual(TEXT("beat count"), Graph.Beats.Num(), 2);
	TestEqual(TEXT("reachable count"), Graph.GetReachableBeats().Num(), 2);
	TestNotNull(TEXT("find beat"), Graph.FindBeat(TEXT("intro")));
	TestNull(TEXT("missing beat"), Graph.FindBeat(TEXT("missing")));

	TSharedPtr<FJsonObject> DuplicateJson = MakeNarrativeJson();
	TArray<TSharedPtr<FJsonValue>> DuplicateBeats = DuplicateJson->GetArrayField(TEXT("beats"));
	const TSharedPtr<FJsonValue> FirstBeat = DuplicateBeats[0];
	DuplicateBeats.Add(FirstBeat);
	DuplicateJson->SetArrayField(TEXT("beats"), DuplicateBeats);
	FEpicodeBeatGraph Duplicate;
	TestTrue(TEXT("duplicate parse"), FEpicodeBeatGraph::Parse(DuplicateJson, Duplicate, Error));
	TestFalse(TEXT("duplicate validate"), Duplicate.Validate(Error));
	TestTrue(TEXT("duplicate code"), Error.Contains(TEXT("DIRECTOR_BEAT_DUPLICATE")));

	TSharedPtr<FJsonObject> MissingEntryJson = MakeNarrativeJson();
	MissingEntryJson->SetStringField(TEXT("entryBeatId"), TEXT("missing"));
	FEpicodeBeatGraph MissingEntry;
	TestTrue(TEXT("missing entry parse"), FEpicodeBeatGraph::Parse(MissingEntryJson, MissingEntry, Error));
	TestFalse(TEXT("missing entry validate"), MissingEntry.Validate(Error));
	TestTrue(TEXT("missing entry code"), Error.Contains(TEXT("DIRECTOR_ENTRY_MISSING")));

	TSharedPtr<FJsonObject> UnreachableJson = MakeNarrativeJson();
	TArray<TSharedPtr<FJsonValue>> UnreachableBeats = UnreachableJson->GetArrayField(TEXT("beats"));
	TSharedPtr<FJsonObject> Orphan = MakeShared<FJsonObject>();
	Orphan->SetStringField(TEXT("id"), TEXT("orphan"));
	UnreachableBeats.Add(MakeShared<FJsonValueObject>(Orphan));
	UnreachableJson->SetArrayField(TEXT("beats"), UnreachableBeats);
	FEpicodeBeatGraph Unreachable;
	TestTrue(TEXT("unreachable parse"), FEpicodeBeatGraph::Parse(UnreachableJson, Unreachable, Error));
	TestFalse(TEXT("unreachable validate"), Unreachable.Validate(Error));
	TestTrue(TEXT("unreachable code"), Error.Contains(TEXT("DIRECTOR_BEAT_UNREACHABLE")));

	TSharedPtr<FJsonObject> BadRefJson = MakeNarrativeJson();
	TArray<TSharedPtr<FJsonValue>> BadRefBeats = BadRefJson->GetArrayField(TEXT("beats"));
	BadRefBeats[0]->AsObject()->SetArrayField(TEXT("next"), {MakeShared<FJsonValueString>(TEXT("missing"))});
	BadRefJson->SetArrayField(TEXT("beats"), BadRefBeats);
	FEpicodeBeatGraph BadRef;
	TestTrue(TEXT("bad ref parse"), FEpicodeBeatGraph::Parse(BadRefJson, BadRef, Error));
	TestFalse(TEXT("bad ref validate"), BadRef.Validate(Error));
	TestTrue(TEXT("bad ref code"), Error.Contains(TEXT("DIRECTOR_BEAT_REFERENCE")));

	TSharedPtr<FJsonObject> BadVariableJson = MakeNarrativeJson();
	TArray<TSharedPtr<FJsonValue>> BadVariableBeats = BadVariableJson->GetArrayField(TEXT("beats"));
	BadVariableBeats[0]->AsObject()->SetArrayField(TEXT("exitConditions"), {MakeShared<FJsonValueString>(TEXT("ghostVariable == 1"))});
	BadVariableJson->SetArrayField(TEXT("beats"), BadVariableBeats);
	FEpicodeBeatGraph BadVariable;
	TestTrue(TEXT("bad variable parse"), FEpicodeBeatGraph::Parse(BadVariableJson, BadVariable, Error));
	TestFalse(TEXT("bad variable validate"), BadVariable.Validate(Error));
	TestTrue(TEXT("bad variable code"), Error.Contains(TEXT("DIRECTOR_VARIABLE_UNDECLARED")));

	TSharedPtr<FJsonObject> LoopJson = MakeNarrativeJson();
	TArray<TSharedPtr<FJsonValue>> LoopBeats = LoopJson->GetArrayField(TEXT("beats"));
	LoopBeats[1]->AsObject()->SetArrayField(TEXT("next"), {MakeShared<FJsonValueString>(TEXT("intro"))});
	LoopJson->SetArrayField(TEXT("beats"), LoopBeats);
	FEpicodeBeatGraph Loop;
	TestTrue(TEXT("loop parse"), FEpicodeBeatGraph::Parse(LoopJson, Loop, Error));
	TestTrue(TEXT("loop validates"), Loop.Validate(Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeDirectorNarrativeTest, "Epicode.Director.NarrativeState", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeDirectorNarrativeTest::RunTest(const FString& Parameters)
{
	FDirectorFixture Fixture;
	FString Error;
	TestTrue(TEXT("fixture"), Fixture.Init(Error));
	TestEqual(TEXT("entry"), Fixture.Controller.GetNarrative().CurrentBeatId, FString(TEXT("intro")));
	TestEqual(TEXT("objective initial"), Fixture.Controller.GetNarrative().Objectives.FindRef(TEXT("steal")), false);
	TestEqual(TEXT("beat event"), CountEvents(Fixture.Events, TEXT("narrative.beatEntered")), 1);

	Fixture.World.Narrative.SetVariable(TEXT("hasKeycard"), FEpicodeSandboxValue::MakeBool(true));
	Fixture.Controller.Tick(0.01, 1);
	TestEqual(TEXT("objective complete"), Fixture.Controller.GetNarrative().Objectives.FindRef(TEXT("steal")), true);
	TestEqual(TEXT("objective event"), CountEvents(Fixture.Events, TEXT("narrative.objectiveCompleted")), 1);
	TestEqual(TEXT("world objective"), Fixture.World.Narrative.Objectives.FindRef(TEXT("steal")), true);

	Fixture.World.Narrative.SetVariable(TEXT("alarmLevel"), FEpicodeSandboxValue::MakeInt(1));
	Fixture.Controller.Tick(0.01, 2);
	TestEqual(TEXT("beat transition"), Fixture.Controller.GetNarrative().CurrentBeatId, FString(TEXT("escape")));
	TestEqual(TEXT("beat event total"), CountEvents(Fixture.Events, TEXT("narrative.beatEntered")), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeDirectorCuesTest, "Epicode.Director.Cues", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeDirectorCuesTest::RunTest(const FString& Parameters)
{
	FDirectorFixture Fixture;
	FString Error;
	TestTrue(TEXT("fixture"), Fixture.Init(Error));
	TestEqual(TEXT("initial beat cue"), Fixture.Controller.GetMessages().Num(), 0);
	Fixture.Controller.Tick(0.2, 1);
	TestTrue(TEXT("beat message"), Fixture.Controller.GetMessages().Contains(TEXT("beat")));
	TestEqual(TEXT("cue failure"), CountEvents(Fixture.Events, TEXT("director.cueFailed")), 1);
	TestEqual(TEXT("cue success"), CountEvents(Fixture.Events, TEXT("director.cueExecuted")), 1);

	Fixture.Controller.Tick(1.0, 2);
	TestTrue(TEXT("time message"), Fixture.Controller.GetMessages().Contains(TEXT("hello")));
	TestEqual(TEXT("camera switched"), Fixture.Controller.GetCameraId(), FString(TEXT("camera-1")));
	TestEqual(TEXT("time event once"), CountEvents(Fixture.Events, TEXT("director.cueExecuted")), 2);

	FEpicodeSandboxEvent Interaction;
	Interaction.Sequence = Fixture.Events.GetNextSequence();
	Interaction.Tick = 3;
	Interaction.Type = TEXT("sandbox.interaction");
	Interaction.ActorId = TEXT("performer-1");
	Fixture.Events.AppendExisting(Interaction, Error);
	Fixture.Controller.Tick(0.01, 3);
	TestTrue(TEXT("event message"), Fixture.Controller.GetMessages().Contains(TEXT("event")));

	Fixture.World.Narrative.SetVariable(TEXT("alarmLevel"), FEpicodeSandboxValue::MakeInt(1));
	Fixture.Controller.Tick(0.01, 4);
	TestEqual(TEXT("condition beat"), Fixture.Controller.GetNarrative().CurrentBeatId, FString(TEXT("escape")));
	TestTrue(TEXT("enter message"), Fixture.Controller.GetMessages().Contains(TEXT("escape")));
	TestEqual(TEXT("condition objective"), Fixture.Controller.GetNarrative().CurrentObjectiveId, FString(TEXT("steal")));
	TestEqual(TEXT("failure once"), CountEvents(Fixture.Events, TEXT("director.cueFailed")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeDirectorControlTest, "Epicode.Director.Control", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeDirectorControlTest::RunTest(const FString& Parameters)
{
	FDirectorFixture Fixture;
	FString Error;
	TestTrue(TEXT("fixture"), Fixture.Init(Error));
	TestTrue(TEXT("pause"), Fixture.Controller.Pause(TEXT("director-1"), TEXT("pause-1"), 1).bSuccess);
	Fixture.Controller.Tick(1.0, 1);
	TestEqual(TEXT("paused time"), Fixture.Controller.GetDirectorTime(), 0.0);
	TestTrue(TEXT("resume"), Fixture.Controller.Resume(TEXT("director-1"), TEXT("resume-1"), 2).bSuccess);
	Fixture.Controller.Tick(1.0, 2);
	TestEqual(TEXT("resumed time"), Fixture.Controller.GetDirectorTime(), 1.0);
	TestTrue(TEXT("scale"), Fixture.Controller.SetTimeScale(TEXT("director-1"), TEXT("scale-1"), 0.5, 3).bSuccess);
	Fixture.Controller.Tick(1.0, 3);
	TestEqual(TEXT("scaled time"), Fixture.Controller.GetDirectorTime(), 1.5);

	const int32 EventCountAfterCommands = Fixture.Events.GetEvents().Num();
	const FEpicodeDirectorCommandResult Duplicate = Fixture.Controller.Pause(TEXT("director-1"), TEXT("pause-1"), 4);
	TestTrue(TEXT("duplicate success"), Duplicate.bSuccess);
	TestTrue(TEXT("duplicate flag"), Duplicate.bDuplicate);
	TestEqual(TEXT("duplicate no event"), Fixture.Events.GetEvents().Num(), EventCountAfterCommands);

	TestFalse(TEXT("performer forbidden"), Fixture.Controller.Pause(TEXT("performer-1"), TEXT("pause-bad"), 5).bSuccess);
	TestTrue(TEXT("forbidden code"), Fixture.Controller.Pause(TEXT("performer-1"), TEXT("pause-bad-2"), 5).Error.Contains(TEXT("FORBIDDEN")));

	TestTrue(TEXT("camera"), Fixture.Controller.SwitchCamera(TEXT("director-1"), TEXT("camera-1"), TEXT("camera-1"), FString(), 6).bSuccess);
	TestEqual(TEXT("camera set"), Fixture.Controller.GetCameraId(), FString(TEXT("camera-1")));
	TestFalse(TEXT("camera missing"), Fixture.Controller.SwitchCamera(TEXT("director-1"), TEXT("camera-bad"), TEXT("missing"), FString(), 7).bSuccess);
	TestTrue(TEXT("camera code"), Fixture.Controller.SwitchCamera(TEXT("director-1"), TEXT("camera-bad-2"), TEXT("missing"), FString(), 7).Error.Contains(TEXT("DIRECTOR_CAMERA_NOT_FOUND")));

	FEpicodeSandboxEntity Spawned = MakeEntity(TEXT("director-prop"), EEpicodeSandboxEntityKind::Prop, FVector(10, 0, 0));
	TestTrue(TEXT("spawn"), Fixture.Controller.SpawnEntity(TEXT("director-1"), TEXT("spawn-1"), Spawned, 8).bSuccess);
	TestNotNull(TEXT("spawned exists"), Fixture.World.FindEntity(TEXT("director-prop")));
	TestTrue(TEXT("remove"), Fixture.Controller.RemoveEntity(TEXT("director-1"), TEXT("remove-1"), TEXT("director-prop"), 9).bSuccess);
	TestNull(TEXT("removed"), Fixture.World.FindEntity(TEXT("director-prop")));

	TestTrue(TEXT("objective"), Fixture.Controller.SetObjective(TEXT("director-1"), TEXT("objective-1"), TEXT("steal"), 10).bSuccess);
	TestEqual(TEXT("current objective"), Fixture.Controller.GetNarrative().CurrentObjectiveId, FString(TEXT("steal")));
	TestTrue(TEXT("enter beat"), Fixture.Controller.EnterBeat(TEXT("director-1"), TEXT("beat-1"), TEXT("escape"), 11).bSuccess);
	TestEqual(TEXT("entered beat"), Fixture.Controller.GetNarrative().CurrentBeatId, FString(TEXT("escape")));
	TestTrue(TEXT("message"), Fixture.Controller.Message(TEXT("director-1"), TEXT("message-1"), TEXT("directed"), 12).bSuccess);
	TestTrue(TEXT("message stored"), Fixture.Controller.GetMessages().Contains(TEXT("directed")));
	TestFalse(TEXT("invalid message"), Fixture.Controller.Message(TEXT("director-1"), TEXT("message-2"), FString(), 13).bSuccess);

	TestTrue(TEXT("pause key"), Fixture.Controller.Pause(TEXT("director-1"), TEXT("pause-2"), 14).bSuccess);
	TestTrue(TEXT("time scale pause"), Fixture.Controller.SetTimeScale(TEXT("director-1"), TEXT("scale-2"), 0.5, 15).bSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeDirectorTakeoverTest, "Epicode.Director.Takeover", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeDirectorTakeoverTest::RunTest(const FString& Parameters)
{
	FDirectorFixture Fixture;
	FString Error;
	TestTrue(TEXT("fixture"), Fixture.Init(Error));
	const FEpicodeDirectorCommandResult Acquired = Fixture.Controller.AcquireTakeover(TEXT("director-1"), TEXT("take-1"), TEXT("npc-participant"), TEXT("perform"), TEXT("manual"), 1);
	TestTrue(TEXT("acquire"), Acquired.bSuccess);
	TestTrue(TEXT("npc take over"), Fixture.World.FindParticipant(TEXT("npc-participant"))->bTakeover);
	TestEqual(TEXT("lease count"), Fixture.Controller.GetTakeoverLeases().Num(), 1);
	TestEqual(TEXT("takeover event"), CountEvents(Fixture.Events, TEXT("director.takeoverStarted")), 1);

	const FEpicodeDirectorCommandResult Duplicate = Fixture.Controller.AcquireTakeover(TEXT("director-1"), TEXT("take-1"), TEXT("npc-participant"), TEXT("perform"), TEXT("manual"), 2);
	TestTrue(TEXT("duplicate"), Duplicate.bDuplicate);
	TestEqual(TEXT("lease unchanged"), Fixture.Controller.GetTakeoverLeases().Num(), 1);

	const FEpicodeDirectorCommandResult Conflict = Fixture.Controller.AcquireTakeover(TEXT("director-2"), TEXT("take-2"), TEXT("npc-participant"), TEXT("perform"), TEXT("manual"), 3);
	TestFalse(TEXT("conflict"), Conflict.bSuccess);
	TestTrue(TEXT("conflict code"), Conflict.Error.Contains(TEXT("DIRECTOR_TAKEOVER_CONFLICT")));
	TestTrue(TEXT("release other forbidden"), Fixture.Controller.ReleaseTakeover(TEXT("director-2"), TEXT("release-other"), TEXT("npc-participant"), 4).Error.Contains(TEXT("FORBIDDEN")));
	TestTrue(TEXT("release"), Fixture.Controller.ReleaseTakeover(TEXT("director-1"), TEXT("release-1"), TEXT("npc-participant"), 5).bSuccess);
	TestFalse(TEXT("released"), Fixture.World.FindParticipant(TEXT("npc-participant"))->bTakeover);
	TestEqual(TEXT("end event"), CountEvents(Fixture.Events, TEXT("director.takeoverEnded")), 1);

	const FEpicodeDirectorCommandResult Viewer = Fixture.Controller.AcquireTakeover(TEXT("director-1"), TEXT("take-viewer"), TEXT("viewer-1"), TEXT("perform"), TEXT("manual"), 6);
	TestFalse(TEXT("viewer invalid"), Viewer.bSuccess);
	TestTrue(TEXT("viewer code"), Viewer.Error.Contains(TEXT("TAKEOVER_TARGET_INVALID")));
	const FEpicodeDirectorCommandResult Missing = Fixture.Controller.AcquireTakeover(TEXT("director-1"), TEXT("take-missing"), TEXT("missing"), TEXT("perform"), TEXT("manual"), 7);
	TestFalse(TEXT("missing invalid"), Missing.bSuccess);
	TestTrue(TEXT("missing code"), Missing.Error.Contains(TEXT("TAKEOVER_TARGET_NOT_FOUND")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeDirectorTakeTest, "Epicode.Director.Take", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeDirectorTakeTest::RunTest(const FString& Parameters)
{
	FDirectorFixture Fixture;
	FString Error;
	TestTrue(TEXT("fixture"), Fixture.Init(Error));
	const FString Root = MakeDirectorTempDirectory(TEXT("takes"));
	FEpicodeDirectorTake Take;
	Take.Id = TEXT("take-1");
	Take.InSequence = 0;
	Take.OutSequence = 10;
	Take.InTick = 0;
	Take.OutTick = 300;
	Take.CameraIds = {TEXT("camera-1")};
	Take.CameraPresetIds = {TEXT("cam-preset")};
	Take.ResolutionPreset = TEXT("1080p");
	FEpicodeDirectorCommandResult Result;
	TestTrue(TEXT("create take"), Fixture.Controller.CreateTake(TEXT("director-1"), TEXT("take-create"), Root, Take, Result, 1));
	TestTrue(TEXT("create result"), Result.bSuccess);
	TestEqual(TEXT("take version"), Take.Version, 1);
	TestTrue(TEXT("take file"), FPaths::FileExists(*FEpicodeTakeLibrary::GetTakePath(Root, TEXT("take-1"), 1)));

	FEpicodeDirectorTake Empty = Take;
	Empty.Id = TEXT("take-empty");
	Empty.InSequence = 10;
	Empty.OutSequence = 10;
	FEpicodeDirectorCommandResult EmptyResult;
	TestFalse(TEXT("empty range"), Fixture.Controller.CreateTake(TEXT("director-1"), TEXT("take-empty-key"), Root, Empty, EmptyResult, 2));
	TestTrue(TEXT("empty code"), EmptyResult.Error.Contains(TEXT("TAKE_RANGE_INVALID")));

	FEpicodeDirectorTake Missing = Take;
	Missing.Id = TEXT("take-missing-camera");
	Missing.CameraIds = {TEXT("missing-camera")};
	FEpicodeDirectorCommandResult MissingResult;
	TestFalse(TEXT("missing camera"), Fixture.Controller.CreateTake(TEXT("director-1"), TEXT("take-missing-key"), Root, Missing, MissingResult, 3));
	TestTrue(TEXT("missing code"), MissingResult.Error.Contains(TEXT("TAKE_DEPENDENCY_MISSING")));

	FEpicodeDirectorTake Revision = Take;
	Revision.InSequence = 2;
	Revision.OutSequence = 12;
	FEpicodeDirectorTake Revised;
	FEpicodeDirectorCommandResult ReviseResult;
	TestTrue(TEXT("revise"), Fixture.Controller.ReviseTake(TEXT("director-1"), TEXT("take-revise"), Root, TEXT("take-1"), Revision, Revised, ReviseResult, 4));
	TestEqual(TEXT("revision version"), Revised.Version, 2);
	FEpicodeDirectorTake Original;
	TestTrue(TEXT("load original"), FEpicodeTakeLibrary::Load(Root, TEXT("take-1"), 1, Original, Error));
	TestEqual(TEXT("original in"), Original.InSequence, static_cast<int64>(0));
	FEpicodeDirectorTake Latest;
	TestTrue(TEXT("load latest"), FEpicodeTakeLibrary::LoadLatest(Root, TEXT("take-1"), Latest, Error));
	TestEqual(TEXT("latest in"), Latest.InSequence, static_cast<int64>(2));
	TestEqual(TEXT("versions"), FEpicodeTakeLibrary::ListVersions(Root, TEXT("take-1")).Num(), 2);

	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeDirectorViewsTest, "Epicode.Director.Views", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeDirectorViewsTest::RunTest(const FString& Parameters)
{
	FDirectorFixture Fixture;
	FString Error;
	TestTrue(TEXT("fixture"), Fixture.Init(Error));
	const FString ReplayRoot = MakeReplayRoot(Fixture.World, Error);
	TestFalse(TEXT("replay root"), ReplayRoot.IsEmpty());

	const FEpicodeDirectorCommandResult Ghost = Fixture.Controller.SetView(TEXT("director-1"), TEXT("view-ghost"), EEpicodeDirectorViewMode::Ghost, ReplayRoot, 1);
	TestTrue(TEXT("ghost view"), Ghost.bSuccess);
	TestEqual(TEXT("ghost mode"), static_cast<int32>(Fixture.Controller.GetViewMode()), static_cast<int32>(EEpicodeDirectorViewMode::Ghost));
	TestEqual(TEXT("ghost world"), Fixture.Controller.GetViewWorld().FindEntity(TEXT("hero"))->Location.X, 5.0);
	TestEqual(TEXT("live unchanged"), Fixture.World.FindEntity(TEXT("hero"))->Location.X, 0.0);

	const FEpicodeDirectorCommandResult ReadOnly = Fixture.Controller.SwitchCamera(TEXT("director-1"), TEXT("ghost-camera"), TEXT("camera-1"), FString(), 2);
	TestFalse(TEXT("ghost read only"), ReadOnly.bSuccess);
	TestTrue(TEXT("read only code"), ReadOnly.Error.Contains(TEXT("REPLAY_READ_ONLY")));

	TestTrue(TEXT("live view"), Fixture.Controller.SetView(TEXT("director-1"), TEXT("view-live"), EEpicodeDirectorViewMode::Live, FString(), 0).bSuccess);
	TestEqual(TEXT("live mode"), static_cast<int32>(Fixture.Controller.GetViewMode()), static_cast<int32>(EEpicodeDirectorViewMode::Live));

	const FString BranchRoot = MakeDirectorTempDirectory(TEXT("branch")) + TEXT("-branch");
	FEpicodeDirectorCommandResult BranchResult;
	TestTrue(TEXT("branch"), Fixture.Controller.CreateBranch(TEXT("director-1"), TEXT("branch-key"), ReplayRoot, BranchRoot, TEXT("branch-1"), 1, BranchResult));
	TestTrue(TEXT("branch result"), BranchResult.bSuccess);
	TestTrue(TEXT("branch dir"), IFileManager::Get().DirectoryExists(*BranchRoot));
	TestTrue(TEXT("branch metadata"), FPaths::FileExists(*FEpicodeReplayBranch::GetBranchMetadataPath(BranchRoot)));

	FEpicodeDirectorCommandResult ForbiddenBranch;
	TestFalse(TEXT("performer branch"), Fixture.Controller.CreateBranch(TEXT("performer-1"), TEXT("branch-performer"), ReplayRoot, BranchRoot, TEXT("branch-2"), 1, ForbiddenBranch));
	TestTrue(TEXT("branch forbidden"), ForbiddenBranch.Error.Contains(TEXT("FORBIDDEN")));

	IFileManager::Get().DeleteDirectory(*ReplayRoot, false, true);
	IFileManager::Get().DeleteDirectory(*BranchRoot, false, true);
	return true;
}
