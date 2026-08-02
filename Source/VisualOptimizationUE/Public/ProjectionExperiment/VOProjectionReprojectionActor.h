#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProjectionExperiment/VOProjectionTypes.h"
#include "VOProjectionReprojectionActor.generated.h"

class UDecalComponent;
class UMaterialInstanceDynamic;

UCLASS()
class VISUALOPTIMIZATIONUE_API AVOProjectionReprojectionActor : public AActor
{
    GENERATED_BODY()

public:
    AVOProjectionReprojectionActor();

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Projection")
    void ApplyProjectionLayers();

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Projection")
    void ClearProjectionLayers();

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Projection")
    void LoadMetadataFromPath();

protected:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection")
    FString CaptureMetadataPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection")
    bool bProjectionEnabled = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection")
    TArray<FVOProjectionLayerReprojectionInput> LayerInputs;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection")
    float ProjectionDepth = 10000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection")
    float DefaultOrthoWidth = 4096.0f;

    UPROPERTY(Transient)
    TArray<UDecalComponent*> ProjectionComponents;

    UPROPERTY(Transient)
    TArray<UMaterialInstanceDynamic*> ProjectionMaterials;

    UPROPERTY(Transient)
    bool bHasLoadedMetadata = false;

    UPROPERTY(Transient)
    FVector MetadataCameraLocation = FVector::ZeroVector;

    UPROPERTY(Transient)
    FRotator MetadataCameraRotation = FRotator::ZeroRotator;

    UPROPERTY(Transient)
    float MetadataOrthoWidth = 4096.0f;

    UPROPERTY(Transient)
    FIntPoint MetadataResolution = FIntPoint(2048, 2048);

    UPROPERTY(Transient)
    FString MetadataProjectionType;

private:
    FString ResolveMetadataPath() const;
    bool TryReadNumberArray(const TSharedPtr<class FJsonObject>& JsonObject, const FString& FieldName, TArray<double>& OutValues) const;
};
