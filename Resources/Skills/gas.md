---
name: gas
description: Gameplay Ability System — attributes, effects, abilities, cues, replication modes, tag conventions, common pitfalls
---

# Gameplay Ability System (GAS)

GAS is the engine's framework for **attributes, abilities, status effects, and cues**, wired together by gameplay tags and replicated through an `AbilitySystemComponent` (ASC). Use this skill when the user's request touches health/stamina/mana, dodge/parry/skill activation, buffs/debuffs/DoTs, cooldowns, hit-react FX, or anything else that's clearly "the gameplay state of a character." Don't reach for GAS if a single replicated `int32 Health` and a couple of RPCs would do — GAS earns its complexity at the *third* attribute, not the first.

## Verifying API surface

Class signatures and exact member names change between engine versions. **Use `read_header`, `get_class_info`, and `find_function` to verify** before writing code against any GAS class. This skill names the right classes and the right shape of the code; treat parameter lists as a starting point, not gospel.

## Module setup

The GAS module is `GameplayAbilities`. To use it from a project module, add these to `Build.cs`:

```csharp
PublicDependencyModuleNames.AddRange(new[] {
    "GameplayAbilities",
    "GameplayTags",
    "GameplayTasks",   // required by GameplayAbilities, won't compile without it
});
```

Then call `UAbilitySystemGlobals::Get().InitGlobalData()` once at startup (typically in `UGameInstance::Init` or the module's `StartupModule`). Skipping this causes `FGameplayAbilityTargetData` serialization to crash the first time a server RPC carries one — a classic "works in editor, crashes on packaged build" bug.

## Where the ASC lives

The single most important architectural decision in any GAS project:

- **Player characters → ASC on the `PlayerState`.** PlayerState is replicated and persists across pawn death/respawn, so attributes and cooldowns survive when the pawn is destroyed. The Character holds a `UPROPERTY()` pointer cached from `OnRep_PlayerState` and `PossessedBy`.
- **AI / NPCs → ASC on the Pawn or Character directly.** AI doesn't respawn-with-persistent-state; putting their ASC on the Pawn keeps everything in one actor and avoids the PlayerState dance.

Mixing these (e.g. ASC on Character for player) is the most common GAS bug class. Attributes appear to reset on every respawn, abilities re-grant on every possession, cooldowns vanish — all symptoms of "ASC died with the pawn."

After choosing where it lives, **always** call `ASC->InitAbilityActorInfo(OwnerActor, AvatarActor)` from both the server *and* the client. The standard pattern:

- **Server**: in `APawn::PossessedBy(AController*)` — runs after PlayerState is assigned.
- **Client**: in `APawn::OnRep_PlayerState()` — runs after the replicated PlayerState arrives.

Forgetting the client-side init is the second most common GAS bug: abilities won't activate locally, cues don't play on the owning client.

## Attributes — `UAttributeSet`

Attributes are float values (`FGameplayAttributeData`) living on a `UAttributeSet` subclass owned by the ASC. They have **Base** (the persistent value) and **Current** (Base + temporary modifiers from active effects).

The boilerplate per attribute:

```cpp
UCLASS()
class UMyAttributeSet : public UAttributeSet {
  GENERATED_BODY()

public:
  UMyAttributeSet();
  virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
  virtual void PreAttributeChange(const FGameplayAttribute& Attr, float& NewValue) override;
  virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

  UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Health)
  FGameplayAttributeData Health;
  ATTRIBUTE_ACCESSORS(UMyAttributeSet, Health)

  UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_MaxHealth)
  FGameplayAttributeData MaxHealth;
  ATTRIBUTE_ACCESSORS(UMyAttributeSet, MaxHealth)

  UFUNCTION() void OnRep_Health(const FGameplayAttributeData& OldValue);
  UFUNCTION() void OnRep_MaxHealth(const FGameplayAttributeData& OldValue);
};
```

The `ATTRIBUTE_ACCESSORS` macro (defined in `AttributeSet.h`) generates `GetHealth()`, `SetHealth()`, `InitHealth()`, `GetHealthAttribute()` — use these everywhere instead of touching the field directly. Setting Base via direct field write skips the modifier pipeline and desyncs the Current value.

Inside `OnRep_X` you must call `GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, Health, OldValue)` — that's what lets the prediction system reconcile predicted-and-confirmed values without flicker.

### Where to clamp

- `PreAttributeChange` — **clamp `NewValue` here** (e.g. `Health = FMath::Clamp(NewValue, 0.f, GetMaxHealth())`). This runs before *any* modification (instant or duration). Cheap, called often, must be branchless / no side effects.
- `PostGameplayEffectExecute` — **react to instant changes here** (e.g. "if Health hit 0, broadcast OnDied"). This runs after instant GEs apply; do not clamp here (too late, the modifier already wrote).

Forgetting to clamp in `PreAttributeChange` causes negative health, health above max, etc. Reacting to deaths in `PreAttributeChange` causes spurious death events for buffs that briefly reduce health.

### Replication modes

Set on the ASC, not the AttributeSet:

```cpp
ASC->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
```

- **Full** — every client gets every GE, every cue, every tag change. Use for **single-player or co-op** where every machine cares about every actor's GAS state.
- **Mixed** — the owning client + server know everything; other clients only see *minimal* (tag changes and cue calls that affect them). Use for **player characters in multiplayer**.
- **Minimal** — only the server runs effects, clients get GE-result events. Use for **AI/NPCs in multiplayer** — saves a *lot* of bandwidth.

Mismatch between intended mode and ASC location is a frequent bug: player ASC on the Pawn with Mixed mode "works" until you have two players, then attributes desync. Player ASC on PlayerState with Full mode burns bandwidth on every other player's full effect history.

## Gameplay Effects (GEs)

A `UGameplayEffect` is a **data asset** (Blueprint asset deriving from `UGameplayEffect`) that describes *what changes and for how long*. Four flavors via `DurationPolicy`:

- **Instant** — applies modifiers once, then expires. Modifies **Base** value. Use for damage, healing, attribute init.
- **Duration** — applies for N seconds, then expires. Modifies **Current** (the Base is restored on expiry). Use for buffs, debuffs.
- **Infinite** — never expires until manually removed. Use for stance effects, equipped-item bonuses.
- **Periodic** — combined with Duration/Infinite + Period → fires "instant"-style modifications every Period seconds. Use for DoTs / HoTs.

Modifier ops: **Add**, **Multiply**, **Divide**, **Override**. Multiple Multiply modifiers stack multiplicatively (1.5 × 1.5 = 2.25, not 2.0). Order matters for non-commutative ops; engine processes Adds before Multiplies before Overrides — verify with `read_header` on `FAggregator` if you're chasing a specific number.

Apply pattern:

```cpp
FGameplayEffectContextHandle Ctx = SourceASC->MakeEffectContext();
Ctx.AddSourceObject(Instigator);
const FGameplayEffectSpecHandle Spec =
    SourceASC->MakeOutgoingSpec(GameplayEffectClass, Level, Ctx);
SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data, TargetASC);
```

The `FGameplayEffectContextHandle` is where you stash instigator/causer/hit-result/source-object. Subclass `FGameplayEffectContext` if you need to carry custom data (and override `NetSerialize` + `Duplicate` and call `UAbilitySystemGlobals::SetGameplayEffectContextClass`).

## Abilities — `UGameplayAbility`

An ability is a Blueprint or C++ class with `ActivateAbility` (or `K2_ActivateAbility` in BP). It's granted to an ASC via `GiveAbility`:

```cpp
FGameplayAbilitySpec Spec(AbilityClass, /*Level*/1, /*InputID*/-1, SourceObject);
const FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(Spec);
```

Granting is server-authoritative. Don't call `GiveAbility` on clients — they'll get the ability locally but the server doesn't know, so any RPC-driven activation fails silently.

Key knobs on `UGameplayAbility`:

- **InstancingPolicy**: `NonInstanced` (singleton, fastest, can't hold state); `InstancedPerActor` (one per character); `InstancedPerExecution` (new instance per activation — most flexible, most allocations). Use `InstancedPerActor` unless you need re-entrance.
- **NetExecutionPolicy**: `LocalOnly`, `LocalPredicted`, `ServerOnly`, `ServerInitiated`. **LocalPredicted** is the typical player ability — runs locally immediately, server confirms or rolls back. Mismatching this with how the ability is triggered is a classic "ability only activates on server" bug.
- **ReplicationPolicy**: `ReplicateNo` (no per-instance replication; default and usually right), `ReplicateYes` (rare; use only if you need replicated ability state across clients).
- **CostGameplayEffectClass** / **CooldownGameplayEffectClass** — the engine handles "can I afford this" and "is this on cooldown" automatically when you set these.

`CanActivateAbility` is the gatekeeper. By default it checks tag requirements, cost, and cooldown. Override only when you need exotic gating (line-of-sight, animation state, etc.).

## Ability Tasks

Latent async actions inside an ability — `WaitGameplayEvent`, `WaitTargetData`, `PlayMontageAndWait`, `WaitInputRelease`, `WaitGameplayTagAdded`, etc. They're how you write "play montage, then on AnimNotify spawn projectile, then wait for input release to fire follow-up" without callback hell.

```cpp
UAbilityTask_PlayMontageAndWait* Task =
    UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
        this, NAME_None, Montage);
Task->OnCompleted.AddDynamic(this, &UMyAbility::OnMontageDone);
Task->OnInterrupted.AddDynamic(this, &UMyAbility::OnMontageInterrupted);
Task->Activate();
```

**Always** bind both completion and failure delegates. Tasks that finish without a bound handler silently end the ability's wait — looks like the ability "just stopped."

## Gameplay Cues

Cues are the **purely cosmetic** layer — particles, sounds, camera shake, decals. They're decoupled from the gameplay logic so they can run on remote clients that don't simulate effects.

Two patterns:

- **Burst** — `ASC->ExecuteGameplayCue(Tag, Params)` — one-shot. Hit FX, impact sounds.
- **Looping** — `ASC->AddGameplayCue(Tag)` / `RemoveGameplayCue(Tag)` — lifecycle-bound. On-fire status FX, glowing buff aura.

Cue handlers live in `AGameplayCueNotify_Burst` / `_Looping` / `_Static` assets. The engine routes by tag — `GameplayCue.Damage.Slash` looks up an asset tagged with that path.

Never put gameplay logic in a cue. Cues run on machines that may not have authority and may not even simulate the effect. Damage in a cue = damage that fires multiple times across clients = ghost hits.

## Gameplay Tags

GAS uses tags everywhere: ability tags, tag-requirement gates on abilities and effects, cue routing, attribute-meta-data lookups.

Conventions worth keeping:

- **Hierarchical, dot-separated.** `State.Stunned`, `State.Burning`, `Ability.Active.Dash`, `Cooldown.Dash`, `Cooldown.UltimateAbility`, `GameplayCue.Damage.Slash`, `Damage.Type.Fire`.
- **Tag categories** (the top-level prefix) should be **few and stable** — `State`, `Ability`, `Cooldown`, `Damage`, `GameplayCue`, `Status`, `Data`. Don't invent a new top-level for one feature.
- **Tags are data, not enums.** New designer-driven content should add tags via the in-editor manager (or UAgent's `create_gameplay_tag` tool), not via C++ tag macros. Hardcoding tags in C++ couples gameplay to engineering velocity.

Tag-requirement fields on abilities and effects:

- `AbilityTags` — what this ability *is* (`Ability.Active.Dash`).
- `ActivationOwnedTags` — tags the ability grants to the owner *while active* (`State.Dashing`).
- `ActivationRequiredTags` / `ActivationBlockedTags` — must-have / must-not-have for activation.
- `SourceRequiredTags` / `SourceBlockedTags` — same but for the GE source.
- `TargetRequiredTags` / `TargetBlockedTags` — same but for the GE target.

A common pattern for "you can't be stunned and dash": Dash ability sets `State.Stunned` in `ActivationBlockedTags`; the Stun GE adds `State.Stunned` to its `GrantedTags`. Now the engine handles the interaction without explicit checks.

## Common pitfalls (in rough order of frequency)

1. **ASC on the Pawn for a player character.** Attributes reset on death. Fix: move to PlayerState.
2. **Forgetting `InitAbilityActorInfo` on the owning client.** Abilities don't activate locally. Fix: call it in `OnRep_PlayerState` *and* `PossessedBy`.
3. **Clamping in `PostGameplayEffectExecute` instead of `PreAttributeChange`.** Negative health flicker. Fix: clamp pre, react post.
4. **`InitGlobalData()` missing.** Packaged-build crash on first replicated ability. Fix: call once at game-instance init.
5. **Granting abilities on the client.** Server doesn't know about them, RPC-driven activation silently no-ops. Fix: `GiveAbility` only on `HasAuthority()`.
6. **Mismatched NetExecutionPolicy.** "Ability only activates on server" or "ability activates locally but never on server." Fix: pick `LocalPredicted` for player abilities and `ServerOnly` for server-driven ones; don't mix.
7. **Replication mode not set, or set wrong.** Either bandwidth explosion or desync between clients. Fix: pick Full/Mixed/Minimal explicitly per-ASC, document the choice.
8. **Putting damage in a cue.** Multi-hit ghosts. Fix: cues are cosmetic only.
9. **Direct attribute writes via the `UPROPERTY` field instead of the accessor.** Prediction breaks, Current desyncs from Base. Fix: always use `GetHealth()` / `SetHealth()`.

## Related UAgent tools

- `create_gameplay_tag` — add tags through the in-editor manager (writes to `Config/Tags/*.ini` correctly).
- `list_gameplay_tags` — survey what's already defined before adding overlap.
- `read_data_asset` / `read_blueprint` — inspect existing `UGameplayEffect` / `UGameplayAbility` Blueprints.
- `find_native_class` / `get_class_info` / `read_header` — verify exact API surface of GAS classes before writing code.
- `create_blueprint` — create new ability or effect Blueprint assets (parent class = `GameplayAbility` or `GameplayEffect`).

## When *not* to reach for GAS

If the request is "give the player health and a damage function," do not introduce GAS. The cost is real: three new classes, replication mode decisions, ASC lifecycle wiring, and a project-wide convention. Worth it for a game built around abilities, expensive for a game that has one health bar.

Rule of thumb: **three or more interacting attributes or abilities → introduce GAS.** Below that, plain replicated UPROPERTYs and a few RPCs are cheaper to write *and* cheaper to read.
