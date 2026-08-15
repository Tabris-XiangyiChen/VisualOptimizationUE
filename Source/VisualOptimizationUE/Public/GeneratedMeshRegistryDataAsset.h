// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GeneratedMeshRegistryDataAsset.generated.h"

class AActor;
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
    FVector LocationOffset = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh")
    FRotator RotationOffset = FRotator::ZeroRotator;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh")
    bool bUseRegistryTransform = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh")
    bool bFitXYToTileSize = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh")
    bool bFitZToTileSize = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh")
    bool bBottomAlignToTileBase = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh")
    bool bUseInstancing = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh|Actor")
    bool bSpawnActorInsteadOfStaticMesh = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh|Actor", meta = (EditCondition = "bSpawnActorInsteadOfStaticMesh"))
    TSubclassOf<AActor> ActorClass = nullptr;
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
