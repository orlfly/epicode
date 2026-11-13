#include "EpicodeImportClassifier.h"

#include "Misc/Paths.h"

namespace
{
	bool LooksCharacter(const FString& BaseName)
	{
		const FString Lower = BaseName.ToLower();
		return Lower.Contains(TEXT("character"))
			|| Lower.Contains(TEXT("char_"))
			|| Lower.Contains(TEXT("_char"))
			|| Lower.Contains(TEXT("rig"))
			|| Lower.Contains(TEXT("skeleton"))
			|| Lower.Contains(TEXT("humanoid"))
			|| Lower.Contains(TEXT("metahuman"));
	}

	bool IsOneOf(const FString& Value, std::initializer_list<const TCHAR*> Candidates)
	{
		for (const TCHAR* Candidate : Candidates)
		{
			if (Value.Equals(Candidate, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}
}

EEpicodeImportKind FEpicodeImportClassifier::Classify(const FString& InFilePath)
{
	const FString Extension = FPaths::GetExtension(InFilePath).ToLower();
	const FString BaseName = FPaths::GetBaseFilename(InFilePath);
	if (Extension == TEXT("umap"))
	{
		return EEpicodeImportKind::Scene;
	}
	if (Extension == TEXT("usd") || Extension == TEXT("usda") || Extension == TEXT("usdc") || Extension == TEXT("usdz"))
	{
		return LooksCharacter(BaseName) ? EEpicodeImportKind::Character : EEpicodeImportKind::Scene;
	}
	if (Extension == TEXT("fbx") || Extension == TEXT("gltf") || Extension == TEXT("glb"))
	{
		return LooksCharacter(BaseName) ? EEpicodeImportKind::Character : EEpicodeImportKind::Prop;
	}
	if (Extension == TEXT("obj"))
	{
		return EEpicodeImportKind::Prop;
	}
	return EEpicodeImportKind::Unknown;
}

bool FEpicodeImportClassifier::IsSupportedExtension(const FString& InFilePath)
{
	return Classify(InFilePath) != EEpicodeImportKind::Unknown;
}

TArray<FString> FEpicodeImportClassifier::SupportedExtensions()
{
	return { TEXT("fbx"), TEXT("gltf"), TEXT("glb"), TEXT("obj"), TEXT("usd"), TEXT("usda"), TEXT("usdc"), TEXT("usdz"), TEXT("umap") };
}

TArray<FEpicodeImportCapability> FEpicodeImportClassifier::Capabilities()
{
	TArray<FEpicodeImportCapability> Capabilities;
	for (const FString& Extension : SupportedExtensions())
	{
		FEpicodeImportCapability Capability;
		Capability.Format = Extension;
		Capability.Kind = Extension == TEXT("obj") ? EEpicodeImportKind::Prop : (Extension == TEXT("umap") ? EEpicodeImportKind::Scene : EEpicodeImportKind::Unknown);
		Capability.bAvailable = Extension != TEXT("umap");
		Capability.Reason = Extension == TEXT("umap")
			? TEXT("Map import is available through the scene library, not the asset importer.")
			: TEXT("Interchange translator available.");
		Capabilities.Add(Capability);
	}
	return Capabilities;
}

FString FEpicodeImportClassifier::KindToString(EEpicodeImportKind Kind)
{
	switch (Kind)
	{
	case EEpicodeImportKind::Scene:
		return TEXT("scene");
	case EEpicodeImportKind::Character:
		return TEXT("character");
	case EEpicodeImportKind::Prop:
		return TEXT("prop");
	default:
		return TEXT("unknown");
	}
}

EEpicodeImportKind FEpicodeImportClassifier::KindFromString(const FString& InKind)
{
	if (InKind.Equals(TEXT("scene"), ESearchCase::IgnoreCase))
	{
		return EEpicodeImportKind::Scene;
	}
	if (InKind.Equals(TEXT("character"), ESearchCase::IgnoreCase) || InKind.Equals(TEXT("char"), ESearchCase::IgnoreCase))
	{
		return EEpicodeImportKind::Character;
	}
	if (InKind.Equals(TEXT("prop"), ESearchCase::IgnoreCase) || InKind.Equals(TEXT("staticmesh"), ESearchCase::IgnoreCase))
	{
		return EEpicodeImportKind::Prop;
	}
	return EEpicodeImportKind::Unknown;
}

bool FEpicodeImportClassifier::IsMeshExtension(const FString& InFilePath)
{
	const FString Extension = FPaths::GetExtension(InFilePath).ToLower();
	return IsOneOf(Extension, { TEXT("fbx"), TEXT("gltf"), TEXT("glb"), TEXT("obj"), TEXT("usd"), TEXT("usda"), TEXT("usdc"), TEXT("usdz") });
}
