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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ASCII Tile")
    FString Symbol;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ASCII Tile")
    FName SlotId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ASCII Tile")
    FName TileTypeId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ASCII Tile")
    FName MeshId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ASCII Tile")
    FName MaterialSlotId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ASCII Tile")
    EAsciiTileRole Role = EAsciiTileRole::Void;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ASCII Tile")
    bool bGenerate = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ASCII Tile")
    float ZOffset = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ASCII Tile")
    float Height = 0.0f;
};
