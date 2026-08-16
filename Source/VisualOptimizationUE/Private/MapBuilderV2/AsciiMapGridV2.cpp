#include "MapBuilderV2/AsciiMapGridV2.h"

void FAsciiMapGridV2::Initialize(const int32 InWidth, const int32 InHeight)
{
    Width = FMath::Max(0, InWidth);
    Height = FMath::Max(0, InHeight);
    Cells.SetNum(Width * Height);

    for (int32 Row = 0; Row < Height; ++Row)
    {
        for (int32 Column = 0; Column < Width; ++Column)
        {
            FResolvedMapCellV2& Cell = Cells[Row * Width + Column];
            Cell.Row = Row;
            Cell.Column = Column;
        }
    }
}

bool FAsciiMapGridV2::IsValidCoordinate(const int32 Row, const int32 Column) const
{
    return Row >= 0 && Row < Height && Column >= 0 && Column < Width;
}

FResolvedMapCellV2* FAsciiMapGridV2::GetCell(const int32 Row, const int32 Column)
{
    return IsValidCoordinate(Row, Column) ? &Cells[Row * Width + Column] : nullptr;
}

const FResolvedMapCellV2* FAsciiMapGridV2::GetCell(const int32 Row, const int32 Column) const
{
    return IsValidCoordinate(Row, Column) ? &Cells[Row * Width + Column] : nullptr;
}
