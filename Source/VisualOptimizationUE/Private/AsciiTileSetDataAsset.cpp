// Fill out your copyright notice in the Description page of Project Settings.


#include "AsciiTileSetDataAsset.h"

bool UAsciiTileSetDataAsset::FindDefinitionBySymbol(const FString& Symbol, FAsciiTileDefinition& OutDefinition) const
{
    for (const FAsciiTileDefinition& Definition : TileDefinitions)
    {
        if (Definition.Symbol == Symbol)
        {
            OutDefinition = Definition;
            return true;
        }
    }

    return false;
}