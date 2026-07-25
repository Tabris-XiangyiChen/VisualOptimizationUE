// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AsciiMapTypes.h"
#include "AsciiMapBuilderActor.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UAsciiTileSetDataAsset;

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

private:
    bool LoadMapLines(TArray<FString>& OutLines) const;
    FVector GridToWorld(int32 Column, int32 Row, int32 Width, int32 Height) const;
    void AddTileInstance(const TCHAR Symbol, const FVector& WorldLocation);
    void CreateDefaultTileDefinitions();
    FAsciiTileDefinition GetDefinitionForSymbol(const TCHAR Symbol) const;

private:
    UPROPERTY(EditAnywhere, Category = "ASCII Map")
    FString RelativeMapPath = TEXT("VisualOptimization/Data/test_map1/map.txt");

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
