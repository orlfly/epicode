#include "Misc/AutomationTest.h"

#include "EpicodeImportClassifier.h"
#include "EpicodeImportService.h"
#include "EpicodeImportTypes.h"
#include "EpicodeSceneSubsystem.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

#if PLATFORM_UNIX
#include <unistd.h>
#endif

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeImportClassifyTest, "Epicode.Import.Classify", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeImportClassifyTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("fbx character"), FEpicodeImportClassifier::Classify(TEXT("/tmp/hero_character.fbx")) == EEpicodeImportKind::Character);
	TestTrue(TEXT("gltf prop"), FEpicodeImportClassifier::Classify(TEXT("/tmp/chair.gltf")) == EEpicodeImportKind::Prop);
	TestTrue(TEXT("obj prop"), FEpicodeImportClassifier::Classify(TEXT("/tmp/quad.obj")) == EEpicodeImportKind::Prop);
	TestTrue(TEXT("usd scene"), FEpicodeImportClassifier::Classify(TEXT("/tmp/set.usdz")) == EEpicodeImportKind::Scene);
	TestTrue(TEXT("unknown extension"), FEpicodeImportClassifier::Classify(TEXT("/tmp/notes.txt")) == EEpicodeImportKind::Unknown);
	TestTrue(TEXT("fbx supported"), FEpicodeImportClassifier::IsSupportedExtension(TEXT("/tmp/a.fbx")));
	TestFalse(TEXT("txt unsupported"), FEpicodeImportClassifier::IsSupportedExtension(TEXT("/tmp/a.txt")));
	TestEqual(TEXT("kind string"), FEpicodeImportClassifier::KindToString(EEpicodeImportKind::Character), FString(TEXT("character")));
	TestTrue(TEXT("kind parse"), FEpicodeImportClassifier::KindFromString(TEXT("prop")) == EEpicodeImportKind::Prop);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeImportRequestTest, "Epicode.Import.Options", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeImportRequestTest::RunTest(const FString& Parameters)
{
	FEpicodeImportRequest Request;
	Request.FilePath = TEXT("/tmp/hero.fbx");
	Request.Kind = EEpicodeImportKind::Character;
	Request.EntityName = TEXT("Hero");
	Request.DestinationPath = TEXT("/Game/Epicode/Imported");
	Request.Scale = 2.0f;

	const TSharedPtr<FJsonObject> Json = Request.ToJson();
	FEpicodeImportRequest Loaded;
	FString Error;
	TestTrue(TEXT("request round trip"), FEpicodeImportRequest::FromJson(Json, Loaded, Error));
	TestEqual(TEXT("path"), Loaded.FilePath, Request.FilePath);
	TestTrue(TEXT("kind"), Loaded.Kind == EEpicodeImportKind::Character);
	TestEqual(TEXT("scale"), Loaded.Scale, 2.0f);
	TestEqual(TEXT("destination"), Loaded.DestinationPath, Request.DestinationPath);

	TSharedPtr<FJsonObject> Options = Request.ToJson();
	Options->SetBoolField(TEXT("autoPlace"), false);
	Options->SetBoolField(TEXT("importMaterials"), false);
	Options->SetBoolField(TEXT("replaceExisting"), true);
	TArray<TSharedPtr<FJsonValue>> Rotation;
	Rotation.Add(MakeShared<FJsonValueNumber>(10.0));
	Rotation.Add(MakeShared<FJsonValueNumber>(20.0));
	Rotation.Add(MakeShared<FJsonValueNumber>(30.0));
	Options->SetArrayField(TEXT("rotationOffset"), Rotation);
	TestTrue(TEXT("options round trip"), FEpicodeImportRequest::FromJson(Options, Loaded, Error));
	TestFalse(TEXT("auto place disabled"), Loaded.bAutoPlace);
	TestFalse(TEXT("materials disabled"), Loaded.bImportMaterials);
	TestTrue(TEXT("replace enabled"), Loaded.bReplaceExisting);
	TestTrue(TEXT("rotation offset"), Loaded.RotationOffset.Equals(FRotator(10.0, 20.0, 30.0)));

	TSharedPtr<FJsonObject> BadScale = Request.ToJson();
	BadScale->SetNumberField(TEXT("scale"), 0.0);
	TestFalse(TEXT("zero scale rejected"), FEpicodeImportRequest::FromJson(BadScale, Loaded, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeImportCharacterTest, "Epicode.Import.Character", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeImportCharacterTest::RunTest(const FString& Parameters)
{
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
	if (!Scene->NewScene(TEXT("Import Character Test"), Error))
	{
		AddError(Error);
		return false;
	}

	FEpicodeImportRequest CharacterRequest;
	CharacterRequest.FilePath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Tests/Fixtures/skeletal.fbx"));
	CharacterRequest.Kind = EEpicodeImportKind::Character;
	CharacterRequest.EntityName = TEXT("Imported Character");
	CharacterRequest.DestinationPath = TEXT("/Game/Epicode/Imported");
	CharacterRequest.bReplaceExisting = true;
	FEpicodeImportResult CharacterResult;
	TestTrue(TEXT("skeletal character imported"), FEpicodeImportService::Get().ImportSynchronously(CharacterRequest, CharacterResult));
	TestTrue(TEXT("character entity created"), CharacterResult.EntityIds.Num() > 0);
	if (CharacterResult.EntityIds.Num() > 0)
	{
		FGuid EntityId;
		FGuid::Parse(CharacterResult.EntityIds[0], EntityId);
		const FEpicodeEntityRecord* Record = Scene->FindEntity(EntityId);
		TestTrue(TEXT("character record exists"), Record != nullptr);
		TestTrue(TEXT("imported record is character"), Record && Record->Type == EEpicodeEntityType::Character);
	}
	bool bSkeletalMeshFound = false;
	for (const FString& Asset : CharacterResult.Assets)
	{
		if (Cast<USkeletalMesh>(LoadObject<UObject>(nullptr, *Asset)))
		{
			bSkeletalMeshFound = true;
			break;
		}
	}
	TestTrue(TEXT("skeletal mesh asset created"), bSkeletalMeshFound);

	FEpicodeImportRequest StaticRequest;
	StaticRequest.FilePath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Tests/Fixtures/cube.fbx"));
	StaticRequest.Kind = EEpicodeImportKind::Character;
	StaticRequest.EntityName = TEXT("Invalid Character");
	StaticRequest.DestinationPath = TEXT("/Game/Epicode/ImportedInvalidCharacter");
	StaticRequest.bReplaceExisting = true;
	FEpicodeImportResult StaticResult;
	TestFalse(TEXT("static mesh rejected as character"), FEpicodeImportService::Get().ImportSynchronously(StaticRequest, StaticResult));
	TestTrue(TEXT("character import failure has message"), !StaticResult.ErrorMessage.IsEmpty());

	Scene->CloseScene(true, Error);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeImportCancelTest, "Epicode.Import.Cancel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeImportCancelTest::RunTest(const FString& Parameters)
{
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
	if (!Scene->NewScene(TEXT("Import Cancel Test"), Error))
	{
		AddError(Error);
		return false;
	}
	const int32 Baseline = Scene->GetDocument().Entities.Num();

	FEpicodeImportRequest Request;
	Request.FilePath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Tests/Fixtures/quad.obj"));
	Request.Kind = EEpicodeImportKind::Prop;
	Request.EntityName = TEXT("Cancelled Prop");
	Request.DestinationPath = TEXT("/Game/Epicode/ImportedCancel");
	Request.bReplaceExisting = true;
	TSharedPtr<FEpicodeImportJob> Job = FEpicodeImportService::Get().Submit(Request, Error);
	TestTrue(TEXT("import job queued"), Job.IsValid());
	if (Job.IsValid())
	{
		TestTrue(TEXT("queued import state"), Job->State == EEpicodeImportState::Queued);
		TestTrue(TEXT("queued import cancelled"), FEpicodeImportService::Get().Cancel(Job->Id));
		TestTrue(TEXT("cancelled import state"), Job->State == EEpicodeImportState::Cancelled);
		TestEqual(TEXT("cancel leaves no partial entities"), Scene->GetDocument().Entities.Num(), Baseline);
		TestFalse(TEXT("cancel again rejected"), FEpicodeImportService::Get().Cancel(Job->Id));
	}
	TestFalse(TEXT("unknown import cancel rejected"), FEpicodeImportService::Get().Cancel(TEXT("missing-job")));

	Scene->CloseScene(true, Error);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeImportPathSafetyTest, "Epicode.Import.PathSafety", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeImportPathSafetyTest::RunTest(const FString& Parameters)
{
	const FString FixturePath = FPaths::Combine(
		FPaths::ProjectDir(), TEXT("Tests/Fixtures/cube.fbx"));
	const FString Root = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("Epicode/Tests/PathSafety"),
		FGuid::NewGuid().ToString(EGuidFormats::Digits));
	IFileManager::Get().MakeDirectory(*Root, true);
	const FString OutsidePath = FPaths::Combine(
		TEXT("/tmp"),
		FString::Printf(TEXT("epicode-outside-%s.fbx"),
			*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	IFileManager::Get().Copy(*OutsidePath, *FixturePath);

	auto MakeRequest = [](const FString& FilePath)
	{
		FEpicodeImportRequest Request;
		Request.FilePath = FilePath;
		Request.Kind = EEpicodeImportKind::Prop;
		Request.DestinationPath = TEXT("/Game/Epicode/Tests/PathSafety");
		Request.bAutoPlace = false;
		return Request;
	};

	{
		FEpicodeImportResult Result;
		TestFalse(TEXT("outside import root rejected"),
			FEpicodeImportService::Get().ImportSynchronously(
				MakeRequest(OutsidePath), Result));
		TestTrue(TEXT("outside root message"),
			Result.ErrorMessage.Contains(TEXT("outside")));
	}
	{
		FEpicodeImportRequest Request = MakeRequest(FixturePath);
		Request.DestinationPath = TEXT("/Game/../Secret");
		FEpicodeImportResult Result;
		TestFalse(TEXT("destination traversal rejected"),
			FEpicodeImportService::Get().ImportSynchronously(Request, Result));
		TestTrue(TEXT("destination traversal message"),
			Result.ErrorMessage.Contains(TEXT("DestinationPath")));
	}
	{
		FEpicodeImportRequest Request = MakeRequest(FixturePath);
		Request.DestinationPath = TEXT("/Game/Epicode\\Secret");
		FEpicodeImportResult Result;
		TestFalse(TEXT("backslash destination rejected"),
			FEpicodeImportService::Get().ImportSynchronously(Request, Result));
	}
	{
		FEpicodeImportRequest Request = MakeRequest(FixturePath);
		Request.DestinationPath = TEXT("/Game/Epicode/Tests/");
		FEpicodeImportResult Result;
		TestFalse(TEXT("trailing slash destination rejected"),
			FEpicodeImportService::Get().ImportSynchronously(Request, Result));
	}
	{
		const FString EmptyPath = FPaths::Combine(Root, TEXT("empty.fbx"));
		FFileHelper::SaveStringToFile(FString(), *EmptyPath);
		FEpicodeImportResult Result;
		TestFalse(TEXT("empty file rejected"),
			FEpicodeImportService::Get().ImportSynchronously(
				MakeRequest(EmptyPath), Result));
		TestTrue(TEXT("empty file message"),
			Result.ErrorMessage.Contains(TEXT("empty")));
	}
	{
		const FString TextPath = FPaths::Combine(Root, TEXT("notes.txt"));
		FFileHelper::SaveStringToFile(TEXT("hello"), *TextPath);
		FEpicodeImportResult Result;
		TestFalse(TEXT("unsupported extension rejected"),
			FEpicodeImportService::Get().ImportSynchronously(
				MakeRequest(TextPath), Result));
		TestTrue(TEXT("extension message"),
			Result.ErrorMessage.Contains(TEXT("Unsupported")));
	}
#if PLATFORM_UNIX
	{
		const FString LinkPath = FPaths::Combine(Root, TEXT("escape_link.fbx"));
		TestEqual(TEXT("symlink created"),
			symlink(TCHAR_TO_UTF8(*OutsidePath), TCHAR_TO_UTF8(*LinkPath)), 0);
		FEpicodeImportResult Result;
		TestFalse(TEXT("symlink escape rejected"),
			FEpicodeImportService::Get().ImportSynchronously(
				MakeRequest(LinkPath), Result));
		TestTrue(TEXT("symlink message"),
			Result.ErrorMessage.Contains(TEXT("symbolic link")));
		IFileManager::Get().Delete(*LinkPath, false, true, true);
	}
#endif

	IFileManager::Get().Delete(*OutsidePath, false, true, true);
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeImportReuseTest, "Epicode.Import.Reuse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeImportReuseTest::RunTest(const FString& Parameters)
{
	FEpicodeImportRequest Request;
	Request.FilePath = FPaths::Combine(
		FPaths::ProjectDir(), TEXT("Tests/Fixtures/cube.fbx"));
	Request.Kind = EEpicodeImportKind::Prop;
	Request.DestinationPath = FString::Printf(
		TEXT("/Game/Epicode/Tests/ImportReuse/%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	Request.bAutoPlace = false;

	FEpicodeImportResult First;
	TestTrue(TEXT("first import succeeds"),
		FEpicodeImportService::Get().ImportSynchronously(Request, First));
	TestFalse(TEXT("first import has assets"), First.Assets.IsEmpty());

	FEpicodeImportResult Second;
	TestTrue(TEXT("second import succeeds"),
		FEpicodeImportService::Get().ImportSynchronously(Request, Second));
	TestTrue(TEXT("second import reuses hash and options"),
		Second.Warnings.ContainsByPredicate([](const FString& Warning)
		{
			return Warning.Contains(TEXT("Reused"));
		}));
	TestEqual(TEXT("reuse keeps asset count"), Second.Assets.Num(), First.Assets.Num());

	if (!Second.Assets.IsEmpty())
	{
		FString PackageFile;
		if (FPackageName::TryConvertLongPackageNameToFilename(
			FPackageName::ObjectPathToPackageName(Second.Assets[0]),
			PackageFile, FPackageName::GetAssetPackageExtension()))
		{
			IFileManager::Get().DeleteDirectory(
				*FPaths::GetPath(PackageFile), false, true);
		}
	}
	return true;
}

#endif
