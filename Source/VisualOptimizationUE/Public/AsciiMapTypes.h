#pragma once

#include "CoreMinimal.h"
#include "AsciiMapTypes.generated.h"

UENUM(BlueprintType)
enum class EAsciiTileRole : uint8
{
    Void,
    Floor,
    Grass,
    Wood,
    Water,
    Wall,
    Door,
    Foliage
};

USTRUCT(BlueprintType)
struct FAsciiTileDefinition
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Symbol;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName SlotId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EAsciiTileRole Role = EAsciiTileRole::Void;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bGenerate = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float ZOffset = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Height = 0.0f;
};