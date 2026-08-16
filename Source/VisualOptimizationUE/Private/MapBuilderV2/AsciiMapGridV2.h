#pragma once

#include "CoreMinimal.h"
#include "MapBuilderV2/AsciiMapBuilderV2Types.h"

class FAsciiMapGridV2
{
public:
    void Initialize(int32 InWidth, int32 InHeight);

    int32 GetWidth() const { return Width; }
    int32 GetHeight() const { return Height; }
    int32 Num() const { return Cells.Num(); }

    bool IsValidCoordinate(int32 Row, int32 Column) const;
    FResolvedMapCellV2* GetCell(int32 Row, int32 Column);
    const FResolvedMapCellV2* GetCell(int32 Row, int32 Column) const;

    TArray<FResolvedMapCellV2>& GetCells() { return Cells; }
    const TArray<FResolvedMapCellV2>& GetCells() const { return Cells; }

private:
    int32 Width = 0;
    int32 Height = 0;
    TArray<FResolvedMapCellV2> Cells;
};
