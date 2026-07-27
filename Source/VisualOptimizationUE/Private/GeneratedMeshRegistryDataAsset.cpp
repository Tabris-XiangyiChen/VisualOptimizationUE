// Fill out your copyright notice in the Description page of Project Settings.


#include "GeneratedMeshRegistryDataAsset.h"

bool UGeneratedMeshRegistryDataAsset::FindMeshById(FName MeshId, FGeneratedMeshEntry& OutEntry) const
{
    for (const FGeneratedMeshEntry& Entry : Meshes)
    {
        if (Entry.MeshId == MeshId)
        {
            OutEntry = Entry;
            return true;
        }
    }

    return false;
}
