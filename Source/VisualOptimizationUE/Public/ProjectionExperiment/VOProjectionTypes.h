#pragma once

#include "CoreMinimal.h"
#include "VOProjectionTypes.generated.h"

class UMaterialInterface;
class UTexture2D;

USTRUCT(BlueprintType)
struct FVOProjectionLayerDefinition
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection")
    FName LayerId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection")
    TArray<FName> MatchMaterialSlotIds;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection")
    TArray<FName> MatchComponentTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection")
    TArray<FString> MatchComponentNameContains;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection")
    FLinearColor SemanticColor = FLinearColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection")
    bool bEnabled = true;
};

USTRUCT(BlueprintType)
struct FVOProjectionCaptureMetadata
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection")
    FString MapId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection")
    FIntPoint Resolution = FIntPoint(2048, 2048);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection")
    FString ProjectionType = TEXT("orthographic");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection")
    FVector CameraLocation = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection")
    FRotator CameraRotation = FRotator::ZeroRotator;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection")
    float OrthoWidth = 4096.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection")
    FString OutputRoot;
};

USTRUCT(BlueprintType)
struct FVOProjectionLayerReprojectionInput
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection")
    FName LayerId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection")
    UTexture2D* GeneratedColorTexture = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection")
    UTexture2D* LayerMaskTexture = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection")
    UMaterialInterface* ProjectionMaterialTemplate = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection")
    float Opacity = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection")
    bool bEnabled = true;
};
