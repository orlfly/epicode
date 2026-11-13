#include "EpicodeProcessRole.h"

namespace
{
	bool HasSwitch(const FString& CommandLine, const TCHAR* Switch)
	{
		return CommandLine.Contains(Switch, ESearchCase::IgnoreCase);
	}
}

FString FEpicodeProcessRole::Resolve(const FString& InCommandLine, bool bIsEditor)
{
	if (HasSwitch(InCommandLine, TEXT("-EpicodeViewer")) || HasSwitch(InCommandLine, TEXT("EpicodeViewer=1")))
	{
		return TEXT("Viewer");
	}
	if (HasSwitch(InCommandLine, TEXT("-EpicodeDirector")))
	{
		return TEXT("Director");
	}
	if (HasSwitch(InCommandLine, TEXT("-EpicodeReplay")))
	{
		return TEXT("Replay");
	}
	if (HasSwitch(InCommandLine, TEXT("-EpicodeAgent")))
	{
		return TEXT("Agent");
	}
	if (HasSwitch(InCommandLine, TEXT("-server")) || HasSwitch(InCommandLine, TEXT("-EpicodeServer")))
	{
		return TEXT("Server");
	}
	if (!bIsEditor && (HasSwitch(InCommandLine, TEXT("-nullrhi")) || HasSwitch(InCommandLine, TEXT("-headless")) || HasSwitch(InCommandLine, TEXT("-unattended"))))
	{
		return TEXT("Headless");
	}
	return bIsEditor ? TEXT("Editor") : TEXT("Headless");
}

bool FEpicodeProcessRole::IsKnown(const FString& InRole)
{
	return InRole == TEXT("Editor")
		|| InRole == TEXT("Headless")
		|| InRole == TEXT("Server")
		|| InRole == TEXT("Viewer")
		|| InRole == TEXT("Agent")
		|| InRole == TEXT("Director")
		|| InRole == TEXT("Replay");
}
