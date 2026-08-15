// Fill out your copyright notice in the Description page of Project Settings.


#include "AsciiMapBuilderActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "AsciiTileSetDataAsset.h"
#include "GeneratedMeshRegistryDataAsset.h"
#include "MaterialRegistryDataAsset.h"

namespace
{
    FName MakeNameFromOptionalString(const FString& Value)
    {
        return Value.IsEmpty() ? NAME_None : FName(*Value);
    }

    bool TryGetOptionalJsonStringField(const TSharedPtr<FJsonObject>& JsonObject, const TCHAR* FieldName, FString& OutValue)
    {
        OutValue.Empty();
        return JsonObject.IsValid() && JsonObject->TryGetStringField(FieldName, OutValue);
    }

    bool CollectValidMapEntries(const TSharedPtr<FJsonObject>& RootObject, TArray<TSharedPtr<FJsonObject>>& OutMapEntries, TArray<FString>* OutMapIds = nullptr)
    {
        OutMapEntries.Empty();
        if (OutMapIds)
        {
            OutMapIds->Empty();
        }

        if (!RootObject.IsValid())
        {
            return false;
        }

        const TArray<TSharedPtr<FJsonValue>>* MapValues = nullptr;
        if (!RootObject->TryGetArrayField(TEXT("maps"), MapValues) || !MapValues)
        {
            return false;
        }

        for (const TSharedPtr<FJsonValue>& MapValue : *MapValues)
        {
            const TSharedPtr<FJsonObject> MapObject = MapValue.IsValid() ? MapValue->AsObject() : nullptr;
            if (!MapObject.IsValid())
            {
                continue;
            }

            FString MapIdString;
            if (!MapObject->TryGetStringField(TEXT("map_id"), MapIdString) || MapIdString.IsEmpty())
            {
                continue;
            }

            OutMapEntries.Add(MapObject);
            if (OutMapIds)
            {
                OutMapIds->Add(MapIdString);
            }
        }

        return OutMapEntries.Num() > 0;
    }

    FString FormatNameSetForLog(const TSet<FName>& Names)
    {
        if (Names.Num() == 0)
        {
            return TEXT("<none>");
        }

        TArray<FString> Values;
        Values.Reserve(Names.Num());
        for (const FName& Name : Names)
        {
            Values.Add(Name.IsNone() ? TEXT("<empty>") : Name.ToString());
        }

        Values.Sort();
        return FString::Join(Values, TEXT(", "));
    }
}

// Sets default values
AAsciiMapBuilderActor::AAsciiMapBuilderActor()
{
    PrimaryActorTick.bCanEverTick = false;

    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

    FloorInstances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("FloorInstances"));
    FloorInstances->SetupAttachment(RootComponent);

    GrassInstances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("GrassInstances"));
    GrassInstances->SetupAttachment(RootComponent);

    WoodInstances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("WoodInstances"));
    WoodInstances->SetupAttachment(RootComponent);

    WaterInstances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("WaterInstances"));
    WaterInstances->SetupAttachment(RootComponent);

    WallInstances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("WallInstances"));
    WallInstances->SetupAttachment(RootComponent);

    DoorInstances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("DoorInstances"));
    DoorInstances->SetupAttachment(RootComponent);

    RebuildTileDefinitionsCache();
}

TArray<FString> AAsciiMapBuilderActor::GetAvailableMapIdOptions() const
{
    TArray<FString> MapIds;

    TSharedPtr<FJsonObject> RootObject;
    FString FullIndexPath;
    if (!LoadMapPackageIndexJson(RootObject, FullIndexPath))
    {
        return MapIds;
    }

    TArray<TSharedPtr<FJsonObject>> MapEntries;
    CollectValidMapEntries(RootObject, MapEntries, &MapIds);
    return MapIds;
}

void AAsciiMapBuilderActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    RebuildTileDefinitionsCache();
    RebuildRuntimeMaterialCache();

    if (TilePlaneMesh)
    {
        FloorInstances->SetStaticMesh(TilePlaneMesh);
        GrassInstances->SetStaticMesh(TilePlaneMesh);
        WoodInstances->SetStaticMesh(TilePlaneMesh);
        WaterInstances->SetStaticMesh(TilePlaneMesh);
    }

    if (CubeMesh)
    {
        WallInstances->SetStaticMesh(CubeMesh);
        DoorInstances->SetStaticMesh(CubeMesh);
    }

    if (StoneFloorMaterial) FloorInstances->SetMaterial(0, StoneFloorMaterial);
    if (GrassMaterial) GrassInstances->SetMaterial(0, GrassMaterial);
    if (WoodMaterial) WoodInstances->SetMaterial(0, WoodMaterial);
    if (WaterMaterial) WaterInstances->SetMaterial(0, WaterMaterial);
    if (WallMaterial) WallInstances->SetMaterial(0, WallMaterial);
    if (DoorMaterial) DoorInstances->SetMaterial(0, DoorMaterial);

    ApplyMaterialsFromRegistry();
}

void AAsciiMapBuilderActor::RebuildTileDefinitionsCache()
{
    TileDefinitions.Empty();

    if (bUseResolvedTileSetJson)
    {
        if (LoadTileDefinitionsFromResolvedJson())
        {
            UE_LOG(LogTemp, Display, TEXT("D2A: TileDefinitions source = resolved_tileset.json"));
            return;
        }

        UE_LOG(LogTemp, Warning, TEXT("D2A: JSON load failed; falling back to TileSet DataAsset"));
        TileDefinitions.Empty();
    }

    if (LoadTileDefinitionsFromTileSetDataAsset())
    {
        UE_LOG(LogTemp, Display, TEXT("D2A: TileDefinitions source = TileSet DataAsset"));
        return;
    }

    TileDefinitions.Empty();
    LoadBuiltInFallbackTileDefinitions();
    UE_LOG(LogTemp, Warning, TEXT("D2A: TileDefinitions source = built-in fallback definitions"));
}

bool AAsciiMapBuilderActor::LoadTileDefinitionsFromResolvedJson()
{
    if (ResolvedTileSetJsonPath.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("D2A: ResolvedTileSetJsonPath is empty."));
        return false;
    }

    FString FullPath = bResolvedTileSetJsonPathIsAbsolute
        ? ResolvedTileSetJsonPath
        : FPaths::ProjectContentDir() / ResolvedTileSetJsonPath;

    FPaths::NormalizeFilename(FullPath);
    UE_LOG(LogTemp, Display, TEXT("D2A: Attempting to load resolved tileset JSON: %s"), *FullPath);

    FString FileContent;
    if (!FFileHelper::LoadFileToString(FileContent, *FullPath))
    {
        UE_LOG(LogTemp, Warning, TEXT("D2A: Failed to read resolved tileset JSON: %s"), *FullPath);
        return false;
    }

    TSharedPtr<FJsonObject> RootObject;
    const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(FileContent);
    if (!FJsonSerializer::Deserialize(JsonReader, RootObject) || !RootObject.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("D2A: Failed to parse resolved tileset JSON: %s"), *FullPath);
        return false;
    }

    FString SchemaVersion;
    if (!RootObject->TryGetStringField(TEXT("schema_version"), SchemaVersion) || SchemaVersion != TEXT("resolved_tileset_v1"))
    {
        UE_LOG(LogTemp, Warning, TEXT("D2A: Unsupported or missing schema_version '%s'. Expected 'resolved_tileset_v1'."),
            *SchemaVersion);
        return false;
    }

    FString MapId;
    RootObject->TryGetStringField(TEXT("map_id"), MapId);
    UE_LOG(LogTemp, Display, TEXT("D2A: Loaded resolved_tileset_v1 for map_id=%s"),
        MapId.IsEmpty() ? TEXT("<unknown>") : *MapId);

    const TArray<TSharedPtr<FJsonValue>>* TileValues = nullptr;
    if (!RootObject->TryGetArrayField(TEXT("tiles"), TileValues) || !TileValues)
    {
        UE_LOG(LogTemp, Warning, TEXT("D2A: resolved_tileset.json does not contain a valid tiles array."));
        return false;
    }

    TMap<FString, FAsciiTileDefinition> LoadedDefinitions;

    for (int32 TileIndex = 0; TileIndex < TileValues->Num(); ++TileIndex)
    {
        const TSharedPtr<FJsonValue>& TileValue = (*TileValues)[TileIndex];
        const TSharedPtr<FJsonObject> TileObject = TileValue.IsValid() ? TileValue->AsObject() : nullptr;
        if (!TileObject.IsValid())
        {
            UE_LOG(LogTemp, Warning, TEXT("D2A: Skipping tile entry %d because it is not a JSON object."), TileIndex);
            continue;
        }

        FString Symbol;
        if (!TileObject->TryGetStringField(TEXT("symbol"), Symbol) || Symbol.IsEmpty())
        {
            UE_LOG(LogTemp, Warning, TEXT("D2A: Skipping tile entry %d because symbol is missing or empty."), TileIndex);
            continue;
        }

        FString TileTypeIdString;
        FString RoleString;
        FString MeshIdString;
        FString MaterialSlotIdString;
        FString SlotIdCompatString;

        TryGetOptionalJsonStringField(TileObject, TEXT("tile_type_id"), TileTypeIdString);
        TryGetOptionalJsonStringField(TileObject, TEXT("role"), RoleString);
        TryGetOptionalJsonStringField(TileObject, TEXT("mesh_id"), MeshIdString);
        TryGetOptionalJsonStringField(TileObject, TEXT("material_slot_id"), MaterialSlotIdString);
        TryGetOptionalJsonStringField(TileObject, TEXT("slot_id_compat"), SlotIdCompatString);

        bool bGenerate = true;
        TileObject->TryGetBoolField(TEXT("generate"), bGenerate);

        double HeightValue = 0.0;
        double ZOffsetValue = 0.0;
        TileObject->TryGetNumberField(TEXT("height"), HeightValue);
        TileObject->TryGetNumberField(TEXT("z_offset"), ZOffsetValue);

        if (LoadedDefinitions.Contains(Symbol))
        {
            UE_LOG(LogTemp, Warning, TEXT("D2A: Duplicate symbol '%s' in JSON; later definition overwrites earlier."), *Symbol);
        }

        if (bGenerate && MeshIdString.IsEmpty())
        {
            UE_LOG(LogTemp, Warning, TEXT("D2A: generate=true but mesh_id missing for symbol '%s'."), *Symbol);
        }

        if (bGenerate && MaterialSlotIdString.IsEmpty())
        {
            UE_LOG(LogTemp, Warning, TEXT("D2A: generate=true but material_slot_id missing for symbol '%s'."), *Symbol);
        }

        FAsciiTileDefinition Def;
        Def.Symbol = Symbol;
        Def.TileTypeId = MakeNameFromOptionalString(TileTypeIdString);
        Def.Role = ConvertResolvedRoleStringToTileRole(RoleString);
        Def.MeshId = MakeNameFromOptionalString(MeshIdString);
        Def.MaterialSlotId = MakeNameFromOptionalString(MaterialSlotIdString);
        Def.SlotId = !MaterialSlotIdString.IsEmpty()
            ? FName(*MaterialSlotIdString)
            : MakeNameFromOptionalString(SlotIdCompatString);
        Def.bGenerate = bGenerate;
        Def.Height = static_cast<float>(HeightValue);
        Def.ZOffset = static_cast<float>(ZOffsetValue);

        LoadedDefinitions.Add(Symbol, Def);
    }

    if (LoadedDefinitions.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("D2A: resolved_tileset.json loaded but no valid tile definitions were found."));
        return false;
    }

    TileDefinitions = MoveTemp(LoadedDefinitions);
    UE_LOG(LogTemp, Display, TEXT("D2A: Loaded %d tile definitions from resolved_tileset.json"), TileDefinitions.Num());
    return true;
}

bool AAsciiMapBuilderActor::LoadTileDefinitionsFromTileSetDataAsset()
{
    if (!TileSet)
    {
        return false;
    }

    for (const FAsciiTileDefinition& Definition : TileSet->TileDefinitions)
    {
        if (Definition.Symbol.IsEmpty())
        {
            UE_LOG(LogTemp, Warning, TEXT("TileSet contains an empty symbol definition."));
            continue;
        }

        if (TileDefinitions.Contains(Definition.Symbol))
        {
            UE_LOG(LogTemp, Warning, TEXT("TileSet contains duplicate symbol '%s'; later definition overwrites earlier."),
                *Definition.Symbol);
        }

        TileDefinitions.Add(Definition.Symbol, Definition);
    }

    UE_LOG(LogTemp, Display, TEXT("Loaded %d tile definitions from TileSet."), TileDefinitions.Num());
    return TileDefinitions.Num() > 0;
}

void AAsciiMapBuilderActor::LoadBuiltInFallbackTileDefinitions()
{
    UE_LOG(LogTemp, Warning, TEXT("No resolved tileset JSON or TileSet assigned. Using built-in fallback tile definitions."));

    TileDefinitions.Add(TEXT("0"), { TEXT("0"), TEXT("void"), TEXT("void"), NAME_None, NAME_None, EAsciiTileRole::Void, false, 0.0f, 0.0f });
    TileDefinitions.Add(TEXT("."), { TEXT("."), TEXT("stone_floor"), TEXT("floor"), TEXT("tile_plane"), TEXT("stone_floor"), EAsciiTileRole::Floor, true, 0.0f, 0.0f });
    TileDefinitions.Add(TEXT("g"), { TEXT("g"), TEXT("grass_ground"), TEXT("grass"), TEXT("tile_plane"), TEXT("grass_ground"), EAsciiTileRole::Grass, true, 0.0f, 0.0f });
    TileDefinitions.Add(TEXT("="), { TEXT("="), TEXT("wood_planks"), TEXT("wood"), TEXT("tile_plane"), TEXT("wood_planks"), EAsciiTileRole::Wood, true, 5.0f, 0.0f });
    TileDefinitions.Add(TEXT("~"), { TEXT("~"), TEXT("water"), TEXT("water"), TEXT("water_plane"), TEXT("water"), EAsciiTileRole::Water, true, -20.0f, 0.0f });
    TileDefinitions.Add(TEXT("#"), { TEXT("#"), TEXT("stone_wall"), TEXT("wall"), TEXT("wall_block"), TEXT("stone_wall"), EAsciiTileRole::Wall, true, 0.0f, WallHeight });
    TileDefinitions.Add(TEXT("D"), { TEXT("D"), TEXT("wooden_door"), TEXT("door"), TEXT("door_proxy"), TEXT("wooden_door"), EAsciiTileRole::Door, true, 0.0f, DoorHeight });

    //TileDefinitions.Add(TEXT("T"), { TEXT("T"), TEXT("tree_foliage_proxy"), EAsciiTileRole::Foliage, true, 0.0f, 200.0f });
    //TileDefinitions.Add(TEXT("c"), { TEXT("c"), TEXT("clover_patch"), EAsciiTileRole::Grass, true, 2.0f, 0.0f });
    //TileDefinitions.Add(TEXT("^"), { TEXT("^"), TEXT("tall_grass"), EAsciiTileRole::Grass, true, 2.0f, 0.0f });

    TileDefinitions.Add(TEXT("T"), { TEXT("T"), TEXT("tree_foliage_proxy"), TEXT("foliage"), TEXT("tile_plane"), TEXT("grass_ground"), EAsciiTileRole::Grass, true, 2.0f, 0.0f });
    TileDefinitions.Add(TEXT("c"), { TEXT("c"), TEXT("clover_patch"), TEXT("foliage"), TEXT("tile_plane"), TEXT("grass_ground"), EAsciiTileRole::Grass, true, 2.0f, 0.0f });
    TileDefinitions.Add(TEXT("^"), { TEXT("^"), TEXT("tall_grass"), TEXT("foliage"), TEXT("tile_plane"), TEXT("grass_ground"), EAsciiTileRole::Grass, true, 2.0f, 0.0f });
}

void AAsciiMapBuilderActor::CreateDefaultTileDefinitions()
{
    RebuildTileDefinitionsCache();
}

EAsciiTileRole AAsciiMapBuilderActor::ConvertResolvedRoleStringToTileRole(const FString& RoleString) const
{
    if (RoleString.Equals(TEXT("void"), ESearchCase::IgnoreCase))
    {
        return EAsciiTileRole::Void;
    }

    if (RoleString.Equals(TEXT("wall_surface"), ESearchCase::IgnoreCase) || RoleString.Equals(TEXT("wall"), ESearchCase::IgnoreCase))
    {
        return EAsciiTileRole::Wall;
    }

    if (RoleString.Equals(TEXT("floor_surface"), ESearchCase::IgnoreCase) || RoleString.Equals(TEXT("floor"), ESearchCase::IgnoreCase))
    {
        return EAsciiTileRole::Floor;
    }

    if (RoleString.Equals(TEXT("door_prop"), ESearchCase::IgnoreCase) || RoleString.Equals(TEXT("door"), ESearchCase::IgnoreCase))
    {
        return EAsciiTileRole::Door;
    }

    if (RoleString.Equals(TEXT("grass_surface"), ESearchCase::IgnoreCase) || RoleString.Equals(TEXT("grass"), ESearchCase::IgnoreCase))
    {
        return EAsciiTileRole::Grass;
    }

    if (RoleString.Equals(TEXT("water_surface"), ESearchCase::IgnoreCase) || RoleString.Equals(TEXT("water"), ESearchCase::IgnoreCase))
    {
        return EAsciiTileRole::Water;
    }

    if (RoleString.Equals(TEXT("wood_surface"), ESearchCase::IgnoreCase) || RoleString.Equals(TEXT("wood"), ESearchCase::IgnoreCase))
    {
        return EAsciiTileRole::Wood;
    }

    if (RoleString.Equals(TEXT("foliage"), ESearchCase::IgnoreCase))
    {
        return EAsciiTileRole::Foliage;
    }

    UE_LOG(LogTemp, Warning, TEXT("D2A: Unknown resolved tile role '%s'. Using Void role."), *RoleString);
    return EAsciiTileRole::Void;
}

bool AAsciiMapBuilderActor::ResolveSelectedMapPackageFromIndex()
{
    LastRuntimeDataAvailableMapCount = 0;
    LastRuntimeDataRequestedMapId = SelectedMapId.IsNone() ? TEXT("<empty>") : SelectedMapId.ToString();
    LastRuntimeDataResolvedMapId.Empty();
    bLastRuntimeDataUsedFirstMapFallback = false;
    LastRuntimeDataIndexFullPath.Empty();

    TSharedPtr<FJsonObject> RootObject;
    FString FullIndexPath;
    if (!LoadMapPackageIndexJson(RootObject, FullIndexPath))
    {
        return false;
    }

    LastRuntimeDataIndexFullPath = FullIndexPath;

    TSharedPtr<FJsonObject> MapEntry;
    FString ResolvedMapId;
    bool bUsedFirstMapFallback = false;
    int32 AvailableMapCount = 0;
    if (!TryFindMapEntryInIndex(RootObject, SelectedMapId, MapEntry, ResolvedMapId, bUsedFirstMapFallback, AvailableMapCount))
    {
        LastRuntimeDataAvailableMapCount = AvailableMapCount;
        UE_LOG(LogTemp, Warning, TEXT("D4B: No usable selected map could be resolved from the package index. Falling back to manual paths."));
        return false;
    }

    LastRuntimeDataAvailableMapCount = AvailableMapCount;
    LastRuntimeDataResolvedMapId = ResolvedMapId;
    bLastRuntimeDataUsedFirstMapFallback = bUsedFirstMapFallback;

    if (!ResolvedMapId.IsEmpty())
    {
        SelectedMapId = FName(*ResolvedMapId);
    }

    if (!ApplyMapEntryRuntimePaths(MapEntry, FullIndexPath))
    {
        return false;
    }

    UE_LOG(LogTemp, Display, TEXT("D4B: Runtime map package resolved. RequestedMapId=%s ResolvedMapId=%s AvailableMaps=%d UsedFirstMapFallback=%s"),
        *LastRuntimeDataRequestedMapId,
        *LastRuntimeDataResolvedMapId,
        LastRuntimeDataAvailableMapCount,
        bLastRuntimeDataUsedFirstMapFallback ? TEXT("true") : TEXT("false"));
    return true;
}

bool AAsciiMapBuilderActor::LoadMapPackageIndexJson(TSharedPtr<FJsonObject>& OutRootObject, FString& OutFullIndexPath) const
{
    OutRootObject.Reset();
    OutFullIndexPath.Empty();

    if (MapPackageIndexPath.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("D4B: MapPackageIndexPath is empty."));
        return false;
    }

    OutFullIndexPath = bMapPackageIndexPathIsAbsolute
        ? MapPackageIndexPath
        : FPaths::ProjectContentDir() / MapPackageIndexPath;

    FPaths::NormalizeFilename(OutFullIndexPath);
    UE_LOG(LogTemp, Display, TEXT("D4B: Attempting to load map package index: %s"), *OutFullIndexPath);

    FString FileContent;
    if (!FFileHelper::LoadFileToString(FileContent, *OutFullIndexPath))
    {
        UE_LOG(LogTemp, Warning, TEXT("D4B: Failed to read map package index: %s"), *OutFullIndexPath);
        return false;
    }

    const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(FileContent);
    if (!FJsonSerializer::Deserialize(JsonReader, OutRootObject) || !OutRootObject.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("D4B: Failed to parse map package index: %s"), *OutFullIndexPath);
        return false;
    }

    FString SchemaVersion;
    if (!OutRootObject->TryGetStringField(TEXT("schema_version"), SchemaVersion) || SchemaVersion != TEXT("map_package_index_v1"))
    {
        UE_LOG(LogTemp, Warning, TEXT("D4B: Unsupported or missing schema_version '%s'. Expected 'map_package_index_v1'."),
            *SchemaVersion);
        return false;
    }

    UE_LOG(LogTemp, Display, TEXT("D4B: Loaded map_package_index_v1"));
    return true;
}

bool AAsciiMapBuilderActor::TryFindMapEntryInIndex(
    const TSharedPtr<FJsonObject>& RootObject,
    FName InSelectedMapId,
    TSharedPtr<FJsonObject>& OutMapEntry,
    FString& OutResolvedMapId,
    bool& bOutUsedFirstMapFallback,
    int32& OutAvailableMapCount) const
{
    OutMapEntry.Reset();
    OutResolvedMapId.Empty();
    bOutUsedFirstMapFallback = false;
    OutAvailableMapCount = 0;

    if (!RootObject.IsValid())
    {
        return false;
    }

    UE_LOG(LogTemp, Display, TEXT("D4B: Requested SelectedMapId = %s"),
        InSelectedMapId.IsNone() ? TEXT("<empty>") : *InSelectedMapId.ToString());

    TArray<TSharedPtr<FJsonObject>> MapEntries;
    TArray<FString> MapIds;
    if (!CollectValidMapEntries(RootObject, MapEntries, &MapIds))
    {
        UE_LOG(LogTemp, Warning, TEXT("D4B: map_package_index.json does not contain any valid maps[].map_id entries."));
        return false;
    }

    OutAvailableMapCount = MapEntries.Num();
    UE_LOG(LogTemp, Display, TEXT("D4B: Available RuntimeData map count = %d"), OutAvailableMapCount);

    const FString SelectedMapIdString = InSelectedMapId.IsNone() ? FString() : InSelectedMapId.ToString();
    if (!SelectedMapIdString.IsEmpty())
    {
        for (int32 MapIndex = 0; MapIndex < MapEntries.Num(); ++MapIndex)
        {
            if (MapIds.IsValidIndex(MapIndex) && MapIds[MapIndex] == SelectedMapIdString)
            {
                OutMapEntry = MapEntries[MapIndex];
                OutResolvedMapId = MapIds[MapIndex];
                return true;
            }
        }
    }

    OutMapEntry = MapEntries[0];
    OutResolvedMapId = MapIds[0];
    bOutUsedFirstMapFallback = true;

    if (SelectedMapIdString.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("D4B: SelectedMapId is empty. Auto-selecting first map from index: %s"), *OutResolvedMapId);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("D4B: SelectedMapId '%s' was not found. Auto-selecting first map from index: %s"),
            *SelectedMapIdString,
            *OutResolvedMapId);
    }

    return true;
}

bool AAsciiMapBuilderActor::ApplyMapEntryRuntimePaths(const TSharedPtr<FJsonObject>& MapEntry, const FString& FullIndexPath)
{
    if (!MapEntry.IsValid())
    {
        return false;
    }

    FString MapIdString;
    FString PackageDirRelativePath;
    FString LayoutRelativePath;
    FString ResolvedTileSetRelativePath;
    FString MaterialManifestRelativePath;
    FString ManifestRelativePath;

    MapEntry->TryGetStringField(TEXT("map_id"), MapIdString);
    MapEntry->TryGetStringField(TEXT("package_dir"), PackageDirRelativePath);
    MapEntry->TryGetStringField(TEXT("manifest"), ManifestRelativePath);

    if (!MapEntry->TryGetStringField(TEXT("layout"), LayoutRelativePath) || LayoutRelativePath.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("D4B: Selected map entry is missing layout path."));
        return false;
    }

    if (!MapEntry->TryGetStringField(TEXT("resolved_tileset"), ResolvedTileSetRelativePath) || ResolvedTileSetRelativePath.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("D4B: Selected map entry is missing resolved_tileset path."));
        return false;
    }

    if (!MapEntry->TryGetStringField(TEXT("material_manifest"), MaterialManifestRelativePath) || MaterialManifestRelativePath.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("D4B: Selected map entry is missing material_manifest path."));
        return false;
    }

    CurrentRuntimeMapId = MapIdString.IsEmpty() ? SelectedMapId.ToString() : MapIdString;
    CurrentRuntimePackageDir = PackageDirRelativePath.IsEmpty()
        ? FPaths::GetPath(ResolveRuntimeDataRelativePath(FullIndexPath, LayoutRelativePath))
        : ResolveRuntimeDataRelativePath(FullIndexPath, PackageDirRelativePath);
    CurrentRuntimeMapFilePath = ResolveRuntimeDataRelativePath(FullIndexPath, LayoutRelativePath);
    CurrentRuntimeResolvedTileSetJsonPath = ResolveRuntimeDataRelativePath(FullIndexPath, ResolvedTileSetRelativePath);
    CurrentRuntimeMaterialManifestJsonPath = ResolveRuntimeDataRelativePath(FullIndexPath, MaterialManifestRelativePath);
    bUseRuntimeResolvedMapFilePath = true;

    UE_LOG(LogTemp, Display, TEXT("D4B: Found map package: %s"),
        PackageDirRelativePath.IsEmpty() ? *CurrentRuntimeMapId : *PackageDirRelativePath);
    UE_LOG(LogTemp, Display, TEXT("D4B: Resolved layout map: %s"), *CurrentRuntimeMapFilePath);
    UE_LOG(LogTemp, Display, TEXT("D4B: Resolved tileset JSON: %s"), *CurrentRuntimeResolvedTileSetJsonPath);
    UE_LOG(LogTemp, Display, TEXT("D4B: Resolved material manifest: %s"), *CurrentRuntimeMaterialManifestJsonPath);

    if (!ManifestRelativePath.IsEmpty())
    {
        const FString FullManifestPath = ResolveRuntimeDataRelativePath(FullIndexPath, ManifestRelativePath);
        FString ManifestContent;
        if (FFileHelper::LoadFileToString(ManifestContent, *FullManifestPath))
        {
            TSharedPtr<FJsonObject> ManifestRootObject;
            const TSharedRef<TJsonReader<>> ManifestReader = TJsonReaderFactory<>::Create(ManifestContent);
            if (FJsonSerializer::Deserialize(ManifestReader, ManifestRootObject) && ManifestRootObject.IsValid())
            {
                FString ManifestSchemaVersion;
                if (!ManifestRootObject->TryGetStringField(TEXT("schema_version"), ManifestSchemaVersion) || ManifestSchemaVersion != TEXT("runtime_map_package_v1"))
                {
                    UE_LOG(LogTemp, Warning, TEXT("D4B: Optional runtime map manifest has schema_version '%s', expected 'runtime_map_package_v1'."),
                        *ManifestSchemaVersion);
                }
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("D4B: Optional runtime map manifest could not be read: %s"), *FullManifestPath);
        }
    }

    return true;
}

FString AAsciiMapBuilderActor::ResolveRuntimeDataRelativePath(const FString& IndexFullPath, const FString& RelativePath) const
{
    if (RelativePath.IsEmpty())
    {
        return FString();
    }

    const FString RuntimeDataRoot = FPaths::GetPath(IndexFullPath);
    FString FullPath = FPaths::IsRelative(RelativePath)
        ? FPaths::Combine(RuntimeDataRoot, RelativePath)
        : RelativePath;

    FPaths::NormalizeFilename(FullPath);
    return FullPath;
}

void AAsciiMapBuilderActor::ClearRuntimeMapPackageState()
{
    CurrentRuntimeMapId.Empty();
    CurrentRuntimePackageDir.Empty();
    CurrentRuntimeMapFilePath.Empty();
    CurrentRuntimeResolvedTileSetJsonPath.Empty();
    CurrentRuntimeMaterialManifestJsonPath.Empty();
    bUseRuntimeResolvedMapFilePath = false;
}

void AAsciiMapBuilderActor::CaptureManualJsonPathSettingsForMapPackageOverride()
{
    if (bHasMapPackageJsonPathSnapshot)
    {
        return;
    }

    bManualUseResolvedTileSetJsonSnapshot = bUseResolvedTileSetJson;
    bManualResolvedTileSetJsonPathIsAbsoluteSnapshot = bResolvedTileSetJsonPathIsAbsolute;
    ManualResolvedTileSetJsonPathSnapshot = ResolvedTileSetJsonPath;
    bManualUseMaterialManifestJsonSnapshot = bUseMaterialManifestJson;
    bManualMaterialManifestJsonPathIsAbsoluteSnapshot = bMaterialManifestJsonPathIsAbsolute;
    ManualMaterialManifestJsonPathSnapshot = MaterialManifestJsonPath;
    bHasMapPackageJsonPathSnapshot = true;
}

void AAsciiMapBuilderActor::RestoreManualJsonPathSettingsIfNeeded()
{
    if (!bHasMapPackageJsonPathSnapshot)
    {
        return;
    }

    bUseResolvedTileSetJson = bManualUseResolvedTileSetJsonSnapshot;
    bResolvedTileSetJsonPathIsAbsolute = bManualResolvedTileSetJsonPathIsAbsoluteSnapshot;
    ResolvedTileSetJsonPath = ManualResolvedTileSetJsonPathSnapshot;
    bUseMaterialManifestJson = bManualUseMaterialManifestJsonSnapshot;
    bMaterialManifestJsonPathIsAbsolute = bManualMaterialManifestJsonPathIsAbsoluteSnapshot;
    MaterialManifestJsonPath = ManualMaterialManifestJsonPathSnapshot;

    bHasMapPackageJsonPathSnapshot = false;
}

FAsciiTileDefinition AAsciiMapBuilderActor::GetDefinitionForSymbol(const TCHAR Symbol) const
{
    const FString Key = FString::Chr(Symbol);

    if (const FAsciiTileDefinition* Found = TileDefinitions.Find(Key))
    {
        return *Found;
    }

    return { Key, TEXT("unknown"), TEXT("unknown"), NAME_None, NAME_None, EAsciiTileRole::Void, false, 0.0f, 0.0f };
}

void AAsciiMapBuilderActor::RebuildRuntimeMaterialCache()
{
    RuntimeMaterialCache.Empty();
    RuntimeLoadedTextures.Empty();

    if (!bUseMaterialManifestJson)
    {
        UE_LOG(LogTemp, Display, TEXT("D3B: Material manifest loading disabled."));
        return;
    }

    UE_LOG(LogTemp, Display, TEXT("D3B: Material manifest loading enabled."));

    if (!RuntimeMaterialMaster)
    {
        UE_LOG(LogTemp, Warning, TEXT("D3B: RuntimeMaterialMaster is null; falling back to MaterialRegistry."));
        return;
    }

    if (!LoadRuntimeMaterialsFromManifest())
    {
        UE_LOG(LogTemp, Warning, TEXT("D3B: Failed to load runtime material manifest. Falling back to MaterialRegistry."));
        RuntimeMaterialCache.Empty();
        RuntimeLoadedTextures.Empty();
    }

    UE_LOG(LogTemp, Display, TEXT("D3B: Runtime material cache count: %d"), RuntimeMaterialCache.Num());
}

bool AAsciiMapBuilderActor::LoadRuntimeMaterialsFromManifest()
{
    if (MaterialManifestJsonPath.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("D3B: MaterialManifestJsonPath is empty."));
        return false;
    }

    FString FullManifestPath = bMaterialManifestJsonPathIsAbsolute
        ? MaterialManifestJsonPath
        : FPaths::ProjectContentDir() / MaterialManifestJsonPath;

    FPaths::NormalizeFilename(FullManifestPath);
    const FString ManifestDir = FPaths::GetPath(FullManifestPath);

    UE_LOG(LogTemp, Display, TEXT("D3B: Attempting to load material manifest: %s"), *FullManifestPath);

    FString FileContent;
    if (!FFileHelper::LoadFileToString(FileContent, *FullManifestPath))
    {
        UE_LOG(LogTemp, Warning, TEXT("D3B: Failed to read material manifest: %s"), *FullManifestPath);
        return false;
    }

    TSharedPtr<FJsonObject> RootObject;
    const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(FileContent);
    if (!FJsonSerializer::Deserialize(JsonReader, RootObject) || !RootObject.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("D3B: Failed to parse material manifest: %s"), *FullManifestPath);
        return false;
    }

    FString SchemaVersion;
    if (!RootObject->TryGetStringField(TEXT("schema_version"), SchemaVersion) || SchemaVersion != TEXT("material_manifest_v1"))
    {
        UE_LOG(LogTemp, Warning, TEXT("D3B: Unsupported or missing schema_version '%s'. Expected 'material_manifest_v1'."),
            *SchemaVersion);
        return false;
    }

    FString MapId;
    RootObject->TryGetStringField(TEXT("map_id"), MapId);
    UE_LOG(LogTemp, Display, TEXT("D3B: Loaded material_manifest_v1 for map_id=%s"),
        MapId.IsEmpty() ? TEXT("<unknown>") : *MapId);

    const TArray<TSharedPtr<FJsonValue>>* MaterialValues = nullptr;
    if (!RootObject->TryGetArrayField(TEXT("materials"), MaterialValues) || !MaterialValues)
    {
        UE_LOG(LogTemp, Warning, TEXT("D3B: material_manifest.json does not contain a valid materials array."));
        return false;
    }

    for (int32 MaterialIndex = 0; MaterialIndex < MaterialValues->Num(); ++MaterialIndex)
    {
        const TSharedPtr<FJsonValue>& MaterialValue = (*MaterialValues)[MaterialIndex];
        const TSharedPtr<FJsonObject> MaterialObject = MaterialValue.IsValid() ? MaterialValue->AsObject() : nullptr;
        if (!MaterialObject.IsValid())
        {
            UE_LOG(LogTemp, Warning, TEXT("D3B: Skipping material entry %d because it is not a JSON object."), MaterialIndex);
            continue;
        }

        FString MaterialSlotIdString;
        if (!MaterialObject->TryGetStringField(TEXT("material_slot_id"), MaterialSlotIdString) || MaterialSlotIdString.IsEmpty())
        {
            UE_LOG(LogTemp, Warning, TEXT("D3B: Skipping material entry %d because material_slot_id is missing or empty."), MaterialIndex);
            continue;
        }

        const TSharedPtr<FJsonObject>* TexturesObjectPtr = nullptr;
        if (!MaterialObject->TryGetObjectField(TEXT("textures"), TexturesObjectPtr) || !TexturesObjectPtr || !TexturesObjectPtr->IsValid())
        {
            UE_LOG(LogTemp, Warning, TEXT("D3B: Missing textures object for slot '%s'; skipping runtime material for this slot."),
                *MaterialSlotIdString);
            continue;
        }

        FString BaseColorTexturePath;
        FString NormalTexturePath;
        FString RoughnessTexturePath;
        FString HeightTexturePath;
        FString MetallicTexturePath;

        TryGetOptionalJsonStringField(*TexturesObjectPtr, TEXT("basecolor"), BaseColorTexturePath);
        TryGetOptionalJsonStringField(*TexturesObjectPtr, TEXT("normal"), NormalTexturePath);
        TryGetOptionalJsonStringField(*TexturesObjectPtr, TEXT("roughness"), RoughnessTexturePath);
        TryGetOptionalJsonStringField(*TexturesObjectPtr, TEXT("height"), HeightTexturePath);
        TryGetOptionalJsonStringField(*TexturesObjectPtr, TEXT("metallic"), MetallicTexturePath);

        const FName MaterialSlotId(*MaterialSlotIdString);
        if (RuntimeMaterialCache.Contains(MaterialSlotId))
        {
            UE_LOG(LogTemp, Warning, TEXT("D3B: Duplicate material_slot_id '%s' in manifest; later definition overwrites earlier."),
                *MaterialSlotIdString);
        }

        if (UMaterialInstanceDynamic* RuntimeMaterial = CreateRuntimeMaterialFromManifestEntry(
            MaterialSlotId,
            ManifestDir,
            BaseColorTexturePath,
            NormalTexturePath,
            RoughnessTexturePath,
            HeightTexturePath,
            MetallicTexturePath))
        {
            RuntimeMaterialCache.Add(MaterialSlotId, RuntimeMaterial);
        }
    }

    return RuntimeMaterialCache.Num() > 0;
}

UMaterialInstanceDynamic* AAsciiMapBuilderActor::CreateRuntimeMaterialFromManifestEntry(
    FName MaterialSlotId,
    const FString& ManifestDir,
    const FString& BaseColorTexturePath,
    const FString& NormalTexturePath,
    const FString& RoughnessTexturePath,
    const FString& HeightTexturePath,
    const FString& MetallicTexturePath)
{
    if (!RuntimeMaterialMaster)
    {
        return nullptr;
    }

    if (BaseColorTexturePath.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("D3B: Missing basecolor for slot '%s'; skipping runtime material for this slot."),
            *MaterialSlotId.ToString());
        return nullptr;
    }

    TArray<FString> LoadedMapNames;

    UTexture2D* BaseColorTexture = LoadTexture2DFromFile(ResolveManifestTexturePath(ManifestDir, BaseColorTexturePath), true);
    if (!BaseColorTexture)
    {
        UE_LOG(LogTemp, Warning, TEXT("D3B: Failed to load basecolor for slot '%s'; skipping runtime material for this slot."),
            *MaterialSlotId.ToString());
        return nullptr;
    }

    RuntimeLoadedTextures.Add(BaseColorTexture);
    LoadedMapNames.Add(TEXT("basecolor"));

    UMaterialInstanceDynamic* RuntimeMaterial = UMaterialInstanceDynamic::Create(RuntimeMaterialMaster, this);
    if (!RuntimeMaterial)
    {
        UE_LOG(LogTemp, Warning, TEXT("D3B: Failed to create dynamic material instance for slot '%s'."),
            *MaterialSlotId.ToString());
        return nullptr;
    }

    RuntimeMaterial->SetTextureParameterValue(TEXT("BaseColorTex"), BaseColorTexture);
    RuntimeMaterial->SetScalarParameterValue(TEXT("RoughnessValue"), 0.8f);
    RuntimeMaterial->SetScalarParameterValue(TEXT("MetallicValue"), 0.0f);
    RuntimeMaterial->SetScalarParameterValue(TEXT("UVTiling"), 1.0f);

    UTexture2D* NormalTexture = nullptr;
    if (!NormalTexturePath.IsEmpty())
    {
        NormalTexture = LoadTexture2DFromFile(ResolveManifestTexturePath(ManifestDir, NormalTexturePath), false);
        if (NormalTexture)
        {
            RuntimeLoadedTextures.Add(NormalTexture);
            RuntimeMaterial->SetTextureParameterValue(TEXT("NormalTex"), NormalTexture);
            LoadedMapNames.Add(TEXT("normal"));
        }
    }

    UTexture2D* RoughnessTexture = nullptr;
    if (!RoughnessTexturePath.IsEmpty())
    {
        RoughnessTexture = LoadTexture2DFromFile(ResolveManifestTexturePath(ManifestDir, RoughnessTexturePath), false);
        if (RoughnessTexture)
        {
            RuntimeLoadedTextures.Add(RoughnessTexture);
            RuntimeMaterial->SetTextureParameterValue(TEXT("RoughnessTex"), RoughnessTexture);
            LoadedMapNames.Add(TEXT("roughness"));
        }
    }

    UTexture2D* HeightTexture = nullptr;
    if (!HeightTexturePath.IsEmpty())
    {
        HeightTexture = LoadTexture2DFromFile(ResolveManifestTexturePath(ManifestDir, HeightTexturePath), false);
        if (HeightTexture)
        {
            RuntimeLoadedTextures.Add(HeightTexture);
            RuntimeMaterial->SetTextureParameterValue(TEXT("HeightTex"), HeightTexture);
            LoadedMapNames.Add(TEXT("height"));
        }
    }

    UTexture2D* MetallicTexture = nullptr;
    if (!MetallicTexturePath.IsEmpty())
    {
        MetallicTexture = LoadTexture2DFromFile(ResolveManifestTexturePath(ManifestDir, MetallicTexturePath), false);
        if (MetallicTexture)
        {
            RuntimeLoadedTextures.Add(MetallicTexture);
            RuntimeMaterial->SetTextureParameterValue(TEXT("MetallicTex"), MetallicTexture);
            LoadedMapNames.Add(TEXT("metallic"));
        }
    }

    RuntimeMaterial->SetScalarParameterValue(TEXT("bUseNormalMap"), NormalTexture ? 1.0f : 0.0f);

    UE_LOG(LogTemp, Display, TEXT("D3B: Loaded runtime material for slot '%s' with maps: %s"),
        *MaterialSlotId.ToString(),
        *FString::Join(LoadedMapNames, TEXT(", ")));

    if (MetallicTexture)
    {
        UE_LOG(LogTemp, Display, TEXT("D3B: Metallic texture loaded for slot '%s'; MetallicValue remains 0.0 unless the master material uses MetallicTex."),
            *MaterialSlotId.ToString());
    }

    return RuntimeMaterial;
}

UTexture2D* AAsciiMapBuilderActor::LoadTexture2DFromFile(const FString& FullPath, bool bSRGB)
{
    if (FullPath.IsEmpty())
    {
        return nullptr;
    }

    TArray<uint8> FileData;
    if (!FFileHelper::LoadFileToArray(FileData, *FullPath) || FileData.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("D3B: Failed to read texture file: %s"), *FullPath);
        return nullptr;
    }

    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    const EImageFormat ImageFormat = ImageWrapperModule.DetectImageFormat(FileData.GetData(), FileData.Num());
    if (ImageFormat == EImageFormat::Invalid)
    {
        UE_LOG(LogTemp, Warning, TEXT("D3B: Unsupported texture image format: %s"), *FullPath);
        return nullptr;
    }

    TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);
    if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(FileData.GetData(), FileData.Num()))
    {
        UE_LOG(LogTemp, Warning, TEXT("D3B: Failed to decode compressed texture: %s"), *FullPath);
        return nullptr;
    }

    TArray64<uint8> RawData;
    if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawData))
    {
        UE_LOG(LogTemp, Warning, TEXT("D3B: Failed to convert texture to BGRA8: %s"), *FullPath);
        return nullptr;
    }

    const int32 Width = ImageWrapper->GetWidth();
    const int32 Height = ImageWrapper->GetHeight();
    const int64 ExpectedRawDataSize = static_cast<int64>(Width) * static_cast<int64>(Height) * 4;
    if (RawData.Num() < ExpectedRawDataSize)
    {
        UE_LOG(LogTemp, Warning, TEXT("D3B: Decoded texture data is smaller than expected: %s"), *FullPath);
        return nullptr;
    }

    UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
    if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("D3B: Failed to create transient texture: %s"), *FullPath);
        return nullptr;
    }

    Texture->SRGB = bSRGB;
    Texture->NeverStream = true;

    void* TextureData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
    if (!TextureData)
    {
        Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
        UE_LOG(LogTemp, Warning, TEXT("D3B: Failed to lock transient texture data: %s"), *FullPath);
        return nullptr;
    }

    FMemory::Memcpy(TextureData, RawData.GetData(), static_cast<SIZE_T>(ExpectedRawDataSize));
    Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
    Texture->UpdateResource();

    return Texture;
}

FString AAsciiMapBuilderActor::ResolveManifestTexturePath(const FString& ManifestDir, const FString& TexturePath) const
{
    if (TexturePath.IsEmpty())
    {
        return FString();
    }

    FString FullPath = FPaths::IsRelative(TexturePath)
        ? FPaths::Combine(ManifestDir, TexturePath)
        : TexturePath;

    FPaths::NormalizeFilename(FullPath);
    return FullPath;
}

UMaterialInterface* AAsciiMapBuilderActor::ResolveMaterialForSlot(FName MaterialSlotId, UMaterialInterface* FallbackMaterial) const
{
    if (bUseMaterialManifestJson)
    {
        if (UMaterialInstanceDynamic* const* RuntimeMaterial = RuntimeMaterialCache.Find(MaterialSlotId))
        {
            if (*RuntimeMaterial)
            {
                UE_LOG(LogTemp, Verbose, TEXT("D3B: Using RuntimeMaterialCache for slot '%s'."), *MaterialSlotId.ToString());
                return *RuntimeMaterial;
            }
        }
    }

    if (MaterialRegistry)
    {
        if (UMaterialInterface* FoundMaterial = MaterialRegistry->FindMaterialBySlotId(MaterialSlotId))
        {
            UE_LOG(LogTemp, Verbose, TEXT("D3B: Falling back to MaterialRegistry for slot '%s'."), *MaterialSlotId.ToString());
            return FoundMaterial;
        }
    }

    return FallbackMaterial;
}

void AAsciiMapBuilderActor::ApplyMaterialsFromRegistry()
{
    UMaterialInterface* FloorMat = FindMaterialForSlot(TEXT("stone_floor"), StoneFloorMaterial);
    UMaterialInterface* GrassMat = FindMaterialForSlot(TEXT("grass_ground"), GrassMaterial);
    UMaterialInterface* WoodMat = FindMaterialForSlot(TEXT("wood_planks"), WoodMaterial);
    UMaterialInterface* WaterMat = FindMaterialForSlot(TEXT("water"), WaterMaterial);
    UMaterialInterface* WallMat = FindMaterialForSlot(TEXT("stone_wall"), WallMaterial);
    UMaterialInterface* DoorMat = FindMaterialForSlot(TEXT("wooden_door"), DoorMaterial);

    if (FloorMat) FloorInstances->SetMaterial(0, FloorMat);
    if (GrassMat) GrassInstances->SetMaterial(0, GrassMat);
    if (WoodMat)  WoodInstances->SetMaterial(0, WoodMat);
    if (WaterMat) WaterInstances->SetMaterial(0, WaterMat);
    if (WallMat)  WallInstances->SetMaterial(0, WallMat);
    if (DoorMat)  DoorInstances->SetMaterial(0, DoorMat);

    UE_LOG(LogTemp, Display, TEXT("Applied materials from runtime cache, MaterialRegistry, or fallback materials."));
}

UMaterialInterface* AAsciiMapBuilderActor::FindMaterialForSlot(FName SlotId, UMaterialInterface* FallbackMaterial) const
{
    return ResolveMaterialForSlot(SlotId, FallbackMaterial);
}

bool AAsciiMapBuilderActor::LoadMapLines(TArray<FString>& OutLines) const
{
    FString FullPath = bUseRuntimeResolvedMapFilePath && !CurrentRuntimeMapFilePath.IsEmpty()
        ? CurrentRuntimeMapFilePath
        : FPaths::ProjectContentDir() / RelativeMapPath;

    FPaths::NormalizeFilename(FullPath);

    FString FileContent;
    if (!FFileHelper::LoadFileToString(FileContent, *FullPath))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load ASCII map: %s"), *FullPath);
        return false;
    }

    FileContent.ParseIntoArrayLines(OutLines, true);

    OutLines.RemoveAll([](const FString& Line)
        {
            return Line.TrimStartAndEnd().IsEmpty();
        });

    if (OutLines.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("ASCII map is empty."));
        return false;
    }

    const int32 ExpectedWidth = OutLines[0].Len();
    for (int32 Row = 0; Row < OutLines.Num(); ++Row)
    {
        if (OutLines[Row].Len() != ExpectedWidth)
        {
            UE_LOG(LogTemp, Error, TEXT("ASCII map is not rectangular. Row %d has width %d, expected %d."),
                Row, OutLines[Row].Len(), ExpectedWidth);
            return false;
        }
    }

    return true;
}

FVector AAsciiMapBuilderActor::GridToWorld(int32 Column, int32 Row, int32 Width, int32 Height) const
{
    float X = Column * TileSize;
    float Y = -Row * TileSize;

    if (bCenterMapOnActor)
    {
        X -= (Width - 1) * TileSize * 0.5f;
        Y += (Height - 1) * TileSize * 0.5f;
    }

    return GetActorLocation() + FVector(X, Y, 0.0f);
}

void AAsciiMapBuilderActor::ClearGeneratedMap()
{
    for (AActor* SpawnedActor : RuntimeSpawnedActors)
    {
        if (IsValid(SpawnedActor))
        {
            SpawnedActor->Destroy();
        }
    }
    RuntimeSpawnedActors.Empty();

    for (TPair<FName, UInstancedStaticMeshComponent*>& Pair : RuntimeInstanceComponents)
    {
        if (Pair.Value)
        {
            Pair.Value->ClearInstances();
            Pair.Value->DestroyComponent();
        }
    }

    RuntimeInstanceComponents.Empty();

    FloorInstances->ClearInstances();
    GrassInstances->ClearInstances();
    WoodInstances->ClearInstances();
    WaterInstances->ClearInstances();
    WallInstances->ClearInstances();
    DoorInstances->ClearInstances();
}

UInstancedStaticMeshComponent* AAsciiMapBuilderActor::GetOrCreateInstanceComponent(FName ComponentKey, UStaticMesh* Mesh, UMaterialInterface* Material)
{
    if (UInstancedStaticMeshComponent** Existing = RuntimeInstanceComponents.Find(ComponentKey))
    {
        return *Existing;
    }

    UInstancedStaticMeshComponent* NewComp = NewObject<UInstancedStaticMeshComponent>(this, ComponentKey);
    NewComp->SetupAttachment(RootComponent);
    NewComp->RegisterComponent();

    NewComp->SetStaticMesh(Mesh);

    if (Material)
    {
        NewComp->SetMaterial(0, Material);
    }

    RuntimeInstanceComponents.Add(ComponentKey, NewComp);

    UE_LOG(LogTemp, Display, TEXT("Created runtime instanced component '%s' with mesh '%s' and material '%s'."),
        *ComponentKey.ToString(),
        Mesh ? *Mesh->GetName() : TEXT("None"),
        Material ? *Material->GetName() : TEXT("None"));

    return NewComp;
}

void AAsciiMapBuilderActor::ApplyResolvedMaterialToSpawnedActor(AActor* SpawnedActor, UMaterialInterface* Material, FName MaterialSlotId) const
{
    if (!SpawnedActor || !Material)
    {
        return;
    }

    TArray<UMeshComponent*> MeshComponents;
    SpawnedActor->GetComponents<UMeshComponent>(MeshComponents);
    if (MeshComponents.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("D6G: Spawned actor '%s' has no mesh components; material slot '%s' was not applied."),
            *SpawnedActor->GetName(),
            *MaterialSlotId.ToString());
        return;
    }

    for (UMeshComponent* MeshComponent : MeshComponents)
    {
        if (MeshComponent)
        {
            MeshComponent->SetMaterial(0, Material);
        }
    }
}

void AAsciiMapBuilderActor::ResetGenerationStats()
{
    GenerationTotalCells = 0;
    GenerationGeneratedCells = 0;
    GenerationSkippedCells = 0;
    GenerationUnknownSymbols = 0;
    GenerationRegistryTransformInstances = 0;
    GenerationRoleFallbackInstances = 0;
    GenerationSpawnedActors = 0;
    GenerationSymbolCounts.Empty();
    GenerationMeshIdsUsed.Empty();
    GenerationMissingMeshIds.Empty();
    GenerationMaterialSlotsUsed.Empty();
    GenerationMissingMaterialSlots.Empty();
    GenerationWarningKeys.Empty();
}

void AAsciiMapBuilderActor::RecordMissingMeshId(FName MeshId, const FString& SymbolKey, const FString& Reason)
{
    GenerationMissingMeshIds.Add(MeshId);

    const FString WarningKey = FString::Printf(TEXT("mesh:%s:%s"), *MeshId.ToString(), *Reason);
    if (!GenerationWarningKeys.Contains(WarningKey))
    {
        GenerationWarningKeys.Add(WarningKey);
        UE_LOG(LogTemp, Warning, TEXT("Runtime resolver fallback for symbol '%s': MeshId '%s' %s."),
            *SymbolKey,
            MeshId.IsNone() ? TEXT("<empty>") : *MeshId.ToString(),
            *Reason);
    }
}

void AAsciiMapBuilderActor::RecordMissingMaterialSlot(FName MaterialSlotId, const FString& SymbolKey, const FString& Reason)
{
    GenerationMissingMaterialSlots.Add(MaterialSlotId);

    const FString WarningKey = FString::Printf(TEXT("material:%s:%s"), *MaterialSlotId.ToString(), *Reason);
    if (!GenerationWarningKeys.Contains(WarningKey))
    {
        GenerationWarningKeys.Add(WarningKey);
        UE_LOG(LogTemp, Warning, TEXT("Runtime resolver fallback for symbol '%s': MaterialSlotId '%s' %s."),
            *SymbolKey,
            MaterialSlotId.IsNone() ? TEXT("<empty>") : *MaterialSlotId.ToString(),
            *Reason);
    }
}

void AAsciiMapBuilderActor::LogGenerationStats(int32 Width, int32 Height) const
{
    UE_LOG(LogTemp, Display, TEXT("D6G: ASCII map generation finished."));
    UE_LOG(LogTemp, Display, TEXT("D6G: RuntimeData mode = %s"), bUseMapPackageIndex ? TEXT("enabled") : TEXT("disabled"));
    UE_LOG(LogTemp, Display, TEXT("D6G: MapPackageIndexPath = %s"), LastRuntimeDataIndexFullPath.IsEmpty() ? TEXT("<manual/unused>") : *LastRuntimeDataIndexFullPath);
    UE_LOG(LogTemp, Display, TEXT("D6G: Available map count = %d"), LastRuntimeDataAvailableMapCount);
    UE_LOG(LogTemp, Display, TEXT("D6G: Requested SelectedMapId = %s"), LastRuntimeDataRequestedMapId.IsEmpty() ? TEXT("<none>") : *LastRuntimeDataRequestedMapId);
    UE_LOG(LogTemp, Display, TEXT("D6G: Resolved SelectedMapId = %s"), LastRuntimeDataResolvedMapId.IsEmpty() ? TEXT("<manual/none>") : *LastRuntimeDataResolvedMapId);
    UE_LOG(LogTemp, Display, TEXT("D6G: Fallback to first map = %s"), bLastRuntimeDataUsedFirstMapFallback ? TEXT("true") : TEXT("false"));
    UE_LOG(LogTemp, Display, TEXT("D6G: Runtime layout path = %s"), CurrentRuntimeMapFilePath.IsEmpty() ? TEXT("<manual>") : *CurrentRuntimeMapFilePath);
    UE_LOG(LogTemp, Display, TEXT("D6G: Runtime resolved_tileset path = %s"), CurrentRuntimeResolvedTileSetJsonPath.IsEmpty() ? TEXT("<manual>") : *CurrentRuntimeResolvedTileSetJsonPath);
    UE_LOG(LogTemp, Display, TEXT("D6G: Runtime material_manifest path = %s"), CurrentRuntimeMaterialManifestJsonPath.IsEmpty() ? TEXT("<manual>") : *CurrentRuntimeMaterialManifestJsonPath);
    UE_LOG(LogTemp, Display, TEXT("D6G: Map size = %d x %d = %d cells"), Width, Height, GenerationTotalCells);
    UE_LOG(LogTemp, Display, TEXT("D6G: Generated cells = %d"), GenerationGeneratedCells);
    UE_LOG(LogTemp, Display, TEXT("D6G: Skipped cells = %d"), GenerationSkippedCells);
    UE_LOG(LogTemp, Display, TEXT("D6G: Unknown symbols = %d"), GenerationUnknownSymbols);
    UE_LOG(LogTemp, Display, TEXT("D6G: Mesh IDs used = %s"), *FormatNameSetForLog(GenerationMeshIdsUsed));
    UE_LOG(LogTemp, Display, TEXT("D6G: Missing mesh IDs = %s"), *FormatNameSetForLog(GenerationMissingMeshIds));
    UE_LOG(LogTemp, Display, TEXT("D6G: Material slots used = %s"), *FormatNameSetForLog(GenerationMaterialSlotsUsed));
    UE_LOG(LogTemp, Display, TEXT("D6G: Missing material slots = %s"), *FormatNameSetForLog(GenerationMissingMaterialSlots));
    UE_LOG(LogTemp, Display, TEXT("D6G: Registry-transform instances = %d"), GenerationRegistryTransformInstances);
    UE_LOG(LogTemp, Display, TEXT("D6G: Role-fallback instances = %d"), GenerationRoleFallbackInstances);
    UE_LOG(LogTemp, Display, TEXT("D6G: Spawned actor count = %d"), GenerationSpawnedActors);

    for (const TPair<FString, int32>& Pair : GenerationSymbolCounts)
    {
        const FAsciiTileDefinition Def = GetDefinitionForSymbol(Pair.Key[0]);
        const FName ResolvedMaterialSlotId = Def.MaterialSlotId.IsNone() ? Def.SlotId : Def.MaterialSlotId;
        UE_LOG(LogTemp, Display, TEXT("D6G: Symbol '%s' | Count=%d | TileTypeId=%s | MeshId=%s | MaterialSlotId=%s | SlotId=%s | Role=%d | Generate=%s"),
            *Pair.Key,
            Pair.Value,
            *Def.TileTypeId.ToString(),
            *Def.MeshId.ToString(),
            *ResolvedMaterialSlotId.ToString(),
            *Def.SlotId.ToString(),
            static_cast<int32>(Def.Role),
            Def.bGenerate ? TEXT("true") : TEXT("false"));
    }

    UE_LOG(LogTemp, Display, TEXT("D6G: Legacy instance counts | Floor=%d Grass=%d Wood=%d Water=%d Wall=%d Door=%d"),
        FloorInstances->GetInstanceCount(),
        GrassInstances->GetInstanceCount(),
        WoodInstances->GetInstanceCount(),
        WaterInstances->GetInstanceCount(),
        WallInstances->GetInstanceCount(),
        DoorInstances->GetInstanceCount());

    UE_LOG(LogTemp, Display, TEXT("D6G: Runtime dynamic component count = %d"), RuntimeInstanceComponents.Num());
    for (const TPair<FName, UInstancedStaticMeshComponent*>& Pair : RuntimeInstanceComponents)
    {
        if (Pair.Value)
        {
            UE_LOG(LogTemp, Display, TEXT("D6G: Component '%s' instance count = %d"),
                *Pair.Key.ToString(),
                Pair.Value->GetInstanceCount());
        }
    }
}

void AAsciiMapBuilderActor::GenerateMap()
{
    if (bUseMapPackageIndex)
    {
        if (ResolveSelectedMapPackageFromIndex())
        {
            if (bAutoEnableJsonLoadersFromMapPackage)
            {
                CaptureManualJsonPathSettingsForMapPackageOverride();

                bUseResolvedTileSetJson = true;
                bResolvedTileSetJsonPathIsAbsolute = true;
                ResolvedTileSetJsonPath = CurrentRuntimeResolvedTileSetJsonPath;

                bUseMaterialManifestJson = true;
                bMaterialManifestJsonPathIsAbsolute = true;
                MaterialManifestJsonPath = CurrentRuntimeMaterialManifestJsonPath;

                UE_LOG(LogTemp, Display, TEXT("D4B: Auto-enabled D2A/D3B JSON loaders from selected map package."));
            }
            else
            {
                RestoreManualJsonPathSettingsIfNeeded();
                UE_LOG(LogTemp, Display, TEXT("D4B: Auto-enable JSON loaders is disabled; D2A/D3B manual settings remain active."));
            }

            UE_LOG(LogTemp, Display, TEXT("D4B: Runtime map switch completed."));
        }
        else
        {
            ClearRuntimeMapPackageState();
            RestoreManualJsonPathSettingsIfNeeded();
            UE_LOG(LogTemp, Warning, TEXT("D4B: Failed to resolve map package. Falling back to manually configured paths."));
        }
    }
    else
    {
        ClearRuntimeMapPackageState();
        RestoreManualJsonPathSettingsIfNeeded();
        LastRuntimeDataAvailableMapCount = 0;
        LastRuntimeDataRequestedMapId = SelectedMapId.IsNone() ? TEXT("<empty>") : SelectedMapId.ToString();
        LastRuntimeDataResolvedMapId.Empty();
        bLastRuntimeDataUsedFirstMapFallback = false;
        LastRuntimeDataIndexFullPath.Empty();
        UE_LOG(LogTemp, Display, TEXT("D4B: MapPackageIndex loading disabled."));
    }

    RebuildTileDefinitionsCache();
    RebuildRuntimeMaterialCache();
    ClearGeneratedMap();
    ApplyMaterialsFromRegistry();

    TArray<FString> Lines;
    if (!LoadMapLines(Lines))
    {
        return;
    }

    const int32 Height = Lines.Num();
    const int32 Width = Lines[0].Len();

    UE_LOG(LogTemp, Display, TEXT("Generating ASCII map: %d x %d"), Width, Height);

    ResetGenerationStats();

    for (int32 Row = 0; Row < Height; ++Row)
    {
        for (int32 Column = 0; Column < Width; ++Column)
        {
            const TCHAR Symbol = Lines[Row][Column];
            const FString SymbolKey = FString::Chr(Symbol);

            GenerationTotalCells++;
            int32& SymbolCount = GenerationSymbolCounts.FindOrAdd(SymbolKey);
            SymbolCount++;

            const FAsciiTileDefinition Def = GetDefinitionForSymbol(Symbol);
            if (!TileDefinitions.Contains(SymbolKey))
            {
                GenerationUnknownSymbols++;
                if (SymbolCount == 1)
                {
                    UE_LOG(LogTemp, Warning, TEXT("Unknown map symbol '%s' first seen at row=%d column=%d"),
                        *SymbolKey, Row, Column);
                }
            }

            if (Def.bGenerate)
            {
                GenerationGeneratedCells++;
            }
            else
            {
                GenerationSkippedCells++;
            }

            const FVector WorldLocation = GridToWorld(Column, Row, Width, Height);
            AddTileInstance(Symbol, WorldLocation);
        }
    }

    LogGenerationStats(Width, Height);
}

void AAsciiMapBuilderActor::AddTileInstance(const TCHAR Symbol, const FVector& WorldLocation)
{
    const FAsciiTileDefinition Def = GetDefinitionForSymbol(Symbol);

    if (TryAddTileInstanceRuntimeResolved(Symbol, WorldLocation, Def))
    {
        return;
    }

    if (!Def.bGenerate)
    {
        return;
    }

    const float PlaneScale = TileSize / 100.0f;
    const float CubeXYScale = TileSize / 100.0f;

    FTransform Transform;
    Transform.SetRotation(FQuat::Identity);

    switch (Def.Role)
    {
    case EAsciiTileRole::Floor:
        Transform.SetLocation(WorldLocation + FVector(0, 0, Def.ZOffset));
        Transform.SetScale3D(FVector(PlaneScale, PlaneScale, 1.0f));
        FloorInstances->AddInstance(Transform);
        GenerationRoleFallbackInstances++;
        break;

    case EAsciiTileRole::Grass:
        Transform.SetLocation(WorldLocation + FVector(0, 0, Def.ZOffset));
        Transform.SetScale3D(FVector(PlaneScale, PlaneScale, 1.0f));
        GrassInstances->AddInstance(Transform);
        GenerationRoleFallbackInstances++;
        break;

    case EAsciiTileRole::Wood:
        Transform.SetLocation(WorldLocation + FVector(0, 0, Def.ZOffset));
        Transform.SetScale3D(FVector(PlaneScale, PlaneScale, 1.0f));
        WoodInstances->AddInstance(Transform);
        GenerationRoleFallbackInstances++;
        break;

    case EAsciiTileRole::Water:
        Transform.SetLocation(WorldLocation + FVector(0, 0, Def.ZOffset));
        Transform.SetScale3D(FVector(PlaneScale, PlaneScale, 1.0f));
        WaterInstances->AddInstance(Transform);
        GenerationRoleFallbackInstances++;
        break;

    case EAsciiTileRole::Wall:
        Transform.SetLocation(WorldLocation + FVector(0, 0, Def.Height * 0.5f));
        Transform.SetScale3D(FVector(CubeXYScale, CubeXYScale, Def.Height / 100.0f));
        WallInstances->AddInstance(Transform);
        GenerationRoleFallbackInstances++;
        break;

    case EAsciiTileRole::Door:
        Transform.SetLocation(WorldLocation + FVector(0, 0, Def.Height * 0.5f));
        Transform.SetScale3D(FVector(CubeXYScale * 0.8f, CubeXYScale * 0.2f, Def.Height / 100.0f));
        DoorInstances->AddInstance(Transform);
        GenerationRoleFallbackInstances++;
        break;

    case EAsciiTileRole::Foliage:
        // First MVP: use door/cube component as proxy for vertical foliage volume.
        Transform.SetLocation(WorldLocation + FVector(0, 0, Def.Height * 0.5f));
        Transform.SetScale3D(FVector(CubeXYScale * 0.7f, CubeXYScale * 0.7f, Def.Height / 100.0f));
        DoorInstances->AddInstance(Transform);
        GenerationRoleFallbackInstances++;
        break;

    default:
        break;
    }
}

bool AAsciiMapBuilderActor::TryAddTileInstanceRuntimeResolved(const TCHAR Symbol, const FVector& WorldLocation, const FAsciiTileDefinition& Def)
{
    if (!bUseRuntimeAssetResolver)
    {
        return false;
    }

    if (!Def.bGenerate)
    {
        return false;
    }

    const FString SymbolKey = FString::Chr(Symbol);

    const FName MeshId = Def.MeshId;
    const FName MaterialSlotId = Def.MaterialSlotId.IsNone() ? Def.SlotId : Def.MaterialSlotId;

    if (MeshId.IsNone())
    {
        RecordMissingMeshId(MeshId, SymbolKey, TEXT("is empty"));
        return false;
    }

    GenerationMeshIdsUsed.Add(MeshId);

    if (MaterialSlotId.IsNone())
    {
        RecordMissingMaterialSlot(MaterialSlotId, SymbolKey, TEXT("and SlotId are empty"));
        return false;
    }

    GenerationMaterialSlotsUsed.Add(MaterialSlotId);

    if (!MeshRegistry)
    {
        RecordMissingMeshId(MeshId, SymbolKey, TEXT("could not be resolved because MeshRegistry is not assigned"));
        return false;
    }

    FGeneratedMeshEntry MeshEntry;
    if (!MeshRegistry->FindMeshById(MeshId, MeshEntry))
    {
        RecordMissingMeshId(MeshId, SymbolKey, TEXT("was not found in MeshRegistry"));
        return false;
    }

    UMaterialInterface* Material = ResolveMaterialForSlot(MaterialSlotId, nullptr);
    if (!Material)
    {
        RecordMissingMaterialSlot(MaterialSlotId, SymbolKey, TEXT("was not found in RuntimeMaterialCache or MaterialRegistry"));
        return false;
    }

    const bool bCanSpawnActor = MeshEntry.bSpawnActorInsteadOfStaticMesh && MeshEntry.ActorClass != nullptr;
    if (MeshEntry.bUseRegistryTransform)
    {
        FVector FinalLocation = WorldLocation + MeshEntry.LocationOffset + FVector(0.0f, 0.0f, Def.ZOffset);
        FVector FinalScale = MeshEntry.DefaultScale;

        if (MeshEntry.Mesh)
        {
            const FBoxSphereBounds MeshBounds = MeshEntry.Mesh->GetBounds();
            const FVector MeshSize = MeshBounds.BoxExtent * 2.0f;

            if (MeshEntry.bFitXYToTileSize)
            {
                if (MeshSize.X > KINDA_SMALL_NUMBER)
                {
                    FinalScale.X = (TileSize * MeshEntry.DefaultScale.X) / MeshSize.X;
                }

                if (MeshSize.Y > KINDA_SMALL_NUMBER)
                {
                    FinalScale.Y = (TileSize * MeshEntry.DefaultScale.Y) / MeshSize.Y;
                }
            }

            if (MeshEntry.bFitZToTileSize && MeshSize.Z > KINDA_SMALL_NUMBER)
            {
                FinalScale.Z = (TileSize * MeshEntry.DefaultScale.Z) / MeshSize.Z;
            }

            if (MeshEntry.bBottomAlignToTileBase)
            {
                const float LocalBottomZ = MeshBounds.Origin.Z - MeshBounds.BoxExtent.Z;
                FinalLocation.Z -= LocalBottomZ * FinalScale.Z;
            }
        }

        FTransform Transform;
        Transform.SetLocation(FinalLocation);
        Transform.SetRotation(MeshEntry.RotationOffset.Quaternion());
        Transform.SetScale3D(FinalScale);

        if (bCanSpawnActor)
        {
            UWorld* World = GetWorld();
            if (!World)
            {
                UE_LOG(LogTemp, Warning, TEXT("Runtime resolver fallback for symbol '%s': cannot spawn ActorClass for MeshId '%s' because World is null."),
                    *SymbolKey,
                    *MeshId.ToString());
                return false;
            }

            FActorSpawnParameters SpawnParams;
            SpawnParams.Owner = this;
            SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

            AActor* SpawnedActor = World->SpawnActor<AActor>(MeshEntry.ActorClass, Transform.GetLocation(), Transform.GetRotation().Rotator(), SpawnParams);
            if (SpawnedActor)
            {
                SpawnedActor->SetActorScale3D(Transform.GetScale3D());
                RuntimeSpawnedActors.Add(SpawnedActor);
                ApplyResolvedMaterialToSpawnedActor(SpawnedActor, Material, MaterialSlotId);
                GenerationRegistryTransformInstances++;
                GenerationSpawnedActors++;
                return true;
            }

            UE_LOG(LogTemp, Warning, TEXT("Runtime resolver fallback for symbol '%s': failed to spawn ActorClass for MeshId '%s'."),
                *SymbolKey,
                *MeshId.ToString());

            if (!MeshEntry.Mesh)
            {
                RecordMissingMeshId(MeshId, SymbolKey, TEXT("has no StaticMesh fallback after actor spawn failed"));
                return false;
            }
        }

        if (!MeshEntry.Mesh)
        {
            RecordMissingMeshId(MeshId, SymbolKey, TEXT("has no StaticMesh assigned"));
            return false;
        }

        const FName ComponentKey(*FString::Printf(TEXT("%s__%s"), *MeshId.ToString(), *MaterialSlotId.ToString()));
        UInstancedStaticMeshComponent* RuntimeComponent = GetOrCreateInstanceComponent(ComponentKey, MeshEntry.Mesh, Material);
        if (!RuntimeComponent)
        {
            UE_LOG(LogTemp, Warning, TEXT("Runtime resolver fallback for symbol '%s': failed to create component '%s'."),
                *SymbolKey,
                *ComponentKey.ToString());
            return false;
        }

        RuntimeComponent->AddInstance(Transform);
        GenerationRegistryTransformInstances++;
        return true;
    }

    if (!MeshEntry.Mesh)
    {
        RecordMissingMeshId(MeshId, SymbolKey, TEXT("has no StaticMesh assigned for role-based fallback"));
        return false;
    }

    const FName ComponentKey(*FString::Printf(TEXT("%s__%s"), *MeshId.ToString(), *MaterialSlotId.ToString()));
    UInstancedStaticMeshComponent* RuntimeComponent = GetOrCreateInstanceComponent(ComponentKey, MeshEntry.Mesh, Material);
    if (!RuntimeComponent)
    {
        UE_LOG(LogTemp, Warning, TEXT("Runtime resolver fallback for symbol '%s': failed to create component '%s'."),
            *SymbolKey,
            *ComponentKey.ToString());
        return false;
    }

    const float PlaneScale = TileSize / 100.0f;
    const float CubeXYScale = TileSize / 100.0f;
    const float WallHeightForTile = Def.Height > 0.0f ? Def.Height : WallHeight;
    const float DoorHeightForTile = Def.Height > 0.0f ? Def.Height : DoorHeight;

    FTransform Transform;
    Transform.SetRotation(FQuat::Identity);

    FVector Scale(PlaneScale, PlaneScale, 1.0f);

    switch (Def.Role)
    {
    case EAsciiTileRole::Floor:
    case EAsciiTileRole::Grass:
    case EAsciiTileRole::Wood:
    case EAsciiTileRole::Water:
        Transform.SetLocation(WorldLocation + FVector(0, 0, Def.ZOffset));
        Scale = FVector(PlaneScale, PlaneScale, 1.0f);
        break;

    case EAsciiTileRole::Wall:
        Transform.SetLocation(WorldLocation + FVector(0, 0, Def.ZOffset + WallHeightForTile * 0.5f));
        Scale = FVector(CubeXYScale, CubeXYScale, WallHeightForTile / 100.0f);
        break;

    case EAsciiTileRole::Door:
        Transform.SetLocation(WorldLocation + FVector(0, 0, Def.ZOffset + DoorHeightForTile * 0.5f));
        Scale = FVector(CubeXYScale * 0.8f, CubeXYScale * 0.2f, DoorHeightForTile / 100.0f);
        break;

    case EAsciiTileRole::Foliage:
        if (Def.Height > 0.0f)
        {
            Transform.SetLocation(WorldLocation + FVector(0, 0, Def.ZOffset + Def.Height * 0.5f));
            Scale = FVector(CubeXYScale * 0.7f, CubeXYScale * 0.7f, Def.Height / 100.0f);
        }
        else
        {
            Transform.SetLocation(WorldLocation + FVector(0, 0, Def.ZOffset));
            Scale = FVector(PlaneScale, PlaneScale, 1.0f);
        }
        break;

    default:
        UE_LOG(LogTemp, Warning, TEXT("Runtime resolver fallback for symbol '%s': role %d is not generated by legacy role geometry."),
            *SymbolKey,
            static_cast<int32>(Def.Role));
        return false;
    }

    Transform.SetScale3D(FVector(
        Scale.X * MeshEntry.DefaultScale.X,
        Scale.Y * MeshEntry.DefaultScale.Y,
        Scale.Z * MeshEntry.DefaultScale.Z));

    RuntimeComponent->AddInstance(Transform);
    GenerationRoleFallbackInstances++;
    return true;
}

// Called when the game starts or when spawned
void AAsciiMapBuilderActor::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AAsciiMapBuilderActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

