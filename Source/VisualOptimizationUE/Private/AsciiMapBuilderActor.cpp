// Fill out your copyright notice in the Description page of Project Settings.


#include "AsciiMapBuilderActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
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
    const FString FullPath = FPaths::ProjectContentDir() / RelativeMapPath;

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

void AAsciiMapBuilderActor::GenerateMap()
{
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

    TMap<FString, int32> SymbolCounts;
    int32 GeneratedTileCount = 0;
    int32 SkippedTileCount = 0;
    int32 UnknownSymbolCount = 0;

    for (int32 Row = 0; Row < Height; ++Row)
    {
        for (int32 Column = 0; Column < Width; ++Column)
        {
            const TCHAR Symbol = Lines[Row][Column];
            const FString SymbolKey = FString::Chr(Symbol);

            SymbolCounts.FindOrAdd(SymbolKey)++;

            const FAsciiTileDefinition Def = GetDefinitionForSymbol(Symbol);
            if (!TileDefinitions.Contains(SymbolKey))
            {
                UnknownSymbolCount++;
                UE_LOG(LogTemp, Warning, TEXT("Unknown map symbol '%s' at row=%d column=%d"),
                    *SymbolKey, Row, Column);
            }

            if (Def.bGenerate)
            {
                GeneratedTileCount++;
            }
            else
            {
                SkippedTileCount++;
            }

            const FVector WorldLocation = GridToWorld(Column, Row, Width, Height);
            AddTileInstance(Symbol, WorldLocation);
        }
    }

    UE_LOG(LogTemp, Display, TEXT("ASCII map generation finished."));
    UE_LOG(LogTemp, Display, TEXT("Map size: %d x %d = %d cells"), Width, Height, Width * Height);
    UE_LOG(LogTemp, Display, TEXT("Generated tiles: %d"), GeneratedTileCount);
    UE_LOG(LogTemp, Display, TEXT("Skipped tiles: %d"), SkippedTileCount);
    UE_LOG(LogTemp, Display, TEXT("Unknown symbols: %d"), UnknownSymbolCount);

    for (const TPair<FString, int32>& Pair : SymbolCounts)
    {
        const FAsciiTileDefinition Def = GetDefinitionForSymbol(Pair.Key[0]);
        const FName ResolvedMaterialSlotId = Def.MaterialSlotId.IsNone() ? Def.SlotId : Def.MaterialSlotId;
        UE_LOG(LogTemp, Display, TEXT("Symbol '%s' | Count=%d | TileTypeId=%s | MeshId=%s | MaterialSlotId=%s | SlotId=%s | Role=%d | Generate=%s"),
            *Pair.Key,
            Pair.Value,
            *Def.TileTypeId.ToString(),
            *Def.MeshId.ToString(),
            *ResolvedMaterialSlotId.ToString(),
            *Def.SlotId.ToString(),
            static_cast<int32>(Def.Role),
            Def.bGenerate ? TEXT("true") : TEXT("false"));
    }

    UE_LOG(LogTemp, Display, TEXT("Instance counts | Floor=%d Grass=%d Wood=%d Water=%d Wall=%d Door=%d"),
        FloorInstances->GetInstanceCount(),
        GrassInstances->GetInstanceCount(),
        WoodInstances->GetInstanceCount(),
        WaterInstances->GetInstanceCount(),
        WallInstances->GetInstanceCount(),
        DoorInstances->GetInstanceCount());

    UE_LOG(LogTemp, Display, TEXT("Runtime dynamic component count: %d"), RuntimeInstanceComponents.Num());
    for (const TPair<FName, UInstancedStaticMeshComponent*>& Pair : RuntimeInstanceComponents)
    {
        if (Pair.Value)
        {
            UE_LOG(LogTemp, Display, TEXT("Component '%s' instance count = %d"),
                *Pair.Key.ToString(),
                Pair.Value->GetInstanceCount());
        }
    }
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
        break;

    case EAsciiTileRole::Grass:
        Transform.SetLocation(WorldLocation + FVector(0, 0, Def.ZOffset));
        Transform.SetScale3D(FVector(PlaneScale, PlaneScale, 1.0f));
        GrassInstances->AddInstance(Transform);
        break;

    case EAsciiTileRole::Wood:
        Transform.SetLocation(WorldLocation + FVector(0, 0, Def.ZOffset));
        Transform.SetScale3D(FVector(PlaneScale, PlaneScale, 1.0f));
        WoodInstances->AddInstance(Transform);
        break;

    case EAsciiTileRole::Water:
        Transform.SetLocation(WorldLocation + FVector(0, 0, Def.ZOffset));
        Transform.SetScale3D(FVector(PlaneScale, PlaneScale, 1.0f));
        WaterInstances->AddInstance(Transform);
        break;

    case EAsciiTileRole::Wall:
        Transform.SetLocation(WorldLocation + FVector(0, 0, Def.Height * 0.5f));
        Transform.SetScale3D(FVector(CubeXYScale, CubeXYScale, Def.Height / 100.0f));
        WallInstances->AddInstance(Transform);
        break;

    case EAsciiTileRole::Door:
        Transform.SetLocation(WorldLocation + FVector(0, 0, Def.Height * 0.5f));
        Transform.SetScale3D(FVector(CubeXYScale * 0.8f, CubeXYScale * 0.2f, Def.Height / 100.0f));
        DoorInstances->AddInstance(Transform);
        break;

    case EAsciiTileRole::Foliage:
        // First MVP: use door/cube component as proxy for vertical foliage volume.
        Transform.SetLocation(WorldLocation + FVector(0, 0, Def.Height * 0.5f));
        Transform.SetScale3D(FVector(CubeXYScale * 0.7f, CubeXYScale * 0.7f, Def.Height / 100.0f));
        DoorInstances->AddInstance(Transform);
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

    if (!MeshRegistry)
    {
        UE_LOG(LogTemp, Warning, TEXT("Runtime resolver fallback for symbol '%s': MeshRegistry is not assigned."), *SymbolKey);
        return false;
    }

    const FName MeshId = Def.MeshId;
    const FName MaterialSlotId = Def.MaterialSlotId.IsNone() ? Def.SlotId : Def.MaterialSlotId;

    if (MeshId.IsNone())
    {
        UE_LOG(LogTemp, Warning, TEXT("Runtime resolver fallback for symbol '%s': MeshId is empty."), *SymbolKey);
        return false;
    }

    if (MaterialSlotId.IsNone())
    {
        UE_LOG(LogTemp, Warning, TEXT("Runtime resolver fallback for symbol '%s': MaterialSlotId and SlotId are empty."), *SymbolKey);
        return false;
    }

    FGeneratedMeshEntry MeshEntry;
    if (!MeshRegistry->FindMeshById(MeshId, MeshEntry) || !MeshEntry.Mesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("Runtime resolver fallback for symbol '%s': MeshId '%s' was not found or has no mesh."),
            *SymbolKey,
            *MeshId.ToString());
        return false;
    }

    UMaterialInterface* Material = ResolveMaterialForSlot(MaterialSlotId, nullptr);
    if (!Material)
    {
        UE_LOG(LogTemp, Warning, TEXT("Runtime resolver fallback for symbol '%s': MaterialSlotId '%s' was not found in RuntimeMaterialCache or MaterialRegistry."),
            *SymbolKey,
            *MaterialSlotId.ToString());
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
        UE_LOG(LogTemp, Warning, TEXT("Runtime resolver fallback for symbol '%s': role %d is not generated by C3."),
            *SymbolKey,
            static_cast<int32>(Def.Role));
        return false;
    }

    Transform.SetScale3D(FVector(
        Scale.X * MeshEntry.DefaultScale.X,
        Scale.Y * MeshEntry.DefaultScale.Y,
        Scale.Z * MeshEntry.DefaultScale.Z));

    RuntimeComponent->AddInstance(Transform);
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

