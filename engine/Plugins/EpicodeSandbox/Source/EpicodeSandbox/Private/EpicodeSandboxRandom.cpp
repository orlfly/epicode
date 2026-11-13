#include "EpicodeSandboxRandom.h"

#include "Dom/JsonValue.h"

void FEpicodeSandboxRandomStreams::Initialize(int32 InSeed)
{
	Reset(InSeed);
}

void FEpicodeSandboxRandomStreams::Reset(int32 InSeed)
{
	Seed = InSeed;
	Streams.Reset();
	Draws.Reset();
}

int32 FEpicodeSandboxRandomStreams::DeriveSeed(int32 InSeed, const FString& InDomain)
{
	const uint32 DomainHash = FCrc::StrCrc32(*InDomain.ToLower());
	return static_cast<int32>(static_cast<uint32>(InSeed) ^ DomainHash);
}

FRandomStream& FEpicodeSandboxRandomStreams::GetStream(const FString& InDomain)
{
	const FString Key = InDomain.ToLower();
	if (FRandomStream* Existing = Streams.Find(Key))
	{
		return *Existing;
	}
	FRandomStream& Stream = Streams.Add(Key, FRandomStream(DeriveSeed(Seed, Key)));
	return Stream;
}

double FEpicodeSandboxRandomStreams::GetFloat(const FString& InDomain, int64 InTick)
{
	const double Value = GetStream(InDomain).FRand();
	FEpicodeSandboxRandomDraw& Draw = Draws.AddDefaulted_GetRef();
	Draw.Tick = InTick;
	Draw.Domain = InDomain;
	Draw.Kind = TEXT("float");
	Draw.Value = Value;
	return Value;
}

int32 FEpicodeSandboxRandomStreams::GetInt(const FString& InDomain, int32 InMin, int32 InMax, int64 InTick)
{
	const int32 Low = FMath::Min(InMin, InMax);
	const int32 High = FMath::Max(InMin, InMax);
	const int32 Value = GetStream(InDomain).RandRange(Low, High);
	FEpicodeSandboxRandomDraw& Draw = Draws.AddDefaulted_GetRef();
	Draw.Tick = InTick;
	Draw.Domain = InDomain;
	Draw.Kind = TEXT("int");
	Draw.Value = static_cast<double>(Value);
	return Value;
}

bool FEpicodeSandboxRandomStreams::GetBool(const FString& InDomain, int64 InTick)
{
	const bool Value = GetStream(InDomain).FRand() < 0.5;
	FEpicodeSandboxRandomDraw& Draw = Draws.AddDefaulted_GetRef();
	Draw.Tick = InTick;
	Draw.Domain = InDomain;
	Draw.Kind = TEXT("bool");
	Draw.Value = Value ? 1.0 : 0.0;
	return Value;
}

TSharedPtr<FJsonObject> FEpicodeSandboxRandomStreams::GetDrawsJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("seed"), Seed);
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FEpicodeSandboxRandomDraw& Draw : Draws)
	{
		TSharedPtr<FJsonObject> DrawJson = MakeShared<FJsonObject>();
		DrawJson->SetNumberField(TEXT("tick"), static_cast<double>(Draw.Tick));
		DrawJson->SetStringField(TEXT("domain"), Draw.Domain);
		DrawJson->SetStringField(TEXT("kind"), Draw.Kind);
		DrawJson->SetNumberField(TEXT("value"), Draw.Value);
		Values.Add(MakeShared<FJsonValueObject>(DrawJson));
	}
	Result->SetArrayField(TEXT("draws"), Values);
	return Result;
}

TSharedPtr<FJsonObject> FEpicodeSandboxRandomStreams::ToJson() const
{
	TSharedPtr<FJsonObject> Result = GetDrawsJson();
	TArray<FString> Domains;
	Streams.GetKeys(Domains);
	Domains.Sort();
	TArray<TSharedPtr<FJsonValue>> StreamValues;
	for (const FString& Domain : Domains)
	{
		const FRandomStream& Stream = Streams.FindChecked(Domain);
		TSharedPtr<FJsonObject> StreamJson = MakeShared<FJsonObject>();
		StreamJson->SetStringField(TEXT("domain"), Domain);
		StreamJson->SetNumberField(TEXT("seed"), Stream.GetCurrentSeed());
		StreamValues.Add(MakeShared<FJsonValueObject>(StreamJson));
	}
	Result->SetArrayField(TEXT("streams"), StreamValues);
	return Result;
}

bool FEpicodeSandboxRandomStreams::FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeSandboxRandomStreams& OutStreams, FString& OutError)
{
	if (!InJson.IsValid())
	{
		OutError = TEXT("RANDOM_STATE_INVALID: missing object");
		return false;
	}
	int32 Seed = 0;
	if (!InJson->TryGetNumberField(TEXT("seed"), Seed))
	{
		OutError = TEXT("RANDOM_STATE_INVALID: missing seed");
		return false;
	}
	OutStreams.Reset(Seed);

	const TArray<TSharedPtr<FJsonValue>>* StreamValues = nullptr;
	if (InJson->TryGetArrayField(TEXT("streams"), StreamValues) && StreamValues)
	{
		for (const TSharedPtr<FJsonValue>& Value : *StreamValues)
		{
			const TSharedPtr<FJsonObject>* StreamJson = nullptr;
			FString Domain;
			int32 StreamSeed = 0;
			if (!Value.IsValid() || !Value->TryGetObject(StreamJson) || !StreamJson || !(*StreamJson)->TryGetStringField(TEXT("domain"), Domain) || !(*StreamJson)->TryGetNumberField(TEXT("seed"), StreamSeed))
			{
				OutError = TEXT("RANDOM_STATE_INVALID: malformed stream");
				return false;
			}
			OutStreams.Streams.Add(Domain.ToLower(), FRandomStream(StreamSeed));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* DrawValues = nullptr;
	if (InJson->TryGetArrayField(TEXT("draws"), DrawValues) && DrawValues)
	{
		for (const TSharedPtr<FJsonValue>& Value : *DrawValues)
		{
			const TSharedPtr<FJsonObject>* DrawJson = nullptr;
			FEpicodeSandboxRandomDraw Draw;
			double Tick = 0.0;
			if (!Value.IsValid() || !Value->TryGetObject(DrawJson) || !DrawJson
				|| !(*DrawJson)->TryGetNumberField(TEXT("tick"), Tick)
				|| !(*DrawJson)->TryGetStringField(TEXT("domain"), Draw.Domain)
				|| !(*DrawJson)->TryGetStringField(TEXT("kind"), Draw.Kind)
				|| !(*DrawJson)->TryGetNumberField(TEXT("value"), Draw.Value))
			{
				OutError = TEXT("RANDOM_STATE_INVALID: malformed draw");
				return false;
			}
			Draw.Tick = static_cast<int64>(Tick);
			OutStreams.Draws.Add(Draw);
		}
	}
	return true;
}
