#pragma once

#include "CoreMinimal.h"

enum class EEpicodeDirectorTab : uint8
{
	Overview,
	Timeline,
	Beats,
	Cues,
	Controls
};

struct EPICODESTUDIO_API FEpicodeStudioEventRow
{
	int64 Sequence = 0;
	FString Type;
	FString ActorId;
	FString Summary;
	FString BeatId;
};

class EPICODESTUDIO_API FEpicodeStudioDirectorModel
{
public:
	void Reset();

	bool ApplySandboxSnapshot(const TSharedPtr<FJsonObject>& InSnapshot);
	bool ApplyDirectorSnapshot(const TSharedPtr<FJsonObject>& InSnapshot);
	void SetActiveTab(EEpicodeDirectorTab InTab) { ActiveTab = InTab; }
	EEpicodeDirectorTab GetActiveTab() const { return ActiveTab; }
	void SetZoom(double InZoom);
	double GetZoom() const { return Zoom; }
	void SetPlayhead(int64 InSequence) { Playhead = FMath::Max<int64>(0, InSequence); }
	int64 GetPlayhead() const { return Playhead; }
	void SetEventFilter(const FString& InFilter) { EventFilter = InFilter; }
	const FString& GetEventFilter() const { return EventFilter; }
	void SetEvents(const TArray<FEpicodeStudioEventRow>& InEvents) { Events = InEvents; }
	const TArray<FEpicodeStudioEventRow>& GetEvents() const { return Events; }
	TArray<FEpicodeStudioEventRow> GetFilteredEvents() const;

	void SetRole(const FString& InRole) { Role = InRole; }
	const FString& GetRole() const { return Role; }
	bool CanControl() const { return Role == TEXT("owner") || Role == TEXT("director"); }
	bool IsReadOnly() const { return Role == TEXT("observer") || Role == TEXT("viewer") || Role == TEXT("performer"); }

	bool SelectTake(const FString& InTakeId, int32 InVersion);
	const FString& GetSelectedTakeId() const { return SelectedTakeId; }
	int32 GetSelectedTakeVersion() const { return SelectedTakeVersion; }
	int32 GetLatestTakeVersion() const { return LatestTakeVersion; }
	void SetLatestTakeVersion(int32 InVersion) { LatestTakeVersion = FMath::Max(0, InVersion); }

	bool EnterGhostMode() { return SetMode(TEXT("ghost")); }
	bool EnterLiveMode() { return SetMode(TEXT("live")); }
	bool EnterResimMode() { return SetMode(TEXT("resim")); }
	const FString& GetMode() const { return Mode; }
	bool IsGhostMode() const { return Mode == TEXT("ghost") || Mode == TEXT("resim"); }
	void SetBranchId(const FString& InBranchId) { BranchId = InBranchId; }
	const FString& GetBranchId() const { return BranchId; }
	bool AcquireTakeover(const FString& InParticipantId, const FString& InDirectorId);
	bool ReleaseTakeover(const FString& InParticipantId);
	const FString& GetTakeoverDirector(const FString& InParticipantId) const;

	double GetFrameBudgetMs() const { return 100.0; }
	double GetLastApplyMs() const { return LastApplyMs; }
	void SetLastApplyMs(double InMs) { LastApplyMs = InMs; }
	bool MeetsResponsivenessBudget() const { return LastApplyMs <= GetFrameBudgetMs(); }

	const FString& GetProductionId() const { return ProductionId; }
	const FString& GetCurrentBeatId() const { return CurrentBeatId; }
	const FString& GetCurrentObjectiveId() const { return CurrentObjectiveId; }
	const FString& GetLastError() const { return LastError; }
	const TArray<FString>& GetBudgetLines() const { return BudgetLines; }
	const TArray<FString>& GetCueLines() const { return CueLines; }
	const TArray<FString>& GetValidationWarnings() const { return ValidationWarnings; }
	bool HasProduction() const { return !ProductionId.IsEmpty(); }
	bool IsSandboxActive() const { return bSandboxActive; }

private:
	bool SetMode(const FString& InMode);

	EEpicodeDirectorTab ActiveTab = EEpicodeDirectorTab::Overview;
	double Zoom = 1.0;
	int64 Playhead = 0;
	FString EventFilter;
	TArray<FEpicodeStudioEventRow> Events;
	FString Role = TEXT("observer");
	FString SelectedTakeId;
	int32 SelectedTakeVersion = 0;
	int32 LatestTakeVersion = 0;
	FString Mode = TEXT("live");
	FString BranchId;
	double LastApplyMs = 0.0;
	FString ProductionId;
	FString CurrentBeatId;
	FString CurrentObjectiveId;
	FString LastError;
	TArray<FString> BudgetLines;
	TArray<FString> CueLines;
	TArray<FString> ValidationWarnings;
	bool bSandboxActive = false;
	TMap<FString, FString> Takeovers;
};
