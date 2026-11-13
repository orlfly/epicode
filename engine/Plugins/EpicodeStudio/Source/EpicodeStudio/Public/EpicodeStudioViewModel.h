#pragma once

#include "CoreMinimal.h"

class EPICODESTUDIO_API FEpicodeStudioViewModel
{
public:
	void SetActiveTab(int32 InTab) { ActiveTab = InTab; }
	int32 GetActiveTab() const { return ActiveTab; }

	bool NeedsRefresh(uint64 Revision) const { return Revision != LastRevision; }
	void MarkRefreshed(uint64 Revision) { LastRevision = Revision; }

	void SelectOnly(const FString& EntityId)
	{
		SelectedEntities.Reset();
		if (!EntityId.IsEmpty())
		{
			SelectedEntities.Add(EntityId);
		}
		SelectedEntityId = EntityId;
	}

	void AddSelectedEntity(const FString& EntityId)
	{
		if (!EntityId.IsEmpty())
		{
			SelectedEntities.Add(EntityId);
			SelectedEntityId = EntityId;
		}
	}

	void RemoveSelectedEntity(const FString& EntityId)
	{
		SelectedEntities.Remove(EntityId);
		if (SelectedEntityId == EntityId)
		{
			SelectedEntityId = SelectedEntities.Num() > 0 ? *SelectedEntities.CreateConstIterator() : FString();
		}
	}

	void ClearSelection()
	{
		SelectedEntities.Reset();
		SelectedEntityId.Reset();
	}

	void SetSelectedEntity(const FString& EntityId) { SelectedEntityId = EntityId; }
	const FString& GetSelectedEntityId() const { return SelectedEntityId; }
	bool IsSelected(const FString& EntityId) const { return SelectedEntities.Contains(EntityId); }
	const TSet<FString>& GetSelectedEntities() const { return SelectedEntities; }
	void RemoveMissingSelection(const TSet<FString>& ExistingIds)
	{
		for (auto It = SelectedEntities.CreateIterator(); It; ++It)
		{
			if (!ExistingIds.Contains(*It))
			{
				It.RemoveCurrent();
			}
		}
		if (!SelectedEntityId.IsEmpty() && !ExistingIds.Contains(SelectedEntityId))
		{
			SelectedEntityId.Reset();
		}
	}

private:
	int32 ActiveTab = 0;
	uint64 LastRevision = 0;
	FString SelectedEntityId;
	TSet<FString> SelectedEntities;
};
