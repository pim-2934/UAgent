#pragma once

#include "CoreMinimal.h"

class UClass;

namespace UAgent::Common {
/**
 * Resolve a class name or path to a UClass*. Accepts:
 *   - short name:  "Character" or "ACharacter"
 *   - script path: "/Script/Engine.Character"
 *   - blueprint:   "/Game/Foo/BP_MyThing" or
 * "/Game/Foo/BP_MyThing.BP_MyThing_C"
 */
UClass *ResolveClass(const FString &NameOrPath, FString &OutError);
} // namespace UAgent::Common
