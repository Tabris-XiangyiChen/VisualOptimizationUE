#include "MapBuilderV2/AsciiMapBuilderV2Actor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "MapBuilderV2/AsciiGeometryEmitterV2.h"
#include "MapBuilderV2/AsciiMapGridV2.h"
#include "MapBuilderV2/AsciiTopologyResolverV2.h"
#include "MapBuilderV2/GeneratedMeshRegistryV2DataAsset.h"
#include "MapBuilderV2/MapBuilderV2ConfigDataAsset.h"
#include "MaterialRegistryDataAsset.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RuntimeCommon/AsciiRuntimeMaterialProvider.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectGlobals.h"

namespace
{
    bool TryGetOptionalName(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, FName& OutName)
    {
        FString Value;
        if (Object.IsValid() && Object->TryGetStringField(FieldName, Value) && !Value.IsEmpty())
        {
            OutName = FName(*Value);
            return true;
        }
        OutName = NAME_None;
        return false;
    }

    FString GetOptionalString(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName)
    {
        FString Value;
        if (Object.IsValid())
        {
            Object->TryGetStringField(FieldName, Value);
        }
        return Value;
    }

    float GetOptionalFloat(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, const float DefaultValue)
    {
        double Value = DefaultValue;
        return Object.IsValid() && Object->TryGetNumberField(FieldName, Value)
            ? static_cast<float>(Value)
            : DefaultValue;
    }

    int32 GetOptionalInt(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, const int32 DefaultValue)
    {
        double Value = DefaultValue;
        return Object.IsValid() && Object->TryGetNumberField(FieldName, Value)
            ? FMath::RoundToInt(Value)
            : DefaultValue;
    }

    bool GetOptionalBool(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, const bool DefaultValue)
    {
        bool Value = DefaultValue;
        return Object.IsValid() && Object->TryGetBoolField(FieldName, Value) ? Value : DefaultValue;
    }
}

AAsciiMapBuilderV2Actor::AAsciiMapBuilderV2Actor()
{
    PrimaryActorTick.bCanEverTick = false;
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);
}

void AAsciiMapBuilderV2Actor::BeginPlay()
{
    Super::BeginPlay();
    if (bGenerateOnBeginPlay)
    {
        GenerateMapV2();
    }
}

void AAsciiMapBuilderV2Actor::GenerateMapV2()
{
    ClearGeneratedMapV2();
    ResetGenerationStats();

    if (!BuilderConfig || !MeshRegistryV2)
    {
        UE_LOG(LogTemp, Error, TEXT("MapBuilderV2: BuilderConfig and MeshRegistryV2 must both be assigned."));
        return;
    }

    FRuntimeMapPackagePathsV2 PackagePaths;
    FString Error;
    if (!ResolveSelectedMapPackage(PackagePaths, Error))
    {
        UE_LOG(LogTemp, Error, TEXT("MapBuilderV2: %s"), *Error);
        return;
    }
    CurrentRuntimeMapId = PackagePaths.MapId;

    TMap<FString, FAsciiRuntimeTileDefinitionV2> Definitions;
    TArray<FString> Lines;
    if (!LoadRuntimeTileDefinitions(PackagePaths.ResolvedTileSetPath, Definitions, Error)
        || !LoadMapLines(PackagePaths.LayoutPath, Lines, Error))
    {
        UE_LOG(LogTemp, Error, TEXT("MapBuilderV2: %s"), *Error);
        return;
    }

    FAsciiMapGridV2 Grid;
    if (!BuildSemanticGrid(Lines, Definitions, Grid, Error))
    {
        UE_LOG(LogTemp, Error, TEXT("MapBuilderV2: %s"), *Error);
        return;
    }
    ValidateRuntimeDefinitions(Definitions);

    RuntimeMaterialProvider = NewObject<UAsciiRuntimeMaterialProvider>(this);
    if (RuntimeMaterialProvider && !PackagePaths.MaterialManifestPath.IsEmpty())
    {
        FString MaterialError;
        if (!RuntimeMaterialProvider->Rebuild(
            this,
            PackagePaths.MaterialManifestPath,
            RuntimeMaterialMaster,
            BuilderConfig->TileSizeCm,
            MaterialError))
        {
            UE_LOG(LogTemp, Warning, TEXT("MapBuilderV2: Runtime materials unavailable (%s). Registry fallback will be used where possible."), *MaterialError);
        }
    }

    FName DominantFloorMaterial = NAME_None;
    FAsciiTopologyResolverV2::Resolve(Grid, *MeshRegistryV2, *BuilderConfig, DominantFloorMaterial, MissingMeshIds);

    for (const FResolvedMapCellV2& Cell : Grid.GetCells())
    {
        AmbiguousOrientationCount += Cell.bOrientationAmbiguous ? 1 : 0;
        if (FAsciiTopologyResolverV2::IsWallLike(Cell) && Cell.bGeneratePrimaryMesh)
        {
            WallConnectivityDistribution.FindOrAdd(Cell.ConnectivityMask)++;
        }
    }

    FAsciiGeometryEmitterV2 Emitter(*this, Grid, *MeshRegistryV2, *BuilderConfig);
    if (BuilderConfig->bGenerateBasePass)
    {
        Emitter.EmitBasePass();
    }
    if (BuilderConfig->bGenerateStructurePass)
    {
        Emitter.EmitStructurePass();
    }
    if (BuilderConfig->bGenerateInteractiveAndDecorationPass)
    {
        Emitter.EmitInteractiveDecorationOverlayPass();
    }

    LastResolvedCells = Grid.GetCells();
    DrawDebugCells(Grid);
    LogGenerationSummary(Grid, DominantFloorMaterial);
}

void AAsciiMapBuilderV2Actor::ClearGeneratedMapV2()
{
    for (TPair<FName, UInstancedStaticMeshComponent*>& Pair : RuntimeInstanceComponents)
    {
        if (IsValid(Pair.Value))
        {
            Pair.Value->DestroyComponent();
        }
    }
    RuntimeInstanceComponents.Reset();

    for (AActor* SpawnedActor : RuntimeSpawnedActors)
    {
        if (IsValid(SpawnedActor))
        {
            SpawnedActor->Destroy();
        }
    }
    RuntimeSpawnedActors.Reset();

    if (RuntimeMaterialProvider)
    {
        RuntimeMaterialProvider->Reset();
    }
    RuntimeMaterialProvider = nullptr;
    LastResolvedCells.Reset();
    CurrentRuntimeMapId.Empty();
}

TArray<FString> AAsciiMapBuilderV2Actor::GetAvailableMapIdOptions() const
{
    TArray<FString> Result;
    TSharedPtr<FJsonObject> RootObject;
    FString IndexPath;
    FString Error;
    if (!LoadMapPackageIndex(RootObject, IndexPath, Error))
    {
        return Result;
    }

    const TArray<TSharedPtr<FJsonValue>>* Maps = nullptr;
    if (!RootObject->TryGetArrayField(TEXT("maps"), Maps) || !Maps)
    {
        return Result;
    }

    for (const TSharedPtr<FJsonValue>& MapValue : *Maps)
    {
        const TSharedPtr<FJsonObject> MapObject = MapValue.IsValid() ? MapValue->AsObject() : nullptr;
        const FString MapId = GetOptionalString(MapObject, TEXT("map_id"));
        if (!MapId.IsEmpty())
        {
            Result.Add(MapId);
        }
    }
    return Result;
}

bool AAsciiMapBuilderV2Actor::ResolveSelectedMapPackage(FRuntimeMapPackagePathsV2& OutPaths, FString& OutError) const
{
    TSharedPtr<FJsonObject> RootObject;
    FString AbsoluteIndexPath;
    if (!LoadMapPackageIndex(RootObject, AbsoluteIndexPath, OutError))
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* Maps = nullptr;
    if (!RootObject->TryGetArrayField(TEXT("maps"), Maps) || !Maps || Maps->IsEmpty())
    {
        OutError = TEXT("map_package_index.json contains no maps.");
        return false;
    }

    TSharedPtr<FJsonObject> SelectedEntry;
    if (!SelectedMapId.IsNone())
    {
        for (const TSharedPtr<FJsonValue>& MapValue : *Maps)
        {
            const TSharedPtr<FJsonObject> Candidate = MapValue.IsValid() ? MapValue->AsObject() : nullptr;
            if (GetOptionalString(Candidate, TEXT("map_id")).Equals(SelectedMapId.ToString(), ESearchCase::IgnoreCase))
            {
                SelectedEntry = Candidate;
                break;
            }
        }
    }

    if (!SelectedEntry.IsValid())
    {
        SelectedEntry = (*Maps)[0].IsValid() ? (*Maps)[0]->AsObject() : nullptr;
        if (!SelectedMapId.IsNone())
        {
            UE_LOG(LogTemp, Warning, TEXT("MapBuilderV2: Map '%s' was not found; using the first package entry."), *SelectedMapId.ToString());
        }
    }

    if (!SelectedEntry.IsValid())
    {
        OutError = TEXT("Selected map package entry is invalid.");
        return false;
    }

    OutPaths.MapId = GetOptionalString(SelectedEntry, TEXT("map_id"));
    OutPaths.PackageDirectory = ResolveIndexRelativePath(AbsoluteIndexPath, GetOptionalString(SelectedEntry, TEXT("package_dir")));
    OutPaths.LayoutPath = ResolveIndexRelativePath(AbsoluteIndexPath, GetOptionalString(SelectedEntry, TEXT("layout")));
    OutPaths.ResolvedTileSetPath = ResolveIndexRelativePath(AbsoluteIndexPath, GetOptionalString(SelectedEntry, TEXT("resolved_tileset")));
    OutPaths.MaterialManifestPath = ResolveIndexRelativePath(AbsoluteIndexPath, GetOptionalString(SelectedEntry, TEXT("material_manifest")));

    if (OutPaths.LayoutPath.IsEmpty() || OutPaths.ResolvedTileSetPath.IsEmpty())
    {
        OutError = TEXT("Selected map package is missing layout or resolved_tileset path.");
        return false;
    }
    return true;
}

bool AAsciiMapBuilderV2Actor::LoadMapPackageIndex(
    TSharedPtr<FJsonObject>& OutRoot,
    FString& OutAbsoluteIndexPath,
    FString& OutError) const
{
    OutAbsoluteIndexPath = ResolveIndexPath();
    FString FileContent;
    if (!FFileHelper::LoadFileToString(FileContent, *OutAbsoluteIndexPath))
    {
        OutError = FString::Printf(TEXT("Failed to read map package index: %s"), *OutAbsoluteIndexPath);
        return false;
    }

    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContent);
    if (!FJsonSerializer::Deserialize(Reader, OutRoot) || !OutRoot.IsValid())
    {
        OutError = FString::Printf(TEXT("Failed to parse map package index: %s"), *OutAbsoluteIndexPath);
        return false;
    }

    FString SchemaVersion;
    OutRoot->TryGetStringField(TEXT("schema_version"), SchemaVersion);
    if (SchemaVersion != TEXT("map_package_index_v1"))
    {
        OutError = FString::Printf(TEXT("Unsupported map package index schema '%s'."), *SchemaVersion);
        return false;
    }
    return true;
}

bool AAsciiMapBuilderV2Actor::LoadRuntimeTileDefinitions(
    const FString& AbsolutePath,
    TMap<FString, FAsciiRuntimeTileDefinitionV2>& OutDefinitions,
    FString& OutError) const
{
    OutDefinitions.Reset();
    FString FileContent;
    if (!FFileHelper::LoadFileToString(FileContent, *AbsolutePath))
    {
        OutError = FString::Printf(TEXT("Failed to read resolved tileset: %s"), *AbsolutePath);
        return false;
    }

    TSharedPtr<FJsonObject> RootObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContent);
    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        OutError = FString::Printf(TEXT("Failed to parse resolved tileset: %s"), *AbsolutePath);
        return false;
    }

    FString SchemaVersion;
    RootObject->TryGetStringField(TEXT("schema_version"), SchemaVersion);
    if (SchemaVersion != TEXT("resolved_tileset_v1"))
    {
        OutError = FString::Printf(TEXT("Unsupported resolved tileset schema '%s'."), *SchemaVersion);
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* Tiles = nullptr;
    if (!RootObject->TryGetArrayField(TEXT("tiles"), Tiles) || !Tiles)
    {
        OutError = TEXT("resolved_tileset.json has no tiles array.");
        return false;
    }

    for (const TSharedPtr<FJsonValue>& TileValue : *Tiles)
    {
        const TSharedPtr<FJsonObject> TileObject = TileValue.IsValid() ? TileValue->AsObject() : nullptr;
        if (!TileObject.IsValid())
        {
            continue;
        }

        FAsciiRuntimeTileDefinitionV2 Definition;
        Definition.Symbol = GetOptionalString(TileObject, TEXT("symbol"));
        Definition.Role = GetOptionalString(TileObject, TEXT("role"));
        TryGetOptionalName(TileObject, TEXT("tile_type_id"), Definition.TileTypeId);
        TryGetOptionalName(TileObject, TEXT("mesh_id"), Definition.MeshId);
        if (!TryGetOptionalName(TileObject, TEXT("material_slot_id"), Definition.MaterialSlotId))
        {
            TryGetOptionalName(TileObject, TEXT("slot_id_compat"), Definition.MaterialSlotId);
        }
        Definition.ShapeType = GetOptionalString(TileObject, TEXT("shape_type"));
        Definition.HeightClass = GetOptionalString(TileObject, TEXT("height_class"));
        Definition.MaterialFamily = GetOptionalString(TileObject, TEXT("material_family"));
        Definition.TileCountInMap = GetOptionalInt(TileObject, TEXT("tile_count_in_map"), 0);
        Definition.Height = GetOptionalFloat(TileObject, TEXT("height"), 0.0f);
        Definition.ZOffset = GetOptionalFloat(TileObject, TEXT("z_offset"), 0.0f);
        Definition.bGenerate = GetOptionalBool(TileObject, TEXT("generate"), false);

        const TArray<TSharedPtr<FJsonValue>>* RoleTags = nullptr;
        if (TileObject->TryGetArrayField(TEXT("selected_mesh_role_tags"), RoleTags) && RoleTags)
        {
            for (const TSharedPtr<FJsonValue>& RoleTagValue : *RoleTags)
            {
                FString RoleTag;
                if (RoleTagValue.IsValid() && RoleTagValue->TryGetString(RoleTag))
                {
                    Definition.SelectedMeshRoleTags.Add(RoleTag);
                }
            }
        }

        if (!Definition.Symbol.IsEmpty())
        {
            OutDefinitions.Add(Definition.Symbol.Left(1), MoveTemp(Definition));
        }
    }

    if (OutDefinitions.IsEmpty())
    {
        OutError = TEXT("resolved_tileset.json produced no symbol definitions.");
        return false;
    }
    return true;
}

bool AAsciiMapBuilderV2Actor::LoadMapLines(
    const FString& AbsolutePath,
    TArray<FString>& OutLines,
    FString& OutError) const
{
    FString FileContent;
    if (!FFileHelper::LoadFileToString(FileContent, *AbsolutePath))
    {
        OutError = FString::Printf(TEXT("Failed to read map layout: %s"), *AbsolutePath);
        return false;
    }

    FileContent.ParseIntoArrayLines(OutLines, false);
    while (!OutLines.IsEmpty() && OutLines.Last().IsEmpty())
    {
        OutLines.Pop();
    }
    if (OutLines.IsEmpty())
    {
        OutError = TEXT("Map layout is empty.");
        return false;
    }
    return true;
}

bool AAsciiMapBuilderV2Actor::BuildSemanticGrid(
    const TArray<FString>& Lines,
    const TMap<FString, FAsciiRuntimeTileDefinitionV2>& Definitions,
    FAsciiMapGridV2& OutGrid,
    FString& OutError)
{
    int32 Width = 0;
    for (const FString& Line : Lines)
    {
        Width = FMath::Max(Width, Line.Len());
    }
    if (Width <= 0)
    {
        OutError = TEXT("Map layout has zero width.");
        return false;
    }

    OutGrid.Initialize(Width, Lines.Num());
    const float TileSize = BuilderConfig->TileSizeCm;
    const float OriginX = BuilderConfig->bCenterMapOnActor ? -0.5f * (Width - 1) * TileSize : 0.0f;
    const float OriginY = BuilderConfig->bCenterMapOnActor ? 0.5f * (Lines.Num() - 1) * TileSize : 0.0f;

    TSet<FString> UnknownSymbols;
    TMap<FString, int32> ActualSymbolCounts;
    for (int32 Row = 0; Row < Lines.Num(); ++Row)
    {
        const FString& Line = Lines[Row];
        for (int32 Column = 0; Column < Width; ++Column)
        {
            FResolvedMapCellV2* Cell = OutGrid.GetCell(Row, Column);
            if (!Cell)
            {
                continue;
            }

            const FString Symbol = Column < Line.Len() ? Line.Mid(Column, 1) : TEXT(" ");
            ActualSymbolCounts.FindOrAdd(Symbol)++;
            if (const FAsciiRuntimeTileDefinitionV2* Definition = Definitions.Find(Symbol))
            {
                Cell->TileDefinition = *Definition;
            }
            else if (!Symbol.TrimStartAndEnd().IsEmpty())
            {
                UnknownSymbols.Add(Symbol);
            }

            Cell->ResolvedLocation = FVector(
                OriginX + Column * TileSize,
                OriginY - Row * TileSize,
                Cell->TileDefinition.ZOffset);
        }
    }

    for (const FString& UnknownSymbol : UnknownSymbols)
    {
        UE_LOG(LogTemp, Warning, TEXT("MapBuilderV2: Layout symbol '%s' has no RuntimeData tile definition."), *UnknownSymbol);
    }

    for (const TPair<FString, FAsciiRuntimeTileDefinitionV2>& Pair : Definitions)
    {
        const int32 ActualCount = ActualSymbolCounts.FindRef(Pair.Key);
        if (Pair.Value.TileCountInMap > 0 && Pair.Value.TileCountInMap != ActualCount)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("MapBuilderV2: symbol '%s' tile_count_in_map=%d but layout count=%d."),
                *Pair.Key,
                Pair.Value.TileCountInMap,
                ActualCount);
        }
    }
    return true;
}

void AAsciiMapBuilderV2Actor::ValidateRuntimeDefinitions(
    const TMap<FString, FAsciiRuntimeTileDefinitionV2>& Definitions)
{
    TSet<FName> ValidatedMeshIds;
    for (const TPair<FString, FAsciiRuntimeTileDefinitionV2>& Pair : Definitions)
    {
        const FAsciiRuntimeTileDefinitionV2& Definition = Pair.Value;
        if (!Definition.bGenerate || Definition.MeshId.IsNone() || ValidatedMeshIds.Contains(Definition.MeshId))
        {
            continue;
        }
        ValidatedMeshIds.Add(Definition.MeshId);

        const FGeneratedMeshEntryV2* Entry = MeshRegistryV2->FindMeshById(Definition.MeshId);
        if (!Entry)
        {
            MissingMeshIds.Add(Definition.MeshId);
            continue;
        }
        if (!Entry->PrimaryMesh && !(Entry->bSpawnActor && Entry->ActorClass != nullptr))
        {
            MissingMeshIds.Add(Definition.MeshId);
            UE_LOG(LogTemp, Warning, TEXT("MapBuilderV2: Registry entry '%s' has neither PrimaryMesh nor a valid ActorClass."), *Definition.MeshId.ToString());
            continue;
        }

        if (Entry->bUseConnectedWallAssembly
            && (Entry->PrimaryLayer != EGeneratedMeshLayerV2::Structure
                || Entry->OrientationPolicy != EOrientationPolicyV2::ConnectFourDirections
                || Entry->CoveragePolicy != EBaseCoveragePolicyV2::RequiresUnderlay
                || !Entry->PrimaryMesh))
        {
            UE_LOG(LogTemp, Warning,
                TEXT("MapBuilderV2: Registry entry '%s' enables connected wall assembly but its layer/orientation/coverage/mesh configuration is incompatible."),
                *Definition.MeshId.ToString());
        }

        if (Entry->PrimaryMesh)
        {
            const FVector BoundsSize = Entry->PrimaryMesh->GetBounds().BoxExtent * 2.0f;
            if (BoundsSize.X <= KINDA_SMALL_NUMBER || BoundsSize.Y <= KINDA_SMALL_NUMBER)
            {
                UE_LOG(LogTemp, Warning, TEXT("MapBuilderV2: Mesh '%s' has degenerate XY bounds %s."), *Definition.MeshId.ToString(), *BoundsSize.ToString());
            }
            if (Entry->bOverrideMaterialSlotZero && Entry->PrimaryMesh->GetStaticMaterials().Num() > 1)
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("MapBuilderV2: Mesh '%s' has %d material slots; V2 currently overrides slot 0 only."),
                    *Definition.MeshId.ToString(),
                    Entry->PrimaryMesh->GetStaticMaterials().Num());
            }
            if (BuilderConfig->bEnableDebugVisualization)
            {
                UE_LOG(LogTemp, Display,
                    TEXT("MapBuilderV2 registry: mesh=%s bounds=%s layer=%d coverage=%d orientation=%d fit=%d"),
                    *Definition.MeshId.ToString(),
                    *BoundsSize.ToString(),
                    static_cast<int32>(Entry->PrimaryLayer),
                    static_cast<int32>(Entry->CoveragePolicy),
                    static_cast<int32>(Entry->OrientationPolicy),
                    static_cast<int32>(Entry->FitMode));
            }
        }
    }

    if (!BuilderConfig->FallbackFloorMeshId.IsNone())
    {
        const FGeneratedMeshEntryV2* FallbackEntry = MeshRegistryV2->FindMeshById(BuilderConfig->FallbackFloorMeshId);
        if (!FallbackEntry)
        {
            MissingMeshIds.Add(BuilderConfig->FallbackFloorMeshId);
            UE_LOG(LogTemp, Warning,
                TEXT("MapBuilderV2: FallbackFloorMeshId '%s' is absent from MeshRegistryV2."),
                *BuilderConfig->FallbackFloorMeshId.ToString());
        }
        else if (FallbackEntry->PrimaryLayer != EGeneratedMeshLayerV2::BaseSurface)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("MapBuilderV2: FallbackFloorMeshId '%s' should use the BaseSurface layer."),
                *BuilderConfig->FallbackFloorMeshId.ToString());
        }
    }
}

FString AAsciiMapBuilderV2Actor::ResolveIndexPath() const
{
    FString Result = bMapPackageIndexPathIsAbsolute
        ? MapPackageIndexPath
        : FPaths::Combine(FPaths::ProjectContentDir(), MapPackageIndexPath);
    FPaths::NormalizeFilename(Result);
    return Result;
}

FString AAsciiMapBuilderV2Actor::ResolveIndexRelativePath(
    const FString& AbsoluteIndexPath,
    const FString& RuntimeRelativePath) const
{
    if (RuntimeRelativePath.IsEmpty())
    {
        return FString();
    }

    FString Result = FPaths::IsRelative(RuntimeRelativePath)
        ? FPaths::Combine(FPaths::GetPath(AbsoluteIndexPath), RuntimeRelativePath)
        : RuntimeRelativePath;
    FPaths::NormalizeFilename(Result);
    return Result;
}

UMaterialInterface* AAsciiMapBuilderV2Actor::ResolveMaterial(const FName MaterialSlotId) const
{
    if (RuntimeMaterialProvider)
    {
        if (UMaterialInterface* RuntimeMaterial = RuntimeMaterialProvider->ResolveMaterial(MaterialSlotId))
        {
            return RuntimeMaterial;
        }
    }

    return MaterialRegistryFallback ? MaterialRegistryFallback->FindMaterialBySlotId(MaterialSlotId) : nullptr;
}

UInstancedStaticMeshComponent* AAsciiMapBuilderV2Actor::GetOrCreateInstanceComponent(
    const FName ComponentKey,
    UStaticMesh* Mesh,
    UMaterialInterface* Material)
{
    if (UInstancedStaticMeshComponent** Existing = RuntimeInstanceComponents.Find(ComponentKey))
    {
        return *Existing;
    }
    if (!Mesh)
    {
        return nullptr;
    }

    const FName UniqueComponentName = MakeUniqueObjectName(this, UInstancedStaticMeshComponent::StaticClass(), ComponentKey);
    UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(this, UniqueComponentName);
    Component->SetupAttachment(SceneRoot);
    Component->SetStaticMesh(Mesh);
    Component->SetMobility(EComponentMobility::Static);
    if (Material)
    {
        Component->SetMaterial(0, Material);
    }
    Component->RegisterComponent();
    AddInstanceComponent(Component);
    RuntimeInstanceComponents.Add(ComponentKey, Component);
    return Component;
}

void AAsciiMapBuilderV2Actor::DrawDebugCells(const FAsciiMapGridV2& Grid) const
{
    if (!BuilderConfig || !BuilderConfig->bEnableDebugVisualization || !GetWorld())
    {
        return;
    }

    for (const FResolvedMapCellV2& Cell : Grid.GetCells())
    {
        if (!Cell.bGeneratePrimaryMesh)
        {
            continue;
        }

        const FString DebugText = FString::Printf(
            TEXT("%d,%d %s\nL%d C%d O%d M%u R=%.0f S=(%.2f,%.2f,%.2f)\nshape=%s height=%s family=%s count=%d\nU:%s (%s)"),
            Cell.Column,
            Cell.Row,
            *Cell.TileDefinition.Role,
            static_cast<int32>(Cell.PrimaryLayer),
            static_cast<int32>(Cell.CoveragePolicy),
            static_cast<int32>(Cell.OrientationPolicy),
            Cell.ConnectivityMask,
            Cell.ResolvedRotation.Yaw,
            Cell.ResolvedScale.X,
            Cell.ResolvedScale.Y,
            Cell.ResolvedScale.Z,
            *Cell.TileDefinition.ShapeType,
            *Cell.TileDefinition.HeightClass,
            *Cell.TileDefinition.MaterialFamily,
            Cell.TileDefinition.TileCountInMap,
            *Cell.ResolvedUnderlayMaterialSlotId.ToString(),
            *Cell.UnderlayResolutionSource);
        const FVector WorldLocation = GetActorTransform().TransformPosition(Cell.ResolvedLocation + FVector(0.0f, 0.0f, 50.0f));
        DrawDebugString(GetWorld(), WorldLocation, DebugText, nullptr, FColor::Cyan, BuilderConfig->DebugDisplayDuration, false, 0.75f);
    }
}

void AAsciiMapBuilderV2Actor::ResetGenerationStats()
{
    GeneratedBaseSurfaceCount = 0;
    GeneratedUnderlayCount = 0;
    GeneratedStructureCount = 0;
    GeneratedInteractiveActorCount = 0;
    GeneratedDecorationCount = 0;
    GeneratedOverlayCount = 0;
    NonUniformStretchCount = 0;
    AmbiguousOrientationCount = 0;
    MissingMeshIds.Reset();
    MissingMaterialSlotIds.Reset();
    WallConnectivityDistribution.Reset();
}

void AAsciiMapBuilderV2Actor::LogGenerationSummary(const FAsciiMapGridV2& Grid, const FName DominantFloorMaterial) const
{
    TArray<FString> MissingMeshes;
    for (const FName MeshId : MissingMeshIds)
    {
        MissingMeshes.Add(MeshId.ToString());
    }
    TArray<FString> MissingMaterials;
    for (const FName MaterialId : MissingMaterialSlotIds)
    {
        MissingMaterials.Add(MaterialId.ToString());
    }

    UE_LOG(LogTemp, Display,
        TEXT("MapBuilderV2 complete: map=%s grid=%dx%d base=%d underlay=%d structure=%d interactiveActors=%d decoration=%d overlay=%d dominantFloor=%s ambiguous=%d nonUniformStretch=%d missingMeshes=[%s] missingMaterials=[%s]"),
        *CurrentRuntimeMapId,
        Grid.GetWidth(),
        Grid.GetHeight(),
        GeneratedBaseSurfaceCount,
        GeneratedUnderlayCount,
        GeneratedStructureCount,
        GeneratedInteractiveActorCount,
        GeneratedDecorationCount,
        GeneratedOverlayCount,
        *DominantFloorMaterial.ToString(),
        AmbiguousOrientationCount,
        NonUniformStretchCount,
        *FString::Join(MissingMeshes, TEXT(",")),
        *FString::Join(MissingMaterials, TEXT(",")));

    for (const TPair<uint8, int32>& Pair : WallConnectivityDistribution)
    {
        UE_LOG(LogTemp, Display, TEXT("MapBuilderV2 wall connectivity: mask=%u count=%d"), Pair.Key, Pair.Value);
    }
}
