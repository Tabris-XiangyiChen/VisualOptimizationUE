// Fill out your copyright notice in the Description page of Project Settings.


#include "AsciiMapBuilderActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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

    CreateDefaultTileDefinitions();
}

void AAsciiMapBuilderActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    CreateDefaultTileDefinitions();

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
}

void AAsciiMapBuilderActor::CreateDefaultTileDefinitions()
{
    TileDefinitions.Empty();

    TileDefinitions.Add(TEXT("0"), { TEXT("0"), TEXT("void"), EAsciiTileRole::Void, false, 0.0f, 0.0f });
    TileDefinitions.Add(TEXT("."), { TEXT("."), TEXT("stone_floor"), EAsciiTileRole::Floor, true, 0.0f, 0.0f });
    TileDefinitions.Add(TEXT("g"), { TEXT("g"), TEXT("grass_ground"), EAsciiTileRole::Grass, true, 0.0f, 0.0f });
    TileDefinitions.Add(TEXT("="), { TEXT("="), TEXT("wood_planks"), EAsciiTileRole::Wood, true, 5.0f, 0.0f });
    TileDefinitions.Add(TEXT("~"), { TEXT("~"), TEXT("water"), EAsciiTileRole::Water, true, -20.0f, 0.0f });
    TileDefinitions.Add(TEXT("#"), { TEXT("#"), TEXT("stone_wall"), EAsciiTileRole::Wall, true, 0.0f, WallHeight });
    TileDefinitions.Add(TEXT("D"), { TEXT("D"), TEXT("wooden_door"), EAsciiTileRole::Door, true, 0.0f, DoorHeight });

    TileDefinitions.Add(TEXT("T"), { TEXT("T"), TEXT("tree_foliage_proxy"), EAsciiTileRole::Foliage, true, 0.0f, 200.0f });
    TileDefinitions.Add(TEXT("c"), { TEXT("c"), TEXT("clover_patch"), EAsciiTileRole::Grass, true, 2.0f, 0.0f });
    TileDefinitions.Add(TEXT("^"), { TEXT("^"), TEXT("tall_grass"), EAsciiTileRole::Grass, true, 2.0f, 0.0f });
}

FAsciiTileDefinition AAsciiMapBuilderActor::GetDefinitionForSymbol(const TCHAR Symbol) const
{
    const FString Key = FString::Chr(Symbol);

    if (const FAsciiTileDefinition* Found = TileDefinitions.Find(Key))
    {
        return *Found;
    }

    return { Key, TEXT("unknown"), EAsciiTileRole::Void, false, 0.0f, 0.0f };
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
    FloorInstances->ClearInstances();
    GrassInstances->ClearInstances();
    WoodInstances->ClearInstances();
    WaterInstances->ClearInstances();
    WallInstances->ClearInstances();
    DoorInstances->ClearInstances();
}

void AAsciiMapBuilderActor::GenerateMap()
{
    ClearGeneratedMap();

    TArray<FString> Lines;
    if (!LoadMapLines(Lines))
    {
        return;
    }

    const int32 Height = Lines.Num();
    const int32 Width = Lines[0].Len();

    UE_LOG(LogTemp, Display, TEXT("Generating ASCII map: %d x %d"), Width, Height);

    for (int32 Row = 0; Row < Height; ++Row)
    {
        for (int32 Column = 0; Column < Width; ++Column)
        {
            const TCHAR Symbol = Lines[Row][Column];
            const FVector WorldLocation = GridToWorld(Column, Row, Width, Height);
            AddTileInstance(Symbol, WorldLocation);
        }
    }
}

void AAsciiMapBuilderActor::AddTileInstance(const TCHAR Symbol, const FVector& WorldLocation)
{
    const FAsciiTileDefinition Def = GetDefinitionForSymbol(Symbol);

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

