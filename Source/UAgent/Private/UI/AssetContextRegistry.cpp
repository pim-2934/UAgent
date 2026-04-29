#include "AssetContextRegistry.h"

#include "../Protocol/ACPTypes.h"

#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Class.h"

namespace UAgent {
static FString AssetFileUri(const FAssetData &Asset) {
  const FString OnDisk = FPackageName::LongPackageNameToFilename(
      Asset.PackageName.ToString(), FPackageName::GetAssetPackageExtension());
  const FString FullDisk = FPaths::ConvertRelativePathToFull(OnDisk);
  return TEXT("file:///") + FullDisk.Replace(TEXT("\\"), TEXT("/"));
}

void FAssetContextBuilderRegistry::Register(UClass *InClass,
                                            FBuilder InBuilder) {
  if (!InClass || !InBuilder) {
    return;
  }
  Entries.Add({InClass, MoveTemp(InBuilder)});
}

FContentBlock FAssetContextBuilderRegistry::Build(const FAssetData &InAsset,
                                                  int32 MaxChars) const {
  if (const UClass *Cls = InAsset.GetClass()) {
    // Most-recently-registered takes precedence — a module loaded later
    // can override an earlier builder without editing its source.
    for (int32 i = Entries.Num() - 1; i >= 0; --i) {
      const FEntry &E = Entries[i];
      if (E.ForClass && Cls->IsChildOf(E.ForClass)) {
        return E.Builder(InAsset, MaxChars);
      }
    }
  }

  return FContentBlock::MakeResourceLink(
      AssetFileUri(InAsset), InAsset.AssetName.ToString(), FString(), -1);
}
} // namespace UAgent
