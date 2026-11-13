#include "Misc/AutomationTest.h"

#include "EpicodeCommand.h"
#include "EpicodeEngineSubsystem.h"
#include "EpicodeProductionDocument.h"
#include "EpicodeProductionLibrary.h"
#include "EpicodeProductionValidation.h"
#include "EpicodeSceneDocument.h"
#include "EpicodeSceneLibrary.h"
#include "EpicodeTypes.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	FString MakeTempDirectory(const FString& InName)
	{
		const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Epicode"), TEXT("ProductionTests"), InName + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
		IFileManager::Get().MakeDirectory(*Directory, true);
		return Directory;
	}

	struct FTestScene
	{
		FEpicodeSceneDocument Document;
		FGuid HeroId;
		FGuid PropId;
	};

	FTestScene MakeSceneWithCharacter()
	{
		FTestScene Result;
		Result.Document = FEpicodeSceneDocument::CreateDefault(TEXT("Production Test Scene"));
		Result.HeroId = FGuid::NewGuid();
		Result.PropId = FGuid::NewGuid();

		FEpicodeEntityRecord Hero;
		Hero.Id = Result.HeroId;
		Hero.Type = EEpicodeEntityType::Character;
		Hero.Name = TEXT("Hero");
		Result.Document.Entities.Add(Hero);

		FEpicodeEntityRecord Prop;
		Prop.Id = Result.PropId;
		Prop.Type = EEpicodeEntityType::Prop;
		Prop.Name = TEXT("Crate");
		Result.Document.Entities.Add(Prop);
		return Result;
	}

	FString SaveScene(const FTestScene& InScene, const FString& InRoot)
	{
		FString Path;
		FString Error;
		FEpicodeSceneLibrary::Save(InScene.Document, InRoot, Path, Error);
		return Path;
	}

	bool HasIssueCode(const FEpicodeProductionValidationResult& InResult, const FString& InCode)
	{
		for (const FEpicodeProductionValidationIssue& Issue : InResult.Issues)
		{
			if (Issue.Code == InCode)
			{
				return true;
			}
		}
		return false;
	}

	struct FTestTransactionSink final : IEpicodeTransactionSink
	{
		int32 Begins = 0;
		int32 Commits = 0;
		int32 Cancels = 0;

		virtual void BeginTransaction(const FString& Description) override { ++Begins; }
		virtual void CommitTransaction() override { ++Commits; }
		virtual void CancelTransaction() override { ++Cancels; }
		virtual void MarkModified(UObject* Object) override {}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeProductionDocumentTest, "Epicode.Production.Document", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeProductionDocumentTest::RunTest(const FString& Parameters)
{
	FEpicodeProductionDocument Document = FEpicodeProductionDocument::CreateDefault(TEXT("scene-1"), TEXT("Document Test"));
	FEpicodeProductionCastMember Member;
	Member.Slot = TEXT("hero");
	Member.EntityId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	Member.ParticipantKind = TEXT("agent");
	Member.Extra = MakeShared<FJsonObject>();
	Member.Extra->SetStringField(TEXT("futureCastField"), TEXT("kept"));
	Document.Cast.Add(Member);
	Document.Extensions->SetStringField(TEXT("studio"), TEXT("epicode"));
	Document.UnknownFields = MakeShared<FJsonObject>();
	Document.UnknownFields->SetObjectField(TEXT("futureTopLevel"), MakeShared<FJsonObject>());
	Document.UnknownFields->GetObjectField(TEXT("futureTopLevel"))->SetNumberField(TEXT("version"), 7);

	TSharedPtr<FJsonObject> Json = Document.ToJson();
	TestEqual(TEXT("schema version"), Json->GetIntegerField(TEXT("schemaVersion")), FEpicodeProductionDocument::CurrentSchemaVersion);
	TestTrue(TEXT("default rules"), Json->GetObjectField(TEXT("worldRules"))->GetArrayField(TEXT("interactionPoints")).Num() == 0);
	TestTrue(TEXT("default narrative"), Json->GetObjectField(TEXT("narrative"))->GetArrayField(TEXT("beats")).Num() == 0);

	FEpicodeProductionDocument Restored;
	FString Error;
	TestTrue(TEXT("roundtrip"), FEpicodeProductionDocument::FromJson(Json, Restored, Error));
	TestEqual(TEXT("name"), Restored.Name, Document.Name);
	TestEqual(TEXT("cast"), Restored.Cast.Num(), 1);
	TestEqual(TEXT("participant kind"), Restored.Cast[0].ParticipantKind, FString(TEXT("agent")));
	TestEqual(TEXT("cast extra"), Restored.Cast[0].Extra->GetStringField(TEXT("futureCastField")), FString(TEXT("kept")));
	TestEqual(TEXT("extensions"), Restored.Extensions->GetStringField(TEXT("studio")), FString(TEXT("epicode")));
	TestTrue(TEXT("unknown top level"), Restored.UnknownFields.IsValid() && Restored.UnknownFields->HasField(TEXT("futureTopLevel")));
	TestEqual(TEXT("unknown nested"), Restored.UnknownFields->GetObjectField(TEXT("futureTopLevel"))->GetIntegerField(TEXT("version")), 7);

	FString ValidationError;
	TestTrue(TEXT("default valid"), Document.Validate(ValidationError));

	TSharedPtr<FJsonObject> LegacyJson = Document.ToJson();
	LegacyJson->RemoveField(TEXT("schemaVersion"));
	LegacyJson->RemoveField(TEXT("worldRules"));
	LegacyJson->RemoveField(TEXT("narrative"));
	FEpicodeProductionDocument Upgraded;
	TestTrue(TEXT("legacy upgraded"), FEpicodeProductionDocument::FromJson(LegacyJson, Upgraded, Error));
	TestTrue(TEXT("pending upgrade"), Upgraded.bPendingUpgrade);
	TestEqual(TEXT("upgraded schema"), Upgraded.SchemaVersion, FEpicodeProductionDocument::CurrentSchemaVersion);
	TestTrue(TEXT("upgraded rules"), Upgraded.WorldRules.IsValid() && Upgraded.WorldRules->HasField(TEXT("budgets")));
	TestTrue(TEXT("upgraded narrative"), Upgraded.Narrative.IsValid() && Upgraded.Narrative->HasField(TEXT("beats")));

	TSharedPtr<FJsonObject> FutureJson = Document.ToJson();
	FutureJson->SetNumberField(TEXT("schemaVersion"), 99);
	FEpicodeProductionDocument Future;
	TestFalse(TEXT("future version rejected"), FEpicodeProductionDocument::FromJson(FutureJson, Future, Error));
	TestTrue(TEXT("future error"), Error.Contains(TEXT("PRODUCTION_VERSION_UNSUPPORTED")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeProductionLibraryTest, "Epicode.Production.Library", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeProductionLibraryTest::RunTest(const FString& Parameters)
{
	const FString Root = MakeTempDirectory(TEXT("library"));
	const FString Trash = MakeTempDirectory(TEXT("trash"));
	FEpicodeProductionDocument Document = FEpicodeProductionDocument::CreateDefault(TEXT("scene-library"), TEXT("Library Production"));
	FString Path;
	FString Error;
	TestTrue(TEXT("create"), FEpicodeProductionLibrary::Create(Document, Root, Path, Error));
	TestTrue(TEXT("document exists"), FPaths::FileExists(Path));

	FEpicodeProductionLibraryEntry Entry;
	TestTrue(TEXT("find"), FEpicodeProductionLibrary::Find(Root, Document.ProductionId, Entry));
	TestEqual(TEXT("found name"), Entry.Name, Document.Name);
	TArray<FEpicodeProductionLibraryEntry> Entries = FEpicodeProductionLibrary::Scan(Root, Document.ProductionId);
	TestEqual(TEXT("scan count"), Entries.Num(), 1);
	TestTrue(TEXT("current entry"), Entries[0].bCurrent);
	TestTrue(TEXT("available entry"), Entries[0].bAvailable);

	Document.Name = TEXT("Library Production Renamed");
	TestTrue(TEXT("save"), FEpicodeProductionLibrary::Save(Document, Root, Path, Error));
	FEpicodeProductionDocument Loaded;
	TestTrue(TEXT("load"), FEpicodeProductionLibrary::Load(Path, Loaded, Error));
	TestEqual(TEXT("renamed"), Loaded.Name, FString(TEXT("Library Production Renamed")));

	FEpicodeProductionDocument Copy;
	FString CopyPath;
	TestTrue(TEXT("save as"), FEpicodeProductionLibrary::SaveAs(Loaded, Root, TEXT("Library Copy"), Copy, CopyPath, Error));
	TestNotEqual(TEXT("new id"), Copy.ProductionId, Loaded.ProductionId);
	TestEqual(TEXT("copy name"), Copy.Name, FString(TEXT("Library Copy")));
	TestTrue(TEXT("copy exists"), FPaths::FileExists(CopyPath));
	TestEqual(TEXT("scan after copy"), FEpicodeProductionLibrary::Scan(Root, Copy.ProductionId).Num(), 2);

	FString TrashPath;
	TestFalse(TEXT("current delete protected"), FEpicodeProductionLibrary::Delete(Entry, Trash, Document.ProductionId, TrashPath, Error));
	TestTrue(TEXT("current delete code"), Error.Contains(TEXT("PRODUCTION_IN_USE")));
	FEpicodeProductionLibraryEntry CopyEntry;
	TestTrue(TEXT("find copy"), FEpicodeProductionLibrary::Find(Root, Copy.ProductionId, CopyEntry));
	TestTrue(TEXT("delete copy"), FEpicodeProductionLibrary::Delete(CopyEntry, Trash, TEXT(""), TrashPath, Error));
	TestTrue(TEXT("trash path exists"), IFileManager::Get().DirectoryExists(*TrashPath));
	TestFalse(TEXT("copy gone"), FEpicodeProductionLibrary::Find(Root, Copy.ProductionId, Entry));

	IFileManager::Get().DeleteDirectory(*Root, false, true);
	IFileManager::Get().DeleteDirectory(*Trash, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeProductionValidationTest, "Epicode.Production.Validation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeProductionValidationTest::RunTest(const FString& Parameters)
{
	const FString SceneRoot = MakeTempDirectory(TEXT("scenes"));
	const FTestScene Scene = MakeSceneWithCharacter();
	SaveScene(Scene, SceneRoot);

	FEpicodeProductionDocument Document = FEpicodeProductionDocument::CreateDefault(Scene.Document.SceneId, TEXT("Valid Production"));
	FEpicodeProductionCastMember Hero;
	Hero.Slot = TEXT("hero");
	Hero.EntityId = Scene.HeroId.ToString(EGuidFormats::DigitsWithHyphens);
	Hero.ParticipantKind = TEXT("performer");
	Document.Cast.Add(Hero);

	FEpicodeProductionValidationResult Result;
	TestTrue(TEXT("valid binding"), FEpicodeProductionValidator::ValidateBinding(Document, SceneRoot, Result));
	TestTrue(TEXT("scene digest"), !Result.SceneDigest.IsEmpty());
	TestEqual(TEXT("entity count"), Result.EntityCount, 2);

	FEpicodeProductionDocument WrongType = Document;
	WrongType.Cast[0].EntityId = Scene.PropId.ToString(EGuidFormats::DigitsWithHyphens);
	TestFalse(TEXT("wrong type rejected"), FEpicodeProductionValidator::ValidateBinding(WrongType, SceneRoot, Result));
	TestTrue(TEXT("type code"), HasIssueCode(Result, TEXT("CAST_ENTITY_TYPE_INVALID")));
	TestTrue(TEXT("type path"), Result.Issues[0].Path.Contains(TEXT("cast[0].entityId")));

	FEpicodeProductionDocument DuplicateSlots = Document;
	FEpicodeProductionCastMember Second;
	Second.Slot = TEXT("HERO");
	Second.EntityId = Scene.PropId.ToString(EGuidFormats::DigitsWithHyphens);
	Second.ParticipantKind = TEXT("viewer");
	DuplicateSlots.Cast.Add(Second);
	TestFalse(TEXT("duplicate slot rejected"), FEpicodeProductionValidator::ValidateBinding(DuplicateSlots, SceneRoot, Result));
	TestTrue(TEXT("duplicate slot code"), HasIssueCode(Result, TEXT("DUPLICATE_CAST_SLOT")));

	FEpicodeProductionDocument MissingEntity = Document;
	MissingEntity.Cast[0].EntityId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	TestFalse(TEXT("missing entity rejected"), FEpicodeProductionValidator::ValidateBinding(MissingEntity, SceneRoot, Result));
	TestTrue(TEXT("missing entity code"), HasIssueCode(Result, TEXT("PRODUCTION_REFERENCE_NOT_FOUND")));

	FEpicodeProductionDocument MissingScene = Document;
	MissingScene.SceneId = TEXT("missing-scene");
	TestFalse(TEXT("missing scene rejected"), FEpicodeProductionValidator::ValidateBinding(MissingScene, SceneRoot, Result));
	TestTrue(TEXT("missing scene code"), HasIssueCode(Result, TEXT("SCENE_NOT_FOUND")));

	const FString FutureSceneId = TEXT("future-scene");
	const FString FutureDirectory = FEpicodeSceneLibrary::GetSceneDirectory(SceneRoot, FutureSceneId);
	IFileManager::Get().MakeDirectory(*FutureDirectory, true);
	FFileHelper::SaveStringToFile(TEXT("{\"schemaVersion\":99,\"sceneId\":\"future-scene\",\"name\":\"Future\"}"), *FPaths::Combine(FutureDirectory, TEXT("scene.json")));
	FEpicodeProductionDocument FutureScene = Document;
	FutureScene.SceneId = FutureSceneId;
	TestFalse(TEXT("future scene rejected"), FEpicodeProductionValidator::ValidateBinding(FutureScene, SceneRoot, Result));
	TestTrue(TEXT("future scene code"), HasIssueCode(Result, TEXT("SCENE_VERSION_UNSUPPORTED")));

	FEpicodeProductionDocument EscapedPath = Document;
	TSharedPtr<FJsonObject> Interaction = MakeShared<FJsonObject>();
	Interaction->SetStringField(TEXT("id"), TEXT("interaction-1"));
	Interaction->SetStringField(TEXT("entityId"), Hero.EntityId);
	Interaction->SetStringField(TEXT("assetPath"), TEXT("../secret.uasset"));
	EscapedPath.WorldRules->SetArrayField(TEXT("interactionPoints"), {MakeShared<FJsonValueObject>(Interaction)});
	TestFalse(TEXT("escaped path rejected"), FEpicodeProductionValidator::ValidateBinding(EscapedPath, SceneRoot, Result));
	TestTrue(TEXT("path code"), HasIssueCode(Result, TEXT("PATH_NOT_ALLOWED")));

	FEpicodeProductionDocument DuplicateRule = Document;
	TSharedPtr<FJsonObject> First = MakeShared<FJsonObject>();
	First->SetStringField(TEXT("id"), TEXT("same-id"));
	TSharedPtr<FJsonObject> SecondRule = MakeShared<FJsonObject>();
	SecondRule->SetStringField(TEXT("id"), TEXT("same-id"));
	DuplicateRule.WorldRules->SetArrayField(TEXT("zones"), {MakeShared<FJsonValueObject>(First), MakeShared<FJsonValueObject>(SecondRule)});
	TestFalse(TEXT("duplicate rule rejected"), FEpicodeProductionValidator::ValidateDocument(DuplicateRule, Result));
	TestTrue(TEXT("duplicate rule code"), HasIssueCode(Result, TEXT("DUPLICATE_RULE_ID")));

	IFileManager::Get().DeleteDirectory(*SceneRoot, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeProductionLegacyTest, "Epicode.Production.Legacy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeProductionLegacyTest::RunTest(const FString& Parameters)
{
	const FTestScene Scene = MakeSceneWithCharacter();
	const FEpicodeProductionDocument Legacy = FEpicodeProductionLegacy::MakeDefaultView(Scene.Document);
	TestTrue(TEXT("legacy id"), Legacy.ProductionId.StartsWith(TEXT("legacy-")));
	TestEqual(TEXT("legacy scene"), Legacy.SceneId, Scene.Document.SceneId);
	TestEqual(TEXT("legacy name"), Legacy.Name, Scene.Document.Name);
	TestEqual(TEXT("legacy cast"), Legacy.Cast.Num(), 1);
	TestEqual(TEXT("legacy cast kind"), Legacy.Cast[0].ParticipantKind, FString(TEXT("performer")));
	TestTrue(TEXT("legacy default rules"), Legacy.WorldRules.IsValid() && Legacy.WorldRules->GetObjectField(TEXT("budgets"))->GetIntegerField(TEXT("maxEntities")) > 0);
	TestTrue(TEXT("legacy narrative"), Legacy.Narrative.IsValid());
	TestEqual(TEXT("legacy beats"), Legacy.Narrative->GetArrayField(TEXT("beats")).Num(), 0);
	TestEqual(TEXT("legacy cues"), Legacy.Narrative->GetArrayField(TEXT("cues")).Num(), 0);
	TestEqual(TEXT("legacy entry"), Legacy.Narrative->GetStringField(TEXT("entryBeatId")), FString());

	const FString Root = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Epicode"), TEXT("ProductionLegacyTests"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
	TestFalse(TEXT("legacy view does not write disk"), IFileManager::Get().DirectoryExists(*Root));
	TestEqual(TEXT("legacy scan empty"), FEpicodeProductionLibrary::Scan(Root, Legacy.ProductionId).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeProductionExampleTest, "Epicode.Production.Example", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeProductionExampleTest::RunTest(const FString& Parameters)
{
	const FString ExamplesRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Docs/Examples")));
	const FString ProductionPath = FPaths::Combine(ExamplesRoot, TEXT("Production/production.json"));
	const FString SceneRoot = FPaths::Combine(ExamplesRoot, TEXT("Scenes"));
	TestTrue(TEXT("example production exists"), FPaths::FileExists(ProductionPath));
	TestTrue(TEXT("example scene root exists"), IFileManager::Get().DirectoryExists(*SceneRoot));
	if (!FPaths::FileExists(ProductionPath))
	{
		return false;
	}

	FEpicodeProductionDocument Document;
	FString Error;
	TestTrue(TEXT("example loads"), FEpicodeProductionLibrary::Load(ProductionPath, Document, Error));
	TestEqual(TEXT("example name"), Document.Name, FString(TEXT("Rooftop Heist Example")));
	TestEqual(TEXT("example cast"), Document.Cast.Num(), 1);
	FEpicodeProductionValidationResult Result;
	TestTrue(TEXT("example validates"), FEpicodeProductionValidator::ValidateBinding(Document, SceneRoot, Result));
	TestFalse(TEXT("example digest"), Result.SceneDigest.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeProductionUndoTest, "Epicode.Production.Undo", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeProductionUndoTest::RunTest(const FString& Parameters)
{
	FEpicodeProductionDocument Document = FEpicodeProductionDocument::CreateDefault(TEXT("scene-undo"), TEXT("Undo Production"));
	FEpicodeProductionEditSession Session(Document);
	FString Error;

	TestTrue(TEXT("rename apply"), Session.Apply(TEXT("rename"), [](FEpicodeProductionDocument& InDocument, FString& OutError)
	{
		InDocument.Name = TEXT("Renamed");
		return true;
	}, Error));
	TestEqual(TEXT("renamed in memory"), Session.GetDocument().Name, FString(TEXT("Renamed")));
	TestTrue(TEXT("dirty"), Session.IsDirty());
	TestEqual(TEXT("undo count"), Session.GetUndoCount(), 1);

	TestTrue(TEXT("undo"), Session.Undo(Error));
	TestEqual(TEXT("undo restored"), Session.GetDocument().Name, FString(TEXT("Undo Production")));
	TestEqual(TEXT("redo count"), Session.GetRedoCount(), 1);
	TestTrue(TEXT("redo"), Session.Redo(Error));
	TestEqual(TEXT("redo applied"), Session.GetDocument().Name, FString(TEXT("Renamed")));

	FTestTransactionSink Sink;
	Session.SetTransactionSink(&Sink);
	TestTrue(TEXT("transaction apply"), Session.Apply(TEXT("transaction rename"), [](FEpicodeProductionDocument& InDocument, FString& OutError)
	{
		InDocument.Name = TEXT("Transaction Renamed");
		return true;
	}, Error));
	TestEqual(TEXT("transaction began"), Sink.Begins, 1);
	TestEqual(TEXT("transaction committed"), Sink.Commits, 1);

	const int32 UndoBeforeFailure = Session.GetUndoCount();
	TestFalse(TEXT("duplicate slots rejected"), Session.Apply(TEXT("invalid"), [](FEpicodeProductionDocument& InDocument, FString& OutError)
	{
		FEpicodeProductionCastMember First;
		First.Slot = TEXT("same");
		First.EntityId = TEXT("entity-a");
		FEpicodeProductionCastMember Second;
		Second.Slot = TEXT("same");
		Second.EntityId = TEXT("entity-b");
		InDocument.Cast.Add(First);
		InDocument.Cast.Add(Second);
		return true;
	}, Error));
	TestTrue(TEXT("failure error"), Error.Contains(TEXT("DUPLICATE_CAST_SLOT")));
	TestEqual(TEXT("failure begins transaction"), Sink.Begins, 2);
	TestEqual(TEXT("failure cancels transaction"), Sink.Cancels, 1);
	TestEqual(TEXT("failure does not add undo"), Session.GetUndoCount(), UndoBeforeFailure);
	TestEqual(TEXT("failure keeps document"), Session.GetDocument().Cast.Num(), 0);

	TestFalse(TEXT("mutation failure rejected"), Session.Apply(TEXT("fail"), [](FEpicodeProductionDocument& InDocument, FString& OutError)
	{
		OutError = TEXT("mutation failed");
		return false;
	}, Error));
	TestEqual(TEXT("mutation failure message"), Error, FString(TEXT("mutation failed")));

	TestTrue(TEXT("batch apply"), Session.Apply(TEXT("batch"), [](FEpicodeProductionDocument& InDocument, FString& OutError)
	{
		InDocument.Name = TEXT("Batch Renamed");
		InDocument.WorldRules->GetObjectField(TEXT("budgets"))->SetNumberField(TEXT("maxEntities"), 250);
		InDocument.Narrative->SetStringField(TEXT("entryBeatId"), TEXT("beat-1"));
		return true;
	}, Error));
	TestEqual(TEXT("batch name"), Session.GetDocument().Name, FString(TEXT("Batch Renamed")));
	TestEqual(TEXT("batch budget"), Session.GetDocument().WorldRules->GetObjectField(TEXT("budgets"))->GetIntegerField(TEXT("maxEntities")), 250);

	TestTrue(TEXT("batch undo"), Session.Undo(Error));
	TestEqual(TEXT("batch undo name"), Session.GetDocument().Name, FString(TEXT("Transaction Renamed")));

	const FString Root = MakeTempDirectory(TEXT("undo"));
	FString DocumentPath;
	TestTrue(TEXT("save clean"), Session.Save(Root, DocumentPath, Error));
	TestFalse(TEXT("clean after save"), Session.IsDirty());
	FString Baseline;
	TestTrue(TEXT("baseline read"), FFileHelper::LoadFileToString(Baseline, *DocumentPath));
	TestTrue(TEXT("post-save edit"), Session.Apply(TEXT("post-save"), [](FEpicodeProductionDocument& InDocument, FString& OutError)
	{
		InDocument.Name = TEXT("Unsaved Rename");
		return true;
	}, Error));
	TestFalse(TEXT("unwritable save rejected"), Session.Save(TEXT("/proc/epicode-unwritable"), DocumentPath, Error));
	FString Preserved;
	TestTrue(TEXT("preserved read"), FFileHelper::LoadFileToString(Preserved, *DocumentPath));
	TestEqual(TEXT("disk preserved"), Preserved, Baseline);
	TestTrue(TEXT("dirty after failed save"), Session.IsDirty());
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeProductionCommandTest, "Epicode.Production.Command", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeProductionCommandTest::RunTest(const FString& Parameters)
{
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	TestNotNull(TEXT("engine subsystem"), Engine);
	if (!Engine || !Engine->GetCommandRegistry())
	{
		return false;
	}

	const FString ExamplesRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Docs/Examples")));
	FEpicodeProductionDocument Document;
	FString Error;
	TestTrue(TEXT("example loads"), FEpicodeProductionLibrary::Load(FPaths::Combine(ExamplesRoot, TEXT("Production/production.json")), Document, Error));

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetObjectField(TEXT("document"), Document.ToJson());
	Params->SetStringField(TEXT("sceneRoot"), FPaths::Combine(ExamplesRoot, TEXT("Scenes")));
	FEpicodeCommandResult Result;
	TestTrue(TEXT("validate executes"), Engine->GetCommandRegistry()->Execute(TEXT("production.validate"), Params, FEpicodeCommandContext(), Result, Error));
	TestTrue(TEXT("validate succeeds"), Result.bSuccess);
	TestTrue(TEXT("validate result"), Result.Result.IsValid());
	if (Result.Result.IsValid())
	{
		TestEqual(TEXT("validate production"), Result.Result->GetStringField(TEXT("productionId")), Document.ProductionId);
		TestTrue(TEXT("validate scene digest"), !Result.Result->GetStringField(TEXT("sceneDigest")).IsEmpty());
	}

	TSharedPtr<FJsonObject> InvalidJson = Document.ToJson();
	InvalidJson->GetArrayField(TEXT("cast"))[0]->AsObject()->SetStringField(TEXT("entityId"), TEXT("33333333-3333-4333-8333-333333333333"));
	TSharedPtr<FJsonObject> InvalidParams = MakeShared<FJsonObject>();
	InvalidParams->SetObjectField(TEXT("document"), InvalidJson);
	InvalidParams->SetStringField(TEXT("sceneRoot"), FPaths::Combine(ExamplesRoot, TEXT("Scenes")));
	FEpicodeCommandResult InvalidResult;
	TestTrue(TEXT("invalid executes"), Engine->GetCommandRegistry()->Execute(TEXT("production.validate"), InvalidParams, FEpicodeCommandContext(), InvalidResult, Error));
	TestFalse(TEXT("invalid fails"), InvalidResult.bSuccess);
	TestEqual(TEXT("invalid code"), InvalidResult.ErrorCode, FString(TEXT("CAST_ENTITY_TYPE_INVALID")));
	return true;
}
