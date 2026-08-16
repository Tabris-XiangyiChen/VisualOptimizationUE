#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MapBuilderV2/AsciiMapBuilderV2Types.h"
#include "RuntimeCommon/AsciiRuntimePackageTypes.h"
#include "AsciiMapBuilderV2Actor.generated.h"

class FAsciiGeometryEmitterV2;
class FAsciiMapGridV2;
class UAsciiRuntimeMaterialProvider;
class UGeneratedMeshRegistryV2DataAsset;
class UInstancedStaticMeshComponent;
class UMapBuilderV2ConfigDataAsset;
class UMaterialInterface;
class UMaterialRegistryDataAsset;
class USceneComponent;
class UStaticMesh;
class FJsonObject;

UCLASS()
class VISUALOPTIMIZATIONUE_API AAsciiMapBuilderV2Actor : public AActor
{
    GENERATED_BODY()

public:
    AAsciiMapBuilderV2Actor();

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "ASCII Map V2")
    void GenerateMapV2();

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "ASCII Map V2")
    void ClearGeneratedMapV2();

    UFUNCTION()
    TArray<FString> GetAvailableMapIdOptions() const;

protected:
    virtual void BeginPlay() override;

private:
    friend class FAsciiGeometryEmitterV2;

    bool ResolveSelectedMapPackage(FRuntimeMapPackagePathsV2& OutPaths, FString& OutError) const;
    bool LoadMapPackageIndex(TSharedPtr<FJsonObject>& OutRoot, FString& OutAbsoluteIndexPath, FString& OutError) const;
    bool LoadRuntimeTileDefinitions(
        const FString& AbsolutePath,
        TMap<FString, FAsciiRuntimeTileDefinitionV2>& OutDefinitions,
        FString& OutError) const;
    bool LoadMapLines(const FString& AbsolutePath, TArray<FString>& OutLines, FString& OutError) const;
    bool BuildSemanticGrid(
        const TArray<FString>& Lines,
        const TMap<FString, FAsciiRuntimeTileDefinitionV2>& Definitions,
        FAsciiMapGridV2& OutGrid,
        FString& OutError);
    void ValidateRuntimeDefinitions(const TMap<FString, FAsciiRuntimeTileDefinitionV2>& Definitions);

    FString ResolveIndexPath() const;
    FString ResolveIndexRelativePath(const FString& AbsoluteIndexPath, const FString& RuntimeRelativePath) const;
    UMaterialInterface* ResolveMaterial(FName MaterialSlotId) const;
    UInstancedStaticMeshComponent* GetOrCreateInstanceComponent(
        FName ComponentKey,
        UStaticMesh* Mesh,
        UMaterialInterface* Material);
    void DrawDebugCells(const FAsciiMapGridV2& Grid) const;
    void ResetGenerationStats();
    void LogGenerationSummary(const FAsciiMapGridV2& Grid, FName DominantFloorMaterial) const;

private:
    UPROPERTY(VisibleAnywhere, Category = "ASCII Map V2")
    USceneComponent* SceneRoot = nullptr;

    UPROPERTY(EditAnywhere, Category = "ASCII Map V2|Configuration")
    UMapBuilderV2ConfigDataAsset* BuilderConfig = nullptr;

    UPROPERTY(EditAnywhere, Category = "ASCII Map V2|Configuration")
    UGeneratedMeshRegistryV2DataAsset* MeshRegistryV2 = nullptr;

    UPROPERTY(EditAnywhere, Category = "ASCII Map V2|Materials")
    UMaterialRegistryDataAsset* MaterialRegistryFallback = nullptr;

    UPROPERTY(EditAnywhere, Category = "ASCII Map V2|Materials")
    UMaterialInterface* RuntimeMaterialMaster = nullptr;

    UPROPERTY(EditAnywhere, Category = "ASCII Map V2|RuntimeData")
    bool bMapPackageIndexPathIsAbsolute = false;

    UPROPERTY(EditAnywhere, Category = "ASCII Map V2|RuntimeData")
    FString MapPackageIndexPath = TEXT("VisualOptimization/RuntimeData/map_package_index.json");

    UPROPERTY(EditAnywhere, Category = "ASCII Map V2|RuntimeData", meta = (GetOptions = "GetAvailableMapIdOptions"))
    FName SelectedMapId = NAME_None;

    UPROPERTY(EditAnywhere, Category = "ASCII Map V2|RuntimeData")
    bool bGenerateOnBeginPlay = false;

    UPROPERTY(Transient)
    UAsciiRuntimeMaterialProvider* RuntimeMaterialProvider = nullptr;

    UPROPERTY(Transient)
    TMap<FName, UInstancedStaticMeshComponent*> RuntimeInstanceComponents;

    UPROPERTY(Transient)
    TArray<AActor*> RuntimeSpawnedActors;

    UPROPERTY(VisibleAnywhere, Transient, Category = "ASCII Map V2|Debug")
    TArray<FResolvedMapCellV2> LastResolvedCells;

    UPROPERTY(VisibleAnywhere, Transient, Category = "ASCII Map V2|Debug")
    FString CurrentRuntimeMapId;

    int32 GeneratedBaseSurfaceCount = 0;
    int32 GeneratedUnderlayCount = 0;
    int32 GeneratedStructureCount = 0;
    int32 GeneratedInteractiveActorCount = 0;
    int32 GeneratedDecorationCount = 0;
    int32 GeneratedOverlayCount = 0;
    int32 NonUniformStretchCount = 0;
    int32 AmbiguousOrientationCount = 0;
    TSet<FName> MissingMeshIds;
    TSet<FName> MissingMaterialSlotIds;
    TMap<uint8, int32> WallConnectivityDistribution;
};
