#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Actor.h"
#include "ProjectionExperiment/VOProjectionTypes.h"
#include "VOProjectionCaptureRig.generated.h"

class UPrimitiveComponent;
class UTextureRenderTarget2D;

UCLASS()
class VISUALOPTIMIZATIONUE_API AVOProjectionCaptureRig : public AActor
{
    GENERATED_BODY()

public:
    AVOProjectionCaptureRig();

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Projection Capture")
    void CaptureAllProjectionOutputs();

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Projection Capture")
    void CaptureFullViewOnly();

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Projection Capture")
    void CaptureLayerMasksOnly();

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Projection Capture")
    void ResetDefaultLayers();

protected:
    virtual void OnConstruction(const FTransform& Transform) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projection Capture")
    USceneCaptureComponent2D* SceneCapture = nullptr;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projection Capture")
    UCameraComponent* PreviewCamera = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection Capture")
    FString MapId = TEXT("test_map1");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection Capture", meta = (ClampMin = "64"))
    int32 CaptureResolutionX = 2048;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection Capture", meta = (ClampMin = "64"))
    int32 CaptureResolutionY = 2048;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection Capture")
    bool bUseOrthographic = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection Capture")
    bool bForceSceneCaptureToActorTransform = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection Capture|Camera")
    bool bUsePreviewCameraView = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection Capture|Camera")
    bool bUseLookAtTarget = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection Capture|Camera", meta = (EditCondition = "bUseLookAtTarget"))
    FVector CaptureLookAtTarget = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection Capture")
    bool bDisableMaterialsForGrayboxCapture = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection Capture|Masks")
    bool bUseRealSceneCaptureMasks = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection Capture|Masks")
    bool bClipLayerMasksToFullSceneDepth = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection Capture|Masks", meta = (ClampMin = "0.0"))
    float LayerDepthVisibilityTolerance = 25.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection Capture|Depth Preview", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float DepthPreviewNearPercentile = 0.02f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection Capture|Depth Preview", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float DepthPreviewFarPercentile = 0.95f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection Capture|Depth Preview", meta = (ClampMin = "0.05", ClampMax = "4.0"))
    float DepthPreviewContrastGamma = 0.55f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection Capture|Legacy", meta = (DisplayName = "Use Legacy Projected Bounds Masks"))
    bool bUseProjectedBoundsMasks = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection Capture|Legacy", meta = (DisplayName = "Write Graybox From Legacy Layer Masks"))
    bool bWriteGrayboxFromLayerMasks = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection Capture", meta = (ClampMin = "1.0"))
    float OrthoWidth = 4096.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection Capture", meta = (ClampMin = "1.0", EditCondition = "!bUseOrthographic"))
    float PerspectiveFOV = 60.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection Capture")
    FString OutputSubfolderName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection Capture")
    uint8 MaskForegroundThreshold = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection Layers")
    TArray<FVOProjectionLayerDefinition> ProjectionLayers;

    UPROPERTY(Transient)
    UTextureRenderTarget2D* FullSceneRenderTarget = nullptr;

    UPROPERTY(Transient)
    UTextureRenderTarget2D* LayerRenderTarget = nullptr;

    UPROPERTY(Transient)
    UTextureRenderTarget2D* DepthRenderTarget = nullptr;

    UPROPERTY(Transient)
    UTextureRenderTarget2D* NormalRenderTarget = nullptr;

private:
    struct FLayerCaptureResult
    {
        FName LayerId;
        int32 ComponentCount = 0;
        FString MaskRelativePath;
        FString DebugRelativePath;
    };

    void EnsureDefaultLayers();
    void InitializeCaptureResources();
    UTextureRenderTarget2D* CreateOrUpdateRenderTarget(UTextureRenderTarget2D* ExistingTarget, const FName TargetName, const FLinearColor& ClearColor, ETextureRenderTargetFormat RenderTargetFormat);
    void ApplySceneCaptureTransform();
    void SyncPreviewCameraToSceneCapture();
    void ConfigureCaptureForRenderTarget(UTextureRenderTarget2D* RenderTarget, ESceneCaptureSource CaptureSource, bool bUseShowOnly);
    bool CaptureRenderTargetPixels(UTextureRenderTarget2D* RenderTarget, TArray<FColor>& OutPixels, FString& OutWarning);
    bool CaptureRenderTargetLinearPixels(UTextureRenderTarget2D* RenderTarget, TArray<FLinearColor>& OutPixels, FString& OutWarning);
    bool CaptureSceneDepthValues(bool bUseShowOnly, const TArray<UPrimitiveComponent*>& ShowOnlyComponents, TArray<float>& OutDepthValues, FString& OutWarning);
    bool CaptureLayerMaskSourcePixels(const TArray<UPrimitiveComponent*>& ShowOnlyComponents, TArray<FColor>& OutPixels, FString& OutWarning);

    FString CreateOutputRoot() const;
    bool CaptureFullRgb(const FString& OutputRoot, TArray<FString>& Warnings);
    bool CaptureDepthPreview(const FString& OutputRoot, TArray<FString>& Warnings);
    bool CaptureNormalPreview(const FString& OutputRoot, TArray<FString>& Warnings);
    void CaptureLayerOutputs(const FString& OutputRoot, TArray<FLayerCaptureResult>& OutLayerResults, TArray<FString>& Warnings);
    void SaveMetadata(const FString& OutputRoot, const TArray<FLayerCaptureResult>& LayerResults, const TArray<FString>& Warnings) const;

    TArray<UPrimitiveComponent*> FindMatchingComponentsForLayer(const FVOProjectionLayerDefinition& Layer) const;
    bool DoesComponentMatchLayer(const UPrimitiveComponent* Component, const FVOProjectionLayerDefinition& Layer) const;
    void BuildBinaryMask(const TArray<FColor>& SourcePixels, TArray<FColor>& OutMaskPixels) const;
    int32 CountMaskForegroundPixels(const TArray<FColor>& MaskPixels) const;
    void BuildMaskFromDepthValues(const TArray<float>& DepthValues, TArray<FColor>& OutMaskPixels) const;
    void FilterMaskToVisibleDepth(const TArray<float>& LayerDepthValues, TArray<FColor>& InOutMaskPixels) const;
    bool BuildDepthPreviewPixels(const TArray<float>& DepthValues, TArray<FColor>& OutPixels, FString& OutWarning) const;
    bool ProjectWorldPointToCapturePixel(const FVector& WorldPoint, FVector2D& OutPixel, float& OutDepth) const;
    bool BuildProjectedBoundsMask(const TArray<UPrimitiveComponent*>& Components, TArray<FColor>& OutMaskPixels, int32& OutCoveredPixels) const;
    bool BuildProjectedDepthPreview(TArray<FColor>& OutPixels, TArray<FString>& Warnings) const;
    void WriteGrayboxFromSemantic(const FString& OutputRoot, const TArray<FColor>& SemanticPixels, TArray<FString>& Warnings) const;
    void CompositeSemanticLayer(const TArray<FColor>& MaskPixels, const FLinearColor& SemanticColor, TArray<FColor>& InOutSemanticPixels) const;

    TArray<float> LastFullSceneDepthValues;
};
