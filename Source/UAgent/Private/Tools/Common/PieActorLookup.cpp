#include "PieActorLookup.h"

#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

namespace UAgent::Common {
UWorld *GetActivePIEWorld() {
#if WITH_EDITOR
  if (!GEditor)
    return nullptr;
  for (const FWorldContext &Ctx : GEditor->GetWorldContexts()) {
    if (Ctx.WorldType == EWorldType::PIE && Ctx.World()) {
      return Ctx.World();
    }
  }
#endif
  return nullptr;
}

AActor *FindActorInWorld(UWorld *World, const FString &NameOrLabel) {
  if (!World || NameOrLabel.IsEmpty())
    return nullptr;
  for (TActorIterator<AActor> It(World); It; ++It) {
    AActor *A = *It;
    if (!A)
      continue;
    if (A->GetName().Equals(NameOrLabel, ESearchCase::IgnoreCase))
      return A;
#if WITH_EDITOR
    if (A->GetActorLabel().Equals(NameOrLabel, ESearchCase::IgnoreCase))
      return A;
#endif
  }
  return nullptr;
}

APawn *GetPlayerPawn(UWorld *World, int32 ControllerIndex) {
  if (!World || ControllerIndex < 0)
    return nullptr;
  APlayerController *PC =
      UGameplayStatics::GetPlayerController(World, ControllerIndex);
  return PC ? PC->GetPawn() : nullptr;
}
} // namespace UAgent::Common
