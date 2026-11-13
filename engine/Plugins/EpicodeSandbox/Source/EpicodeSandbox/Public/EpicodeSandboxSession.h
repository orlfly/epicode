#pragma once

#include "CoreMinimal.h"
#include "EpicodeProductionDocument.h"
#include "EpicodeSandboxClock.h"
#include "EpicodeSandboxRandom.h"
#include "EpicodeSandboxWorld.h"
#include "EpicodeSceneDocument.h"

enum class EEpicodeSandboxSessionState : uint8
{
	Uninitialized,
	Ready,
	Running,
	Closed,
	Failed
};

struct EPICODESANDBOX_API FEpicodeSandboxSessionConfig
{
	FString SessionId;
	int32 TickRate = 30;
	int32 Seed = 0;
	int32 MaxTicksPerAdvance = 8;
};

class EPICODESANDBOX_API FEpicodeSandboxSession
{
public:
	bool Load(const FEpicodeProductionDocument& InProduction, const FEpicodeSceneDocument& InScene, const FEpicodeSandboxSessionConfig& InConfig, FString& OutError);
	bool LoadLegacy(const FEpicodeSceneDocument& InScene, const FEpicodeSandboxSessionConfig& InConfig, FString& OutError);
	bool Reset(FString& OutError);
	bool Start(FString& OutError);
	bool Stop(FString& OutError);
	bool Close(FString& OutError);
	int32 Tick(double InDeltaSeconds);
	bool SaveSnapshot(const FString& InSessionRoot, FString& OutError) const;
	bool RestoreSnapshot(const FString& InSessionRoot, FString& OutError);

	EEpicodeSandboxSessionState GetState() const { return State; }
	bool IsRunning() const { return State == EEpicodeSandboxSessionState::Running; }
	const FString& GetError() const { return Error; }
	const FEpicodeProductionDocument& GetProduction() const { return Production; }
	const FEpicodeSceneDocument& GetScene() const { return Scene; }
	const FEpicodeSandboxWorld& GetWorld() const { return World; }
	FEpicodeSandboxWorld& GetMutableWorld() { return World; }
	const FEpicodeSandboxSimulationClock& GetClock() const { return Clock; }
	const FEpicodeSandboxRandomStreams& GetRandomStreams() const { return RandomStreams; }
	FEpicodeSandboxRandomStreams& GetMutableRandomStreams() { return RandomStreams; }
	const FEpicodeSandboxEventStream& GetEvents() const { return Events; }
	FEpicodeSandboxEventStream& GetMutableEvents() { return Events; }
	const FEpicodeSandboxSessionConfig& GetConfig() const { return Config; }
	FString GetDigest() const;

private:
	bool BuildWorld(FString& OutError);

	EEpicodeSandboxSessionState State = EEpicodeSandboxSessionState::Uninitialized;
	FString Error;
	FEpicodeProductionDocument Production;
	FEpicodeSceneDocument Scene;
	FEpicodeSandboxSessionConfig Config;
	FEpicodeSandboxWorld World;
	FEpicodeSandboxSimulationClock Clock;
	FEpicodeSandboxRandomStreams RandomStreams;
	FEpicodeSandboxEventStream Events;
};
