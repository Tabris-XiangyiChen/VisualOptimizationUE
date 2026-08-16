#pragma once

#include "CoreMinimal.h"
#include "AsciiMapBuilderV2Types.generated.h"

// Tell which pass will generate this mesh.
UENUM(BlueprintType)
enum class EGeneratedMeshLayerV2 : uint8
{
    // Basic ground like floor, grass or water.
    BaseSurface,

    // Building part like wall, pillar or stair.
    Structure,

    // Object can have game action, for example door.
    Interactive,

    // Small object only for making scene look better.
    Decoration,

    // Extra surface put above another layer.
    Overlay
};

// Tell if one tile still needs a ground mesh.
UENUM(BlueprintType)
enum class EBaseCoveragePolicyV2 : uint8
{
    // This mesh covers the tile, so no ground under it.
    ReplaceBase,

    // This mesh is not full tile, so need ground under it.
    RequiresUnderlay,

    // Do not make any ground under this mesh.
    NoUnderlay
};

// Tell how Builder finds the mesh direction.
UENUM(BlueprintType)
enum class EOrientationPolicyV2 : uint8
{
    // Always use the rotation set in Registry.
    Fixed,

    // Read north, east, south and west connections.
    ConnectFourDirections,

    // Rotate mesh to the same line with neighbor walls.
    AlignToAdjacentWalls,

    // Make mesh front side look to open floor.
    FaceOpenSpace,

    // Do not guess direction, only use given rotation.
    Manual
};

// Tell how Builder changes imported mesh size.
UENUM(BlueprintType)
enum class EMeshFitModeV2 : uint8
{
    // Keep imported model size and model ratio.
    PreserveNative,

    // Scale all axis until width fits one tile.
    UniformFitWidth,

    // Scale all axis until height fits target height.
    UniformFitHeight,

    // Scale same ratio and keep mesh inside one tile.
    UniformFitFootprint,

    // Scale every axis alone to fill the tile size.
    StretchToTile
};

namespace AsciiMapV2Connectivity
{
    static constexpr uint8 North = 1;
    static constexpr uint8 East = 2;
    static constexpr uint8 South = 4;
    static constexpr uint8 West = 8;
}

USTRUCT(BlueprintType)
struct FAsciiRuntimeTileDefinitionV2
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Runtime Tile")
    FString Symbol;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Runtime Tile")
    FString Role;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Runtime Tile")
    FName TileTypeId = NAME_None;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Runtime Tile")
    FName MeshId = NAME_None;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Runtime Tile")
    FName MaterialSlotId = NAME_None;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Runtime Tile")
    FString ShapeType;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Runtime Tile")
    FString HeightClass;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Runtime Tile")
    TArray<FString> SelectedMeshRoleTags;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Runtime Tile")
    FString MaterialFamily;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Runtime Tile")
    int32 TileCountInMap = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Runtime Tile")
    float Height = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Runtime Tile")
    float ZOffset = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Runtime Tile")
    bool bGenerate = false;
};

USTRUCT(BlueprintType)
struct FResolvedMapCellV2
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Cell")
    int32 Row = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Cell")
    int32 Column = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Cell")
    FAsciiRuntimeTileDefinitionV2 TileDefinition;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Cell")
    EGeneratedMeshLayerV2 PrimaryLayer = EGeneratedMeshLayerV2::BaseSurface;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Cell")
    EBaseCoveragePolicyV2 CoveragePolicy = EBaseCoveragePolicyV2::ReplaceBase;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Cell")
    EOrientationPolicyV2 OrientationPolicy = EOrientationPolicyV2::Fixed;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Cell")
    EMeshFitModeV2 FitMode = EMeshFitModeV2::PreserveNative;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Cell")
    uint8 ConnectivityMask = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Cell")
    FName ResolvedUnderlayMeshId = NAME_None;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Cell")
    FName ResolvedUnderlayMaterialSlotId = NAME_None;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Cell")
    FVector ResolvedLocation = FVector::ZeroVector;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Cell")
    FVector ResolvedScale = FVector::OneVector;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Cell")
    FRotator ResolvedRotation = FRotator::ZeroRotator;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Cell")
    bool bGenerateUnderlay = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Cell")
    bool bGeneratePrimaryMesh = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Cell")
    bool bSpawnAsActor = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Cell")
    bool bOrientationAmbiguous = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Cell")
    FString UnderlayResolutionSource;
};
