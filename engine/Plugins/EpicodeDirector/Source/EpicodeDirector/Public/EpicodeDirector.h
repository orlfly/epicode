#pragma once

#include "CoreMinimal.h"
#include "EpicodeProductionDocument.h"
#include "EpicodeReplayEngine.h"
#include "EpicodeSandboxRules.h"
#include "EpicodeSandboxWorld.h"

enum class EEpicodeDirectorViewMode : uint8
{
	Live,
	Ghost,
	Resim
};

enum class EEpicodeCueTrigger : uint8
{
	Time,
	Event,
	Beat,
	Condition
};

struct EPICODEDIRECTOR_API FEpicodeDirectorCue
{
	FString Id;
	EEpicodeCueTrigger Trigger = EEpicodeCueTrigger::Time;
	double Time = 0.0;
	FString EventType;
	FString BeatId;
	FString ConditionText;
	FEpicodeSandboxCondition Condition;
	bool bHasCondition = false;
	bool bOnce = true;
	TArray<FString> Actions;

	static bool Parse(const TSharedPtr<FJsonObject>& InJson, FEpicodeDirectorCue& OutCue, FString& OutError);
	TSharedPtr<FJsonObject> ToJson() const;
};

struct EPICODEDIRECTOR_API FEpicodeDirectorBeat
{
	FString Id;
	FString Name;
	TArray<FEpicodeSandboxCondition> EntryConditions;
	TArray<FEpicodeSandboxCondition> ExitConditions;
	TArray<FString> ObjectiveIds;
	TArray<FString> CueIds;
	TArray<FString> NextBeatIds;

	static bool Parse(const TSharedPtr<FJsonObject>& InJson, FEpicodeDirectorBeat& OutBeat, FString& OutError);
	TSharedPtr<FJsonObject> ToJson() const;
};

struct EPICODEDIRECTOR_API FEpicodeDirectorObjective
{
	FString Id;
	FString Description;
	FString ConditionText;
	FEpicodeSandboxCondition Condition;
	bool bHasCondition = false;

	static bool Parse(const TSharedPtr<FJsonObject>& InJson, FEpicodeDirectorObjective& OutObjective, FString& OutError);
	TSharedPtr<FJsonObject> ToJson() const;
};

class EPICODEDIRECTOR_API FEpicodeBeatGraph
{
public:
	FString EntryBeatId;
	TMap<FString, FEpicodeSandboxValue> Variables;
	TArray<FEpicodeDirectorBeat> Beats;
	TArray<FEpicodeDirectorObjective> Objectives;
	TArray<FEpicodeDirectorCue> Cues;

	static bool Parse(const TSharedPtr<FJsonObject>& InNarrative, FEpicodeBeatGraph& OutGraph, FString& OutError);
	bool Validate(FString& OutError) const;
	const FEpicodeDirectorBeat* FindBeat(const FString& InId) const;
	const FEpicodeDirectorObjective* FindObjective(const FString& InId) const;
	const FEpicodeDirectorCue* FindCue(const FString& InId) const;
	TArray<FString> GetReachableBeats() const;
};

struct EPICODEDIRECTOR_API FEpicodeDirectorTake
{
	FString Id;
	int32 Version = 1;
	int64 InSequence = 0;
	int64 OutSequence = 0;
	int64 InTick = 0;
	int64 OutTick = 0;
	FString BeatId;
	FString ResolutionPreset = TEXT("1080p");
	FString ContentDigest;
	FString Note;
	TArray<FString> CameraIds;
	TArray<FString> CameraPresetIds;
	FString Checksum;

	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeDirectorTake& OutTake, FString& OutError);
	FString ComputeChecksum() const;
};

class EPICODEDIRECTOR_API FEpicodeTakeLibrary
{
public:
	static bool Save(const FString& InRoot, FEpicodeDirectorTake& InOutTake, FString& OutError);
	static bool Load(const FString& InRoot, const FString& InTakeId, int32 InVersion, FEpicodeDirectorTake& OutTake, FString& OutError);
	static bool LoadLatest(const FString& InRoot, const FString& InTakeId, FEpicodeDirectorTake& OutTake, FString& OutError);
	static TArray<int32> ListVersions(const FString& InRoot, const FString& InTakeId);
	static FString GetTakePath(const FString& InRoot, const FString& InTakeId, int32 InVersion);
};

struct EPICODEDIRECTOR_API FEpicodeTakeoverLease
{
	FString ParticipantId;
	FString DirectorId;
	FString OriginalControllerId;
	FString Reason;
	FString EndCondition;
	int64 StartedTick = 0;
	bool bActive = false;

	TSharedPtr<FJsonObject> ToJson() const;
};

struct EPICODEDIRECTOR_API FEpicodeDirectorCommandResult
{
	bool bSuccess = false;
	bool bDuplicate = false;
	FString Error;
};

class EPICODEDIRECTOR_API FEpicodeDirectorController
{
public:
	bool Initialize(const FEpicodeBeatGraph& InGraph, FEpicodeSandboxWorld& InWorld, FEpicodeSandboxEventStream* InEvents, const TArray<FEpicodeProductionCameraPreset>& InCameraPresets, FString& OutError);
	bool Initialize(const FEpicodeProductionDocument& InProduction, FEpicodeSandboxWorld& InWorld, FEpicodeSandboxEventStream* InEvents, FString& OutError);
	int32 Tick(double InDeltaSeconds, int64 InTick);

	FEpicodeDirectorCommandResult Pause(const FString& InActorId, const FString& InKey, int64 InTick);
	FEpicodeDirectorCommandResult Resume(const FString& InActorId, const FString& InKey, int64 InTick);
	FEpicodeDirectorCommandResult SetTimeScale(const FString& InActorId, const FString& InKey, double InScale, int64 InTick);
	FEpicodeDirectorCommandResult SwitchCamera(const FString& InActorId, const FString& InKey, const FString& InCameraId, const FString& InPresetId, int64 InTick);
	FEpicodeDirectorCommandResult SetEnvironment(const FString& InActorId, const FString& InKey, const TSharedPtr<FJsonObject>& InEnvironment, int64 InTick);
	FEpicodeDirectorCommandResult SpawnEntity(const FString& InActorId, const FString& InKey, const FEpicodeSandboxEntity& InEntity, int64 InTick);
	FEpicodeDirectorCommandResult RemoveEntity(const FString& InActorId, const FString& InKey, const FString& InEntityId, int64 InTick);
	FEpicodeDirectorCommandResult SetObjective(const FString& InActorId, const FString& InKey, const FString& InObjectiveId, int64 InTick);
	FEpicodeDirectorCommandResult EnterBeat(const FString& InActorId, const FString& InKey, const FString& InBeatId, int64 InTick);
	FEpicodeDirectorCommandResult Message(const FString& InActorId, const FString& InKey, const FString& InText, int64 InTick);

	FEpicodeDirectorCommandResult AcquireTakeover(const FString& InActorId, const FString& InKey, const FString& InParticipantId, const FString& InReason, const FString& InEndCondition, int64 InTick);
	FEpicodeDirectorCommandResult ReleaseTakeover(const FString& InActorId, const FString& InKey, const FString& InParticipantId, int64 InTick);

	bool CreateTake(const FString& InActorId, const FString& InKey, const FString& InRoot, FEpicodeDirectorTake& InOutTake, FEpicodeDirectorCommandResult& OutResult, int64 InTick);
	bool ReviseTake(const FString& InActorId, const FString& InKey, const FString& InRoot, const FString& InTakeId, FEpicodeDirectorTake InDraft, FEpicodeDirectorTake& OutTake, FEpicodeDirectorCommandResult& OutResult, int64 InTick);

	FEpicodeDirectorCommandResult SetView(const FString& InActorId, const FString& InKey, EEpicodeDirectorViewMode InMode, const FString& InReplayRoot, int64 InSequence);
	bool CreateBranch(const FString& InActorId, const FString& InKey, const FString& InParentRoot, const FString& InBranchRoot, const FString& InBranchId, int64 InForkSequence, FEpicodeDirectorCommandResult& OutResult);

	const FEpicodeBeatGraph& GetGraph() const { return Graph; }
	const FEpicodeSandboxNarrativeState& GetNarrative() const { return Narrative; }
	const TArray<FEpicodeTakeoverLease>& GetTakeoverLeases() const { return TakeoverLeases; }
	const TArray<FString>& GetMessages() const { return Messages; }
	EEpicodeDirectorViewMode GetViewMode() const { return ViewMode; }
	const FEpicodeSandboxWorld& GetViewWorld() const;
	double GetDirectorTime() const { return DirectorTime; }
	double GetTimeScale() const { return TimeScale; }
	bool IsPaused() const { return bPaused; }
	const FString& GetCameraId() const { return CameraId; }
	const FString& GetCameraPresetId() const { return CameraPresetId; }
	const TSharedPtr<FJsonObject>& GetEnvironment() const { return Environment; }
	int64 GetViewSequence() const { return ViewSequence; }

private:
	bool BeginCommand(const FString& InActorId, const FString& InKey, FEpicodeDirectorCommandResult& OutResult, bool bAllowNonLive = false);
	void EndCommand(const FString& InKey, bool bInSuccess, const FString& InError, FEpicodeDirectorCommandResult& OutResult);
	bool CanDirect(const FString& InActorId, FString& OutError) const;
	bool AppendEvent(const FString& InType, const FString& InActorId, const TSharedPtr<FJsonObject>& InPayload, int64 InTick, FString& OutError);
	void SyncNarrative();
	bool EnterBeatInternal(const FString& InBeatId, const FString& InActorId, int64 InTick, FString& OutError);
	bool CompleteObjectiveInternal(const FString& InObjectiveId, int64 InTick, FString& OutError);
	int32 RunCues(int64 InTick);
	bool ExecuteCueAction(const FString& InAction, const FEpicodeDirectorCue& InCue, int64 InTick, FString& OutError);
	bool IsCameraKnown(const FString& InCameraId, const FString& InPresetId) const;
	bool ValidateTake(const FEpicodeDirectorTake& InTake, FString& OutError) const;
	void UpdateViewWorld();

	FEpicodeBeatGraph Graph;
	FEpicodeSandboxWorld* World = nullptr;
	FEpicodeSandboxEventStream* Events = nullptr;
	TArray<FEpicodeProductionCameraPreset> CameraPresets;
	FEpicodeSandboxNarrativeState Narrative;
	TMap<FString, bool> CommandResults;
	TMap<FString, FString> CommandErrors;
	TSet<FString> ExecutedCues;
	TMap<FString, bool> CueConditionLast;
	TMap<FString, FEpicodeTakeoverLease> ActiveTakeovers;
	TArray<FEpicodeTakeoverLease> TakeoverLeases;
	TArray<FString> Messages;
	double DirectorTime = 0.0;
	double TimeScale = 1.0;
	bool bPaused = false;
	FString CameraId;
	FString CameraPresetId;
	TSharedPtr<FJsonObject> Environment;
	EEpicodeDirectorViewMode ViewMode = EEpicodeDirectorViewMode::Live;
	FString ReplayRoot;
	int64 ViewSequence = 0;
	FEpicodeSandboxWorld ViewWorld;
	int64 LastCueEventSequence = 0;
};
