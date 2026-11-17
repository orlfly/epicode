#include "EpicodeScript.h"

#include "Dom/JsonObject.h"
#include "EpicodeEngineSubsystem.h"
#include "EpicodeSceneSubsystem.h"
#include "EpicodeTypes.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	constexpr int32 ScriptSchemaVersion = 1;
	constexpr int32 MaxOperations = 50;
	constexpr int32 MaxAffectedEntities = 100;

	const TArray<FString>& AllowedMethods()
	{
		static const TArray<FString> Methods = {
			TEXT("character.spawn"),
			TEXT("character.bind"),
			TEXT("character.play"),
			TEXT("character.pause"),
			TEXT("character.setSpeed"),
			TEXT("character.setMaterial"),
			TEXT("prop.spawn"),
			TEXT("prop.create"),
			TEXT("prop.setMaterial"),
			TEXT("prop.resetMaterial"),
			TEXT("prop.setPhysics"),
			TEXT("light.create"),
			TEXT("light.update"),
			TEXT("light.delete"),
			TEXT("camera.create"),
			TEXT("camera.update"),
			TEXT("camera.activate"),
			TEXT("camera.delete"),
			TEXT("entity.setTransform"),
			TEXT("entity.setVisible"),
			TEXT("entity.rename"),
			TEXT("entity.delete"),
			TEXT("entity.duplicate"),
			TEXT("scene.save"),
		};
		return Methods;
	}

	bool IsCreateMethod(const FString& Method)
	{
		return Method == TEXT("character.spawn")
			|| Method == TEXT("prop.spawn")
			|| Method == TEXT("prop.create")
			|| Method == TEXT("light.create")
			|| Method == TEXT("camera.create")
			|| Method == TEXT("entity.duplicate");
	}

	bool IsDeleteMethod(const FString& Method)
	{
		return Method.EndsWith(TEXT(".delete"));
	}

	bool IsEntityMethod(const FString& Method)
	{
		return Method.StartsWith(TEXT("character."))
			|| Method.StartsWith(TEXT("prop."))
			|| Method.StartsWith(TEXT("light."))
			|| Method.StartsWith(TEXT("camera."))
			|| Method.StartsWith(TEXT("entity."));
	}

	void AddError(
		FEpicodeScriptService::FValidation& Validation,
		int32 Index,
		const FString& Code,
		const FString& Message)
	{
		TSharedRef<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetNumberField(TEXT("index"), Index);
		Error->SetStringField(TEXT("code"), Code);
		Error->SetStringField(TEXT("message"), Message);
		Validation.Errors.Add(MakeShared<FJsonValueObject>(Error));
	}

	bool IsValidClientId(const FString& ClientId)
	{
		if (ClientId.IsEmpty() || ClientId.Len() > 64)
		{
			return false;
		}
		for (const TCHAR Char : ClientId)
		{
			if (!FChar::IsAlnum(Char) && Char != TEXT('_') && Char != TEXT('-'))
			{
				return false;
			}
		}
		return true;
	}

	bool ResolveJsonValue(
		const TSharedPtr<FJsonValue>& Value,
		const TMap<FString, FString>& ClientIds,
		TSharedPtr<FJsonValue>& OutValue,
		FString& OutError)
	{
		if (!Value.IsValid())
		{
			OutValue = MakeShared<FJsonValueNull>();
			return true;
		}

		if (Value->Type == EJson::String)
		{
			const FString Text = Value->AsString();
			if (Text.StartsWith(TEXT("$")))
			{
				const FString Reference = Text.Mid(1);
				if (!ClientIds.Contains(Reference))
				{
					OutError = FString::Printf(
						TEXT("未知或前向引用的 clientId：$%s"), *Reference);
					return false;
				}
				OutValue = MakeShared<FJsonValueString>(ClientIds[Reference]);
			}
			else
			{
				OutValue = Value;
			}
			return true;
		}

		if (Value->Type == EJson::Object)
		{
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			for (const auto& Pair : Value->AsObject()->Values)
			{
				TSharedPtr<FJsonValue> Resolved;
				if (!ResolveJsonValue(Pair.Value, ClientIds, Resolved, OutError))
				{
					return false;
				}
				Object->SetField(Pair.Key, Resolved);
			}
			OutValue = MakeShared<FJsonValueObject>(Object);
			return true;
		}

		if (Value->Type == EJson::Array)
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
			{
				TSharedPtr<FJsonValue> Resolved;
				if (!ResolveJsonValue(Item, ClientIds, Resolved, OutError))
				{
					return false;
				}
				Values.Add(Resolved);
			}
			OutValue = MakeShared<FJsonValueArray>(Values);
			return true;
		}

		OutValue = Value;
		return true;
	}

	bool ResolveParams(
		const TSharedPtr<FJsonObject>& Params,
		const TMap<FString, FString>& ClientIds,
		TSharedPtr<FJsonObject>& OutParams,
		FString& OutError)
	{
		OutParams = MakeShared<FJsonObject>();
		if (!Params.IsValid())
		{
			return true;
		}
		for (const auto& Pair : Params->Values)
		{
			TSharedPtr<FJsonValue> Resolved;
			if (!ResolveJsonValue(Pair.Value, ClientIds, Resolved, OutError))
			{
				return false;
			}
			OutParams->SetField(Pair.Key, Resolved);
		}
		return true;
	}

	FString AuditPath()
	{
		return FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("Epicode/Script/audit.jsonl"));
	}

	bool WriteAudit(
		const TSharedPtr<FJsonObject>& Params,
		const TSharedPtr<FJsonObject>& Script,
		int32 OperationCount,
		int32 AffectedCount,
		bool bSuccess,
		const FString& ErrorCode,
		double DurationSeconds,
		bool bConfirmed,
		FString& OutWarning)
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("timestamp"), FDateTime::UtcNow().ToIso8601());
		FString ConversationId;
		FString Model;
		FString Backend;
		if (Params.IsValid())
		{
			Params->TryGetStringField(TEXT("conversationId"), ConversationId);
			Params->TryGetStringField(TEXT("model"), Model);
			Params->TryGetStringField(TEXT("backend"), Backend);
		}
		Entry->SetStringField(TEXT("conversationId"), ConversationId);
		Entry->SetStringField(TEXT("model"), Model);
		Entry->SetStringField(TEXT("backend"), Backend);
		FString Summary;
		if (Script.IsValid())
		{
			Script->TryGetStringField(TEXT("summary"), Summary);
		}
		if (Summary.Len() > 200)
		{
			Summary = Summary.Left(200) + TEXT("…");
		}
		Entry->SetStringField(TEXT("summary"), Summary);
		Entry->SetNumberField(TEXT("operationCount"), OperationCount);
		Entry->SetNumberField(TEXT("affectedCount"), AffectedCount);
		Entry->SetBoolField(TEXT("success"), bSuccess);
		Entry->SetBoolField(TEXT("confirmed"), bConfirmed);
		Entry->SetStringField(TEXT("errorCode"), ErrorCode);
		Entry->SetNumberField(TEXT("durationMs"), DurationSeconds * 1000.0);

		FString Line;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line);
		if (!FJsonSerializer::Serialize(Entry, Writer))
		{
			OutWarning = TEXT("脚本审计记录无法序列化");
			return false;
		}
		Line += TEXT("\n");

		const FString Path = AuditPath();
		if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true))
		{
			OutWarning = FString::Printf(TEXT("无法创建脚本审计目录：%s"), *FPaths::GetPath(Path));
			return false;
		}
		// Early builds could leave a UTF-16 BOM file. Appending UTF-8 JSONL to it
		// makes the whole file unreadable, so preserve it as .1 and start fresh.
		TArray<uint8> Header;
		if (FFileHelper::LoadFileToArray(Header, *Path) && Header.Num() >= 2
			&& ((Header[0] == 0xFF && Header[1] == 0xFE)
				|| (Header[0] == 0xFE && Header[1] == 0xFF)))
		{
			const FString LegacyPath = Path + TEXT(".1");
			IFileManager::Get().Delete(*LegacyPath, false, true, true);
			IFileManager::Get().Move(*LegacyPath, *Path, true, true);
		}
		if (!FFileHelper::SaveStringToFile(
			Line,
			*Path,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(),
			FILEWRITE_Append))
		{
			OutWarning = FString::Printf(TEXT("无法写入脚本审计：%s"), *Path);
			return false;
		}
		return true;
	}

	bool GetScriptParam(
		const TSharedPtr<FJsonObject>& Params,
		TSharedPtr<FJsonObject>& OutScript,
		FString& OutError)
	{
		if (!Params.IsValid())
		{
			OutError = TEXT("params must be an object");
			return false;
		}
		const TSharedPtr<FJsonObject>* ScriptObject = nullptr;
		if (Params->TryGetObjectField(TEXT("script"), ScriptObject)
			&& ScriptObject != nullptr && ScriptObject->IsValid())
		{
			OutScript = *ScriptObject;
			return true;
		}
		FString ScriptJson;
		if (Params->TryGetStringField(TEXT("scriptJson"), ScriptJson)
			&& !ScriptJson.IsEmpty())
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ScriptJson);
			if (!FJsonSerializer::Deserialize(Reader, OutScript) || !OutScript.IsValid())
			{
				OutError = TEXT("scriptJson is not a valid JSON object");
				return false;
			}
			return true;
		}
		if (Params->HasField(TEXT("operations")))
		{
			OutScript = Params;
			return true;
		}
		OutError = TEXT("script or scriptJson is required");
		return false;
	}

	bool ValidateTransform(
		const TSharedPtr<FJsonObject>& Params,
		FString& OutError)
	{
		if (!Params.IsValid())
		{
			return true;
		}
		TSharedPtr<FJsonObject> TransformJson = Params;
		const TSharedPtr<FJsonObject>* NestedTransform = nullptr;
		if (Params->TryGetObjectField(TEXT("transform"), NestedTransform)
			&& NestedTransform != nullptr && NestedTransform->IsValid())
		{
			TransformJson = *NestedTransform;
		}
		else if (!Params->HasField(TEXT("location"))
			&& !Params->HasField(TEXT("rotation"))
			&& !Params->HasField(TEXT("scale")))
		{
			return true;
		}
		FEpicodeTransform Transform;
		if (!FEpicodeTransform::FromJson(TransformJson, Transform, OutError))
		{
			return false;
		}
		if (!Transform.IsFinite())
		{
			OutError = TEXT("transform contains non-finite values");
			return false;
		}
		if (Transform.Scale.X <= 0.0 || Transform.Scale.Y <= 0.0 || Transform.Scale.Z <= 0.0)
		{
			OutError = TEXT("transform scale must be positive");
			return false;
		}
		return true;
	}

	bool ValidateVectorArray(
		const TSharedPtr<FJsonObject>& Params,
		const TCHAR* Field,
		FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Params->TryGetArrayField(Field, Values) || Values == nullptr)
		{
			return true;
		}
		if (Values->Num() != 3)
		{
			OutError = FString::Printf(TEXT("%s must contain exactly 3 numbers"), Field);
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			if (!Value.IsValid() || Value->Type != EJson::Number)
			{
				OutError = FString::Printf(TEXT("%s must contain only numbers"), Field);
				return false;
			}
		}
		return true;
	}

	bool ValidateAssetPath(const FString& Path, FString& OutError)
	{
		if (Path.IsEmpty())
		{
			return true;
		}
		if (!Path.StartsWith(TEXT("/Game/")))
		{
			OutError = FString::Printf(TEXT("asset path must start with /Game/: %s"), *Path);
			return false;
		}
		const FString PackageName = FPackageName::ObjectPathToPackageName(Path);
		if (!FPackageName::IsValidLongPackageName(PackageName))
		{
			OutError = FString::Printf(TEXT("invalid asset package: %s"), *Path);
			return false;
		}
		if (!FPackageName::DoesPackageExist(PackageName))
		{
			OutError = FString::Printf(TEXT("asset does not exist: %s"), *Path);
			return false;
		}
		return true;
	}

	void ValidateAssets(
		const TSharedPtr<FJsonObject>& Params,
		TArray<TSharedPtr<FJsonValue>>& PreviewAssets,
		FString& OutError)
	{
		for (const TCHAR* Field : {
			TEXT("staticMesh"), TEXT("skeletalMesh"), TEXT("animation"),
			TEXT("material"), TEXT("assetPath"), TEXT("mesh") })
		{
			FString Value;
			if (!Params.IsValid() || !Params->TryGetStringField(Field, Value) || Value.IsEmpty())
			{
				continue;
			}
			if (Value.StartsWith(TEXT("$")))
			{
				continue;
			}
			if (!ValidateAssetPath(Value, OutError))
			{
				return;
			}
			PreviewAssets.Add(MakeShared<FJsonValueString>(Value));
		}
	}

	void ValidateEntityReference(
		const TSharedPtr<FJsonObject>& Params,
		const TSet<FString>& PriorClientIds,
		UEpicodeSceneSubsystem* Scene,
		bool bRequired,
		int32 Index,
		FEpicodeScriptService::FValidation& Validation)
	{
		FString Id;
		if (!Params.IsValid() || !Params->TryGetStringField(TEXT("id"), Id) || Id.IsEmpty())
		{
			if (bRequired)
			{
				AddError(Validation, Index, TEXT("MISSING_ID"), TEXT("id is required"));
			}
			return;
		}
		if (Id.StartsWith(TEXT("$")))
		{
			const FString Reference = Id.Mid(1);
			if (!PriorClientIds.Contains(Reference))
			{
				AddError(
					Validation, Index, TEXT("UNKNOWN_CLIENT_ID"),
					FString::Printf(TEXT("unknown or forward clientId reference: $%s"), *Reference));
			}
			return;
		}
		FGuid EntityId;
		if (!FGuid::Parse(Id, EntityId))
		{
			AddError(Validation, Index, TEXT("INVALID_ID"), FString::Printf(TEXT("invalid entity id: %s"), *Id));
			return;
		}
		if (!Scene || !Scene->FindEntity(EntityId))
		{
			AddError(Validation, Index, TEXT("ENTITY_NOT_FOUND"), FString::Printf(TEXT("entity not found: %s"), *Id));
		}
	}

	void ValidateNumericField(
		const TSharedPtr<FJsonObject>& Params,
		const TCHAR* Field,
		double MinValue,
		double MaxValue,
		int32 Index,
		FEpicodeScriptService::FValidation& Validation)
	{
		if (!Params.IsValid() || !Params->HasField(Field))
		{
			return;
		}
		double Value = 0.0;
		if (!Params->TryGetNumberField(Field, Value) || !FMath::IsFinite(Value)
			|| Value < MinValue || Value > MaxValue)
		{
			AddError(
				Validation, Index, TEXT("INVALID_RANGE"),
				FString::Printf(TEXT("%s must be between %g and %g"), Field, MinValue, MaxValue));
		}
	}

	void ValidateOperation(
		const TSharedPtr<FJsonObject>& Operation,
		int32 Index,
		const TSet<FString>& PriorClientIds,
		UEpicodeSceneSubsystem* Scene,
		FEpicodeScriptService::FValidation& Validation,
		TSharedPtr<FJsonObject>& OutPreview)
	{
		const TSharedPtr<FJsonObject>* ParamsObject = nullptr;
		const TSharedPtr<FJsonObject> Params =
			Operation->TryGetObjectField(TEXT("params"), ParamsObject) && ParamsObject
				? *ParamsObject
				: MakeShared<FJsonObject>();

		FString Method;
		if (!Operation->TryGetStringField(TEXT("method"), Method) || Method.IsEmpty())
		{
			AddError(Validation, Index, TEXT("MISSING_METHOD"), TEXT("method is required"));
			return;
		}
		if (!AllowedMethods().Contains(Method))
		{
			AddError(
				Validation, Index, TEXT("METHOD_NOT_ALLOWED"),
				FString::Printf(TEXT("method is not allowed: %s"), *Method));
			return;
		}

		FString Target;
		Params->TryGetStringField(TEXT("name"), Target);
		if (Target.IsEmpty())
		{
			Params->TryGetStringField(TEXT("id"), Target);
		}
		FString Action = IsCreateMethod(Method)
			? TEXT("create")
			: (IsDeleteMethod(Method) ? TEXT("delete") : TEXT("modify"));

		TArray<TSharedPtr<FJsonValue>> Assets;
		FString Error;
		ValidateAssets(Params, Assets, Error);
		if (!Error.IsEmpty())
		{
			AddError(Validation, Index, TEXT("INVALID_ASSET"), Error);
		}

		if (Method == TEXT("scene.save"))
		{
			// No parameters.
		}
		else if (Method == TEXT("character.spawn") || Method == TEXT("character.bind"))
		{
			FString Mesh;
			if (!Params->TryGetStringField(TEXT("skeletalMesh"), Mesh) || Mesh.IsEmpty())
			{
				AddError(Validation, Index, TEXT("MISSING_ASSET"), TEXT("skeletalMesh is required"));
			}
			if (Method == TEXT("character.bind"))
			{
				ValidateEntityReference(Params, PriorClientIds, Scene, true, Index, Validation);
			}
		}
		else if (Method == TEXT("prop.spawn") || Method == TEXT("prop.create"))
		{
			FString Mesh;
			if (!Params->TryGetStringField(TEXT("staticMesh"), Mesh) || Mesh.IsEmpty())
			{
				AddError(Validation, Index, TEXT("MISSING_ASSET"), TEXT("staticMesh is required"));
			}
		}
		else if (Method == TEXT("light.create"))
		{
			FString LightType;
			if (!Params->TryGetStringField(TEXT("lightType"), LightType)
				&& !Params->TryGetStringField(TEXT("type"), LightType))
			{
				AddError(Validation, Index, TEXT("MISSING_LIGHT_TYPE"), TEXT("lightType is required"));
			}
			else
			{
				const FString Lower = LightType.ToLower();
				if (Lower != TEXT("directional") && Lower != TEXT("point")
					&& Lower != TEXT("spot") && Lower != TEXT("rect"))
				{
					AddError(Validation, Index, TEXT("INVALID_LIGHT_TYPE"),
						TEXT("lightType must be directional, point, spot or rect"));
				}
			}
			ValidateNumericField(Params, TEXT("innerConeAngle"), 0.0, 89.0, Index, Validation);
			ValidateNumericField(Params, TEXT("outerConeAngle"), 0.0, 89.0, Index, Validation);
		}
		else if (Method == TEXT("camera.create"))
		{
			ValidateNumericField(Params, TEXT("fieldOfView"), 1.0, 179.0, Index, Validation);
		}
		else if (Method == TEXT("entity.rename"))
		{
			FString Name;
			if (!Params->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
			{
				AddError(Validation, Index, TEXT("MISSING_NAME"), TEXT("name is required"));
			}
			else if (Name.Len() > 128)
			{
				AddError(Validation, Index, TEXT("INVALID_NAME"), TEXT("name must be at most 128 characters"));
			}
			ValidateEntityReference(Params, PriorClientIds, Scene, true, Index, Validation);
		}
		else if (Method == TEXT("character.setSpeed"))
		{
			ValidateNumericField(Params, TEXT("speed"), -100.0, 100.0, Index, Validation);
			ValidateEntityReference(Params, PriorClientIds, Scene, true, Index, Validation);
		}
		else if (Method == TEXT("character.play"))
		{
			FString Animation;
			if (!Params->TryGetStringField(TEXT("animation"), Animation) || Animation.IsEmpty())
			{
				AddError(Validation, Index, TEXT("MISSING_ANIMATION"), TEXT("animation is required"));
			}
			ValidateEntityReference(Params, PriorClientIds, Scene, true, Index, Validation);
		}
		else if (Method == TEXT("entity.setTransform"))
		{
			if (!ValidateTransform(Params, Error))
			{
				AddError(Validation, Index, TEXT("INVALID_TRANSFORM"), Error);
			}
			if (!ValidateVectorArray(Params, TEXT("location"), Error)
				|| !ValidateVectorArray(Params, TEXT("rotation"), Error)
				|| !ValidateVectorArray(Params, TEXT("scale"), Error))
			{
				AddError(Validation, Index, TEXT("INVALID_TRANSFORM"), Error);
			}
			ValidateEntityReference(Params, PriorClientIds, Scene, true, Index, Validation);
		}
		else if (Method == TEXT("entity.duplicate"))
		{
			if (!ValidateVectorArray(Params, TEXT("offset"), Error))
			{
				AddError(Validation, Index, TEXT("INVALID_OFFSET"), Error);
			}
			ValidateEntityReference(Params, PriorClientIds, Scene, true, Index, Validation);
		}
		else if (Method == TEXT("entity.delete")
			|| Method == TEXT("light.delete")
			|| Method == TEXT("camera.delete"))
		{
			ValidateEntityReference(Params, PriorClientIds, Scene, true, Index, Validation);
		}
		else if (Method == TEXT("character.setMaterial")
			|| Method == TEXT("prop.setMaterial"))
		{
			FString Material;
			if (!Params->TryGetStringField(TEXT("material"), Material) || Material.IsEmpty())
			{
				AddError(Validation, Index, TEXT("MISSING_MATERIAL"), TEXT("material is required"));
			}
			ValidateEntityReference(Params, PriorClientIds, Scene, true, Index, Validation);
		}
		else if (IsEntityMethod(Method))
		{
			ValidateEntityReference(Params, PriorClientIds, Scene, true, Index, Validation);
		}

		OutPreview = MakeShared<FJsonObject>();
		OutPreview->SetNumberField(TEXT("index"), Index);
		FString ClientId;
		Operation->TryGetStringField(TEXT("clientId"), ClientId);
		OutPreview->SetStringField(TEXT("clientId"), ClientId);
		OutPreview->SetStringField(TEXT("method"), Method);
		OutPreview->SetStringField(TEXT("action"), Action);
		OutPreview->SetStringField(TEXT("target"), Target);
		OutPreview->SetBoolField(TEXT("destructive"),
			IsDeleteMethod(Method) || Method == TEXT("camera.activate"));
		TArray<TSharedPtr<FJsonValue>> Fields;
		for (const auto& Pair : Params->Values)
		{
			Fields.Add(MakeShared<FJsonValueString>(FString(Pair.Key)));
		}
		OutPreview->SetArrayField(TEXT("fields"), Fields);
		OutPreview->SetArrayField(TEXT("assets"), Assets);
	}
}

const TArray<FString>& FEpicodeScriptService::GetAllowedMethods()
{
	return AllowedMethods();
}

TSharedPtr<FJsonObject> FEpicodeScriptService::FValidation::ToJson() const
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("valid"), bValid);
	Result->SetBoolField(TEXT("requiresConfirmation"), bRequiresConfirmation);
	Result->SetNumberField(TEXT("operationCount"), OperationCount);
	Result->SetNumberField(TEXT("affectedCount"), AffectedCount);
	Result->SetObjectField(
		TEXT("normalizedScript"),
		NormalizedScript.IsValid() ? NormalizedScript : MakeShared<FJsonObject>());
	Result->SetArrayField(TEXT("errors"), Errors);

	TArray<TSharedPtr<FJsonValue>> WarningValues;
	for (const FString& Warning : Warnings)
	{
		WarningValues.Add(MakeShared<FJsonValueString>(Warning));
	}
	Result->SetArrayField(TEXT("warnings"), WarningValues);
	Result->SetArrayField(TEXT("preview"), PreviewOperations);
	return Result;
}

FString FEpicodeScriptService::ExtractJsonObject(const FString& Text)
{
	FString Source = Text.TrimStartAndEnd();
	if (Source.IsEmpty())
	{
		return FString();
	}

	int32 Start = Source.Find(TEXT("```json"), ESearchCase::IgnoreCase);
	int32 FenceLength = 7;
	if (Start == INDEX_NONE)
	{
		Start = Source.Find(TEXT("```"));
		FenceLength = 3;
	}
	if (Start != INDEX_NONE)
	{
		Start += FenceLength;
		int32 End = Source.Find(TEXT("```"), ESearchCase::IgnoreCase, ESearchDir::FromStart, Start);
		if (End != INDEX_NONE)
		{
			Source = Source.Mid(Start, End - Start).TrimStartAndEnd();
		}
	}

	Start = Source.Find(TEXT("{"));
	if (Start == INDEX_NONE)
	{
		return FString();
	}

	bool bInString = false;
	bool bEscaped = false;
	int32 Depth = 0;
	for (int32 Index = Start; Index < Source.Len(); ++Index)
	{
		const TCHAR Char = Source[Index];
		if (bInString)
		{
			if (bEscaped)
			{
				bEscaped = false;
			}
			else if (Char == TEXT('\\'))
			{
				bEscaped = true;
			}
			else if (Char == TEXT('"'))
			{
				bInString = false;
			}
			continue;
		}
		if (Char == TEXT('"'))
		{
			bInString = true;
		}
		else if (Char == TEXT('{'))
		{
			++Depth;
		}
		else if (Char == TEXT('}'))
		{
			--Depth;
			if (Depth == 0)
			{
				return Source.Mid(Start, Index - Start + 1);
			}
		}
	}
	return FString();
}

bool FEpicodeScriptService::Validate(
	const TSharedPtr<FJsonObject>& Script,
	FValidation& OutValidation,
	FString& OutError)
{
	OutValidation = FValidation();
	OutError.Reset();
	if (!Script.IsValid())
	{
		OutError = TEXT("script must be an object");
		return false;
	}

	int32 SchemaVersion = 0;
	if (!Script->TryGetNumberField(TEXT("schemaVersion"), SchemaVersion))
	{
		AddError(OutValidation, -1, TEXT("MISSING_SCHEMA_VERSION"), TEXT("schemaVersion is required"));
	}
	else if (SchemaVersion != ScriptSchemaVersion)
	{
		AddError(OutValidation, -1, TEXT("UNSUPPORTED_SCHEMA_VERSION"),
			FString::Printf(TEXT("schemaVersion must be %d"), ScriptSchemaVersion));
	}

	FString Summary;
	if (Script->TryGetStringField(TEXT("summary"), Summary) && Summary.Len() > 200)
	{
		AddError(OutValidation, -1, TEXT("SUMMARY_TOO_LONG"), TEXT("summary must be at most 200 characters"));
	}
	if (Summary.IsEmpty())
	{
		Summary = TEXT("AI 编辑脚本");
	}

	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Script->TryGetArrayField(TEXT("operations"), Operations) || Operations == nullptr)
	{
		AddError(OutValidation, -1, TEXT("MISSING_OPERATIONS"), TEXT("operations must be an array"));
		OutValidation.NormalizedScript = Script;
		return true;
	}
	if (Operations->Num() == 0)
	{
		AddError(OutValidation, -1, TEXT("EMPTY_OPERATIONS"), TEXT("operations must not be empty"));
	}
	if (Operations->Num() > MaxOperations)
	{
		AddError(OutValidation, -1, TEXT("TOO_MANY_OPERATIONS"),
			FString::Printf(TEXT("operations must be at most %d"), MaxOperations));
	}

	UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
	if (!Scene || !Scene->HasScene())
	{
		AddError(OutValidation, -1, TEXT("SCENE_REQUIRED"), TEXT("an open scene is required"));
	}

	TSet<FString> ClientIds;
	TSet<FString> AffectedKeys;
	bool bComputedConfirmation = false;
	TArray<TSharedPtr<FJsonValue>> PreviewOperations;

	for (int32 Index = 0; Index < Operations->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject> Operation = (*Operations)[Index]->AsObject();
		if (!Operation.IsValid())
		{
			AddError(OutValidation, Index, TEXT("INVALID_OPERATION"), TEXT("operation must be an object"));
			continue;
		}

		FString ClientId;
		if (Operation->TryGetStringField(TEXT("clientId"), ClientId) && !ClientId.IsEmpty())
		{
			if (!IsValidClientId(ClientId))
			{
				AddError(OutValidation, Index, TEXT("INVALID_CLIENT_ID"),
					TEXT("clientId may contain only letters, numbers, '_' and '-' and be at most 64 characters"));
			}
			else if (ClientIds.Contains(ClientId))
			{
				AddError(OutValidation, Index, TEXT("DUPLICATE_CLIENT_ID"),
					FString::Printf(TEXT("duplicate clientId: %s"), *ClientId));
			}
		}

		TSharedPtr<FJsonObject> Preview;
		ValidateOperation(Operation, Index, ClientIds, Scene, OutValidation, Preview);
		if (Preview.IsValid())
		{
			PreviewOperations.Add(MakeShared<FJsonValueObject>(Preview));
		}

		const TSharedPtr<FJsonObject>* Params = nullptr;
		const TSharedPtr<FJsonObject> ParamsObject =
			Operation->TryGetObjectField(TEXT("params"), Params) && Params
				? *Params
				: MakeShared<FJsonObject>();
		FString Method;
		Operation->TryGetStringField(TEXT("method"), Method);
		if (IsDeleteMethod(Method) || Method == TEXT("camera.activate"))
		{
			bComputedConfirmation = true;
		}

		FString Id;
		if (ParamsObject->TryGetStringField(TEXT("id"), Id))
		{
			AffectedKeys.Add(Id);
		}
		else if (IsCreateMethod(Method))
		{
			AffectedKeys.Add(ClientId.IsEmpty()
				? FString::Printf(TEXT("__op_%d"), Index)
				: FString::Printf(TEXT("__client_%s"), *ClientId));
		}

		if (ClientId.IsEmpty() || !ClientIds.Contains(ClientId))
		{
			if (!ClientId.IsEmpty() && IsValidClientId(ClientId))
			{
				ClientIds.Add(ClientId);
			}
		}
	}

	if (AffectedKeys.Num() > MaxAffectedEntities)
	{
		AddError(OutValidation, -1, TEXT("TOO_MANY_AFFECTED"),
			FString::Printf(TEXT("script affects more than %d entities"), MaxAffectedEntities));
	}

	bool bRequiresConfirmation = false;
	Script->TryGetBoolField(TEXT("requiresConfirmation"), bRequiresConfirmation);
	OutValidation.bRequiresConfirmation = bRequiresConfirmation || bComputedConfirmation;
	if (bComputedConfirmation && !bRequiresConfirmation)
	{
		OutValidation.Warnings.Add(TEXT("脚本包含删除或活动相机操作，需要应用前确认"));
	}

	TSharedRef<FJsonObject> Normalized = MakeShared<FJsonObject>();
	Normalized->SetNumberField(TEXT("schemaVersion"), ScriptSchemaVersion);
	Normalized->SetStringField(TEXT("summary"), Summary);
	Normalized->SetBoolField(TEXT("requiresConfirmation"), OutValidation.bRequiresConfirmation);
	Normalized->SetArrayField(TEXT("operations"), *Operations);
	OutValidation.NormalizedScript = Normalized;
	OutValidation.OperationCount = Operations->Num();
	OutValidation.AffectedCount = AffectedKeys.Num();
	OutValidation.PreviewOperations = MoveTemp(PreviewOperations);
	OutValidation.bValid = OutValidation.Errors.Num() == 0;
	return true;
}

namespace
{
	FEpicodeCommandResult ApplyValidatedScript(
		const TSharedPtr<FJsonObject>& Script,
		const FEpicodeScriptService::FValidation& Validation,
		const TSharedPtr<FJsonObject>& Params,
		const FEpicodeCommandContext& Context,
		bool bConfirmed)
	{
		UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
		UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
		if (!Scene || !Scene->HasScene() || !Engine)
		{
			return FEpicodeCommandResult::Failure(
				TEXT("SCENE_REQUIRED"), TEXT("An open scene and engine subsystem are required."));
		}

		const FString Description = FString::Printf(
			TEXT("Apply Epicode script: %s"),
			*Validation.NormalizedScript->GetStringField(TEXT("summary")));
		FEpicodeSceneDocument Backup = Scene->GetDocument();
		const bool bWasDirty = Scene->IsDirty();
		IEpicodeTransactionSink* Sink = Scene->GetTransactionSink();
		if (Sink && Context.bUndoable)
		{
			Sink->BeginTransaction(Description);
		}

		const double StartTime = FPlatformTime::Seconds();
		TMap<FString, FString> ClientIds;
		TArray<FString> Affected;
		const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
		Validation.NormalizedScript->TryGetArrayField(TEXT("operations"), Operations);
		for (int32 Index = 0; Operations != nullptr && Index < Operations->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject> Operation = (*Operations)[Index]->AsObject();
			FString Method;
			FString Error;
			Operation->TryGetStringField(TEXT("method"), Method);
			const TSharedPtr<FJsonObject>* ParamsObject = nullptr;
			const TSharedPtr<FJsonObject> RawParams =
				Operation->TryGetObjectField(TEXT("params"), ParamsObject) && ParamsObject
					? *ParamsObject
					: MakeShared<FJsonObject>();
			TSharedPtr<FJsonObject> ResolvedParams;
			if (!ResolveParams(RawParams, ClientIds, ResolvedParams, Error))
			{
				Scene->RestoreDocument(Backup);
				Scene->SetDirty(bWasDirty);
				if (Sink && Context.bUndoable)
				{
					Sink->CancelTransaction();
				}
				FEpicodeCommandResult Failure = FEpicodeCommandResult::Failure(
					TEXT("SCRIPT_REFERENCE_INVALID"),
					FString::Printf(TEXT("operation %d: %s"), Index, *Error));
				Failure.ErrorDetail = Error;
				return Failure;
			}

			FEpicodeCommandContext ChildContext = Context;
			ChildContext.bUndoable = false;
			ChildContext.Source = TEXT("script");
			const FEpicodeCommandResult Result =
				Engine->ExecuteCommand(Method, ResolvedParams, ChildContext);
			if (!Result.bSuccess)
			{
				Scene->RestoreDocument(Backup);
				Scene->SetDirty(bWasDirty);
				if (Sink && Context.bUndoable)
				{
					Sink->CancelTransaction();
				}
				FEpicodeCommandResult Failure = FEpicodeCommandResult::Failure(
					Result.ErrorCode.IsEmpty() ? TEXT("SCRIPT_OPERATION_FAILED") : Result.ErrorCode,
					FString::Printf(TEXT("operation %d (%s): %s"), Index, *Method,
						*Result.ErrorMessage));
				Failure.ErrorDetail = Result.ErrorDetail;
				return Failure;
			}

			FString ClientId;
			Operation->TryGetStringField(TEXT("clientId"), ClientId);
			if (!ClientId.IsEmpty() && Result.AffectedIds.Num() > 0)
			{
				ClientIds.Add(ClientId, Result.AffectedIds[0]);
			}
			Affected.Append(Result.AffectedIds);
		}

		if (Sink && Context.bUndoable)
		{
			Sink->CommitTransaction();
		}
		TArray<TSharedPtr<FJsonValue>> AffectedValues;
		for (const FString& Id : Affected)
		{
			AffectedValues.Add(MakeShared<FJsonValueString>(Id));
		}
		TSharedRef<FJsonObject> ClientIdObject = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& Pair : ClientIds)
		{
			ClientIdObject->SetStringField(Pair.Key, Pair.Value);
		}

		TSharedRef<FJsonObject> ResultObject = MakeShared<FJsonObject>();
		ResultObject->SetBoolField(TEXT("applied"), true);
		ResultObject->SetStringField(TEXT("summary"),
			Validation.NormalizedScript->GetStringField(TEXT("summary")));
		ResultObject->SetNumberField(TEXT("operationCount"), Validation.OperationCount);
		ResultObject->SetNumberField(TEXT("affectedCount"), Affected.Num());
		ResultObject->SetArrayField(TEXT("affected"), AffectedValues);
		ResultObject->SetObjectField(TEXT("clientIds"), ClientIdObject);
		TArray<TSharedPtr<FJsonValue>> Warnings;
		for (const FString& Warning : Validation.Warnings)
		{
			Warnings.Add(MakeShared<FJsonValueString>(Warning));
		}
		ResultObject->SetArrayField(TEXT("warnings"), Warnings);
		ResultObject->SetStringField(TEXT("auditPath"), AuditPath());

		FString AuditWarning;
		WriteAudit(
			Params, Script, Validation.OperationCount, Affected.Num(),
			true, FString(), FPlatformTime::Seconds() - StartTime,
			bConfirmed, AuditWarning);
		if (!AuditWarning.IsEmpty())
		{
			ResultObject->SetStringField(TEXT("auditWarning"), AuditWarning);
		}
		return FEpicodeCommandResult::Success(ResultObject, Affected);
	}
}

void FEpicodeScriptService::Register(FEpicodeCommandRegistry& Registry)
{
	Registry.Register(
		TEXT("script.validate"),
		TEXT("Validate an AI edit script without modifying the scene."),
		[](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
		{
			TSharedPtr<FJsonObject> Script;
			FString Error;
			if (!GetScriptParam(Params, Script, Error))
			{
				return FEpicodeCommandResult::Failure(TEXT("SCRIPT_INVALID"), Error);
			}
			FValidation Validation;
			if (!Validate(Script, Validation, Error))
			{
				return FEpicodeCommandResult::Failure(TEXT("SCRIPT_INVALID"), Error);
			}
			return FEpicodeCommandResult::Success(Validation.ToJson());
		},
		false);

	Registry.Register(
		TEXT("script.apply"),
		TEXT("Validate and apply an AI edit script in one undoable transaction."),
		[](const TSharedPtr<FJsonObject>& Params, const FEpicodeCommandContext& Context)
		{
			TSharedPtr<FJsonObject> Script;
			FString Error;
			if (!GetScriptParam(Params, Script, Error))
			{
				return FEpicodeCommandResult::Failure(TEXT("SCRIPT_INVALID"), Error);
			}
			FValidation Validation;
			if (!Validate(Script, Validation, Error) || !Validation.bValid)
			{
				FString Detail;
				if (Validation.Errors.Num() > 0)
				{
					const TSharedPtr<FJsonObject> FirstError = Validation.Errors[0]->AsObject();
					FirstError->TryGetStringField(TEXT("message"), Detail);
				}
				FString AuditWarning;
				WriteAudit(Params, Script, Validation.OperationCount, Validation.AffectedCount,
					false, TEXT("SCRIPT_INVALID"), 0.0, false, AuditWarning);
				return FEpicodeCommandResult::Failure(
					TEXT("SCRIPT_INVALID"),
					Detail.IsEmpty() ? TEXT("Script validation failed.") : Detail,
					Detail);
			}

			bool bConfirmed = false;
			Params.IsValid() && Params->TryGetBoolField(TEXT("confirmed"), bConfirmed);
			if (Validation.bRequiresConfirmation && !bConfirmed)
			{
				return FEpicodeCommandResult::Failure(
					TEXT("CONFIRMATION_REQUIRED"),
					TEXT("This script requires explicit user confirmation."));
			}

			FEpicodeCommandResult Result = ApplyValidatedScript(
				Script, Validation, Params, Context, bConfirmed);
			if (!Result.bSuccess)
			{
				FString AuditWarning;
				WriteAudit(Params, Script, Validation.OperationCount, Validation.AffectedCount,
					false, Result.ErrorCode, 0.0, bConfirmed, AuditWarning);
				if (!AuditWarning.IsEmpty())
				{
					Result.Warnings.Add(AuditWarning);
				}
			}
			return Result;
		});
}
