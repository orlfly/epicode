#include "Misc/AutomationTest.h"

#include "EpicodeReplayCheckpoint.h"
#include "EpicodeReplayEngine.h"
#include "EpicodeReplayJob.h"
#include "EpicodeReplayJournal.h"
#include "EpicodeReplayRetention.h"
#include "EpicodeSandboxJson.h"
#include "EpicodeSandboxWorld.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	const FString ReplayContentDigest = TEXT("content-v1");

	FString MakeReplayTempDirectory(const FString& InName)
	{
		const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Epicode"), TEXT("ReplayTests"), InName + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
		IFileManager::Get().MakeDirectory(*Directory, true);
		return Directory;
	}

	FEpicodeSandboxWorld MakeReplayWorld()
	{
		FEpicodeSandboxWorld World = FEpicodeSandboxWorld::Create(TEXT("replay-session"), TEXT("replay-production"), TEXT("replay-scene"), 42);
		FEpicodeSandboxEntity Hero;
		Hero.Id = TEXT("hero");
		Hero.Kind = EEpicodeSandboxEntityKind::Character;
		Hero.Location = FVector::ZeroVector;
		Hero.Data = MakeShared<FJsonObject>();
		FString Error;
		World.AddEntity(Hero, Error);
		FEpicodeSandboxEntity Crate;
		Crate.Id = TEXT("crate");
		Crate.Kind = EEpicodeSandboxEntityKind::Prop;
		Crate.Location = FVector(100, 0, 0);
		World.AddEntity(Crate, Error);
		World.Narrative.SetVariable(TEXT("alarmLevel"), FEpicodeSandboxValue::MakeInt(0));
		return World;
	}

	TSharedPtr<FJsonObject> MakeChange(const FString& InOp)
	{
		TSharedPtr<FJsonObject> Change = MakeShared<FJsonObject>();
		Change->SetStringField(TEXT("op"), InOp);
		return Change;
	}

	FEpicodeSandboxEvent MakeVariableEvent(int64 InSequence, int64 InTick, const FString& InEntityId, const FString& InItemId, int32 InCount)
	{
		FEpicodeSandboxEvent Event;
		Event.Sequence = InSequence;
		Event.Tick = InTick;
		Event.Type = TEXT("sandbox.interaction");
		Event.ActorId = TEXT("hero");
		Event.Payload = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Changes;
		TSharedPtr<FJsonObject> Variable = MakeChange(TEXT("narrative.variable"));
		Variable->SetStringField(TEXT("name"), TEXT("alarmLevel"));
		Variable->SetField(TEXT("value"), FEpicodeSandboxValue::MakeInt(1).ToJson());
		Changes.Add(MakeShared<FJsonValueObject>(Variable));
		TSharedPtr<FJsonObject> Inventory = MakeChange(TEXT("entity.inventory"));
		Inventory->SetStringField(TEXT("id"), InEntityId);
		Inventory->SetStringField(TEXT("itemId"), InItemId);
		Inventory->SetNumberField(TEXT("count"), InCount);
		Changes.Add(MakeShared<FJsonValueObject>(Inventory));
		Event.Payload->SetArrayField(TEXT("changes"), Changes);
		return Event;
	}

	FEpicodeSandboxEvent MakeVisibleEvent(int64 InSequence, int64 InTick, const FString& InEntityId, bool bInVisible)
	{
		FEpicodeSandboxEvent Event;
		Event.Sequence = InSequence;
		Event.Tick = InTick;
		Event.Type = TEXT("sandbox.director.visibility");
		Event.ActorId = TEXT("director");
		Event.Payload = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> Visible = MakeChange(TEXT("entity.visible"));
		Visible->SetStringField(TEXT("id"), InEntityId);
		Visible->SetBoolField(TEXT("value"), bInVisible);
		Event.Payload->SetArrayField(TEXT("changes"), {MakeShared<FJsonValueObject>(Visible)});
		return Event;
	}

	FEpicodeSandboxEvent MakeLocationEvent(int64 InSequence, int64 InTick, const FString& InEntityId, const FVector& InLocation)
	{
		FEpicodeSandboxEvent Event;
		Event.Sequence = InSequence;
		Event.Tick = InTick;
		Event.Type = TEXT("sandbox.director.location");
		Event.ActorId = TEXT("director");
		Event.Payload = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> Location = MakeChange(TEXT("entity.location"));
		Location->SetStringField(TEXT("id"), InEntityId);
		Location->SetArrayField(TEXT("location"), {
			MakeShared<FJsonValueNumber>(InLocation.X),
			MakeShared<FJsonValueNumber>(InLocation.Y),
			MakeShared<FJsonValueNumber>(InLocation.Z)
		});
		Event.Payload->SetArrayField(TEXT("changes"), {MakeShared<FJsonValueObject>(Location)});
		return Event;
	}

	bool AppendReplayEvent(FEpicodeReplayJournal& InJournal, FEpicodeSandboxWorld& InOutWorld, const FEpicodeSandboxEvent& InEvent, FString& OutError, const FString& InDigestOverride = FString())
	{
		FString ApplyError;
		if (!FEpicodeReplayState::ApplyEvent(InOutWorld, InEvent, ApplyError))
		{
			OutError = ApplyError;
			return false;
		}
		const FString Digest = InDigestOverride.IsEmpty() ? FEpicodeSandboxDigest::ComputeWorldDigest(InOutWorld) : InDigestOverride;
		return InJournal.Append(InEvent, Digest, TEXT("2026-01-01T00:00:00Z"), OutError);
	}

	bool SaveReplayCheckpoint(const FString& InRoot, int64 InSequence, const FEpicodeSandboxWorld& InWorld, int32 InSeed, FString& OutError)
	{
		FEpicodeReplayCheckpoint Checkpoint;
		Checkpoint.Sequence = InSequence;
		Checkpoint.Tick = InWorld.Tick;
		Checkpoint.TickRate = 30;
		Checkpoint.Seed = InSeed;
		Checkpoint.ContentDigest = ReplayContentDigest;
		Checkpoint.World = InWorld;
		Checkpoint.Random.Initialize(InSeed);
		return FEpicodeReplayCheckpoints::Save(InRoot, Checkpoint, OutError);
	}

	struct FReplayFixture
	{
		FString Root;
		FEpicodeSandboxWorld World;
		TArray<FString> Digests;

		bool Init(FString& OutError)
		{
			Root = MakeReplayTempDirectory(TEXT("base"));
			World = MakeReplayWorld();
			if (!SaveReplayCheckpoint(Root, 0, World, 42, OutError))
			{
				return false;
			}
			FEpicodeReplayJournalHeader Header;
			Header.SessionId = TEXT("replay-session");
			Header.ContentDigest = ReplayContentDigest;
			Header.Seed = 42;
			Header.StartSequence = 1;
			FEpicodeReplayJournal Journal;
			if (!Journal.Create(Root, Header, OutError))
			{
				return false;
			}
			const FEpicodeSandboxEvent Event1 = MakeVariableEvent(1, 10, TEXT("hero"), TEXT("keycard"), 1);
			const FEpicodeSandboxEvent Event2 = MakeVisibleEvent(2, 20, TEXT("crate"), false);
			for (const FEpicodeSandboxEvent& Event : {Event1, Event2})
			{
				if (!AppendReplayEvent(Journal, World, Event, OutError))
				{
					return false;
				}
				Digests.Add(FEpicodeSandboxDigest::ComputeWorldDigest(World));
			}
			if (!SaveReplayCheckpoint(Root, 2, World, 42, OutError))
			{
				return false;
			}
			const FEpicodeSandboxEvent Event3 = MakeLocationEvent(3, 30, TEXT("hero"), FVector(5, 0, 0));
			if (!AppendReplayEvent(Journal, World, Event3, OutError))
			{
				return false;
			}
			Digests.Add(FEpicodeSandboxDigest::ComputeWorldDigest(World));
			if (!Journal.Finalize(OutError))
			{
				return false;
			}
			return true;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeReplayJournalTest, "Epicode.Replay.Journal", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeReplayJournalTest::RunTest(const FString& Parameters)
{
	const FString Root = MakeReplayTempDirectory(TEXT("journal"));
	FEpicodeSandboxWorld World = MakeReplayWorld();
	FEpicodeReplayJournalHeader Header;
	Header.SessionId = TEXT("journal-session");
	Header.ContentDigest = ReplayContentDigest;
	Header.Seed = 7;
	FEpicodeReplayJournal Journal;
	FString Error;
	TestTrue(TEXT("create"), Journal.Create(Root, Header, Error));
	TestTrue(TEXT("append one"), AppendReplayEvent(Journal, World, MakeVariableEvent(1, 10, TEXT("hero"), TEXT("keycard"), 1), Error));
	TestTrue(TEXT("append two"), AppendReplayEvent(Journal, World, MakeVisibleEvent(2, 20, TEXT("crate"), false), Error));
	TestTrue(TEXT("finalize"), Journal.Finalize(Error));
	TestEqual(TEXT("last sequence"), Journal.GetLastSequence(), static_cast<int64>(2));
	TestFalse(TEXT("final digest"), Journal.GetFinalDigest().IsEmpty());
	TestFalse(TEXT("append after finalize"), AppendReplayEvent(Journal, World, MakeLocationEvent(3, 30, TEXT("hero"), FVector(1, 0, 0)), Error));
	TestTrue(TEXT("finalized error"), Error.Contains(TEXT("JOURNAL_FINALIZED")));

	FEpicodeReplayJournal Reloaded;
	TestTrue(TEXT("open"), Reloaded.Open(Root, Error));
	TestTrue(TEXT("reloaded finalized"), Reloaded.IsFinalized());
	TestEqual(TEXT("entries"), Reloaded.GetEntries().Num(), 2);
	TestEqual(TEXT("reloaded digest"), Reloaded.GetFinalDigest(), Journal.GetFinalDigest());

	const FString GapRoot = MakeReplayTempDirectory(TEXT("gap"));
	FEpicodeReplayJournal GapJournal;
	TestTrue(TEXT("gap create"), GapJournal.Create(GapRoot, Header, Error));
	FEpicodeSandboxEvent GapEvent = MakeVariableEvent(2, 10, TEXT("hero"), TEXT("keycard"), 1);
	TestFalse(TEXT("sequence gap"), GapJournal.Append(GapEvent, FString(), TEXT("2026-01-01T00:00:00Z"), Error));
	TestTrue(TEXT("gap code"), Error.Contains(TEXT("JOURNAL_SEQUENCE")));

	const FString CorruptRoot = MakeReplayTempDirectory(TEXT("corrupt"));
	FEpicodeReplayJournal CorruptJournal;
	TestTrue(TEXT("corrupt create"), CorruptJournal.Create(CorruptRoot, Header, Error));
	TestTrue(TEXT("corrupt append"), AppendReplayEvent(CorruptJournal, World, MakeVariableEvent(1, 10, TEXT("hero"), TEXT("keycard"), 1), Error));
	TestTrue(TEXT("corrupt finalize"), CorruptJournal.Finalize(Error));
	FString Text;
	FFileHelper::LoadFileToString(Text, *FEpicodeReplayJournal::GetJournalPath(CorruptRoot));
	Text.ReplaceInline(TEXT("\"checksum\":\""), TEXT("\"checksum\":\"0"), ESearchCase::CaseSensitive);
	FFileHelper::SaveStringToFile(Text, *FEpicodeReplayJournal::GetJournalPath(CorruptRoot), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	FEpicodeReplayJournal CorruptOpen;
	TestFalse(TEXT("corrupt rejected"), CorruptOpen.Open(CorruptRoot, Error));
	TestTrue(TEXT("corrupt code"), Error.Contains(TEXT("JOURNAL_CORRUPT")));

	IFileManager::Get().DeleteDirectory(*Root, false, true);
	IFileManager::Get().DeleteDirectory(*GapRoot, false, true);
	IFileManager::Get().DeleteDirectory(*CorruptRoot, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeReplaySnapshotTest, "Epicode.Replay.Snapshot", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeReplaySnapshotTest::RunTest(const FString& Parameters)
{
	FReplayFixture Fixture;
	FString Error;
	TestTrue(TEXT("fixture"), Fixture.Init(Error));
	TestEqual(TEXT("checkpoint list"), FEpicodeReplayCheckpoints::List(Fixture.Root).Num(), 2);
	FEpicodeReplayCheckpoint Checkpoint;
	TestTrue(TEXT("nearest one"), FEpicodeReplayCheckpoints::LoadNearest(Fixture.Root, 1, Checkpoint, Error));
	TestEqual(TEXT("nearest one sequence"), Checkpoint.Sequence, static_cast<int64>(0));
	TestTrue(TEXT("nearest three"), FEpicodeReplayCheckpoints::LoadNearest(Fixture.Root, 3, Checkpoint, Error));
	TestEqual(TEXT("nearest three sequence"), Checkpoint.Sequence, static_cast<int64>(2));
	TestEqual(TEXT("checkpoint digest"), Checkpoint.ContentDigest, ReplayContentDigest);

	FEpicodeSandboxRandomStreams Random;
	Random.Initialize(99);
	Random.GetFloat(TEXT("spawner:test"), 1);
	FEpicodeReplayCheckpoint RandomCheckpoint;
	RandomCheckpoint.Sequence = 10;
	RandomCheckpoint.TickRate = 30;
	RandomCheckpoint.Seed = 99;
	RandomCheckpoint.ContentDigest = ReplayContentDigest;
	RandomCheckpoint.World = Fixture.World;
	RandomCheckpoint.Random = Random;
	const FString RandomRoot = MakeReplayTempDirectory(TEXT("random"));
	TestTrue(TEXT("random save"), FEpicodeReplayCheckpoints::Save(RandomRoot, RandomCheckpoint, Error));
	FEpicodeReplayCheckpoint RandomLoaded;
	TestTrue(TEXT("random load"), FEpicodeReplayCheckpoints::LoadAt(RandomRoot, 10, RandomLoaded, Error));
	const double ExpectedNext = Random.GetFloat(TEXT("spawner:test"), 2);
	const double ActualNext = RandomLoaded.Random.GetFloat(TEXT("spawner:test"), 2);
	TestEqual(TEXT("random resumes"), ActualNext, ExpectedNext);

	const FString MissingRoot = MakeReplayTempDirectory(TEXT("missing"));
	TestFalse(TEXT("missing snapshot"), FEpicodeReplayCheckpoints::LoadNearest(MissingRoot, 1, Checkpoint, Error));
	TestTrue(TEXT("missing code"), Error.Contains(TEXT("SNAPSHOT_MISSING")));

	FString CorruptText;
	FFileHelper::LoadFileToString(CorruptText, *FEpicodeReplayCheckpoints::GetCheckpointPath(Fixture.Root, 2));
	CorruptText.ReplaceInline(TEXT("\"checksum\": \""), TEXT("\"checksum\": \"0"), ESearchCase::CaseSensitive);
	FFileHelper::SaveStringToFile(CorruptText, *FEpicodeReplayCheckpoints::GetCheckpointPath(Fixture.Root, 2), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	FEpicodeReplayCheckpoint CorruptCheckpoint;
	TestFalse(TEXT("corrupt checkpoint"), FEpicodeReplayCheckpoints::LoadAt(Fixture.Root, 2, CorruptCheckpoint, Error));
	TestTrue(TEXT("corrupt checkpoint code"), Error.Contains(TEXT("SNAPSHOT_CORRUPT")));

	IFileManager::Get().DeleteDirectory(*Fixture.Root, false, true);
	IFileManager::Get().DeleteDirectory(*RandomRoot, false, true);
	IFileManager::Get().DeleteDirectory(*MissingRoot, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeReplayGhostTest, "Epicode.Replay.Ghost", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeReplayGhostTest::RunTest(const FString& Parameters)
{
	FReplayFixture Fixture;
	FString Error;
	TestTrue(TEXT("fixture"), Fixture.Init(Error));
	FEpicodeReplayGhost Ghost;
	TestTrue(TEXT("ghost open"), Ghost.Open(Fixture.Root, Error));
	TestTrue(TEXT("ghost read only"), Ghost.IsReadOnly());
	TestEqual(TEXT("ghost first"), Ghost.GetFirstSequence(), static_cast<int64>(1));
	TestEqual(TEXT("ghost last"), Ghost.GetLastSequence(), static_cast<int64>(3));
	FEpicodeReplayGhostPage Page;
	TestTrue(TEXT("ghost page"), Ghost.GetPage(1, 2, Page, Error));
	TestEqual(TEXT("page size"), Page.Entries.Num(), 2);
	TestEqual(TEXT("page first"), Page.Entries[0].Event.Sequence, static_cast<int64>(1));
	TestTrue(TEXT("ghost page empty"), Ghost.GetPage(4, 2, Page, Error));
	TestEqual(TEXT("page empty size"), Page.Entries.Num(), 0);
	TestFalse(TEXT("ghost gap"), Ghost.GetPage(0, 2, Page, Error));
	TestTrue(TEXT("ghost gap code"), Error.Contains(TEXT("REPLAY_HISTORY_UNAVAILABLE")));

	FEpicodeReplayResimResult Result;
	const bool bSeeked = Ghost.Seek(2, Result, Error);
	TestTrue(TEXT("ghost seek"), bSeeked);
	if (!bSeeked)
	{
		AddError(FString::Printf(TEXT("ghost seek error: %s"), *Error));
	}
	AddInfo(FString::Printf(TEXT("ghost seek entities: %d"), Result.World.Entities.Num()));
	TestEqual(TEXT("seek digest"), Result.Digest, Fixture.Digests[1]);
	TestEqual(TEXT("seek card"), Result.World.FindEntity(TEXT("hero"))->GetItemCount(TEXT("keycard")), 1);
	TestFalse(TEXT("seek hidden crate"), Result.World.FindEntity(TEXT("crate"))->bVisible);
	TestEqual(TEXT("seek location"), Result.World.FindEntity(TEXT("hero"))->Location.X, 0.0);

	IFileManager::Get().DeleteDirectory(*Fixture.Root, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeReplayResimTest, "Epicode.Replay.Resim", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeReplayResimTest::RunTest(const FString& Parameters)
{
	FReplayFixture Fixture;
	FString Error;
	TestTrue(TEXT("fixture"), Fixture.Init(Error));
	FEpicodeReplayResimOptions Options;
	Options.ExpectedContentDigest = ReplayContentDigest;
	FEpicodeReplayResimResult Result;
	TestTrue(TEXT("resim target"), FEpicodeReplayResim::Run(Fixture.Root, 3, Options, Result, Error));
	TestTrue(TEXT("resim exact"), Result.bExact);
	TestEqual(TEXT("resim applied"), Result.AppliedEvents, static_cast<int64>(1));
	TestEqual(TEXT("resim digest"), Result.Digest, Fixture.Digests[2]);
	TestEqual(TEXT("resim location"), Result.World.FindEntity(TEXT("hero"))->Location.X, 5.0);
	TestEqual(TEXT("resim variable"), Result.World.Narrative.FindVariable(TEXT("alarmLevel"))->AsInt(), static_cast<int64>(1));

	FEpicodeReplayResimResult Early;
	TestTrue(TEXT("resim early"), FEpicodeReplayResim::Run(Fixture.Root, 2, Options, Early, Error));
	TestEqual(TEXT("early exact"), Early.Digest, Fixture.Digests[1]);

	FEpicodeReplayResimOptions WrongVersion;
	WrongVersion.ExpectedContentDigest = TEXT("content-v2");
	TestFalse(TEXT("version mismatch"), FEpicodeReplayResim::Run(Fixture.Root, 3, WrongVersion, Result, Error));
	TestTrue(TEXT("version code"), Error.Contains(TEXT("REPLAY_VERSION_MISMATCH")));
	WrongVersion.bAllowInexact = true;
	TestTrue(TEXT("inexact allowed"), FEpicodeReplayResim::Run(Fixture.Root, 3, WrongVersion, Result, Error));
	TestTrue(TEXT("inexact flag"), Result.bInexact);
	TestFalse(TEXT("inexact exact"), Result.bExact);

	FEpicodeReplayResimRunner Runner;
	TestTrue(TEXT("runner begin"), Runner.Begin(Fixture.Root, 3, Options, Error));
	Runner.Cancel();
	TestEqual(TEXT("runner cancel"), static_cast<int32>(Runner.Step(10, Error)), static_cast<int32>(EEpicodeReplayStepResult::Cancelled));
	TestTrue(TEXT("runner cancel error"), Error.Contains(TEXT("REPLAY_CANCELLED")));

	TestFalse(TEXT("target unavailable"), FEpicodeReplayResim::Run(Fixture.Root, 99, Options, Result, Error));
	TestTrue(TEXT("target code"), Error.Contains(TEXT("REPLAY_TARGET_UNAVAILABLE")));

	const FString DivergeRoot = MakeReplayTempDirectory(TEXT("diverge"));
	FEpicodeSandboxWorld DivergeWorld = MakeReplayWorld();
	TestTrue(TEXT("diverge checkpoint"), SaveReplayCheckpoint(DivergeRoot, 0, DivergeWorld, 42, Error));
	FEpicodeReplayJournalHeader DivergeHeader;
	DivergeHeader.SessionId = TEXT("diverge-session");
	DivergeHeader.ContentDigest = ReplayContentDigest;
	DivergeHeader.Seed = 42;
	FEpicodeReplayJournal DivergeJournal;
	TestTrue(TEXT("diverge create"), DivergeJournal.Create(DivergeRoot, DivergeHeader, Error));
	TestTrue(TEXT("diverge append"), AppendReplayEvent(DivergeJournal, DivergeWorld, MakeVariableEvent(1, 10, TEXT("hero"), TEXT("keycard"), 1), Error, TEXT("deadbeef")));
	TestTrue(TEXT("diverge finalize"), DivergeJournal.Finalize(Error));
	TestFalse(TEXT("diverge detected"), FEpicodeReplayResim::Run(DivergeRoot, 1, Options, Result, Error));
	TestTrue(TEXT("diverge code"), Error.Contains(TEXT("REPLAY_DIVERGED")));
	TestTrue(TEXT("diverge diff"), Result.Diffs.Num() > 0);

	const FString NoSnapshotRoot = MakeReplayTempDirectory(TEXT("nosnapshot"));
	FEpicodeReplayJournal NoSnapshot;
	TestTrue(TEXT("nosnapshot create"), NoSnapshot.Create(NoSnapshotRoot, DivergeHeader, Error));
	TestTrue(TEXT("nosnapshot append"), AppendReplayEvent(NoSnapshot, DivergeWorld, MakeVariableEvent(1, 10, TEXT("hero"), TEXT("keycard"), 1), Error));
	TestTrue(TEXT("nosnapshot finalize"), NoSnapshot.Finalize(Error));
	TestFalse(TEXT("nosnapshot rejected"), FEpicodeReplayResim::Run(NoSnapshotRoot, 1, Options, Result, Error));
	TestTrue(TEXT("nosnapshot code"), Error.Contains(TEXT("SNAPSHOT_MISSING")));

	IFileManager::Get().DeleteDirectory(*Fixture.Root, false, true);
	IFileManager::Get().DeleteDirectory(*DivergeRoot, false, true);
	IFileManager::Get().DeleteDirectory(*NoSnapshotRoot, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeReplayBranchTest, "Epicode.Replay.Branch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeReplayBranchTest::RunTest(const FString& Parameters)
{
	FReplayFixture Fixture;
	FString Error;
	TestTrue(TEXT("fixture"), Fixture.Init(Error));
	const FString ParentJournalPath = FEpicodeReplayJournal::GetJournalPath(Fixture.Root);
	const FString ParentBefore = [&ParentJournalPath]()
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *ParentJournalPath);
		return Text;
	}();
	const FString BranchRoot = MakeReplayTempDirectory(TEXT("branch")) + TEXT("-branch");
	const bool bBranchCreated = FEpicodeReplayBranch::Create(Fixture.Root, BranchRoot, TEXT("branch-1"), 2, TEXT("director-1"), EEpicodeSandboxParticipantKind::Director, Error);
	TestTrue(TEXT("branch create"), bBranchCreated);
	if (!bBranchCreated)
	{
		AddError(FString::Printf(TEXT("branch create error: %s"), *Error));
	}
	TestTrue(TEXT("branch dir"), IFileManager::Get().DirectoryExists(*BranchRoot));
	TestTrue(TEXT("branch metadata"), FPaths::FileExists(*FEpicodeReplayBranch::GetBranchMetadataPath(BranchRoot)));
	FEpicodeReplayJournal BranchJournal;
	TestTrue(TEXT("branch journal"), BranchJournal.Open(BranchRoot, Error));
	TestEqual(TEXT("branch session"), BranchJournal.GetHeader().SessionId, FString(TEXT("branch-1")));
	TestEqual(TEXT("branch start"), BranchJournal.GetHeader().StartSequence, static_cast<int64>(3));
	TestEqual(TEXT("branch entries"), BranchJournal.GetEntries().Num(), 0);

	FEpicodeSandboxWorld BranchWorld = FEpicodeReplayResimResult().World;
	FEpicodeReplayResimResult ForkResult;
	TestTrue(TEXT("branch resim"), FEpicodeReplayResim::Run(BranchRoot, 2, FEpicodeReplayResimOptions(), ForkResult, Error));
	BranchWorld = ForkResult.World;
	const FEpicodeSandboxEvent BranchEvent = MakeLocationEvent(3, 40, TEXT("hero"), FVector(9, 0, 0));
	TestTrue(TEXT("branch append"), AppendReplayEvent(BranchJournal, BranchWorld, BranchEvent, Error));
	TestTrue(TEXT("branch finalize"), BranchJournal.Finalize(Error));
	TestEqual(TEXT("branch last"), BranchJournal.GetLastSequence(), static_cast<int64>(3));

	FEpicodeReplayJournal ParentAfter;
	TestTrue(TEXT("parent reopen"), ParentAfter.Open(Fixture.Root, Error));
	TestEqual(TEXT("parent unchanged count"), ParentAfter.GetEntries().Num(), 3);
	FString ParentAfterText;
	FFileHelper::LoadFileToString(ParentAfterText, *ParentJournalPath);
	TestEqual(TEXT("parent unchanged bytes"), ParentAfterText, ParentBefore);

	const FString InvalidRoot = MakeReplayTempDirectory(TEXT("invalid")) + TEXT("-branch");
	TestFalse(TEXT("invalid fork"), FEpicodeReplayBranch::Create(Fixture.Root, InvalidRoot, TEXT("branch-bad"), 99, TEXT("director-1"), EEpicodeSandboxParticipantKind::Director, Error));
	TestTrue(TEXT("invalid fork code"), Error.Contains(TEXT("BRANCH_POINT_UNAVAILABLE")));
	TestFalse(TEXT("invalid no dir"), IFileManager::Get().DirectoryExists(*InvalidRoot));
	TestFalse(TEXT("performer forbidden"), FEpicodeReplayBranch::Create(Fixture.Root, InvalidRoot, TEXT("branch-bad"), 2, TEXT("performer-1"), EEpicodeSandboxParticipantKind::Performer, Error));
	TestTrue(TEXT("forbidden code"), Error.Contains(TEXT("FORBIDDEN")));

	IFileManager::Get().DeleteDirectory(*Fixture.Root, false, true);
	IFileManager::Get().DeleteDirectory(*BranchRoot, false, true);
	IFileManager::Get().DeleteDirectory(*InvalidRoot, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeReplayRetentionTest, "Epicode.Replay.Retention", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeReplayRetentionTest::RunTest(const FString& Parameters)
{
	FReplayFixture Fixture;
	FString Error;
	TestTrue(TEXT("fixture"), Fixture.Init(Error));
	TestTrue(TEXT("compact"), FEpicodeReplayRetention::Compact(Fixture.Root, 2, EEpicodeSandboxParticipantKind::Owner, Error));
	FEpicodeReplayJournal Compacted;
	TestTrue(TEXT("compacted open"), Compacted.Open(Fixture.Root, Error));
	TestEqual(TEXT("compacted first"), Compacted.GetFirstSequence(), static_cast<int64>(3));
	TestEqual(TEXT("compacted entries"), Compacted.GetEntries().Num(), 1);
	TestTrue(TEXT("archive"), FPaths::FileExists(*FEpicodeReplayRetention::GetArchivePath(Fixture.Root)));
	TestEqual(TEXT("checkpoints kept"), FEpicodeReplayCheckpoints::List(Fixture.Root).Num(), 1);

	FEpicodeReplayResimResult Result;
	TestFalse(TEXT("history unavailable"), FEpicodeReplayResim::Run(Fixture.Root, 1, FEpicodeReplayResimOptions(), Result, Error));
	TestTrue(TEXT("history code"), Error.Contains(TEXT("REPLAY_HISTORY_UNAVAILABLE")));
	TestTrue(TEXT("compacted replay"), FEpicodeReplayResim::Run(Fixture.Root, 3, FEpicodeReplayResimOptions(), Result, Error));
	TestEqual(TEXT("compacted digest"), Result.Digest, Fixture.Digests[2]);

	FString Forbidden;
	TestFalse(TEXT("retention forbidden"), FEpicodeReplayRetention::Compact(Fixture.Root, 1, EEpicodeSandboxParticipantKind::Viewer, Forbidden));
	TestTrue(TEXT("retention forbidden code"), Forbidden.Contains(TEXT("FORBIDDEN")));

	IFileManager::Get().DeleteDirectory(*Fixture.Root, false, true);

	FReplayFixture PolicyFixture;
	TestTrue(TEXT("policy fixture"), PolicyFixture.Init(Error));
	FEpicodeReplayRetentionPolicy Policy;
	Policy.MaxEvents = 1;
	TestTrue(TEXT("policy apply"), FEpicodeReplayRetention::ApplyPolicy(PolicyFixture.Root, Policy, EEpicodeSandboxParticipantKind::Director, Error));
	FEpicodeReplayJournal PolicyJournal;
	TestTrue(TEXT("policy journal"), PolicyJournal.Open(PolicyFixture.Root, Error));
	TestEqual(TEXT("policy first"), PolicyJournal.GetFirstSequence(), static_cast<int64>(3));
	TestEqual(TEXT("policy entries"), PolicyJournal.GetEntries().Num(), 1);
	IFileManager::Get().DeleteDirectory(*PolicyFixture.Root, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeReplayJobTest, "Epicode.Replay.Job", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeReplayJobTest::RunTest(const FString& Parameters)
{
	FReplayFixture Fixture;
	FString Error;
	TestTrue(TEXT("fixture"), Fixture.Init(Error));
	IFileManager::Get().Delete(*FEpicodeReplayCheckpoints::GetCheckpointPath(Fixture.Root, 2));
	FEpicodeReplayJobQueue Queue;
	Queue.Initialize(1);
	FEpicodeReplayResimOptions Options;
	Options.ExpectedContentDigest = ReplayContentDigest;
	const FString First = Queue.Submit(Fixture.Root, 3, Options, TEXT("director-1"), EEpicodeSandboxParticipantKind::Director, Error);
	const FString Second = Queue.Submit(Fixture.Root, 2, Options, TEXT("director-1"), EEpicodeSandboxParticipantKind::Director, Error);
	TestFalse(TEXT("first submitted"), First.IsEmpty());
	TestFalse(TEXT("second submitted"), Second.IsEmpty());
	TestEqual(TEXT("start one"), Queue.StartQueued(Error), 1);
	TestEqual(TEXT("running"), Queue.GetRunningCount(), 1);
	TestEqual(TEXT("first running"), static_cast<int32>(Queue.FindJob(First)->State), static_cast<int32>(EEpicodeReplayJobState::Running));
	TestEqual(TEXT("second queued"), static_cast<int32>(Queue.FindJob(Second)->State), static_cast<int32>(EEpicodeReplayJobState::Queued));

	TestTrue(TEXT("step first"), Queue.Step(First, 1, Error));
	TestEqual(TEXT("first applied"), Queue.FindJob(First)->AppliedEvents, 1);
	Queue.TickLive(0.033);
	TestEqual(TEXT("live ticks"), Queue.GetLiveTicks(), 1);
	TestEqual(TEXT("first still running"), static_cast<int32>(Queue.FindJob(First)->State), static_cast<int32>(EEpicodeReplayJobState::Running));

	TestTrue(TEXT("cancel first"), Queue.Cancel(First, Error));
	TestEqual(TEXT("first cancelled"), static_cast<int32>(Queue.FindJob(First)->State), static_cast<int32>(EEpicodeReplayJobState::Cancelled));
	TestEqual(TEXT("second starts"), Queue.StartQueued(Error), 1);
	TestTrue(TEXT("second completes"), Queue.RunToCompletion(Second, Error));
	TestEqual(TEXT("second completed"), static_cast<int32>(Queue.FindJob(Second)->State), static_cast<int32>(EEpicodeReplayJobState::Completed));
	TestFalse(TEXT("second digest"), Queue.FindJob(Second)->Digest.IsEmpty());

	TestTrue(TEXT("performer forbidden"), Queue.Submit(Fixture.Root, 3, Options, TEXT("performer-1"), EEpicodeSandboxParticipantKind::Performer, Error).IsEmpty());
	TestTrue(TEXT("forbidden code"), Error.Contains(TEXT("FORBIDDEN")));

	IFileManager::Get().DeleteDirectory(*Fixture.Root, false, true);
	return true;
}
