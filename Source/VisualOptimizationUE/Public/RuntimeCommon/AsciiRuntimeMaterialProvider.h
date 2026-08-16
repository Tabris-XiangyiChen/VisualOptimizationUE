#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AsciiRuntimeMaterialProvider.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture2D;
class FJsonObject;

UCLASS()
class VISUALOPTIMIZATIONUE_API UAsciiRuntimeMaterialProvider : public UObject
{
    GENERATED_BODY()

public:
    bool Rebuild(
        UObject* MaterialOuter,
        const FString& AbsoluteManifestPath,
        UMaterialInterface* RuntimeMaterialMaster,
        float WorldTileSizeCm,
        FString& OutError);

    UMaterialInterface* ResolveMaterial(FName MaterialSlotId) const;
    void Reset();
    int32 GetMaterialCount() const { return RuntimeMaterialCache.Num(); }

private:
    UMaterialInstanceDynamic* CreateMaterial(
        UObject* MaterialOuter,
        UMaterialInterface* RuntimeMaterialMaster,
        FName MaterialSlotId,
        const FString& ManifestDirectory,
        const TSharedPtr<FJsonObject>& TexturesObject,
        float WorldTileSizeCm);

    UTexture2D* LoadTexture2D(const FString& AbsolutePath, bool bSRGB);
    FString ResolveTexturePath(const FString& ManifestDirectory, const FString& TexturePath) const;

    UPROPERTY(Transient)
    TMap<FName, UMaterialInstanceDynamic*> RuntimeMaterialCache;

    UPROPERTY(Transient)
    TArray<UTexture2D*> RuntimeLoadedTextures;
};
