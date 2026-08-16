#pragma once

#include "CoreMinimal.h"
#include "MapBuilderV2/AsciiMapBuilderV2Types.h"

class AAsciiMapBuilderV2Actor;
class FAsciiMapGridV2;
class UGeneratedMeshRegistryV2DataAsset;
class UMapBuilderV2ConfigDataAsset;
class UMaterialInterface;
struct FGeneratedMeshEntryV2;
struct FResolvedMapCellV2;

class FAsciiGeometryEmitterV2
{
public:
    FAsciiGeometryEmitterV2(
        AAsciiMapBuilderV2Actor& InOwner,
        FAsciiMapGridV2& InGrid,
        const UGeneratedMeshRegistryV2DataAsset& InMeshRegistry,
        const UMapBuilderV2ConfigDataAsset& InConfig);

    void EmitBasePass();
    void EmitStructurePass();
    void EmitInteractiveDecorationOverlayPass();

private:
    bool EmitPrimary(FResolvedMapCellV2& Cell, const TCHAR* VariantKey);
    bool EmitUnderlay(const FResolvedMapCellV2& Cell);
    bool EmitConnectedThinWall(FResolvedMapCellV2& Cell, const FGeneratedMeshEntryV2& Entry);
    bool EmitEntryTransform(
        const FGeneratedMeshEntryV2& Entry,
        FName MaterialSlotId,
        const FTransform& LocalTransform,
        EGeneratedMeshLayerV2 Layer,
        const TCHAR* VariantKey);
    bool EmitActor(
        const FGeneratedMeshEntryV2& Entry,
        UMaterialInterface* Material,
        const FTransform& LocalTransform);

    FTransform BuildPrimaryTransform(const FResolvedMapCellV2& Cell, const FGeneratedMeshEntryV2& Entry);
    FTransform BuildSizedWallTransform(
        const FResolvedMapCellV2& Cell,
        const FGeneratedMeshEntryV2& Entry,
        float LengthCm,
        float ThicknessCm,
        float HeightCm,
        float YawDegrees,
        const FVector& PlanarOffset) const;
    FVector CalculateFitScale(const FResolvedMapCellV2& Cell, const FGeneratedMeshEntryV2& Entry);
    float ResolveTargetHeight(const FResolvedMapCellV2& Cell) const;
    FVector BottomAlignLocation(
        const FVector& BaseLocation,
        const FGeneratedMeshEntryV2& Entry,
        const FVector& Scale) const;
    FName BuildComponentKey(
        FName MeshId,
        FName MaterialSlotId,
        EGeneratedMeshLayerV2 Layer,
        const TCHAR* VariantKey) const;

    AAsciiMapBuilderV2Actor& Owner;
    FAsciiMapGridV2& Grid;
    const UGeneratedMeshRegistryV2DataAsset& MeshRegistry;
    const UMapBuilderV2ConfigDataAsset& Config;
};
