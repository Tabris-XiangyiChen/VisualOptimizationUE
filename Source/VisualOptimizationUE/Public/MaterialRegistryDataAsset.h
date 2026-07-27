// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MaterialRegistryDataAsset.generated.h"

class UMaterialInterface;

USTRUCT(BlueprintType)
struct FGeneratedMaterialEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Material")
    FName SlotId;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Material")
    UMaterialInterface* Material = nullptr;
};


/**
 * 
 */
UCLASS()
class VISUALOPTIMIZATIONUE_API UMaterialRegistryDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Materials")
    TArray<FGeneratedMaterialEntry> Materials;

    UMaterialInterface* FindMaterialBySlotId(FName SlotId) const;
	
};
