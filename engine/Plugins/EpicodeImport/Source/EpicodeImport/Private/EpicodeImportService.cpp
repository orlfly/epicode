#include "EpicodeImportService.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "EpicodeCommand.h"
#include "EpicodeEngineSubsystem.h"
#include "EpicodeImportClassifier.h"
#include "EpicodeSceneSubsystem.h"
#include "EpicodeSettings.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "InterchangeManager.h"
#include "InterchangeSourceData.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if PLATFORM_UNIX
#include <sys/stat.h>
#endif

namespace
{
	bool IsSymlinkPath(const FString& Path)
	{
#if PLATFORM_UNIX
		struct stat Info;
		return lstat(TCHAR_TO_UTF8(*Path), &Info) == 0 && S_ISLNK(Info.st_mode);
#else
		return IFileManager::Get().IsSymlink(*Path);
#endif
	}

	bool HasSymlinkComponent(const FString& FullPath, const FString& FullRoot)
	{
		FString Current = FullPath;
		while (!Current.IsEmpty() && Current.Len() > FullRoot.Len())
		{
			if (IsSymlinkPath(Current))
			{
				return true;
			}
			const FString Parent = FPaths::GetPath(Current);
			if (Parent.IsEmpty() || Parent == Current)
			{
				break;
			}
			Current = Parent;
		}
		return IsSymlinkPath(FullRoot);
	}
}

FEpicodeImportService& FEpicodeImportService::Get()
{
	static FEpicodeImportService Instance;
	return Instance;
}

void FEpicodeImportService::Initialize()
{
	if (bInitialized)
	{
		return;
	}
	bInitialized = true;
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FEpicodeImportService::Tick), 0.0f);
}

void FEpicodeImportService::Shutdown()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	FScopeLock ScopeLock(&Lock);
	Jobs.Reset();
	bInitialized = false;
}

bool FEpicodeImportService::ValidateRequest(const FEpicodeImportRequest& InRequest, FString& OutError) const
{
	const FString FullPath = FPaths::ConvertRelativePathToFull(InRequest.FilePath);
	if (!IFileManager::Get().FileExists(*FullPath))
	{
		OutError = FString::Printf(TEXT("File not found: %s"), *FullPath);
		return false;
	}
	const int64 Size = IFileManager::Get().FileSize(*FullPath);
	if (Size <= 0)
	{
		OutError = TEXT("Import file is empty.");
		return false;
	}
	if (Size > UEpicodeSettings::Get()->MaxImportBytes)
	{
		OutError = FString::Printf(TEXT("Import file exceeds the %lld byte limit."), UEpicodeSettings::Get()->MaxImportBytes);
		return false;
	}
	if (!FEpicodeImportClassifier::IsSupportedExtension(FullPath))
	{
		OutError = FString::Printf(TEXT("Unsupported import format: %s"), *FPaths::GetExtension(FullPath));
		return false;
	}
	const FString Extension = FPaths::GetExtension(FullPath).ToLower();
	if (Extension == TEXT("umap"))
	{
		OutError = TEXT("Map import is not available in the asset importer. Use the scene library to open a map.");
		return false;
	}
	const TArray<FString> Roots = UEpicodeSettings::Get()->GetResolvedImportRoots();
	if (Roots.Num() > 0)
	{
		bool bAllowed = false;
		FString MatchedRoot;
		for (const FString& Root : Roots)
		{
			FString FullRoot = FPaths::ConvertRelativePathToFull(Root);
			FPaths::NormalizeDirectoryName(FullRoot);
			if (FullPath.Equals(FullRoot, ESearchCase::IgnoreCase)
				|| FullPath.StartsWith(FullRoot + TEXT("/"), ESearchCase::IgnoreCase))
			{
				bAllowed = true;
				MatchedRoot = FullRoot;
				break;
			}
		}
		if (!bAllowed)
		{
			OutError = TEXT("Import file is outside the configured import roots.");
			return false;
		}
		if (HasSymlinkComponent(FullPath, MatchedRoot))
		{
			OutError = TEXT("Import file path contains a symbolic link.");
			return false;
		}
	}
	if (!InRequest.DestinationPath.IsEmpty())
	{
		if (!InRequest.DestinationPath.StartsWith(TEXT("/Game/"), ESearchCase::CaseSensitive)
			|| InRequest.DestinationPath.Contains(TEXT(".."))
			|| InRequest.DestinationPath.Contains(TEXT("\\"))
			|| InRequest.DestinationPath.Contains(TEXT("//"))
			|| InRequest.DestinationPath.EndsWith(TEXT("/")))
		{
			OutError = TEXT("DestinationPath must be a content path under /Game/.");
			return false;
		}
	}
	if (InRequest.Kind == EEpicodeImportKind::Unknown)
	{
		OutError = TEXT("Import kind could not be determined.");
		return false;
	}
	return true;
}

FString FEpicodeImportService::ResolveDestination(const FEpicodeImportRequest& InRequest) const
{
	if (!InRequest.DestinationPath.IsEmpty())
	{
		return InRequest.DestinationPath;
	}
	const FString Kind = FEpicodeImportClassifier::KindToString(InRequest.Kind == EEpicodeImportKind::Unknown ? FEpicodeImportClassifier::Classify(InRequest.FilePath) : InRequest.Kind);
	FString BaseName = FPaths::GetBaseFilename(InRequest.FilePath);
	BaseName = FPaths::MakeValidFileName(BaseName, TEXT('_'));
	return FString::Printf(TEXT("%s/%s/%s"), *UEpicodeSettings::Get()->ImportContentRoot, *Kind, *BaseName);
}

FString FEpicodeImportService::ComputeContentHash(const FString& FullPath, FString& OutError) const
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *FullPath))
	{
		OutError = FString::Printf(TEXT("Unable to read import file: %s"), *FullPath);
		return FString();
	}
	const FSHAHash Hash = FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num());
	return Hash.ToString();
}

void FEpicodeImportService::EnsureIndexLoaded() const
{
	FScopeLock ScopeLock(&IndexLock);
	if (ImportIndex.IsValid())
	{
		return;
	}
	ImportIndex = MakeShared<FJsonObject>();
	ImportIndex->SetNumberField(TEXT("version"), 1);
	TSharedPtr<FJsonObject> Entries = MakeShared<FJsonObject>();
	ImportIndex->SetObjectField(TEXT("entries"), Entries);

	const FString IndexPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Epicode/imports.json"));
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *IndexPath))
	{
		return;
	}
	TSharedPtr<FJsonObject> Loaded;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Loaded) || !Loaded.IsValid())
	{
		return;
	}
	const TSharedPtr<FJsonObject>* LoadedEntries = nullptr;
	if (Loaded->TryGetObjectField(TEXT("entries"), LoadedEntries) && LoadedEntries)
	{
		ImportIndex->SetObjectField(TEXT("entries"), *LoadedEntries);
	}
}

bool FEpicodeImportService::SaveIndex() const
{
	if (!ImportIndex.IsValid())
	{
		return false;
	}
	const FString IndexPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Epicode/imports.json"));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(IndexPath), true);
	FString Text;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
	FJsonSerializer::Serialize(ImportIndex.ToSharedRef(), Writer);
	return FFileHelper::SaveStringToFile(Text, *IndexPath);
}

FString FEpicodeImportService::MakeIndexKey(const FString& ContentHash, const FEpicodeImportRequest& InRequest) const
{
	return FString::Printf(TEXT("%s|%s|%s|%.4f|%.4f|%.4f|%.4f|%d"),
		*ContentHash,
		*FEpicodeImportClassifier::KindToString(InRequest.Kind),
		*ResolveDestination(InRequest),
		InRequest.Scale,
		InRequest.RotationOffset.Pitch,
		InRequest.RotationOffset.Yaw,
		InRequest.RotationOffset.Roll,
		InRequest.bImportMaterials ? 1 : 0);
}

bool FEpicodeImportService::TryReuseIndexedAssets(const FString& ContentHash, const FEpicodeImportRequest& InRequest, TArray<UObject*>& OutObjects, FEpicodeImportResult& OutResult)
{
	if (InRequest.bReplaceExisting)
	{
		return false;
	}
	EnsureIndexLoaded();
	const FString Key = MakeIndexKey(ContentHash, InRequest);
	FScopeLock ScopeLock(&IndexLock);
	const TSharedPtr<FJsonObject>* Entries = nullptr;
	if (!ImportIndex->TryGetObjectField(TEXT("entries"), Entries) || !Entries)
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* Entry = nullptr;
	if (!(*Entries)->TryGetObjectField(Key, Entry) || !Entry)
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Assets = nullptr;
	if (!(*Entry)->TryGetArrayField(TEXT("assets"), Assets) || !Assets || Assets->Num() == 0)
	{
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Assets)
	{
		const FString AssetPath = Value->AsString();
		UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
		if (!Asset)
		{
			(*Entries)->RemoveField(Key);
			SaveIndex();
			return false;
		}
		OutObjects.Add(Asset);
		OutResult.Assets.Add(AssetPath);
	}
	OutResult.Warnings.Add(TEXT("Reused previously imported assets with the same content hash and options."));
	OutResult.bSuccess = true;
	return true;
}

void FEpicodeImportService::RecordIndexEntry(const FString& ContentHash, const FEpicodeImportRequest& InRequest, const TArray<UObject*>& ImportedObjects)
{
	EnsureIndexLoaded();
	const FString Key = MakeIndexKey(ContentHash, InRequest);
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("hash"), ContentHash);
	Entry->SetStringField(TEXT("sourcePath"), FPaths::ConvertRelativePathToFull(InRequest.FilePath));
	Entry->SetStringField(TEXT("updatedAt"), FDateTime::UtcNow().ToIso8601());
	TArray<TSharedPtr<FJsonValue>> Assets;
	for (const UObject* Object : ImportedObjects)
	{
		if (Object)
		{
			Assets.Add(MakeShared<FJsonValueString>(Object->GetPathName()));
		}
	}
	Entry->SetArrayField(TEXT("assets"), Assets);

	FScopeLock ScopeLock(&IndexLock);
	const TSharedPtr<FJsonObject>* Entries = nullptr;
	if (ImportIndex->TryGetObjectField(TEXT("entries"), Entries) && Entries)
	{
		(*Entries)->SetObjectField(Key, Entry);
		SaveIndex();
	}
}

bool FEpicodeImportService::ImportSynchronously(const FEpicodeImportRequest& InRequest, FEpicodeImportResult& OutResult)
{
	OutResult = FEpicodeImportResult();
	FString Error;
	if (!ValidateRequest(InRequest, Error))
	{
		OutResult.ErrorCode = TEXT("INVALID_IMPORT");
		OutResult.ErrorMessage = Error;
		return false;
	}
	IEpicodeTransactionSink* TransactionSink = nullptr;
	if (UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get())
	{
		TransactionSink = EngineSubsystem->GetTransactionSink();
	}
	if (TransactionSink)
	{
		TransactionSink->BeginTransaction(TEXT("Epicode Import"));
	}
	const bool bSuccess = ImportWithInterchange(InRequest, OutResult);
	if (TransactionSink)
	{
		if (bSuccess)
		{
			TransactionSink->CommitTransaction();
		}
		else
		{
			TransactionSink->CancelTransaction();
		}
	}
	return bSuccess;
}

bool FEpicodeImportService::ImportWithInterchange(const FEpicodeImportRequest& InRequest, FEpicodeImportResult& OutResult)
{
	const FString FullPath = FPaths::ConvertRelativePathToFull(InRequest.FilePath);
	FString HashError;
	const FString ContentHash = ComputeContentHash(FullPath, HashError);
	TArray<UObject*> ImportedObjects;
	if (!ContentHash.IsEmpty() && TryReuseIndexedAssets(ContentHash, InRequest, ImportedObjects, OutResult))
	{
		return CreateEntitiesForAssets(InRequest, ImportedObjects, OutResult);
	}
	FString Destination = ResolveDestination(InRequest);
	const FString ExistingPackage = FString::Printf(
		TEXT("%s/%s"), *Destination, *FPaths::GetBaseFilename(Destination));
	if (FPackageName::DoesPackageExist(ExistingPackage) && !InRequest.bReplaceExisting)
	{
		Destination = FString::Printf(TEXT("%s_%s"), *Destination, *FDateTime::UtcNow().ToString(TEXT("%Y%m%d%H%M%S")));
	}

	UInterchangeSourceData* SourceData = NewObject<UInterchangeSourceData>(GetTransientPackage());
	SourceData->SetFilename(FullPath);

	FImportAssetParameters Parameters;
	Parameters.bIsAutomated = true;
	Parameters.bReplaceExisting = InRequest.bReplaceExisting;
	Parameters.DestinationName = FPaths::GetBaseFilename(Destination);

	const bool bImported = UInterchangeManager::GetInterchangeManager().ImportAsset(Destination, SourceData, Parameters, ImportedObjects);
	if (!bImported)
	{
		OutResult.ErrorCode = TEXT("IMPORT_FAILED");
		OutResult.ErrorMessage = FString::Printf(TEXT("Interchange could not import %s."), *FullPath);
		return false;
	}

	for (UObject* Object : ImportedObjects)
	{
		if (!Object)
		{
			continue;
		}
		const FString AssetPath = Object->GetPathName();
		OutResult.Assets.AddUnique(AssetPath);
	}
	if (OutResult.Assets.Num() == 0)
	{
		OutResult.Warnings.Add(TEXT("Interchange completed but returned no assets."));
	}
	else if (!ContentHash.IsEmpty())
	{
		RecordIndexEntry(ContentHash, InRequest, ImportedObjects);
	}

	if (!CreateEntitiesForAssets(InRequest, ImportedObjects, OutResult))
	{
		return false;
	}
	OutResult.bSuccess = true;
	return true;
}

bool FEpicodeImportService::CreateEntitiesForAssets(const FEpicodeImportRequest& InRequest, const TArray<UObject*>& ImportedObjects, FEpicodeImportResult& OutResult)
{
	UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
	if (!Scene || !Scene->HasScene())
	{
		OutResult.Warnings.Add(TEXT("No scene is open; assets were imported without scene entities."));
		return true;
	}
	int32 CreatedIndex = 0;
	bool bSawSkeletalMesh = false;
	for (UObject* Object : ImportedObjects)
	{
		if (!Object)
		{
			continue;
		}
		EEpicodeEntityType EntityType;
		FString AssetField;
		if (Cast<USkeletalMesh>(Object))
		{
			bSawSkeletalMesh = true;
			EntityType = EEpicodeEntityType::Character;
			AssetField = TEXT("skeletalMesh");
		}
		else if (Cast<UStaticMesh>(Object))
		{
			EntityType = EEpicodeEntityType::Prop;
			AssetField = TEXT("staticMesh");
		}
		else
		{
			continue;
		}
		if (InRequest.Kind == EEpicodeImportKind::Character && EntityType != EEpicodeEntityType::Character)
		{
			continue;
		}
		if (InRequest.Kind == EEpicodeImportKind::Prop && EntityType != EEpicodeEntityType::Prop)
		{
			continue;
		}
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(AssetField, Object->GetPathName());
		FEpicodeTransform Transform = InRequest.Transform;
		Transform.Scale *= InRequest.Scale;
		Transform.Rotation += InRequest.RotationOffset;
		if (InRequest.bAutoPlace)
		{
			Transform.Location += FVector(static_cast<double>(CreatedIndex) * 100.0, 0.0, 0.0);
		}
		FString Name = InRequest.EntityName;
		if (Name.IsEmpty() || CreatedIndex > 0)
		{
			Name = FString::Printf(TEXT("%s_%s"), *FPaths::GetBaseFilename(InRequest.FilePath), *Object->GetName());
		}
		FGuid EntityId;
		FString Error;
		if (Scene->CreateEntity(EntityType, Name, FGuid(), Transform, Data, EntityId, Error))
		{
			OutResult.EntityIds.Add(EntityId.ToString(EGuidFormats::DigitsWithHyphens));
			++CreatedIndex;
		}
		else
		{
			OutResult.Warnings.Add(FString::Printf(TEXT("Imported asset but could not create entity: %s"), *Error));
		}
	}
	if (InRequest.Kind == EEpicodeImportKind::Character && !bSawSkeletalMesh)
	{
		OutResult.ErrorCode = TEXT("IMPORT_CHARACTER_NO_SKELETON");
		OutResult.ErrorMessage = TEXT("The imported asset has no skeleton or skeletal mesh. Import it as a prop instead.");
		return false;
	}
	return true;
}

TSharedPtr<FEpicodeImportJob> FEpicodeImportService::Submit(const FEpicodeImportRequest& InRequest, FString& OutError)
{
	FString ValidationError;
	if (!ValidateRequest(InRequest, ValidationError))
	{
		OutError = ValidationError;
		return nullptr;
	}
	Initialize();
	TSharedPtr<FEpicodeImportJob> Job = MakeShared<FEpicodeImportJob>();
	Job->Id = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	Job->Request = InRequest;
	Job->State = EEpicodeImportState::Queued;
	Job->Stage = TEXT("queued");
	{
		FScopeLock ScopeLock(&Lock);
		Jobs.Add(Job);
	}
	return Job;
}

TSharedPtr<FEpicodeImportJob> FEpicodeImportService::GetJob(const FString& InJobId) const
{
	FScopeLock ScopeLock(&Lock);
	for (const TSharedPtr<FEpicodeImportJob>& Job : Jobs)
	{
		if (Job.IsValid() && Job->Id == InJobId)
		{
			return Job;
		}
	}
	return nullptr;
}

TArray<TSharedPtr<FEpicodeImportJob>> FEpicodeImportService::GetJobs() const
{
	FScopeLock ScopeLock(&Lock);
	return Jobs;
}

bool FEpicodeImportService::Cancel(const FString& InJobId)
{
	FScopeLock ScopeLock(&Lock);
	for (const TSharedPtr<FEpicodeImportJob>& Job : Jobs)
	{
		if (Job.IsValid() && Job->Id == InJobId && (Job->State == EEpicodeImportState::Queued || Job->State == EEpicodeImportState::Running))
		{
			Job->State = EEpicodeImportState::Cancelled;
			Job->Stage = TEXT("cancelled");
			Job->FinishedAt = FDateTime::UtcNow();
			return true;
		}
	}
	return false;
}

bool FEpicodeImportService::Tick(float DeltaSeconds)
{
	TSharedPtr<FEpicodeImportJob> Job;
	{
		FScopeLock ScopeLock(&Lock);
		for (const TSharedPtr<FEpicodeImportJob>& Candidate : Jobs)
		{
			if (Candidate.IsValid() && Candidate->State == EEpicodeImportState::Queued)
			{
				Job = Candidate;
				Job->State = EEpicodeImportState::Running;
				Job->Stage = TEXT("importing");
				Job->Progress = 0.1f;
				break;
			}
		}
	}
	if (!Job.IsValid())
	{
		return true;
	}

	FEpicodeImportResult Result;
	ImportSynchronously(Job->Request, Result);

	{
		FScopeLock ScopeLock(&Lock);
		if (Job->State == EEpicodeImportState::Cancelled)
		{
			ScopeLock.Unlock();
			if (UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary())
			{
				for (const FString& EntityId : Result.EntityIds)
				{
					FGuid Id;
					if (FGuid::Parse(EntityId, Id))
					{
						TArray<FString> Removed;
						FString Error;
						Scene->DeleteEntity(Id, true, Removed, Error);
					}
				}
			}
			return true;
		}
		Job->Result = Result;
		Job->Progress = 1.0f;
		Job->FinishedAt = FDateTime::UtcNow();
		if (Result.bSuccess)
		{
			Job->State = EEpicodeImportState::Succeeded;
			Job->Stage = TEXT("done");
		}
		else
		{
			Job->State = EEpicodeImportState::Failed;
			Job->Stage = TEXT("failed");
		}
	}
	return true;
}
