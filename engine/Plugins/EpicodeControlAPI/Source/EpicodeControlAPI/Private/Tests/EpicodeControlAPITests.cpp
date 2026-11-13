#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "EpicodeCommandEnvelope.h"
#include "EpicodeEventFilter.h"
#include "EpicodeParticipantBinding.h"
#include "EpicodeAgentAuditLog.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeAPIParticipantBindingTest, "Epicode.API.ParticipantBinding", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeAPIParticipantBindingTest::RunTest(const FString& Parameters)
{
	FEpicodeCommandContext Context;
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	FString Code;
	FString Message;

	TestFalse(TEXT("missing token rejected"), EpicodeParticipantBinding::Bind(FString(), Params, Context, Code, Message));
	TestEqual(TEXT("missing token code"), Code, FString(TEXT("PARTICIPANT_NOT_BOUND")));

	TestFalse(TEXT("forged token rejected"), EpicodeParticipantBinding::BindWithResolver(
		TEXT("forged"),
		Params,
		Context,
		Code,
		Message,
		[](const FString&, FString&, FString& OutError)
		{
			OutError = TEXT("PARTICIPANT_NOT_BOUND: participant token is missing or invalid.");
			return false;
		}));
	TestEqual(TEXT("forged token code"), Code, FString(TEXT("PARTICIPANT_NOT_BOUND")));

	TestFalse(TEXT("viewer binding rejected"), EpicodeParticipantBinding::BindWithResolver(
		TEXT("viewer-token"),
		Params,
		Context,
		Code,
		Message,
		[](const FString&, FString&, FString& OutError)
		{
			OutError = TEXT("FORBIDDEN: participant token is not bound to a connected agent.");
			return false;
		}));
	TestEqual(TEXT("viewer binding code"), Code, FString(TEXT("FORBIDDEN")));
	TestTrue(TEXT("viewer binding message"), Message.Contains(TEXT("connected agent")));

	Context = FEpicodeCommandContext();
	Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("playerId"), TEXT("agent-other"));
	TestFalse(TEXT("cross participant action rejected"), EpicodeParticipantBinding::BindWithResolver(
		TEXT("agent-token"),
		Params,
		Context,
		Code,
		Message,
		[](const FString& Token, FString& OutPlayerId, FString&)
		{
			if (Token != TEXT("agent-token"))
			{
				return false;
			}
			OutPlayerId = TEXT("agent-7");
			return true;
		}));
	TestEqual(TEXT("cross participant code"), Code, FString(TEXT("FORBIDDEN")));

	Context = FEpicodeCommandContext();
	Params = MakeShared<FJsonObject>();
	TestTrue(TEXT("valid token binds"), EpicodeParticipantBinding::BindWithResolver(
		TEXT("agent-token"),
		Params,
		Context,
		Code,
		Message,
		[](const FString& Token, FString& OutPlayerId, FString&)
		{
			if (Token != TEXT("agent-token"))
			{
				return false;
			}
			OutPlayerId = TEXT("agent-7");
			return true;
		}));
	TestEqual(TEXT("bound player injected"), Params->GetStringField(TEXT("playerId")), FString(TEXT("agent-7")));
	TestEqual(TEXT("bound requester injected"), Params->GetStringField(TEXT("requesterId")), FString(TEXT("agent-7")));
	TestEqual(TEXT("context token retained"), Context.ParticipantToken, FString(TEXT("agent-token")));

	Context = FEpicodeCommandContext();
	Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("requesterId"), TEXT("agent-7"));
	TestTrue(TEXT("matching requester binds"), EpicodeParticipantBinding::BindWithResolver(
		TEXT("agent-token"),
		Params,
		Context,
		Code,
		Message,
		[](const FString&, FString& OutPlayerId, FString&)
		{
			OutPlayerId = TEXT("agent-7");
			return true;
		}));
	TestEqual(TEXT("player injected from requester"), Params->GetStringField(TEXT("playerId")), FString(TEXT("agent-7")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeAPICommandEnvelopeTest, "Epicode.API.CommandEnvelope", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeAPICommandEnvelopeTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetNumberField(TEXT("stateVersion"), 42.0);
	Payload->SetBoolField(TEXT("accepted"), true);
	const FEpicodeCommandResult Success = FEpicodeCommandResult::Success(Payload, { TEXT("agent-7") });
	const TSharedPtr<FJsonObject> Envelope = EpicodeCommandEnvelope::Build(TEXT("character.moveTo"), TEXT("req-1"), Success, 200, false, 99);
	TestTrue(TEXT("ok flag"), Envelope->GetBoolField(TEXT("ok")));
	TestEqual(TEXT("request id"), Envelope->GetStringField(TEXT("requestId")), FString(TEXT("req-1")));
	TestEqual(TEXT("method"), Envelope->GetStringField(TEXT("method")), FString(TEXT("character.moveTo")));
	TestEqual(TEXT("state version"), Envelope->GetNumberField(TEXT("stateVersion")), 42.0);
	TestEqual(TEXT("event sequence"), Envelope->GetNumberField(TEXT("eventSequence")), 99.0);
	TestFalse(TEXT("not a replay"), Envelope->GetBoolField(TEXT("idempotentReplay")));
	TestTrue(TEXT("result object"), Envelope->GetObjectField(TEXT("result"))->GetBoolField(TEXT("accepted")));
	TestEqual(TEXT("affected ids"), Envelope->GetArrayField(TEXT("affected")).Num(), 1);

	const TSharedPtr<FJsonObject> Replay = EpicodeCommandEnvelope::Build(TEXT("character.stop"), TEXT("req-2"), Success, 200, true, 100);
	TestTrue(TEXT("replay flag"), Replay->GetBoolField(TEXT("idempotentReplay")));

	const FEpicodeCommandResult Stale = FEpicodeCommandResult::Failure(TEXT("STALE_STATE"), TEXT("version moved"), FString(), true);
	const TSharedPtr<FJsonObject> ErrorEnvelope = EpicodeCommandEnvelope::Build(TEXT("character.lookAt"), FString(), Stale, 409, false, 101);
	TestFalse(TEXT("error ok flag"), ErrorEnvelope->GetBoolField(TEXT("ok")));
	TestEqual(TEXT("error request fallback"), ErrorEnvelope->GetStringField(TEXT("requestId")), FString(TEXT("unknown")));
	TestEqual(TEXT("error code"), ErrorEnvelope->GetObjectField(TEXT("error"))->GetStringField(TEXT("code")), FString(TEXT("STALE_STATE")));
	TestTrue(TEXT("error retryable"), ErrorEnvelope->GetObjectField(TEXT("error"))->GetBoolField(TEXT("retryable")));
	TestEqual(TEXT("error http status"), ErrorEnvelope->GetNumberField(TEXT("httpStatus")), 409.0);

	TestTrue(TEXT("unpinned version matches"), EpicodeCommandEnvelope::MatchesStateVersion(-1.0, 77));
	TestTrue(TEXT("current version matches"), EpicodeCommandEnvelope::MatchesStateVersion(77.0, 77));
	TestFalse(TEXT("old version is stale"), EpicodeCommandEnvelope::MatchesStateVersion(76.0, 77));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeAPIEventFilterTest, "Epicode.API.EventFilters", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeAPIEventFilterTest::RunTest(const FString& Parameters)
{
	FEpicodeEvent MoveEvent;
	MoveEvent.Sequence = 10;
	MoveEvent.Type = TEXT("character.move.completed");
	MoveEvent.SceneId = TEXT("scene-1");
	MoveEvent.EntityId = TEXT("character-1");
	MoveEvent.Payload = MakeShared<FJsonObject>();
	MoveEvent.Payload->SetStringField(TEXT("playerId"), TEXT("agent-7"));
	MoveEvent.Payload->SetStringField(TEXT("characterId"), TEXT("character-1"));

	FEpicodeEvent ItemEvent;
	ItemEvent.Sequence = 11;
	ItemEvent.Type = TEXT("item.spawned");
	ItemEvent.SceneId = TEXT("scene-2");
	ItemEvent.EntityId = TEXT("item-1");
	ItemEvent.Payload = MakeShared<FJsonObject>();
	ItemEvent.Payload->SetStringField(TEXT("playerId"), TEXT("agent-8"));

	TestTrue(TEXT("empty filter accepts"), FEpicodeEventFilter().Matches(MoveEvent));
	TestTrue(TEXT("empty filter is empty"), FEpicodeEventFilter().IsEmpty());

	FEpicodeEventFilter CategoryFilter;
	CategoryFilter.Categories = { TEXT("character") };
	TestTrue(TEXT("category match"), CategoryFilter.Matches(MoveEvent));
	TestFalse(TEXT("category reject"), CategoryFilter.Matches(ItemEvent));

	FEpicodeEventFilter ParticipantFilter;
	ParticipantFilter.ParticipantId = TEXT("agent-7");
	TestTrue(TEXT("participant match"), ParticipantFilter.Matches(MoveEvent));
	TestFalse(TEXT("participant reject"), ParticipantFilter.Matches(ItemEvent));

	FEpicodeEventFilter CharacterFilter;
	CharacterFilter.CharacterId = TEXT("character-1");
	TestTrue(TEXT("entity character match"), CharacterFilter.Matches(MoveEvent));
	TestFalse(TEXT("entity character reject"), CharacterFilter.Matches(ItemEvent));

	FEpicodeEventFilter SessionFilter;
	SessionFilter.SessionId = TEXT("scene-2");
	TestFalse(TEXT("session reject"), SessionFilter.Matches(MoveEvent));
	TestTrue(TEXT("session match"), SessionFilter.Matches(ItemEvent));

	const FEpicodeEventFilter Parsed = FEpicodeEventFilter::FromQueryString(
		TEXT("/api/v1/ws?since=4&filter.session=scene-1&filter.participant=agent-7&filter.character=character-1&filter.category=character,agent"));
	TestEqual(TEXT("parsed session"), Parsed.SessionId, FString(TEXT("scene-1")));
	TestEqual(TEXT("parsed participant"), Parsed.ParticipantId, FString(TEXT("agent-7")));
	TestEqual(TEXT("parsed character"), Parsed.CharacterId, FString(TEXT("character-1")));
	TestEqual(TEXT("parsed categories"), Parsed.Categories.Num(), 2);
	TestTrue(TEXT("parsed filter matches"), Parsed.Matches(MoveEvent));

	const TSharedPtr<FJsonObject> Json = MoveEvent.ToJson();
	TestTrue(TEXT("json filter matches"), CharacterFilter.MatchesJson(Json));
	TestFalse(TEXT("json filter rejects"), CategoryFilter.MatchesJson(ItemEvent.ToJson()));

	TestFalse(TEXT("no gap for live tail"), FEpicodeEventFilter::NeedsGap(0, 10));
	TestFalse(TEXT("no gap when history retained"), FEpicodeEventFilter::NeedsGap(9, 10));
	TestTrue(TEXT("gap when history dropped"), FEpicodeEventFilter::NeedsGap(5, 10));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEpicodeAPIAgentAuditTest, "Epicode.API.AgentAudit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEpicodeAPIAgentAuditTest::RunTest(const FString& Parameters)
{
	FEpicodeAgentAuditEntry Entry;
	Entry.Method = TEXT("character.moveTo");
	Entry.RequestId = TEXT("req-audit");
	Entry.ParticipantId = TEXT("agent-7");
	Entry.Source = TEXT("http");
	Entry.bOk = false;
	Entry.HttpStatus = 403;
	Entry.ErrorCode = TEXT("FORBIDDEN");
	Entry.StateVersion = 12.0;
	Entry.EventSequence = 44;
	const FString Line = FEpicodeAgentAuditLog::BuildLine(Entry);
	TestTrue(TEXT("audit method"), Line.Contains(TEXT("character.moveTo")));
	TestTrue(TEXT("audit participant"), Line.Contains(TEXT("agent-7")));
	TestTrue(TEXT("audit error"), Line.Contains(TEXT("FORBIDDEN")));
	TestFalse(TEXT("audit omits token field"), Line.Contains(TEXT("participantToken")));
	TestFalse(TEXT("audit omits idempotency key"), Line.Contains(TEXT("idempotencyKey")));
	TestFalse(TEXT("audit omits secrets"), Line.Contains(TEXT("secret")));

	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("agent-audit-") + FGuid::NewGuid().ToString() + TEXT(".log"));
	IFileManager::Get().Delete(*Path, false, true, true);
	IFileManager::Get().Delete(*(Path + TEXT(".1")), false, true, true);
	const FString LongLine = FString::ChrN(600, TEXT('x'));
	TestTrue(TEXT("append first line"), FEpicodeAgentAuditLog::AppendRotating(Path, 1024, LongLine));
	TestTrue(TEXT("append rotated line"), FEpicodeAgentAuditLog::AppendRotating(Path, 1024, LongLine));
	TestTrue(TEXT("rotated file exists"), IFileManager::Get().FileExists(*(Path + TEXT(".1"))));
	TestTrue(TEXT("current file exists"), IFileManager::Get().FileExists(*Path));
	IFileManager::Get().Delete(*Path, false, true, true);
	IFileManager::Get().Delete(*(Path + TEXT(".1")), false, true, true);
	return true;
}

#endif
