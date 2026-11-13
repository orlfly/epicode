#include "EpicodeWorldState.h"

#include "Misc/Guid.h"

namespace
{
	FString MakeId()
	{
		return FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	}

	TArray<TSharedPtr<FJsonValue>> ItemIdsToJson(const TArray<FString>& InIds)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& Id : InIds)
		{
			Values.Add(MakeShared<FJsonValueString>(Id));
		}
		return Values;
	}
}

TSharedPtr<FJsonObject> FEpicodeItemRecord::ToJson() const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("itemId"), ItemId);
	Json->SetStringField(TEXT("itemType"), ItemType);
	Json->SetStringField(TEXT("displayName"), DisplayName);
	Json->SetNumberField(TEXT("durability"), Durability);
	Json->SetNumberField(TEXT("quantity"), Quantity);
	Json->SetStringField(TEXT("ownerPlayerId"), OwnerPlayerId);
	Json->SetStringField(TEXT("holderPlayerId"), HolderPlayerId);
	Json->SetStringField(TEXT("slot"), Slot);
	Json->SetStringField(TEXT("anchor"), Anchor.ToString());
	Json->SetBoolField(TEXT("replicates"), bReplicates);
	Json->SetStringField(TEXT("replicationClass"), ReplicationClass);
	return Json;
}

TSharedPtr<FJsonObject> FEpicodeInventoryRecord::ToJson() const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("playerId"), PlayerId);
	Json->SetArrayField(TEXT("itemIds"), ItemIdsToJson(ItemIds));
	Json->SetNumberField(TEXT("capacity"), Capacity);
	Json->SetNumberField(TEXT("count"), ItemIds.Num());
	return Json;
}

TSharedPtr<FJsonObject> FEpicodeCraftJob::ToJson(double InNowSeconds) const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("jobId"), JobId);
	Json->SetStringField(TEXT("recipeId"), RecipeId);
	Json->SetStringField(TEXT("playerId"), PlayerId);
	Json->SetBoolField(TEXT("active"), bActive);
	Json->SetNumberField(TEXT("startedAt"), StartedAt);
	Json->SetNumberField(TEXT("duration"), Duration);
	Json->SetNumberField(TEXT("elapsed"), InNowSeconds >= 0.0 ? FMath::Max(0.0, InNowSeconds - StartedAt) : 0.0);
	Json->SetNumberField(TEXT("progress"), InNowSeconds >= 0.0 && Duration > 0.0 ? FMath::Clamp((InNowSeconds - StartedAt) / Duration, 0.0, 1.0) : 0.0);
	Json->SetStringField(TEXT("outputItemId"), OutputItemId);
	return Json;
}

TSharedPtr<FJsonObject> FEpicodeBuildRecord::ToJson() const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("buildId"), BuildId);
	Json->SetStringField(TEXT("playerId"), PlayerId);
	Json->SetStringField(TEXT("entityType"), EntityType);
	Json->SetStringField(TEXT("location"), Location.ToString());
	Json->SetStringField(TEXT("rotation"), Rotation.ToString());
	Json->SetNumberField(TEXT("materialCost"), MaterialCost);
	return Json;
}

void FEpicodeWorldState::Reset()
{
	Items.Reset();
	Inventories.Reset();
	Crafts.Reset();
	Builds.Reset();
	Recipes.Reset();
	Materials.Reset();
}

void FEpicodeWorldState::Tick(double InNowSeconds)
{
	for (FEpicodeCraftJob& Job : Crafts)
	{
		if (!Job.bActive || InNowSeconds - Job.StartedAt < Job.Duration)
		{
			continue;
		}
		const FEpicodeRecipe* Recipe = FindRecipe(Job.RecipeId);
		FEpicodeItemRecord Item;
		Item.ItemId = Job.OutputItemId;
		Item.ItemType = Recipe ? Recipe->OutputItemType : TEXT("crafted");
		Item.DisplayName = Recipe ? Recipe->DisplayName : TEXT("Crafted Item");
		Item.Durability = 1.0;
		Item.Quantity = 1;
		Item.OwnerPlayerId = Job.PlayerId;
		Item.ReplicationClass = TEXT("owner");
		Items.Add(Item);
		FindOrCreateInventory(Job.PlayerId).ItemIds.Add(Item.ItemId);
		Job.bActive = false;
	}
}

FEpicodeItemRecord* FEpicodeWorldState::FindItemMutable(const FString& InItemId)
{
	return Items.FindByPredicate([&InItemId](const FEpicodeItemRecord& Item) { return Item.ItemId == InItemId; });
}

const FEpicodeItemRecord* FEpicodeWorldState::FindItem(const FString& InItemId) const
{
	return Items.FindByPredicate([&InItemId](const FEpicodeItemRecord& Item) { return Item.ItemId == InItemId; });
}

FEpicodeInventoryRecord& FEpicodeWorldState::FindOrCreateInventory(const FString& InPlayerId)
{
	for (FEpicodeInventoryRecord& Inventory : Inventories)
	{
		if (Inventory.PlayerId == InPlayerId)
		{
			return Inventory;
		}
	}
	FEpicodeInventoryRecord Inventory;
	Inventory.PlayerId = InPlayerId;
	return Inventories[Inventories.Add(MoveTemp(Inventory))];
}

const FEpicodeRecipe* FEpicodeWorldState::FindRecipe(const FString& InRecipeId) const
{
	return Recipes.FindByPredicate([&InRecipeId](const FEpicodeRecipe& Recipe) { return Recipe.RecipeId == InRecipeId; });
}

bool FEpicodeWorldState::SpawnItem(const FString& InType, const FString& InName, const FVector& InLocation, double InDurability,
	int32 InQuantity, FString& OutItemId, FString& OutError)
{
	if (InType.IsEmpty() || InName.IsEmpty() || InLocation.ContainsNaN())
	{
		OutError = TEXT("INVALID_PARAMS: item type, name and finite location are required.");
		return false;
	}
	if (!FMath::IsFinite(InDurability) || InDurability < 0.0 || InQuantity <= 0)
	{
		OutError = TEXT("INVALID_PARAMS: durability must be non-negative and quantity positive.");
		return false;
	}
	FEpicodeItemRecord Item;
	Item.ItemId = MakeId();
	Item.ItemType = InType;
	Item.DisplayName = InName;
	Item.Durability = InDurability;
	Item.Quantity = InQuantity;
	Item.Anchor = InLocation;
	Item.ReplicationClass = TEXT("owner");
	Items.Add(Item);
	OutItemId = Item.ItemId;
	return true;
}

bool FEpicodeWorldState::SetItemState(const FString& InPlayerId, const FString& InItemId, double InDurability, int32 InQuantity, FString& OutError)
{
	FEpicodeItemRecord* Item = FindItemMutable(InItemId);
	if (!Item)
	{
		OutError = TEXT("ITEM_NOT_FOUND: the item does not exist.");
		return false;
	}
	if (!Item->OwnerPlayerId.IsEmpty() && Item->OwnerPlayerId != InPlayerId)
	{
		OutError = TEXT("FORBIDDEN: only the item owner can change authoritative state.");
		return false;
	}
	if (!FMath::IsFinite(InDurability) || InDurability < 0.0 || InQuantity <= 0)
	{
		OutError = TEXT("INVALID_PARAMS: invalid durability or quantity.");
		return false;
	}
	Item->Durability = InDurability;
	Item->Quantity = InQuantity;
	return true;
}

bool FEpicodeWorldState::PickupItem(const FString& InPlayerId, const FString& InItemId, double InDistance, double InMaxDistance, FString& OutError)
{
	FEpicodeItemRecord* Item = FindItemMutable(InItemId);
	if (!Item)
	{
		OutError = TEXT("ITEM_NOT_FOUND: the item does not exist.");
		return false;
	}
	if (!FMath::IsFinite(InDistance) || InDistance < 0.0 || InDistance > InMaxDistance)
	{
		OutError = TEXT("DISTANCE_TOO_FAR: the item is out of interaction range.");
		return false;
	}
	if (!Item->HolderPlayerId.IsEmpty() && Item->HolderPlayerId != InPlayerId)
	{
		OutError = TEXT("ITEM_ALREADY_HELD: another player holds the item.");
		return false;
	}
	FEpicodeInventoryRecord& Inventory = FindOrCreateInventory(InPlayerId);
	if (!Inventory.ItemIds.Contains(Item->ItemId) && Inventory.ItemIds.Num() >= Inventory.Capacity)
	{
		OutError = TEXT("INVENTORY_FULL: no free inventory slot.");
		return false;
	}
	if (!Inventory.ItemIds.Contains(Item->ItemId))
	{
		Inventory.ItemIds.Add(Item->ItemId);
	}
	Item->OwnerPlayerId = InPlayerId;
	Item->HolderPlayerId = InPlayerId;
	Item->Slot = TEXT("hand");
	Item->Anchor = FVector::ZeroVector;
	return true;
}

bool FEpicodeWorldState::HoldItem(const FString& InPlayerId, const FString& InItemId, const FString& InSlot, FString& OutError)
{
	FEpicodeItemRecord* Item = FindItemMutable(InItemId);
	if (!Item)
	{
		OutError = TEXT("ITEM_NOT_FOUND: the item does not exist.");
		return false;
	}
	if (Item->HolderPlayerId != InPlayerId)
	{
		OutError = TEXT("NOT_ITEM_HOLDER: the player does not hold the item.");
		return false;
	}
	Item->Slot = InSlot.IsEmpty() ? TEXT("hand") : InSlot.Left(32);
	return true;
}

bool FEpicodeWorldState::ReleaseItem(const FString& InPlayerId, const FString& InItemId, FString& OutError)
{
	FEpicodeItemRecord* Item = FindItemMutable(InItemId);
	if (!Item)
	{
		OutError = TEXT("ITEM_NOT_FOUND: the item does not exist.");
		return false;
	}
	if (Item->HolderPlayerId != InPlayerId)
	{
		OutError = TEXT("NOT_ITEM_HOLDER: the player does not hold the item.");
		return false;
	}
	Item->HolderPlayerId.Reset();
	Item->Slot.Reset();
	return true;
}

bool FEpicodeWorldState::DropItem(const FString& InPlayerId, const FString& InItemId, FString& OutError)
{
	FEpicodeItemRecord* Item = FindItemMutable(InItemId);
	if (!Item)
	{
		OutError = TEXT("ITEM_NOT_FOUND: the item does not exist.");
		return false;
	}
	if (Item->HolderPlayerId != InPlayerId)
	{
		OutError = TEXT("NOT_ITEM_HOLDER: the player does not hold the item.");
		return false;
	}
	FEpicodeInventoryRecord& Inventory = FindOrCreateInventory(InPlayerId);
	Inventory.ItemIds.Remove(Item->ItemId);
	Item->HolderPlayerId.Reset();
	Item->Slot.Reset();
	Item->OwnerPlayerId.Reset();
	Item->Anchor = FVector(0.0, 0.0, 0.0);
	return true;
}

bool FEpicodeWorldState::GetInventory(const FString& InRequesterId, const FString& InPlayerId, FEpicodeInventoryRecord& OutInventory, FString& OutError) const
{
	if (InRequesterId != InPlayerId)
	{
		OutError = TEXT("FORBIDDEN: inventories are private to their owner.");
		return false;
	}
	for (const FEpicodeInventoryRecord& Inventory : Inventories)
	{
		if (Inventory.PlayerId == InPlayerId)
		{
			OutInventory = Inventory;
			return true;
		}
	}
	OutInventory = FEpicodeInventoryRecord();
	OutInventory.PlayerId = InPlayerId;
	return true;
}

int32 FEpicodeWorldState::GetMaterials(const FString& InPlayerId) const
{
	const int32* Value = Materials.Find(InPlayerId);
	return Value ? *Value : 0;
}

void FEpicodeWorldState::GrantMaterials(const FString& InPlayerId, int32 InAmount)
{
	Materials.FindOrAdd(InPlayerId) += FMath::Max(0, InAmount);
}

bool FEpicodeWorldState::AddRecipe(const FEpicodeRecipe& InRecipe, FString& OutError)
{
	if (InRecipe.RecipeId.IsEmpty() || InRecipe.OutputItemType.IsEmpty() || InRecipe.Duration <= 0.0)
	{
		OutError = TEXT("INVALID_PARAMS: recipe id, output and positive duration are required.");
		return false;
	}
	if (FindRecipe(InRecipe.RecipeId))
	{
		OutError = TEXT("RECIPE_ALREADY_EXISTS: duplicate recipe id.");
		return false;
	}
	Recipes.Add(InRecipe);
	return true;
}

bool FEpicodeWorldState::StartCraft(const FString& InPlayerId, const FString& InRecipeId, double InNowSeconds, FString& OutJobId, FString& OutError)
{
	const FEpicodeRecipe* Recipe = FindRecipe(InRecipeId);
	if (!Recipe)
	{
		OutError = TEXT("RECIPE_NOT_FOUND: unknown recipe.");
		return false;
	}
	FEpicodeInventoryRecord& Inventory = FindOrCreateInventory(InPlayerId);
	for (const FString& RequiredType : Recipe->InputItemTypes)
	{
		const FString* Match = Inventory.ItemIds.FindByPredicate([this, &RequiredType](const FString& ItemId)
		{
			const FEpicodeItemRecord* Item = FindItem(ItemId);
			return Item && Item->ItemType == RequiredType;
		});
		if (!Match)
		{
			OutError = FString::Printf(TEXT("MISSING_MATERIALS: missing item type '%s'."), *RequiredType);
			return false;
		}
	}
	for (const FString& RequiredType : Recipe->InputItemTypes)
	{
		const int32 Index = Inventory.ItemIds.IndexOfByPredicate([this, &RequiredType](const FString& ItemId)
		{
			const FEpicodeItemRecord* Item = FindItem(ItemId);
			return Item && Item->ItemType == RequiredType;
		});
		if (Index != INDEX_NONE)
		{
			Items.RemoveAll([this, &Inventory, Index](const FEpicodeItemRecord& Item)
			{
				return Item.ItemId == Inventory.ItemIds[Index];
			});
			Inventory.ItemIds.RemoveAt(Index);
		}
	}
	FEpicodeCraftJob Job;
	Job.JobId = MakeId();
	Job.RecipeId = Recipe->RecipeId;
	Job.PlayerId = InPlayerId;
	Job.StartedAt = InNowSeconds;
	Job.Duration = Recipe->Duration;
	Job.OutputItemId = MakeId();
	Crafts.Add(Job);
	OutJobId = Job.JobId;
	return true;
}

bool FEpicodeWorldState::GetCraftStatus(const FString& InRequesterId, const FString& InJobId, double InNowSeconds, TSharedPtr<FJsonObject>& OutStatus, FString& OutError) const
{
	const FEpicodeCraftJob* Job = Crafts.FindByPredicate([&InJobId](const FEpicodeCraftJob& Entry) { return Entry.JobId == InJobId; });
	if (!Job)
	{
		OutError = TEXT("CRAFT_NOT_FOUND: unknown craft job.");
		return false;
	}
	if (Job->PlayerId != InRequesterId)
	{
		OutError = TEXT("FORBIDDEN: craft jobs are private to their owner.");
		return false;
	}
	OutStatus = Job->ToJson(InNowSeconds);
	return true;
}

bool FEpicodeWorldState::CancelCraft(const FString& InPlayerId, const FString& InJobId, FString& OutError)
{
	const int32 Index = Crafts.IndexOfByPredicate([&InJobId](const FEpicodeCraftJob& Job) { return Job.JobId == InJobId; });
	if (Index == INDEX_NONE)
	{
		OutError = TEXT("CRAFT_NOT_FOUND: unknown craft job.");
		return false;
	}
	FEpicodeCraftJob& Job = Crafts[Index];
	if (Job.PlayerId != InPlayerId)
	{
		OutError = TEXT("FORBIDDEN: only the craft owner can cancel.");
		return false;
	}
	if (!Job.bActive)
	{
		OutError = TEXT("CRAFT_ALREADY_COMPLETE: the job is already complete.");
		return false;
	}
	Job.bActive = false;
	Job.OutputItemId.Reset();
	return true;
}

void FEpicodeWorldState::TransferCrafts(const FString& InFromPlayerId, const FString& InToPlayerId)
{
	for (FEpicodeCraftJob& Job : Crafts)
	{
		if (Job.PlayerId == InFromPlayerId)
		{
			Job.PlayerId = InToPlayerId;
		}
	}
}

bool FEpicodeWorldState::ConfirmBuild(const FString& InPlayerId, const FString& InEntityType, const FVector& InBuilderLocation,
	const FVector& InLocation, const FRotator& InRotation, int32 InMaterialCost, double InMaxDistance,
	FString& OutBuildId, FString& OutError)
{
	if (InEntityType.IsEmpty() || InLocation.ContainsNaN() || InBuilderLocation.ContainsNaN())
	{
		OutError = TEXT("INVALID_PARAMS: build type and finite transforms are required.");
		return false;
	}
	if (FMath::Abs(InLocation.X) > 10000.0 || FMath::Abs(InLocation.Y) > 10000.0 || InLocation.Z < -500.0)
	{
		OutError = TEXT("BUILD_OUT_OF_BOUNDS: build location is outside the allowed zone.");
		return false;
	}
	if (FVector::Dist(InBuilderLocation, InLocation) > InMaxDistance)
	{
		OutError = TEXT("BUILD_TOO_FAR: build location exceeds the interaction range.");
		return false;
	}
	for (const FEpicodeBuildRecord& Existing : Builds)
	{
		if (FVector::DistSquared(Existing.Location, InLocation) < FMath::Square(50.0))
		{
			OutError = TEXT("BUILD_COLLISION: build overlaps an existing structure.");
			return false;
		}
	}
	const int32 Cost = FMath::Max(0, InMaterialCost);
	if (GetMaterials(InPlayerId) < Cost)
	{
		OutError = TEXT("MISSING_MATERIALS: not enough build materials.");
		return false;
	}
	Materials.FindOrAdd(InPlayerId) -= Cost;
	FEpicodeBuildRecord Build;
	Build.BuildId = MakeId();
	Build.PlayerId = InPlayerId;
	Build.EntityType = InEntityType;
	Build.Location = InLocation;
	Build.Rotation = InRotation;
	Build.MaterialCost = Cost;
	Builds.Add(Build);
	OutBuildId = Build.BuildId;
	return true;
}

bool FEpicodeWorldState::UndoBuild(const FString& InPlayerId, const FString& InBuildId, FString& OutError)
{
	const int32 Index = Builds.IndexOfByPredicate([&InBuildId](const FEpicodeBuildRecord& Build) { return Build.BuildId == InBuildId; });
	if (Index == INDEX_NONE)
	{
		OutError = TEXT("BUILD_NOT_FOUND: unknown build record.");
		return false;
	}
	if (Builds[Index].PlayerId != InPlayerId)
	{
		OutError = TEXT("FORBIDDEN: only the builder can undo this build.");
		return false;
	}
	Materials.FindOrAdd(InPlayerId) += Builds[Index].MaterialCost;
	Builds.RemoveAt(Index);
	return true;
}

TSharedPtr<FJsonObject> FEpicodeWorldState::ToJson(double InNowSeconds) const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> ItemValues;
	for (const FEpicodeItemRecord& Item : Items)
	{
		ItemValues.Add(MakeShared<FJsonValueObject>(Item.ToJson()));
	}
	TArray<TSharedPtr<FJsonValue>> InventoryValues;
	for (const FEpicodeInventoryRecord& Inventory : Inventories)
	{
		InventoryValues.Add(MakeShared<FJsonValueObject>(Inventory.ToJson()));
	}
	TArray<TSharedPtr<FJsonValue>> CraftValues;
	for (const FEpicodeCraftJob& Job : Crafts)
	{
		CraftValues.Add(MakeShared<FJsonValueObject>(Job.ToJson(InNowSeconds)));
	}
	TArray<TSharedPtr<FJsonValue>> BuildValues;
	for (const FEpicodeBuildRecord& Build : Builds)
	{
		BuildValues.Add(MakeShared<FJsonValueObject>(Build.ToJson()));
	}
	Json->SetArrayField(TEXT("items"), ItemValues);
	Json->SetArrayField(TEXT("inventories"), InventoryValues);
	Json->SetArrayField(TEXT("crafts"), CraftValues);
	Json->SetArrayField(TEXT("builds"), BuildValues);
	return Json;
}
