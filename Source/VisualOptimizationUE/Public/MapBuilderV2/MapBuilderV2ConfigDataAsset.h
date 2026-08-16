#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MapBuilderV2ConfigDataAsset.generated.h"

UCLASS(BlueprintType)
class VISUALOPTIMIZATIONUE_API UMapBuilderV2ConfigDataAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid", meta = (ClampMin = "1.0"))
    float TileSizeCm = 400.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid", meta = (ClampMin = "1.0"))
    float DefaultWallHeightCm = 300.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid", meta = (ClampMin = "1.0"))
    float DefaultDoorHeightCm = 250.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Walls", meta = (ClampMin = "1.0"))
    float ThinWallThicknessCm = 40.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Walls", meta = (ClampMin = "0.0"))
    float ThinWallArmOverlapCm = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fallback")
    FName FallbackFloorMeshId = FName(TEXT("tile_plane"));

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fallback")
    FName FallbackFloorMaterialSlotId = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passes")
    bool bGenerateBasePass = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passes")
    bool bGenerateStructurePass = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passes")
    bool bGenerateInteractiveAndDecorationPass = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
    bool bCenterMapOnActor = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Debug")
    bool bEnableDebugVisualization = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Debug", meta = (EditCondition = "bEnableDebugVisualization", ClampMin = "0.0"))
    float DebugDisplayDuration = 20.0f;
};
