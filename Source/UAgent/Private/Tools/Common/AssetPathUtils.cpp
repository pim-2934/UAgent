#include "AssetPathUtils.h"

#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

namespace UAgent::Common {
FString NormalizeAssetPath(const FString &InPath) {
  FString Path = InPath;
  if (!Path.Contains(TEXT("."))) {
    int32 LastSlash = INDEX_NONE;
    Path.FindLastChar(TEXT('/'), LastSlash);
    if (LastSlash != INDEX_NONE) {
      const FString Name = Path.RightChop(LastSlash + 1);
      Path = Path + TEXT(".") + Name;
    }
  }
  return Path;
}

UObject *LoadAssetByPath(const FString &InPath, UClass *Expected,
                         FString &OutError) {
  const FString Path = NormalizeAssetPath(InPath);
  UObject *Loaded = StaticLoadObject(UObject::StaticClass(), nullptr, *Path);
  if (!Loaded) {
    OutError = FString::Printf(TEXT("could not load asset at '%s'"), *InPath);
    return nullptr;
  }
  if (Expected && !Loaded->IsA(Expected)) {
    OutError =
        FString::Printf(TEXT("asset at '%s' is %s, expected %s"), *InPath,
                        *Loaded->GetClass()->GetName(), *Expected->GetName());
    return nullptr;
  }
  return Loaded;
}

bool SplitContentPath(const FString &InPath, FString &OutPackageName,
                      FString &OutAssetName, FString &OutError) {
  if (InPath.IsEmpty() || !InPath.StartsWith(TEXT("/"))) {
    OutError = TEXT("path must be a content-browser path starting with '/' "
                    "(e.g. '/Game/Foo/Bar')");
    return false;
  }
  FString Trimmed = InPath;
  int32 Dot = INDEX_NONE;
  if (Trimmed.FindChar(TEXT('.'), Dot))
    Trimmed = Trimmed.Left(Dot);

  int32 LastSlash = INDEX_NONE;
  if (!Trimmed.FindLastChar(TEXT('/'), LastSlash) || LastSlash == 0 ||
      LastSlash == Trimmed.Len() - 1) {
    OutError = FString::Printf(TEXT("malformed path '%s'"), *InPath);
    return false;
  }
  OutPackageName = Trimmed;
  OutAssetName = Trimmed.RightChop(LastSlash + 1);
  return true;
}
} // namespace UAgent::Common
