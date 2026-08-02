#pragma once

#include "CoreMinimal.h"

namespace VOProjectionImageUtils
{
    VISUALOPTIMIZATIONUE_API FString SanitizeFileToken(const FString& InToken);
    VISUALOPTIMIZATIONUE_API FString MakeTimestampString();
    VISUALOPTIMIZATIONUE_API bool EnsureDirectory(const FString& DirectoryPath);
    VISUALOPTIMIZATIONUE_API bool SaveColorArrayAsPng(const FString& FullPath, const TArray<FColor>& Pixels, int32 Width, int32 Height, FString& OutError);
    VISUALOPTIMIZATIONUE_API bool SaveStringToFile(const FString& FullPath, const FString& Contents, FString& OutError);
    VISUALOPTIMIZATIONUE_API FColor LinearColorToFColor(const FLinearColor& Color);
}
