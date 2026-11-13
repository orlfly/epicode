#include "EpicodeSceneCommands.h"

#include "Animation/AnimSequenceBase.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "EpicodeCommand.h"
#include "EpicodeCoreModule.h"
#include "EpicodeDiagnostics.h"
#include "EpicodeEngineSubsystem.h"
#include "EpicodeEntityActors.h"
#include "EpicodeEnvironmentController.h"
#include "EpicodeEventBus.h"
#include "EpicodeSceneSubsystem.h"
#include "EpicodeTypes.h"
#include "Materials/MaterialInterface.h"

namespace
{
	UEpicodeSceneSubsystem* ResolveScene(const FEpicodeCommandContext& Context)
	{
		UWorld* World = Context.World ? Context.World : UEpicodeSceneSubsystem::GetPrimaryWorld();
		return UEpicodeSceneSubsystem::Get(World);
	}

	FEpicodeCommandResult SceneRequired()
	{
		return FEpicodeCommandResult::Failure(TEXT("NO_SCENE"), TEXT("No scene is open."));
	}

	FEpicodeCommandResult FailureFrom(const FString& Code, const FString& Message)
	{
		return FEpicodeCommandResult::Failure(Code, Message);
	}

	FEpicodeSceneDocument CloneDocument(const FEpicodeSceneDocument& Source)
	{
		FEpicodeSceneDocument Copy;
		FString Error;
		if (!FEpicodeSceneDocument::FromJson(Source.ToJson(), Copy, Error))
		{
			Copy = Source;
		}
		Copy.Revision = Source.Revision;
		return Copy;
	}

	FEpicodeCommandResult RunMutation(UEpicodeSceneSubsystem* Scene, const FEpicodeCommandContext& Context, const FString& Description, TFunction<FEpicodeCommandResult()> Fn)
	{
		const FEpicodeSceneDocument Backup = Scene ? CloneDocument(Scene->GetDocument()) : FEpicodeSceneDocument();
		IEpicodeTransactionSink* Sink = Scene ? Scene->GetTransactionSink() : nullptr;
		if (Sink && Context.bUndoable)
		{
			Sink->BeginTransaction(Description);
		}
		FEpicodeCommandResult Result = Fn();
		if (Sink && Context.bUndoable)
		{
			if (Result.bSuccess)
			{
				Sink->CommitTransaction();
			}
			else
			{
				Sink->CancelTransaction();
			}
		}
		if (Scene && !Result.bSuccess)
		{
			Scene->RestoreDocument(Backup);
		}
		return Result;
	}

	bool GetGuid(const TSharedPtr<FJsonObject>& Params, const TCHAR* Field, FGuid& OutGuid, FString& OutError)
	{
		FString Value;
		if (!EpicodeJson::GetRequiredString(Params, Field, Value, OutError))
		{
			return false;
		}
		if (!FGuid::Parse(Value, OutGuid))
		{
			OutError = FString::Printf(TEXT("%s must be a GUID"), Field);
			return false;
		}
		return true;
	}

	bool GetOptionalGuid(const TSharedPtr<FJsonObject>& Params, const TCHAR* Field, FGuid& OutGuid)
	{
		FString Value;
		if (!EpicodeJson::GetOptionalString(Params, Field, Value) || Value.IsEmpty())
		{
			OutGuid.Invalidate();
			return true;
		}
		return FGuid::Parse(Value, OutGuid);
	}

	bool GetTransform(const TSharedPtr<FJsonObject>& Params, FEpicodeTransform& OutTransform, FString& OutError)
	{
		const TSharedPtr<FJsonObject>* TransformObject = nullptr;
		if (Params.IsValid() && Params->TryGetObjectField(TEXT("transform"), TransformObject) && TransformObject)
		{
			return FEpicodeTransform::FromJson(*TransformObject, OutTransform, OutError);
		}
		if (Params.IsValid() && (Params->HasField(TEXT("location")) || Params->HasField(TEXT("rotation")) || Params->HasField(TEXT("scale"))))
		{
			if (!EpicodeJson::GetVector(Params, TEXT("location"), OutTransform.Location, OutError)
				|| !EpicodeJson::GetRotator(Params, TEXT("rotation"), OutTransform.Rotation, OutError)
				|| !EpicodeJson::GetVector(Params, TEXT("scale"), OutTransform.Scale, OutError))
			{
				return false;
			}
			return OutTransform.IsFinite();
		}
		OutTransform = FEpicodeTransform();
		return true;
	}

	TSharedPtr<FJsonObject> MakeEntityResult(UEpicodeSceneSubsystem* Scene, const FEpicodeEntityRecord& Entity)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetObjectField(TEXT("entity"), Entity.ToJson());
		Result->SetStringField(TEXT("sceneId"), Scene->GetCurrentSceneId());
		Result->SetNumberField(TEXT("version"), static_cast<double>(Scene->GetDocument().Revision));
		return Result;
	}

	TSharedPtr<FJsonObject> MakeSceneResult(UEpicodeSceneSubsystem* Scene)
	{
		TSharedPtr<const FEpicodeSceneSnapshot> Snapshot = Scene->GetSnapshot();
		if (!Snapshot.IsValid())
		{
			Snapshot = Scene->BuildSnapshot();
		}
		return Snapshot->ToJson();
	}

	FEpicodeCommandResult EntityMutationResult(UEpicodeSceneSubsystem* Scene, const FGuid& EntityId)
	{
		const FEpicodeEntityRecord* Entity = Scene->FindEntity(EntityId);
		if (!Entity)
		{
			return FailureFrom(TEXT("ENTITY_NOT_FOUND"), TEXT("Entity was not found after the operation."));
		}
		return FEpicodeCommandResult::Success(MakeEntityResult(Scene, *Entity), { EntityId.ToString(EGuidFormats::DigitsWithHyphens) });
	}

	TSharedPtr<FJsonObject> GetEntityData(const TSharedPtr<FJsonObject>& Params, EEpicodeEntityType Type)
	{
		const TSharedPtr<FJsonObject>* DataObject = nullptr;
		if (Params.IsValid() && Params->TryGetObjectField(TEXT("data"), DataObject) && DataObject)
		{
			return *DataObject;
		}
		const FString Key = LexToString(Type).ToLower();
		if (Params.IsValid() && Params->TryGetObjectField(Key, DataObject) && DataObject)
		{
			return *DataObject;
		}
		return MakeShared<FJsonObject>();
	}

	void CopyFields(const TSharedPtr<FJsonObject>& Source, const TSharedPtr<FJsonObject>& Target)
	{
		if (!Source.IsValid() || !Target.IsValid())
		{
			return;
		}
		for (const auto& Pair : Source->Values)
		{
			const FString Key = FString(*Pair.Key);
			if (Key != TEXT("id") && Key != TEXT("data"))
			{
				Target->SetField(Key, Pair.Value);
			}
		}
	}

	void AddMaterialOverride(const TSharedPtr<FJsonObject>& Data, int32 Slot, const FString& MaterialPath)
	{
		if (!Data.IsValid())
		{
			return;
		}
		TArray<TSharedPtr<FJsonValue>> Values = EpicodeJson::GetOptionalArray(Data, TEXT("materialOverrides"));
		for (TSharedPtr<FJsonValue>& Value : Values)
		{
			TSharedPtr<FJsonObject> Entry = Value->AsObject();
			if (Entry.IsValid())
			{
				int32 ExistingSlot = INDEX_NONE;
				Entry->TryGetNumberField(TEXT("slot"), ExistingSlot);
				if (ExistingSlot == Slot)
				{
					Entry->SetStringField(TEXT("material"), MaterialPath);
					Data->SetArrayField(TEXT("materialOverrides"), Values);
					return;
				}
			}
		}
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("slot"), Slot);
		Entry->SetStringField(TEXT("material"), MaterialPath);
		Values.Add(MakeShared<FJsonValueObject>(Entry));
		Data->SetArrayField(TEXT("materialOverrides"), Values);
	}

	void RemoveMaterialOverride(const TSharedPtr<FJsonObject>& Data, int32 Slot)
	{
		if (!Data.IsValid())
		{
			return;
		}
		TArray<TSharedPtr<FJsonValue>> Values = EpicodeJson::GetOptionalArray(Data, TEXT("materialOverrides"));
		Values.RemoveAll([Slot](const TSharedPtr<FJsonValue>& Value)
		{
			const TSharedPtr<FJsonObject> Entry = Value->AsObject();
			int32 ExistingSlot = INDEX_NONE;
			return Entry.IsValid() && Entry->TryGetNumberField(TEXT("slot"), ExistingSlot) && ExistingSlot == Slot;
		});
		Data->SetArrayField(TEXT("materialOverrides"), Values);
	}

	FEpicodeCommandResult CreateCharacterSpawn(const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context, bool bCreateAlias)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		if (!Scene || !Scene->HasScene())
		{
			return SceneRequired();
		}
		FString MeshPath;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("skeletalMesh"), MeshPath, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error);
		}
		if (!LoadObject<USkeletalMesh>(nullptr, *MeshPath))
		{
			return FailureFrom(TEXT("INVALID_ASSET"), FString::Printf(TEXT("Skeletal mesh not found: %s"), *MeshPath));
		}
		FString Name;
		EpicodeJson::GetOptionalString(Params, TEXT("name"), Name, TEXT("Character"));
		FEpicodeTransform Transform;
		if (!GetTransform(Params, Transform, Error))
		{
			return FailureFrom(TEXT("INVALID_TRANSFORM"), Error);
		}
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("skeletalMesh"), MeshPath);
		FString Animation;
		if (EpicodeJson::GetOptionalString(Params, TEXT("animation"), Animation) && !Animation.IsEmpty())
		{
			if (!LoadObject<UAnimationAsset>(nullptr, *Animation))
			{
				return FailureFrom(TEXT("INVALID_ASSET"), FString::Printf(TEXT("Animation not found: %s"), *Animation));
			}
			Data->SetStringField(TEXT("animation"), Animation);
		}
		bool bLoop = true;
		EpicodeJson::GetOptionalBool(Params, TEXT("loop"), bLoop, true);
		Data->SetBoolField(TEXT("loop"), bLoop);
		double Rate = 1.0;
		EpicodeJson::GetOptionalNumber(Params, TEXT("playRate"), Rate, 1.0);
		Data->SetNumberField(TEXT("playRate"), Rate);
		FGuid Id;
		return RunMutation(Scene, Context, bCreateAlias ? TEXT("character.spawn") : TEXT("character.spawn"), [&]()
		{
			if (!Scene->CreateEntity(EEpicodeEntityType::Character, Name, FGuid(), Transform, Data, Id, Error))
			{
				return FailureFrom(TEXT("CREATE_FAILED"), Error);
			}
			return EntityMutationResult(Scene, Id);
		});
	}

	FEpicodeCommandResult CreatePropSpawn(const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		if (!Scene || !Scene->HasScene())
		{
			return SceneRequired();
		}
		FString MeshPath;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("staticMesh"), MeshPath, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error);
		}
		if (!LoadObject<UStaticMesh>(nullptr, *MeshPath))
		{
			return FailureFrom(TEXT("INVALID_ASSET"), FString::Printf(TEXT("Static mesh not found: %s"), *MeshPath));
		}
		FString Name;
		EpicodeJson::GetOptionalString(Params, TEXT("name"), Name, TEXT("Prop"));
		FEpicodeTransform Transform;
		if (!GetTransform(Params, Transform, Error))
		{
			return FailureFrom(TEXT("INVALID_TRANSFORM"), Error);
		}
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("staticMesh"), MeshPath);
		bool bSimulate = false;
		EpicodeJson::GetOptionalBool(Params, TEXT("simulatePhysics"), bSimulate, false);
		Data->SetBoolField(TEXT("simulatePhysics"), bSimulate);
		FString CollisionProfile;
		if (EpicodeJson::GetOptionalString(Params, TEXT("collisionProfile"), CollisionProfile) && !CollisionProfile.IsEmpty())
		{
			Data->SetStringField(TEXT("collisionProfile"), CollisionProfile);
		}
		const TArray<TSharedPtr<FJsonValue>> Overrides = EpicodeJson::GetOptionalArray(Params, TEXT("materialOverrides"));
		if (Overrides.Num() > 0)
		{
			Data->SetArrayField(TEXT("materialOverrides"), Overrides);
		}
		FGuid Id;
		return RunMutation(Scene, Context, TEXT("prop.spawn"), [&]()
		{
			if (!Scene->CreateEntity(EEpicodeEntityType::Prop, Name, FGuid(), Transform, Data, Id, Error))
			{
				return FailureFrom(TEXT("CREATE_FAILED"), Error);
			}
			return EntityMutationResult(Scene, Id);
		});
	}

	FEpicodeCommandResult CreateLight(const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		if (!Scene || !Scene->HasScene())
		{
			return SceneRequired();
		}
		FString LightType;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("lightType"), LightType, Error) && !EpicodeJson::GetRequiredString(Params, TEXT("type"), LightType, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), TEXT("lightType is required."));
		}
		if (!LightType.Equals(TEXT("directional"), ESearchCase::IgnoreCase)
			&& !LightType.Equals(TEXT("point"), ESearchCase::IgnoreCase)
			&& !LightType.Equals(TEXT("spot"), ESearchCase::IgnoreCase)
			&& !LightType.Equals(TEXT("rect"), ESearchCase::IgnoreCase))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), TEXT("lightType must be directional, point, spot or rect."));
		}
		double Inner = 0.0;
		double Outer = 0.0;
		EpicodeJson::GetOptionalNumber(Params, TEXT("innerConeAngle"), Inner, 0.0);
		EpicodeJson::GetOptionalNumber(Params, TEXT("outerConeAngle"), Outer, 0.0);
		if (LightType.Equals(TEXT("spot"), ESearchCase::IgnoreCase) && Inner > Outer)
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), TEXT("innerConeAngle must not exceed outerConeAngle."));
		}
		FString Name;
		EpicodeJson::GetOptionalString(Params, TEXT("name"), Name, TEXT("Light"));
		FEpicodeTransform Transform;
		if (!GetTransform(Params, Transform, Error))
		{
			return FailureFrom(TEXT("INVALID_TRANSFORM"), Error);
		}
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("lightType"), LightType.ToLower());
		CopyFields(Params, Data);
		Data->SetStringField(TEXT("lightType"), LightType.ToLower());
		FGuid Id;
		return RunMutation(Scene, Context, TEXT("light.create"), [&]()
		{
			if (!Scene->CreateEntity(EEpicodeEntityType::Light, Name, FGuid(), Transform, Data, Id, Error))
			{
				return FailureFrom(TEXT("CREATE_FAILED"), Error);
			}
			return EntityMutationResult(Scene, Id);
		});
	}

	FEpicodeCommandResult CreateCamera(const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		if (!Scene || !Scene->HasScene())
		{
			return SceneRequired();
		}
		FString Name;
		EpicodeJson::GetOptionalString(Params, TEXT("name"), Name, TEXT("Camera"));
		FEpicodeTransform Transform;
		FString Error;
		if (!GetTransform(Params, Transform, Error))
		{
			return FailureFrom(TEXT("INVALID_TRANSFORM"), Error);
		}
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		CopyFields(Params, Data);
		double FOV = 35.0;
		EpicodeJson::GetOptionalNumber(Params, TEXT("fieldOfView"), FOV, 35.0);
		Data->SetNumberField(TEXT("fieldOfView"), FOV);
		FGuid Id;
		return RunMutation(Scene, Context, TEXT("camera.create"), [&]()
		{
			if (!Scene->CreateEntity(EEpicodeEntityType::Camera, Name, FGuid(), Transform, Data, Id, Error))
			{
				return FailureFrom(TEXT("CREATE_FAILED"), Error);
			}
			return EntityMutationResult(Scene, Id);
		});
	}

	TArray<TSharedPtr<FJsonValue>> EntitiesOfType(UEpicodeSceneSubsystem* Scene, EEpicodeEntityType Type)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FEpicodeEntityRecord& Entity : Scene->GetDocument().Entities)
		{
			if (Entity.Type == Type)
			{
				Values.Add(MakeShared<FJsonValueObject>(Entity.ToJson()));
			}
		}
		return Values;
	}
}

void FEpicodeSceneCommands::Register(FEpicodeCommandRegistry& Registry)
{
	Registry.Register(TEXT("system.health"), TEXT("Return engine, API, scene and plugin health."), [](const TSharedPtr<FJsonObject>&, const FEpicodeCommandContext&)
	{
		const FEpicodeDiagnosticsResult Diagnostics = FEpicodeDiagnostics::Run();
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("status"), Diagnostics.bOk ? TEXT("ok") : TEXT("degraded"));
		Result->SetBoolField(TEXT("ok"), Diagnostics.bOk);
		Result->SetBoolField(TEXT("degraded"), Diagnostics.bDegraded);
		Result->SetStringField(TEXT("engineVersion"), Diagnostics.EngineVersion);
		Result->SetStringField(TEXT("projectVersion"), Diagnostics.ProjectVersion);
		Result->SetStringField(TEXT("renderBackend"), Diagnostics.RenderBackend);
		Result->SetStringField(TEXT("gpuAdapter"), Diagnostics.GpuAdapter);
		Result->SetNumberField(TEXT("gpuVendorId"), Diagnostics.GpuVendorId);
		Result->SetNumberField(TEXT("gpuDeviceId"), Diagnostics.GpuDeviceId);
		TArray<TSharedPtr<FJsonValue>> Messages;
		for (const FString& Message : Diagnostics.Messages)
		{
			Messages.Add(MakeShared<FJsonValueString>(Message));
		}
		Result->SetArrayField(TEXT("messages"), Messages);
		UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
		Result->SetBoolField(TEXT("sceneOpen"), Scene && Scene->HasScene());
		Result->SetStringField(TEXT("sceneId"), Scene ? Scene->GetCurrentSceneId() : FString());
		Result->SetStringField(TEXT("sceneName"), Scene ? Scene->GetCurrentSceneName() : FString());
		Result->SetBoolField(TEXT("sceneDirty"), Scene ? Scene->IsDirty() : false);
		if (UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get())
		{
			if (FEpicodeCommandRegistry* CommandRegistry = EngineSubsystem->GetCommandRegistry())
			{
				Result->SetNumberField(TEXT("commandCount"), CommandRegistry->GetMethodNames().Num());
			}
			if (FEpicodeEventBus* EventBus = EngineSubsystem->GetEventBus())
			{
				Result->SetNumberField(TEXT("eventSequence"), static_cast<double>(EventBus->GetLatestSequence()));
			}
		}
		if (const TSharedPtr<FJsonObject> Network = FEpicodeDiagnostics::GetNetworkStatus())
		{
			Result->SetObjectField(TEXT("network"), Network);
		}
		for (const TPair<FString, TSharedPtr<FJsonObject>>& Component : FEpicodeDiagnostics::GetComponentStatuses())
		{
			if (Component.Value.IsValid())
			{
				Result->SetObjectField(Component.Key, Component.Value);
			}
		}
		return FEpicodeCommandResult::Success(Result);
	}, false);

	Registry.Register(TEXT("system.methods"), TEXT("List registered command methods."), [](const TSharedPtr<FJsonObject>&, const FEpicodeCommandContext&)
	{
		UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get();
		if (!EngineSubsystem || !EngineSubsystem->GetCommandRegistry())
		{
			return FailureFrom(TEXT("NOT_INITIALIZED"), TEXT("Engine subsystem is unavailable."));
		}
		return FEpicodeCommandResult::Success(EngineSubsystem->GetCommandRegistry()->DescribeMethods());
	}, false);

	Registry.Register(TEXT("system.stats"), TEXT("Return runtime counters."), [](const TSharedPtr<FJsonObject>&, const FEpicodeCommandContext&)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
		Result->SetNumberField(TEXT("sceneRevision"), Scene ? static_cast<double>(Scene->GetDocument().Revision) : 0.0);
		if (UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get())
		{
			if (FEpicodeCommandRegistry* CommandRegistry = EngineSubsystem->GetCommandRegistry())
			{
				Result->SetNumberField(TEXT("commandCount"), CommandRegistry->GetMethodNames().Num());
			}
			if (FEpicodeEventBus* EventBus = EngineSubsystem->GetEventBus())
			{
				Result->SetNumberField(TEXT("eventSequence"), static_cast<double>(EventBus->GetLatestSequence()));
				Result->SetNumberField(TEXT("eventHistory"), EventBus->GetHistorySize());
			}
		}
		return FEpicodeCommandResult::Success(Result);
	}, false);

	Registry.Register(TEXT("scene.new"), TEXT("Create a new scene."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		if (!Scene)
		{
			return SceneRequired();
		}
		FString Name;
		EpicodeJson::GetOptionalString(Params, TEXT("name"), Name, TEXT("Untitled"));
		FString Error;
		if (!Scene->NewScene(Name, Error))
		{
			return FailureFrom(TEXT("SCENE_CREATE_FAILED"), Error);
		}
		return FEpicodeCommandResult::Success(MakeSceneResult(Scene));
	});

	Registry.Register(TEXT("scene.open"), TEXT("Open a scene from the scene library."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FString SceneId;
		FString Error;
		if (!Scene || !EpicodeJson::GetRequiredString(Params, TEXT("sceneId"), SceneId, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		if (!Scene->OpenScene(SceneId, Error))
		{
			return FailureFrom(TEXT("SCENE_OPEN_FAILED"), Error);
		}
		return FEpicodeCommandResult::Success(MakeSceneResult(Scene));
	});

	Registry.Register(TEXT("scene.switch"), TEXT("Switch to a scene from the scene library."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FString SceneId;
		FString Error;
		if (!Scene || !EpicodeJson::GetRequiredString(Params, TEXT("sceneId"), SceneId, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		if (!Scene->SwitchScene(SceneId, Error))
		{
			return FailureFrom(TEXT("SCENE_SWITCH_FAILED"), Error);
		}
		return FEpicodeCommandResult::Success(MakeSceneResult(Scene));
	});

	Registry.Register(TEXT("scene.save"), TEXT("Save the current scene."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FString Path;
		FString Error;
		if (!Scene || !Scene->SaveScene(Path, Error))
		{
			return FailureFrom(TEXT("SCENE_SAVE_FAILED"), Error);
		}
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("path"), Path);
		return FEpicodeCommandResult::Success(Result);
	});

	Registry.Register(TEXT("scene.saveAs"), TEXT("Save the current scene under a new name."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FString Name;
		FString Path;
		FString Error;
		EpicodeJson::GetOptionalString(Params, TEXT("name"), Name, TEXT("Untitled"));
		if (!Scene || !Scene->SaveSceneAs(Name, Path, Error))
		{
			return FailureFrom(TEXT("SCENE_SAVE_FAILED"), Error);
		}
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("path"), Path);
		return FEpicodeCommandResult::Success(Result);
	});

	Registry.Register(TEXT("scene.close"), TEXT("Close the current scene."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		bool bDiscard = false;
		EpicodeJson::GetOptionalBool(Params, TEXT("discard"), bDiscard, false);
		FString Error;
		if (!Scene || !Scene->CloseScene(bDiscard, Error))
		{
			return FailureFrom(TEXT("SCENE_CLOSE_FAILED"), Error);
		}
		return FEpicodeCommandResult::Success(MakeShared<FJsonObject>());
	});

	Registry.Register(TEXT("scene.list"), TEXT("List scenes in the scene library."), [](const TSharedPtr<FJsonObject>&, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		if (!Scene)
		{
			return SceneRequired();
		}
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FEpicodeSceneLibraryEntry& Entry : Scene->ListScenes())
		{
			Values.Add(MakeShared<FJsonValueObject>(Entry.ToJson()));
		}
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("scenes"), Values);
		Result->SetStringField(TEXT("currentSceneId"), Scene->GetCurrentSceneId());
		return FEpicodeCommandResult::Success(Result);
	}, false);

	Registry.Register(TEXT("scene.delete"), TEXT("Move a closed scene to the trash directory."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FString SceneId;
		FString Error;
		if (!Scene || !EpicodeJson::GetRequiredString(Params, TEXT("sceneId"), SceneId, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		FString TrashPath;
		FEpicodeCommandResult Result = RunMutation(Scene, Context, TEXT("scene.delete"), [&]()
		{
			if (!Scene->DeleteScene(SceneId, TrashPath, Error))
			{
				return FailureFrom(TEXT("SCENE_DELETE_FAILED"), Error);
			}
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("sceneId"), SceneId);
			Payload->SetStringField(TEXT("trashPath"), TrashPath);
			return FEpicodeCommandResult::Success(Payload);
		});
		return Result;
	});

	Registry.Register(TEXT("scene.snapshot"), TEXT("Return the current scene snapshot, optionally since an event sequence."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		if (!Scene || !Scene->HasScene())
		{
			return SceneRequired();
		}
		double SinceNumber = 0.0;
		EpicodeJson::GetOptionalNumber(Params, TEXT("since"), SinceNumber, 0.0);
		if (SinceNumber < 0.0)
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), TEXT("since must be zero or greater."));
		}
		const uint64 Since = static_cast<uint64>(SinceNumber);
		TSharedPtr<FJsonObject> Result = MakeSceneResult(Scene);
		UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get();
		FEpicodeEventBus* EventBus = EngineSubsystem ? EngineSubsystem->GetEventBus() : nullptr;
		if (!EventBus)
		{
			return FEpicodeCommandResult::Success(Result);
		}
		const uint64 Latest = EventBus->GetLatestSequence();
		if (Since > Latest)
		{
			return FailureFrom(TEXT("SNAPSHOT_VERSION_AHEAD"), TEXT("since is newer than the current event sequence."));
		}
		Result->SetNumberField(TEXT("eventSequence"), static_cast<double>(Latest));
		Result->SetNumberField(TEXT("since"), static_cast<double>(Since));
		if (Since == 0)
		{
			Result->SetBoolField(TEXT("incremental"), false);
			return FEpicodeCommandResult::Success(Result);
		}
		const bool bStale = Latest > Since && Latest - Since > static_cast<uint64>(EventBus->GetHistorySize());
		Result->SetBoolField(TEXT("incremental"), !bStale);
		Result->SetBoolField(TEXT("fullSnapshotRequired"), bStale);
		TArray<TSharedPtr<FJsonValue>> Events;
		if (!bStale)
		{
			for (const FEpicodeEvent& Event : EventBus->GetSince(Since, 1024))
			{
				Events.Add(MakeShared<FJsonValueObject>(Event.ToJson()));
			}
		}
		Result->SetArrayField(TEXT("events"), Events);
		Result->SetBoolField(TEXT("unchanged"), !bStale && Events.Num() == 0);
		return FEpicodeCommandResult::Success(Result);
	}, false);

	Registry.Register(TEXT("scene.runtime.get"), TEXT("Return the scene runtime metadata."), [](const TSharedPtr<FJsonObject>&, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		if (!Scene || !Scene->HasScene())
		{
			return SceneRequired();
		}
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("sceneId"), Scene->GetCurrentSceneId());
		Result->SetNumberField(TEXT("version"), static_cast<double>(Scene->GetDocument().Revision));
		Result->SetStringField(TEXT("levelPath"), Scene->GetDocument().LevelPath);
		Result->SetObjectField(TEXT("runtime"), Scene->GetDocument().Runtime.ToJson());
		return FEpicodeCommandResult::Success(Result);
	});

	Registry.Register(TEXT("scene.runtime.set"), TEXT("Replace the scene runtime metadata."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		if (!Scene || !Scene->HasScene())
		{
			return SceneRequired();
		}
		const TSharedPtr<FJsonObject> RuntimeJson = EpicodeJson::GetOptionalObject(Params, TEXT("runtime"));
		if (!RuntimeJson.IsValid())
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), TEXT("runtime must be an object."));
		}
		FEpicodeRuntimeMetadata Runtime;
		FString Error;
		if (!FEpicodeRuntimeMetadata::FromJson(RuntimeJson, Runtime, Error))
		{
			return FailureFrom(TEXT("INVALID_RUNTIME"), Error);
		}
		FString LevelPath;
		EpicodeJson::GetOptionalString(Params, TEXT("levelPath"), LevelPath, Scene->GetDocument().LevelPath);
		bool bValidateForRuntime = false;
		EpicodeJson::GetOptionalBool(Params, TEXT("validateForRuntime"), bValidateForRuntime, false);
		return RunMutation(Scene, Context, TEXT("scene.runtime.set"), [&]()
		{
			if (!Scene->SetRuntimeMetadata(Runtime, LevelPath, bValidateForRuntime, Error))
			{
				return FailureFrom(TEXT("INVALID_RUNTIME"), Error);
			}
			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("sceneId"), Scene->GetCurrentSceneId());
			Result->SetNumberField(TEXT("version"), static_cast<double>(Scene->GetDocument().Revision));
			Result->SetStringField(TEXT("levelPath"), Scene->GetDocument().LevelPath);
			Result->SetObjectField(TEXT("runtime"), Scene->GetDocument().Runtime.ToJson());
			return FEpicodeCommandResult::Success(Result);
		});
	});

	Registry.Register(TEXT("scene.transaction"), TEXT("Execute multiple scene commands atomically."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		if (!Scene || !Scene->HasScene())
		{
			return SceneRequired();
		}
		const TArray<TSharedPtr<FJsonValue>> Commands = EpicodeJson::GetOptionalArray(Params, TEXT("commands"));
		if (Commands.Num() == 0)
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), TEXT("commands must be a non-empty array."));
		}
		UEpicodeEngineSubsystem* EngineSubsystem = UEpicodeEngineSubsystem::Get();
		if (!EngineSubsystem)
		{
			return FailureFrom(TEXT("NOT_INITIALIZED"), TEXT("Engine subsystem is unavailable."));
		}
		const FEpicodeSceneDocument Backup = CloneDocument(Scene->GetDocument());
		TArray<FString> Affected;
		for (const TSharedPtr<FJsonValue>& Value : Commands)
		{
			const TSharedPtr<FJsonObject> Command = Value->AsObject();
			if (!Command.IsValid())
			{
				Scene->RestoreDocument(Backup);
				return FailureFrom(TEXT("TRANSACTION_FAILED"), TEXT("Each command must be an object."));
			}
			FString Method;
			FString Error;
			if (!EpicodeJson::GetRequiredString(Command, TEXT("method"), Method, Error))
			{
				Scene->RestoreDocument(Backup);
				return FailureFrom(TEXT("TRANSACTION_FAILED"), Error);
			}
			if (Method == TEXT("scene.transaction"))
			{
				Scene->RestoreDocument(Backup);
				return FailureFrom(TEXT("TRANSACTION_FAILED"), TEXT("Nested scene.transaction is not supported."));
			}
			const TSharedPtr<FJsonObject>* CommandParams = nullptr;
			TSharedPtr<FJsonObject> ParamsCopy = MakeShared<FJsonObject>();
			if (Command->TryGetObjectField(TEXT("params"), CommandParams) && CommandParams)
			{
				ParamsCopy = *CommandParams;
			}
			FEpicodeCommandContext ChildContext = Context;
			ChildContext.Source = TEXT("transaction");
			FEpicodeCommandResult ChildResult;
			FString UnknownMethod;
			if (!EngineSubsystem->GetCommandRegistry()->Execute(Method, ParamsCopy, ChildContext, ChildResult, UnknownMethod) || !ChildResult.bSuccess)
			{
				Scene->RestoreDocument(Backup);
				return FailureFrom(ChildResult.ErrorCode.IsEmpty() ? TEXT("TRANSACTION_FAILED") : ChildResult.ErrorCode, ChildResult.ErrorMessage.IsEmpty() ? FString::Printf(TEXT("Command failed: %s"), *Method) : ChildResult.ErrorMessage);
			}
			Affected.Append(ChildResult.AffectedIds);
		}
		return FEpicodeCommandResult::Success(EpicodeJson::MakeResult(Affected), Affected);
	});

	Registry.Register(TEXT("entity.list"), TEXT("List all entities in the current scene."), [](const TSharedPtr<FJsonObject>&, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		if (!Scene || !Scene->HasScene())
		{
			return SceneRequired();
		}
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FEpicodeEntityRecord& Entity : Scene->GetDocument().Entities)
		{
			Values.Add(MakeShared<FJsonValueObject>(Entity.ToJson()));
		}
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("entities"), Values);
		Result->SetStringField(TEXT("sceneId"), Scene->GetCurrentSceneId());
		return FEpicodeCommandResult::Success(Result);
	}, false);

	Registry.Register(TEXT("entity.get"), TEXT("Get an entity by id."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FGuid Id;
		FString Error;
		if (!Scene || !GetGuid(Params, TEXT("id"), Id, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		const FEpicodeEntityRecord* Entity = Scene->FindEntity(Id);
		if (!Entity)
		{
			return FailureFrom(TEXT("ENTITY_NOT_FOUND"), TEXT("Entity not found."));
		}
		return FEpicodeCommandResult::Success(MakeEntityResult(Scene, *Entity));
	}, false);

	Registry.Register(TEXT("entity.create"), TEXT("Create a generic entity."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		if (!Scene || !Scene->HasScene())
		{
			return SceneRequired();
		}
		FString TypeString;
		FString Error;
		if (!EpicodeJson::GetRequiredString(Params, TEXT("type"), TypeString, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error);
		}
		EEpicodeEntityType Type;
		if (!ParseEntityType(TypeString, Type))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), TEXT("Unknown entity type."));
		}
		FString Name;
		EpicodeJson::GetOptionalString(Params, TEXT("name"), Name, LexToString(Type));
		FGuid ParentId;
		if (!GetOptionalGuid(Params, TEXT("parentId"), ParentId))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), TEXT("parentId must be a GUID."));
		}
		FEpicodeTransform Transform;
		if (!GetTransform(Params, Transform, Error))
		{
			return FailureFrom(TEXT("INVALID_TRANSFORM"), Error);
		}
		TSharedPtr<FJsonObject> Data = GetEntityData(Params, Type);
		FGuid Id;
		return RunMutation(Scene, Context, TEXT("entity.create"), [&]()
		{
			if (!Scene->CreateEntity(Type, Name, ParentId, Transform, Data, Id, Error))
			{
				return FailureFrom(TEXT("CREATE_FAILED"), Error);
			}
			return EntityMutationResult(Scene, Id);
		});
	});

	Registry.Register(TEXT("entity.delete"), TEXT("Delete an entity."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FGuid Id;
		FString Error;
		if (!Scene || !GetGuid(Params, TEXT("id"), Id, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		bool bCascade = true;
		EpicodeJson::GetOptionalBool(Params, TEXT("cascade"), bCascade, true);
		TArray<FString> Removed;
		return RunMutation(Scene, Context, TEXT("entity.delete"), [&]()
		{
			if (!Scene->DeleteEntity(Id, bCascade, Removed, Error))
			{
				return FailureFrom(TEXT("ENTITY_DELETE_FAILED"), Error);
			}
			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const FString& Value : Removed)
			{
				Values.Add(MakeShared<FJsonValueString>(Value));
			}
			Result->SetArrayField(TEXT("removed"), Values);
			return FEpicodeCommandResult::Success(Result, Removed);
		});
	});

	Registry.Register(TEXT("entity.duplicate"), TEXT("Duplicate an entity."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FGuid Id;
		FString Error;
		if (!Scene || !GetGuid(Params, TEXT("id"), Id, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		FVector Offset = FVector(50.0, 0.0, 0.0);
		EpicodeJson::GetVector(Params, TEXT("offset"), Offset, Error);
		FGuid NewId;
		return RunMutation(Scene, Context, TEXT("entity.duplicate"), [&]()
		{
			if (!Scene->DuplicateEntity(Id, Offset, NewId, Error))
			{
				return FailureFrom(TEXT("ENTITY_DUPLICATE_FAILED"), Error);
			}
			return EntityMutationResult(Scene, NewId);
		});
	});

	Registry.Register(TEXT("entity.rename"), TEXT("Rename an entity."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FGuid Id;
		FString Name;
		FString Error;
		if (!Scene || !GetGuid(Params, TEXT("id"), Id, Error) || !EpicodeJson::GetRequiredString(Params, TEXT("name"), Name, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		return RunMutation(Scene, Context, TEXT("entity.rename"), [&]()
		{
			if (!Scene->RenameEntity(Id, Name, Error))
			{
				return FailureFrom(TEXT("ENTITY_RENAME_FAILED"), Error);
			}
			return EntityMutationResult(Scene, Id);
		});
	});

	Registry.Register(TEXT("entity.reparent"), TEXT("Reparent an entity."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FGuid Id;
		FGuid ParentId;
		FString Error;
		if (!Scene || !GetGuid(Params, TEXT("id"), Id, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		GetOptionalGuid(Params, TEXT("parentId"), ParentId);
		bool bKeepWorld = true;
		EpicodeJson::GetOptionalBool(Params, TEXT("keepWorldTransform"), bKeepWorld, true);
		return RunMutation(Scene, Context, TEXT("entity.reparent"), [&]()
		{
			if (!Scene->ReparentEntity(Id, ParentId, bKeepWorld, Error))
			{
				return FailureFrom(TEXT("ENTITY_REPARENT_FAILED"), Error);
			}
			return EntityMutationResult(Scene, Id);
		});
	});

	Registry.Register(TEXT("entity.setTransform"), TEXT("Set an entity transform."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FGuid Id;
		FString Error;
		if (!Scene || !GetGuid(Params, TEXT("id"), Id, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		FEpicodeTransform Transform;
		if (!GetTransform(Params, Transform, Error))
		{
			return FailureFrom(TEXT("INVALID_TRANSFORM"), Error);
		}
		bool bWorld = false;
		EpicodeJson::GetOptionalBool(Params, TEXT("worldSpace"), bWorld, false);
		return RunMutation(Scene, Context, TEXT("entity.setTransform"), [&]()
		{
			if (!Scene->SetEntityTransform(Id, Transform, bWorld, Error))
			{
				return FailureFrom(TEXT("ENTITY_TRANSFORM_FAILED"), Error);
			}
			return EntityMutationResult(Scene, Id);
		});
	});

	Registry.Register(TEXT("entity.setVisible"), TEXT("Set entity visibility."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FGuid Id;
		FString Error;
		bool bVisible = true;
		if (!Scene || !GetGuid(Params, TEXT("id"), Id, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		EpicodeJson::GetOptionalBool(Params, TEXT("visible"), bVisible, true);
		return RunMutation(Scene, Context, TEXT("entity.setVisible"), [&]()
		{
			if (!Scene->SetEntityVisible(Id, bVisible, Error))
			{
				return FailureFrom(TEXT("ENTITY_VISIBILITY_FAILED"), Error);
			}
			return EntityMutationResult(Scene, Id);
		});
	});

	Registry.Register(TEXT("character.spawn"), TEXT("Create a character from a skeletal mesh asset."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		return CreateCharacterSpawn(Params, Context, false);
	});
	Registry.Register(TEXT("character.bind"), TEXT("Bind a character to a skeletal mesh and optional animation."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FGuid Id;
		FString MeshPath;
		FString Error;
		if (!Scene || !GetGuid(Params, TEXT("id"), Id, Error) || !EpicodeJson::GetRequiredString(Params, TEXT("skeletalMesh"), MeshPath, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		AEpicodeCharacterActor* Actor = Cast<AEpicodeCharacterActor>(Scene->GetEntityActor(Id));
		if (!Actor)
		{
			return FailureFrom(TEXT("ENTITY_NOT_FOUND"), TEXT("Character actor not found."));
		}
		if (!Actor->SetSkeletalMeshPath(MeshPath, Error))
		{
			return FailureFrom(TEXT("INVALID_ASSET"), Error);
		}
		FEpicodeEntityRecord* Record = Scene->FindEntity(Id);
		if (Record)
		{
			Record->Data->SetStringField(TEXT("skeletalMesh"), MeshPath);
		}
		Scene->NotifyEntityMutated(Id);
		return EntityMutationResult(Scene, Id);
	});
	Registry.Register(TEXT("character.play"), TEXT("Play an animation on a character."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FGuid Id;
		FString Animation;
		FString Error;
		if (!Scene || !GetGuid(Params, TEXT("id"), Id, Error) || !EpicodeJson::GetRequiredString(Params, TEXT("animation"), Animation, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		AEpicodeCharacterActor* Actor = Cast<AEpicodeCharacterActor>(Scene->GetEntityActor(Id));
		if (!Actor)
		{
			return FailureFrom(TEXT("ENTITY_NOT_FOUND"), TEXT("Character actor not found."));
		}
		bool bLoop = true;
		double Rate = 1.0;
		EpicodeJson::GetOptionalBool(Params, TEXT("loop"), bLoop, true);
		EpicodeJson::GetOptionalNumber(Params, TEXT("playRate"), Rate, 1.0);
		if (!Actor->PlayAnimationPath(Animation, bLoop, static_cast<float>(Rate), Error))
		{
			return FailureFrom(TEXT("INVALID_ASSET"), Error);
		}
		FEpicodeEntityRecord* Record = Scene->FindEntity(Id);
		if (Record)
		{
			Record->Data->SetStringField(TEXT("animation"), Animation);
			Record->Data->SetBoolField(TEXT("loop"), bLoop);
			Record->Data->SetNumberField(TEXT("playRate"), Rate);
		}
		Scene->NotifyEntityMutated(Id);
		return EntityMutationResult(Scene, Id);
	});
	Registry.Register(TEXT("character.pause"), TEXT("Pause or resume character animation."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FGuid Id;
		FString Error;
		bool bPause = true;
		if (!Scene || !GetGuid(Params, TEXT("id"), Id, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		EpicodeJson::GetOptionalBool(Params, TEXT("pause"), bPause, true);
		AEpicodeCharacterActor* Actor = Cast<AEpicodeCharacterActor>(Scene->GetEntityActor(Id));
		if (!Actor)
		{
			return FailureFrom(TEXT("ENTITY_NOT_FOUND"), TEXT("Character actor not found."));
		}
		Actor->PauseAnimation(bPause);
		if (FEpicodeEntityRecord* Record = Scene->FindEntity(Id))
		{
			Record->Data->SetBoolField(TEXT("paused"), bPause);
		}
		Scene->NotifyEntityMutated(Id);
		return EntityMutationResult(Scene, Id);
	});
	Registry.Register(TEXT("character.seek"), TEXT("Seek character animation time."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FGuid Id;
		FString Error;
		double Time = 0.0;
		if (!Scene || !GetGuid(Params, TEXT("id"), Id, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		EpicodeJson::GetOptionalNumber(Params, TEXT("time"), Time, 0.0);
		if (Time < 0.0)
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), TEXT("time must not be negative."));
		}
		AEpicodeCharacterActor* Actor = Cast<AEpicodeCharacterActor>(Scene->GetEntityActor(Id));
		if (!Actor)
		{
			return FailureFrom(TEXT("ENTITY_NOT_FOUND"), TEXT("Character actor not found."));
		}
		if (!Actor->SetAnimationTime(static_cast<float>(Time), Error))
		{
			return FailureFrom(TEXT("ANIMATION_FAILED"), Error);
		}
		if (FEpicodeEntityRecord* Record = Scene->FindEntity(Id))
		{
			Record->Data->SetNumberField(TEXT("time"), Time);
		}
		Scene->NotifyEntityMutated(Id);
		return EntityMutationResult(Scene, Id);
	});
	Registry.Register(TEXT("character.setSpeed"), TEXT("Set character animation speed."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FGuid Id;
		FString Error;
		double Rate = 1.0;
		if (!Scene || !GetGuid(Params, TEXT("id"), Id, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		EpicodeJson::GetOptionalNumber(Params, TEXT("playRate"), Rate, 1.0);
		if (Rate < 0.0)
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), TEXT("playRate must not be negative."));
		}
		AEpicodeCharacterActor* Actor = Cast<AEpicodeCharacterActor>(Scene->GetEntityActor(Id));
		if (!Actor)
		{
			return FailureFrom(TEXT("ENTITY_NOT_FOUND"), TEXT("Character actor not found."));
		}
		Actor->SetAnimationRate(static_cast<float>(Rate));
		if (FEpicodeEntityRecord* Record = Scene->FindEntity(Id))
		{
			Record->Data->SetNumberField(TEXT("playRate"), Rate);
		}
		Scene->NotifyEntityMutated(Id);
		return EntityMutationResult(Scene, Id);
	});
	Registry.Register(TEXT("character.lookAt"), TEXT("Rotate a character toward a target entity or position."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FGuid Id;
		FString Error;
		if (!Scene || !GetGuid(Params, TEXT("id"), Id, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		FVector Target;
		FGuid TargetId;
		if (GetOptionalGuid(Params, TEXT("targetId"), TargetId) && TargetId.IsValid())
		{
			if (const FEpicodeEntityRecord* TargetEntity = Scene->FindEntity(TargetId))
			{
				Target = Scene->GetDocument().ComputeWorldTransform(TargetEntity->Id).Location;
			}
			else
			{
				return FailureFrom(TEXT("ENTITY_NOT_FOUND"), TEXT("Target entity not found."));
			}
		}
		else if (!EpicodeJson::GetVector(Params, TEXT("target"), Target, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), TEXT("target or targetId is required."));
		}
		const FEpicodeEntityRecord* Record = Scene->FindEntity(Id);
		if (!Record)
		{
			return FailureFrom(TEXT("ENTITY_NOT_FOUND"), TEXT("Entity not found."));
		}
		FEpicodeTransform Transform = Scene->GetDocument().ComputeWorldTransform(Id);
		Transform.Rotation = (Target - Transform.Location).Rotation();
		return RunMutation(Scene, Context, TEXT("character.lookAt"), [&]()
		{
			if (!Scene->SetEntityTransform(Id, Transform, true, Error))
			{
				return FailureFrom(TEXT("ENTITY_TRANSFORM_FAILED"), Error);
			}
			return EntityMutationResult(Scene, Id);
		});
	});
	Registry.Register(TEXT("character.attach"), TEXT("Attach a character to another entity."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FGuid Id;
		FGuid ParentId;
		FString Error;
		if (!Scene || !GetGuid(Params, TEXT("id"), Id, Error) || !GetOptionalGuid(Params, TEXT("parentId"), ParentId) || !ParentId.IsValid())
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), TEXT("id and parentId are required."));
		}
		FString Socket;
		EpicodeJson::GetOptionalString(Params, TEXT("socket"), Socket);
		TSharedPtr<FJsonObject> Patch = MakeShared<FJsonObject>();
		if (!Socket.IsEmpty())
		{
			Patch->SetStringField(TEXT("socket"), Socket);
		}
		FEpicodeEntityRecord* Record = Scene->FindEntity(Id);
		TSharedPtr<FJsonObject> Data = Record ? Record->Data : MakeShared<FJsonObject>();
		CopyFields(Patch, Data);
		return RunMutation(Scene, Context, TEXT("character.attach"), [&]()
		{
			if (!Scene->SetEntityParentAndData(Id, ParentId, Data, Error))
			{
				return FailureFrom(TEXT("ATTACH_FAILED"), Error);
			}
			if (!Socket.IsEmpty())
			{
				if (AEpicodeCharacterActor* ParentActor = Cast<AEpicodeCharacterActor>(Scene->GetEntityActor(ParentId)))
				{
					if (AActor* Actor = Scene->GetEntityActor(Id))
					{
						Actor->AttachToComponent(ParentActor->MeshComponent, FAttachmentTransformRules::KeepWorldTransform, *Socket);
					}
				}
			}
			return EntityMutationResult(Scene, Id);
		});
	});
	Registry.Register(TEXT("character.detach"), TEXT("Detach a character from its parent."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FGuid Id;
		FString Error;
		if (!Scene || !GetGuid(Params, TEXT("id"), Id, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		return RunMutation(Scene, Context, TEXT("character.detach"), [&]()
		{
			if (!Scene->ReparentEntity(Id, FGuid(), false, Error))
			{
				return FailureFrom(TEXT("DETACH_FAILED"), Error);
			}
			return EntityMutationResult(Scene, Id);
		});
	});
	Registry.Register(TEXT("character.setMaterial"), TEXT("Override a character material slot."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FGuid Id;
		FString MaterialPath;
		FString Error;
		double SlotNumber = 0.0;
		if (!Scene || !GetGuid(Params, TEXT("id"), Id, Error) || !EpicodeJson::GetRequiredString(Params, TEXT("material"), MaterialPath, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		EpicodeJson::GetOptionalNumber(Params, TEXT("slot"), SlotNumber, 0.0);
		AEpicodeCharacterActor* Actor = Cast<AEpicodeCharacterActor>(Scene->GetEntityActor(Id));
		if (!Actor)
		{
			return FailureFrom(TEXT("ENTITY_NOT_FOUND"), TEXT("Character actor not found."));
		}
		if (!Actor->SetMaterialOverride(static_cast<int32>(SlotNumber), MaterialPath, Error))
		{
			return FailureFrom(TEXT("MATERIAL_FAILED"), Error);
		}
		if (FEpicodeEntityRecord* Record = Scene->FindEntity(Id))
		{
			AddMaterialOverride(Record->Data, static_cast<int32>(SlotNumber), MaterialPath);
		}
		Scene->NotifyEntityMutated(Id);
		return EntityMutationResult(Scene, Id);
	});

	Registry.Register(TEXT("prop.spawn"), TEXT("Create a prop from a static mesh asset."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		return CreatePropSpawn(Params, Context);
	});
	Registry.Register(TEXT("prop.create"), TEXT("Create a prop from a static mesh asset."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		return CreatePropSpawn(Params, Context);
	});
	Registry.Register(TEXT("prop.setMaterial"), TEXT("Override a prop material slot."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FGuid Id;
		FString MaterialPath;
		FString Error;
		double SlotNumber = 0.0;
		if (!Scene || !GetGuid(Params, TEXT("id"), Id, Error) || !EpicodeJson::GetRequiredString(Params, TEXT("material"), MaterialPath, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		EpicodeJson::GetOptionalNumber(Params, TEXT("slot"), SlotNumber, 0.0);
		AEpicodePropActor* Actor = Cast<AEpicodePropActor>(Scene->GetEntityActor(Id));
		if (!Actor)
		{
			return FailureFrom(TEXT("ENTITY_NOT_FOUND"), TEXT("Prop actor not found."));
		}
		if (!Actor->SetMaterialOverride(static_cast<int32>(SlotNumber), MaterialPath, Error))
		{
			return FailureFrom(TEXT("MATERIAL_FAILED"), Error);
		}
		if (FEpicodeEntityRecord* Record = Scene->FindEntity(Id))
		{
			AddMaterialOverride(Record->Data, static_cast<int32>(SlotNumber), MaterialPath);
		}
		Scene->NotifyEntityMutated(Id);
		return EntityMutationResult(Scene, Id);
	});
	Registry.Register(TEXT("prop.resetMaterial"), TEXT("Restore a prop material slot."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FGuid Id;
		FString Error;
		double SlotNumber = 0.0;
		if (!Scene || !GetGuid(Params, TEXT("id"), Id, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		EpicodeJson::GetOptionalNumber(Params, TEXT("slot"), SlotNumber, 0.0);
		AEpicodePropActor* Actor = Cast<AEpicodePropActor>(Scene->GetEntityActor(Id));
		if (!Actor)
		{
			return FailureFrom(TEXT("ENTITY_NOT_FOUND"), TEXT("Prop actor not found."));
		}
		Actor->ClearMaterialOverride(static_cast<int32>(SlotNumber));
		if (FEpicodeEntityRecord* Record = Scene->FindEntity(Id))
		{
			RemoveMaterialOverride(Record->Data, static_cast<int32>(SlotNumber));
		}
		Scene->NotifyEntityMutated(Id);
		return EntityMutationResult(Scene, Id);
	});
	Registry.Register(TEXT("prop.setPhysics"), TEXT("Enable or disable prop physics."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FGuid Id;
		FString Error;
		bool bSimulate = false;
		if (!Scene || !GetGuid(Params, TEXT("id"), Id, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		EpicodeJson::GetOptionalBool(Params, TEXT("simulate"), bSimulate, false);
		AEpicodePropActor* Actor = Cast<AEpicodePropActor>(Scene->GetEntityActor(Id));
		if (!Actor)
		{
			return FailureFrom(TEXT("ENTITY_NOT_FOUND"), TEXT("Prop actor not found."));
		}
		Actor->SetSimulatePhysics(bSimulate);
		if (FEpicodeEntityRecord* Record = Scene->FindEntity(Id))
		{
			Record->Data->SetBoolField(TEXT("simulatePhysics"), bSimulate);
		}
		Scene->NotifyEntityMutated(Id);
		return EntityMutationResult(Scene, Id);
	});
	Registry.Register(TEXT("prop.attach"), TEXT("Attach a prop to another entity."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FGuid Id;
		FGuid ParentId;
		FString Error;
		if (!Scene || !GetGuid(Params, TEXT("id"), Id, Error) || !GetOptionalGuid(Params, TEXT("parentId"), ParentId))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		return RunMutation(Scene, Context, TEXT("prop.attach"), [&]()
		{
			if (!Scene->ReparentEntity(Id, ParentId, true, Error))
			{
				return FailureFrom(TEXT("ATTACH_FAILED"), Error);
			}
			return EntityMutationResult(Scene, Id);
		});
	});

	Registry.Register(TEXT("light.create"), TEXT("Create a light."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		return CreateLight(Params, Context);
	});
	Registry.Register(TEXT("light.update"), TEXT("Update light parameters."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FGuid Id;
		FString Error;
		if (!Scene || !GetGuid(Params, TEXT("id"), Id, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		FEpicodeEntityRecord* Record = Scene->FindEntity(Id);
		if (!Record || Record->Type != EEpicodeEntityType::Light)
		{
			return FailureFrom(TEXT("ENTITY_NOT_FOUND"), TEXT("Light not found."));
		}
		TSharedPtr<FJsonObject> Data = Record->Data;
		CopyFields(Params, Data);
		double Inner = 0.0;
		double Outer = 0.0;
		Data->TryGetNumberField(TEXT("innerConeAngle"), Inner);
		Data->TryGetNumberField(TEXT("outerConeAngle"), Outer);
		if (Inner > Outer && Outer > 0.0)
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), TEXT("innerConeAngle must not exceed outerConeAngle."));
		}
		return RunMutation(Scene, Context, TEXT("light.update"), [&]()
		{
			if (!Scene->SetEntityData(Id, Data, Error))
			{
				return FailureFrom(TEXT("LIGHT_UPDATE_FAILED"), Error);
			}
			return EntityMutationResult(Scene, Id);
		});
	});
	Registry.Register(TEXT("light.delete"), TEXT("Delete a light."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FGuid Id;
		FString Error;
		if (!Scene || !GetGuid(Params, TEXT("id"), Id, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		TArray<FString> Removed;
		return RunMutation(Scene, Context, TEXT("light.delete"), [&]()
		{
			if (!Scene->DeleteEntity(Id, false, Removed, Error))
			{
				return FailureFrom(TEXT("LIGHT_DELETE_FAILED"), Error);
			}
			return FEpicodeCommandResult::Success(EpicodeJson::MakeResult(Removed), Removed);
		});
	});
	Registry.Register(TEXT("light.list"), TEXT("List lights in the current scene."), [](const TSharedPtr<FJsonObject>&, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		if (!Scene || !Scene->HasScene())
		{
			return SceneRequired();
		}
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("lights"), EntitiesOfType(Scene, EEpicodeEntityType::Light));
		return FEpicodeCommandResult::Success(Result);
	}, false);

	Registry.Register(TEXT("environment.get"), TEXT("Return the current environment."), [](const TSharedPtr<FJsonObject>&, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		if (!Scene || !Scene->HasScene())
		{
			return SceneRequired();
		}
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetObjectField(TEXT("environment"), Scene->CaptureEnvironment());
		return FEpicodeCommandResult::Success(Result);
	}, false);
	Registry.Register(TEXT("environment.set"), TEXT("Set environment parameters."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		if (!Scene || !Scene->HasScene())
		{
			return SceneRequired();
		}
		TSharedPtr<FJsonObject> Environment = EpicodeJson::GetOptionalObject(Params, TEXT("environment"));
		if (!Environment.IsValid())
		{
			Environment = Params;
		}
		FString Error;
		return RunMutation(Scene, Context, TEXT("environment.set"), [&]()
		{
			if (!Scene->SetEnvironment(Environment, Error))
			{
				return FailureFrom(TEXT("ENVIRONMENT_FAILED"), Error);
			}
			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetObjectField(TEXT("environment"), Scene->CaptureEnvironment());
			return FEpicodeCommandResult::Success(Result);
		});
	});
	Registry.Register(TEXT("environment.applyPreset"), TEXT("Apply a named environment preset."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FString Preset;
		FString Error;
		if (!Scene || !EpicodeJson::GetRequiredString(Params, TEXT("preset"), Preset, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		return RunMutation(Scene, Context, TEXT("environment.applyPreset"), [&]()
		{
			if (!Scene->ApplyEnvironmentPreset(Preset, Error))
			{
				return FailureFrom(TEXT("ENVIRONMENT_FAILED"), Error);
			}
			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetObjectField(TEXT("environment"), Scene->CaptureEnvironment());
			return FEpicodeCommandResult::Success(Result);
		});
	});
	Registry.Register(TEXT("environment.listPresets"), TEXT("List environment presets."), [](const TSharedPtr<FJsonObject>&, const FEpicodeCommandContext&)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& Preset : FEpicodeEnvironmentController::GetPresetNames())
		{
			Values.Add(MakeShared<FJsonValueString>(Preset));
		}
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("presets"), Values);
		return FEpicodeCommandResult::Success(Result);
	}, false);

	Registry.Register(TEXT("camera.create"), TEXT("Create a camera."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		return CreateCamera(Params, Context);
	});
	Registry.Register(TEXT("camera.update"), TEXT("Update camera parameters."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FGuid Id;
		FString Error;
		if (!Scene || !GetGuid(Params, TEXT("id"), Id, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		FEpicodeEntityRecord* Record = Scene->FindEntity(Id);
		if (!Record || Record->Type != EEpicodeEntityType::Camera)
		{
			return FailureFrom(TEXT("ENTITY_NOT_FOUND"), TEXT("Camera not found."));
		}
		CopyFields(Params, Record->Data);
		return RunMutation(Scene, Context, TEXT("camera.update"), [&]()
		{
			if (!Scene->SetEntityData(Id, Record->Data, Error))
			{
				return FailureFrom(TEXT("CAMERA_UPDATE_FAILED"), Error);
			}
			return EntityMutationResult(Scene, Id);
		});
	});
	Registry.Register(TEXT("camera.delete"), TEXT("Delete a camera."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FGuid Id;
		FString Error;
		if (!Scene || !GetGuid(Params, TEXT("id"), Id, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		TArray<FString> Removed;
		return RunMutation(Scene, Context, TEXT("camera.delete"), [&]()
		{
			if (!Scene->DeleteEntity(Id, false, Removed, Error))
			{
				return FailureFrom(TEXT("CAMERA_DELETE_FAILED"), Error);
			}
			return FEpicodeCommandResult::Success(EpicodeJson::MakeResult(Removed), Removed);
		});
	});
	Registry.Register(TEXT("camera.activate"), TEXT("Activate a camera."), [](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		FGuid Id;
		FString Error;
		if (!Scene || !GetGuid(Params, TEXT("id"), Id, Error))
		{
			return FailureFrom(TEXT("INVALID_PARAMS"), Error.IsEmpty() ? TEXT("No scene subsystem.") : Error);
		}
		return RunMutation(Scene, Context, TEXT("camera.activate"), [&]()
		{
			if (!Scene->SetActiveCamera(Id, Error))
			{
				return FailureFrom(TEXT("CAMERA_ACTIVATE_FAILED"), Error);
			}
			return EntityMutationResult(Scene, Id);
		});
	});
	Registry.Register(TEXT("camera.list"), TEXT("List cameras in the current scene."), [](const TSharedPtr<FJsonObject>&, const FEpicodeCommandContext& Context)
	{
		UEpicodeSceneSubsystem* Scene = ResolveScene(Context);
		if (!Scene || !Scene->HasScene())
		{
			return SceneRequired();
		}
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("cameras"), EntitiesOfType(Scene, EEpicodeEntityType::Camera));
		Result->SetStringField(TEXT("activeCameraId"), Scene->GetActiveCameraId().ToString(EGuidFormats::DigitsWithHyphens));
		return FEpicodeCommandResult::Success(Result);
	}, false);
}
