#include "MapBuilderV2/GeneratedMeshRegistryV2DataAsset.h"

const FGeneratedMeshEntryV2* UGeneratedMeshRegistryV2DataAsset::FindMeshById(const FName MeshId) const
{
    if (MeshId.IsNone())
    {
        return nullptr;
    }

    return Meshes.FindByPredicate([MeshId](const FGeneratedMeshEntryV2& Entry)
    {
        return Entry.MeshId == MeshId;
    });
}
