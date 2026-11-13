#include "EpicodeReplayJob.h"

#include "EpicodeSandboxJson.h"

namespace
{
	FString StateToString(EEpicodeReplayJobState InState)
	{
		switch (InState)
		{
		case EEpicodeReplayJobState::Queued: return TEXT("queued");
		case EEpicodeReplayJobState::Running: return TEXT("running");
		case EEpicodeReplayJobState::Completed: return TEXT("completed");
		case EEpicodeReplayJobState::Failed: return TEXT("failed");
		case EEpicodeReplayJobState::Cancelled: return TEXT("cancelled");
		default: return TEXT("unknown");
		}
	}
}

TSharedPtr<FJsonObject> FEpicodeReplayJobStatus::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("id"), Id);
	Result->SetStringField(TEXT("root"), Root);
	Result->SetStringField(TEXT("requestedBy"), RequestedBy);
	Result->SetNumberField(TEXT("targetSequence"), static_cast<double>(TargetSequence));
	Result->SetStringField(TEXT("state"), StateToString(State));
	Result->SetNumberField(TEXT("appliedEvents"), AppliedEvents);
	Result->SetStringField(TEXT("error"), Error);
	Result->SetStringField(TEXT("digest"), Digest);
	TArray<TSharedPtr<FJsonValue>> DiffValues;
	for (const FEpicodeReplayDiff& Diff : Diffs)
	{
		DiffValues.Add(MakeShared<FJsonValueObject>(Diff.ToJson()));
	}
	Result->SetArrayField(TEXT("diffs"), DiffValues);
	return Result;
}

void FEpicodeReplayJobQueue::Initialize(int32 InMaxConcurrent)
{
	MaxConcurrent = FMath::Max(1, InMaxConcurrent);
	Jobs.Reset();
	Runners.Reset();
	LiveSeconds = 0.0;
	LiveTicks = 0;
}

FString FEpicodeReplayJobQueue::Submit(
	const FString& InRoot,
	int64 InTargetSequence,
	const FEpicodeReplayResimOptions& InOptions,
	const FString& InRequestedBy,
	EEpicodeSandboxParticipantKind InActorKind,
	FString& OutError)
{
	if (!FEpicodeReplayState::CanReplay(InActorKind, OutError))
	{
		return FString();
	}
	if (InRoot.IsEmpty() || InTargetSequence < 0 || InRequestedBy.IsEmpty())
	{
		OutError = TEXT("REPLAY_JOB_INVALID: missing root, target or requester");
		return FString();
	}
	FEpicodeReplayJobStatus Job;
	Job.Id = TEXT("replay-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	Job.Root = InRoot;
	Job.RequestedBy = InRequestedBy;
	Job.TargetSequence = InTargetSequence;
	Job.State = EEpicodeReplayJobState::Queued;
	Jobs.Add(Job);
	TUniquePtr<FEpicodeReplayResimRunner> Runner = MakeUnique<FEpicodeReplayResimRunner>();
	if (!Runner->Begin(InRoot, InTargetSequence, InOptions, OutError))
	{
		FEpicodeReplayJobStatus* Stored = FindMutableJob(Job.Id);
		Stored->State = EEpicodeReplayJobState::Failed;
		Stored->Error = OutError;
		return Job.Id;
	}
	Runners.Add(Job.Id, MoveTemp(Runner));
	return Job.Id;
}

FEpicodeReplayJobStatus* FEpicodeReplayJobQueue::FindMutableJob(const FString& InJobId)
{
	return Jobs.FindByPredicate([&InJobId](const FEpicodeReplayJobStatus& Job)
	{
		return Job.Id == InJobId;
	});
}

const FEpicodeReplayJobStatus* FEpicodeReplayJobQueue::FindJob(const FString& InJobId) const
{
	return Jobs.FindByPredicate([&InJobId](const FEpicodeReplayJobStatus& Job)
	{
		return Job.Id == InJobId;
	});
}

int32 FEpicodeReplayJobQueue::GetRunningCount() const
{
	int32 Count = 0;
	for (const FEpicodeReplayJobStatus& Job : Jobs)
	{
		if (Job.State == EEpicodeReplayJobState::Running)
		{
			++Count;
		}
	}
	return Count;
}

int32 FEpicodeReplayJobQueue::StartQueued(FString& OutError)
{
	OutError.Reset();
	int32 Started = 0;
	for (FEpicodeReplayJobStatus& Job : Jobs)
	{
		if (GetRunningCount() >= MaxConcurrent)
		{
			break;
		}
		if (Job.State != EEpicodeReplayJobState::Queued)
		{
			continue;
		}
		TUniquePtr<FEpicodeReplayResimRunner>* RunnerPtr = Runners.Find(Job.Id);
		FEpicodeReplayResimRunner* Runner = RunnerPtr ? RunnerPtr->Get() : nullptr;
		if (!Runner)
		{
			Job.State = EEpicodeReplayJobState::Failed;
			Job.Error = TEXT("REPLAY_JOB_INVALID: missing runner");
			continue;
		}
		Job.State = EEpicodeReplayJobState::Running;
		++Started;
	}
	return Started;
}

void FEpicodeReplayJobQueue::UpdateFromRunner(FEpicodeReplayJobStatus& InOutJob) const
{
	const TUniquePtr<FEpicodeReplayResimRunner>* RunnerPtr = Runners.Find(InOutJob.Id);
	if (!RunnerPtr || !RunnerPtr->IsValid())
	{
		return;
	}
	const FEpicodeReplayResimResult& Result = (*RunnerPtr)->GetResult();
	InOutJob.AppliedEvents = static_cast<int32>(Result.AppliedEvents);
	InOutJob.Digest = Result.Digest;
	InOutJob.Error = Result.Error;
	InOutJob.Diffs = Result.Diffs;
}

bool FEpicodeReplayJobQueue::Step(const FString& InJobId, int32 InMaxEvents, FString& OutError)
{
	OutError.Reset();
	FEpicodeReplayJobStatus* Job = FindMutableJob(InJobId);
	if (!Job)
	{
		OutError = TEXT("REPLAY_JOB_NOT_FOUND: unknown job");
		return false;
	}
	if (Job->State == EEpicodeReplayJobState::Completed || Job->State == EEpicodeReplayJobState::Failed || Job->State == EEpicodeReplayJobState::Cancelled)
	{
		OutError = TEXT("REPLAY_JOB_TERMINAL: job is already finished");
		return false;
	}
	if (Job->State == EEpicodeReplayJobState::Queued)
	{
		StartQueued(OutError);
	}
	if (Job->State != EEpicodeReplayJobState::Running)
	{
		OutError = TEXT("REPLAY_JOB_NOT_STARTED: job is still queued");
		return false;
	}
	TUniquePtr<FEpicodeReplayResimRunner>* RunnerPtr = Runners.Find(InJobId);
	FEpicodeReplayResimRunner* Runner = RunnerPtr ? RunnerPtr->Get() : nullptr;
	if (!Runner)
	{
		Job->State = EEpicodeReplayJobState::Failed;
		Job->Error = TEXT("REPLAY_JOB_INVALID: missing runner");
		OutError = Job->Error;
		return false;
	}
	const EEpicodeReplayStepResult StepResult = Runner->Step(InMaxEvents, OutError);
	UpdateFromRunner(*Job);
	switch (StepResult)
	{
	case EEpicodeReplayStepResult::Completed:
		Job->State = EEpicodeReplayJobState::Completed;
		return true;
	case EEpicodeReplayStepResult::Failed:
		Job->State = EEpicodeReplayJobState::Failed;
		Job->Error = OutError;
		return false;
	case EEpicodeReplayStepResult::Cancelled:
		Job->State = EEpicodeReplayJobState::Cancelled;
		Job->Error = TEXT("REPLAY_CANCELLED");
		return false;
	default:
		return true;
	}
}

bool FEpicodeReplayJobQueue::RunToCompletion(const FString& InJobId, FString& OutError)
{
	FEpicodeReplayJobStatus* Job = FindMutableJob(InJobId);
	if (!Job)
	{
		OutError = TEXT("REPLAY_JOB_NOT_FOUND: unknown job");
		return false;
	}
	if (Job->State == EEpicodeReplayJobState::Queued)
	{
		StartQueued(OutError);
	}
	while (Job->State == EEpicodeReplayJobState::Running)
	{
		if (!Step(InJobId, 1000000, OutError))
		{
			return false;
		}
	}
	return Job->State == EEpicodeReplayJobState::Completed;
}

bool FEpicodeReplayJobQueue::Cancel(const FString& InJobId, FString& OutError)
{
	OutError.Reset();
	FEpicodeReplayJobStatus* Job = FindMutableJob(InJobId);
	if (!Job)
	{
		OutError = TEXT("REPLAY_JOB_NOT_FOUND: unknown job");
		return false;
	}
	if (Job->State == EEpicodeReplayJobState::Completed)
	{
		OutError = TEXT("REPLAY_JOB_TERMINAL: completed job cannot be cancelled");
		return false;
	}
	if (Job->State == EEpicodeReplayJobState::Failed || Job->State == EEpicodeReplayJobState::Cancelled)
	{
		return true;
	}
	if (TUniquePtr<FEpicodeReplayResimRunner>* RunnerPtr = Runners.Find(InJobId))
	{
		if (RunnerPtr->IsValid())
		{
			(*RunnerPtr)->Cancel();
		}
	}
	Job->State = EEpicodeReplayJobState::Cancelled;
	Job->Error = TEXT("REPLAY_CANCELLED");
	Runners.Remove(InJobId);
	return true;
}
