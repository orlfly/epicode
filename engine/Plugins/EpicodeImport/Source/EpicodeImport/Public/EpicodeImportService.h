#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "EpicodeImportTypes.h"

class EPICODEIMPORT_API FEpicodeImportService
{
public:
	static FEpicodeImportService& Get();

	void Initialize();
	void Shutdown();

	TSharedPtr<FEpicodeImportJob> Submit(const FEpicodeImportRequest& InRequest, FString& OutError);
	bool ImportSynchronously(const FEpicodeImportRequest& InRequest, FEpicodeImportResult& OutResult);
	TSharedPtr<FEpicodeImportJob> GetJob(const FString& InJobId) const;
	TArray<TSharedPtr<FEpicodeImportJob>> GetJobs() const;
	bool Cancel(const FString& InJobId);

private:
	bool Tick(float DeltaSeconds);
	bool ValidateRequest(const FEpicodeImportRequest& InRequest, FString& OutError) const;
	bool ImportWithInterchange(const FEpicodeImportRequest& InRequest, FEpicodeImportResult& OutResult);
	bool CreateEntitiesForAssets(const FEpicodeImportRequest& InRequest, const TArray<UObject*>& ImportedObjects, FEpicodeImportResult& OutResult);
	FString ResolveDestination(const FEpicodeImportRequest& InRequest) const;
	void EnsureIndexLoaded() const;
	bool SaveIndex() const;
	FString ComputeContentHash(const FString& FullPath, FString& OutError) const;
	FString MakeIndexKey(const FString& ContentHash, const FEpicodeImportRequest& InRequest) const;
	bool TryReuseIndexedAssets(const FString& ContentHash, const FEpicodeImportRequest& InRequest, TArray<UObject*>& OutObjects, FEpicodeImportResult& OutResult);
	void RecordIndexEntry(const FString& ContentHash, const FEpicodeImportRequest& InRequest, const TArray<UObject*>& ImportedObjects);

	mutable FCriticalSection Lock;
	TArray<TSharedPtr<FEpicodeImportJob>> Jobs;
	FTSTicker::FDelegateHandle TickerHandle;
	mutable TSharedPtr<FJsonObject> ImportIndex;
	mutable FCriticalSection IndexLock;
	bool bInitialized = false;
};
