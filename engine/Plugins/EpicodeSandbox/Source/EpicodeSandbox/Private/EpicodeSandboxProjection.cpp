#include "EpicodeSandboxProjection.h"

#include "EpicodeSandboxJson.h"
#include "EpicodeSandboxWorld.h"

bool FEpicodeSandboxProjection::IsProjectable(EEpicodeSandboxEntityKind InKind)
{
	return InKind != EEpicodeSandboxEntityKind::Trigger;
}

TArray<FEpicodeSandboxProjectionCommand> FEpicodeSandboxProjection::Build(const FEpicodeSandboxWorld& InWorld)
{
	TArray<FEpicodeSandboxProjectionCommand> Commands;
	for (const FString& EntityId : InWorld.GetSortedEntityIds())
	{
		const FEpicodeSandboxEntity& Entity = InWorld.Entities.FindRef(EntityId);
		if (!Entity.bVisible || !IsProjectable(Entity.Kind))
		{
			continue;
		}
		FEpicodeSandboxProjectionCommand& Command = Commands.AddDefaulted_GetRef();
		Command.Operation = TEXT("upsert");
		Command.EntityId = Entity.Id;
		Command.Kind = Entity.Kind;
		Command.ArchetypeId = Entity.ArchetypeId;
		Command.Location = Entity.Location;
		Command.Rotation = Entity.Rotation;
		Command.Data = Entity.Data.IsValid() ? FEpicodeSandboxJson::CloneObject(Entity.Data) : MakeShared<FJsonObject>();
	}
	return Commands;
}
