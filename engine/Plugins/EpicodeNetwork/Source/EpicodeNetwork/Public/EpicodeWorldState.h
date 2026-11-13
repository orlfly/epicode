#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct EPICODENETWORK_API FEpicodeItemRecord
{
	FString ItemId;
	FString ItemType = TEXT("prop");
	FString DisplayName;
	double Durability = 1.0;
	int32 Quantity = 1;
	FString OwnerPlayerId;
	FString HolderPlayerId;
	FString Slot;
	FVector Anchor = FVector::ZeroVector;
	bool bReplicates = true;
	FString ReplicationClass = TEXT("owner");

	TSharedPtr<FJsonObject> ToJson() const;
};

struct EPICODENETWORK_API FEpicodeInventoryRecord
{
	FString PlayerId;
	TArray<FString> ItemIds;
	int32 Capacity = 8;

	TSharedPtr<FJsonObject> ToJson() const;
};

struct EPICODENETWORK_API FEpicodeCraftJob
{
	FString JobId;
	FString RecipeId;
	FString PlayerId;
	double StartedAt = 0.0;
	double Duration = 2.0;
	bool bActive = true;
	FString OutputItemId;

	TSharedPtr<FJsonObject> ToJson(double InNowSeconds) const;
};

struct EPICODENETWORK_API FEpicodeBuildRecord
{
	FString BuildId;
	FString PlayerId;
	FString EntityType = TEXT("prop");
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	int32 MaterialCost = 1;

	TSharedPtr<FJsonObject> ToJson() const;
};

struct EPICODENETWORK_API FEpicodeRecipe
{
	FString RecipeId;
	FString DisplayName;
	TArray<FString> InputItemTypes;
	FString OutputItemType = TEXT("crafted");
	double Duration = 2.0;
	int32 MaterialCost = 0;
};

class EPICODENETWORK_API FEpicodeWorldState
{
public:
	void Reset();
	void Tick(double InNowSeconds);

	bool SpawnItem(const FString& InType, const FString& InName, const FVector& InLocation, double InDurability,
		int32 InQuantity, FString& OutItemId, FString& OutError);
	bool SetItemState(const FString& InPlayerId, const FString& InItemId, double InDurability, int32 InQuantity, FString& OutError);
	bool PickupItem(const FString& InPlayerId, const FString& InItemId, double InDistance, double InMaxDistance, FString& OutError);
	bool HoldItem(const FString& InPlayerId, const FString& InItemId, const FString& InSlot, FString& OutError);
	bool ReleaseItem(const FString& InPlayerId, const FString& InItemId, FString& OutError);
	bool DropItem(const FString& InPlayerId, const FString& InItemId, FString& OutError);

	bool GetInventory(const FString& InRequesterId, const FString& InPlayerId, FEpicodeInventoryRecord& OutInventory, FString& OutError) const;
	int32 GetMaterials(const FString& InPlayerId) const;
	void GrantMaterials(const FString& InPlayerId, int32 InAmount);

	bool AddRecipe(const FEpicodeRecipe& InRecipe, FString& OutError);
	bool StartCraft(const FString& InPlayerId, const FString& InRecipeId, double InNowSeconds, FString& OutJobId, FString& OutError);
	bool GetCraftStatus(const FString& InRequesterId, const FString& InJobId, double InNowSeconds, TSharedPtr<FJsonObject>& OutStatus, FString& OutError) const;
	bool CancelCraft(const FString& InPlayerId, const FString& InJobId, FString& OutError);
	void TransferCrafts(const FString& InFromPlayerId, const FString& InToPlayerId);

	bool ConfirmBuild(const FString& InPlayerId, const FString& InEntityType, const FVector& InBuilderLocation,
		const FVector& InLocation, const FRotator& InRotation, int32 InMaterialCost, double InMaxDistance,
		FString& OutBuildId, FString& OutError);
	bool UndoBuild(const FString& InPlayerId, const FString& InBuildId, FString& OutError);

	const FEpicodeItemRecord* FindItem(const FString& InItemId) const;
	const TArray<FEpicodeItemRecord>& GetItems() const { return Items; }
	const TArray<FEpicodeCraftJob>& GetCrafts() const { return Crafts; }
	const TArray<FEpicodeBuildRecord>& GetBuilds() const { return Builds; }
	TSharedPtr<FJsonObject> ToJson(double InNowSeconds = -1.0) const;

private:
	FEpicodeItemRecord* FindItemMutable(const FString& InItemId);
	FEpicodeInventoryRecord& FindOrCreateInventory(const FString& InPlayerId);
	const FEpicodeRecipe* FindRecipe(const FString& InRecipeId) const;

	TArray<FEpicodeItemRecord> Items;
	TArray<FEpicodeInventoryRecord> Inventories;
	TArray<FEpicodeCraftJob> Crafts;
	TArray<FEpicodeBuildRecord> Builds;
	TArray<FEpicodeRecipe> Recipes;
	TMap<FString, int32> Materials;
};
