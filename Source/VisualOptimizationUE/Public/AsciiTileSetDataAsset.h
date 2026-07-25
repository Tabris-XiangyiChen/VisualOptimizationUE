// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AsciiMapTypes.h"
#include "AsciiTileSetDataAsset.generated.h"

UCLASS(BlueprintType)
class VISUALOPTIMIZATIONUE_API UAsciiTileSetDataAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ASCII Tile Set")
    TArray<FAsciiTileDefinition> TileDefinitions;

    bool FindDefinitionBySymbol(const FString& Symbol, FAsciiTileDefinition& OutDefinition) const;
};