#include "EpicodeSandboxSettings.h"

#include "Misc/Paths.h"

UEpicodeSandboxSettings::UEpicodeSandboxSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("Epicode Sandbox");
}

namespace
{
	FString ResolvePath(const FString& InPath)
	{
		if (InPath.IsEmpty())
		{
			return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		}
		if (FPaths::IsRelative(InPath))
		{
			return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), InPath));
		}
		return FPaths::ConvertRelativePathToFull(InPath);
	}
}

FString UEpicodeSandboxSettings::GetProductionRootPath() const
{
	return ResolvePath(ProductionRoot);
}

FString UEpicodeSandboxSettings::GetProductionTrashRootPath() const
{
	return ResolvePath(ProductionTrashRoot);
}

FString UEpicodeSandboxSettings::GetSessionRootPath() const
{
	return ResolvePath(SessionRoot);
}

bool UEpicodeSandboxSettings::Validate(FString& OutError) const
{
	OutError.Reset();
	if (FixedTickRate <= 0)
	{
		OutError = TEXT("FixedTickRate must be greater than zero.");
		return false;
	}
	if (RandomSeed < 0)
	{
		OutError = TEXT("RandomSeed must not be negative.");
		return false;
	}
	if (CheckpointIntervalEvents <= 0)
	{
		OutError = TEXT("CheckpointIntervalEvents must be greater than zero.");
		return false;
	}
	const int64 RetentionEvents = static_cast<int64>(ReplayRetentionHours) * 3600 * FixedTickRate;
	if (CheckpointIntervalEvents > RetentionEvents)
	{
		OutError = TEXT("CheckpointIntervalEvents must not exceed the replay retention window.");
		return false;
	}
	if (MaxEntities <= 0 || MaxEventsPerSecond <= 0 || MaxDirectorCues <= 0)
	{
		OutError = TEXT("Sandbox budgets must be greater than zero.");
		return false;
	}
	if (MaxConcurrentReplays <= 0)
	{
		OutError = TEXT("MaxConcurrentReplays must be greater than zero.");
		return false;
	}
	if (ProductionRoot.TrimStartAndEnd().IsEmpty() || ProductionTrashRoot.TrimStartAndEnd().IsEmpty() || SessionRoot.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("Sandbox paths must not be empty.");
		return false;
	}
	if (RuleSchemaVersion.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("RuleSchemaVersion must not be empty.");
		return false;
	}
	return true;
}
