#pragma once

#include "CoreMinimal.h"

class FAsciiMapGridV2;
class UGeneratedMeshRegistryV2DataAsset;
class UMapBuilderV2ConfigDataAsset;
struct FResolvedMapCellV2;

class FAsciiTopologyResolverV2
{
public:
    static bool Resolve(
        FAsciiMapGridV2& Grid,
        const UGeneratedMeshRegistryV2DataAsset& MeshRegistry,
        const UMapBuilderV2ConfigDataAsset& Config,
        FName& OutDominantFloorMaterial,
        TSet<FName>& OutMissingMeshIds);

    static bool IsWallLike(const FResolvedMapCellV2& Cell);
    static bool IsDoorLike(const FResolvedMapCellV2& Cell);

private:
    static uint8 BuildConnectivityMask(const FAsciiMapGridV2& Grid, const FResolvedMapCellV2& Cell);
    static void ResolveOrientation(const FAsciiMapGridV2& Grid, FResolvedMapCellV2& Cell);
    static void ResolveUnderlay(
        const FAsciiMapGridV2& Grid,
        FResolvedMapCellV2& Cell,
        const UMapBuilderV2ConfigDataAsset& Config,
        FName DominantFloorMaterial);
    static FName FindDominantFloorMaterial(const FAsciiMapGridV2& Grid);
};
