#include "ProjectionExperiment/VOProjectionCaptureRig.h"

#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "ProjectionExperiment/VOProjectionImageUtils.h"
#include "RenderingThread.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
    constexpr float MaxReasonableSceneDepth = 100000000.0f;

    bool IsUsableSceneDepth(float DepthValue)
    {
        return FMath::IsFinite(DepthValue) && DepthValue > KINDA_SMALL_NUMBER && DepthValue < MaxReasonableSceneDepth;
    }

    float GetSortedPercentile(const TArray<float>& SortedValues, float Percentile)
    {
        if (SortedValues.Num() == 0)
        {
            return 0.0f;
        }

        const float ClampedPercentile = FMath::Clamp(Percentile, 0.0f, 1.0f);
        const int32 Index = FMath::Clamp(FMath::RoundToInt(ClampedPercentile * static_cast<float>(SortedValues.Num() - 1)), 0, SortedValues.Num() - 1);
        return SortedValues[Index];
    }

    bool ComputeDepthForegroundStats(const TArray<float>& DepthValues, float& OutNearDepth, float& OutFarDepth, float& OutForegroundFarCutoff)
    {
        TArray<float> ValidDepths;
        ValidDepths.Reserve(DepthValues.Num());

        for (const float DepthValue : DepthValues)
        {
            if (IsUsableSceneDepth(DepthValue))
            {
                ValidDepths.Add(DepthValue);
            }
        }

        if (ValidDepths.Num() == 0)
        {
            return false;
        }

        ValidDepths.Sort();

        const float MinDepth = ValidDepths[0];
        const float MaxDepth = ValidDepths.Last();
        const float DepthRange = MaxDepth - MinDepth;
        if (DepthRange <= KINDA_SMALL_NUMBER)
        {
            OutNearDepth = MinDepth;
            OutFarDepth = MaxDepth + 1.0f;
            OutForegroundFarCutoff = MaxDepth + 1.0f;
            return true;
        }

        const float BackgroundMargin = FMath::Max(DepthRange * 0.001f, 0.01f);
        OutForegroundFarCutoff = MaxDepth - BackgroundMargin;

        TArray<float> ForegroundDepths;
        ForegroundDepths.Reserve(ValidDepths.Num());
        for (const float DepthValue : ValidDepths)
        {
            if (DepthValue <= OutForegroundFarCutoff)
            {
                ForegroundDepths.Add(DepthValue);
            }
        }

        if (ForegroundDepths.Num() < FMath::Max(16, ValidDepths.Num() / 100))
        {
            ForegroundDepths = ValidDepths;
            OutForegroundFarCutoff = MaxDepth + BackgroundMargin;
        }

        OutNearDepth = GetSortedPercentile(ForegroundDepths, 0.01f);
        OutFarDepth = GetSortedPercentile(ForegroundDepths, 0.99f);

        if (OutFarDepth <= OutNearDepth)
        {
            OutNearDepth = ForegroundDepths[0];
            OutFarDepth = ForegroundDepths.Last();
        }

        if (OutFarDepth <= OutNearDepth)
        {
            OutFarDepth = OutNearDepth + FMath::Max(1.0f, FMath::Abs(OutNearDepth) * 0.001f);
        }

        return true;
    }

    FString NormalizeProjectionMatchString(const FString& InValue)
    {
        FString Result = InValue.ToLower();
        Result.ReplaceInline(TEXT("_"), TEXT(""));
        Result.ReplaceInline(TEXT("-"), TEXT(""));
        Result.ReplaceInline(TEXT(" "), TEXT(""));
        return Result;
    }

    TSharedPtr<FJsonValue> MakeJsonNumberValue(double Value)
    {
        return MakeShared<FJsonValueNumber>(Value);
    }

    TArray<TSharedPtr<FJsonValue>> MakeVectorJsonArray(const FVector& Vector)
    {
        TArray<TSharedPtr<FJsonValue>> Values;
        Values.Add(MakeJsonNumberValue(Vector.X));
        Values.Add(MakeJsonNumberValue(Vector.Y));
        Values.Add(MakeJsonNumberValue(Vector.Z));
        return Values;
    }

    TArray<TSharedPtr<FJsonValue>> MakeRotatorJsonArray(const FRotator& Rotator)
    {
        TArray<TSharedPtr<FJsonValue>> Values;
        Values.Add(MakeJsonNumberValue(Rotator.Pitch));
        Values.Add(MakeJsonNumberValue(Rotator.Yaw));
        Values.Add(MakeJsonNumberValue(Rotator.Roll));
        return Values;
    }

    FVOProjectionLayerDefinition MakeDefaultProjectionLayer(
        const FName LayerId,
        const FLinearColor& SemanticColor,
        const TArray<FString>& NameContains)
    {
        FVOProjectionLayerDefinition Layer;
        Layer.LayerId = LayerId;
        Layer.MatchMaterialSlotIds.Add(LayerId);
        Layer.MatchComponentNameContains = NameContains;
        Layer.SemanticColor = SemanticColor;
        Layer.bEnabled = true;
        return Layer;
    }
}

AVOProjectionCaptureRig::AVOProjectionCaptureRig()
{
    PrimaryActorTick.bCanEverTick = false;

    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

    SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("ProjectionSceneCapture"));
    SceneCapture->SetupAttachment(RootComponent);
    SceneCapture->bCaptureEveryFrame = false;
    SceneCapture->bCaptureOnMovement = false;
    SceneCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;

    PreviewCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ProjectionPreviewCamera"));
    PreviewCamera->SetupAttachment(RootComponent);
    PreviewCamera->bConstrainAspectRatio = true;
    PreviewCamera->SetActive(true);

    EnsureDefaultLayers();
}

void AVOProjectionCaptureRig::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    EnsureDefaultLayers();

    if (SceneCapture)
    {
        ApplySceneCaptureTransform();
        SceneCapture->ProjectionType = bUseOrthographic ? ECameraProjectionMode::Orthographic : ECameraProjectionMode::Perspective;
        SceneCapture->OrthoWidth = OrthoWidth;
        SceneCapture->FOVAngle = PerspectiveFOV;
        SyncPreviewCameraToSceneCapture();
    }
}

void AVOProjectionCaptureRig::CaptureAllProjectionOutputs()
{
    EnsureDefaultLayers();
    InitializeCaptureResources();

    const FString OutputRoot = CreateOutputRoot();
    VOProjectionImageUtils::EnsureDirectory(OutputRoot / TEXT("full"));
    VOProjectionImageUtils::EnsureDirectory(OutputRoot / TEXT("masks"));
    VOProjectionImageUtils::EnsureDirectory(OutputRoot / TEXT("debug"));

    TArray<FString> Warnings;
    TArray<FLayerCaptureResult> LayerResults;

    CaptureFullRgb(OutputRoot, Warnings);
    CaptureDepthPreview(OutputRoot, Warnings);
    CaptureNormalPreview(OutputRoot, Warnings);
    CaptureLayerOutputs(OutputRoot, LayerResults, Warnings);
    SaveMetadata(OutputRoot, LayerResults, Warnings);

    UE_LOG(LogTemp, Display, TEXT("E1: Projection capture complete: %s"), *OutputRoot);
}

void AVOProjectionCaptureRig::CaptureFullViewOnly()
{
    EnsureDefaultLayers();
    InitializeCaptureResources();

    const FString OutputRoot = CreateOutputRoot();
    VOProjectionImageUtils::EnsureDirectory(OutputRoot / TEXT("full"));

    TArray<FString> Warnings;
    CaptureFullRgb(OutputRoot, Warnings);
    CaptureDepthPreview(OutputRoot, Warnings);
    CaptureNormalPreview(OutputRoot, Warnings);
    SaveMetadata(OutputRoot, TArray<FLayerCaptureResult>(), Warnings);

    UE_LOG(LogTemp, Display, TEXT("E1: Full projection view capture complete: %s"), *OutputRoot);
}

void AVOProjectionCaptureRig::CaptureLayerMasksOnly()
{
    EnsureDefaultLayers();
    InitializeCaptureResources();

    const FString OutputRoot = CreateOutputRoot();
    VOProjectionImageUtils::EnsureDirectory(OutputRoot / TEXT("masks"));
    VOProjectionImageUtils::EnsureDirectory(OutputRoot / TEXT("debug"));

    TArray<FString> Warnings;
    TArray<FLayerCaptureResult> LayerResults;
    CaptureLayerOutputs(OutputRoot, LayerResults, Warnings);
    SaveMetadata(OutputRoot, LayerResults, Warnings);

    UE_LOG(LogTemp, Display, TEXT("E1: Projection layer mask capture complete: %s"), *OutputRoot);
}

void AVOProjectionCaptureRig::ResetDefaultLayers()
{
    ProjectionLayers.Empty();
    EnsureDefaultLayers();
}

void AVOProjectionCaptureRig::EnsureDefaultLayers()
{
    if (ProjectionLayers.Num() > 0)
    {
        return;
    }

    ProjectionLayers.Add(MakeDefaultProjectionLayer(TEXT("grass_ground"), FLinearColor(0.1f, 0.8f, 0.15f), { TEXT("grass_ground"), TEXT("GrassInstances"), TEXT("grass") }));
    ProjectionLayers.Add(MakeDefaultProjectionLayer(TEXT("stone_floor"), FLinearColor(0.45f, 0.45f, 0.45f), { TEXT("stone_floor"), TEXT("FloorInstances"), TEXT("stonefloor"), TEXT("floor") }));
    ProjectionLayers.Add(MakeDefaultProjectionLayer(TEXT("stone_wall"), FLinearColor(0.75f, 0.75f, 0.75f), { TEXT("stone_wall"), TEXT("WallInstances"), TEXT("stonewall"), TEXT("wall") }));
    ProjectionLayers.Add(MakeDefaultProjectionLayer(TEXT("water"), FLinearColor(0.05f, 0.25f, 1.0f), { TEXT("water"), TEXT("WaterInstances") }));
    ProjectionLayers.Add(MakeDefaultProjectionLayer(TEXT("wood_planks"), FLinearColor(0.55f, 0.32f, 0.12f), { TEXT("wood_planks"), TEXT("WoodInstances"), TEXT("woodplanks") }));
    ProjectionLayers.Add(MakeDefaultProjectionLayer(TEXT("wooden_door"), FLinearColor(0.35f, 0.08f, 0.04f), { TEXT("wooden_door"), TEXT("DoorInstances"), TEXT("woodendoor"), TEXT("door") }));
}

void AVOProjectionCaptureRig::InitializeCaptureResources()
{
    FullSceneRenderTarget = CreateOrUpdateRenderTarget(FullSceneRenderTarget, TEXT("VOProjectionRT_FullScene"), FLinearColor::Black, RTF_RGBA8);
    LayerRenderTarget = CreateOrUpdateRenderTarget(LayerRenderTarget, TEXT("VOProjectionRT_Layer"), FLinearColor::Black, RTF_RGBA8);
    DepthRenderTarget = CreateOrUpdateRenderTarget(DepthRenderTarget, TEXT("VOProjectionRT_Depth"), FLinearColor::Black, RTF_RGBA16f);
    NormalRenderTarget = CreateOrUpdateRenderTarget(NormalRenderTarget, TEXT("VOProjectionRT_Normal"), FLinearColor::Black, RTF_RGBA8);
}

UTextureRenderTarget2D* AVOProjectionCaptureRig::CreateOrUpdateRenderTarget(UTextureRenderTarget2D* ExistingTarget, const FName TargetName, const FLinearColor& ClearColor, ETextureRenderTargetFormat RenderTargetFormat)
{
    UTextureRenderTarget2D* Target = ExistingTarget;
    if (!Target)
    {
        Target = NewObject<UTextureRenderTarget2D>(this, TargetName);
    }

    Target->RenderTargetFormat = RenderTargetFormat;
    Target->ClearColor = ClearColor;
    Target->bAutoGenerateMips = false;
    Target->InitAutoFormat(CaptureResolutionX, CaptureResolutionY);
    Target->UpdateResourceImmediate(true);

    return Target;
}

void AVOProjectionCaptureRig::ApplySceneCaptureTransform()
{
    if (!SceneCapture)
    {
        return;
    }

    const FVector CaptureLocation = GetActorLocation();
    const FVector LookAtDirection = CaptureLookAtTarget - CaptureLocation;
    if (bUseLookAtTarget && !LookAtDirection.IsNearlyZero())
    {
        SceneCapture->SetWorldLocationAndRotation(CaptureLocation, LookAtDirection.Rotation());
        SyncPreviewCameraToSceneCapture();
        return;
    }

    if (bUsePreviewCameraView && PreviewCamera)
    {
        SceneCapture->SetWorldLocationAndRotation(PreviewCamera->GetComponentLocation(), PreviewCamera->GetComponentRotation());
        SyncPreviewCameraToSceneCapture();
        return;
    }

    if (!bForceSceneCaptureToActorTransform)
    {
        SyncPreviewCameraToSceneCapture();
        return;
    }

    SceneCapture->SetWorldLocationAndRotation(CaptureLocation, GetActorRotation());
    SyncPreviewCameraToSceneCapture();
}

void AVOProjectionCaptureRig::SyncPreviewCameraToSceneCapture()
{
    if (!PreviewCamera || !SceneCapture)
    {
        return;
    }

    PreviewCamera->SetWorldLocationAndRotation(SceneCapture->GetComponentLocation(), SceneCapture->GetComponentRotation());
    PreviewCamera->ProjectionMode = bUseOrthographic ? ECameraProjectionMode::Orthographic : ECameraProjectionMode::Perspective;
    PreviewCamera->OrthoWidth = OrthoWidth;
    PreviewCamera->FieldOfView = PerspectiveFOV;
    PreviewCamera->AspectRatio = CaptureResolutionY > 0
        ? static_cast<float>(CaptureResolutionX) / static_cast<float>(CaptureResolutionY)
        : 1.0f;
}

void AVOProjectionCaptureRig::ConfigureCaptureForRenderTarget(UTextureRenderTarget2D* RenderTarget, ESceneCaptureSource CaptureSource, bool bUseShowOnly)
{
    if (!SceneCapture || !RenderTarget)
    {
        return;
    }

    ApplySceneCaptureTransform();
    SceneCapture->TextureTarget = RenderTarget;
    SceneCapture->CaptureSource = CaptureSource;
    SceneCapture->ProjectionType = bUseOrthographic ? ECameraProjectionMode::Orthographic : ECameraProjectionMode::Perspective;
    SceneCapture->OrthoWidth = OrthoWidth;
    SceneCapture->FOVAngle = PerspectiveFOV;
    SyncPreviewCameraToSceneCapture();
    SceneCapture->PrimitiveRenderMode = bUseShowOnly
        ? ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList
        : ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
    SceneCapture->ShowFlags.SetFog(true);
    SceneCapture->ShowFlags.SetAtmosphere(true);
    SceneCapture->ShowFlags.SetSkyLighting(true);

    if (!bUseShowOnly)
    {
        SceneCapture->ClearShowOnlyComponents();
    }
}

bool AVOProjectionCaptureRig::CaptureRenderTargetPixels(UTextureRenderTarget2D* RenderTarget, TArray<FColor>& OutPixels, FString& OutWarning)
{
    OutWarning.Empty();

    if (!SceneCapture || !RenderTarget)
    {
        OutWarning = TEXT("SceneCapture or render target is missing.");
        return false;
    }

    OutPixels.Empty();
    SceneCapture->CaptureScene();
    FlushRenderingCommands();

    FTextureRenderTargetResource* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();
    if (!RenderTargetResource)
    {
        OutWarning = TEXT("Render target resource is unavailable.");
        return false;
    }

    FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
    ReadFlags.SetLinearToGamma(true);
    if (!RenderTargetResource->ReadPixels(OutPixels, ReadFlags))
    {
        OutWarning = TEXT("ReadPixels failed.");
        return false;
    }

    return OutPixels.Num() == CaptureResolutionX * CaptureResolutionY;
}

bool AVOProjectionCaptureRig::CaptureRenderTargetLinearPixels(UTextureRenderTarget2D* RenderTarget, TArray<FLinearColor>& OutPixels, FString& OutWarning)
{
    OutWarning.Empty();

    if (!SceneCapture || !RenderTarget)
    {
        OutWarning = TEXT("SceneCapture or render target is missing.");
        return false;
    }

    OutPixels.Empty();
    SceneCapture->CaptureScene();
    FlushRenderingCommands();

    FTextureRenderTargetResource* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();
    if (!RenderTargetResource)
    {
        OutWarning = TEXT("Render target resource is unavailable.");
        return false;
    }

    FReadSurfaceDataFlags ReadFlags(RCM_MinMax);
    ReadFlags.SetLinearToGamma(false);
    if (!RenderTargetResource->ReadLinearColorPixels(OutPixels, ReadFlags))
    {
        OutWarning = TEXT("ReadLinearColorPixels failed.");
        return false;
    }

    return OutPixels.Num() == CaptureResolutionX * CaptureResolutionY;
}

bool AVOProjectionCaptureRig::CaptureSceneDepthValues(bool bUseShowOnly, const TArray<UPrimitiveComponent*>& ShowOnlyComponents, TArray<float>& OutDepthValues, FString& OutWarning)
{
    OutWarning.Empty();
    OutDepthValues.Empty();

    ConfigureCaptureForRenderTarget(DepthRenderTarget, SCS_SceneDepth, bUseShowOnly);
    if (!SceneCapture || !DepthRenderTarget)
    {
        OutWarning = TEXT("SceneCapture or depth render target is missing.");
        return false;
    }

    SceneCapture->ShowFlags.SetMaterials(false);
    SceneCapture->ShowFlags.SetLighting(false);
    SceneCapture->ShowFlags.SetPostProcessing(false);
    SceneCapture->ShowFlags.SetFog(false);
    SceneCapture->ShowFlags.SetAtmosphere(false);
    SceneCapture->ShowFlags.SetSkyLighting(false);

    if (bUseShowOnly)
    {
        SceneCapture->ClearShowOnlyComponents();
        for (UPrimitiveComponent* Component : ShowOnlyComponents)
        {
            if (Component)
            {
                SceneCapture->ShowOnlyComponent(Component);
            }
        }
    }

    TArray<FLinearColor> LinearPixels;
    if (!CaptureRenderTargetLinearPixels(DepthRenderTarget, LinearPixels, OutWarning))
    {
        return false;
    }

    OutDepthValues.SetNumUninitialized(LinearPixels.Num());
    for (int32 PixelIndex = 0; PixelIndex < LinearPixels.Num(); ++PixelIndex)
    {
        OutDepthValues[PixelIndex] = LinearPixels[PixelIndex].R;
    }

    return true;
}

bool AVOProjectionCaptureRig::CaptureLayerMaskSourcePixels(const TArray<UPrimitiveComponent*>& ShowOnlyComponents, TArray<FColor>& OutPixels, FString& OutWarning)
{
    OutWarning.Empty();
    OutPixels.Empty();

    if (ShowOnlyComponents.Num() == 0)
    {
        OutWarning = TEXT("No components were supplied for layer mask source capture.");
        return false;
    }

    ConfigureCaptureForRenderTarget(LayerRenderTarget, SCS_BaseColor, true);
    if (!SceneCapture || !LayerRenderTarget)
    {
        OutWarning = TEXT("SceneCapture or layer render target is missing.");
        return false;
    }

    SceneCapture->ClearShowOnlyComponents();
    SceneCapture->ShowFlags.SetMaterials(true);
    SceneCapture->ShowFlags.SetLighting(false);
    SceneCapture->ShowFlags.SetPostProcessing(false);
    SceneCapture->ShowFlags.SetFog(false);
    SceneCapture->ShowFlags.SetAtmosphere(false);
    SceneCapture->ShowFlags.SetSkyLighting(false);

    for (UPrimitiveComponent* Component : ShowOnlyComponents)
    {
        if (Component)
        {
            SceneCapture->ShowOnlyComponent(Component);
        }
    }

    return CaptureRenderTargetPixels(LayerRenderTarget, OutPixels, OutWarning);
}

FString AVOProjectionCaptureRig::CreateOutputRoot() const
{
    const FString FolderName = OutputSubfolderName.IsEmpty()
        ? FString::Printf(TEXT("%s_%s_fixedview"),
            *VOProjectionImageUtils::MakeTimestampString(),
            *VOProjectionImageUtils::SanitizeFileToken(MapId))
        : VOProjectionImageUtils::SanitizeFileToken(OutputSubfolderName);

    return FPaths::ProjectSavedDir() / TEXT("VisualOptimization/ProjectionCaptures") / FolderName;
}

bool AVOProjectionCaptureRig::CaptureFullRgb(const FString& OutputRoot, TArray<FString>& Warnings)
{
    ConfigureCaptureForRenderTarget(FullSceneRenderTarget, SCS_FinalColorLDR, false);
    SceneCapture->ShowFlags.SetMaterials(true);
    SceneCapture->ShowFlags.SetLighting(true);
    SceneCapture->ShowFlags.SetPostProcessing(true);

    TArray<FColor> Pixels;
    FString Warning;
    if (!CaptureRenderTargetPixels(FullSceneRenderTarget, Pixels, Warning))
    {
        Warnings.Add(FString::Printf(TEXT("Full RGB capture failed: %s"), *Warning));
        return false;
    }

    FString Error;
    const FString FullRgbPath = OutputRoot / TEXT("full/full_rgb.png");
    if (!VOProjectionImageUtils::SaveColorArrayAsPng(FullRgbPath, Pixels, CaptureResolutionX, CaptureResolutionY, Error))
    {
        Warnings.Add(Error);
        return false;
    }

    TArray<FColor> GrayboxPixels = Pixels;
    if (bDisableMaterialsForGrayboxCapture)
    {
        SceneCapture->ShowFlags.SetMaterials(false);
        SceneCapture->ShowFlags.SetLighting(false);
        SceneCapture->ShowFlags.SetPostProcessing(false);

        FString GrayboxWarning;
        if (!CaptureRenderTargetPixels(FullSceneRenderTarget, GrayboxPixels, GrayboxWarning))
        {
            Warnings.Add(FString::Printf(TEXT("Graybox capture failed and reused full scene RGB: %s"), *GrayboxWarning));
            GrayboxPixels = Pixels;
        }

        SceneCapture->ShowFlags.SetMaterials(true);
        SceneCapture->ShowFlags.SetLighting(true);
        SceneCapture->ShowFlags.SetPostProcessing(true);
    }
    else
    {
        Warnings.Add(TEXT("full_graybox_or_scene.png reused full_rgb.png because bDisableMaterialsForGrayboxCapture is false."));
    }

    const FString GrayboxPath = OutputRoot / TEXT("full/full_graybox_or_scene.png");
    if (!VOProjectionImageUtils::SaveColorArrayAsPng(GrayboxPath, GrayboxPixels, CaptureResolutionX, CaptureResolutionY, Error))
    {
        Warnings.Add(Error);
    }

    return true;
}

bool AVOProjectionCaptureRig::CaptureDepthPreview(const FString& OutputRoot, TArray<FString>& Warnings)
{
    TArray<float> DepthValues;
    FString DepthWarning;
    if (!CaptureSceneDepthValues(false, TArray<UPrimitiveComponent*>(), DepthValues, DepthWarning))
    {
        Warnings.Add(FString::Printf(TEXT("Depth preview capture failed: %s"), *DepthWarning));
        DepthValues.Empty();
    }

    LastFullSceneDepthValues = DepthValues;

    TArray<FColor> Pixels;
    FString PreviewWarning;
    if (!BuildDepthPreviewPixels(DepthValues, Pixels, PreviewWarning))
    {
        Warnings.Add(PreviewWarning);
        Pixels.Init(FColor::Black, CaptureResolutionX * CaptureResolutionY);
    }

    FString Error;
    if (!VOProjectionImageUtils::SaveColorArrayAsPng(OutputRoot / TEXT("full/full_depth_preview.png"), Pixels, CaptureResolutionX, CaptureResolutionY, Error))
    {
        Warnings.Add(Error);
        return false;
    }

    Warnings.Add(TEXT("full_depth_preview.png is normalized from the real SceneCapture SceneDepth buffer for preview; use metadata camera values for reprojection."));
    return true;
}

bool AVOProjectionCaptureRig::CaptureNormalPreview(const FString& OutputRoot, TArray<FString>& Warnings)
{
    ConfigureCaptureForRenderTarget(NormalRenderTarget, SCS_Normal, false);

    TArray<FColor> Pixels;
    FString Warning;
    if (!CaptureRenderTargetPixels(NormalRenderTarget, Pixels, Warning))
    {
        Warnings.Add(FString::Printf(TEXT("Normal capture failed: %s"), *Warning));
        Pixels.Init(FColor(128, 128, 255, 255), CaptureResolutionX * CaptureResolutionY);
    }
    else
    {
        Warnings.Add(TEXT("full_normal.png is captured from SceneCapture normal source; verify orientation before using it for downstream conditioning."));
    }

    FString Error;
    if (!VOProjectionImageUtils::SaveColorArrayAsPng(OutputRoot / TEXT("full/full_normal.png"), Pixels, CaptureResolutionX, CaptureResolutionY, Error))
    {
        Warnings.Add(Error);
        return false;
    }

    return true;
}

void AVOProjectionCaptureRig::CaptureLayerOutputs(const FString& OutputRoot, TArray<FLayerCaptureResult>& OutLayerResults, TArray<FString>& Warnings)
{
    TArray<FColor> SemanticPixels;
    SemanticPixels.Init(FColor::Black, CaptureResolutionX * CaptureResolutionY);

    if (bUseRealSceneCaptureMasks && bClipLayerMasksToFullSceneDepth && LastFullSceneDepthValues.Num() != CaptureResolutionX * CaptureResolutionY)
    {
        FString FullDepthWarning;
        if (!CaptureSceneDepthValues(false, TArray<UPrimitiveComponent*>(), LastFullSceneDepthValues, FullDepthWarning))
        {
            Warnings.Add(FString::Printf(TEXT("Full scene depth for visible layer masks failed: %s"), *FullDepthWarning));
            LastFullSceneDepthValues.Empty();
        }
    }

    for (const FVOProjectionLayerDefinition& Layer : ProjectionLayers)
    {
        if (!Layer.bEnabled || Layer.LayerId.IsNone())
        {
            continue;
        }

        const FString LayerFileToken = VOProjectionImageUtils::SanitizeFileToken(Layer.LayerId.ToString());
        const TArray<UPrimitiveComponent*> MatchingComponents = FindMatchingComponentsForLayer(Layer);

        TArray<FColor> LayerPixels;
        if (MatchingComponents.Num() > 0)
        {
            ConfigureCaptureForRenderTarget(LayerRenderTarget, SCS_FinalColorLDR, true);
            SceneCapture->ClearShowOnlyComponents();
            SceneCapture->ShowFlags.SetMaterials(true);
            SceneCapture->ShowFlags.SetLighting(true);
            SceneCapture->ShowFlags.SetPostProcessing(true);
            for (UPrimitiveComponent* Component : MatchingComponents)
            {
                SceneCapture->ShowOnlyComponent(Component);
            }

            FString Warning;
            if (!CaptureRenderTargetPixels(LayerRenderTarget, LayerPixels, Warning))
            {
                Warnings.Add(FString::Printf(TEXT("Layer '%s' isolated capture failed: %s"), *Layer.LayerId.ToString(), *Warning));
                LayerPixels.Init(FColor::Black, CaptureResolutionX * CaptureResolutionY);
            }
        }
        else
        {
            Warnings.Add(FString::Printf(TEXT("Layer '%s' matched no primitive components."), *Layer.LayerId.ToString()));
            LayerPixels.Init(FColor::Black, CaptureResolutionX * CaptureResolutionY);
        }

        TArray<FColor> MaskPixels;
        bool bBuiltMask = false;

        if (bUseRealSceneCaptureMasks && MatchingComponents.Num() > 0)
        {
            TArray<FColor> MaskSourcePixels;
            FString MaskSourceWarning;
            if (CaptureLayerMaskSourcePixels(MatchingComponents, MaskSourcePixels, MaskSourceWarning))
            {
                BuildBinaryMask(MaskSourcePixels, MaskPixels);

                TArray<float> LayerDepthValues;
                FString LayerDepthWarning;
                if (bClipLayerMasksToFullSceneDepth)
                {
                    if (CaptureSceneDepthValues(true, MatchingComponents, LayerDepthValues, LayerDepthWarning))
                    {
                        FilterMaskToVisibleDepth(LayerDepthValues, MaskPixels);
                    }
                    else
                    {
                        Warnings.Add(FString::Printf(TEXT("Layer '%s' visible-depth clip skipped: %s"), *Layer.LayerId.ToString(), *LayerDepthWarning));
                    }
                }

                if (CountMaskForegroundPixels(MaskPixels) == 0)
                {
                    Warnings.Add(FString::Printf(TEXT("Layer '%s' generated an empty real SceneCapture mask from the current camera view."), *Layer.LayerId.ToString()));
                }

                bBuiltMask = true;
            }
            else
            {
                Warnings.Add(FString::Printf(TEXT("Layer '%s' real SceneCapture mask source failed: %s"), *Layer.LayerId.ToString(), *MaskSourceWarning));
            }
        }

        if (!bBuiltMask && bUseProjectedBoundsMasks)
        {
            int32 ProjectedCoveredPixels = 0;
            bBuiltMask = BuildProjectedBoundsMask(MatchingComponents, MaskPixels, ProjectedCoveredPixels);
            if (bBuiltMask)
            {
                Warnings.Add(FString::Printf(TEXT("Layer '%s' used LEGACY projected-bounds mask fallback."), *Layer.LayerId.ToString()));
            }
        }

        if (!bBuiltMask)
        {
            BuildBinaryMask(LayerPixels, MaskPixels);
            if (MatchingComponents.Num() > 0)
            {
                Warnings.Add(FString::Printf(TEXT("Layer '%s' fell back to isolated RGB threshold mask."), *Layer.LayerId.ToString()));
            }
        }

        CompositeSemanticLayer(MaskPixels, Layer.SemanticColor, SemanticPixels);

        const FString MaskRelativePath = FString::Printf(TEXT("masks/mask_%s.png"), *LayerFileToken);
        const FString DebugRelativePath = FString::Printf(TEXT("debug/layer_%s_isolated.png"), *LayerFileToken);

        FString Error;
        if (!VOProjectionImageUtils::SaveColorArrayAsPng(OutputRoot / MaskRelativePath, MaskPixels, CaptureResolutionX, CaptureResolutionY, Error))
        {
            Warnings.Add(Error);
        }

        if (!VOProjectionImageUtils::SaveColorArrayAsPng(OutputRoot / DebugRelativePath, LayerPixels, CaptureResolutionX, CaptureResolutionY, Error))
        {
            Warnings.Add(Error);
        }

        FLayerCaptureResult Result;
        Result.LayerId = Layer.LayerId;
        Result.ComponentCount = MatchingComponents.Num();
        Result.MaskRelativePath = MaskRelativePath;
        Result.DebugRelativePath = DebugRelativePath;
        OutLayerResults.Add(Result);
    }

    SceneCapture->ClearShowOnlyComponents();
    SceneCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;

    FString Error;
    if (!VOProjectionImageUtils::SaveColorArrayAsPng(OutputRoot / TEXT("full/full_semantic_color.png"), SemanticPixels, CaptureResolutionX, CaptureResolutionY, Error))
    {
        Warnings.Add(Error);
    }

    if (bWriteGrayboxFromLayerMasks && !bUseRealSceneCaptureMasks)
    {
        WriteGrayboxFromSemantic(OutputRoot, SemanticPixels, Warnings);
    }
}

void AVOProjectionCaptureRig::SaveMetadata(const FString& OutputRoot, const TArray<FLayerCaptureResult>& LayerResults, const TArray<FString>& Warnings) const
{
    TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
    RootObject->SetStringField(TEXT("schema_version"), TEXT("vo_projection_capture_metadata_v1"));
    RootObject->SetStringField(TEXT("map_id"), MapId);
    RootObject->SetStringField(TEXT("capture_time"), FDateTime::Now().ToIso8601());

    TArray<TSharedPtr<FJsonValue>> ResolutionValues;
    ResolutionValues.Add(MakeJsonNumberValue(CaptureResolutionX));
    ResolutionValues.Add(MakeJsonNumberValue(CaptureResolutionY));
    RootObject->SetArrayField(TEXT("resolution"), ResolutionValues);

    const FVector ActualCaptureLocation = SceneCapture ? SceneCapture->GetComponentLocation() : GetActorLocation();
    const FRotator ActualCaptureRotation = SceneCapture ? SceneCapture->GetComponentRotation() : GetActorRotation();

    RootObject->SetStringField(TEXT("projection_type"), bUseOrthographic ? TEXT("orthographic") : TEXT("perspective"));
    RootObject->SetArrayField(TEXT("camera_location"), MakeVectorJsonArray(ActualCaptureLocation));
    RootObject->SetArrayField(TEXT("camera_rotation"), MakeRotatorJsonArray(ActualCaptureRotation));
    RootObject->SetArrayField(TEXT("actor_location"), MakeVectorJsonArray(GetActorLocation()));
    RootObject->SetArrayField(TEXT("actor_rotation"), MakeRotatorJsonArray(GetActorRotation()));
    RootObject->SetArrayField(TEXT("scene_capture_location"), MakeVectorJsonArray(ActualCaptureLocation));
    RootObject->SetArrayField(TEXT("scene_capture_rotation"), MakeRotatorJsonArray(ActualCaptureRotation));
    RootObject->SetBoolField(TEXT("use_preview_camera_view"), bUsePreviewCameraView);
    RootObject->SetBoolField(TEXT("use_look_at_target"), bUseLookAtTarget);
    RootObject->SetArrayField(TEXT("capture_look_at_target"), MakeVectorJsonArray(CaptureLookAtTarget));
    const FString MaskGenerationMode = bUseRealSceneCaptureMasks
        ? (bClipLayerMasksToFullSceneDepth
            ? TEXT("real_scene_capture_basecolor_with_depth_visibility_clip")
            : TEXT("real_scene_capture_basecolor"))
        : (bUseProjectedBoundsMasks ? TEXT("legacy_projected_bounds") : TEXT("isolated_rgb_threshold"));
    RootObject->SetStringField(TEXT("mask_generation_mode"), MaskGenerationMode);
    RootObject->SetBoolField(TEXT("clip_layer_masks_to_full_scene_depth"), bClipLayerMasksToFullSceneDepth);
    RootObject->SetNumberField(TEXT("layer_depth_visibility_tolerance"), LayerDepthVisibilityTolerance);
    RootObject->SetNumberField(TEXT("depth_preview_near_percentile"), DepthPreviewNearPercentile);
    RootObject->SetNumberField(TEXT("depth_preview_far_percentile"), DepthPreviewFarPercentile);
    RootObject->SetNumberField(TEXT("depth_preview_contrast_gamma"), DepthPreviewContrastGamma);
    RootObject->SetStringField(TEXT("depth_generation_mode"), TEXT("real_scene_capture_scene_depth"));
    RootObject->SetStringField(TEXT("graybox_generation_mode"), bDisableMaterialsForGrayboxCapture ? TEXT("real_scene_capture_materials_disabled") : TEXT("real_scene_capture_final_color_copy"));
    RootObject->SetNumberField(TEXT("ortho_width"), OrthoWidth);
    RootObject->SetNumberField(TEXT("perspective_fov"), PerspectiveFOV);
    RootObject->SetStringField(TEXT("output_root"), OutputRoot);

    TSharedRef<FJsonObject> FilesObject = MakeShared<FJsonObject>();
    FilesObject->SetStringField(TEXT("full_rgb"), TEXT("full/full_rgb.png"));
    FilesObject->SetStringField(TEXT("full_graybox_or_scene"), TEXT("full/full_graybox_or_scene.png"));
    FilesObject->SetStringField(TEXT("full_semantic_color"), TEXT("full/full_semantic_color.png"));
    FilesObject->SetStringField(TEXT("full_depth_preview"), TEXT("full/full_depth_preview.png"));
    FilesObject->SetStringField(TEXT("full_normal"), TEXT("full/full_normal.png"));
    RootObject->SetObjectField(TEXT("files"), FilesObject);

    TArray<TSharedPtr<FJsonValue>> LayerValues;
    for (const FLayerCaptureResult& LayerResult : LayerResults)
    {
        TSharedRef<FJsonObject> LayerObject = MakeShared<FJsonObject>();
        LayerObject->SetStringField(TEXT("layer_id"), LayerResult.LayerId.ToString());
        LayerObject->SetNumberField(TEXT("component_count"), LayerResult.ComponentCount);
        LayerObject->SetStringField(TEXT("mask"), LayerResult.MaskRelativePath);
        LayerObject->SetStringField(TEXT("debug_rgb"), LayerResult.DebugRelativePath);
        LayerValues.Add(MakeShared<FJsonValueObject>(LayerObject));
    }
    RootObject->SetArrayField(TEXT("layers"), LayerValues);

    TArray<TSharedPtr<FJsonValue>> WarningValues;
    for (const FString& Warning : Warnings)
    {
        WarningValues.Add(MakeShared<FJsonValueString>(Warning));
    }
    RootObject->SetArrayField(TEXT("warnings"), WarningValues);

    FString JsonString;
    const TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&JsonString);
    FJsonSerializer::Serialize(RootObject, JsonWriter);

    FString Error;
    if (!VOProjectionImageUtils::SaveStringToFile(OutputRoot / TEXT("metadata.json"), JsonString, Error))
    {
        UE_LOG(LogTemp, Warning, TEXT("E1: Failed to save projection metadata: %s"), *Error);
    }
}

TArray<UPrimitiveComponent*> AVOProjectionCaptureRig::FindMatchingComponentsForLayer(const FVOProjectionLayerDefinition& Layer) const
{
    TArray<UPrimitiveComponent*> MatchingComponents;

    UWorld* World = GetWorld();
    if (!World)
    {
        return MatchingComponents;
    }

    for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
    {
        AActor* Actor = *ActorIt;
        if (!Actor || Actor == this)
        {
            continue;
        }

        TArray<UPrimitiveComponent*> PrimitiveComponents;
        Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
        for (UPrimitiveComponent* Component : PrimitiveComponents)
        {
            if (Component && Component->IsRegistered() && Component->IsVisible() && DoesComponentMatchLayer(Component, Layer))
            {
                MatchingComponents.Add(Component);
            }
        }
    }

    return MatchingComponents;
}

bool AVOProjectionCaptureRig::DoesComponentMatchLayer(const UPrimitiveComponent* Component, const FVOProjectionLayerDefinition& Layer) const
{
    if (!Component)
    {
        return false;
    }

    const AActor* OwningActor = Component->GetOwner();

    for (const FName& Tag : Layer.MatchComponentTags)
    {
        if (!Tag.IsNone() && (Component->ComponentHasTag(Tag) || (OwningActor && OwningActor->ActorHasTag(Tag))))
        {
            return true;
        }
    }

    TArray<FString> Haystacks;
    Haystacks.Add(Component->GetName());
    if (OwningActor)
    {
        Haystacks.Add(OwningActor->GetName());
    }

    for (int32 MaterialIndex = 0; MaterialIndex < Component->GetNumMaterials(); ++MaterialIndex)
    {
        if (UMaterialInterface* Material = Component->GetMaterial(MaterialIndex))
        {
            Haystacks.Add(Material->GetName());
        }
    }

    TArray<FString> NormalizedHaystacks;
    for (const FString& Haystack : Haystacks)
    {
        NormalizedHaystacks.Add(NormalizeProjectionMatchString(Haystack));
    }

    for (const FString& Needle : Layer.MatchComponentNameContains)
    {
        const FString NormalizedNeedle = NormalizeProjectionMatchString(Needle);
        if (NormalizedNeedle.IsEmpty())
        {
            continue;
        }

        for (const FString& Haystack : NormalizedHaystacks)
        {
            if (Haystack.Contains(NormalizedNeedle))
            {
                return true;
            }
        }
    }

    for (const FName& MaterialSlotId : Layer.MatchMaterialSlotIds)
    {
        const FString NormalizedSlot = NormalizeProjectionMatchString(MaterialSlotId.ToString());
        if (NormalizedSlot.IsEmpty())
        {
            continue;
        }

        for (const FString& Haystack : NormalizedHaystacks)
        {
            if (Haystack.Contains(NormalizedSlot))
            {
                return true;
            }
        }
    }

    return false;
}

void AVOProjectionCaptureRig::BuildBinaryMask(const TArray<FColor>& SourcePixels, TArray<FColor>& OutMaskPixels) const
{
    OutMaskPixels.Reset(SourcePixels.Num());

    for (const FColor& Pixel : SourcePixels)
    {
        const bool bForeground =
            Pixel.R > MaskForegroundThreshold ||
            Pixel.G > MaskForegroundThreshold ||
            Pixel.B > MaskForegroundThreshold;

        OutMaskPixels.Add(bForeground ? FColor::White : FColor::Black);
    }
}

int32 AVOProjectionCaptureRig::CountMaskForegroundPixels(const TArray<FColor>& MaskPixels) const
{
    int32 ForegroundPixels = 0;
    for (const FColor& Pixel : MaskPixels)
    {
        if (Pixel.R > 0 || Pixel.G > 0 || Pixel.B > 0)
        {
            ++ForegroundPixels;
        }
    }

    return ForegroundPixels;
}

void AVOProjectionCaptureRig::BuildMaskFromDepthValues(const TArray<float>& DepthValues, TArray<FColor>& OutMaskPixels) const
{
    OutMaskPixels.Reset(DepthValues.Num());

    float NearDepth = 0.0f;
    float FarDepth = 0.0f;
    float ForegroundFarCutoff = 0.0f;
    if (!ComputeDepthForegroundStats(DepthValues, NearDepth, FarDepth, ForegroundFarCutoff))
    {
        OutMaskPixels.Init(FColor::Black, DepthValues.Num());
        return;
    }

    for (const float DepthValue : DepthValues)
    {
        const bool bForeground = IsUsableSceneDepth(DepthValue) && DepthValue <= ForegroundFarCutoff;
        OutMaskPixels.Add(bForeground ? FColor::White : FColor::Black);
    }
}

void AVOProjectionCaptureRig::FilterMaskToVisibleDepth(const TArray<float>& LayerDepthValues, TArray<FColor>& InOutMaskPixels) const
{
    if (!bClipLayerMasksToFullSceneDepth ||
        LastFullSceneDepthValues.Num() != LayerDepthValues.Num() ||
        InOutMaskPixels.Num() != LayerDepthValues.Num())
    {
        return;
    }

    for (int32 PixelIndex = 0; PixelIndex < InOutMaskPixels.Num(); ++PixelIndex)
    {
        if (InOutMaskPixels[PixelIndex].R == 0)
        {
            continue;
        }

        const float LayerDepth = LayerDepthValues[PixelIndex];
        const float FullDepth = LastFullSceneDepthValues[PixelIndex];
        const bool bVisible =
            IsUsableSceneDepth(LayerDepth) &&
            IsUsableSceneDepth(FullDepth) &&
            FMath::Abs(LayerDepth - FullDepth) <= LayerDepthVisibilityTolerance;

        if (!bVisible)
        {
            InOutMaskPixels[PixelIndex] = FColor::Black;
        }
    }
}

bool AVOProjectionCaptureRig::BuildDepthPreviewPixels(const TArray<float>& DepthValues, TArray<FColor>& OutPixels, FString& OutWarning) const
{
    OutWarning.Empty();
    OutPixels.Init(FColor::Black, CaptureResolutionX * CaptureResolutionY);

    if (DepthValues.Num() != CaptureResolutionX * CaptureResolutionY)
    {
        OutWarning = TEXT("Depth preview skipped because the captured depth buffer has an unexpected size.");
        return false;
    }

    float MinDepth = 0.0f;
    float MaxDepth = 0.0f;
    float ForegroundFarCutoff = 0.0f;
    if (!ComputeDepthForegroundStats(DepthValues, MinDepth, MaxDepth, ForegroundFarCutoff))
    {
        OutWarning = TEXT("Depth preview skipped because no valid SceneDepth range was captured.");
        return false;
    }

    TArray<float> PreviewDepths;
    PreviewDepths.Reserve(DepthValues.Num());
    for (const float DepthValue : DepthValues)
    {
        if (IsUsableSceneDepth(DepthValue) && DepthValue <= ForegroundFarCutoff)
        {
            PreviewDepths.Add(DepthValue);
        }
    }

    if (PreviewDepths.Num() == 0)
    {
        OutWarning = TEXT("Depth preview skipped because no foreground SceneDepth pixels were captured.");
        return false;
    }

    PreviewDepths.Sort();

    const float NearPercentile = FMath::Min(DepthPreviewNearPercentile, DepthPreviewFarPercentile);
    const float FarPercentile = FMath::Max(DepthPreviewNearPercentile, DepthPreviewFarPercentile);
    MinDepth = GetSortedPercentile(PreviewDepths, NearPercentile);
    MaxDepth = GetSortedPercentile(PreviewDepths, FarPercentile);

    if (MaxDepth <= MinDepth)
    {
        MinDepth = PreviewDepths[0];
        MaxDepth = PreviewDepths.Last();
    }

    if (MaxDepth <= MinDepth)
    {
        MaxDepth = MinDepth + FMath::Max(1.0f, FMath::Abs(MinDepth) * 0.001f);
    }

    const float DepthRange = FMath::Max(MaxDepth - MinDepth, KINDA_SMALL_NUMBER);
    const float ContrastGamma = FMath::Max(DepthPreviewContrastGamma, 0.05f);
    for (int32 PixelIndex = 0; PixelIndex < DepthValues.Num(); ++PixelIndex)
    {
        const float DepthValue = DepthValues[PixelIndex];
        if (!IsUsableSceneDepth(DepthValue) || DepthValue > ForegroundFarCutoff)
        {
            OutPixels[PixelIndex] = FColor::Black;
            continue;
        }

        const float NormalizedDepth = FMath::Clamp((DepthValue - MinDepth) / DepthRange, 0.0f, 1.0f);
        const float ContrastDepth = FMath::Pow(NormalizedDepth, ContrastGamma);
        const uint8 Shade = static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(250.0f, 25.0f, ContrastDepth)));
        OutPixels[PixelIndex] = FColor(Shade, Shade, Shade, 255);
    }

    return true;
}

bool AVOProjectionCaptureRig::ProjectWorldPointToCapturePixel(const FVector& WorldPoint, FVector2D& OutPixel, float& OutDepth) const
{
    if (CaptureResolutionX <= 0 || CaptureResolutionY <= 0)
    {
        return false;
    }

    const FTransform CaptureTransform = SceneCapture ? SceneCapture->GetComponentTransform() : GetActorTransform();
    const FVector LocalPoint = CaptureTransform.InverseTransformPosition(WorldPoint);
    OutDepth = LocalPoint.X;

    float NormalizedX = 0.0f;
    float NormalizedY = 0.0f;

    if (bUseOrthographic)
    {
        const float SafeOrthoWidth = FMath::Max(OrthoWidth, 1.0f);
        const float OrthoHeight = SafeOrthoWidth * (static_cast<float>(CaptureResolutionY) / static_cast<float>(CaptureResolutionX));
        NormalizedX = 0.5f + (LocalPoint.Y / SafeOrthoWidth);
        NormalizedY = 0.5f - (LocalPoint.Z / OrthoHeight);
    }
    else
    {
        if (LocalPoint.X <= KINDA_SMALL_NUMBER)
        {
            return false;
        }

        const float ClampedFov = FMath::Clamp(PerspectiveFOV, 1.0f, 170.0f);
        const float HalfFovRadians = FMath::DegreesToRadians(ClampedFov * 0.5f);
        const float HalfWidth = FMath::Tan(HalfFovRadians) * LocalPoint.X;
        const float AspectRatio = static_cast<float>(CaptureResolutionX) / static_cast<float>(CaptureResolutionY);
        const float HalfHeight = HalfWidth / FMath::Max(AspectRatio, KINDA_SMALL_NUMBER);

        if (HalfWidth <= KINDA_SMALL_NUMBER || HalfHeight <= KINDA_SMALL_NUMBER)
        {
            return false;
        }

        NormalizedX = 0.5f + (LocalPoint.Y / (2.0f * HalfWidth));
        NormalizedY = 0.5f - (LocalPoint.Z / (2.0f * HalfHeight));
    }

    OutPixel = FVector2D(NormalizedX * CaptureResolutionX, NormalizedY * CaptureResolutionY);
    return FMath::IsFinite(OutPixel.X) && FMath::IsFinite(OutPixel.Y) && FMath::IsFinite(OutDepth);
}

bool AVOProjectionCaptureRig::BuildProjectedBoundsMask(const TArray<UPrimitiveComponent*>& Components, TArray<FColor>& OutMaskPixels, int32& OutCoveredPixels) const
{
    OutMaskPixels.Init(FColor::Black, CaptureResolutionX * CaptureResolutionY);
    OutCoveredPixels = 0;

    if (CaptureResolutionX <= 0 || CaptureResolutionY <= 0 || Components.Num() == 0)
    {
        return false;
    }

    bool bProjectedAnyComponent = false;
    auto ProjectWorldCornersToRect = [this](const TArray<FVector>& WorldCorners, int32& OutMinPixelX, int32& OutMinPixelY, int32& OutMaxPixelX, int32& OutMaxPixelY)
    {
        const float HugeValue = 1.0e20f;
        float MinX = HugeValue;
        float MinY = HugeValue;
        float MaxX = -HugeValue;
        float MaxY = -HugeValue;
        int32 ProjectedCornerCount = 0;

        for (const FVector& Corner : WorldCorners)
        {
            FVector2D Pixel;
            float Depth = 0.0f;
            if (!ProjectWorldPointToCapturePixel(Corner, Pixel, Depth))
            {
                continue;
            }

            MinX = FMath::Min(MinX, Pixel.X);
            MinY = FMath::Min(MinY, Pixel.Y);
            MaxX = FMath::Max(MaxX, Pixel.X);
            MaxY = FMath::Max(MaxY, Pixel.Y);
            ++ProjectedCornerCount;
        }

        if (ProjectedCornerCount == 0 || MaxX < 0.0f || MaxY < 0.0f || MinX >= CaptureResolutionX || MinY >= CaptureResolutionY)
        {
            return false;
        }

        OutMinPixelX = FMath::Clamp(FMath::FloorToInt(MinX), 0, CaptureResolutionX - 1);
        OutMinPixelY = FMath::Clamp(FMath::FloorToInt(MinY), 0, CaptureResolutionY - 1);
        OutMaxPixelX = FMath::Clamp(FMath::CeilToInt(MaxX), 0, CaptureResolutionX - 1);
        OutMaxPixelY = FMath::Clamp(FMath::CeilToInt(MaxY), 0, CaptureResolutionY - 1);

        return OutMaxPixelX >= OutMinPixelX && OutMaxPixelY >= OutMinPixelY;
    };

    auto AddLocalBoxCorners = [](const FBox& LocalBox, const FTransform& LocalToWorld, TArray<FVector>& OutWorldCorners)
    {
        OutWorldCorners.Reset(8);
        for (int32 XIndex = 0; XIndex < 2; ++XIndex)
        {
            for (int32 YIndex = 0; YIndex < 2; ++YIndex)
            {
                for (int32 ZIndex = 0; ZIndex < 2; ++ZIndex)
                {
                    const FVector LocalCorner(
                        XIndex == 0 ? LocalBox.Min.X : LocalBox.Max.X,
                        YIndex == 0 ? LocalBox.Min.Y : LocalBox.Max.Y,
                        ZIndex == 0 ? LocalBox.Min.Z : LocalBox.Max.Z);
                    OutWorldCorners.Add(LocalToWorld.TransformPosition(LocalCorner));
                }
            }
        }
    };

    auto AddWorldAabbCorners = [](const FBoxSphereBounds& Bounds, TArray<FVector>& OutWorldCorners)
    {
        OutWorldCorners.Reset(8);
        const FVector Origin = Bounds.Origin;
        const FVector Extent = Bounds.BoxExtent;

        for (int32 XSign = -1; XSign <= 1; XSign += 2)
        {
            for (int32 YSign = -1; YSign <= 1; YSign += 2)
            {
                for (int32 ZSign = -1; ZSign <= 1; ZSign += 2)
                {
                    OutWorldCorners.Add(Origin + FVector(Extent.X * XSign, Extent.Y * YSign, Extent.Z * ZSign));
                }
            }
        }
    };

    auto PaintRect = [this, &OutMaskPixels, &OutCoveredPixels](int32 MinPixelX, int32 MinPixelY, int32 MaxPixelX, int32 MaxPixelY)
    {
        for (int32 PixelY = MinPixelY; PixelY <= MaxPixelY; ++PixelY)
        {
            const int32 RowOffset = PixelY * CaptureResolutionX;
            for (int32 PixelX = MinPixelX; PixelX <= MaxPixelX; ++PixelX)
            {
                FColor& Pixel = OutMaskPixels[RowOffset + PixelX];
                if (Pixel.R == 0)
                {
                    ++OutCoveredPixels;
                }
                Pixel = FColor::White;
            }
        }
    };

    for (const UPrimitiveComponent* Component : Components)
    {
        if (!Component)
        {
            continue;
        }

        if (const UInstancedStaticMeshComponent* InstancedComponent = Cast<UInstancedStaticMeshComponent>(Component))
        {
            const UStaticMesh* StaticMesh = InstancedComponent->GetStaticMesh();
            const FBox LocalBox = StaticMesh ? StaticMesh->GetBoundingBox() : FBox(FVector(-50.0f, -50.0f, -50.0f), FVector(50.0f, 50.0f, 50.0f));
            TArray<FVector> WorldCorners;

            for (int32 InstanceIndex = 0; InstanceIndex < InstancedComponent->GetInstanceCount(); ++InstanceIndex)
            {
                FTransform InstanceTransform;
                if (!InstancedComponent->GetInstanceTransform(InstanceIndex, InstanceTransform, true))
                {
                    continue;
                }

                AddLocalBoxCorners(LocalBox, InstanceTransform, WorldCorners);

                int32 MinPixelX = 0;
                int32 MinPixelY = 0;
                int32 MaxPixelX = 0;
                int32 MaxPixelY = 0;
                if (ProjectWorldCornersToRect(WorldCorners, MinPixelX, MinPixelY, MaxPixelX, MaxPixelY))
                {
                    bProjectedAnyComponent = true;
                    PaintRect(MinPixelX, MinPixelY, MaxPixelX, MaxPixelY);
                }
            }

            continue;
        }

        TArray<FVector> WorldCorners;
        AddWorldAabbCorners(Component->Bounds, WorldCorners);

        int32 MinPixelX = 0;
        int32 MinPixelY = 0;
        int32 MaxPixelX = 0;
        int32 MaxPixelY = 0;
        if (ProjectWorldCornersToRect(WorldCorners, MinPixelX, MinPixelY, MaxPixelX, MaxPixelY))
        {
            bProjectedAnyComponent = true;
            PaintRect(MinPixelX, MinPixelY, MaxPixelX, MaxPixelY);
        }
    }

    return bProjectedAnyComponent && OutCoveredPixels > 0;
}

bool AVOProjectionCaptureRig::BuildProjectedDepthPreview(TArray<FColor>& OutPixels, TArray<FString>& Warnings) const
{
    struct FProjectedDepthRect
    {
        int32 MinX = 0;
        int32 MinY = 0;
        int32 MaxX = 0;
        int32 MaxY = 0;
        float Depth = 0.0f;
    };

    OutPixels.Init(FColor::Black, CaptureResolutionX * CaptureResolutionY);

    if (CaptureResolutionX <= 0 || CaptureResolutionY <= 0)
    {
        return false;
    }

    TArray<UPrimitiveComponent*> Components;
    for (const FVOProjectionLayerDefinition& Layer : ProjectionLayers)
    {
        if (!Layer.bEnabled || Layer.LayerId.IsNone())
        {
            continue;
        }

        for (UPrimitiveComponent* Component : FindMatchingComponentsForLayer(Layer))
        {
            Components.AddUnique(Component);
        }
    }

    if (Components.Num() == 0)
    {
        Warnings.Add(TEXT("Projected depth preview skipped because no projection layer components were matched."));
        return false;
    }

    TArray<FProjectedDepthRect> Rects;
    float MinDepth = 1.0e20f;
    float MaxDepth = -1.0e20f;

    auto ProjectWorldCornersToDepthRect = [this](const TArray<FVector>& WorldCorners, FProjectedDepthRect& OutRect)
    {
        const float HugeValue = 1.0e20f;
        float MinX = HugeValue;
        float MinY = HugeValue;
        float MaxX = -HugeValue;
        float MaxY = -HugeValue;
        float DepthSum = 0.0f;
        int32 ProjectedCornerCount = 0;

        for (const FVector& Corner : WorldCorners)
        {
            FVector2D Pixel;
            float Depth = 0.0f;
            if (!ProjectWorldPointToCapturePixel(Corner, Pixel, Depth))
            {
                continue;
            }

            MinX = FMath::Min(MinX, Pixel.X);
            MinY = FMath::Min(MinY, Pixel.Y);
            MaxX = FMath::Max(MaxX, Pixel.X);
            MaxY = FMath::Max(MaxY, Pixel.Y);
            DepthSum += Depth;
            ++ProjectedCornerCount;
        }

        if (ProjectedCornerCount == 0 || MaxX < 0.0f || MaxY < 0.0f || MinX >= CaptureResolutionX || MinY >= CaptureResolutionY)
        {
            return false;
        }

        OutRect.MinX = FMath::Clamp(FMath::FloorToInt(MinX), 0, CaptureResolutionX - 1);
        OutRect.MinY = FMath::Clamp(FMath::FloorToInt(MinY), 0, CaptureResolutionY - 1);
        OutRect.MaxX = FMath::Clamp(FMath::CeilToInt(MaxX), 0, CaptureResolutionX - 1);
        OutRect.MaxY = FMath::Clamp(FMath::CeilToInt(MaxY), 0, CaptureResolutionY - 1);
        OutRect.Depth = DepthSum / static_cast<float>(ProjectedCornerCount);

        return OutRect.MaxX >= OutRect.MinX && OutRect.MaxY >= OutRect.MinY;
    };

    auto AddLocalBoxCorners = [](const FBox& LocalBox, const FTransform& LocalToWorld, TArray<FVector>& OutWorldCorners)
    {
        OutWorldCorners.Reset(8);
        for (int32 XIndex = 0; XIndex < 2; ++XIndex)
        {
            for (int32 YIndex = 0; YIndex < 2; ++YIndex)
            {
                for (int32 ZIndex = 0; ZIndex < 2; ++ZIndex)
                {
                    const FVector LocalCorner(
                        XIndex == 0 ? LocalBox.Min.X : LocalBox.Max.X,
                        YIndex == 0 ? LocalBox.Min.Y : LocalBox.Max.Y,
                        ZIndex == 0 ? LocalBox.Min.Z : LocalBox.Max.Z);
                    OutWorldCorners.Add(LocalToWorld.TransformPosition(LocalCorner));
                }
            }
        }
    };

    auto AddWorldAabbCorners = [](const FBoxSphereBounds& Bounds, TArray<FVector>& OutWorldCorners)
    {
        OutWorldCorners.Reset(8);
        const FVector Origin = Bounds.Origin;
        const FVector Extent = Bounds.BoxExtent;

        for (int32 XSign = -1; XSign <= 1; XSign += 2)
        {
            for (int32 YSign = -1; YSign <= 1; YSign += 2)
            {
                for (int32 ZSign = -1; ZSign <= 1; ZSign += 2)
                {
                    OutWorldCorners.Add(Origin + FVector(Extent.X * XSign, Extent.Y * YSign, Extent.Z * ZSign));
                }
            }
        }
    };

    auto AddDepthRect = [&ProjectWorldCornersToDepthRect, &Rects, &MinDepth, &MaxDepth](const TArray<FVector>& WorldCorners)
    {
        FProjectedDepthRect Rect;
        if (!ProjectWorldCornersToDepthRect(WorldCorners, Rect))
        {
            return;
        }
        MinDepth = FMath::Min(MinDepth, Rect.Depth);
        MaxDepth = FMath::Max(MaxDepth, Rect.Depth);
        Rects.Add(Rect);
    };

    for (const UPrimitiveComponent* Component : Components)
    {
        if (!Component)
        {
            continue;
        }

        if (const UInstancedStaticMeshComponent* InstancedComponent = Cast<UInstancedStaticMeshComponent>(Component))
        {
            const UStaticMesh* StaticMesh = InstancedComponent->GetStaticMesh();
            const FBox LocalBox = StaticMesh ? StaticMesh->GetBoundingBox() : FBox(FVector(-50.0f, -50.0f, -50.0f), FVector(50.0f, 50.0f, 50.0f));
            TArray<FVector> WorldCorners;

            for (int32 InstanceIndex = 0; InstanceIndex < InstancedComponent->GetInstanceCount(); ++InstanceIndex)
            {
                FTransform InstanceTransform;
                if (!InstancedComponent->GetInstanceTransform(InstanceIndex, InstanceTransform, true))
                {
                    continue;
                }

                AddLocalBoxCorners(LocalBox, InstanceTransform, WorldCorners);
                AddDepthRect(WorldCorners);
            }

            continue;
        }

        TArray<FVector> WorldCorners;
        AddWorldAabbCorners(Component->Bounds, WorldCorners);
        AddDepthRect(WorldCorners);
    }

    if (Rects.Num() == 0)
    {
        Warnings.Add(TEXT("Projected depth preview skipped because matched component bounds did not project into the capture frame."));
        return false;
    }

    Rects.Sort([](const FProjectedDepthRect& A, const FProjectedDepthRect& B)
    {
        return A.Depth > B.Depth;
    });

    const float DepthRange = FMath::Max(MaxDepth - MinDepth, KINDA_SMALL_NUMBER);
    for (const FProjectedDepthRect& Rect : Rects)
    {
        const float NormalizedDepth = FMath::Clamp((Rect.Depth - MinDepth) / DepthRange, 0.0f, 1.0f);
        const uint8 Shade = static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(245.0f, 45.0f, NormalizedDepth)));
        const FColor DepthColor(Shade, Shade, Shade, 255);

        for (int32 PixelY = Rect.MinY; PixelY <= Rect.MaxY; ++PixelY)
        {
            const int32 RowOffset = PixelY * CaptureResolutionX;
            for (int32 PixelX = Rect.MinX; PixelX <= Rect.MaxX; ++PixelX)
            {
                OutPixels[RowOffset + PixelX] = DepthColor;
            }
        }
    }

    return true;
}

void AVOProjectionCaptureRig::WriteGrayboxFromSemantic(const FString& OutputRoot, const TArray<FColor>& SemanticPixels, TArray<FString>& Warnings) const
{
    TArray<FColor> GrayboxPixels;
    GrayboxPixels.SetNumUninitialized(CaptureResolutionX * CaptureResolutionY);

    const int32 PixelCount = FMath::Min(GrayboxPixels.Num(), SemanticPixels.Num());
    for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
    {
        const FColor& SemanticPixel = SemanticPixels[PixelIndex];
        const bool bCovered = SemanticPixel.R > 0 || SemanticPixel.G > 0 || SemanticPixel.B > 0;
        GrayboxPixels[PixelIndex] = bCovered ? FColor(150, 150, 150, 255) : FColor::Black;
    }

    for (int32 PixelIndex = PixelCount; PixelIndex < GrayboxPixels.Num(); ++PixelIndex)
    {
        GrayboxPixels[PixelIndex] = FColor::Black;
    }

    FString Error;
    if (!VOProjectionImageUtils::SaveColorArrayAsPng(OutputRoot / TEXT("full/full_graybox_or_scene.png"), GrayboxPixels, CaptureResolutionX, CaptureResolutionY, Error))
    {
        Warnings.Add(Error);
        return;
    }

    Warnings.Add(TEXT("full_graybox_or_scene.png was generated from layer masks to avoid runtime material texture noise."));
}

void AVOProjectionCaptureRig::CompositeSemanticLayer(const TArray<FColor>& MaskPixels, const FLinearColor& SemanticColor, TArray<FColor>& InOutSemanticPixels) const
{
    const FColor SemanticFColor = VOProjectionImageUtils::LinearColorToFColor(SemanticColor);
    const int32 PixelCount = FMath::Min(MaskPixels.Num(), InOutSemanticPixels.Num());

    for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
    {
        if (MaskPixels[PixelIndex].R > 0)
        {
            InOutSemanticPixels[PixelIndex] = SemanticFColor;
        }
    }
}
