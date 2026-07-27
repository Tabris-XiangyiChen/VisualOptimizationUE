// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GeneratedMeshRegistryDataAsset.generated.h"

class UStaticMesh;

USTRUCT(BlueprintType)
struct FGeneratedMeshEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh")
    FName MeshId;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh")
    UStaticMesh* Mesh = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh")
    FVector DefaultScale = FVector(1.0f, 1.0f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh")
    bool bUseInstancing = true;
};

/**
 * 
 */
UCLASS(BlueprintType)
class VISUALOPTIMIZATIONUE_API UGeneratedMeshRegistryDataAsset : public UDataAsset
{
	GENERATED_BODY()
	
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Meshes")
    TArray<FGeneratedMeshEntry> Meshes;

    bool FindMeshById(FName MeshId, FGeneratedMeshEntry& OutEntry) const;
};
