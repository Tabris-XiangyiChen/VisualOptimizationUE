// Fill out your copyright notice in the Description page of Project Settings.


#include "MaterialRegistryDataAsset.h"
#include "Materials/MaterialInterface.h"

UMaterialInterface* UMaterialRegistryDataAsset::FindMaterialBySlotId(FName SlotId) const
{
    for (const FGeneratedMaterialEntry& Entry : Materials)
    {
        if (Entry.SlotId == SlotId)
        {
            return Entry.Material;
        }
    }

    return nullptr;
}
