#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "EpicodeRenderTypes.h"

class EPICODERENDER_API FEpicodeRenderService
{
public:
	static FEpicodeRenderService& Get();

	void Initialize();
	void Shutdown();

	TSharedPtr<FEpicodeRenderJob> Submit(const FEpicodeRenderRequest& InRequest, FString& OutError);
	TSharedPtr<FEpicodeRenderJob> GetJob(const FString& InJobId) const;
	TArray<TSharedPtr<FEpicodeRenderJob>> GetJobs() const;
	bool Cancel(const FString& InJobId);
	TSharedPtr<FEpicodeRenderJob> Retry(const FString& InJobId, FString& OutError);
	bool IsWorkerInvocation() const { return bWorkerInvocation; }

	void RegisterCommands();
	static bool RunWorker(const FString& RequestPath);

private:
	bool Tick(float DeltaSeconds);
	bool WriteRequestFile(const TSharedPtr<FEpicodeRenderJob>& Job, FString& OutPath);
	bool LaunchWorker(const TSharedPtr<FEpicodeRenderJob>& Job, FString& OutError);
	void FinishJobFromDisk(const TSharedPtr<FEpicodeRenderJob>& Job);
	void CollectOutputFiles(const FString& Directory, TArray<FString>& OutFiles) const;

	mutable FCriticalSection Lock;
	TArray<TSharedPtr<FEpicodeRenderJob>> Jobs;
	FTSTicker::FDelegateHandle TickerHandle;
	bool bInitialized = false;
	bool bWorkerInvocation = false;
};
