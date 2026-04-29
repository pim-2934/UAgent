#pragma once

#include "CoreMinimal.h"

class UClass;
class UObject;

namespace UAgent::Common {
/**
 * Normalize an asset path to the "/Game/X/Y.Y" form expected by
 * StaticLoadObject. Accepts "/Game/X/Y", "/Game/X/Y.Y", or "/Game/X/Y.Y_C".
 */
FString NormalizeAssetPath(const FString &InPath);

/**
 * Load an asset by path, optionally checked against an expected UClass.
 * Accepts the same forms as NormalizeAssetPath. Returns nullptr with OutError
 * set on failure.
 */
UObject *LoadAssetByPath(const FString &InPath, UClass *Expected,
                         FString &OutError);

/**
 * Split "/Game/Foo/Bar" (or "/Game/Foo/Bar.Bar") into the long package name
 * ("/Game/Foo/Bar") and the asset/object name ("Bar"). Returns false with
 * OutError set if the path is empty, doesn't start with '/', or lacks a
 * terminal segment.
 */
bool SplitContentPath(const FString &InPath, FString &OutPackageName,
                      FString &OutAssetName, FString &OutError);
} // namespace UAgent::Common
