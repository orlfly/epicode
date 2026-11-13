#include "EpicodeSandboxRules.h"

#include "Dom/JsonValue.h"
#include "EpicodeProductionValidation.h"
#include "EpicodeSandboxSettings.h"
#include "EpicodeSandboxWorld.h"

namespace
{
	bool ReadStringArray(const TSharedPtr<FJsonObject>& InObject, const TCHAR* InField, TArray<FString>& OutValues, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!InObject->TryGetArrayField(InField, Values) || !Values)
		{
			return true;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			if (Value->Type != EJson::String || Value->AsString().IsEmpty())
			{
				OutError = FString::Printf(TEXT("%s must contain non-empty strings"), InField);
				return false;
			}
			OutValues.Add(Value->AsString());
		}
		return true;
	}

	bool ReadDouble(const TSharedPtr<FJsonObject>& InObject, const TCHAR* InField, double& OutValue)
	{
		return InObject->TryGetNumberField(InField, OutValue);
	}

	bool ReadInt(const TSharedPtr<FJsonObject>& InObject, const TCHAR* InField, int32& OutValue)
	{
		return InObject->TryGetNumberField(InField, OutValue);
	}

	bool ReadVector(const TSharedPtr<FJsonObject>& InObject, const TCHAR* InField, FVector& OutValue)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!InObject->TryGetArrayField(InField, Values) || !Values || Values->Num() != 3)
		{
			return false;
		}
		OutValue = FVector((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
		return true;
	}

	bool GetObjectArray(const TSharedPtr<FJsonObject>& InObject, const TCHAR* InField, TArray<TSharedPtr<FJsonObject>>& OutValues, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!InObject->TryGetArrayField(InField, Values) || !Values)
		{
			return true;
		}
		for (int32 Index = 0; Index < Values->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject> Item = (*Values)[Index]->AsObject();
			if (!Item.IsValid())
			{
				OutError = FString::Printf(TEXT("%s[%d] must be an object"), InField, Index);
				return false;
			}
			OutValues.Add(Item);
		}
		return true;
	}

	bool ParseValueLiteral(FString InLiteral, FEpicodeSandboxValue& OutValue)
	{
		InLiteral = InLiteral.TrimStartAndEnd();
		if (InLiteral.Equals(TEXT("true"), ESearchCase::IgnoreCase))
		{
			OutValue = FEpicodeSandboxValue::MakeBool(true);
			return true;
		}
		if (InLiteral.Equals(TEXT("false"), ESearchCase::IgnoreCase))
		{
			OutValue = FEpicodeSandboxValue::MakeBool(false);
			return true;
		}
		if (InLiteral.Len() >= 2 && InLiteral.StartsWith(TEXT("\"")) && InLiteral.EndsWith(TEXT("\"")))
		{
			OutValue = FEpicodeSandboxValue::MakeString(InLiteral.Mid(1, InLiteral.Len() - 2));
			return true;
		}
		if (InLiteral.Len() >= 2 && InLiteral.StartsWith(TEXT("'")) && InLiteral.EndsWith(TEXT("'")))
		{
			OutValue = FEpicodeSandboxValue::MakeString(InLiteral.Mid(1, InLiteral.Len() - 2));
			return true;
		}
		if (InLiteral.IsNumeric())
		{
			if (InLiteral.Contains(TEXT(".")))
			{
				OutValue = FEpicodeSandboxValue::MakeNumber(FCString::Atof(*InLiteral));
			}
			else
			{
				OutValue = FEpicodeSandboxValue::MakeInt(FCString::Atoi64(*InLiteral));
			}
			return true;
		}
		if (!InLiteral.IsEmpty())
		{
			OutValue = FEpicodeSandboxValue::MakeString(InLiteral);
			return true;
		}
		return false;
	}

	bool CompareValues(const FEpicodeSandboxValue& InActual, EEpicodeSandboxCompareOp InOp, const FEpicodeSandboxValue& InExpected)
	{
		if (InActual.Type == EEpicodeSandboxValueType::String && InExpected.Type == EEpicodeSandboxValueType::String)
		{
			const int32 Comparison = InActual.StringValue.Compare(InExpected.StringValue, ESearchCase::CaseSensitive);
			switch (InOp)
			{
			case EEpicodeSandboxCompareOp::Equal: return Comparison == 0;
			case EEpicodeSandboxCompareOp::NotEqual: return Comparison != 0;
			case EEpicodeSandboxCompareOp::Less: return Comparison < 0;
			case EEpicodeSandboxCompareOp::LessEqual: return Comparison <= 0;
			case EEpicodeSandboxCompareOp::Greater: return Comparison > 0;
			case EEpicodeSandboxCompareOp::GreaterEqual: return Comparison >= 0;
			default: return false;
			}
		}
		const double Actual = InActual.AsNumber();
		const double Expected = InExpected.AsNumber();
		switch (InOp)
		{
		case EEpicodeSandboxCompareOp::Equal: return FMath::IsNearlyEqual(Actual, Expected);
		case EEpicodeSandboxCompareOp::NotEqual: return !FMath::IsNearlyEqual(Actual, Expected);
		case EEpicodeSandboxCompareOp::Less: return Actual < Expected;
		case EEpicodeSandboxCompareOp::LessEqual: return Actual <= Expected;
		case EEpicodeSandboxCompareOp::Greater: return Actual > Expected;
		case EEpicodeSandboxCompareOp::GreaterEqual: return Actual >= Expected;
		default: return false;
		}
	}

	bool FindOperator(const FString& InText, FString& OutLeft, FString& OutRight, EEpicodeSandboxCompareOp& OutOp)
	{
		const TCHAR* Operators[] = {TEXT(">="), TEXT("<="), TEXT("=="), TEXT("!="), TEXT(">"), TEXT("<")};
		const EEpicodeSandboxCompareOp Ops[] = {
			EEpicodeSandboxCompareOp::GreaterEqual,
			EEpicodeSandboxCompareOp::LessEqual,
			EEpicodeSandboxCompareOp::Equal,
			EEpicodeSandboxCompareOp::NotEqual,
			EEpicodeSandboxCompareOp::Greater,
			EEpicodeSandboxCompareOp::Less
		};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Ops); ++Index)
		{
			const int32 Position = InText.Find(Operators[Index]);
			if (Position != INDEX_NONE)
			{
				OutLeft = InText.Left(Position).TrimStartAndEnd();
				OutRight = InText.Mid(Position + FCString::Strlen(Operators[Index])).TrimStartAndEnd();
				OutOp = Ops[Index];
				return true;
			}
		}
		return false;
	}
}

bool FEpicodeSandboxCondition::Parse(const FString& InExpression, FEpicodeSandboxCondition& OutCondition, FString& OutError)
{
	OutCondition = FEpicodeSandboxCondition();
	FString Expression = InExpression.TrimStartAndEnd();
	if (Expression.StartsWith(TEXT("!")))
	{
		OutCondition.bNegate = true;
		Expression = Expression.Mid(1).TrimStartAndEnd();
	}
	if (Expression.IsEmpty())
	{
		OutError = TEXT("condition is empty");
		return false;
	}
	if (Expression.Equals(TEXT("true"), ESearchCase::IgnoreCase))
	{
		OutCondition.bAlwaysTrue = true;
		return true;
	}
	if (Expression.Equals(TEXT("false"), ESearchCase::IgnoreCase))
	{
		OutCondition.bAlwaysFalse = true;
		return true;
	}
	FString Left;
	FString Right;
	if (FindOperator(Expression, Left, Right, OutCondition.Op))
	{
		if (Left.IsEmpty() || !ParseValueLiteral(Right, OutCondition.Value))
		{
			OutError = FString::Printf(TEXT("invalid condition: %s"), *InExpression);
			return false;
		}
		OutCondition.Variable = Left;
		return true;
	}
	OutCondition.Variable = Expression;
	OutCondition.Op = EEpicodeSandboxCompareOp::NotEqual;
	OutCondition.Value = FEpicodeSandboxValue::MakeBool(false);
	return true;
}

bool FEpicodeSandboxCondition::Evaluate(const FEpicodeSandboxNarrativeState& InState) const
{
	if (bAlwaysTrue)
	{
		return !bNegate;
	}
	if (bAlwaysFalse)
	{
		return bNegate;
	}
	const FEpicodeSandboxValue* Actual = InState.FindVariable(Variable);
	if (!Actual)
	{
		return false;
	}
	const bool bResult = CompareValues(*Actual, Op, Value);
	return bNegate ? !bResult : bResult;
}

FString FEpicodeSandboxCondition::ToExpression() const
{
	if (bAlwaysTrue)
	{
		return bNegate ? TEXT("false") : TEXT("true");
	}
	if (bAlwaysFalse)
	{
		return bNegate ? TEXT("true") : TEXT("false");
	}
	FString Operator;
	switch (Op)
	{
	case EEpicodeSandboxCompareOp::Equal: Operator = TEXT("=="); break;
	case EEpicodeSandboxCompareOp::NotEqual: Operator = TEXT("!="); break;
	case EEpicodeSandboxCompareOp::Less: Operator = TEXT("<"); break;
	case EEpicodeSandboxCompareOp::LessEqual: Operator = TEXT("<="); break;
	case EEpicodeSandboxCompareOp::Greater: Operator = TEXT(">"); break;
	case EEpicodeSandboxCompareOp::GreaterEqual: Operator = TEXT(">="); break;
	}
	const FString Expression = Variable + TEXT(" ") + Operator + TEXT(" ") + Value.ToDisplayString();
	return bNegate ? TEXT("!") + Expression : Expression;
}

bool FEpicodeSandboxOutcome::Parse(const FString& InExpression, FEpicodeSandboxOutcome& OutOutcome, FString& OutError)
{
	OutOutcome = FEpicodeSandboxOutcome();
	FString Expression = InExpression.TrimStartAndEnd();
	int32 Colon = INDEX_NONE;
	if (!Expression.FindChar(TEXT(':'), Colon))
	{
		OutError = FString::Printf(TEXT("outcome must be action:key=value: %s"), *InExpression);
		return false;
	}
	OutOutcome.Action = Expression.Left(Colon).TrimStartAndEnd();
	FString Body = Expression.Mid(Colon + 1).TrimStartAndEnd();
	if (OutOutcome.Action.IsEmpty() || Body.IsEmpty())
	{
		OutError = FString::Printf(TEXT("outcome has empty action or key: %s"), *InExpression);
		return false;
	}
	int32 Equals = INDEX_NONE;
	if (Body.FindChar(TEXT('='), Equals))
	{
		OutOutcome.Key = Body.Left(Equals).TrimStartAndEnd();
		const FString ValueText = Body.Mid(Equals + 1).TrimStartAndEnd();
		if (!ParseValueLiteral(ValueText, OutOutcome.Value))
		{
			OutError = FString::Printf(TEXT("outcome value is invalid: %s"), *InExpression);
			return false;
		}
	}
	else
	{
		OutOutcome.Key = Body;
		OutOutcome.Value = FEpicodeSandboxValue::MakeBool(true);
	}
	return !OutOutcome.Key.IsEmpty();
}

bool FEpicodeSandboxRules::Parse(const TSharedPtr<FJsonObject>& InWorldRules, FEpicodeSandboxRules& OutRules, FString& OutError)
{
	OutRules = FEpicodeSandboxRules();
	if (!InWorldRules.IsValid())
	{
		return true;
	}

	TArray<TSharedPtr<FJsonObject>> Values;
	if (!GetObjectArray(InWorldRules, TEXT("interactionPoints"), Values, OutError))
	{
		return false;
	}
	for (const TSharedPtr<FJsonObject>& Value : Values)
	{
		FEpicodeSandboxInteractionPoint Interaction;
		if (!Value->TryGetStringField(TEXT("id"), Interaction.Id) || Interaction.Id.IsEmpty())
		{
			OutError = TEXT("RULE_ID_REQUIRED: interaction point id is empty");
			return false;
		}
		if (!Value->TryGetStringField(TEXT("entityId"), Interaction.EntityId) || Interaction.EntityId.IsEmpty())
		{
			OutError = FString::Printf(TEXT("RULE_REFERENCE_REQUIRED: interaction %s has no entityId"), *Interaction.Id);
			return false;
		}
		ReadStringArray(Value, TEXT("verbs"), Interaction.Verbs, OutError);
		ReadDouble(Value, TEXT("maxDistance"), Interaction.MaxDistance);
		ReadDouble(Value, TEXT("cooldownSeconds"), Interaction.CooldownSeconds);
		TArray<FString> Conditions;
		ReadStringArray(Value, TEXT("conditions"), Conditions, OutError);
		for (const FString& ConditionText : Conditions)
		{
			FEpicodeSandboxCondition Condition;
			if (!FEpicodeSandboxCondition::Parse(ConditionText, Condition, OutError))
			{
				return false;
			}
			Interaction.Conditions.Add(Condition);
		}
		TArray<FString> Outcomes;
		ReadStringArray(Value, TEXT("outcomes"), Outcomes, OutError);
		for (const FString& OutcomeText : Outcomes)
		{
			FEpicodeSandboxOutcome Outcome;
			if (!FEpicodeSandboxOutcome::Parse(OutcomeText, Outcome, OutError))
			{
				return false;
			}
			Interaction.Outcomes.Add(Outcome);
		}
		OutRules.Interactions.Add(Interaction);
	}

	Values.Reset();
	if (!GetObjectArray(InWorldRules, TEXT("zones"), Values, OutError))
	{
		return false;
	}
	for (const TSharedPtr<FJsonObject>& Value : Values)
	{
		FEpicodeSandboxZone Zone;
		if (!Value->TryGetStringField(TEXT("id"), Zone.Id) || Zone.Id.IsEmpty())
		{
			OutError = TEXT("RULE_ID_REQUIRED: zone id is empty");
			return false;
		}
		Value->TryGetStringField(TEXT("name"), Zone.Name);
		ReadInt(Value, TEXT("maxEntities"), Zone.MaxEntities);
		Zone.bHasBounds = ReadVector(Value, TEXT("center"), Zone.Center) && ReadVector(Value, TEXT("extent"), Zone.Extent);
		OutRules.Zones.Add(Zone);
	}

	Values.Reset();
	if (!GetObjectArray(InWorldRules, TEXT("spawnTables"), Values, OutError))
	{
		return false;
	}
	for (const TSharedPtr<FJsonObject>& Value : Values)
	{
		FEpicodeSandboxSpawnTable Table;
		if (!Value->TryGetStringField(TEXT("id"), Table.Id) || Table.Id.IsEmpty())
		{
			OutError = TEXT("RULE_ID_REQUIRED: spawn table id is empty");
			return false;
		}
		TArray<TSharedPtr<FJsonObject>> Entries;
		if (!GetObjectArray(Value, TEXT("entries"), Entries, OutError))
		{
			return false;
		}
		for (const TSharedPtr<FJsonObject>& EntryJson : Entries)
		{
			FEpicodeSandboxSpawnEntry Entry;
			FString Kind;
			if (!EntryJson->TryGetStringField(TEXT("entityType"), Kind))
			{
				EntryJson->TryGetStringField(TEXT("kind"), Kind);
			}
			if (!ParseSandboxEntityKind(Kind, Entry.Kind))
			{
				OutError = FString::Printf(TEXT("SPAWN_ENTRY_INVALID: %s has invalid entityType"), *Table.Id);
				return false;
			}
			if (!EntryJson->TryGetStringField(TEXT("archetypeId"), Entry.ArchetypeId))
			{
				EntryJson->TryGetStringField(TEXT("assetPath"), Entry.ArchetypeId);
			}
			ReadDouble(EntryJson, TEXT("weight"), Entry.Weight);
			if (Entry.Weight <= 0.0)
			{
				OutError = FString::Printf(TEXT("SPAWN_ENTRY_INVALID: %s entry weight must be positive"), *Table.Id);
				return false;
			}
			Table.Entries.Add(Entry);
		}
		OutRules.SpawnTables.Add(Table);
	}

	Values.Reset();
	if (!GetObjectArray(InWorldRules, TEXT("spawners"), Values, OutError))
	{
		return false;
	}
	for (const TSharedPtr<FJsonObject>& Value : Values)
	{
		FEpicodeSandboxSpawner Spawner;
		if (!Value->TryGetStringField(TEXT("id"), Spawner.Id) || Spawner.Id.IsEmpty())
		{
			OutError = TEXT("RULE_ID_REQUIRED: spawner id is empty");
			return false;
		}
		Value->TryGetStringField(TEXT("zoneId"), Spawner.ZoneId);
		Value->TryGetStringField(TEXT("spawnTableId"), Spawner.SpawnTableId);
		ReadDouble(Value, TEXT("intervalSeconds"), Spawner.IntervalSeconds);
		ReadInt(Value, TEXT("maxAlive"), Spawner.MaxAlive);
		OutRules.Spawners.Add(Spawner);
	}

	Values.Reset();
	if (!GetObjectArray(InWorldRules, TEXT("resources"), Values, OutError))
	{
		return false;
	}
	for (const TSharedPtr<FJsonObject>& Value : Values)
	{
		FEpicodeSandboxResourceRule Resource;
		if (!Value->TryGetStringField(TEXT("id"), Resource.Id) || Resource.Id.IsEmpty())
		{
			OutError = TEXT("RULE_ID_REQUIRED: resource id is empty");
			return false;
		}
		Value->TryGetStringField(TEXT("entityId"), Resource.EntityId);
		Value->TryGetStringField(TEXT("itemId"), Resource.ItemId);
		ReadInt(Value, TEXT("capacity"), Resource.Capacity);
		ReadDouble(Value, TEXT("respawnSeconds"), Resource.RespawnSeconds);
		OutRules.Resources.Add(Resource);
	}

	Values.Reset();
	if (!GetObjectArray(InWorldRules, TEXT("npcProfiles"), Values, OutError))
	{
		return false;
	}
	for (const TSharedPtr<FJsonObject>& Value : Values)
	{
		FEpicodeSandboxNpcProfile Profile;
		if (!Value->TryGetStringField(TEXT("id"), Profile.Id) || Profile.Id.IsEmpty())
		{
			OutError = TEXT("RULE_ID_REQUIRED: npc profile id is empty");
			return false;
		}
		Value->TryGetStringField(TEXT("behavior"), Profile.Behavior);
		Value->TryGetStringField(TEXT("targetTag"), Profile.TargetTag);
		ReadDouble(Value, TEXT("perceptionRadius"), Profile.PerceptionRadius);
		ReadDouble(Value, TEXT("speed"), Profile.Speed);
		const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
		if (Value->TryGetArrayField(TEXT("patrolPoints"), Points) && Points)
		{
			for (const TSharedPtr<FJsonValue>& Point : *Points)
			{
				const TSharedPtr<FJsonObject> PointObject = Point->AsObject();
				FVector Position;
				if (PointObject.IsValid() && ReadVector(PointObject, TEXT("location"), Position))
				{
					Profile.PatrolPoints.Add(Position);
				}
			}
		}
		OutRules.NpcProfiles.Add(Profile);
	}

	Values.Reset();
	if (!GetObjectArray(InWorldRules, TEXT("recipes"), Values, OutError))
	{
		return false;
	}
	for (const TSharedPtr<FJsonObject>& Value : Values)
	{
		FEpicodeSandboxRecipe Recipe;
		if (!Value->TryGetStringField(TEXT("id"), Recipe.Id) || Recipe.Id.IsEmpty())
		{
			OutError = TEXT("RULE_ID_REQUIRED: recipe id is empty");
			return false;
		}
		ReadDouble(Value, TEXT("durationSeconds"), Recipe.DurationSeconds);
		const TSharedPtr<FJsonObject>* Inputs = nullptr;
		if (Value->TryGetObjectField(TEXT("inputs"), Inputs) && Inputs)
		{
			for (const auto& Pair : (*Inputs)->Values)
			{
				Recipe.Inputs.Add(FString(*Pair.Key), static_cast<int32>(Pair.Value->AsNumber()));
			}
		}
		const TSharedPtr<FJsonObject>* Outputs = nullptr;
		if (Value->TryGetObjectField(TEXT("outputs"), Outputs) && Outputs)
		{
			for (const auto& Pair : (*Outputs)->Values)
			{
				Recipe.Outputs.Add(FString(*Pair.Key), static_cast<int32>(Pair.Value->AsNumber()));
			}
		}
		OutRules.Recipes.Add(Recipe);
	}

	const TSharedPtr<FJsonObject>* Budgets = nullptr;
	if (InWorldRules->TryGetObjectField(TEXT("budgets"), Budgets) && Budgets)
	{
		ReadInt(*Budgets, TEXT("maxEntities"), OutRules.Budget.MaxEntities);
		ReadInt(*Budgets, TEXT("maxEventsPerSecond"), OutRules.Budget.MaxEventsPerSecond);
		ReadInt(*Budgets, TEXT("maxSpawnPerSecond"), OutRules.Budget.MaxSpawnPerSecond);
		ReadInt(*Budgets, TEXT("maxDirectorCues"), OutRules.Budget.MaxDirectorCues);
	}
	return true;
}

bool FEpicodeSandboxRules::Validate(const FEpicodeSandboxWorld* InWorld, FString& OutError) const
{
	TSet<FString> Ids;
	auto CheckId = [&Ids, &OutError](const FString& InId, const FString& InKind) -> bool
	{
		if (InId.TrimStartAndEnd().IsEmpty())
		{
			OutError = FString::Printf(TEXT("RULE_ID_REQUIRED: %s id is empty"), *InKind);
			return false;
		}
		if (Ids.Contains(InId.ToLower()))
		{
			OutError = FString::Printf(TEXT("DUPLICATE_RULE_ID: %s (%s)"), *InId, *InKind);
			return false;
		}
		Ids.Add(InId.ToLower());
		return true;
	};

	for (const FEpicodeSandboxInteractionPoint& Interaction : Interactions)
	{
		if (!CheckId(Interaction.Id, TEXT("interaction")))
		{
			return false;
		}
		if (Interaction.Verbs.Num() == 0)
		{
			OutError = FString::Printf(TEXT("INTERACTION_VERB_REQUIRED: %s"), *Interaction.Id);
			return false;
		}
		if (Interaction.MaxDistance <= 0.0)
		{
			OutError = FString::Printf(TEXT("INTERACTION_DISTANCE_INVALID: %s"), *Interaction.Id);
			return false;
		}
		if (InWorld && !InWorld->FindEntity(Interaction.EntityId))
		{
			OutError = FString::Printf(TEXT("RULE_REFERENCE_NOT_FOUND: interaction %s entity %s"), *Interaction.Id, *Interaction.EntityId);
			return false;
		}
	}
	for (const FEpicodeSandboxZone& Zone : Zones)
	{
		if (!CheckId(Zone.Id, TEXT("zone")))
		{
			return false;
		}
		if (Zone.MaxEntities < 0)
		{
			OutError = FString::Printf(TEXT("ZONE_BUDGET_INVALID: %s"), *Zone.Id);
			return false;
		}
	}
	for (const FEpicodeSandboxSpawnTable& Table : SpawnTables)
	{
		if (!CheckId(Table.Id, TEXT("spawnTable")))
		{
			return false;
		}
		if (Table.Entries.Num() == 0)
		{
			OutError = FString::Printf(TEXT("SPAWN_TABLE_EMPTY: %s"), *Table.Id);
			return false;
		}
		for (const FEpicodeSandboxSpawnEntry& Entry : Table.Entries)
		{
			if (Entry.Weight <= 0.0)
			{
				OutError = FString::Printf(TEXT("SPAWN_ENTRY_INVALID: %s"), *Table.Id);
				return false;
			}
			if (!Entry.ArchetypeId.IsEmpty() && !FEpicodeProductionValidator::IsAllowedAssetPath(Entry.ArchetypeId))
			{
				OutError = FString::Printf(TEXT("PATH_NOT_ALLOWED: %s"), *Entry.ArchetypeId);
				return false;
			}
		}
	}
	for (const FEpicodeSandboxSpawner& Spawner : Spawners)
	{
		if (!CheckId(Spawner.Id, TEXT("spawner")))
		{
			return false;
		}
		if (!Spawner.ZoneId.IsEmpty() && !FindZone(Spawner.ZoneId))
		{
			OutError = FString::Printf(TEXT("RULE_REFERENCE_NOT_FOUND: spawner %s zone %s"), *Spawner.Id, *Spawner.ZoneId);
			return false;
		}
		if (!FindSpawnTable(Spawner.SpawnTableId))
		{
			OutError = FString::Printf(TEXT("RULE_REFERENCE_NOT_FOUND: spawner %s table %s"), *Spawner.Id, *Spawner.SpawnTableId);
			return false;
		}
		if (Spawner.IntervalSeconds <= 0.0 || Spawner.MaxAlive <= 0)
		{
			OutError = FString::Printf(TEXT("SPAWNER_INTERVAL_INVALID: %s"), *Spawner.Id);
			return false;
		}
		if (Budget.MaxSpawnPerSecond > 0 && (1.0 / Spawner.IntervalSeconds) > static_cast<double>(Budget.MaxSpawnPerSecond) + KINDA_SMALL_NUMBER)
		{
			OutError = FString::Printf(TEXT("PRODUCTION_BUDGET_EXCEEDED: spawner %s rate exceeds %d/s"), *Spawner.Id, Budget.MaxSpawnPerSecond);
			return false;
		}
	}
	for (const FEpicodeSandboxResourceRule& Resource : Resources)
	{
		if (!CheckId(Resource.Id, TEXT("resource")))
		{
			return false;
		}
		if (Resource.ItemId.IsEmpty() || Resource.Capacity <= 0)
		{
			OutError = FString::Printf(TEXT("RESOURCE_INVALID: %s"), *Resource.Id);
			return false;
		}
		if (InWorld && !InWorld->FindEntity(Resource.EntityId))
		{
			OutError = FString::Printf(TEXT("RULE_REFERENCE_NOT_FOUND: resource %s entity %s"), *Resource.Id, *Resource.EntityId);
			return false;
		}
	}
	for (const FEpicodeSandboxNpcProfile& Profile : NpcProfiles)
	{
		if (!CheckId(Profile.Id, TEXT("npcProfile")))
		{
			return false;
		}
		if (Profile.PerceptionRadius <= 0.0 || Profile.Speed < 0.0)
		{
			OutError = FString::Printf(TEXT("NPC_PROFILE_INVALID: %s"), *Profile.Id);
			return false;
		}
	}
	for (const FEpicodeSandboxRecipe& Recipe : Recipes)
	{
		if (!CheckId(Recipe.Id, TEXT("recipe")))
		{
			return false;
		}
		if (Recipe.Inputs.Num() == 0 || Recipe.Outputs.Num() == 0)
		{
			OutError = FString::Printf(TEXT("RECIPE_INVALID: %s"), *Recipe.Id);
			return false;
		}
		for (const TPair<FString, int32>& Pair : Recipe.Inputs)
		{
			if (Pair.Value <= 0)
			{
				OutError = FString::Printf(TEXT("RECIPE_INVALID: %s input %s"), *Recipe.Id, *Pair.Key);
				return false;
			}
		}
		for (const TPair<FString, int32>& Pair : Recipe.Outputs)
		{
			if (Pair.Value <= 0)
			{
				OutError = FString::Printf(TEXT("RECIPE_INVALID: %s output %s"), *Recipe.Id, *Pair.Key);
				return false;
			}
		}
	}

	if (Budget.MaxEntities <= 0 || Budget.MaxEventsPerSecond <= 0 || Budget.MaxSpawnPerSecond <= 0 || Budget.MaxDirectorCues <= 0)
	{
		OutError = TEXT("PRODUCTION_BUDGET_INVALID: budgets must be positive");
		return false;
	}
	if (const UEpicodeSandboxSettings* Settings = UEpicodeSandboxSettings::Get())
	{
		if (Budget.MaxEntities > Settings->MaxEntities || Budget.MaxEventsPerSecond > Settings->MaxEventsPerSecond || Budget.MaxDirectorCues > Settings->MaxDirectorCues)
		{
			OutError = TEXT("PRODUCTION_BUDGET_EXCEEDED: budgets exceed sandbox settings");
			return false;
		}
	}
	return true;
}

const FEpicodeSandboxInteractionPoint* FEpicodeSandboxRules::FindInteractionById(const FString& InId) const
{
	for (const FEpicodeSandboxInteractionPoint& Interaction : Interactions)
	{
		if (Interaction.Id.Equals(InId, ESearchCase::IgnoreCase))
		{
			return &Interaction;
		}
	}
	return nullptr;
}

const FEpicodeSandboxInteractionPoint* FEpicodeSandboxRules::FindInteractionByEntity(const FString& InEntityId) const
{
	for (const FEpicodeSandboxInteractionPoint& Interaction : Interactions)
	{
		if (Interaction.EntityId.Equals(InEntityId, ESearchCase::IgnoreCase))
		{
			return &Interaction;
		}
	}
	return nullptr;
}

const FEpicodeSandboxZone* FEpicodeSandboxRules::FindZone(const FString& InId) const
{
	for (const FEpicodeSandboxZone& Zone : Zones)
	{
		if (Zone.Id.Equals(InId, ESearchCase::IgnoreCase))
		{
			return &Zone;
		}
	}
	return nullptr;
}

const FEpicodeSandboxSpawnTable* FEpicodeSandboxRules::FindSpawnTable(const FString& InId) const
{
	for (const FEpicodeSandboxSpawnTable& Table : SpawnTables)
	{
		if (Table.Id.Equals(InId, ESearchCase::IgnoreCase))
		{
			return &Table;
		}
	}
	return nullptr;
}

const FEpicodeSandboxSpawnEntry* FEpicodeSandboxRules::PickSpawnEntry(const FEpicodeSandboxSpawnTable& InTable, double InRoll) const
{
	double TotalWeight = 0.0;
	for (const FEpicodeSandboxSpawnEntry& Entry : InTable.Entries)
	{
		TotalWeight += Entry.Weight;
	}
	if (TotalWeight <= 0.0)
	{
		return nullptr;
	}
	double Cursor = FMath::Clamp(InRoll, 0.0, 1.0) * TotalWeight;
	for (const FEpicodeSandboxSpawnEntry& Entry : InTable.Entries)
	{
		Cursor -= Entry.Weight;
		if (Cursor <= 0.0)
		{
			return &Entry;
		}
	}
	return &InTable.Entries.Last();
}

const FEpicodeSandboxResourceRule* FEpicodeSandboxRules::FindResourceByEntity(const FString& InEntityId) const
{
	for (const FEpicodeSandboxResourceRule& Resource : Resources)
	{
		if (Resource.EntityId.Equals(InEntityId, ESearchCase::IgnoreCase))
		{
			return &Resource;
		}
	}
	return nullptr;
}

const FEpicodeSandboxNpcProfile* FEpicodeSandboxRules::FindNpcProfile(const FString& InId) const
{
	for (const FEpicodeSandboxNpcProfile& Profile : NpcProfiles)
	{
		if (Profile.Id.Equals(InId, ESearchCase::IgnoreCase))
		{
			return &Profile;
		}
	}
	return nullptr;
}

const FEpicodeSandboxRecipe* FEpicodeSandboxRules::FindRecipe(const FString& InId) const
{
	for (const FEpicodeSandboxRecipe& Recipe : Recipes)
	{
		if (Recipe.Id.Equals(InId, ESearchCase::IgnoreCase))
		{
			return &Recipe;
		}
	}
	return nullptr;
}
