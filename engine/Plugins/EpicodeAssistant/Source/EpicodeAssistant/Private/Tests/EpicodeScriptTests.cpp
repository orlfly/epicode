#include "Misc/AutomationTest.h"

#include "Dom/JsonObject.h"
#include "EpicodeCommand.h"
#include "EpicodeEngineSubsystem.h"
#include "EpicodeSceneSubsystem.h"
#include "EpicodeScript.h"
#include "EpicodeTypes.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	TSharedPtr<FJsonObject> MakeScript(const TArray<TSharedPtr<FJsonObject>>& Operations)
	{
		TSharedRef<FJsonObject> Script = MakeShared<FJsonObject>();
		Script->SetNumberField(TEXT("schemaVersion"), 1);
		Script->SetStringField(TEXT("summary"), TEXT("脚本测试"));
		Script->SetBoolField(TEXT("requiresConfirmation"), false);
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const TSharedPtr<FJsonObject>& Operation : Operations)
		{
			Values.Add(MakeShared<FJsonValueObject>(Operation));
		}
		Script->SetArrayField(TEXT("operations"), Values);
		return Script;
	}

	TSharedPtr<FJsonObject> MakeOperation(
		const FString& Method,
		const TSharedPtr<FJsonObject>& Params,
		const FString& ClientId = FString())
	{
		TSharedRef<FJsonObject> Operation = MakeShared<FJsonObject>();
		Operation->SetStringField(TEXT("method"), Method);
		Operation->SetObjectField(TEXT("params"), Params);
		if (!ClientId.IsEmpty())
		{
			Operation->SetStringField(TEXT("clientId"), ClientId);
		}
		return Operation;
	}

	TSharedPtr<FJsonObject> MakeTransformParams(const FString& Id, const FVector& Location)
	{
		TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("id"), Id);
		TSharedRef<FJsonObject> Transform = MakeShared<FJsonObject>();
		Transform->SetArrayField(TEXT("location"), {
			MakeShared<FJsonValueNumber>(Location.X),
			MakeShared<FJsonValueNumber>(Location.Y),
			MakeShared<FJsonValueNumber>(Location.Z) });
		Transform->SetArrayField(TEXT("rotation"), {
			MakeShared<FJsonValueNumber>(0.0),
			MakeShared<FJsonValueNumber>(0.0),
			MakeShared<FJsonValueNumber>(0.0) });
		Transform->SetArrayField(TEXT("scale"), {
			MakeShared<FJsonValueNumber>(1.0),
			MakeShared<FJsonValueNumber>(1.0),
			MakeShared<FJsonValueNumber>(1.0) });
		Params->SetObjectField(TEXT("transform"), Transform);
		return Params;
	}

	bool StartScriptScene(UEpicodeSceneSubsystem* Scene, const FString& Name)
	{
		if (!Scene)
		{
			return false;
		}
		FString Error;
		if (Scene->HasScene())
		{
			Scene->CloseScene(true, Error);
		}
		return Scene->NewScene(Name, Error);
	}

	FString AuditFilePath()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Epicode/Script/audit.jsonl"));
	}

	struct FTestTransactionSink final : IEpicodeTransactionSink
	{
		int32 Begins = 0;
		int32 Commits = 0;
		int32 Cancels = 0;

		virtual void BeginTransaction(const FString& Description) override { ++Begins; }
		virtual void CommitTransaction() override { ++Commits; }
		virtual void CancelTransaction() override { ++Cancels; }
		virtual void MarkModified(UObject* Object) override {}
	};

	TSharedPtr<FJsonObject> MakeLightScript(const FString& ClientId)
	{
		TSharedPtr<FJsonObject> LightParams = MakeShared<FJsonObject>();
		LightParams->SetStringField(TEXT("lightType"), TEXT("point"));
		LightParams->SetStringField(TEXT("name"), TEXT("Script Undo Lamp"));
		return MakeScript({
			MakeOperation(TEXT("light.create"), LightParams, ClientId),
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEpicodeScriptValidateTest,
	"Epicode.Script.Validate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeScriptValidateTest::RunTest(const FString& Parameters)
{
	UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	if (!Scene || !Engine || !StartScriptScene(Scene, TEXT("Script Validate Test")))
	{
		AddError(TEXT("scene and engine subsystems are required"));
		return false;
	}

	TSharedPtr<FJsonObject> LightParams = MakeShared<FJsonObject>();
	LightParams->SetStringField(TEXT("lightType"), TEXT("point"));
	LightParams->SetStringField(TEXT("name"), TEXT("Script Lamp"));
	TSharedPtr<FJsonObject> CameraParams = MakeShared<FJsonObject>();
	CameraParams->SetStringField(TEXT("name"), TEXT("Script Camera"));
	CameraParams->SetNumberField(TEXT("fieldOfView"), 40.0);
	TSharedPtr<FJsonObject> ActivateParams = MakeShared<FJsonObject>();
	ActivateParams->SetStringField(TEXT("id"), TEXT("$cam"));

	TSharedPtr<FJsonObject> Script = MakeScript({
		MakeOperation(TEXT("light.create"), LightParams, TEXT("lamp")),
		MakeOperation(TEXT("entity.setTransform"), MakeTransformParams(TEXT("$lamp"), FVector(10, 20, 30)), TEXT("move")),
		MakeOperation(TEXT("camera.create"), CameraParams, TEXT("cam")),
		MakeOperation(TEXT("camera.activate"), ActivateParams),
	});

	FEpicodeScriptService::FValidation Validation;
	FString Error;
	TestTrue(TEXT("validate returns"), FEpicodeScriptService::Validate(Script, Validation, Error));
	TestTrue(TEXT("valid script"), Validation.bValid);
	TestEqual(TEXT("operation count"), Validation.OperationCount, 4);
	TestTrue(TEXT("confirmation required"), Validation.bRequiresConfirmation);
	TestEqual(TEXT("preview count"), Validation.PreviewOperations.Num(), 4);
	TestTrue(TEXT("normalized summary"),
		Validation.NormalizedScript.IsValid()
		&& Validation.NormalizedScript->GetStringField(TEXT("summary")) == TEXT("脚本测试"));

	TSharedPtr<FJsonObject> ValidateParams = MakeShared<FJsonObject>();
	ValidateParams->SetObjectField(TEXT("script"), Script);
	const FEpicodeCommandResult CommandResult = Engine->ExecuteCommand(
		TEXT("script.validate"), ValidateParams, FEpicodeCommandContext());
	TestTrue(TEXT("script.validate command"), CommandResult.bSuccess);
	TestTrue(TEXT("command valid"), CommandResult.Result.IsValid()
		&& CommandResult.Result->GetBoolField(TEXT("valid")));

	TSharedPtr<FJsonObject> InvalidRef = MakeShared<FJsonObject>();
	InvalidRef->SetStringField(TEXT("id"), TEXT("$does-not-exist"));
	TSharedPtr<FJsonObject> Invalid = MakeScript({
		MakeOperation(TEXT("import.file"), MakeShared<FJsonObject>()),
		MakeOperation(TEXT("entity.setVisible"), InvalidRef),
	});
	TestTrue(TEXT("invalid validate returns"), FEpicodeScriptService::Validate(Invalid, Validation, Error));
	TestFalse(TEXT("invalid script rejected"), Validation.bValid);
	TestTrue(TEXT("method/reference errors"), Validation.Errors.Num() >= 2);

	TSharedPtr<FJsonObject> AssetParams = MakeShared<FJsonObject>();
	AssetParams->SetStringField(TEXT("staticMesh"), TEXT("/Engine/EngineMeshes/Cube.Cube"));
	TSharedPtr<FJsonObject> AssetScript = MakeScript({
		MakeOperation(TEXT("prop.spawn"), AssetParams),
	});
	TestTrue(TEXT("asset validate returns"), FEpicodeScriptService::Validate(AssetScript, Validation, Error));
	TestFalse(TEXT("non-Game asset rejected"), Validation.bValid);

	const FString Extracted = FEpicodeScriptService::ExtractJsonObject(
		TEXT("说明文字\n```json\n{\"schemaVersion\":1,\"operations\":[]}\n```\n后续"));
	TestTrue(TEXT("fenced JSON extracted"), Extracted.StartsWith(TEXT("{")));
	TestTrue(TEXT("fenced JSON complete"), Extracted.EndsWith(TEXT("}")));

	FString CloseError;
	Scene->CloseScene(true, CloseError);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEpicodeScriptReferenceTest,
	"Epicode.Script.Reference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeScriptReferenceTest::RunTest(const FString& Parameters)
{
	UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
	if (!Scene || !StartScriptScene(Scene, TEXT("Script Reference Test")))
	{
		AddError(TEXT("scene is required"));
		return false;
	}

	TSharedPtr<FJsonObject> LaterParams = MakeShared<FJsonObject>();
	LaterParams->SetStringField(TEXT("name"), TEXT("Later Lamp"));
	TSharedPtr<FJsonObject> Forward = MakeScript({
		MakeOperation(TEXT("entity.setTransform"),
			MakeTransformParams(TEXT("$later"), FVector(1, 2, 3))),
		MakeOperation(TEXT("light.create"), LaterParams, TEXT("later")),
	});
	FEpicodeScriptService::FValidation Validation;
	FString Error;
	TestTrue(TEXT("forward reference validates"),
		FEpicodeScriptService::Validate(Forward, Validation, Error));
	TestFalse(TEXT("forward reference rejected"), Validation.bValid);
	TestTrue(TEXT("unknown client error"),
		Validation.Errors.Num() > 0
		&& Validation.Errors[0]->AsObject()->GetStringField(TEXT("code"))
			== TEXT("UNKNOWN_CLIENT_ID"));

	TSharedPtr<FJsonObject> Duplicate = MakeScript({
		MakeOperation(TEXT("light.create"), LaterParams, TEXT("same")),
		MakeOperation(TEXT("light.create"), LaterParams, TEXT("same")),
	});
	TestTrue(TEXT("duplicate validates"),
		FEpicodeScriptService::Validate(Duplicate, Validation, Error));
	TestFalse(TEXT("duplicate client rejected"), Validation.bValid);

	FString CloseError;
	Scene->CloseScene(true, CloseError);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEpicodeScriptApplyTest,
	"Epicode.Script.Apply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeScriptApplyTest::RunTest(const FString& Parameters)
{
	const FString AuditPath = AuditFilePath();
	FString OriginalAudit;
	const bool bHadAudit = FFileHelper::LoadFileToString(OriginalAudit, *AuditPath);
	UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	if (!Scene || !Engine || !StartScriptScene(Scene, TEXT("Script Apply Test")))
	{
		AddError(TEXT("scene and engine subsystems are required"));
		return false;
	}

	TSharedPtr<FJsonObject> LightParams = MakeShared<FJsonObject>();
	LightParams->SetStringField(TEXT("lightType"), TEXT("spot"));
	LightParams->SetStringField(TEXT("name"), TEXT("Applied Lamp"));
	TSharedPtr<FJsonObject> RenameParams = MakeShared<FJsonObject>();
	RenameParams->SetStringField(TEXT("id"), TEXT("$lamp"));
	RenameParams->SetStringField(TEXT("name"), TEXT("Renamed Lamp"));

	TSharedPtr<FJsonObject> Script = MakeScript({
		MakeOperation(TEXT("light.create"), LightParams, TEXT("lamp")),
		MakeOperation(TEXT("entity.setTransform"),
			MakeTransformParams(TEXT("$lamp"), FVector(11, 22, 33)), TEXT("move")),
		MakeOperation(TEXT("entity.rename"), RenameParams),
	});

	TSharedPtr<FJsonObject> ApplyParams = MakeShared<FJsonObject>();
	ApplyParams->SetObjectField(TEXT("script"), Script);
	ApplyParams->SetBoolField(TEXT("confirmed"), true);
	ApplyParams->SetStringField(TEXT("conversationId"), TEXT("script-test-conversation"));
	ApplyParams->SetStringField(TEXT("model"), TEXT("deepseek-chat"));
	ApplyParams->SetStringField(TEXT("backend"), TEXT("mock"));

	const int32 Before = Scene->GetDocument().Entities.Num();
	const FEpicodeCommandResult Applied = Engine->ExecuteCommand(
		TEXT("script.apply"), ApplyParams, FEpicodeCommandContext());
	TestTrue(TEXT("script applied"), Applied.bSuccess);
	TestEqual(TEXT("one new entity"), Scene->GetDocument().Entities.Num(), Before + 1);

	FGuid LampId;
	if (Applied.Result.IsValid() && Applied.Result->HasField(TEXT("clientIds")))
	{
		FString IdString;
		if (Applied.Result->GetObjectField(TEXT("clientIds"))->TryGetStringField(TEXT("lamp"), IdString))
		{
			FGuid::Parse(IdString, LampId);
		}
	}
	TestTrue(TEXT("lamp id is valid"), LampId.IsValid());
	if (const FEpicodeEntityRecord* Entity = Scene->FindEntity(LampId))
	{
		TestEqual(TEXT("renamed lamp"), Entity->Name, FString(TEXT("Renamed Lamp")));
		TestTrue(TEXT("transformed lamp"), Entity->Transform.Location.Equals(FVector(11, 22, 33), 0.01));
	}
	else
	{
		AddError(TEXT("applied lamp entity missing"));
	}

	FString Audit;
	TestTrue(TEXT("audit file written"), FFileHelper::LoadFileToString(Audit, *AuditPath));
	TestTrue(TEXT("audit contains conversation"), Audit.Contains(TEXT("script-test-conversation")));
	TestTrue(TEXT("audit records success"), Audit.Contains(TEXT("\"success\":true")));

	// A failure after the first operation must roll back the whole script.
	TSharedPtr<FJsonObject> BadParams = MakeShared<FJsonObject>();
	BadParams->SetStringField(TEXT("id"), TEXT("$lamp"));
	TSharedPtr<FJsonObject> BadScript = MakeScript({
		MakeOperation(TEXT("light.create"), LightParams, TEXT("lamp2")),
		MakeOperation(TEXT("camera.activate"), BadParams),
	});
	TSharedPtr<FJsonObject> BadApply = MakeShared<FJsonObject>();
	BadApply->SetObjectField(TEXT("script"), BadScript);
	BadApply->SetBoolField(TEXT("confirmed"), true);
	const int32 BeforeFailure = Scene->GetDocument().Entities.Num();
	const FEpicodeCommandResult Failed = Engine->ExecuteCommand(
		TEXT("script.apply"), BadApply, FEpicodeCommandContext());
	TestFalse(TEXT("invalid operation fails"), Failed.bSuccess);
	TestEqual(TEXT("failed script rolled back"), Scene->GetDocument().Entities.Num(), BeforeFailure);

	FString CloseError;
	Scene->CloseScene(true, CloseError);
	if (bHadAudit)
	{
		FFileHelper::SaveStringToFile(OriginalAudit, *AuditPath);
	}
	else
	{
		IFileManager::Get().Delete(*AuditPath, false, true, true);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEpicodeScriptUndoTest,
	"Epicode.Script.Undo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeScriptUndoTest::RunTest(const FString& Parameters)
{
	UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	if (!Scene || !Engine || !StartScriptScene(Scene, TEXT("Script Undo Test")))
	{
		AddError(TEXT("scene and engine subsystems are required"));
		return false;
	}

	IEpicodeTransactionSink* PreviousSink = Engine->GetTransactionSink();
	FTestTransactionSink Sink;
	Engine->SetTransactionSink(&Sink);

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetObjectField(TEXT("script"), MakeLightScript(TEXT("undoLamp")));
	const int32 Before = Scene->GetDocument().Entities.Num();
	FEpicodeCommandContext Context;
	Context.bUndoable = true;
	const FEpicodeCommandResult Result = Engine->ExecuteCommand(
		TEXT("script.apply"), Params, Context);
	TestTrue(TEXT("undoable script applied"), Result.bSuccess);
	TestEqual(TEXT("one transaction begun"), Sink.Begins, 1);
	TestEqual(TEXT("one transaction committed"), Sink.Commits, 1);
	TestEqual(TEXT("no transaction cancelled"), Sink.Cancels, 0);
	TestEqual(TEXT("entity created"), Scene->GetDocument().Entities.Num(), Before + 1);

	Engine->SetTransactionSink(PreviousSink);
	FString CloseError;
	Scene->CloseScene(true, CloseError);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEpicodeScriptAuditTest,
	"Epicode.Script.Audit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeScriptAuditTest::RunTest(const FString& Parameters)
{
	const FString AuditPath = AuditFilePath();
	FString OriginalAudit;
	const bool bHadAudit = FFileHelper::LoadFileToString(OriginalAudit, *AuditPath);
	UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	if (!Scene || !Engine || !StartScriptScene(Scene, TEXT("Script Audit Test")))
	{
		AddError(TEXT("scene and engine subsystems are required"));
		return false;
	}

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetObjectField(TEXT("script"), MakeLightScript(TEXT("auditLamp")));
	Params->SetBoolField(TEXT("confirmed"), true);
	Params->SetStringField(TEXT("conversationId"), TEXT("audit-conversation"));
	Params->SetStringField(TEXT("model"), TEXT("deepseek-chat"));
	Params->SetStringField(TEXT("backend"), TEXT("mock"));
	const FEpicodeCommandResult Result = Engine->ExecuteCommand(
		TEXT("script.apply"), Params, FEpicodeCommandContext());
	TestTrue(TEXT("audit script applied"), Result.bSuccess);

	FString Audit;
	TestTrue(TEXT("audit file loaded"), FFileHelper::LoadFileToString(Audit, *AuditPath));
	TArray<FString> Lines;
	Audit.ParseIntoArrayLines(Lines);
	TestTrue(TEXT("audit has line"), Lines.Num() > 0);
	TSharedPtr<FJsonObject> Entry;
	FString LastLine;
	for (int32 Index = Lines.Num() - 1; Index >= 0; --Index)
	{
		if (!Lines[Index].TrimStartAndEnd().IsEmpty())
		{
			LastLine = Lines[Index];
			break;
		}
	}
	if (!LastLine.IsEmpty())
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(LastLine);
		TestTrue(TEXT("audit line is JSON"),
			FJsonSerializer::Deserialize(Reader, Entry) && Entry.IsValid());
	}
	if (Entry.IsValid())
	{
		FString ConversationId;
		FString Backend;
		FString ErrorCode;
		FString Summary;
		int32 OperationCount = 0;
		double DurationMs = -1.0;
		bool bSuccess = false;
		bool bConfirmed = false;
		Entry->TryGetStringField(TEXT("conversationId"), ConversationId);
		Entry->TryGetStringField(TEXT("backend"), Backend);
		Entry->TryGetStringField(TEXT("errorCode"), ErrorCode);
		Entry->TryGetStringField(TEXT("summary"), Summary);
		Entry->TryGetNumberField(TEXT("operationCount"), OperationCount);
		Entry->TryGetNumberField(TEXT("durationMs"), DurationMs);
		Entry->TryGetBoolField(TEXT("success"), bSuccess);
		Entry->TryGetBoolField(TEXT("confirmed"), bConfirmed);
		TestEqual(TEXT("audit conversation"), ConversationId, FString(TEXT("audit-conversation")));
		TestEqual(TEXT("audit backend"), Backend, FString(TEXT("mock")));
		TestEqual(TEXT("audit operation count"), OperationCount, 1);
		TestEqual(TEXT("audit summary"), Summary, FString(TEXT("脚本测试")));
		TestTrue(TEXT("audit success"), bSuccess);
		TestTrue(TEXT("audit confirmed"), bConfirmed);
		TestTrue(TEXT("audit no error"), ErrorCode.IsEmpty());
		TestTrue(TEXT("audit duration"), DurationMs >= 0.0);
	}

	FString CloseError;
	Scene->CloseScene(true, CloseError);
	if (bHadAudit)
	{
		FFileHelper::SaveStringToFile(OriginalAudit, *AuditPath);
	}
	else
	{
		IFileManager::Get().Delete(*AuditPath, false, true, true);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEpicodeScriptSafetyTest,
	"Epicode.Script.Safety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeScriptSafetyTest::RunTest(const FString& Parameters)
{
	UEpicodeSceneSubsystem* Scene = UEpicodeSceneSubsystem::GetPrimary();
	UEpicodeEngineSubsystem* Engine = UEpicodeEngineSubsystem::Get();
	if (!Scene || !Engine || !StartScriptScene(Scene, TEXT("Script Safety Test")))
	{
		AddError(TEXT("scene and engine subsystems are required"));
		return false;
	}

	TSharedPtr<FJsonObject> CameraParams = MakeShared<FJsonObject>();
	CameraParams->SetStringField(TEXT("name"), TEXT("Safety Camera"));
	TSharedPtr<FJsonObject> ActivateParams = MakeShared<FJsonObject>();
	ActivateParams->SetStringField(TEXT("id"), TEXT("$cam"));
	TSharedPtr<FJsonObject> Script = MakeScript({
		MakeOperation(TEXT("camera.create"), CameraParams, TEXT("cam")),
		MakeOperation(TEXT("camera.activate"), ActivateParams),
	});

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetObjectField(TEXT("script"), Script);
	const int32 Before = Scene->GetDocument().Entities.Num();
	const FEpicodeCommandResult Unconfirmed = Engine->ExecuteCommand(
		TEXT("script.apply"), Params, FEpicodeCommandContext());
	TestFalse(TEXT("confirmation required"), Unconfirmed.bSuccess);
	TestEqual(TEXT("unconfirmed error"), Unconfirmed.ErrorCode, FString(TEXT("CONFIRMATION_REQUIRED")));
	TestEqual(TEXT("unconfirmed scene unchanged"), Scene->GetDocument().Entities.Num(), Before);

	TArray<TSharedPtr<FJsonObject>> TooMany;
	for (int32 Index = 0; Index < 51; ++Index)
	{
		TSharedPtr<FJsonObject> Camera = MakeShared<FJsonObject>();
		Camera->SetStringField(TEXT("name"), FString::Printf(TEXT("Cam %d"), Index));
		TooMany.Add(MakeOperation(TEXT("camera.create"), Camera));
	}
	FEpicodeScriptService::FValidation Validation;
	FString Error;
	TestTrue(TEXT("too many validate"),
		FEpicodeScriptService::Validate(MakeScript(TooMany), Validation, Error));
	TestFalse(TEXT("too many rejected"), Validation.bValid);

	FString CloseError;
	Scene->CloseScene(true, CloseError);
	return true;
}

#endif
