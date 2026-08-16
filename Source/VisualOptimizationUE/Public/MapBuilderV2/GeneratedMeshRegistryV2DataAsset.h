#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MapBuilderV2/AsciiMapBuilderV2Types.h"
#include "GeneratedMeshRegistryV2DataAsset.generated.h"

class AActor;
class UStaticMesh;

USTRUCT(BlueprintType)
struct FGeneratedMeshEntryV2
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh V2")
    FName MeshId = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh V2")
    UStaticMesh* PrimaryMesh = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh V2|Placement")
    EGeneratedMeshLayerV2 PrimaryLayer = EGeneratedMeshLayerV2::Structure;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh V2|Placement")
    EBaseCoveragePolicyV2 CoveragePolicy = EBaseCoveragePolicyV2::RequiresUnderlay;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh V2|Placement")
    EOrientationPolicyV2 OrientationPolicy = EOrientationPolicyV2::Fixed;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh V2|Placement")
    EMeshFitModeV2 FitMode = EMeshFitModeV2::PreserveNative;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh V2|Transform")
    FVector DefaultScale = FVector::OneVector;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh V2|Transform")
    FVector LocationOffset = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh V2|Transform")
    FRotator RotationOffset = FRotator::ZeroRotator;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh V2|Transform")
    bool bBottomAlignToCellBase = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh V2|Semantics")
    bool bWalkable = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh V2|Semantics")
    bool bBlocksTile = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh V2|Emission")
    bool bUseInstancing = true;

    // Make center and wall arms from this simple mesh. Do not use for detail mesh.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh V2|Emission")
    bool bUseConnectedWallAssembly = false;

    // Put runtime material on slot 0. Turn off to keep model material.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh V2|Emission")
    bool bOverrideMaterialSlotZero = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh V2|Emission")
    bool bSpawnActor = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Mesh V2|Emission", meta = (EditCondition = "bSpawnActor"))
    TSubclassOf<AActor> ActorClass = nullptr;
};

UCLASS(BlueprintType)
class VISUALOPTIMIZATIONUE_API UGeneratedMeshRegistryV2DataAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generated Meshes V2")
    TArray<FGeneratedMeshEntryV2> Meshes;

    const FGeneratedMeshEntryV2* FindMeshById(FName MeshId) const;
};
