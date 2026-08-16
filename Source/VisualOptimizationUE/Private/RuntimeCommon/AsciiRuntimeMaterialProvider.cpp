#include "RuntimeCommon/AsciiRuntimeMaterialProvider.h"

#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    FString GetOptionalTexturePath(const TSharedPtr<FJsonObject>& TexturesObject, const TCHAR* FieldName)
    {
        FString Result;
        if (TexturesObject.IsValid())
        {
            TexturesObject->TryGetStringField(FieldName, Result);
        }
        return Result;
    }
}

bool UAsciiRuntimeMaterialProvider::Rebuild(
    UObject* MaterialOuter,
    const FString& AbsoluteManifestPath,
    UMaterialInterface* RuntimeMaterialMaster,
    const float WorldTileSizeCm,
    FString& OutError)
{
    Reset();
    OutError.Empty();

    if (!RuntimeMaterialMaster)
    {
        OutError = TEXT("RuntimeMaterialMaster is not assigned.");
        return false;
    }

    if (AbsoluteManifestPath.IsEmpty())
    {
        OutError = TEXT("Material manifest path is empty.");
        return false;
    }

    FString FileContent;
    if (!FFileHelper::LoadFileToString(FileContent, *AbsoluteManifestPath))
    {
        OutError = FString::Printf(TEXT("Failed to read material manifest: %s"), *AbsoluteManifestPath);
        return false;
    }

    TSharedPtr<FJsonObject> RootObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContent);
    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        OutError = FString::Printf(TEXT("Failed to parse material manifest: %s"), *AbsoluteManifestPath);
        return false;
    }

    FString SchemaVersion;
    RootObject->TryGetStringField(TEXT("schema_version"), SchemaVersion);
    if (SchemaVersion != TEXT("material_manifest_v1"))
    {
        OutError = FString::Printf(TEXT("Unsupported material manifest schema '%s'."), *SchemaVersion);
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* MaterialValues = nullptr;
    if (!RootObject->TryGetArrayField(TEXT("materials"), MaterialValues) || !MaterialValues)
    {
        OutError = TEXT("Material manifest has no materials array.");
        return false;
    }

    const FString ManifestDirectory = FPaths::GetPath(AbsoluteManifestPath);
    for (const TSharedPtr<FJsonValue>& MaterialValue : *MaterialValues)
    {
        const TSharedPtr<FJsonObject> MaterialObject = MaterialValue.IsValid() ? MaterialValue->AsObject() : nullptr;
        if (!MaterialObject.IsValid())
        {
            continue;
        }

        FString MaterialSlotIdString;
        if (!MaterialObject->TryGetStringField(TEXT("material_slot_id"), MaterialSlotIdString) || MaterialSlotIdString.IsEmpty())
        {
            continue;
        }

        const TSharedPtr<FJsonObject>* TexturesObject = nullptr;
        if (!MaterialObject->TryGetObjectField(TEXT("textures"), TexturesObject) || !TexturesObject || !TexturesObject->IsValid())
        {
            UE_LOG(LogTemp, Warning, TEXT("MapBuilderV2: Material '%s' has no textures object."), *MaterialSlotIdString);
            continue;
        }

        const FName MaterialSlotId(*MaterialSlotIdString);
        if (UMaterialInstanceDynamic* Material = CreateMaterial(
            MaterialOuter ? MaterialOuter : this,
            RuntimeMaterialMaster,
            MaterialSlotId,
            ManifestDirectory,
            *TexturesObject,
            WorldTileSizeCm))
        {
            RuntimeMaterialCache.Add(MaterialSlotId, Material);
        }
    }

    if (RuntimeMaterialCache.IsEmpty())
    {
        OutError = TEXT("The material manifest was read, but no runtime materials could be created.");
        return false;
    }

    return true;
}

UMaterialInterface* UAsciiRuntimeMaterialProvider::ResolveMaterial(const FName MaterialSlotId) const
{
    if (UMaterialInstanceDynamic* const* Found = RuntimeMaterialCache.Find(MaterialSlotId))
    {
        return *Found;
    }
    return nullptr;
}

void UAsciiRuntimeMaterialProvider::Reset()
{
    RuntimeMaterialCache.Reset();
    RuntimeLoadedTextures.Reset();
}

UMaterialInstanceDynamic* UAsciiRuntimeMaterialProvider::CreateMaterial(
    UObject* MaterialOuter,
    UMaterialInterface* RuntimeMaterialMaster,
    const FName MaterialSlotId,
    const FString& ManifestDirectory,
    const TSharedPtr<FJsonObject>& TexturesObject,
    const float WorldTileSizeCm)
{
    const FString BaseColorPath = GetOptionalTexturePath(TexturesObject, TEXT("basecolor"));
    if (BaseColorPath.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("MapBuilderV2: Material '%s' has no basecolor texture."), *MaterialSlotId.ToString());
        return nullptr;
    }

    UTexture2D* BaseColorTexture = LoadTexture2D(ResolveTexturePath(ManifestDirectory, BaseColorPath), true);
    if (!BaseColorTexture)
    {
        return nullptr;
    }

    UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(RuntimeMaterialMaster, MaterialOuter);
    if (!Material)
    {
        return nullptr;
    }

    RuntimeLoadedTextures.Add(BaseColorTexture);
    Material->SetTextureParameterValue(TEXT("BaseColorTex"), BaseColorTexture);
    Material->SetScalarParameterValue(TEXT("RoughnessValue"), 0.8f);
    Material->SetScalarParameterValue(TEXT("MetallicValue"), 0.0f);
    Material->SetScalarParameterValue(TEXT("UVTiling"), 1.0f);
    Material->SetScalarParameterValue(TEXT("WorldTileSize"), WorldTileSizeCm);
    Material->SetScalarParameterValue(TEXT("bUseWorldAligned"), 1.0f);

    const struct FOptionalMap
    {
        const TCHAR* JsonField;
        const TCHAR* MaterialParameter;
    } OptionalMaps[] =
    {
        {TEXT("normal"), TEXT("NormalTex")},
        {TEXT("roughness"), TEXT("RoughnessTex")},
        {TEXT("height"), TEXT("HeightTex")},
        {TEXT("metallic"), TEXT("MetallicTex")}
    };

    bool bLoadedNormal = false;
    for (const FOptionalMap& OptionalMap : OptionalMaps)
    {
        const FString TexturePath = GetOptionalTexturePath(TexturesObject, OptionalMap.JsonField);
        if (TexturePath.IsEmpty())
        {
            continue;
        }

        if (UTexture2D* Texture = LoadTexture2D(ResolveTexturePath(ManifestDirectory, TexturePath), false))
        {
            RuntimeLoadedTextures.Add(Texture);
            Material->SetTextureParameterValue(OptionalMap.MaterialParameter, Texture);
            bLoadedNormal |= FCString::Stricmp(OptionalMap.JsonField, TEXT("normal")) == 0;
        }
    }

    Material->SetScalarParameterValue(TEXT("bUseNormalMap"), bLoadedNormal ? 1.0f : 0.0f);
    return Material;
}

UTexture2D* UAsciiRuntimeMaterialProvider::LoadTexture2D(const FString& AbsolutePath, const bool bSRGB)
{
    TArray<uint8> FileData;
    if (AbsolutePath.IsEmpty() || !FFileHelper::LoadFileToArray(FileData, *AbsolutePath) || FileData.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("MapBuilderV2: Failed to read texture: %s"), *AbsolutePath);
        return nullptr;
    }

    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    const EImageFormat ImageFormat = ImageWrapperModule.DetectImageFormat(FileData.GetData(), FileData.Num());
    if (ImageFormat == EImageFormat::Invalid)
    {
        UE_LOG(LogTemp, Warning, TEXT("MapBuilderV2: Unsupported texture format: %s"), *AbsolutePath);
        return nullptr;
    }

    const TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);
    if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(FileData.GetData(), FileData.Num()))
    {
        return nullptr;
    }

    TArray64<uint8> RawData;
    if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawData))
    {
        return nullptr;
    }

    const int32 Width = ImageWrapper->GetWidth();
    const int32 Height = ImageWrapper->GetHeight();
    const int64 ExpectedSize = static_cast<int64>(Width) * static_cast<int64>(Height) * 4;
    if (Width <= 0 || Height <= 0 || RawData.Num() < ExpectedSize)
    {
        return nullptr;
    }

    UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
    if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.IsEmpty())
    {
        return nullptr;
    }

    Texture->SRGB = bSRGB;
    Texture->NeverStream = true;
    FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
    void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
    if (!TextureData)
    {
        Mip.BulkData.Unlock();
        return nullptr;
    }

    FMemory::Memcpy(TextureData, RawData.GetData(), static_cast<SIZE_T>(ExpectedSize));
    Mip.BulkData.Unlock();
    Texture->UpdateResource();
    return Texture;
}

FString UAsciiRuntimeMaterialProvider::ResolveTexturePath(const FString& ManifestDirectory, const FString& TexturePath) const
{
    FString Result = FPaths::IsRelative(TexturePath)
        ? FPaths::Combine(ManifestDirectory, TexturePath)
        : TexturePath;
    FPaths::NormalizeFilename(Result);
    return Result;
}
