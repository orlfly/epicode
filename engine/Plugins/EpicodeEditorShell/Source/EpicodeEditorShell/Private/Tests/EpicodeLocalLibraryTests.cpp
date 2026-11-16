#include "Misc/AutomationTest.h"

#include "EpicodeLocalLibrary.h"
#include "EpicodeSceneSubsystem.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEpicodeLibraryIndexTest,
	"Epicode.Library.Index",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeLibraryIndexTest::RunTest(const FString& Parameters)
{
	const FString Root = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("Epicode/Tests/LibraryIndex"),
		FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString IndexPath = FPaths::Combine(Root, TEXT("index.json"));
	IFileManager::Get().MakeDirectory(*Root, true);

	FString Error;
	{
		FEpicodeLocalLibrary Library(IndexPath);
		TestTrue(TEXT("initial rebuild"), Library.Rebuild(Error));
		TestTrue(TEXT("index file written"), IFileManager::Get().FileExists(*IndexPath));
		TestEqual(TEXT("schema version"), Library.GetSchemaVersion(), FEpicodeLocalLibrary::CurrentSchemaVersion);
		const int32 SceneEntryCount = Library.GetEntries().FilterByPredicate(
			[](const FEpicodeLibraryEntry& Entry)
			{
				return Entry.Type == EEpicodeLibraryEntryType::Scene;
			}).Num();
		UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
		TestEqual(
			TEXT("all scenes indexed"),
			SceneEntryCount,
			Scene ? Scene->ListScenes().Num() : 0);

		FString Text;
		TestTrue(TEXT("index is readable"), FFileHelper::LoadFileToString(Text, *IndexPath));
		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		TestTrue(
			TEXT("index is valid JSON"),
			FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid());
		if (RootObject.IsValid())
		{
			double Version = 0.0;
			TestTrue(TEXT("json version"), RootObject->TryGetNumberField(TEXT("version"), Version));
			TestEqual(TEXT("json schema"), static_cast<int32>(Version), FEpicodeLocalLibrary::CurrentSchemaVersion);
			const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
			TestTrue(TEXT("json entries"), RootObject->TryGetArrayField(TEXT("entries"), Entries) && Entries);
		}

		FEpicodeLibraryEntry Custom;
		Custom.Id = TEXT("asset:/Game/Epicode/Tests/CustomProp.CustomProp");
		Custom.Type = EEpicodeLibraryEntryType::Prop;
		Custom.Name = TEXT("CustomProp");
		Custom.Path = TEXT("/Game/Epicode/Tests/CustomProp.CustomProp");
		Custom.Tags.Add(TEXT("test"));
		TestTrue(TEXT("custom entry upserted"), Library.UpsertEntry(Custom, Error));
		TestNotNull(TEXT("custom entry found"), Library.FindEntry(Custom.Id));
	}

	{
		FEpicodeLocalLibrary Library(IndexPath);
		TestTrue(TEXT("index reloaded"), Library.LoadFromDisk(Error));
		const FEpicodeLibraryEntry* Custom = Library.FindEntry(
			TEXT("asset:/Game/Epicode/Tests/CustomProp.CustomProp"));
		TestNotNull(TEXT("custom entry persisted"), Custom);
		if (Custom)
		{
			TestEqual(TEXT("custom tags persisted"), Custom->Tags.Num(), 1);
		}

		TestTrue(TEXT("refresh after reload"), Library.Refresh(Error));
		const FEpicodeLibraryEntry* Missing = Library.FindEntry(
			TEXT("asset:/Game/Epicode/Tests/CustomProp.CustomProp"));
		TestNotNull(TEXT("missing asset retained"), Missing);
		if (Missing)
		{
			TestFalse(TEXT("missing asset marked unavailable"), Missing->bAvailable);
			TestTrue(TEXT("missing asset error"), Missing->Error.Contains(TEXT("找不到")));
		}
	}

	TestTrue(
		TEXT("corrupt index written"),
		FFileHelper::SaveStringToFile(TEXT("{ this is not json"), *IndexPath));
	{
		FEpicodeLocalLibrary Library(IndexPath);
		TestTrue(TEXT("corrupt index rebuilt"), Library.Refresh(Error));
		TestTrue(TEXT("rebuilt index exists"), IFileManager::Get().FileExists(*IndexPath));
		TestFalse(TEXT("corruption warning retained"), Library.GetLastWarning().IsEmpty());
	}

	TArray<FString> Backups;
	IFileManager::Get().FindFiles(
		Backups, *(IndexPath + TEXT(".corrupt-*")), true, false);
	TestTrue(TEXT("corrupt index backed up"), Backups.Num() >= 1);

	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

#endif
