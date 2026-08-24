#include "MapBuilderV2/AsciiGeometryEmitterV2.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "MapBuilderV2/AsciiMapBuilderV2Actor.h"
#include "MapBuilderV2/AsciiMapGridV2.h"
#include "MapBuilderV2/AsciiTopologyResolverV2.h"
#include "MapBuilderV2/GeneratedMeshRegistryV2DataAsset.h"
#include "MapBuilderV2/MapBuilderV2ConfigDataAsset.h"
#include "Materials/MaterialInterface.h"

namespace
{
    float SignedScaleForTarget(const float TargetSize, const float MeshSize, const float DefaultScale)
    {
        if (FMath::IsNearlyZero(MeshSize))
        {
            return DefaultScale;
        }
        return FMath::Sign(DefaultScale == 0.0f ? 1.0f : DefaultScale) * TargetSize / MeshSize;
    }

    bool IsHorizontalMask(const uint8 Mask)
    {
        return Mask != 0 && (Mask & ~(AsciiMapV2Connectivity::East | AsciiMapV2Connectivity::West)) == 0;
    }

    bool IsVerticalMask(const uint8 Mask)
    {
        return Mask != 0 && (Mask & ~(AsciiMapV2Connectivity::North | AsciiMapV2Connectivity::South)) == 0;
    }
}

FAsciiGeometryEmitterV2::FAsciiGeometryEmitterV2(
    AAsciiMapBuilderV2Actor& InOwner,
    FAsciiMapGridV2& InGrid,
    const UGeneratedMeshRegistryV2DataAsset& InMeshRegistry,
    const UMapBuilderV2ConfigDataAsset& InConfig)
    : Owner(InOwner)
    , Grid(InGrid)
    , MeshRegistry(InMeshRegistry)
    , Config(InConfig)
{
}

void FAsciiGeometryEmitterV2::EmitBasePass()
{
    for (FResolvedMapCellV2& Cell : Grid.GetCells())
    {
        if (!Cell.bGeneratePrimaryMesh)
        {
            continue;
        }

        if (Cell.PrimaryLayer == EGeneratedMeshLayerV2::BaseSurface)
        {
            if (EmitPrimary(Cell, TEXT("base_primary")))
            {
                ++Owner.GeneratedBaseSurfaceCount;
            }
        }
        else if (Cell.bGenerateUnderlay && EmitUnderlay(Cell))
        {
            ++Owner.GeneratedUnderlayCount;
        }
    }
}

void FAsciiGeometryEmitterV2::EmitStructurePass()
{
    for (FResolvedMapCellV2& Cell : Grid.GetCells())
    {
        if (!Cell.bGeneratePrimaryMesh || Cell.PrimaryLayer != EGeneratedMeshLayerV2::Structure)
        {
            continue;
        }

        const FGeneratedMeshEntryV2* Entry = MeshRegistry.FindMeshById(Cell.TileDefinition.MeshId);
        if (!Entry)
        {
            Owner.MissingMeshIds.Add(Cell.TileDefinition.MeshId);
            continue;
        }

        const bool bConnectedThinWall = FAsciiTopologyResolverV2::IsWallLike(Cell)
            && Entry->bUseConnectedWallAssembly
            && Entry->OrientationPolicy == EOrientationPolicyV2::ConnectFourDirections
            && Entry->CoveragePolicy == EBaseCoveragePolicyV2::RequiresUnderlay
            && Entry->PrimaryMesh != nullptr;

        const bool bEmitted = bConnectedThinWall
            ? EmitConnectedThinWall(Cell, *Entry)
            : EmitPrimary(Cell, TEXT("structure_primary"));
        if (bEmitted)
        {
            ++Owner.GeneratedStructureCount;
        }
    }
}

void FAsciiGeometryEmitterV2::EmitInteractiveDecorationOverlayPass()
{
    for (FResolvedMapCellV2& Cell : Grid.GetCells())
    {
        if (!Cell.bGeneratePrimaryMesh
            || Cell.PrimaryLayer == EGeneratedMeshLayerV2::BaseSurface
            || Cell.PrimaryLayer == EGeneratedMeshLayerV2::Structure)
        {
            continue;
        }

        if (!EmitPrimary(Cell, TEXT("object_primary")))
        {
            continue;
        }

        switch (Cell.PrimaryLayer)
        {
        case EGeneratedMeshLayerV2::Interactive:
            Owner.GeneratedInteractiveActorCount += Cell.bSpawnAsActor ? 1 : 0;
            break;
        case EGeneratedMeshLayerV2::Decoration:
            ++Owner.GeneratedDecorationCount;
            break;
        case EGeneratedMeshLayerV2::Overlay:
            ++Owner.GeneratedOverlayCount;
            break;
        default:
            break;
        }
    }
}

bool FAsciiGeometryEmitterV2::EmitPrimary(FResolvedMapCellV2& Cell, const TCHAR* VariantKey)
{
    const FGeneratedMeshEntryV2* Entry = MeshRegistry.FindMeshById(Cell.TileDefinition.MeshId);
    if (!Entry)
    {
        Owner.MissingMeshIds.Add(Cell.TileDefinition.MeshId);
        return false;
    }

    const FTransform Transform = BuildPrimaryTransform(Cell, *Entry);
    Cell.ResolvedScale = Transform.GetScale3D();
    Cell.ResolvedLocation = Transform.GetLocation();
    return EmitEntryTransform(
        *Entry,
        Cell.TileDefinition.MaterialSlotId,
        Transform,
        Cell.PrimaryLayer,
        VariantKey);
}

bool FAsciiGeometryEmitterV2::EmitUnderlay(const FResolvedMapCellV2& Cell)
{
    if (Cell.bGenerateUnderlay && Cell.ResolvedUnderlayMaterialSlotId.IsNone())
    {
        ++Owner.UnresolvedUnderlayCount;
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("MapBuilderV2: underlay material unresolved: map=%s row=%d column=%d primaryMesh=%s resolutionSource=%s"),
            *Owner.CurrentRuntimeMapId,
            Cell.Row,
            Cell.Column,
            *Cell.TileDefinition.MeshId.ToString(),
            *Cell.UnderlayResolutionSource);
    }

    const FGeneratedMeshEntryV2* UnderlayEntry = MeshRegistry.FindMeshById(Cell.ResolvedUnderlayMeshId);
    if (!UnderlayEntry)
    {
        Owner.MissingMeshIds.Add(Cell.ResolvedUnderlayMeshId);
        return false;
    }

    FResolvedMapCellV2 UnderlayCell = Cell;
    UnderlayCell.TileDefinition.MeshId = Cell.ResolvedUnderlayMeshId;
    UnderlayCell.TileDefinition.MaterialSlotId = Cell.ResolvedUnderlayMaterialSlotId;
    UnderlayCell.TileDefinition.Height = 0.0f;
    UnderlayCell.TileDefinition.ZOffset = 0.0f;
    UnderlayCell.ResolvedLocation.Z = 0.0f;
    UnderlayCell.PrimaryLayer = EGeneratedMeshLayerV2::BaseSurface;
    UnderlayCell.FitMode = UnderlayEntry->FitMode;
    UnderlayCell.ResolvedRotation = UnderlayEntry->RotationOffset;

    const FTransform Transform = BuildPrimaryTransform(UnderlayCell, *UnderlayEntry);
    return EmitEntryTransform(
        *UnderlayEntry,
        Cell.ResolvedUnderlayMaterialSlotId,
        Transform,
        EGeneratedMeshLayerV2::BaseSurface,
        TEXT("base_underlay"));
}

bool FAsciiGeometryEmitterV2::EmitConnectedThinWall(
    FResolvedMapCellV2& Cell,
    const FGeneratedMeshEntryV2& Entry)
{
    const float Height = ResolveTargetHeight(Cell);
    const float Thickness = FMath::Min(Config.ThinWallThicknessCm, Config.TileSizeCm);
    const float HalfArmLength = Config.TileSizeCm * 0.5f + Config.ThinWallArmOverlapCm;
    const uint8 Mask = Cell.ConnectivityMask;
    bool bEmitted = false;

    auto EmitSizedPart = [this, &Cell, &Entry, Height, Thickness, &bEmitted](
        const float Length,
        const float Yaw,
        const FVector& Offset,
        const TCHAR* Variant)
    {
        const FTransform Transform = BuildSizedWallTransform(Cell, Entry, Length, Thickness, Height, Yaw, Offset);
        bEmitted |= EmitEntryTransform(
            Entry,
            Cell.TileDefinition.MaterialSlotId,
            Transform,
            EGeneratedMeshLayerV2::Structure,
            Variant);
    };

    if (Mask == 0 || IsHorizontalMask(Mask))
    {
        EmitSizedPart(Config.TileSizeCm + Config.ThinWallArmOverlapCm * 2.0f, 0.0f, FVector::ZeroVector, TEXT("wall_full_horizontal"));
    }
    else if (IsVerticalMask(Mask))
    {
        EmitSizedPart(Config.TileSizeCm + Config.ThinWallArmOverlapCm * 2.0f, 90.0f, FVector::ZeroVector, TEXT("wall_full_vertical"));
    }
    else
    {
        const FTransform CenterTransform = BuildSizedWallTransform(
            Cell,
            Entry,
            Thickness,
            Thickness,
            Height,
            0.0f,
            FVector::ZeroVector);
        bEmitted |= EmitEntryTransform(
            Entry,
            Cell.TileDefinition.MaterialSlotId,
            CenterTransform,
            EGeneratedMeshLayerV2::Structure,
            TEXT("wall_center"));

        if ((Mask & AsciiMapV2Connectivity::East) != 0)
        {
            EmitSizedPart(HalfArmLength, 0.0f, FVector(-Config.TileSizeCm * 0.25f, 0.0f, 0.0f), TEXT("wall_arm_horizontal"));
        }
        if ((Mask & AsciiMapV2Connectivity::West) != 0)
        {
            EmitSizedPart(HalfArmLength, 0.0f, FVector(Config.TileSizeCm * 0.25f, 0.0f, 0.0f), TEXT("wall_arm_horizontal"));
        }
        if ((Mask & AsciiMapV2Connectivity::North) != 0)
        {
            EmitSizedPart(HalfArmLength, 90.0f, FVector(0.0f, Config.TileSizeCm * 0.25f, 0.0f), TEXT("wall_arm_vertical"));
        }
        if ((Mask & AsciiMapV2Connectivity::South) != 0)
        {
            EmitSizedPart(HalfArmLength, 90.0f, FVector(0.0f, -Config.TileSizeCm * 0.25f, 0.0f), TEXT("wall_arm_vertical"));
        }
    }

    Cell.ResolvedScale = Entry.DefaultScale;
    return bEmitted;
}

bool FAsciiGeometryEmitterV2::EmitEntryTransform(
    const FGeneratedMeshEntryV2& Entry,
    const FName MaterialSlotId,
    const FTransform& LocalTransform,
    const EGeneratedMeshLayerV2 Layer,
    const TCHAR* VariantKey)
{
    UMaterialInterface* Material = Entry.bOverrideMaterialSlotZero ? Owner.ResolveMaterial(MaterialSlotId) : nullptr;
    if (Entry.bOverrideMaterialSlotZero && !MaterialSlotId.IsNone() && !Material)
    {
        Owner.MissingMaterialSlotIds.Add(MaterialSlotId);
    }

    if (Entry.bSpawnActor)
    {
        return EmitActor(Entry, Material, LocalTransform);
    }

    if (!Entry.PrimaryMesh)
    {
        Owner.MissingMeshIds.Add(Entry.MeshId);
        return false;
    }

    if (!Entry.bUseInstancing)
    {
        UWorld* World = Owner.GetWorld();
        if (!World)
        {
            return false;
        }

        FActorSpawnParameters SpawnParameters;
        SpawnParameters.Owner = &Owner;
        SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        const FTransform WorldTransform = LocalTransform * Owner.GetActorTransform();
        AStaticMeshActor* StaticMeshActor = World->SpawnActor<AStaticMeshActor>(
            AStaticMeshActor::StaticClass(),
            WorldTransform,
            SpawnParameters);
        if (!StaticMeshActor)
        {
            return false;
        }

        StaticMeshActor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
        StaticMeshActor->GetStaticMeshComponent()->SetStaticMesh(Entry.PrimaryMesh);
        StaticMeshActor->GetStaticMeshComponent()->SetCollisionEnabled(
            Entry.bBlocksTile ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
        if (Material)
        {
            StaticMeshActor->GetStaticMeshComponent()->SetMaterial(0, Material);
        }
        Owner.RuntimeSpawnedActors.Add(StaticMeshActor);
        return true;
    }

    const FName ComponentKey = BuildComponentKey(Entry.MeshId, MaterialSlotId, Layer, VariantKey);
    UInstancedStaticMeshComponent* Component = Owner.GetOrCreateInstanceComponent(ComponentKey, Entry.PrimaryMesh, Material);
    if (!Component)
    {
        return false;
    }
    Component->SetCollisionEnabled(Entry.bBlocksTile ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    Component->AddInstance(LocalTransform, false);
    return true;
}

bool FAsciiGeometryEmitterV2::EmitActor(
    const FGeneratedMeshEntryV2& Entry,
    UMaterialInterface* Material,
    const FTransform& LocalTransform)
{
    if (Entry.ActorClass == nullptr || !Owner.GetWorld())
    {
        Owner.MissingMeshIds.Add(Entry.MeshId);
        return false;
    }

    FActorSpawnParameters SpawnParameters;
    SpawnParameters.Owner = &Owner;
    SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FTransform WorldTransform = LocalTransform * Owner.GetActorTransform();
    AActor* SpawnedActor = Owner.GetWorld()->SpawnActor<AActor>(
        Entry.ActorClass,
        WorldTransform,
        SpawnParameters);
    if (!SpawnedActor)
    {
        return false;
    }

    if (Material)
    {
        TInlineComponentArray<UMeshComponent*> MeshComponents;
        SpawnedActor->GetComponents(MeshComponents);
        for (UMeshComponent* MeshComponent : MeshComponents)
        {
            if (MeshComponent && MeshComponent->GetNumMaterials() > 0)
            {
                MeshComponent->SetMaterial(0, Material);
            }
        }
    }

    Owner.RuntimeSpawnedActors.Add(SpawnedActor);
    return true;
}

FTransform FAsciiGeometryEmitterV2::BuildPrimaryTransform(
    const FResolvedMapCellV2& Cell,
    const FGeneratedMeshEntryV2& Entry)
{
    const FVector Scale = CalculateFitScale(Cell, Entry);
    FVector Location = Cell.ResolvedLocation + Entry.LocationOffset;
    Location = BottomAlignLocation(Location, Entry, Scale);
    return FTransform(Cell.ResolvedRotation, Location, Scale);
}

FTransform FAsciiGeometryEmitterV2::BuildSizedWallTransform(
    const FResolvedMapCellV2& Cell,
    const FGeneratedMeshEntryV2& Entry,
    const float LengthCm,
    const float ThicknessCm,
    const float HeightCm,
    const float YawDegrees,
    const FVector& PlanarOffset) const
{
    if (!Entry.PrimaryMesh)
    {
        return FTransform::Identity;
    }

    const FVector MeshSize = Entry.PrimaryMesh->GetBounds().BoxExtent * 2.0f;
    const FVector Scale(
        SignedScaleForTarget(LengthCm, MeshSize.X, Entry.DefaultScale.X),
        SignedScaleForTarget(ThicknessCm, MeshSize.Y, Entry.DefaultScale.Y),
        SignedScaleForTarget(HeightCm, MeshSize.Z, Entry.DefaultScale.Z));
    const FVector Location = BottomAlignLocation(
        Cell.ResolvedLocation + Entry.LocationOffset + PlanarOffset,
        Entry,
        Scale);
    FRotator Rotation = Entry.RotationOffset;
    Rotation.Yaw += YawDegrees;
    return FTransform(Rotation, Location, Scale);
}

FVector FAsciiGeometryEmitterV2::CalculateFitScale(
    const FResolvedMapCellV2& Cell,
    const FGeneratedMeshEntryV2& Entry)
{
    if (!Entry.PrimaryMesh)
    {
        return Entry.DefaultScale;
    }

    const FVector MeshSize = Entry.PrimaryMesh->GetBounds().BoxExtent * 2.0f;
    FVector Scale = Entry.DefaultScale;
    const float SafeX = FMath::Max(FMath::Abs(MeshSize.X * Scale.X), KINDA_SMALL_NUMBER);
    const float SafeY = FMath::Max(FMath::Abs(MeshSize.Y * Scale.Y), KINDA_SMALL_NUMBER);
    const float SafeZ = FMath::Max(FMath::Abs(MeshSize.Z * Scale.Z), KINDA_SMALL_NUMBER);

    switch (Entry.FitMode)
    {
    case EMeshFitModeV2::PreserveNative:
        break;

    case EMeshFitModeV2::UniformFitWidth:
    {
        const float Factor = Config.TileSizeCm / SafeX;
        Scale *= Factor;
        break;
    }

    case EMeshFitModeV2::UniformFitHeight:
    {
        const float TargetHeight = ResolveTargetHeight(Cell);
        if (TargetHeight > KINDA_SMALL_NUMBER)
        {
            Scale *= TargetHeight / SafeZ;
        }
        break;
    }

    case EMeshFitModeV2::UniformFitFootprint:
    {
        const float Factor = FMath::Min(Config.TileSizeCm / SafeX, Config.TileSizeCm / SafeY);
        Scale *= Factor;
        break;
    }

    case EMeshFitModeV2::StretchToTile:
    {
        Scale.X = SignedScaleForTarget(Config.TileSizeCm, MeshSize.X, Entry.DefaultScale.X);
        Scale.Y = SignedScaleForTarget(Config.TileSizeCm, MeshSize.Y, Entry.DefaultScale.Y);
        const float TargetHeight = ResolveTargetHeight(Cell);
        if (TargetHeight > KINDA_SMALL_NUMBER && MeshSize.Z > KINDA_SMALL_NUMBER)
        {
            Scale.Z = SignedScaleForTarget(TargetHeight, MeshSize.Z, Entry.DefaultScale.Z);
        }
        ++Owner.NonUniformStretchCount;
        break;
    }
    }

    return Scale;
}

float FAsciiGeometryEmitterV2::ResolveTargetHeight(const FResolvedMapCellV2& Cell) const
{
    if (Cell.TileDefinition.Height > KINDA_SMALL_NUMBER)
    {
        return Cell.TileDefinition.Height;
    }
    if (FAsciiTopologyResolverV2::IsWallLike(Cell))
    {
        return Config.DefaultWallHeightCm;
    }
    if (FAsciiTopologyResolverV2::IsDoorLike(Cell))
    {
        return Config.DefaultDoorHeightCm;
    }
    return 0.0f;
}

FVector FAsciiGeometryEmitterV2::BottomAlignLocation(
    const FVector& BaseLocation,
    const FGeneratedMeshEntryV2& Entry,
    const FVector& Scale) const
{
    if (!Entry.bBottomAlignToCellBase || !Entry.PrimaryMesh)
    {
        return BaseLocation;
    }

    FVector Result = BaseLocation;
    const FBoxSphereBounds Bounds = Entry.PrimaryMesh->GetBounds();
    const float LocalBottom = Bounds.Origin.Z - Bounds.BoxExtent.Z;
    Result.Z -= LocalBottom * Scale.Z;
    return Result;
}

FName FAsciiGeometryEmitterV2::BuildComponentKey(
    const FName MeshId,
    const FName MaterialSlotId,
    const EGeneratedMeshLayerV2 Layer,
    const TCHAR* VariantKey) const
{
    FString Key = FString::Printf(
        TEXT("V2_%s_%s_L%d_%s"),
        *MeshId.ToString(),
        *MaterialSlotId.ToString(),
        static_cast<int32>(Layer),
        VariantKey);
    for (TCHAR& Character : Key)
    {
        if (!FChar::IsAlnum(Character) && Character != TEXT('_'))
        {
            Character = TEXT('_');
        }
    }
    return FName(*Key);
}
