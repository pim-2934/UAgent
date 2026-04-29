#include "ACPAssetContext.h"

#include "../Protocol/ACPTypes.h"
#include "../Tools/Blueprint/BlueprintSerialization.h"

#include "Engine/Blueprint.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UAgent {
static FString AssetFileUriForBlueprint(const FAssetData &Asset) {
  const FString OnDisk = FPackageName::LongPackageNameToFilename(
      Asset.PackageName.ToString(), FPackageName::GetAssetPackageExtension());
  const FString FullDisk = FPaths::ConvertRelativePathToFull(OnDisk);
  return TEXT("file:///") + FullDisk.Replace(TEXT("\\"), TEXT("/"));
}

static FContentBlock BuildBlueprintBlock(const FAssetData &Asset,
                                         int32 MaxChars) {
  const FString Uri = AssetFileUriForBlueprint(Asset);

  FString Err;
  TSharedPtr<FJsonObject> Dump = BlueprintAccess::BuildBlueprintDump(
      Asset.GetObjectPathString(), MaxChars, Err);
  if (!Dump.IsValid()) {
    return FContentBlock::MakeResourceLink(Uri, Asset.AssetName.ToString(),
                                           FString(), -1);
  }

  FString Serialized;
  TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
  FJsonSerializer::Serialize(Dump.ToSharedRef(), Writer);
  if (Serialized.Len() > MaxChars) {
    Serialized = Serialized.Left(MaxChars) + TEXT("\n…[truncated]");
  }
  return FContentBlock::MakeResource(
      Uri, TEXT("application/x-ue-blueprint+json"), Serialized);
}

void RegisterBuiltinAssetContextBuilders(
    FAssetContextBuilderRegistry &Registry) {
  Registry.Register(UBlueprint::StaticClass(), &BuildBlueprintBlock);
}
} // namespace UAgent
