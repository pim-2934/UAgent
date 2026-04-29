#pragma once

#include "AssetRegistry/AssetData.h"
#include "CoreMinimal.h"
#include "Templates/Function.h"

class UClass;

namespace UAgent {
struct FContentBlock;

/**
 * Registry of per-asset-class content-block builders. The chat window asks
 * the registry for a content block matching a given FAssetData; builders
 * are matched in registration order by `IsChildOf`, so a Blueprint builder
 * covers WidgetBlueprint/AnimBlueprint too. Unknown classes fall back to
 * a generic resource_link pointing at the .uasset on disk.
 *
 * Adding a new asset kind = one file that defines a builder function + a
 * line in `RegisterBuiltinAssetContextBuilders`. Existing callers don't
 * change.
 */
class FAssetContextBuilderRegistry {
public:
  using FBuilder = TFunction<FContentBlock(const FAssetData & /*Asset*/,
                                           int32 /*MaxChars*/)>;

  /**
   * Register a builder for any asset whose class is, or derives from,
   * InClass. Later registrations win when multiple entries match.
   */
  void Register(UClass *InClass, FBuilder InBuilder);

  /**
   * Build a content block for InAsset. Never returns a defaulted block —
   * if no class-specific builder matches, a resource_link is produced.
   */
  FContentBlock Build(const FAssetData &InAsset, int32 MaxChars) const;

private:
  struct FEntry {
    UClass *ForClass = nullptr;
    FBuilder Builder;
  };
  TArray<FEntry> Entries;
};

/**
 * Register the built-in builders (currently: UBlueprint subclasses). Call
 * once at module startup against a fresh registry.
 */
void RegisterBuiltinAssetContextBuilders(
    FAssetContextBuilderRegistry &Registry);
} // namespace UAgent
