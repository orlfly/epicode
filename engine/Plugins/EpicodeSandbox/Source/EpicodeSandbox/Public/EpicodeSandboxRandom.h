#pragma once

#include "CoreMinimal.h"
#include "EpicodeSandboxTypes.h"

class EPICODESANDBOX_API FEpicodeSandboxRandomStreams
{
public:
	void Initialize(int32 InSeed);
	void Reset(int32 InSeed);

	double GetFloat(const FString& InDomain, int64 InTick = 0);
	int32 GetInt(const FString& InDomain, int32 InMin, int32 InMax, int64 InTick = 0);
	bool GetBool(const FString& InDomain, int64 InTick = 0);
	const TArray<FEpicodeSandboxRandomDraw>& GetDraws() const { return Draws; }
	void ClearDraws() { Draws.Reset(); }
	int32 GetSeed() const { return Seed; }
	static int32 DeriveSeed(int32 InSeed, const FString& InDomain);

	TSharedPtr<FJsonObject> GetDrawsJson() const;
	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& InJson, FEpicodeSandboxRandomStreams& OutStreams, FString& OutError);

private:
	FRandomStream& GetStream(const FString& InDomain);

	int32 Seed = 0;
	TMap<FString, FRandomStream> Streams;
	TArray<FEpicodeSandboxRandomDraw> Draws;
};
