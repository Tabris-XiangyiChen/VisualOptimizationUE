#pragma once

#include "CoreMinimal.h"

// Resolved absolute paths for one entry in map_package_index_v1.
// This is intentionally not tied to either builder Actor.
struct FRuntimeMapPackagePathsV2
{
    FString MapId;
    FString PackageDirectory;
    FString LayoutPath;
    FString ResolvedTileSetPath;
    FString MaterialManifestPath;
};
