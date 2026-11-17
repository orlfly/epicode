#include "EpicodeScopedTestSceneRoot.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EpicodeSettings.h"
#include "HAL/FileManager.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

FEpicodeScopedTestSceneRoot::FEpicodeScopedTestSceneRoot(const FString& InLabel)
{
	Root = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("Epicode/Tests/Scenes"),
		FString::Printf(
			TEXT("%s-%s"),
			*InLabel,
			*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	IFileManager::Get().MakeDirectory(*Root, true);

	if (UEpicodeSettings* Settings = GetMutableDefault<UEpicodeSettings>())
	{
		PreviousSceneRoot = Settings->SceneRoot;
		PreviousSceneTrashRoot = Settings->SceneTrashRoot;
		PreviousRenderOutputRoot = Settings->RenderOutputRoot;
		bSettingsCaptured = true;

		Settings->SceneRoot = FPaths::Combine(Root, TEXT("Scenes"));
		Settings->SceneTrashRoot = FPaths::Combine(Root, TEXT("Trash"));
		Settings->RenderOutputRoot = FPaths::Combine(Root, TEXT("Renders"));
	}
}

FEpicodeScopedTestSceneRoot::~FEpicodeScopedTestSceneRoot()
{
	if (bSettingsCaptured)
	{
		if (UEpicodeSettings* Settings = GetMutableDefault<UEpicodeSettings>())
		{
			Settings->SceneRoot = PreviousSceneRoot;
			Settings->SceneTrashRoot = PreviousSceneTrashRoot;
			Settings->RenderOutputRoot = PreviousRenderOutputRoot;
		}
	}
	IFileManager::Get().DeleteDirectory(*Root, false, true);
}

#endif
