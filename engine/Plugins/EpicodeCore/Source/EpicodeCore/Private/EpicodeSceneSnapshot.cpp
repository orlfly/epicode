#include "EpicodeSceneSnapshot.h"

TSharedPtr<FJsonObject> FEpicodeSceneSnapshot::ToJson() const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("version"), static_cast<double>(Version));
	Result->SetStringField(TEXT("sceneId"), SceneId);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetObjectField(TEXT("scene"), Json.IsValid() ? Json : MakeShared<FJsonObject>());
	return Result;
}
