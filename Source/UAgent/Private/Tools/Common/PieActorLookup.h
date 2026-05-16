#pragma once

#include "CoreMinimal.h"

class AActor;
class APawn;
class UWorld;

namespace UAgent::Common {
/**
 * Return the active Play-In-Editor world, or nullptr when PIE is not running.
 * Walks GEditor's world contexts looking for EWorldType::PIE. When multiple
 * PIE worlds exist (split-screen / multi-client PIE), returns the first one
 * that has an actual UWorld pointer.
 */
UWorld *GetActivePIEWorld();

/**
 * Walk `World`'s level actors and return the first whose internal name or
 * display label matches `NameOrLabel` (case-insensitive). nullptr on miss.
 * Use with GetActivePIEWorld() to look up actors in PIE; use
 * UEditorActorSubsystem for the editor world.
 */
AActor *FindActorInWorld(UWorld *World, const FString &NameOrLabel);

/**
 * Return the pawn currently possessed by the local player controller at
 * `ControllerIndex` in `World`. Returns nullptr when no such controller
 * exists or the controller has no pawn. Skips controllers between Possess
 * and pawn instantiation, so this can transiently return null in the first
 * tick of PIE.
 */
APawn *GetPlayerPawn(UWorld *World, int32 ControllerIndex);
} // namespace UAgent::Common
