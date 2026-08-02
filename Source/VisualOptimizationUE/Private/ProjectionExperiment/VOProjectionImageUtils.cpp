#include "ProjectionExperiment/VOProjectionImageUtils.h"

#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace VOProjectionImageUtils
{
    FString SanitizeFileToken(const FString& InToken)
    {
        FString Result = InToken;
        const TCHAR* InvalidCharacters = TEXT("\\/:*?\"<>|. ");
        for (int32 Index = 0; InvalidCharacters[Index] != TEXT('\0'); ++Index)
        {
            Result.ReplaceCharInline(InvalidCharacters[Index], TEXT('_'));
        }

        while (Result.Contains(TEXT("__")))
        {
            Result = Result.Replace(TEXT("__"), TEXT("_"));
        }

        return Result.IsEmpty() ? TEXT("unnamed") : Result;
    }

    FString MakeTimestampString()
    {
        return FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    }

    bool EnsureDirectory(const FString& DirectoryPath)
    {
        if (DirectoryPath.IsEmpty())
        {
            return false;
        }

        return IFileManager::Get().MakeDirectory(*DirectoryPath, true);
    }

    bool SaveColorArrayAsPng(const FString& FullPath, const TArray<FColor>& Pixels, int32 Width, int32 Height, FString& OutError)
    {
        OutError.Empty();

        if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
        {
            OutError = FString::Printf(TEXT("Invalid image size for %s"), *FullPath);
            return false;
        }

        const FString Directory = FPaths::GetPath(FullPath);
        if (!EnsureDirectory(Directory))
        {
            OutError = FString::Printf(TEXT("Failed to create directory %s"), *Directory);
            return false;
        }

        IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
        TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
        if (!ImageWrapper.IsValid())
        {
            OutError = TEXT("Failed to create PNG image wrapper");
            return false;
        }

        if (!ImageWrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), Width, Height, ERGBFormat::BGRA, 8))
        {
            OutError = FString::Printf(TEXT("Failed to encode PNG pixels for %s"), *FullPath);
            return false;
        }

        const TArray64<uint8>& CompressedData = ImageWrapper->GetCompressed(100);
        if (CompressedData.Num() > MAX_int32)
        {
            OutError = FString::Printf(TEXT("PNG output is too large for SaveArrayToFile: %s"), *FullPath);
            return false;
        }

        TArray<uint8> FileBytes;
        FileBytes.SetNumUninitialized(static_cast<int32>(CompressedData.Num()));
        FMemory::Memcpy(FileBytes.GetData(), CompressedData.GetData(), FileBytes.Num());

        if (!FFileHelper::SaveArrayToFile(FileBytes, *FullPath))
        {
            OutError = FString::Printf(TEXT("Failed to save PNG file %s"), *FullPath);
            return false;
        }

        return true;
    }

    bool SaveStringToFile(const FString& FullPath, const FString& Contents, FString& OutError)
    {
        OutError.Empty();

        const FString Directory = FPaths::GetPath(FullPath);
        if (!EnsureDirectory(Directory))
        {
            OutError = FString::Printf(TEXT("Failed to create directory %s"), *Directory);
            return false;
        }

        if (!FFileHelper::SaveStringToFile(Contents, *FullPath))
        {
            OutError = FString::Printf(TEXT("Failed to save text file %s"), *FullPath);
            return false;
        }

        return true;
    }

    FColor LinearColorToFColor(const FLinearColor& Color)
    {
        return Color.ToFColor(true);
    }
}
