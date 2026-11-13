#include "EpicodeStudioAPICommands.h"

#include "EpicodeCommand.h"
#include "EpicodeDirector.h"
#include "EpicodeNetworkSandboxBridge.h"
#include "EpicodeNetworkSettings.h"
#include "EpicodeNetworkSubsystem.h"
#include "EpicodeProductionDocument.h"
#include "EpicodeProductionLibrary.h"
#include "EpicodeProductionValidation.h"
#include "EpicodeReplayEngine.h"
#include "EpicodeReplayJob.h"
#include "EpicodeSandboxGameplay.h"
#include "EpicodeSandboxSession.h"
#include "EpicodeSandboxSettings.h"
#include "EpicodeSettings.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

namespace
{
	FEpicodeCommandResult Failure(const FString& InCode, const FString& InMessage)
	{
		return FEpicodeCommandResult::Failure(InCode, InMessage);
	}

	FEpicodeCommandResult FailureFromError(const FString& InError, const FString& InFallback = TEXT("INVALID_PARAMS"))
	{
		int32 Colon = INDEX_NONE;
		if (InError.FindChar(TEXT(':'), Colon))
		{
			return Failure(InError.Left(Colon).TrimStartAndEnd(), InError.Mid(Colon + 1).TrimStartAndEnd());
		}
		return Failure(InFallback, InError);
	}

	FString ProductionRoot()
	{
		const UEpicodeSandboxSettings* Settings = UEpicodeSandboxSettings::Get();
		return Settings ? Settings->GetProductionRootPath() : FString(TEXT("Saved/Epicode/Productions"));
	}

	FString TakeRoot()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Epicode"), TEXT("Takes"));
	}

	FEpicodeNetworkSandboxBridge* GetBridge()
	{
		UEpicodeNetworkSubsystem* Subsystem = UEpicodeNetworkSubsystem::Get();
		return Subsystem ? Subsystem->GetSandboxBridgeMutable() : nullptr;
	}

	FEpicodeCommandResult RequireBridge(FEpicodeNetworkSandboxBridge*& OutBridge)
	{
		OutBridge = GetBridge();
		if (!OutBridge || !OutBridge->IsActive())
		{
			return Failure(TEXT("SANDBOX_NOT_FOUND"), TEXT("No active sandbox session."));
		}
		return FEpicodeCommandResult::Success();
	}

	const FEpicodeSandboxParticipant* FindParticipant(FEpicodeNetworkSandboxBridge& Bridge, const FString& InActorId)
	{
		return InActorId.IsEmpty() ? nullptr : Bridge.GetWorld().FindParticipant(InActorId);
	}

	FEpicodeCommandResult RequireRole(FEpicodeNetworkSandboxBridge& Bridge, const FString& InActorId, bool bAllowPerformer, bool bAllowViewer = false)
	{
		const FEpicodeSandboxParticipant* Participant = FindParticipant(Bridge, InActorId);
		if (!Participant)
		{
			return Failure(TEXT("PARTICIPANT_NOT_FOUND"), FString::Printf(TEXT("Unknown sandbox participant: %s"), *InActorId));
		}
		if (Participant->Kind == EEpicodeSandboxParticipantKind::Viewer && !bAllowViewer)
		{
			return Failure(TEXT("VIEWER_READ_ONLY"), TEXT("Viewer cannot modify the sandbox."));
		}
		if (!bAllowPerformer && Participant->Kind != EEpicodeSandboxParticipantKind::Owner && Participant->Kind != EEpicodeSandboxParticipantKind::Director)
		{
			return Failure(TEXT("FORBIDDEN"), TEXT("Owner or director role is required."));
		}
		return FEpicodeCommandResult::Success();
	}

	FEpicodeCommandResult RequireAuthoring(const FString& InActorId)
	{
		FEpicodeNetworkSandboxBridge* Bridge = GetBridge();
		if (!Bridge || InActorId.IsEmpty())
		{
			return FEpicodeCommandResult::Success();
		}
		return RequireRole(*Bridge, InActorId, false);
	}

	EEpicodeSandboxParticipantKind ParticipantKind(const FString& InActorId)
	{
		if (FEpicodeNetworkSandboxBridge* Bridge = GetBridge())
		{
			if (const FEpicodeSandboxParticipant* Participant = FindParticipant(*Bridge, InActorId))
			{
				return Participant->Kind;
			}
		}
		return EEpicodeSandboxParticipantKind::Owner;
	}

	TArray<TSharedPtr<FJsonValue>> StringArray(const TArray<FString>& InValues)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		for (const FString& Value : InValues)
		{
			Result.Add(MakeShared<FJsonValueString>(Value));
		}
		return Result;
	}

	FEpicodeCommandResult LoadProduction(const TSharedPtr<FJsonObject>& Params, FEpicodeProductionDocument& OutDocument, FString& OutPath)
	{
		FString ProductionId;
		if (!Params.IsValid() || !Params->TryGetStringField(TEXT("productionId"), ProductionId) || ProductionId.IsEmpty())
		{
			return Failure(TEXT("INVALID_PARAMS"), TEXT("productionId is required."));
		}
		OutPath = FEpicodeProductionLibrary::GetDocumentPath(ProductionRoot(), ProductionId);
		FString Error;
		if (!FEpicodeProductionLibrary::Load(OutPath, OutDocument, Error))
		{
			return FailureFromError(Error, TEXT("PRODUCTION_NOT_FOUND"));
		}
		return FEpicodeCommandResult::Success();
	}

	FEpicodeCommandResult ParseRules(FEpicodeNetworkSandboxBridge& Bridge, FEpicodeSandboxRules& OutRules)
	{
		FString Error;
		if (!FEpicodeSandboxRules::Parse(Bridge.GetSession().GetProduction().WorldRules, OutRules, Error))
		{
			return FailureFromError(Error);
		}
		return FEpicodeCommandResult::Success();
	}

	FEpicodeCommandResult CreateGameplay(FEpicodeNetworkSandboxBridge& Bridge, FEpicodeSandboxRules& OutRules, FEpicodeSandboxGameplay& OutGameplay)
	{
		FEpicodeCommandResult Parsed = ParseRules(Bridge, OutRules);
		if (!Parsed.bSuccess)
		{
			return Parsed;
		}
		FEpicodeSandboxSession& Session = Bridge.GetMutableSession();
		FString Error;
		if (!OutGameplay.Initialize(OutRules, Session.GetMutableWorld(), Session.GetMutableRandomStreams(), Session.GetMutableEvents(), Session.GetConfig().TickRate, Error))
		{
			return FailureFromError(Error);
		}
		return FEpicodeCommandResult::Success();
	}

	TSharedPtr<FEpicodeDirectorController> GetDirector(FString& OutError)
	{
		static TMap<FString, TSharedPtr<FEpicodeDirectorController>> Directors;
		static FCriticalSection Lock;
		FEpicodeNetworkSandboxBridge* Bridge = GetBridge();
		if (!Bridge || !Bridge->IsActive())
		{
			OutError = TEXT("SANDBOX_NOT_FOUND: no active sandbox session");
			return nullptr;
		}
		const FString Key = Bridge->GetSession().GetConfig().SessionId;
		FScopeLock ScopeLock(&Lock);
		TSharedPtr<FEpicodeDirectorController>& Existing = Directors.FindOrAdd(Key);
		if (Existing.IsValid())
		{
			return Existing;
		}
		TSharedPtr<FEpicodeDirectorController> Controller = MakeShared<FEpicodeDirectorController>();
		FEpicodeSandboxSession& Session = Bridge->GetMutableSession();
		if (!Controller->Initialize(Session.GetProduction(), Session.GetMutableWorld(), &Session.GetMutableEvents(), OutError))
		{
			Directors.Remove(Key);
			return nullptr;
		}
		Existing = Controller;
		return Controller;
	}

	FEpicodeReplayJobQueue& ReplayQueue()
	{
		static FEpicodeReplayJobQueue Queue;
		static bool bInitialized = false;
		if (!bInitialized)
		{
			Queue.Initialize(1);
			bInitialized = true;
		}
		return Queue;
	}

	FEpicodeCommandResult DirectorResult(const FEpicodeDirectorCommandResult& InResult)
	{
		if (!InResult.bSuccess)
		{
			return FailureFromError(InResult.Error, TEXT("DIRECTOR_ERROR"));
		}
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("duplicate"), InResult.bDuplicate);
		return FEpicodeCommandResult::Success(Result);
	}

	TSharedPtr<FJsonObject> DirectorStatus(FEpicodeDirectorController& Controller)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("paused"), Controller.IsPaused());
		Result->SetNumberField(TEXT("timeScale"), Controller.GetTimeScale());
		Result->SetNumberField(TEXT("directorTime"), Controller.GetDirectorTime());
		Result->SetStringField(TEXT("cameraId"), Controller.GetCameraId());
		Result->SetStringField(TEXT("cameraPresetId"), Controller.GetCameraPresetId());
		Result->SetStringField(TEXT("beatId"), Controller.GetNarrative().CurrentBeatId);
		Result->SetStringField(TEXT("objectiveId"), Controller.GetNarrative().CurrentObjectiveId);
		Result->SetNumberField(TEXT("viewMode"), static_cast<int32>(Controller.GetViewMode()));
		Result->SetNumberField(TEXT("viewSequence"), static_cast<double>(Controller.GetViewSequence()));
		Result->SetArrayField(TEXT("messages"), StringArray(Controller.GetMessages()));
		TArray<TSharedPtr<FJsonValue>> Leases;
		for (const FEpicodeTakeoverLease& Lease : Controller.GetTakeoverLeases())
		{
			Leases.Add(MakeShared<FJsonValueObject>(Lease.ToJson()));
		}
		Result->SetArrayField(TEXT("takeovers"), Leases);
		Result->SetObjectField(TEXT("narrative"), Controller.GetNarrative().ToJson());
		return Result;
	}

	FEpicodeCommandResult RunDirectorCommand(const TSharedPtr<FJsonObject>& Params, const TFunction<FEpicodeDirectorCommandResult(FEpicodeDirectorController&, const FString&, const FString&, int64)>& InCommand)
	{
		FString ActorId;
		EpicodeJson::GetOptionalString(Params, TEXT("actorId"), ActorId, TEXT("director"));
		FEpicodeNetworkSandboxBridge* Bridge = nullptr;
		FEpicodeCommandResult Required = RequireBridge(Bridge);
		if (!Required.bSuccess)
		{
			return Required;
		}
		FEpicodeCommandResult Role = RequireRole(*Bridge, ActorId, false);
		if (!Role.bSuccess)
		{
			return Role;
		}
		FString Error;
		TSharedPtr<FEpicodeDirectorController> Controller = GetDirector(Error);
		if (!Controller.IsValid())
		{
			return FailureFromError(Error, TEXT("DIRECTOR_UNAVAILABLE"));
		}
		FString Key;
		EpicodeJson::GetOptionalString(Params, TEXT("idempotencyKey"), Key);
		if (Key.IsEmpty())
		{
			Key = TEXT("api-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		}
		return DirectorResult(InCommand(*Controller, ActorId, Key, Bridge->GetWorld().Tick));
	}
}

void FEpicodeStudioAPICommands::Register(FEpicodeCommandRegistry& Registry)
{
	Registry.Register(TEXT("production.list"), TEXT("List production packages."), [](const TSharedPtr<FJsonObject>&, const FEpicodeCommandContext&)
	{
		TArray<TSharedPtr<FJsonValue>> Items;
		for (const FEpicodeProductionLibraryEntry& Entry : FEpicodeProductionLibrary::Scan(ProductionRoot()))
		{
			Items.Add(MakeShared<FJsonValueObject>(Entry.ToJson()));
		}
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("productions"), Items);
		Result->SetNumberField(TEXT("count"), Items.Num());
		Result->SetStringField(TEXT("root"), ProductionRoot());
		return FEpicodeCommandResult::Success(Result);
	}, false);

	Registry.Register(TEXT("production.open"), TEXT("Load a production package."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FEpicodeProductionDocument Document;
		FString Path;
		FEpicodeCommandResult Loaded = LoadProduction(Params, Document, Path);
		if (!Loaded.bSuccess)
		{
			return Loaded;
		}
		TSharedPtr<FJsonObject> Result = Document.ToJson();
		Result->SetStringField(TEXT("documentPath"), Path);
		return FEpicodeCommandResult::Success(Result);
	}, false);

	Registry.Register(TEXT("production.validate"), TEXT("Validate a production document against a scene."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FEpicodeProductionDocument Document;
		const TSharedPtr<FJsonObject>* DocumentJson = nullptr;
		if (Params.IsValid() && Params->TryGetObjectField(TEXT("document"), DocumentJson) && DocumentJson && (*DocumentJson).IsValid())
		{
			FString Error;
			if (!FEpicodeProductionDocument::FromJson(*DocumentJson, Document, Error))
			{
				return FailureFromError(Error);
			}
		}
		else
		{
			FString Path;
			FEpicodeCommandResult Loaded = LoadProduction(Params, Document, Path);
			if (!Loaded.bSuccess)
			{
				return Loaded;
			}
		}
		FString SceneRoot;
		EpicodeJson::GetOptionalString(Params, TEXT("sceneRoot"), SceneRoot);
		if (SceneRoot.IsEmpty())
		{
			SceneRoot = UEpicodeSettings::Get()->GetSceneRootPath();
		}
		FEpicodeProductionValidationResult Report;
		const bool bValid = FEpicodeProductionValidator::ValidateBinding(Document, SceneRoot, Report);
		TSharedPtr<FJsonObject> Result = Report.ToJson();
		Result->SetStringField(TEXT("productionId"), Document.ProductionId);
		Result->SetStringField(TEXT("sceneRoot"), SceneRoot);
		if (!bValid)
		{
			return Failure(Report.GetFirstCode().IsEmpty() ? TEXT("PRODUCTION_INVALID") : Report.GetFirstCode(), Report.GetFirstError());
		}
		return FEpicodeCommandResult::Success(Result);
	}, false);

	Registry.Register(TEXT("production.create"), TEXT("Create and validate a production package."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString ActorId;
		EpicodeJson::GetOptionalString(Params, TEXT("actorId"), ActorId);
		FEpicodeCommandResult Role = RequireAuthoring(ActorId);
		if (!Role.bSuccess)
		{
			return Role;
		}
		FString SceneId;
		FString Name;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("sceneId"), SceneId, Error))
		{
			return Failure(TEXT("INVALID_PARAMS"), Error);
		}
		EpicodeJson::GetOptionalString(Params, TEXT("name"), Name, TEXT("Untitled Production"));
		FEpicodeProductionDocument Document = FEpicodeProductionDocument::CreateDefault(SceneId, Name);
		const TSharedPtr<FJsonObject>* DocumentJson = nullptr;
		if (Params->TryGetObjectField(TEXT("document"), DocumentJson) && DocumentJson && (*DocumentJson).IsValid())
		{
			if (!FEpicodeProductionDocument::FromJson(*DocumentJson, Document, Error))
			{
				return FailureFromError(Error);
			}
			Document.SceneId = SceneId;
			Document.Name = Name;
		}
		FString ProductionId;
		if (Params->TryGetStringField(TEXT("productionId"), ProductionId) && !ProductionId.IsEmpty())
		{
			Document.ProductionId = ProductionId;
		}
		if (!Document.Validate(Error))
		{
			return FailureFromError(Error);
		}
		FString Path;
		if (!FEpicodeProductionLibrary::Create(Document, ProductionRoot(), Path, Error))
		{
			return FailureFromError(Error, TEXT("PRODUCTION_CREATE_FAILED"));
		}
		TSharedPtr<FJsonObject> Result = Document.ToJson();
		Result->SetStringField(TEXT("documentPath"), Path);
		return FEpicodeCommandResult::Success(Result);
	});

	Registry.Register(TEXT("production.save"), TEXT("Save a production package."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString ActorId;
		EpicodeJson::GetOptionalString(Params, TEXT("actorId"), ActorId);
		FEpicodeCommandResult Role = RequireAuthoring(ActorId);
		if (!Role.bSuccess)
		{
			return Role;
		}
		FEpicodeProductionDocument Document;
		FString Path;
		FEpicodeCommandResult Loaded = LoadProduction(Params, Document, Path);
		if (!Loaded.bSuccess)
		{
			return Loaded;
		}
		const TSharedPtr<FJsonObject>* DocumentJson = nullptr;
		FString Error;
		if (Params->TryGetObjectField(TEXT("document"), DocumentJson) && DocumentJson && (*DocumentJson).IsValid())
		{
			FString ExistingId = Document.ProductionId;
			if (!FEpicodeProductionDocument::FromJson(*DocumentJson, Document, Error))
			{
				return FailureFromError(Error);
			}
			Document.ProductionId = ExistingId;
		}
		if (!Document.Validate(Error))
		{
			return FailureFromError(Error);
		}
		FString SavedPath;
		if (!FEpicodeProductionLibrary::Save(Document, ProductionRoot(), SavedPath, Error))
		{
			return FailureFromError(Error, TEXT("PRODUCTION_SAVE_FAILED"));
		}
		TSharedPtr<FJsonObject> Result = Document.ToJson();
		Result->SetStringField(TEXT("documentPath"), SavedPath);
		return FEpicodeCommandResult::Success(Result);
	});

	Registry.Register(TEXT("production.delete"), TEXT("Move a production package to trash."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString ActorId;
		EpicodeJson::GetOptionalString(Params, TEXT("actorId"), ActorId);
		FEpicodeCommandResult Role = RequireAuthoring(ActorId);
		if (!Role.bSuccess)
		{
			return Role;
		}
		FString ProductionId;
		if (!Params.IsValid() || !Params->TryGetStringField(TEXT("productionId"), ProductionId) || ProductionId.IsEmpty())
		{
			return Failure(TEXT("INVALID_PARAMS"), TEXT("productionId is required."));
		}
		FEpicodeProductionLibraryEntry Entry;
		FString Error;
		if (!FEpicodeProductionLibrary::Find(ProductionRoot(), ProductionId, Entry))
		{
			return Failure(TEXT("PRODUCTION_NOT_FOUND"), ProductionId);
		}
		FString TrashPath;
		if (!FEpicodeProductionLibrary::Delete(Entry, UEpicodeSandboxSettings::Get()->GetProductionTrashRootPath(), FString(), TrashPath, Error))
		{
			return FailureFromError(Error, TEXT("PRODUCTION_DELETE_FAILED"));
		}
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("productionId"), ProductionId);
		Result->SetStringField(TEXT("trashPath"), TrashPath);
		return FEpicodeCommandResult::Success(Result);
	});

	Registry.Register(TEXT("sandbox.state"), TEXT("Return the active sandbox state and digest."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FEpicodeNetworkSandboxBridge* Bridge = nullptr;
		FEpicodeCommandResult Required = RequireBridge(Bridge);
		if (!Required.bSuccess)
		{
			return Required;
		}
		const TSharedPtr<FJsonObject> Result = Bridge->GetWorld().ToJson();
		Result->SetStringField(TEXT("productionId"), Bridge->GetSession().GetProduction().ProductionId);
		Result->SetStringField(TEXT("digest"), Bridge->GetDigest());
		Result->SetNumberField(TEXT("eventSequence"), static_cast<double>(Bridge->GetEventSequence()));
		Result->SetNumberField(TEXT("tick"), static_cast<double>(Bridge->GetWorld().Tick));
		Result->SetBoolField(TEXT("replayable"), Bridge->IsReplayable());
		Result->SetBoolField(TEXT("legacy"), Bridge->IsLegacy());
		return FEpicodeCommandResult::Success(Result);
	}, false);

	Registry.Register(TEXT("sandbox.rules.get"), TEXT("Return production world rules for the active sandbox."), [](const TSharedPtr<FJsonObject>&, const FEpicodeCommandContext&)
	{
		FEpicodeNetworkSandboxBridge* Bridge = nullptr;
		FEpicodeCommandResult Required = RequireBridge(Bridge);
		if (!Required.bSuccess)
		{
			return Required;
		}
		return FEpicodeCommandResult::Success(Bridge->GetSession().GetProduction().WorldRules);
	}, false);

	Registry.Register(TEXT("sandbox.rules.update"), TEXT("Validate and save production world rules."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString ActorId;
		EpicodeJson::GetOptionalString(Params, TEXT("actorId"), ActorId);
		FEpicodeCommandResult Role = RequireAuthoring(ActorId);
		if (!Role.bSuccess)
		{
			return Role;
		}
		const TSharedPtr<FJsonObject>* RulesJson = nullptr;
		if (!Params->TryGetObjectField(TEXT("rules"), RulesJson) || !RulesJson || !(*RulesJson).IsValid())
		{
			return Failure(TEXT("INVALID_PARAMS"), TEXT("rules object is required."));
		}
		FEpicodeProductionDocument Document;
		FString Path;
		FEpicodeCommandResult Loaded = LoadProduction(Params, Document, Path);
		if (!Loaded.bSuccess)
		{
			return Loaded;
		}
		Document.WorldRules = *RulesJson;
		FString Error;
		if (!Document.Validate(Error))
		{
			return FailureFromError(Error);
		}
		FString SavedPath;
		if (!FEpicodeProductionLibrary::Save(Document, ProductionRoot(), SavedPath, Error))
		{
			return FailureFromError(Error, TEXT("PRODUCTION_SAVE_FAILED"));
		}
		return FEpicodeCommandResult::Success(Document.WorldRules);
	});

	Registry.Register(TEXT("sandbox.budget"), TEXT("Report sandbox budget usage."), [](const TSharedPtr<FJsonObject>&, const FEpicodeCommandContext&)
	{
		FEpicodeNetworkSandboxBridge* Bridge = nullptr;
		FEpicodeCommandResult Required = RequireBridge(Bridge);
		if (!Required.bSuccess)
		{
			return Required;
		}
		FEpicodeSandboxRules Rules;
		FEpicodeSandboxGameplay Gameplay;
		FEpicodeCommandResult Ready = CreateGameplay(*Bridge, Rules, Gameplay);
		if (!Ready.bSuccess)
		{
			return Ready;
		}
		TSharedPtr<FJsonObject> Result = Gameplay.GetBudgetReport();
		Result->SetNumberField(TEXT("entityCount"), Bridge->GetWorld().Entities.Num());
		return FEpicodeCommandResult::Success(Result);
	}, false);

	Registry.Register(TEXT("sandbox.interact"), TEXT("Execute a sandbox interaction as a participant."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString PlayerId;
		FString EntityId;
		FString Verb;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("playerId"), PlayerId, Error)
			|| !EpicodeJson::GetRequiredString(Params, TEXT("entityId"), EntityId, Error)
			|| !EpicodeJson::GetRequiredString(Params, TEXT("verb"), Verb, Error))
		{
			return Failure(TEXT("INVALID_PARAMS"), Error);
		}
		FEpicodeNetworkSandboxBridge* Bridge = nullptr;
		FEpicodeCommandResult Required = RequireBridge(Bridge);
		if (!Required.bSuccess)
		{
			return Required;
		}
		FEpicodeCommandResult Role = RequireRole(*Bridge, PlayerId, true);
		if (!Role.bSuccess)
		{
			return Role;
		}
		FEpicodeSandboxRules Rules;
		FEpicodeSandboxGameplay Gameplay;
		FEpicodeCommandResult Ready = CreateGameplay(*Bridge, Rules, Gameplay);
		if (!Ready.bSuccess)
		{
			return Ready;
		}
		if (!Gameplay.ExecuteInteraction(PlayerId, EntityId, Verb, Error))
		{
			return FailureFromError(Error);
		}
		return FEpicodeCommandResult::Success(Bridge->GetWorld().FindEntity(PlayerId) ? Bridge->GetWorld().FindEntity(PlayerId)->ToJson() : Bridge->GetWorld().ToJson());
	});

	Registry.Register(TEXT("sandbox.spawn"), TEXT("Spawn a sandbox entity as director or owner."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString ActorId;
		FString Error;
		EpicodeJson::GetOptionalString(Params, TEXT("actorId"), ActorId);
		FEpicodeNetworkSandboxBridge* Bridge = nullptr;
		FEpicodeCommandResult Required = RequireBridge(Bridge);
		if (!Required.bSuccess)
		{
			return Required;
		}
		FEpicodeCommandResult Role = RequireRole(*Bridge, ActorId, false);
		if (!Role.bSuccess)
		{
			return Role;
		}
		FEpicodeSandboxRules Rules;
		FEpicodeSandboxGameplay Gameplay;
		FEpicodeCommandResult Ready = CreateGameplay(*Bridge, Rules, Gameplay);
		if (!Ready.bSuccess)
		{
			return Ready;
		}
		if (!Gameplay.CanSpawn(1, Error))
		{
			return FailureFromError(Error, TEXT("SANDBOX_BUDGET_EXCEEDED"));
		}
		FEpicodeSandboxEntity Entity;
		FString EntityId;
		EpicodeJson::GetOptionalString(Params, TEXT("entityId"), EntityId, FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens));
		Entity.Id = EntityId;
		FString KindText;
		EpicodeJson::GetOptionalString(Params, TEXT("kind"), KindText, TEXT("prop"));
		if (!ParseSandboxEntityKind(KindText, Entity.Kind))
		{
			return Failure(TEXT("INVALID_PARAMS"), FString::Printf(TEXT("Unknown entity kind: %s"), *KindText));
		}
		EpicodeJson::GetOptionalString(Params, TEXT("archetypeId"), Entity.ArchetypeId);
		EpicodeJson::GetVector(Params, TEXT("location"), Entity.Location, Error);
		EpicodeJson::GetRotator(Params, TEXT("rotation"), Entity.Rotation, Error);
		Entity.Data = MakeShared<FJsonObject>();
		if (const TSharedPtr<FJsonObject>* Data = nullptr; Params->TryGetObjectField(TEXT("data"), Data) && Data)
		{
			Entity.Data = *Data;
		}
		if (!Bridge->GetMutableWorld().AddEntity(Entity, Error))
		{
			return FailureFromError(Error);
		}
		Bridge->GetMutableSession().GetMutableEvents().Append(Bridge->GetWorld().Tick, TEXT("sandbox.spawned"), ActorId, Entity.Data, Entity.Id);
		return FEpicodeCommandResult::Success(Entity.ToJson(), { Entity.Id });
	});

	Registry.Register(TEXT("sandbox.collect"), TEXT("Collect from a sandbox resource node."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString PlayerId;
		FString NodeId;
		double Amount = 1.0;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("playerId"), PlayerId, Error)
			|| !EpicodeJson::GetRequiredString(Params, TEXT("entityId"), NodeId, Error))
		{
			return Failure(TEXT("INVALID_PARAMS"), Error);
		}
		EpicodeJson::GetOptionalNumber(Params, TEXT("amount"), Amount, 1.0);
		FEpicodeNetworkSandboxBridge* Bridge = nullptr;
		FEpicodeCommandResult Required = RequireBridge(Bridge);
		if (!Required.bSuccess)
		{
			return Required;
		}
		FEpicodeCommandResult Role = RequireRole(*Bridge, PlayerId, true);
		if (!Role.bSuccess)
		{
			return Role;
		}
		FEpicodeSandboxRules Rules;
		FEpicodeSandboxGameplay Gameplay;
		FEpicodeCommandResult Ready = CreateGameplay(*Bridge, Rules, Gameplay);
		if (!Ready.bSuccess)
		{
			return Ready;
		}
		if (!Gameplay.Collect(PlayerId, NodeId, static_cast<int32>(Amount), Error))
		{
			return FailureFromError(Error);
		}
		return FEpicodeCommandResult::Success(Bridge->GetWorld().ToJson());
	});

	Registry.Register(TEXT("sandbox.craft"), TEXT("Craft an item from a sandbox recipe."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString PlayerId;
		FString RecipeId;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("playerId"), PlayerId, Error)
			|| !EpicodeJson::GetRequiredString(Params, TEXT("recipeId"), RecipeId, Error))
		{
			return Failure(TEXT("INVALID_PARAMS"), Error);
		}
		FEpicodeNetworkSandboxBridge* Bridge = nullptr;
		FEpicodeCommandResult Required = RequireBridge(Bridge);
		if (!Required.bSuccess)
		{
			return Required;
		}
		FEpicodeCommandResult Role = RequireRole(*Bridge, PlayerId, true);
		if (!Role.bSuccess)
		{
			return Role;
		}
		FEpicodeSandboxRules Rules;
		FEpicodeSandboxGameplay Gameplay;
		FEpicodeCommandResult Ready = CreateGameplay(*Bridge, Rules, Gameplay);
		if (!Ready.bSuccess)
		{
			return Ready;
		}
		if (!Gameplay.Craft(PlayerId, RecipeId, Error))
		{
			return FailureFromError(Error);
		}
		return FEpicodeCommandResult::Success(Bridge->GetWorld().ToJson());
	});

	Registry.Register(TEXT("director.status"), TEXT("Return director state."), [](const TSharedPtr<FJsonObject>&, const FEpicodeCommandContext&)
	{
		FString Error;
		TSharedPtr<FEpicodeDirectorController> Controller = GetDirector(Error);
		if (!Controller.IsValid())
		{
			return FailureFromError(Error, TEXT("DIRECTOR_UNAVAILABLE"));
		}
		return FEpicodeCommandResult::Success(DirectorStatus(*Controller));
	}, false);

	Registry.Register(TEXT("director.pause"), TEXT("Pause the sandbox timeline."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		return RunDirectorCommand(Params, [](FEpicodeDirectorController& C, const FString& A, const FString& K, int64 T) { return C.Pause(A, K, T); });
	});

	Registry.Register(TEXT("director.resume"), TEXT("Resume the sandbox timeline."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		return RunDirectorCommand(Params, [](FEpicodeDirectorController& C, const FString& A, const FString& K, int64 T) { return C.Resume(A, K, T); });
	});

	Registry.Register(TEXT("director.setTimeScale"), TEXT("Set sandbox time scale."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		double Scale = 1.0;
		FString Error;
		if (!Params.IsValid() || !Params->TryGetNumberField(TEXT("scale"), Scale))
		{
			return Failure(TEXT("INVALID_PARAMS"), TEXT("scale is required."));
		}
		return RunDirectorCommand(Params, [Scale](FEpicodeDirectorController& C, const FString& A, const FString& K, int64 T) { return C.SetTimeScale(A, K, Scale, T); });
	});

	Registry.Register(TEXT("director.acquireTakeover"), TEXT("Take control of a participant character."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString ParticipantId;
		FString Reason;
		FString EndCondition;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("participantId"), ParticipantId, Error))
		{
			return Failure(TEXT("INVALID_PARAMS"), Error);
		}
		EpicodeJson::GetOptionalString(Params, TEXT("reason"), Reason);
		EpicodeJson::GetOptionalString(Params, TEXT("endCondition"), EndCondition);
		return RunDirectorCommand(Params, [ParticipantId, Reason, EndCondition](FEpicodeDirectorController& C, const FString& A, const FString& K, int64 T) { return C.AcquireTakeover(A, K, ParticipantId, Reason, EndCondition, T); });
	});

	Registry.Register(TEXT("director.releaseTakeover"), TEXT("Release a character takeover."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString ParticipantId;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("participantId"), ParticipantId, Error))
		{
			return Failure(TEXT("INVALID_PARAMS"), Error);
		}
		return RunDirectorCommand(Params, [ParticipantId](FEpicodeDirectorController& C, const FString& A, const FString& K, int64 T) { return C.ReleaseTakeover(A, K, ParticipantId, T); });
	});

	Registry.Register(TEXT("director.setObjective"), TEXT("Set the active narrative objective."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString ObjectiveId;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("objectiveId"), ObjectiveId, Error))
		{
			return Failure(TEXT("INVALID_PARAMS"), Error);
		}
		return RunDirectorCommand(Params, [ObjectiveId](FEpicodeDirectorController& C, const FString& A, const FString& K, int64 T) { return C.SetObjective(A, K, ObjectiveId, T); });
	});

	Registry.Register(TEXT("director.enterBeat"), TEXT("Enter a narrative beat."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString BeatId;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("beatId"), BeatId, Error))
		{
			return Failure(TEXT("INVALID_PARAMS"), Error);
		}
		return RunDirectorCommand(Params, [BeatId](FEpicodeDirectorController& C, const FString& A, const FString& K, int64 T) { return C.EnterBeat(A, K, BeatId, T); });
	});

	Registry.Register(TEXT("director.setView"), TEXT("Switch director view between live and ghost."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString ActorId;
		EpicodeJson::GetOptionalString(Params, TEXT("actorId"), ActorId, TEXT("director"));
		FEpicodeNetworkSandboxBridge* Bridge = nullptr;
		FEpicodeCommandResult Required = RequireBridge(Bridge);
		if (!Required.bSuccess)
		{
			return Required;
		}
		FEpicodeCommandResult Role = RequireRole(*Bridge, ActorId, false);
		if (!Role.bSuccess)
		{
			return Role;
		}
		FString Error;
		TSharedPtr<FEpicodeDirectorController> Controller = GetDirector(Error);
		if (!Controller.IsValid())
		{
			return FailureFromError(Error, TEXT("DIRECTOR_UNAVAILABLE"));
		}
		double Mode = 0.0;
		EpicodeJson::GetOptionalNumber(Params, TEXT("mode"), Mode, 0.0);
		FString ReplayRoot;
		double Sequence = 0.0;
		EpicodeJson::GetOptionalString(Params, TEXT("replayRoot"), ReplayRoot);
		EpicodeJson::GetOptionalNumber(Params, TEXT("sequence"), Sequence, 0.0);
		FString Key;
		EpicodeJson::GetOptionalString(Params, TEXT("idempotencyKey"), Key);
		if (Key.IsEmpty())
		{
			Key = TEXT("api-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		}
		return DirectorResult(Controller->SetView(ActorId, Key, static_cast<EEpicodeDirectorViewMode>(static_cast<int32>(Mode)), ReplayRoot, static_cast<int64>(Sequence)));
	});

	Registry.Register(TEXT("replay.ghost"), TEXT("Open a replay ghost and page its events."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString Root;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("root"), Root, Error))
		{
			return Failure(TEXT("INVALID_PARAMS"), Error);
		}
		FEpicodeReplayGhost Ghost;
		if (!Ghost.Open(Root, Error))
		{
			return FailureFromError(Error, TEXT("REPLAY_HISTORY_UNAVAILABLE"));
		}
		double Start = 0.0;
		double Count = 50.0;
		EpicodeJson::GetOptionalNumber(Params, TEXT("startSequence"), Start, static_cast<double>(Ghost.GetFirstSequence()));
		EpicodeJson::GetOptionalNumber(Params, TEXT("count"), Count, 50.0);
		FEpicodeReplayGhostPage Page;
		if (!Ghost.GetPage(static_cast<int64>(Start), FMath::Clamp(static_cast<int32>(Count), 1, 500), Page, Error))
		{
			return FailureFromError(Error, TEXT("REPLAY_HISTORY_UNAVAILABLE"));
		}
		TArray<TSharedPtr<FJsonValue>> Entries;
		for (const FEpicodeReplayJournalEntry& Entry : Page.Entries)
		{
			Entries.Add(MakeShared<FJsonValueObject>(Entry.ToJson()));
		}
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("root"), Root);
		Result->SetNumberField(TEXT("firstSequence"), static_cast<double>(Ghost.GetFirstSequence()));
		Result->SetNumberField(TEXT("lastSequence"), static_cast<double>(Ghost.GetLastSequence()));
		Result->SetNumberField(TEXT("startSequence"), static_cast<double>(Page.StartSequence));
		Result->SetArrayField(TEXT("events"), Entries);
		return FEpicodeCommandResult::Success(Result);
	}, false);

	Registry.Register(TEXT("replay.branch"), TEXT("Create an isolated branch from a replay sequence."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString ParentRoot;
		FString BranchRoot;
		FString BranchId;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("parentRoot"), ParentRoot, Error)
			|| !EpicodeJson::GetRequiredString(Params, TEXT("branchRoot"), BranchRoot, Error)
			|| !EpicodeJson::GetRequiredString(Params, TEXT("branchId"), BranchId, Error))
		{
			return Failure(TEXT("INVALID_PARAMS"), Error);
		}
		FString ActorId;
		EpicodeJson::GetOptionalString(Params, TEXT("actorId"), ActorId);
		const EEpicodeSandboxParticipantKind Kind = ParticipantKind(ActorId);
		if (!FEpicodeReplayState::CanReplay(Kind, Error))
		{
			return FailureFromError(Error, TEXT("VIEWER_READ_ONLY"));
		}
		double Fork = 0.0;
		EpicodeJson::GetOptionalNumber(Params, TEXT("forkSequence"), Fork, 0.0);
		if (!FEpicodeReplayBranch::Create(ParentRoot, BranchRoot, BranchId, static_cast<int64>(Fork), ActorId.IsEmpty() ? TEXT("api") : ActorId, Kind, Error))
		{
			return FailureFromError(Error, TEXT("REPLAY_BRANCH_FAILED"));
		}
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("branchId"), BranchId);
		Result->SetStringField(TEXT("branchRoot"), BranchRoot);
		Result->SetStringField(TEXT("metadataPath"), FEpicodeReplayBranch::GetBranchMetadataPath(BranchRoot));
		return FEpicodeCommandResult::Success(Result);
	});

	Registry.Register(TEXT("replay.job"), TEXT("Submit and run a deterministic replay job."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FEpicodeReplayJobQueue& Queue = ReplayQueue();
		FString Root;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("root"), Root, Error))
		{
			return Failure(TEXT("INVALID_PARAMS"), Error);
		}
		double Target = 0.0;
		EpicodeJson::GetOptionalNumber(Params, TEXT("targetSequence"), Target, 0.0);
		FString ActorId;
		EpicodeJson::GetOptionalString(Params, TEXT("actorId"), ActorId);
		const EEpicodeSandboxParticipantKind Kind = ParticipantKind(ActorId);
		FEpicodeReplayResimOptions Options;
		Params->TryGetBoolField(TEXT("allowInexact"), Options.bAllowInexact);
		EpicodeJson::GetOptionalString(Params, TEXT("expectedContentDigest"), Options.ExpectedContentDigest);
		const FString JobId = Queue.Submit(Root, static_cast<int64>(Target), Options, ActorId.IsEmpty() ? TEXT("api") : ActorId, Kind, Error);
		if (JobId.IsEmpty())
		{
			return FailureFromError(Error, TEXT("REPLAY_JOB_FAILED"));
		}
		Queue.StartQueued(Error);
		Queue.RunToCompletion(JobId, Error);
		const FEpicodeReplayJobStatus* Status = Queue.FindJob(JobId);
		if (!Status)
		{
			return Failure(TEXT("REPLAY_JOB_NOT_FOUND"), JobId);
		}
		return FEpicodeCommandResult::Success(Status->ToJson());
	});

	Registry.Register(TEXT("replay.cancel"), TEXT("Cancel a queued or running replay job."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString JobId;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("jobId"), JobId, Error))
		{
			return Failure(TEXT("INVALID_PARAMS"), Error);
		}
		FEpicodeReplayJobQueue& Queue = ReplayQueue();
		if (!Queue.FindJob(JobId))
		{
			return Failure(TEXT("REPLAY_JOB_NOT_FOUND"), JobId);
		}
		if (!Queue.Cancel(JobId, Error))
		{
			return FailureFromError(Error, TEXT("REPLAY_JOB_NOT_CANCELLABLE"));
		}
		return FEpicodeCommandResult::Success(Queue.FindJob(JobId)->ToJson());
	});

	Registry.Register(TEXT("take.list"), TEXT("List versions of a take."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString TakeId;
		FString Root;
		EpicodeJson::GetOptionalString(Params, TEXT("takeId"), TakeId);
		EpicodeJson::GetOptionalString(Params, TEXT("root"), Root, TakeRoot());
		TArray<TSharedPtr<FJsonValue>> Versions;
		for (const int32 Version : FEpicodeTakeLibrary::ListVersions(Root, TakeId))
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetNumberField(TEXT("version"), Version);
			Entry->SetStringField(TEXT("path"), FEpicodeTakeLibrary::GetTakePath(Root, TakeId, Version));
			Versions.Add(MakeShared<FJsonValueObject>(Entry));
		}
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("takeId"), TakeId);
		Result->SetStringField(TEXT("root"), Root);
		Result->SetArrayField(TEXT("versions"), Versions);
		return FEpicodeCommandResult::Success(Result);
	}, false);

	Registry.Register(TEXT("take.get"), TEXT("Load a take version."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString TakeId;
		FString Root;
		double Version = 0.0;
		EpicodeJson::GetOptionalString(Params, TEXT("takeId"), TakeId);
		EpicodeJson::GetOptionalString(Params, TEXT("root"), Root, TakeRoot());
		EpicodeJson::GetOptionalNumber(Params, TEXT("version"), Version, 0.0);
		FEpicodeDirectorTake Take;
		FString Error;
		if (!FEpicodeTakeLibrary::Load(Root, TakeId, static_cast<int32>(Version), Take, Error))
		{
			return FailureFromError(Error, TEXT("TAKE_NOT_FOUND"));
		}
		return FEpicodeCommandResult::Success(Take.ToJson());
	}, false);

	Registry.Register(TEXT("take.create"), TEXT("Create an immutable take version."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		const TSharedPtr<FJsonObject>* TakeJson = nullptr;
		if (!Params->TryGetObjectField(TEXT("take"), TakeJson) || !TakeJson || !(*TakeJson).IsValid())
		{
			return Failure(TEXT("INVALID_PARAMS"), TEXT("take object is required."));
		}
		FEpicodeDirectorTake Take;
		FString Error;
		if (!FEpicodeDirectorTake::FromJson(*TakeJson, Take, Error))
		{
			return FailureFromError(Error);
		}
		FString Root;
		EpicodeJson::GetOptionalString(Params, TEXT("root"), Root, TakeRoot());
		if (!FEpicodeTakeLibrary::Save(Root, Take, Error))
		{
			return FailureFromError(Error, TEXT("TAKE_SAVE_FAILED"));
		}
		return FEpicodeCommandResult::Success(Take.ToJson(), { Take.Id });
	});

	Registry.Register(TEXT("take.revise"), TEXT("Create a new immutable version of an existing take."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext&)
	{
		FString TakeId;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("takeId"), TakeId, Error))
		{
			return Failure(TEXT("INVALID_PARAMS"), Error);
		}
		FString Root;
		EpicodeJson::GetOptionalString(Params, TEXT("root"), Root, TakeRoot());
		FEpicodeDirectorTake Existing;
		if (!FEpicodeTakeLibrary::LoadLatest(Root, TakeId, Existing, Error))
		{
			return FailureFromError(Error, TEXT("TAKE_NOT_FOUND"));
		}
		const TSharedPtr<FJsonObject>* Patch = nullptr;
		if (Params->TryGetObjectField(TEXT("take"), Patch) && Patch && (*Patch).IsValid())
		{
			FEpicodeDirectorTake Draft;
			if (!FEpicodeDirectorTake::FromJson(*Patch, Draft, Error))
			{
				return FailureFromError(Error);
			}
			Draft.Id = TakeId;
			Draft.Version = Existing.Version + 1;
			Existing = Draft;
		}
		else
		{
			Existing.Version = Existing.Version + 1;
		}
		if (!FEpicodeTakeLibrary::Save(Root, Existing, Error))
		{
			return FailureFromError(Error, TEXT("TAKE_SAVE_FAILED"));
		}
		return FEpicodeCommandResult::Success(Existing.ToJson(), { Existing.Id });
	});
}
