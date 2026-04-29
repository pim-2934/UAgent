#include "ClassResolver.h"

#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

namespace UAgent::Common {
namespace {
UClass *TryLoadByPath(const FString &Path) {
  if (UObject *Obj = StaticLoadObject(UClass::StaticClass(), nullptr, *Path)) {
    return Cast<UClass>(Obj);
  }
  return nullptr;
}

UClass *TryStripClassPrefix(const FString &Name) {
  // Blueprint or raw class lookups tend to come in with or without the U/A/F
  // prefix.
  if (Name.Len() >= 2 && (Name[0] == 'U' || Name[0] == 'A' || Name[0] == 'F') &&
      FChar::IsUpper(Name[1])) {
    return FindFirstObject<UClass>(*Name.RightChop(1),
                                   EFindFirstObjectOptions::NativeFirst);
  }
  return nullptr;
}
} // namespace

UClass *ResolveClass(const FString &NameOrPath, FString &OutError) {
  if (NameOrPath.IsEmpty()) {
    OutError = TEXT("empty class name");
    return nullptr;
  }

  // Path form — try as-is, then with the _C suffix (blueprint generated class).
  if (NameOrPath.StartsWith(TEXT("/")) || NameOrPath.Contains(TEXT("."))) {
    if (UClass *C = TryLoadByPath(NameOrPath))
      return C;

    // "/Game/Foo/BP_X" — normalize to "/Game/Foo/BP_X.BP_X_C".
    FString Path = NameOrPath;
    if (!Path.Contains(TEXT("."))) {
      int32 LastSlash = INDEX_NONE;
      Path.FindLastChar(TEXT('/'), LastSlash);
      if (LastSlash != INDEX_NONE) {
        Path = Path + TEXT(".") + Path.RightChop(LastSlash + 1) + TEXT("_C");
      }
    } else if (!Path.EndsWith(TEXT("_C"))) {
      Path = Path + TEXT("_C");
    }
    if (UClass *C = TryLoadByPath(Path))
      return C;
  }

  // Short name lookup.
  if (UClass *C = FindFirstObject<UClass>(
          *NameOrPath, EFindFirstObjectOptions::NativeFirst)) {
    return C;
  }
  if (UClass *C = TryStripClassPrefix(NameOrPath)) {
    return C;
  }

  OutError = FString::Printf(TEXT("class not found: %s"), *NameOrPath);
  return nullptr;
}
} // namespace UAgent::Common
