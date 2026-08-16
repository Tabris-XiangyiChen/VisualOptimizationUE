#include "MapBuilderV2/AsciiTopologyResolverV2.h"

#include "MapBuilderV2/AsciiMapGridV2.h"
#include "MapBuilderV2/GeneratedMeshRegistryV2DataAsset.h"
#include "MapBuilderV2/MapBuilderV2ConfigDataAsset.h"

namespace
{
    struct FNeighborDirectionV2
    {
        int32 RowOffset;
        int32 ColumnOffset;
        uint8 Bit;
        float FacingYaw;
    };

    static const FNeighborDirectionV2 NeighborDirections[] =
    {
        {-1, 0, AsciiMapV2Connectivity::North, 90.0f},
        {0, 1, AsciiMapV2Connectivity::East, 0.0f},
        {1, 0, AsciiMapV2Connectivity::South, -90.0f},
        {0, -1, AsciiMapV2Connectivity::West, 180.0f}
    };

    bool ContainsSemanticText(const FString& Value, const TCHAR* SearchText)
    {
        return Value.Contains(SearchText, ESearchCase::IgnoreCase);
    }

    bool ContainsSemanticTag(const FAsciiRuntimeTileDefinitionV2& Definition, const TCHAR* SearchText)
    {
        return Definition.SelectedMeshRoleTags.ContainsByPredicate([SearchText](const FString& Tag)
        {
            return Tag.Contains(SearchText, ESearchCase::IgnoreCase);
        });
    }

    bool IsUsableBaseCell(const FResolvedMapCellV2* Cell)
    {
        if (!Cell || !Cell->bGeneratePrimaryMesh || Cell->PrimaryLayer != EGeneratedMeshLayerV2::BaseSurface)
        {
            return false;
        }

        return !Cell->TileDefinition.MaterialSlotId.IsNone();
    }

    bool IsUsableFloorCell(const FResolvedMapCellV2* Cell)
    {
        if (!IsUsableBaseCell(Cell))
        {
            return false;
        }

        const FAsciiRuntimeTileDefinitionV2& Definition = Cell->TileDefinition;
        return !ContainsSemanticText(Definition.Role, TEXT("water"))
            && !ContainsSemanticText(Definition.MaterialFamily, TEXT("water"));
    }

    FName SelectMostFrequentMaterial(const TMap<FName, int32>& Counts)
    {
        FName Winner = NAME_None;
        int32 BestCount = -1;
        FString BestName;

        for (const TPair<FName, int32>& Pair : Counts)
        {
            const FString CandidateName = Pair.Key.ToString();
            if (Pair.Value > BestCount || (Pair.Value == BestCount && (BestName.IsEmpty() || CandidateName < BestName)))
            {
                Winner = Pair.Key;
                BestCount = Pair.Value;
                BestName = CandidateName;
            }
        }

        return Winner;
    }

    int32 CountSetBits(const uint8 Mask)
    {
        int32 Count = 0;
        for (uint8 Bit = 1; Bit <= AsciiMapV2Connectivity::West; Bit <<= 1)
        {
            Count += (Mask & Bit) != 0 ? 1 : 0;
        }
        return Count;
    }
}

bool FAsciiTopologyResolverV2::Resolve(
    FAsciiMapGridV2& Grid,
    const UGeneratedMeshRegistryV2DataAsset& MeshRegistry,
    const UMapBuilderV2ConfigDataAsset& Config,
    FName& OutDominantFloorMaterial,
    TSet<FName>& OutMissingMeshIds)
{
    for (FResolvedMapCellV2& Cell : Grid.GetCells())
    {
        const FAsciiRuntimeTileDefinitionV2& Definition = Cell.TileDefinition;
        if (!Definition.bGenerate || Definition.MeshId.IsNone())
        {
            continue;
        }

        const FGeneratedMeshEntryV2* Entry = MeshRegistry.FindMeshById(Definition.MeshId);
        if (!Entry)
        {
            OutMissingMeshIds.Add(Definition.MeshId);
            continue;
        }

        Cell.PrimaryLayer = Entry->PrimaryLayer;
        Cell.CoveragePolicy = Entry->CoveragePolicy;
        Cell.OrientationPolicy = Entry->OrientationPolicy;
        Cell.FitMode = Entry->FitMode;
        Cell.ResolvedRotation = Entry->RotationOffset;
        Cell.bGeneratePrimaryMesh = Entry->PrimaryMesh != nullptr || (Entry->bSpawnActor && Entry->ActorClass != nullptr);
        Cell.bSpawnAsActor = Entry->bSpawnActor;
        Cell.bGenerateUnderlay = Entry->CoveragePolicy == EBaseCoveragePolicyV2::RequiresUnderlay;
    }

    OutDominantFloorMaterial = FindDominantFloorMaterial(Grid);

    for (FResolvedMapCellV2& Cell : Grid.GetCells())
    {
        if (!Cell.bGeneratePrimaryMesh)
        {
            continue;
        }

        Cell.ConnectivityMask = BuildConnectivityMask(Grid, Cell);
        ResolveOrientation(Grid, Cell);

        if (Cell.bGenerateUnderlay)
        {
            ResolveUnderlay(Grid, Cell, Config, OutDominantFloorMaterial);
        }
    }

    return true;
}

bool FAsciiTopologyResolverV2::IsWallLike(const FResolvedMapCellV2& Cell)
{
    const FAsciiRuntimeTileDefinitionV2& Definition = Cell.TileDefinition;
    return ContainsSemanticText(Definition.Role, TEXT("wall"))
        || ContainsSemanticTag(Definition, TEXT("wall"));
}

bool FAsciiTopologyResolverV2::IsDoorLike(const FResolvedMapCellV2& Cell)
{
    const FAsciiRuntimeTileDefinitionV2& Definition = Cell.TileDefinition;
    return ContainsSemanticText(Definition.Role, TEXT("door"))
        || ContainsSemanticText(Definition.Role, TEXT("gate"))
        || ContainsSemanticTag(Definition, TEXT("door"))
        || ContainsSemanticTag(Definition, TEXT("gate"));
}

uint8 FAsciiTopologyResolverV2::BuildConnectivityMask(const FAsciiMapGridV2& Grid, const FResolvedMapCellV2& Cell)
{
    if (Cell.OrientationPolicy != EOrientationPolicyV2::ConnectFourDirections)
    {
        return 0;
    }

    uint8 Mask = 0;
    const bool bCurrentIsWall = IsWallLike(Cell);

    for (const FNeighborDirectionV2& Direction : NeighborDirections)
    {
        const FResolvedMapCellV2* Neighbor = Grid.GetCell(Cell.Row + Direction.RowOffset, Cell.Column + Direction.ColumnOffset);
        if (!Neighbor || !Neighbor->bGeneratePrimaryMesh)
        {
            continue;
        }

        const bool bConnects = bCurrentIsWall
            ? (IsWallLike(*Neighbor) || IsDoorLike(*Neighbor))
            : (Neighbor->OrientationPolicy == EOrientationPolicyV2::ConnectFourDirections
                && Neighbor->TileDefinition.Role.Equals(Cell.TileDefinition.Role, ESearchCase::IgnoreCase));

        if (bConnects)
        {
            Mask |= Direction.Bit;
        }
    }

    return Mask;
}

void FAsciiTopologyResolverV2::ResolveOrientation(const FAsciiMapGridV2& Grid, FResolvedMapCellV2& Cell)
{
    switch (Cell.OrientationPolicy)
    {
    case EOrientationPolicyV2::Fixed:
    case EOrientationPolicyV2::Manual:
        return;

    case EOrientationPolicyV2::ConnectFourDirections:
    {
        const bool bHorizontal = (Cell.ConnectivityMask & (AsciiMapV2Connectivity::East | AsciiMapV2Connectivity::West)) != 0;
        const bool bVertical = (Cell.ConnectivityMask & (AsciiMapV2Connectivity::North | AsciiMapV2Connectivity::South)) != 0;
        if (bHorizontal && !bVertical)
        {
            Cell.ResolvedRotation.Yaw += 0.0f;
        }
        else if (bVertical && !bHorizontal)
        {
            Cell.ResolvedRotation.Yaw += 90.0f;
        }
        else if (CountSetBits(Cell.ConnectivityMask) == 0)
        {
            Cell.bOrientationAmbiguous = true;
        }
        return;
    }

    case EOrientationPolicyV2::AlignToAdjacentWalls:
    {
        int32 HorizontalWallCount = 0;
        int32 VerticalWallCount = 0;
        for (const FNeighborDirectionV2& Direction : NeighborDirections)
        {
            const FResolvedMapCellV2* Neighbor = Grid.GetCell(Cell.Row + Direction.RowOffset, Cell.Column + Direction.ColumnOffset);
            if (!Neighbor || !Neighbor->bGeneratePrimaryMesh || !IsWallLike(*Neighbor))
            {
                continue;
            }

            if (Direction.Bit == AsciiMapV2Connectivity::East || Direction.Bit == AsciiMapV2Connectivity::West)
            {
                ++HorizontalWallCount;
            }
            else
            {
                ++VerticalWallCount;
            }
        }

        if (HorizontalWallCount > VerticalWallCount)
        {
            Cell.ResolvedRotation.Yaw += 0.0f;
        }
        else if (VerticalWallCount > HorizontalWallCount)
        {
            Cell.ResolvedRotation.Yaw += 90.0f;
        }
        else
        {
            Cell.bOrientationAmbiguous = true;
        }
        return;
    }

    case EOrientationPolicyV2::FaceOpenSpace:
    {
        int32 CandidateCount = 0;
        float CandidateYaw = Cell.ResolvedRotation.Yaw;
        for (const FNeighborDirectionV2& Direction : NeighborDirections)
        {
            const FResolvedMapCellV2* Neighbor = Grid.GetCell(Cell.Row + Direction.RowOffset, Cell.Column + Direction.ColumnOffset);
            if (IsUsableFloorCell(Neighbor))
            {
                CandidateYaw = Direction.FacingYaw;
                ++CandidateCount;
            }
        }

        if (CandidateCount > 0)
        {
            Cell.ResolvedRotation.Yaw = CandidateYaw;
            Cell.bOrientationAmbiguous = CandidateCount > 1;
        }
        else
        {
            Cell.bOrientationAmbiguous = true;
        }
        return;
    }
    }
}

void FAsciiTopologyResolverV2::ResolveUnderlay(
    const FAsciiMapGridV2& Grid,
    FResolvedMapCellV2& Cell,
    const UMapBuilderV2ConfigDataAsset& Config,
    const FName DominantFloorMaterial)
{
    Cell.ResolvedUnderlayMeshId = Config.FallbackFloorMeshId;
    TMap<FName, int32> PreferredCounts;
    TMap<FName, int32> AllCounts;

    bool bPreferNorthSouth = false;
    bool bPreferEastWest = false;
    if (IsDoorLike(Cell) || Cell.OrientationPolicy == EOrientationPolicyV2::AlignToAdjacentWalls)
    {
        int32 HorizontalWalls = 0;
        int32 VerticalWalls = 0;
        for (const FNeighborDirectionV2& Direction : NeighborDirections)
        {
            const FResolvedMapCellV2* Neighbor = Grid.GetCell(Cell.Row + Direction.RowOffset, Cell.Column + Direction.ColumnOffset);
            if (!Neighbor || !IsWallLike(*Neighbor))
            {
                continue;
            }

            if (Direction.Bit == AsciiMapV2Connectivity::East || Direction.Bit == AsciiMapV2Connectivity::West)
            {
                ++HorizontalWalls;
            }
            else
            {
                ++VerticalWalls;
            }
        }
        bPreferNorthSouth = HorizontalWalls > VerticalWalls;
        bPreferEastWest = VerticalWalls > HorizontalWalls;
    }

    for (const FNeighborDirectionV2& Direction : NeighborDirections)
    {
        const FResolvedMapCellV2* Neighbor = Grid.GetCell(Cell.Row + Direction.RowOffset, Cell.Column + Direction.ColumnOffset);
        if (!IsUsableBaseCell(Neighbor))
        {
            continue;
        }

        const FName MaterialId = Neighbor->TileDefinition.MaterialSlotId;
        AllCounts.FindOrAdd(MaterialId)++;

        const bool bNorthSouthDirection = Direction.Bit == AsciiMapV2Connectivity::North || Direction.Bit == AsciiMapV2Connectivity::South;
        const bool bEastWestDirection = Direction.Bit == AsciiMapV2Connectivity::East || Direction.Bit == AsciiMapV2Connectivity::West;
        if ((bPreferNorthSouth && bNorthSouthDirection) || (bPreferEastWest && bEastWestDirection))
        {
            PreferredCounts.FindOrAdd(MaterialId)++;
        }
    }

    Cell.ResolvedUnderlayMaterialSlotId = SelectMostFrequentMaterial(PreferredCounts);
    if (!Cell.ResolvedUnderlayMaterialSlotId.IsNone())
    {
        Cell.UnderlayResolutionSource = TEXT("door_open_sides");
        return;
    }

    Cell.ResolvedUnderlayMaterialSlotId = SelectMostFrequentMaterial(AllCounts);
    if (!Cell.ResolvedUnderlayMaterialSlotId.IsNone())
    {
        Cell.UnderlayResolutionSource = TEXT("adjacent_base_surface");
        return;
    }

    if (!DominantFloorMaterial.IsNone())
    {
        Cell.ResolvedUnderlayMaterialSlotId = DominantFloorMaterial;
        Cell.UnderlayResolutionSource = TEXT("map_dominant_floor");
        return;
    }

    Cell.ResolvedUnderlayMaterialSlotId = Config.FallbackFloorMaterialSlotId;
    Cell.UnderlayResolutionSource = Cell.ResolvedUnderlayMaterialSlotId.IsNone()
        ? TEXT("unresolved")
        : TEXT("config_fallback");
}

FName FAsciiTopologyResolverV2::FindDominantFloorMaterial(const FAsciiMapGridV2& Grid)
{
    TMap<FName, int32> Counts;
    for (const FResolvedMapCellV2& Cell : Grid.GetCells())
    {
        if (IsUsableFloorCell(&Cell))
        {
            Counts.FindOrAdd(Cell.TileDefinition.MaterialSlotId)++;
        }
    }
    return SelectMostFrequentMaterial(Counts);
}
