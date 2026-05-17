---
name: enhanced-input
description: Enhanced Input — actions, mapping contexts, modifiers, triggers, priority, binding, common pitfalls and UAgent tool tie-ins
---

# Enhanced Input

Enhanced Input is the engine's modern input system — it replaced the legacy `InputComponent` axis/action mappings. Use this skill when the request involves **binding a key/button/axis to gameplay code**, switching control schemes (combat vs UI vs vehicle), modifying input (deadzones, sensitivity), or building combo / hold / tap mechanics.

Don't reach for the legacy `BindAction("Jump", IE_Pressed, this, &APlayer::Jump)` path on new code — Enhanced Input is the supported direction and the legacy system is in maintenance.

## Mental model

Three actors and one subsystem:

1. **`UInputAction`** — an abstract verb. "Jump", "Move", "Aim". *Has no key.* Has a value type (bool, Axis1D, Axis2D, Axis3D) and a tag.
2. **`UInputMappingContext` (IMC)** — a bundle of (key → action) bindings, each with optional **Modifiers** and **Triggers**. "DefaultPlayerContext", "VehicleContext", "UIContext".
3. **`UEnhancedInputComponent`** — lives on the Pawn/PlayerController, binds action events to C++/BP functions.
4. **`UEnhancedInputLocalPlayerSubsystem`** — manages which IMCs are active for a local player. You **push/pop contexts** here to switch control schemes.

The runtime flow on a key press:

```
Hardware → Subsystem (active contexts, sorted by priority)
        → IMC binding match (with Modifiers applied)
        → Triggers (Pressed / Held / Tap / ...)
        → InputComponent dispatches to bound C++/BP function
```

Modifiers transform the value (negate, scale, swizzle XY→YX, deadzone). Triggers decide *when* to fire (immediately on press, after a hold duration, on a tap, on a chord).

## Module setup

Already in UAgent's project dependencies, but for a fresh project:

```csharp
PublicDependencyModuleNames.AddRange(new[] {
    "EnhancedInput",
});
```

In Project Settings → Input, set the **Default Player Input Class** to `UEnhancedPlayerInput` and the **Default Input Component Class** to `UEnhancedInputComponent`. Without this swap, your `UInputAction` bindings never fire and you'll waste an hour wondering why.

## Adding an Input Mapping Context

The standard pattern, in `APawn::PawnClientRestart()` or `APlayerController::SetupInputComponent()` / `OnPossess()`:

```cpp
APlayerController* PC = Cast<APlayerController>(GetController());
if (!PC) return;

ULocalPlayer* LP = PC->GetLocalPlayer();
if (!LP) return;

UEnhancedInputLocalPlayerSubsystem* Subsystem =
    LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
if (!Subsystem) return;

Subsystem->AddMappingContext(DefaultMappingContext, /*Priority=*/0);
```

`DefaultMappingContext` is a `TObjectPtr<UInputMappingContext>` member set in the Pawn's Blueprint defaults (or the C++ constructor with `ConstructorHelpers::FObjectFinder`).

Higher priority wins for conflicting bindings on the same key. So:

- 0 — default gameplay context
- 10 — vehicle context (overrides gameplay while driving)
- 100 — UI / menu context (overrides everything)

Add via `AddMappingContext`, remove via `RemoveMappingContext`. You don't usually "swap" — you stack contexts and rely on priority + `bConsumeInput`.

## Binding actions

In `SetupPlayerInputComponent`:

```cpp
void AMyCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) {
  Super::SetupPlayerInputComponent(PlayerInputComponent);
  UEnhancedInputComponent* EIC = CastChecked<UEnhancedInputComponent>(PlayerInputComponent);

  EIC->BindAction(JumpAction, ETriggerEvent::Started, this, &AMyCharacter::OnJump);
  EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AMyCharacter::OnMove);
  EIC->BindAction(AimAction, ETriggerEvent::Started, this, &AMyCharacter::OnAimBegin);
  EIC->BindAction(AimAction, ETriggerEvent::Completed, this, &AMyCharacter::OnAimEnd);
}
```

The bound function receives a `const FInputActionValue&`:

```cpp
void AMyCharacter::OnMove(const FInputActionValue& Value) {
  const FVector2D Axis = Value.Get<FVector2D>();   // for Axis2D actions
  // ... use Axis.X / Axis.Y
}

void AMyCharacter::OnJump(const FInputActionValue& Value) {
  // Started events still have a value but for digital actions it's just bool == true
  Jump();
}
```

`Value.Get<T>()` must match the action's **Value Type** (set on the `UInputAction` asset). Mismatch returns zero / default — silent bug.

## Trigger events

The five common ones, in lifecycle order:

- **`Started`** — the trigger condition began to be satisfied (e.g. key press for a Pressed trigger, first frame of holding for a Hold trigger).
- **`Triggered`** — the trigger condition is satisfied this frame. For a Hold, this fires once after the threshold; for a continuous "Move" axis, this fires every frame the key is down.
- **`Ongoing`** — the trigger is being evaluated but not yet fully triggered (e.g. mid-hold, before the duration is met).
- **`Completed`** — the trigger condition stopped being satisfied (key released).
- **`Canceled`** — the trigger condition was active but ended without firing (e.g. released a Hold before the threshold).

**Pick the right event per action**:

- Jump → `Started` (instant on press).
- Move/Look → `Triggered` (continuous while held).
- Aim (toggle on press) → `Started` for begin, `Completed` for end.
- Charge attack (charge-up) → `Ongoing` for visualizing the charge, `Triggered` for release-after-full-charge.

## Triggers (on the IMC binding, not the action)

Set per (key → action) binding inside the IMC asset:

- **Pressed** (default — actually the absence of a trigger gives you per-frame Triggered).
- **Released** — fires on key up.
- **Held** — fires once after `HoldTimeThreshold` seconds.
- **Hold And Release** — Triggered fires on release if held long enough.
- **Tap** — fires if released *before* `TapReleaseTimeThreshold`.
- **Pulse** — fires every `Interval` seconds while held.
- **Chorded Action** — fires only if another action is currently active. Use for "Shift+W = sprint, W alone = walk."
- **Combo** — chains multiple actions in order. The UE marketplace and Lyra both use this for fighting-game-style combos.
- **Down** — value is non-zero. The most "raw" trigger.

Triggers stack per binding. Multiple triggers on one binding form an AND: all must be active for the event to fire.

## Modifiers (also on the IMC binding)

Transform the input value before it reaches Triggers:

- **Negate** — flip sign.
- **Scalar** — multiply by a constant. Use for sensitivity (mouse look at 0.5x).
- **Swizzle Input Axis Values** — reorder XY → YX, etc. Useful when the world-space convention differs from the input convention.
- **Dead Zone** — zero out values below a threshold. Stick drift fix.
- **Smooth** — low-pass filter. Use sparingly — adds latency.
- **Modifier Collection** — bundle of modifiers reusable across bindings.

Order matters: modifiers run in array order. A Scalar before Dead Zone vs after gives different results.

## Common patterns

### Move + Look on twin-stick

```cpp
// Header
UPROPERTY(EditAnywhere, Category="Input") UInputAction* MoveAction;
UPROPERTY(EditAnywhere, Category="Input") UInputAction* LookAction;
UPROPERTY(EditAnywhere, Category="Input") UInputMappingContext* DefaultContext;

// Body
void AMyCharacter::OnMove(const FInputActionValue& Value) {
  const FVector2D Axis = Value.Get<FVector2D>();
  if (Controller) {
    const FRotator YawOnly(0.f, Controller->GetControlRotation().Yaw, 0.f);
    const FVector Forward = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::X);
    const FVector Right   = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::Y);
    AddMovementInput(Forward, Axis.Y);
    AddMovementInput(Right, Axis.X);
  }
}

void AMyCharacter::OnLook(const FInputActionValue& Value) {
  const FVector2D Axis = Value.Get<FVector2D>();
  AddControllerYawInput(Axis.X);
  AddControllerPitchInput(Axis.Y);
}
```

The IMC has:
- `MoveAction` bound to W/A/S/D with a Swizzle (W = +Y, A = -X) and a 2D Axis combination, *or* to the gamepad left stick directly.
- `LookAction` bound to mouse XY with Negate on Y, or to right stick with Scalar for sensitivity.

### Context switching (combat vs menu)

```cpp
void AMyPlayerController::EnterMenu() {
  if (auto* Sub = GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()) {
    Sub->AddMappingContext(MenuContext, /*Priority=*/100);
  }
}

void AMyPlayerController::ExitMenu() {
  if (auto* Sub = GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()) {
    Sub->RemoveMappingContext(MenuContext);
  }
}
```

`MenuContext` binds actions like `Cancel`, `Confirm`, `Navigate` — possibly to the *same keys* as gameplay actions. The higher priority means those bindings win, and `bConsumeInput=true` on them stops the gameplay action from also firing.

### Hold for action

In the IMC binding for `ChargeAttack`:
- Add a **Hold** trigger with `HoldTimeThreshold=0.5`.
- The action's `Started` fires on key press, `Triggered` fires after 0.5s of holding, `Completed` fires on release.

In code, bind to `Triggered` for "fire the charged attack." Bind to `Ongoing` if you want to drive a charging UI.

### Combo (UE5.3+)

For fighting-game-style "Down, Down-Forward, Forward + Punch":
- Define one `UInputAction` per direction and per button.
- Define `ComboAction` (the result).
- In the IMC, bind `ComboAction` with a **Combo** trigger listing the sequence of `UInputAction`s and their max-time-between-inputs.

## Common pitfalls

1. **Wrong `Value.Get<T>()` type.** Returns zero. Check the `UInputAction`'s Value Type matches.
2. **Binding to `Triggered` for a Pressed action and getting per-frame fires.** Use `Started` for one-shot, `Triggered` for continuous.
3. **Default Player Input Class not swapped to `UEnhancedPlayerInput`.** Nothing fires, no error. Fix in Project Settings.
4. **Forgetting to `AddMappingContext` after `PawnClientRestart`.** After respawn, the IMC isn't re-added. Either re-add in `PawnClientRestart`, or use the player's persistent ASC-equivalent if you have one.
5. **Priority order ignored because of `bConsumeInput=false`.** Higher-priority binding fires *and* lower-priority binding also fires. Set `bConsumeInput=true` on the binding (per-binding flag in the IMC).
6. **Modifier order producing surprises.** `Scalar(0.5)` then `DeadZone(0.1)` zeros out small inputs after halving them — the deadzone threshold is now effectively 0.2 in raw. Swap if that wasn't intended.
7. **Gamepad axis "feels off" — usually a missing Dead Zone modifier.** Stick drift presents as constant tiny movement.
8. **`SetupPlayerInputComponent` on a Pawn that's possessed by an AIController.** The AIController has no `UEnhancedInputComponent`. Either guard with `Cast<APlayerController>(GetController())` or move the binding to the PlayerController.

## Related UAgent tools

- `create_input_action` — create a `UInputAction` asset with the right value type and tag.
- `create_input_mapping_context` — create a `UInputMappingContext` asset; binding edits happen in the editor (open the IMC after).
- `read_input_mappings` — list existing IMCs and their bindings before adding new ones.
- `read_header EnhancedInputComponent.h` / `read_header InputAction.h` — verify current API.

## When *not* to use Enhanced Input

- **Server-side gameplay logic.** Input is a *local-player* concern. The server doesn't dispatch input events on remote actors. Server-authoritative actions still flow through input → Server RPC.
- **Pure debug commands.** A `UFUNCTION(Exec)` console command is faster to wire than an IMC binding when you're just spamming `RestartLevel`.
