// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AsciiMapTypes.h"
#include "AsciiMapBuilderActor.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UAsciiTileSetDataAsset;
class UMaterialRegistryDataAsset;
class UGeneratedMeshRegistryDataAsset;
class UTexture2D;

UCLASS()
class VISUALOPTIMIZATIONUE_API AAsciiMapBuilderActor : public AActor
{
	GENERATED_BODY()

	
public:	
	// Sets default values for this actor's properties
	AAsciiMapBuilderActor();


	UFUNCTION(CallInEditor, BlueprintCallable, Category = "ASCII Map")
	void GenerateMap();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "ASCII Map")
	void ClearGeneratedMap();

protected:
    virtual void OnConstruction(const FTransform& Transform) override;

    UPROPERTY(EditAnywhere, Category = "ASCII Map")
    UAsciiTileSetDataAsset* TileSet = nullptr;

    UPROPERTY(EditAnywhere, Category = "ASCII Map")
    UMaterialRegistryDataAsset* MaterialRegistry = nullptr;

    UPROPERTY(EditAnywhere, Category = "ASCII Map")
    UGeneratedMeshRegistryDataAsset* MeshRegistry = nullptr;

    UPROPERTY(EditAnywhere, Category = "ASCII Map|Runtime Resolver")
    bool bUseRuntimeAssetResolver = true;

    UPROPERTY()
    TMap<FName, UInstancedStaticMeshComponent*> RuntimeInstanceComponents;

private:
    bool LoadMapLines(TArray<FString>& OutLines) const;
    FVector GridToWorld(int32 Column, int32 Row, int32 Width, int32 Height) const;
    void AddTileInstance(const TCHAR Symbol, const FVector& WorldLocation);
    bool TryAddTileInstanceRuntimeResolved(const TCHAR Symbol, const FVector& WorldLocation, const FAsciiTileDefinition& Def);
    void RebuildTileDefinitionsCache();
    bool LoadTileDefinitionsFromResolvedJson();
    bool LoadTileDefinitionsFromTileSetDataAsset();
    void LoadBuiltInFallbackTileDefinitions();
    void CreateDefaultTileDefinitions();
    FAsciiTileDefinition GetDefinitionForSymbol(const TCHAR Symbol) const;
    EAsciiTileRole ConvertResolvedRoleStringToTileRole(const FString& RoleString) const;

    void RebuildRuntimeMaterialCache();
    bool LoadRuntimeMaterialsFromManifest();
    UMaterialInstanceDynamic* CreateRuntimeMaterialFromManifestEntry(
        FName MaterialSlotId,
        const FString& ManifestDir,
        const FString& BaseColorTexturePath,
        const FString& NormalTexturePath,
        const FString& RoughnessTexturePath,
        const FString& HeightTexturePath,
        const FString& MetallicTexturePath);
    UTexture2D* LoadTexture2DFromFile(const FString& FullPath, bool bSRGB);
    FString ResolveManifestTexturePath(const FString& ManifestDir, const FString& TexturePath) const;
    UMaterialInterface* ResolveMaterialForSlot(FName MaterialSlotId, UMaterialInterface* FallbackMaterial) const;

    void ApplyMaterialsFromRegistry();
    UMaterialInterface* FindMaterialForSlot(FName SlotId, UMaterialInterface* FallbackMaterial) const;

    UInstancedStaticMeshComponent* GetOrCreateInstanceComponent(FName ComponentKey, UStaticMesh* Mesh, UMaterialInterface* Material);
private:
    UPROPERTY(EditAnywhere, Category = "ASCII Map")
    FString RelativeMapPath = TEXT("VisualOptimization/Data/test_map1/map.txt");

    UPROPERTY(EditAnywhere, Category = "ASCII Map|Resolved TileSet JSON")
    bool bUseResolvedTileSetJson = false;

    UPROPERTY(EditAnywhere, Category = "ASCII Map|Resolved TileSet JSON")
    bool bResolvedTileSetJsonPathIsAbsolute = false;

    UPROPERTY(EditAnywhere, Category = "ASCII Map|Resolved TileSet JSON")
    FString ResolvedTileSetJsonPath = TEXT("VisualOptimization/Data/test_map1/resolved_tileset.json");

    UPROPERTY(EditAnywhere, Category = "ASCII Map|Material Manifest JSON")
    bool bUseMaterialManifestJson = false;

    UPROPERTY(EditAnywhere, Category = "ASCII Map|Material Manifest JSON")
    bool bMaterialManifestJsonPathIsAbsolute = false;

    UPROPERTY(EditAnywhere, Category = "ASCII Map|Material Manifest JSON")
    FString MaterialManifestJsonPath = TEXT("VisualOptimization/Generated/test_map1/material_manifest.json");

    UPROPERTY(EditAnywhere, Category = "ASCII Map|Material Manifest JSON")
    UMaterialInterface* RuntimeMaterialMaster = nullptr;

    UPROPERTY(Transient)
    TMap<FName, UMaterialInstanceDynamic*> RuntimeMaterialCache;

    UPROPERTY(Transient)
    TArray<UTexture2D*> RuntimeLoadedTextures;

    UPROPERTY(EditAnywhere, Category = "ASCII Map")
    float TileSize = 400.0f;

    UPROPERTY(EditAnywhere, Category = "ASCII Map")
    float WallHeight = 300.0f;

    UPROPERTY(EditAnywhere, Category = "ASCII Map")
    float DoorHeight = 250.0f;

    UPROPERTY(EditAnywhere, Category = "ASCII Map")
    bool bCenterMapOnActor = true;

    UPROPERTY(EditAnywhere, Category = "Meshes")
    UStaticMesh* TilePlaneMesh = nullptr;

    UPROPERTY(EditAnywhere, Category = "Meshes")
    UStaticMesh* CubeMesh = nullptr;

    UPROPERTY(EditAnywhere, Category = "Materials")
    UMaterialInterface* StoneFloorMaterial = nullptr;

    UPROPERTY(EditAnywhere, Category = "Materials")
    UMaterialInterface* GrassMaterial = nullptr;

    UPROPERTY(EditAnywhere, Category = "Materials")
    UMaterialInterface* WoodMaterial = nullptr;

    UPROPERTY(EditAnywhere, Category = "Materials")
    UMaterialInterface* WaterMaterial = nullptr;

    UPROPERTY(EditAnywhere, Category = "Materials")
    UMaterialInterface* WallMaterial = nullptr;

    UPROPERTY(EditAnywhere, Category = "Materials")
    UMaterialInterface* DoorMaterial = nullptr;

    UPROPERTY(VisibleAnywhere)
    UInstancedStaticMeshComponent* FloorInstances;

    UPROPERTY(VisibleAnywhere)
    UInstancedStaticMeshComponent* GrassInstances;

    UPROPERTY(VisibleAnywhere)
    UInstancedStaticMeshComponent* WoodInstances;

    UPROPERTY(VisibleAnywhere)
    UInstancedStaticMeshComponent* WaterInstances;

    UPROPERTY(VisibleAnywhere)
    UInstancedStaticMeshComponent* WallInstances;

    UPROPERTY(VisibleAnywhere)
    UInstancedStaticMeshComponent* DoorInstances;

    TMap<FString, FAsciiTileDefinition> TileDefinitions;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

};
