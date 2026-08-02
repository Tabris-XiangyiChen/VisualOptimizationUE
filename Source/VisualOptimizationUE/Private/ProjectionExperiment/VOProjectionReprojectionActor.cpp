#include "ProjectionExperiment/VOProjectionReprojectionActor.h"

#include "Components/DecalComponent.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    FString MakeProjectionComponentName(const FName LayerId)
    {
        FString Token = LayerId.ToString();
        Token.ReplaceInline(TEXT(" "), TEXT("_"));
        Token.ReplaceInline(TEXT("/"), TEXT("_"));
        Token.ReplaceInline(TEXT("\\"), TEXT("_"));
        return FString::Printf(TEXT("VOProjectionDecal_%s"), *Token);
    }
}

AVOProjectionReprojectionActor::AVOProjectionReprojectionActor()
{
    PrimaryActorTick.bCanEverTick = false;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AVOProjectionReprojectionActor::ApplyProjectionLayers()
{
    ClearProjectionLayers();

    if (!bProjectionEnabled)
    {
        UE_LOG(LogTemp, Display, TEXT("E1: Projection is disabled; no reprojection layers were applied."));
        return;
    }

    if (!CaptureMetadataPath.IsEmpty() && !bHasLoadedMetadata)
    {
        LoadMetadataFromPath();
    }

    const FVector ProjectionLocation = bHasLoadedMetadata ? MetadataCameraLocation : GetActorLocation();
    const FRotator ProjectionRotation = bHasLoadedMetadata ? MetadataCameraRotation : GetActorRotation();
    const float ProjectionWidth = bHasLoadedMetadata ? MetadataOrthoWidth : DefaultOrthoWidth;

    int32 AppliedLayerCount = 0;
    for (const FVOProjectionLayerReprojectionInput& LayerInput : LayerInputs)
    {
        if (!LayerInput.bEnabled)
        {
            continue;
        }

        if (!LayerInput.ProjectionMaterialTemplate)
        {
            UE_LOG(LogTemp, Warning, TEXT("E1: ProjectionMaterialTemplate is missing for layer '%s'; decal projection skipped."),
                *LayerInput.LayerId.ToString());
            continue;
        }

        if (!LayerInput.GeneratedColorTexture)
        {
            UE_LOG(LogTemp, Warning, TEXT("E1: GeneratedColorTexture is missing for layer '%s'; decal projection skipped."),
                *LayerInput.LayerId.ToString());
            continue;
        }

        if (!LayerInput.LayerMaskTexture)
        {
            UE_LOG(LogTemp, Warning, TEXT("E1: LayerMaskTexture is missing for layer '%s'; applying generated texture without a mask may depend on the material template."),
                *LayerInput.LayerId.ToString());
        }

        UMaterialInstanceDynamic* DynamicMaterial = UMaterialInstanceDynamic::Create(LayerInput.ProjectionMaterialTemplate, this);
        if (!DynamicMaterial)
        {
            UE_LOG(LogTemp, Warning, TEXT("E1: Failed to create projection material instance for layer '%s'."),
                *LayerInput.LayerId.ToString());
            continue;
        }

        DynamicMaterial->SetTextureParameterValue(TEXT("GeneratedTexture"), LayerInput.GeneratedColorTexture);
        if (LayerInput.LayerMaskTexture)
        {
            DynamicMaterial->SetTextureParameterValue(TEXT("LayerMask"), LayerInput.LayerMaskTexture);
        }
        DynamicMaterial->SetScalarParameterValue(TEXT("Opacity"), LayerInput.Opacity);

        const FName ComponentName(*MakeProjectionComponentName(LayerInput.LayerId));
        UDecalComponent* DecalComponent = NewObject<UDecalComponent>(this, ComponentName);
        if (!DecalComponent)
        {
            continue;
        }

        DecalComponent->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
        DecalComponent->SetWorldLocation(ProjectionLocation);
        DecalComponent->SetWorldRotation(ProjectionRotation);
        DecalComponent->DecalSize = FVector(ProjectionDepth, ProjectionWidth * 0.5f, ProjectionWidth * 0.5f);
        DecalComponent->SetDecalMaterial(DynamicMaterial);
        DecalComponent->RegisterComponent();

        ProjectionMaterials.Add(DynamicMaterial);
        ProjectionComponents.Add(DecalComponent);
        ++AppliedLayerCount;

        UE_LOG(LogTemp, Display, TEXT("E1: Applied decal projection layer '%s'."), *LayerInput.LayerId.ToString());
    }

    if (AppliedLayerCount == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("E1: No projection layers were applied. Assign GeneratedColorTexture and ProjectionMaterialTemplate for at least one layer."));
    }
}

void AVOProjectionReprojectionActor::ClearProjectionLayers()
{
    for (UDecalComponent* Component : ProjectionComponents)
    {
        if (Component)
        {
            Component->DestroyComponent();
        }
    }

    ProjectionComponents.Empty();
    ProjectionMaterials.Empty();

    UE_LOG(LogTemp, Display, TEXT("E1: Cleared projection layers."));
}

void AVOProjectionReprojectionActor::LoadMetadataFromPath()
{
    bHasLoadedMetadata = false;

    const FString FullPath = ResolveMetadataPath();
    if (FullPath.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("E1: CaptureMetadataPath is empty."));
        return;
    }

    FString FileContent;
    if (!FFileHelper::LoadFileToString(FileContent, *FullPath))
    {
        UE_LOG(LogTemp, Warning, TEXT("E1: Failed to read projection metadata: %s"), *FullPath);
        return;
    }

    TSharedPtr<FJsonObject> RootObject;
    const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(FileContent);
    if (!FJsonSerializer::Deserialize(JsonReader, RootObject) || !RootObject.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("E1: Failed to parse projection metadata: %s"), *FullPath);
        return;
    }

    FString SchemaVersion;
    if (!RootObject->TryGetStringField(TEXT("schema_version"), SchemaVersion) || SchemaVersion != TEXT("vo_projection_capture_metadata_v1"))
    {
        UE_LOG(LogTemp, Warning, TEXT("E1: Projection metadata schema '%s' is not vo_projection_capture_metadata_v1."),
            *SchemaVersion);
    }

    TArray<double> LocationValues;
    if (TryReadNumberArray(RootObject, TEXT("camera_location"), LocationValues) && LocationValues.Num() >= 3)
    {
        MetadataCameraLocation = FVector(LocationValues[0], LocationValues[1], LocationValues[2]);
    }

    TArray<double> RotationValues;
    if (TryReadNumberArray(RootObject, TEXT("camera_rotation"), RotationValues) && RotationValues.Num() >= 3)
    {
        MetadataCameraRotation = FRotator(RotationValues[0], RotationValues[1], RotationValues[2]);
    }

    TArray<double> ResolutionValues;
    if (TryReadNumberArray(RootObject, TEXT("resolution"), ResolutionValues) && ResolutionValues.Num() >= 2)
    {
        MetadataResolution = FIntPoint(static_cast<int32>(ResolutionValues[0]), static_cast<int32>(ResolutionValues[1]));
    }

    double OrthoWidthValue = DefaultOrthoWidth;
    RootObject->TryGetNumberField(TEXT("ortho_width"), OrthoWidthValue);
    MetadataOrthoWidth = static_cast<float>(OrthoWidthValue);
    RootObject->TryGetStringField(TEXT("projection_type"), MetadataProjectionType);

    bHasLoadedMetadata = true;
    UE_LOG(LogTemp, Display, TEXT("E1: Loaded projection metadata: %s"), *FullPath);
}

FString AVOProjectionReprojectionActor::ResolveMetadataPath() const
{
    if (CaptureMetadataPath.IsEmpty())
    {
        return FString();
    }

    FString FullPath = FPaths::IsRelative(CaptureMetadataPath)
        ? FPaths::ProjectSavedDir() / CaptureMetadataPath
        : CaptureMetadataPath;

    FPaths::NormalizeFilename(FullPath);
    return FullPath;
}

bool AVOProjectionReprojectionActor::TryReadNumberArray(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName, TArray<double>& OutValues) const
{
    OutValues.Empty();

    if (!JsonObject.IsValid())
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* JsonValues = nullptr;
    if (!JsonObject->TryGetArrayField(FieldName, JsonValues) || !JsonValues)
    {
        return false;
    }

    for (const TSharedPtr<FJsonValue>& Value : *JsonValues)
    {
        double NumberValue = 0.0;
        if (Value.IsValid() && Value->TryGetNumber(NumberValue))
        {
            OutValues.Add(NumberValue);
        }
    }

    return OutValues.Num() > 0;
}
