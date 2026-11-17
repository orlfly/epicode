#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EpicodeSettings.generated.h"

UCLASS(config = Epicode, defaultconfig, meta = (DisplayName = "Epicode Video Engine"))
class EPICODECORE_API UEpicodeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UEpicodeSettings();

	virtual FName GetContainerName() const override { return TEXT("Project"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("Epicode Video Engine"); }

	virtual void PostInitProperties() override;
	virtual void PostReloadConfig(FProperty* PropertyThatWasLoaded) override;

	static const UEpicodeSettings* Get() { return GetDefault<UEpicodeSettings>(); }

	/** Applies environment overrides in place. Safe to call repeatedly. */
	void ApplyEnvironmentOverrides();

	/** Returns the environment token, the token file contents, or creates a new token. */
	FString GetApiToken() const;

	/** Creates a token file when authentication is enabled and no token exists. */
	bool EnsureApiToken(FString& OutError) const;

	FString ResolveProjectPath(const FString& InPath) const;
	FString GetSceneRootPath() const { return ResolveProjectPath(SceneRoot); }
	FString GetSceneTrashRootPath() const { return ResolveProjectPath(SceneTrashRoot); }
	FString GetRenderOutputRootPath() const { return ResolveProjectPath(RenderOutputRoot); }
	FString GetApiTokenFilePath() const { return ResolveProjectPath(ApiTokenFile); }
	FString GetHttpBaseUrl() const;
	FString GetWebSocketUrl() const;
	TArray<FString> GetResolvedImportRoots() const;
	static FString GenerateToken(bool bUseSystemRandom = true);

	/** Validates ports, remote binding, token, and directories. */
	bool Validate(FString& OutError) const;

	UPROPERTY(EditAnywhere, Config, Category = "API")
	bool bApiEnabled = true;

	UPROPERTY(EditAnywhere, Config, Category = "API")
	FString ApiHost = TEXT("127.0.0.1");

	UPROPERTY(EditAnywhere, Config, Category = "API", meta = (ClampMin = "1", ClampMax = "65535"))
	int32 ApiHttpPort = 8710;

	UPROPERTY(EditAnywhere, Config, Category = "API", meta = (ClampMin = "1", ClampMax = "65535"))
	int32 ApiWsPort = 8711;

	UPROPERTY(EditAnywhere, Config, Category = "API")
	bool bRequireAuth = true;

	UPROPERTY(EditAnywhere, Config, Category = "API")
	FString ApiTokenFile = TEXT("Saved/Epicode/api-token");

	UPROPERTY(EditAnywhere, Config, Category = "API")
	bool bAllowRemote = false;

	UPROPERTY(EditAnywhere, Config, Category = "API")
	bool bEnableCors = false;

	UPROPERTY(EditAnywhere, Config, Category = "API", meta = (ClampMin = "0", ClampMax = "10000"))
	int32 MaxRequestsPerSecond = 200;

	UPROPERTY(EditAnywhere, Config, Category = "Paths")
	TArray<FString> ImportRoots;

	UPROPERTY(EditAnywhere, Config, Category = "Paths")
	FString ImportContentRoot = TEXT("/Game/Epicode/Imported");

	UPROPERTY(EditAnywhere, Config, Category = "Paths")
	FString SceneRoot = TEXT("Saved/Epicode/Scenes");

	UPROPERTY(EditAnywhere, Config, Category = "Paths")
	FString SceneTrashRoot = TEXT("Saved/Epicode/Trash/Scenes");

	UPROPERTY(EditAnywhere, Config, Category = "Paths")
	FString RenderOutputRoot = TEXT("Saved/Epicode/Renders");

	UPROPERTY(EditAnywhere, Config, Category = "Limits")
	int64 MaxImportBytes = 2147483648LL;

	UPROPERTY(EditAnywhere, Config, Category = "Limits")
	int64 MaxRequestBytes = 8388608LL;

	UPROPERTY(EditAnywhere, Config, Category = "Rendering", meta = (ClampMin = "1", ClampMax = "8"))
	int32 RenderConcurrency = 1;

	UPROPERTY(EditAnywhere, Config, Category = "Rendering", meta = (ClampMin = "1"))
	int32 RenderTimeoutSeconds = 1800;

	UPROPERTY(EditAnywhere, Config, Category = "Assistant")
	bool bAssistantEnabled = true;

	/** mock, unavailable, or a future transport such as deepseek_sidecar. */
	UPROPERTY(EditAnywhere, Config, Category = "Assistant")
	FString AssistantBackend = TEXT("deepseek_sidecar");

	UPROPERTY(EditAnywhere, Config, Category = "Assistant")
	FString AssistantModel = TEXT("deepseek-chat");

	UPROPERTY(EditAnywhere, Config, Category = "Assistant")
	FString AssistantEndpoint = TEXT("https://api.deepseek.com");

	UPROPERTY(EditAnywhere, Config, Category = "Assistant")
	FString AssistantSidecarCommand = TEXT("python3");

	UPROPERTY(EditAnywhere, Config, Category = "Assistant")
	FString AssistantSidecarScript = TEXT("Scripts/Assistant/DeepSeekHarnessSidecar.py");

	UPROPERTY(EditAnywhere, Config, Category = "Assistant")
	FString AssistantSidecarMode = TEXT("stdio");

	/** Local, untracked file containing DEEPSEEK_API_KEY; environment wins. */
	UPROPERTY(EditAnywhere, Config, Category = "Assistant")
	FString AssistantApiKeyFile = TEXT("Saved/Epicode/deepseek.env");

	UPROPERTY(EditAnywhere, Config, Category = "Assistant", meta = (ClampMin = "1024", ClampMax = "262144"))
	int32 AssistantMaxContextTokens = 32000;

	UPROPERTY(EditAnywhere, Config, Category = "Assistant", meta = (ClampMin = "1024", ClampMax = "65536"))
	int32 AssistantMaxContextChars = 12000;

	UPROPERTY(EditAnywhere, Config, Category = "Assistant", meta = (ClampMin = "0", ClampMax = "5"))
	int32 AssistantMaxRepairAttempts = 2;

	UPROPERTY(EditAnywhere, Config, Category = "Assistant", meta = (ClampMin = "5", ClampMax = "300"))
	int32 AssistantRequestTimeoutSeconds = 60;

	UPROPERTY(EditAnywhere, Config, Category = "Assistant", meta = (ClampMin = "30", ClampMax = "3600"))
	int32 AssistantSidecarIdleSeconds = 300;

private:
};
