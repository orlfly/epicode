#include "Misc/AutomationTest.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "EpicodeCommand.h"
#include "EpicodeEngineSubsystem.h"
#include "EpicodeEntityActors.h"
#include "EpicodeEventBus.h"
#include "EpicodeSceneDocument.h"
#include "EpicodeSceneLibrary.h"
#include "EpicodeScopedTestSceneRoot.h"
#include "EpicodeSceneSubsystem.h"
#include "EpicodeSettings.h"
#include "EpicodeTypes.h"
#include "HAL/FileManager.h"
#include "Misc/Base64.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FEpicodeEntityRecord MakeEntity(const FString& Name, EEpicodeEntityType Type = EEpicodeEntityType::Group, const FGuid& Parent = FGuid())
	{
		FEpicodeEntityRecord Record;
		Record.Id = FGuid::NewGuid();
		Record.Type = Type;
		Record.Name = Name;
		Record.ParentId = Parent;
		return Record;
	}

	FEpicodeAgentSpawnPoint DefaultSpawn(const FString& Id)
	{
		FEpicodeAgentSpawnPoint Spawn;
		Spawn.Id = Id;
		return Spawn;
	}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeRecordTest, "Epicode.Core.Record", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeRecordTest::RunTest(const FString& Parameters)
{
	FEpicodeEntityRecord Record = MakeEntity(TEXT("Hero"), EEpicodeEntityType::Character);
	Record.Transform.Location = FVector(1.0, 2.0, 3.0);
	Record.Transform.Rotation = FRotator(10.0, 20.0, 30.0);
	Record.Data->SetStringField(TEXT("mesh"), TEXT("/Game/Hero"));

	const TSharedPtr<FJsonObject> Json = Record.ToJson();
	FEpicodeEntityRecord Loaded;
	FString Error;
	TestTrue(TEXT("record round trip"), FEpicodeEntityRecord::FromJson(Json, Loaded, Error));
	TestEqual(TEXT("name"), Loaded.Name, Record.Name);
	TestTrue(TEXT("id"), Loaded.Id == Record.Id);
	TestEqual(TEXT("type"), Loaded.GetTypeName(), FString(TEXT("character")));
	TestTrue(TEXT("location"), Loaded.Transform.Location.Equals(Record.Transform.Location));

	Loaded.Transform.Location.X = NAN;
	TestFalse(TEXT("NaN transform rejected"), Loaded.IsValid());

	EEpicodeEntityType Parsed = EEpicodeEntityType::Group;
	TestFalse(TEXT("unknown type rejected"), ParseEntityType(TEXT("spaceship"), Parsed));
	TestTrue(TEXT("known type parsed"), ParseEntityType(TEXT("prop"), Parsed));
	TestEqual(TEXT("prop type"), LexToString(Parsed), FString(TEXT("prop")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeSceneDocumentTest, "Epicode.Core.SceneDocument", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeSceneDocumentTest::RunTest(const FString& Parameters)
{
	FEpicodeSceneDocument Document = FEpicodeSceneDocument::CreateDefault(TEXT("Round Trip"));
	FEpicodeEntityRecord Root = MakeEntity(TEXT("Root"));
	FEpicodeEntityRecord Child = MakeEntity(TEXT("Child"), EEpicodeEntityType::Prop, Root.Id);
	Document.Entities.Add(Root);
	Document.Entities.Add(Child);

	const TSharedPtr<FJsonObject> Json = Document.ToJson();
	FEpicodeSceneDocument Loaded;
	FString Error;
	TestTrue(TEXT("document round trip"), FEpicodeSceneDocument::FromJson(Json, Loaded, Error));
	TestEqual(TEXT("name"), Loaded.Name, FString(TEXT("Round Trip")));
	TestEqual(TEXT("entity count"), Loaded.Entities.Num(), 2);
	TestTrue(TEXT("revision advanced"), Loaded.Revision > 0);

	TSharedPtr<FJsonObject> Future = Document.ToJson();
	Future->SetNumberField(TEXT("schemaVersion"), 99);
	TestFalse(TEXT("future schema rejected"), FEpicodeSceneDocument::FromJson(Future, Loaded, Error));

	FEpicodeSceneDocument Duplicate = FEpicodeSceneDocument::CreateDefault(TEXT("Duplicate"));
	FEpicodeEntityRecord First = MakeEntity(TEXT("A"));
	FEpicodeEntityRecord Second = MakeEntity(TEXT("B"));
	Second.Id = First.Id;
	Duplicate.Entities.Add(First);
	Duplicate.Entities.Add(Second);
	TestFalse(TEXT("duplicate id rejected"), Duplicate.Validate(Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeHierarchyTest, "Epicode.Core.Hierarchy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeSceneMigrationTest, "Epicode.Core.Migration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeSceneMigrationTest::RunTest(const FString& Parameters)
{
	const FEpicodeEntityRecord LegacyEntity = MakeEntity(TEXT("Legacy Actor"), EEpicodeEntityType::Prop);
	TSharedPtr<FJsonObject> Legacy = MakeShared<FJsonObject>();
	Legacy->SetStringField(TEXT("sceneId"), TEXT("legacy-scene"));
	Legacy->SetStringField(TEXT("name"), TEXT("Legacy"));
	Legacy->SetStringField(TEXT("levelPath"), TEXT("/Game/Legacy"));
	Legacy->SetArrayField(TEXT("actors"), { MakeShared<FJsonValueObject>(LegacyEntity.ToJson()) });

	FEpicodeSceneDocument Migrated;
	FString Error;
	TestTrue(TEXT("version 0 document migrates"), FEpicodeSceneDocument::FromJson(Legacy, Migrated, Error));
	TestEqual(TEXT("schema version upgraded"), Migrated.SchemaVersion, FEpicodeSceneDocument::CurrentSchemaVersion);
	TestEqual(TEXT("legacy actors become entities"), Migrated.Entities.Num(), 1);
	TestTrue(TEXT("default environment added"), Migrated.Environment.IsValid());
	TestTrue(TEXT("active camera field added"), Migrated.ActiveCameraId.IsEmpty());

	TSharedPtr<FJsonObject> Invalid = MakeShared<FJsonObject>();
	TestFalse(TEXT("null-ish migration rejected"), FEpicodeSceneMigration::Migrate(Invalid, 5, 1, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeCameraTest, "Epicode.Core.Camera", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeCameraTest::RunTest(const FString& Parameters)
{
	const double FiftyMillimeterFov = FEpicodeCamera::FocalLengthToFieldOfView(50.0);
	TestTrue(TEXT("50mm is about 39.6 degrees"), FMath::IsNearlyEqual(FiftyMillimeterFov, 39.5978, 0.01));
	TestTrue(TEXT("focal length round trip"), FMath::IsNearlyEqual(FEpicodeCamera::FieldOfViewToFocalLength(FiftyMillimeterFov), 50.0, 0.01));
	TestEqual(TEXT("invalid focal length falls back"), FEpicodeCamera::FocalLengthToFieldOfView(0.0), 90.0);
	TestEqual(TEXT("invalid field of view rejected"), FEpicodeCamera::FieldOfViewToFocalLength(180.0), 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeTokenTest, "Epicode.Core.Token", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeTokenTest::RunTest(const FString& Parameters)
{
	const FString Token = UEpicodeSettings::GenerateToken(false);
	TArray<uint8> Decoded;
	TestTrue(TEXT("fallback token is base64"), FBase64::Decode(Token, Decoded));
	TestEqual(TEXT("fallback token is 32 bytes"), Decoded.Num(), 32);
	return true;
}

bool FEpicodeHierarchyTest::RunTest(const FString& Parameters)
{
	FEpicodeSceneDocument Document = FEpicodeSceneDocument::CreateDefault(TEXT("Hierarchy"));
	FEpicodeEntityRecord Root = MakeEntity(TEXT("Root"));
	FEpicodeEntityRecord Child = MakeEntity(TEXT("Child"), EEpicodeEntityType::Prop, Root.Id);
	FEpicodeEntityRecord Grandchild = MakeEntity(TEXT("Grandchild"), EEpicodeEntityType::Prop, Child.Id);
	Document.Entities.Add(Root);
	Document.Entities.Add(Child);
	Document.Entities.Add(Grandchild);

	FString Error;
	TestFalse(TEXT("cycle rejected"), Document.ReparentEntity(Root.Id, Grandchild.Id, false, Error));
	TestEqual(TEXT("children of root"), Document.GetChildren(Root.Id).Num(), 1);
	TestTrue(TEXT("descendant detected"), Document.IsDescendant(Root.Id, Grandchild.Id));

	FGuid CopyId;
	TestTrue(TEXT("duplicate"), Document.DuplicateEntity(Root.Id, CopyId, FVector(10.0, 0.0, 0.0), Error));
	TestTrue(TEXT("copy has new id"), CopyId != Root.Id);
	TestEqual(TEXT("copy count"), Document.Entities.Num(), 4);

	TArray<FString> Removed;
	Document.RemoveEntityCascade(Root.Id, Removed);
	TestEqual(TEXT("cascade removed"), Removed.Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeCommandRegistryTest, "Epicode.Core.CommandRegistry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeCommandRegistryTest::RunTest(const FString& Parameters)
{
	FEpicodeCommandRegistry Registry;
	Registry.Register(TEXT("test.echo"), TEXT("Echo"), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("value"), Params->GetStringField(TEXT("value")));
		return FEpicodeCommandResult::Success(Result);
	}, false);

	TestTrue(TEXT("registered"), Registry.Has(TEXT("test.echo")));
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("value"), TEXT("hello"));
	FEpicodeCommandResult Result;
	FString Error;
	TestTrue(TEXT("execute"), Registry.Execute(TEXT("test.echo"), Params, FEpicodeCommandContext(), Result, Error));
	TestEqual(TEXT("echo"), Result.Result->GetStringField(TEXT("value")), FString(TEXT("hello")));

	TestFalse(TEXT("unknown method"), Registry.Execute(TEXT("test.missing"), Params, FEpicodeCommandContext(), Result, Error));
	TestEqual(TEXT("unknown method reported"), Error, FString(TEXT("test.missing")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeEventBusTest, "Epicode.Core.Events", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeEventBusTest::RunTest(const FString& Parameters)
{
	FEpicodeEventBus Bus;
	const uint64 First = Bus.Publish(TEXT("entity.created"), TEXT("scene"), TEXT("a"));
	const uint64 Second = Bus.Publish(TEXT("entity.renamed"), TEXT("scene"), TEXT("a"));
	const uint64 Third = Bus.Publish(TEXT("entity.deleted"), TEXT("scene"), TEXT("a"));
	TestTrue(TEXT("monotonic"), First < Second && Second < Third);
	TestEqual(TEXT("latest"), Bus.GetLatestSequence(), Third);

	const TArray<FEpicodeEvent> Since = Bus.GetSince(First, 10);
	TestEqual(TEXT("since count"), Since.Num(), 2);
	TestEqual(TEXT("since first type"), Since[0].Type, FString(TEXT("entity.renamed")));
	TestEqual(TEXT("history size"), Bus.GetHistorySize(), 3);
	Bus.Reset();
	TestEqual(TEXT("reset"), Bus.GetLatestSequence(), static_cast<uint64>(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeCommandsTest, "Epicode.Core.Commands", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeCommandsTest::RunTest(const FString& Parameters)
{
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
	if (!Engine || !Scene)
	{
		AddError(TEXT("Epicode subsystems are unavailable."));
		return false;
	}
	FString Error;
	if (Scene->HasScene())
	{
		Scene->CloseScene(true, Error);
	}
	if (!Scene->NewScene(TEXT("Command Test"), Error))
	{
		AddError(Error);
		return false;
	}
	const int32 Baseline = Scene->GetDocument().Entities.Num();

	TSharedPtr<FJsonObject> InvalidParams = MakeShared<FJsonObject>();
	InvalidParams->SetStringField(TEXT("type"), TEXT("spaceship"));
	InvalidParams->SetStringField(TEXT("name"), TEXT("Invalid"));
	const FEpicodeCommandResult Invalid = Engine->ExecuteCommand(TEXT("entity.create"), InvalidParams, FEpicodeCommandContext());
	TestFalse(TEXT("invalid command fails"), Invalid.bSuccess);
	TestEqual(TEXT("invalid command has no side effects"), Scene->GetDocument().Entities.Num(), Baseline);

	TArray<TSharedPtr<FJsonValue>> Commands;
	TSharedPtr<FJsonObject> CreateCommand = MakeShared<FJsonObject>();
	CreateCommand->SetStringField(TEXT("method"), TEXT("entity.create"));
	TSharedPtr<FJsonObject> CreateParams = MakeShared<FJsonObject>();
	CreateParams->SetStringField(TEXT("type"), TEXT("group"));
	CreateParams->SetStringField(TEXT("name"), TEXT("Rollback Group"));
	CreateCommand->SetObjectField(TEXT("params"), CreateParams);
	Commands.Add(MakeShared<FJsonValueObject>(CreateCommand));
	TSharedPtr<FJsonObject> BadCommand = MakeShared<FJsonObject>();
	BadCommand->SetStringField(TEXT("method"), TEXT("entity.missing"));
	Commands.Add(MakeShared<FJsonValueObject>(BadCommand));
	TSharedPtr<FJsonObject> TransactionParams = MakeShared<FJsonObject>();
	TransactionParams->SetArrayField(TEXT("commands"), Commands);
	const FEpicodeCommandResult Transaction = Engine->ExecuteCommand(TEXT("scene.transaction"), TransactionParams, FEpicodeCommandContext());
	TestFalse(TEXT("transaction fails"), Transaction.bSuccess);
	TestEqual(TEXT("transaction rolls back"), Scene->GetDocument().Entities.Num(), Baseline);

	TSharedPtr<FJsonObject> CameraParams = MakeShared<FJsonObject>();
	CameraParams->SetStringField(TEXT("name"), TEXT("Test Camera"));
	CameraParams->SetNumberField(TEXT("fieldOfView"), 50.0);
	const FEpicodeCommandResult CameraCreated = Engine->ExecuteCommand(TEXT("camera.create"), CameraParams, FEpicodeCommandContext());
	TestTrue(TEXT("camera created"), CameraCreated.bSuccess);
	const FString CameraId = CameraCreated.Result->GetObjectField(TEXT("entity"))->GetStringField(TEXT("id"));

	const FEpicodeCommandResult BeforeActivate = Engine->ExecuteCommand(TEXT("camera.list"), MakeShared<FJsonObject>(), FEpicodeCommandContext());
	TestNotEqual(TEXT("new camera is not active by default"), BeforeActivate.Result->GetStringField(TEXT("activeCameraId")), CameraId);

	TSharedPtr<FJsonObject> ActivateParams = MakeShared<FJsonObject>();
	ActivateParams->SetStringField(TEXT("id"), CameraId);
	TestTrue(TEXT("camera activated"), Engine->ExecuteCommand(TEXT("camera.activate"), ActivateParams, FEpicodeCommandContext()).bSuccess);
	const FEpicodeCommandResult AfterActivate = Engine->ExecuteCommand(TEXT("camera.list"), MakeShared<FJsonObject>(), FEpicodeCommandContext());
	TestEqual(TEXT("active camera switched"), AfterActivate.Result->GetStringField(TEXT("activeCameraId")), CameraId);
	TestEqual(TEXT("two cameras present"), AfterActivate.Result->GetArrayField(TEXT("cameras")).Num(), 2);

	TSharedPtr<FJsonObject> DeleteParams = MakeShared<FJsonObject>();
	DeleteParams->SetStringField(TEXT("id"), CameraId);
	TestTrue(TEXT("camera deleted"), Engine->ExecuteCommand(TEXT("camera.delete"), DeleteParams, FEpicodeCommandContext()).bSuccess);
	const FEpicodeCommandResult AfterDelete = Engine->ExecuteCommand(TEXT("camera.list"), MakeShared<FJsonObject>(), FEpicodeCommandContext());
	TestEqual(TEXT("one camera remains"), AfterDelete.Result->GetArrayField(TEXT("cameras")).Num(), 1);
	TestFalse(TEXT("active camera fell back"), AfterDelete.Result->GetStringField(TEXT("activeCameraId")).IsEmpty());

	TSharedPtr<FJsonObject> DeleteDefault = MakeShared<FJsonObject>();
	DeleteDefault->SetStringField(TEXT("id"), AfterDelete.Result->GetStringField(TEXT("activeCameraId")));
	TestTrue(TEXT("last camera deleted"), Engine->ExecuteCommand(TEXT("camera.delete"), DeleteDefault, FEpicodeCommandContext()).bSuccess);
	const FEpicodeCommandResult AfterDefaultDelete = Engine->ExecuteCommand(TEXT("camera.list"), MakeShared<FJsonObject>(), FEpicodeCommandContext());
	TestEqual(TEXT("default camera recreated"), AfterDefaultDelete.Result->GetArrayField(TEXT("cameras")).Num(), 1);
	TestFalse(TEXT("recreated camera is active"), AfterDefaultDelete.Result->GetStringField(TEXT("activeCameraId")).IsEmpty());

	const FEpicodeCommandResult FullSnapshot = Engine->ExecuteCommand(TEXT("scene.snapshot"), MakeShared<FJsonObject>(), FEpicodeCommandContext());
	TestTrue(TEXT("full snapshot"), FullSnapshot.bSuccess);
	const int32 SnapshotSequence = static_cast<int32>(FullSnapshot.Result->GetNumberField(TEXT("eventSequence")));
	TSharedPtr<FJsonObject> SinceParams = MakeShared<FJsonObject>();
	SinceParams->SetNumberField(TEXT("since"), SnapshotSequence);
	const FEpicodeCommandResult Unchanged = Engine->ExecuteCommand(TEXT("scene.snapshot"), SinceParams, FEpicodeCommandContext());
	TestTrue(TEXT("unchanged snapshot"), Unchanged.Result->GetBoolField(TEXT("unchanged")));
	TestEqual(TEXT("no events"), Unchanged.Result->GetArrayField(TEXT("events")).Num(), 0);

	TSharedPtr<FJsonObject> CreateEntityParams = MakeShared<FJsonObject>();
	CreateEntityParams->SetStringField(TEXT("type"), TEXT("group"));
	CreateEntityParams->SetStringField(TEXT("name"), TEXT("Snapshot Change"));
	TestTrue(TEXT("snapshot change created"), Engine->ExecuteCommand(TEXT("entity.create"), CreateEntityParams, FEpicodeCommandContext()).bSuccess);
	const FEpicodeCommandResult Incremental = Engine->ExecuteCommand(TEXT("scene.snapshot"), SinceParams, FEpicodeCommandContext());
	TestTrue(TEXT("incremental snapshot"), Incremental.Result->GetBoolField(TEXT("incremental")));
	TestTrue(TEXT("incremental events present"), Incremental.Result->GetArrayField(TEXT("events")).Num() > 0);

	TSharedPtr<FJsonObject> AheadParams = MakeShared<FJsonObject>();
	AheadParams->SetNumberField(TEXT("since"), SnapshotSequence + 1000);
	TestFalse(TEXT("future snapshot sequence rejected"), Engine->ExecuteCommand(TEXT("scene.snapshot"), AheadParams, FEpicodeCommandContext()).bSuccess);

	TSharedPtr<FJsonObject> CharacterParams = MakeShared<FJsonObject>();
	CharacterParams->SetStringField(TEXT("skeletalMesh"), TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP.TutorialTPP"));
	CharacterParams->SetStringField(TEXT("name"), TEXT("Attach Character"));
	const FEpicodeCommandResult CharacterCreated = Engine->ExecuteCommand(TEXT("character.spawn"), CharacterParams, FEpicodeCommandContext());
	TestTrue(TEXT("attach character spawned"), CharacterCreated.bSuccess);
	const FString CharacterId = CharacterCreated.Result->GetObjectField(TEXT("entity"))->GetStringField(TEXT("id"));
	FGuid CharacterGuid;
	FGuid::Parse(CharacterId, CharacterGuid);
	AEpicodeCharacterActor* CharacterActor = Cast<AEpicodeCharacterActor>(Scene->GetEntityActor(CharacterGuid));
	USkeletalMesh* SkeletalMesh = CharacterActor ? CharacterActor->MeshComponent->GetSkeletalMeshAsset() : nullptr;
	if (SkeletalMesh && SkeletalMesh->GetRefSkeleton().GetNum() > 0)
	{
		USkeletalMeshSocket* Socket = NewObject<USkeletalMeshSocket>(SkeletalMesh);
		Socket->SocketName = TEXT("EpicodeTestSocket");
		Socket->BoneName = SkeletalMesh->GetRefSkeleton().GetBoneName(0);
		SkeletalMesh->GetMeshOnlySocketList().Add(Socket);

		TSharedPtr<FJsonObject> PropParams = MakeShared<FJsonObject>();
		PropParams->SetStringField(TEXT("staticMesh"), TEXT("/Engine/EngineMeshes/Cube.Cube"));
		PropParams->SetStringField(TEXT("name"), TEXT("Attach Prop"));
		const FEpicodeCommandResult PropCreated = Engine->ExecuteCommand(TEXT("prop.spawn"), PropParams, FEpicodeCommandContext());
		TestTrue(TEXT("attach prop spawned"), PropCreated.bSuccess);
		const FString PropId = PropCreated.Result->GetObjectField(TEXT("entity"))->GetStringField(TEXT("id"));
		FGuid PropGuid;
		FGuid::Parse(PropId, PropGuid);

		TSharedPtr<FJsonObject> AttachParams = MakeShared<FJsonObject>();
		AttachParams->SetStringField(TEXT("id"), PropId);
		AttachParams->SetStringField(TEXT("parentId"), CharacterId);
		AttachParams->SetStringField(TEXT("socket"), TEXT("EpicodeTestSocket"));
		TestTrue(TEXT("character socket attach"), Engine->ExecuteCommand(TEXT("character.attach"), AttachParams, FEpicodeCommandContext()).bSuccess);
		AActor* PropActor = Scene->GetEntityActor(PropGuid);
		TestTrue(TEXT("prop attached to character"), PropActor && PropActor->GetAttachParentActor() == CharacterActor);
		TestEqual(TEXT("prop uses requested socket"), PropActor ? PropActor->GetAttachParentSocketName() : FName(), FName(TEXT("EpicodeTestSocket")));
		const FVector AttachedLocation = PropActor ? PropActor->GetActorLocation() : FVector::ZeroVector;
		CharacterActor->SetActorLocation(CharacterActor->GetActorLocation() + FVector(100.0, 0.0, 0.0));
		TestTrue(TEXT("attached prop follows character"), PropActor && !PropActor->GetActorLocation().Equals(AttachedLocation));

		TSharedPtr<FJsonObject> DetachParams = MakeShared<FJsonObject>();
		DetachParams->SetStringField(TEXT("id"), PropId);
		TestTrue(TEXT("character socket detach"), Engine->ExecuteCommand(TEXT("character.detach"), DetachParams, FEpicodeCommandContext()).bSuccess);
		TestTrue(TEXT("prop detached"), !PropActor || PropActor->GetAttachParentActor() == nullptr);
		SkeletalMesh->GetMeshOnlySocketList().Remove(Socket);
	}
	else
	{
		AddError(TEXT("Attach test requires a skeletal mesh with at least one bone."));
	}

	Scene->CloseScene(true, Error);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeProjectionTest, "Epicode.Core.Projection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeProjectionTest::RunTest(const FString& Parameters)
{
	UWorld* World = UEpicodeSceneSubsystem::GetPrimaryWorld();
	if (!World)
	{
		AddError(TEXT("Editor world is unavailable."));
		return false;
	}

	FEpicodeEntityRecord Group = MakeEntity(TEXT("Projection Group"));
	Group.Transform.Location = FVector(100.0, 200.0, 50.0);
	AActor* GroupActor = EpicodeActorFactory::SpawnForRecord(World, Group);
	TestNotNull(TEXT("group actor spawned"), GroupActor);
	TestTrue(TEXT("group actor is custom class"), Cast<AEpicodeGroupActor>(GroupActor) != nullptr);
	TestTrue(TEXT("group entity id linked"), EpicodeActorFactory::GetEntityId(GroupActor) == Group.Id);
	TestTrue(TEXT("group transform applied"), GroupActor->GetActorLocation().Equals(Group.Transform.Location));

	Group.Transform.Location = FVector(300.0, 400.0, 75.0);
	Group.bVisible = false;
	EpicodeActorFactory::ApplyRecord(GroupActor, Group);
	TestTrue(TEXT("group transform updated"), GroupActor->GetActorLocation().Equals(Group.Transform.Location));
	TestTrue(TEXT("group hidden"), GroupActor->IsHidden());

	FEpicodeEntityRecord Child = MakeEntity(TEXT("Projection Child"), EEpicodeEntityType::Group, Group.Id);
	AActor* ChildActor = EpicodeActorFactory::SpawnForRecord(World, Child);
	TestNotNull(TEXT("child actor spawned"), ChildActor);
	ChildActor->AttachToActor(GroupActor, FAttachmentTransformRules::KeepWorldTransform);
	TestTrue(TEXT("child attached to parent"), ChildActor->GetAttachParentActor() == GroupActor);
	ChildActor->Destroy();
	GroupActor->Destroy();

	FEpicodeEntityRecord Prop = MakeEntity(TEXT("Projection Prop"), EEpicodeEntityType::Prop);
	AActor* PropActor = EpicodeActorFactory::SpawnForRecord(World, Prop);
	TestTrue(TEXT("prop actor is custom class"), Cast<AEpicodePropActor>(PropActor) != nullptr);
	TestTrue(TEXT("prop entity id linked"), EpicodeActorFactory::GetEntityId(PropActor) == Prop.Id);
	PropActor->Destroy();

	FEpicodeEntityRecord Character = MakeEntity(TEXT("Projection Character"), EEpicodeEntityType::Character);
	AActor* CharacterActor = EpicodeActorFactory::SpawnForRecord(World, Character);
	TestTrue(TEXT("character actor is custom class"), Cast<AEpicodeCharacterActor>(CharacterActor) != nullptr);
	TestTrue(TEXT("character entity id linked"), EpicodeActorFactory::GetEntityId(CharacterActor) == Character.Id);
	CharacterActor->Destroy();

	FEpicodeEntityRecord Light = MakeEntity(TEXT("Projection Light"), EEpicodeEntityType::Light);
	Light.Data->SetStringField(TEXT("lightType"), TEXT("directional"));
	AActor* LightActor = EpicodeActorFactory::SpawnForRecord(World, Light);
	TestNotNull(TEXT("light actor spawned"), LightActor);
	TestTrue(TEXT("light entity id linked"), EpicodeActorFactory::GetEntityId(LightActor) == Light.Id);
	LightActor->Destroy();

	FEpicodeEntityRecord Camera = MakeEntity(TEXT("Projection Camera"), EEpicodeEntityType::Camera);
	AActor* CameraActor = EpicodeActorFactory::SpawnForRecord(World, Camera);
	TestNotNull(TEXT("camera actor spawned"), CameraActor);
	TestTrue(TEXT("camera entity id linked"), EpicodeActorFactory::GetEntityId(CameraActor) == Camera.Id);
	CameraActor->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeSceneLifecycleTest, "Epicode.Core.SceneLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeSceneLifecycleTest::RunTest(const FString& Parameters)
{
	FEpicodeScopedTestSceneRoot SceneRootGuard(TEXT("SceneLifecycle"));
	UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
	if (!Scene)
	{
		AddError(TEXT("Scene subsystem is unavailable."));
		return false;
	}
	FString Error;
	if (Scene->HasScene())
	{
		Scene->CloseScene(true, Error);
	}
	TestTrue(TEXT("new scene"), Scene->NewScene(TEXT("Lifecycle Test"), Error));
	const int32 EntityCount = Scene->GetDocument().Entities.Num();
	FString SavePath;
	TestTrue(TEXT("scene saved"), Scene->SaveScene(SavePath, Error));
	const FString SceneId = Scene->GetCurrentSceneId();
	TestTrue(TEXT("clean scene closes"), Scene->CloseScene(false, Error));
	TestTrue(TEXT("scene reopened"), Scene->OpenScene(SceneId, Error));
	TestEqual(TEXT("entities restored"), Scene->GetDocument().Entities.Num(), EntityCount);
	FString CopyPath;
	TestTrue(TEXT("scene saved as"), Scene->SaveSceneAs(TEXT("Lifecycle Copy"), CopyPath, Error));
	TestTrue(TEXT("save-as file exists"), IFileManager::Get().FileExists(*CopyPath));

	const FString FailureRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Epicode/Tests/SceneSaveFailure"));
	FString FailurePath;
	TestTrue(TEXT("baseline save"), FEpicodeSceneLibrary::Save(Scene->GetDocument(), FailureRoot, FailurePath, Error));
	FString BaselineJson;
	TestTrue(TEXT("baseline readable"), FFileHelper::LoadFileToString(BaselineJson, *FailurePath));
	FString RejectedPath;
	TestFalse(TEXT("unwritable root rejected"), FEpicodeSceneLibrary::Save(Scene->GetDocument(), TEXT("/proc/epicode-unwritable"), RejectedPath, Error));
	FString PreservedJson;
	TestTrue(TEXT("old file preserved"), FFileHelper::LoadFileToString(PreservedJson, *FailurePath));
	TestEqual(TEXT("old file unchanged"), PreservedJson, BaselineJson);
	IFileManager::Get().DeleteDirectory(*FailureRoot, false, true);
	TestTrue(TEXT("save-as scene closes"), Scene->CloseScene(false, Error));

	TestTrue(TEXT("dirty scene created"), Scene->NewScene(TEXT("Lifecycle Dirty"), Error));
	FGuid Unused;
	TestTrue(TEXT("dirty entity created"), Scene->CreateEntity(EEpicodeEntityType::Group, TEXT("Dirty"), FGuid(), FEpicodeTransform(), MakeShared<FJsonObject>(), Unused, Error));
	TestFalse(TEXT("unsaved close blocked"), Scene->CloseScene(false, Error));
	TestTrue(TEXT("discard close"), Scene->CloseScene(true, Error));
	IFileManager::Get().DeleteDirectory(*FPaths::GetPath(SavePath), false, true);
	IFileManager::Get().DeleteDirectory(*FPaths::GetPath(CopyPath), false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeCharacterTest, "Epicode.Core.Character", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeCharacterTest::RunTest(const FString& Parameters)
{
	UWorld* World = UEpicodeSceneSubsystem::GetPrimaryWorld();
	FEpicodeEntityRecord Record = MakeEntity(TEXT("Test Character"), EEpicodeEntityType::Character);
	AEpicodeCharacterActor* Character = World ? Cast<AEpicodeCharacterActor>(EpicodeActorFactory::SpawnForRecord(World, Record)) : nullptr;
	if (!Character)
	{
		AddError(TEXT("Character actor could not be spawned."));
		return false;
	}
	FString Error;
	TestTrue(TEXT("skeletal mesh loaded"), Character->SetSkeletalMeshPath(TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP.TutorialTPP"), Error));
	TestNotNull(TEXT("skeletal mesh assigned"), Character->MeshComponent->GetSkeletalMeshAsset());
	TestTrue(TEXT("animation loaded"), Character->PlayAnimationPath(TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/Tutorial_Walk_Fwd.Tutorial_Walk_Fwd"), true, 1.0f, Error));
	Character->PauseAnimation(true);
	TestTrue(TEXT("animation paused"), Character->MeshComponent->bPauseAnims);
	Character->PauseAnimation(false);
	TestFalse(TEXT("animation resumed"), Character->MeshComponent->bPauseAnims);
	Character->SetAnimationRate(2.0f);
	TestEqual(TEXT("animation rate applied"), Character->MeshComponent->GetPlayRate(), 2.0f);
	TestTrue(TEXT("animation seek"), Character->SetAnimationTime(0.25f, Error));
	TestTrue(TEXT("character material override"), Character->SetMaterialOverride(0, TEXT("/Engine/EngineMeshes/BackgroundMaterial.BackgroundMaterial"), Error));
	UMaterialInterface* Override = Character->MeshComponent->GetMaterial(0);
	TestNotNull(TEXT("override material assigned"), Override);
	Character->ClearMaterialOverride(0);
	TestTrue(TEXT("character material restored"), Character->MeshComponent->GetMaterial(0) != Override);
	Character->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodePropTest, "Epicode.Core.Prop", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodePropTest::RunTest(const FString& Parameters)
{
	UWorld* World = UEpicodeSceneSubsystem::GetPrimaryWorld();
	FEpicodeEntityRecord Record = MakeEntity(TEXT("Test Prop"), EEpicodeEntityType::Prop);
	AEpicodePropActor* Prop = World ? Cast<AEpicodePropActor>(EpicodeActorFactory::SpawnForRecord(World, Record)) : nullptr;
	if (!Prop)
	{
		AddError(TEXT("Prop actor could not be spawned."));
		return false;
	}
	FString Error;
	TestTrue(TEXT("static mesh loaded"), Prop->SetStaticMeshPath(TEXT("/Engine/EngineMeshes/Cube.Cube"), Error));
	TestTrue(TEXT("static mesh assigned"), Prop->MeshComponent->GetStaticMesh() != nullptr);
	Prop->SetCollisionProfile(TEXT("BlockAll"));
	TestEqual(TEXT("collision profile applied"), Prop->MeshComponent->GetCollisionProfileName(), FName(TEXT("BlockAll")));
	Prop->SetSimulatePhysics(true);
	TestTrue(TEXT("physics enabled"), Prop->MeshComponent->IsSimulatingPhysics());
	Prop->SetSimulatePhysics(false);
	TestFalse(TEXT("physics disabled"), Prop->MeshComponent->IsSimulatingPhysics());
	TestTrue(TEXT("prop material override"), Prop->SetMaterialOverride(0, TEXT("/Engine/EngineMeshes/BackgroundMaterial.BackgroundMaterial"), Error));
	UMaterialInterface* Override = Prop->MeshComponent->GetMaterial(0);
	TestNotNull(TEXT("prop override assigned"), Override);
	Prop->ClearMaterialOverride(0);
	TestTrue(TEXT("prop material restored"), Prop->MeshComponent->GetMaterial(0) != Override);
	Prop->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeEnvironmentTest, "Epicode.Core.Environment", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeEnvironmentTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Environment = MakeEpicodeDefaultEnvironment();
	TestTrue(TEXT("has time"), Environment->HasField(TEXT("timeOfDay")));
	TestTrue(TEXT("has preset"), Environment->HasField(TEXT("preset")));
	TestTrue(TEXT("has fog"), Environment->HasField(TEXT("fogDensity")));
	TestEqual(TEXT("default preset"), Environment->GetStringField(TEXT("preset")), FString(TEXT("clear")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeEntityEditTest, "Epicode.Core.EntityEdit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeEntityEditTest::RunTest(const FString& Parameters)
{
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
	if (!Engine || !Scene)
	{
		AddError(TEXT("Epicode subsystems are unavailable."));
		return false;
	}
	FString Error;
	if (Scene->HasScene())
	{
		Scene->CloseScene(true, Error);
	}
	if (!Scene->NewScene(TEXT("Entity Edit Test"), Error))
	{
		AddError(Error);
		return false;
	}
	const int32 Baseline = Scene->GetDocument().Entities.Num();

	auto CreateGroup = [&](const FString& Name)
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("type"), TEXT("group"));
		Params->SetStringField(TEXT("name"), Name);
		const FEpicodeCommandResult Result = Engine->ExecuteCommand(TEXT("entity.create"), Params, FEpicodeCommandContext());
		TestTrue(FString::Printf(TEXT("%s created"), *Name), Result.bSuccess);
		return Result.bSuccess ? Result.Result->GetObjectField(TEXT("entity"))->GetStringField(TEXT("id")) : FString();
	};

	const FString FirstId = CreateGroup(TEXT("First"));
	const FString SecondId = CreateGroup(TEXT("Second"));
	if (FirstId.IsEmpty() || SecondId.IsEmpty())
	{
		Scene->CloseScene(true, Error);
		return false;
	}

	TSharedPtr<FJsonObject> RenameParams = MakeShared<FJsonObject>();
	RenameParams->SetStringField(TEXT("id"), FirstId);
	RenameParams->SetStringField(TEXT("name"), TEXT("Renamed"));
	TestTrue(TEXT("entity renamed"), Engine->ExecuteCommand(TEXT("entity.rename"), RenameParams, FEpicodeCommandContext()).bSuccess);

	TSharedPtr<FJsonObject> GetParams = MakeShared<FJsonObject>();
	GetParams->SetStringField(TEXT("id"), FirstId);
	FEpicodeCommandResult GetResult = Engine->ExecuteCommand(TEXT("entity.get"), GetParams, FEpicodeCommandContext());
	TestTrue(TEXT("renamed entity readable"), GetResult.bSuccess);
	if (!GetResult.bSuccess || !GetResult.Result.IsValid())
	{
		AddError(TEXT("Entity could not be read after rename."));
		Scene->CloseScene(true, Error);
		return false;
	}
	TestEqual(TEXT("renamed entity name"), GetResult.Result->GetObjectField(TEXT("entity"))->GetStringField(TEXT("name")), FString(TEXT("Renamed")));

	TSharedPtr<FJsonObject> ReparentParams = MakeShared<FJsonObject>();
	ReparentParams->SetStringField(TEXT("id"), SecondId);
	ReparentParams->SetStringField(TEXT("parentId"), FirstId);
	ReparentParams->SetBoolField(TEXT("keepWorldTransform"), true);
	TestTrue(TEXT("entity reparented"), Engine->ExecuteCommand(TEXT("entity.reparent"), ReparentParams, FEpicodeCommandContext()).bSuccess);
	GetParams->SetStringField(TEXT("id"), SecondId);
	GetResult = Engine->ExecuteCommand(TEXT("entity.get"), GetParams, FEpicodeCommandContext());
	TestEqual(TEXT("entity parent id"), GetResult.Result->GetObjectField(TEXT("entity"))->GetStringField(TEXT("parentId")), FirstId);

	TSharedPtr<FJsonObject> Transform = MakeShared<FJsonObject>();
	Transform->SetArrayField(TEXT("location"), {
		MakeShared<FJsonValueNumber>(10.0),
		MakeShared<FJsonValueNumber>(20.0),
		MakeShared<FJsonValueNumber>(30.0)
	});
	Transform->SetArrayField(TEXT("rotation"), {
		MakeShared<FJsonValueNumber>(0.0),
		MakeShared<FJsonValueNumber>(90.0),
		MakeShared<FJsonValueNumber>(0.0)
	});
	Transform->SetArrayField(TEXT("scale"), {
		MakeShared<FJsonValueNumber>(2.0),
		MakeShared<FJsonValueNumber>(2.0),
		MakeShared<FJsonValueNumber>(2.0)
	});
	TSharedPtr<FJsonObject> TransformParams = MakeShared<FJsonObject>();
	TransformParams->SetStringField(TEXT("id"), SecondId);
	TransformParams->SetObjectField(TEXT("transform"), Transform);
	TestTrue(TEXT("entity transform set"), Engine->ExecuteCommand(TEXT("entity.setTransform"), TransformParams, FEpicodeCommandContext()).bSuccess);
	GetResult = Engine->ExecuteCommand(TEXT("entity.get"), GetParams, FEpicodeCommandContext());
	if (!GetResult.bSuccess || !GetResult.Result.IsValid())
	{
		AddError(TEXT("Entity could not be read after transform."));
		Scene->CloseScene(true, Error);
		return false;
	}
	const TSharedPtr<FJsonObject> Applied = GetResult.Result->GetObjectField(TEXT("entity"))->GetObjectField(TEXT("transform"));
	TestEqual(TEXT("location x"), Applied->GetArrayField(TEXT("location"))[0]->AsNumber(), 10.0);
	TestEqual(TEXT("rotation yaw"), Applied->GetArrayField(TEXT("rotation"))[1]->AsNumber(), 90.0);
	TestEqual(TEXT("scale z"), Applied->GetArrayField(TEXT("scale"))[2]->AsNumber(), 2.0);

	TSharedPtr<FJsonObject> VisibleParams = MakeShared<FJsonObject>();
	VisibleParams->SetStringField(TEXT("id"), SecondId);
	VisibleParams->SetBoolField(TEXT("visible"), false);
	TestTrue(TEXT("entity hidden"), Engine->ExecuteCommand(TEXT("entity.setVisible"), VisibleParams, FEpicodeCommandContext()).bSuccess);
	GetResult = Engine->ExecuteCommand(TEXT("entity.get"), GetParams, FEpicodeCommandContext());
	TestFalse(TEXT("entity visible flag"), GetResult.Result->GetObjectField(TEXT("entity"))->GetBoolField(TEXT("visible")));

	TSharedPtr<FJsonObject> DeleteParams = MakeShared<FJsonObject>();
	DeleteParams->SetStringField(TEXT("id"), SecondId);
	TestTrue(TEXT("entity deleted"), Engine->ExecuteCommand(TEXT("entity.delete"), DeleteParams, FEpicodeCommandContext()).bSuccess);
	TestEqual(TEXT("deleted entity count"), Scene->GetDocument().Entities.Num(), Baseline + 1);
	TestFalse(TEXT("deleted entity missing"), Engine->ExecuteCommand(TEXT("entity.get"), GetParams, FEpicodeCommandContext()).bSuccess);

	Scene->CloseScene(true, Error);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeRuntimeMetadataTest, "Epicode.Scene.RuntimeMetadata", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeRuntimeMetadataTest::RunTest(const FString& Parameters)
{
	FEpicodeSceneDocument Document = FEpicodeSceneDocument::CreateDefault(TEXT("Runtime Metadata"));
	FEpicodeEntityRecord Character = MakeEntity(TEXT("Agent Hero"), EEpicodeEntityType::Character);
	FEpicodeEntityRecord Camera = MakeEntity(TEXT("Viewer Camera"), EEpicodeEntityType::Camera);
	Document.Entities.Add(Character);
	Document.Entities.Add(Camera);
	Document.Runtime.Agent.ControllableCharacterIds.Add(Character.Id.ToString(EGuidFormats::DigitsWithHyphens));
	Document.Runtime.Agent.AllowedActions.Add(TEXT("wave"));
	FEpicodeAgentSpawnPoint Spawn;
	Spawn.Id = TEXT("agent-spawn");
	Spawn.Transform.Location = FVector(10.0, 20.0, 30.0);
	Document.Runtime.Agent.SpawnPoints.Add(Spawn);
	Document.Runtime.Viewer.CameraIds.Add(Camera.Id.ToString(EGuidFormats::DigitsWithHyphens));
	Document.Runtime.Viewer.DefaultCameraId = Camera.Id.ToString(EGuidFormats::DigitsWithHyphens);
	Document.Runtime.Viewer.SpawnTransform.Location = FVector(1.0, 2.0, 3.0);
	Document.Runtime.Extra = MakeShared<FJsonObject>();
	Document.Runtime.Extra->SetStringField(TEXT("futureRuntimeField"), TEXT("kept"));
	Document.Runtime.Agent.Extra = MakeShared<FJsonObject>();
	Document.Runtime.Agent.Extra->SetStringField(TEXT("futureAgentField"), TEXT("kept"));

	const TSharedPtr<FJsonObject> Json = Document.ToJson();
	TestEqual(TEXT("schema version"), Json->GetNumberField(TEXT("schemaVersion")), 2.0);
	TestTrue(TEXT("runtime object present"), Json->HasField(TEXT("runtime")));
	FEpicodeSceneDocument Loaded;
	FString Error;
	TestTrue(TEXT("runtime round trip"), FEpicodeSceneDocument::FromJson(Json, Loaded, Error));
	TestEqual(TEXT("agent character"), Loaded.Runtime.Agent.ControllableCharacterIds.Num(), 1);
	TestEqual(TEXT("viewer camera"), Loaded.Runtime.Viewer.CameraIds.Num(), 1);
	TestEqual(TEXT("spawn point"), Loaded.Runtime.Agent.SpawnPoints.Num(), 1);
	TestEqual(TEXT("spawn point id"), Loaded.Runtime.Agent.SpawnPoints[0].Id, FString(TEXT("agent-spawn")));
	TestEqual(TEXT("unknown runtime field"), Loaded.Runtime.Extra->GetStringField(TEXT("futureRuntimeField")), FString(TEXT("kept")));
	TestEqual(TEXT("unknown agent field"), Loaded.Runtime.Agent.Extra->GetStringField(TEXT("futureAgentField")), FString(TEXT("kept")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeRuntimeMigrationTest, "Epicode.Scene.RuntimeMigration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeRuntimeMigrationTest::RunTest(const FString& Parameters)
{
	FEpicodeSceneDocument VersionOne = FEpicodeSceneDocument::CreateDefault(TEXT("Version One"));
	VersionOne.Entities.Add(MakeEntity(TEXT("Legacy Character"), EEpicodeEntityType::Character));
	TSharedPtr<FJsonObject> Json = VersionOne.ToJson();
	Json->SetNumberField(TEXT("schemaVersion"), 1);
	Json->RemoveField(TEXT("runtime"));

	FEpicodeSceneDocument Migrated;
	FString Error;
	TestTrue(TEXT("v1 migrates"), FEpicodeSceneDocument::FromJson(Json, Migrated, Error));
	TestEqual(TEXT("schema upgraded"), Migrated.SchemaVersion, FEpicodeSceneDocument::CurrentSchemaVersion);
	TestEqual(TEXT("runtime defaults empty"), Migrated.Runtime.Agent.ControllableCharacterIds.Num(), 0);
	TestTrue(TEXT("runtime serialized"), Migrated.ToJson()->HasField(TEXT("runtime")));
	TestEqual(TEXT("entities preserved"), Migrated.Entities.Num(), 1);
	TestEqual(TEXT("active camera preserved"), Migrated.ActiveCameraId, VersionOne.ActiveCameraId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeRuntimeValidationTest, "Epicode.Scene.RuntimeValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeRuntimeValidationTest::RunTest(const FString& Parameters)
{
	FEpicodeSceneDocument Document = FEpicodeSceneDocument::CreateDefault(TEXT("Runtime Validation"));
	FEpicodeEntityRecord Character = MakeEntity(TEXT("Agent Hero"), EEpicodeEntityType::Character);
	FEpicodeEntityRecord Camera = MakeEntity(TEXT("Viewer Camera"), EEpicodeEntityType::Camera);
	Document.Entities.Add(Character);
	Document.Entities.Add(Camera);
	FString Error;

	Document.Runtime.Agent.ControllableCharacterIds.Add(TEXT("missing-character"));
	TestFalse(TEXT("missing agent character rejected"), Document.ValidateRuntimeReferences(Error));
	TestTrue(TEXT("missing character error"), Error.Contains(TEXT("AGENT_CHARACTER_NOT_FOUND")));
	Document.Runtime.Agent.ControllableCharacterIds.Reset();

	Document.Runtime.Agent.SpawnPoints.Add(DefaultSpawn(TEXT("spawn")));
	Document.Runtime.Agent.SpawnPoints.Add(DefaultSpawn(TEXT("spawn")));
	TestFalse(TEXT("duplicate spawn rejected"), Document.ValidateRuntimeReferences(Error));
	TestTrue(TEXT("duplicate spawn error"), Error.Contains(TEXT("DUPLICATE_AGENT_SPAWN_ID")));
	Document.Runtime.Agent.SpawnPoints.Reset();

	Document.Runtime.Agent.AllowedActions.Add(FString());
	TestFalse(TEXT("empty action rejected"), Document.ValidateRuntimeReferences(Error));
	TestTrue(TEXT("empty action error"), Error.Contains(TEXT("EMPTY_AGENT_ACTION")));
	Document.Runtime.Agent.AllowedActions.Reset();

	Document.Runtime.Viewer.CameraIds.Add(Camera.Id.ToString(EGuidFormats::DigitsWithHyphens));
	Document.Runtime.Viewer.DefaultCameraId = Character.Id.ToString(EGuidFormats::DigitsWithHyphens);
	TestFalse(TEXT("default camera not listed rejected"), Document.ValidateRuntimeReferences(Error));
	TestTrue(TEXT("default camera error"), Error.Contains(TEXT("VIEWER_DEFAULT_CAMERA_NOT_LISTED")));
	Document.Runtime.Viewer.DefaultCameraId.Empty();

	Document.LevelPath.Empty();
	TestFalse(TEXT("missing level rejected for runtime"), Document.ValidateForRuntime(Error));
	TestTrue(TEXT("missing level error"), Error.Contains(TEXT("LEVEL_PATH_MISSING")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeRuntimeCommandsTest, "Epicode.Scene.RuntimeCommands", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeRuntimeCommandsTest::RunTest(const FString& Parameters)
{
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
	if (!Engine || !Scene)
	{
		AddError(TEXT("Epicode subsystems are unavailable."));
		return false;
	}
	FString Error;
	if (Scene->HasScene())
	{
		Scene->CloseScene(true, Error);
	}
	if (!Scene->NewScene(TEXT("Runtime Command Test"), Error))
	{
		AddError(Error);
		return false;
	}

	TSharedPtr<FJsonObject> CameraParams = MakeShared<FJsonObject>();
	CameraParams->SetStringField(TEXT("name"), TEXT("Runtime Camera"));
	const FEpicodeCommandResult CameraCreated = Engine->ExecuteCommand(TEXT("camera.create"), CameraParams, FEpicodeCommandContext());
	TestTrue(TEXT("camera created"), CameraCreated.bSuccess);
	const FString CameraId = CameraCreated.Result->GetObjectField(TEXT("entity"))->GetStringField(TEXT("id"));

	TSharedPtr<FJsonObject> CharacterParams = MakeShared<FJsonObject>();
	CharacterParams->SetStringField(TEXT("type"), TEXT("character"));
	CharacterParams->SetStringField(TEXT("name"), TEXT("Runtime Character"));
	const FEpicodeCommandResult CharacterCreated = Engine->ExecuteCommand(TEXT("entity.create"), CharacterParams, FEpicodeCommandContext());
	TestTrue(TEXT("character created"), CharacterCreated.bSuccess);
	const FString CharacterId = CharacterCreated.Result->GetObjectField(TEXT("entity"))->GetStringField(TEXT("id"));

	TSharedPtr<FJsonObject> Runtime = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> Agent = MakeShared<FJsonObject>();
	Agent->SetArrayField(TEXT("controllableCharacterIds"), { MakeShared<FJsonValueString>(CharacterId) });
	Agent->SetArrayField(TEXT("allowedActions"), { MakeShared<FJsonValueString>(TEXT("wave")) });
	Runtime->SetObjectField(TEXT("agent"), Agent);
	TSharedPtr<FJsonObject> Viewer = MakeShared<FJsonObject>();
	Viewer->SetArrayField(TEXT("cameraIds"), { MakeShared<FJsonValueString>(CameraId) });
	Viewer->SetStringField(TEXT("defaultCameraId"), CameraId);
	Runtime->SetObjectField(TEXT("viewer"), Viewer);
	TSharedPtr<FJsonObject> SetParams = MakeShared<FJsonObject>();
	SetParams->SetObjectField(TEXT("runtime"), Runtime);
	SetParams->SetStringField(TEXT("levelPath"), Scene->GetDocument().LevelPath);
	SetParams->SetBoolField(TEXT("validateForRuntime"), true);
	const FEpicodeCommandResult SetResult = Engine->ExecuteCommand(TEXT("scene.runtime.set"), SetParams, FEpicodeCommandContext());
	TestTrue(TEXT("runtime set"), SetResult.bSuccess);
	const FEpicodeCommandResult GetResult = Engine->ExecuteCommand(TEXT("scene.runtime.get"), MakeShared<FJsonObject>(), FEpicodeCommandContext());
	TestTrue(TEXT("runtime get"), GetResult.bSuccess);
	TestEqual(TEXT("runtime character"), GetResult.Result->GetObjectField(TEXT("runtime"))->GetObjectField(TEXT("agent"))->GetArrayField(TEXT("controllableCharacterIds"))[0]->AsString(), CharacterId);
	TestEqual(TEXT("runtime camera"), GetResult.Result->GetObjectField(TEXT("runtime"))->GetObjectField(TEXT("viewer"))->GetStringField(TEXT("defaultCameraId")), CameraId);

	TSharedPtr<FJsonObject> InvalidViewer = MakeShared<FJsonObject>();
	InvalidViewer->SetStringField(TEXT("defaultCameraId"), CharacterId);
	TSharedPtr<FJsonObject> InvalidRuntime = MakeShared<FJsonObject>();
	InvalidRuntime->SetObjectField(TEXT("viewer"), InvalidViewer);
	TSharedPtr<FJsonObject> InvalidParams = MakeShared<FJsonObject>();
	InvalidParams->SetObjectField(TEXT("runtime"), InvalidRuntime);
	const FEpicodeCommandResult InvalidResult = Engine->ExecuteCommand(TEXT("scene.runtime.set"), InvalidParams, FEpicodeCommandContext());
	TestFalse(TEXT("invalid runtime rejected"), InvalidResult.bSuccess);
	TestEqual(TEXT("failed set preserves runtime"), Scene->GetDocument().Runtime.Viewer.DefaultCameraId, CameraId);

	Scene->CloseScene(true, Error);
	return true;
}

#endif
