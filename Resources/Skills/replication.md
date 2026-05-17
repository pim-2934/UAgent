---
name: replication
description: Networking — authority, ownership, RPCs (Server/Client/NetMulticast), RepNotify, replicated subobjects, relevancy, common bugs
---

# Replication (UE5 networking)

Use this skill when the request touches **multiplayer state**: replicated variables, server/client RPCs, owned vs unowned actors, "why doesn't my client see X." Single-player code doesn't need this — but the moment a request mentions "the server" / "the client" / "the other player," start here.

The mental model that prevents most bugs:

> **Server is authoritative.** Clients are stale views of server state that catch up via property replication and react to events via RPCs. Anything important happens on the server first, then propagates.

Internalizing this resolves most "it works in single-player but breaks in PIE-with-2-players" issues without further investigation.

## Authority vs ownership — two different things

People conflate these constantly. They are not the same:

- **Authority** = "this machine is the server for this actor." Check with `HasAuthority()`. On a dedicated/listen server, this is true for replicated actors spawned by the server. On a client, it's false for those actors — but true for purely-local actors (HUD, cosmetic actors the client spawned itself).
- **Ownership** = "this PlayerController owns this actor." Set via `SetOwner()`. Drives:
  - Who can call **Server RPCs** on the actor (only the owning client).
  - Who receives **Client RPCs** invoked on the actor (only the owning client).
  - Network relevancy for `bNetUseOwnerRelevancy=true` actors.

A `PlayerController` owns its possessed `Pawn` automatically. Components on an owned actor inherit the same owner.

**Common bug**: "my Server RPC isn't firing on the server." Check 1) is the actor replicated (`bReplicates=true`), 2) does the calling client own it (the actor is the locally-controlled pawn, or the PlayerController itself, or something the controller `SetOwner`'d). A Server RPC called from a non-owning client is dropped silently by the engine.

## Actor replication setup

The four knobs you'll touch on every replicated actor:

```cpp
AMyActor::AMyActor() {
  bReplicates = true;              // turn on replication
  bAlwaysRelevant = false;         // default; set true for global actors (GameState, large bosses)
  bNetLoadOnClient = true;         // default; set false if the actor is server-only-spawned
  NetUpdateFrequency = 100.f;      // hz — how often the server tries to push updates
  MinNetUpdateFrequency = 2.f;     // hz — floor when nothing changes; saves bandwidth
  NetCullDistanceSquared = ...;    // squared distance beyond which the actor is irrelevant
}
```

Movement replication for `ACharacter` is handled by `UCharacterMovementComponent` automatically — don't write your own RepNotify for `GetActorLocation()`. For non-character pawns, `bReplicateMovement=true` does the basic position/rotation/velocity sync.

## Replicated properties

Declare with `UPROPERTY(Replicated)` or `UPROPERTY(ReplicatedUsing=OnRep_X)` and register in `GetLifetimeReplicatedProps`:

```cpp
void AMyActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const {
  Super::GetLifetimeReplicatedProps(Out);
  DOREPLIFETIME(AMyActor, Health);
  DOREPLIFETIME_CONDITION(AMyActor, AmmoCount, COND_OwnerOnly);
}
```

Conditions worth knowing:

- `COND_OwnerOnly` — only the owning client receives updates. Use for HUD-only stats (ammo, energy that other players shouldn't see).
- `COND_SkipOwner` — owning client doesn't receive updates (they predicted it locally already).
- `COND_InitialOnly` — replicated once on spawn, never updated. Use for spawn-time configuration.
- `COND_SimulatedOnly` — only non-owning clients receive it.

### RepNotify (`ReplicatedUsing=`)

```cpp
UPROPERTY(ReplicatedUsing=OnRep_Health)
float Health;

UFUNCTION() void OnRep_Health(float OldHealth);
```

`OnRep_X` fires on **remote machines** when the replicated value arrives — *not* on the machine that authored the change. The server (which set the value) does not get its own OnRep automatically. Two consequences:

1. **Don't put gameplay logic in OnRep_X expecting it to run on the server.** Server-side, write the value via a setter that does both: set the property and call the reaction function directly.
2. To fire OnRep on the server too, set the property *and then* call `OnRep_X(OldValue)` explicitly. Common pattern for symmetric logic:

   ```cpp
   void AMyActor::SetHealth(float NewHealth) {
     const float Old = Health;
     Health = NewHealth;
     OnRep_Health(Old);   // server fires it explicitly; clients get it via replication
   }
   ```

`OldValue` parameter is optional but **extremely useful** — without it you can't tell "took damage" from "was healed."

### Race conditions to be aware of

- **`OnRep_X` can fire before `BeginPlay`.** A client-spawned actor receives initial replicated state before `BeginPlay` runs. If `OnRep_X` references a component, that component might not exist yet. Guard with `IsValid` and consider deferring the reaction.
- **`OnRep_PlayerState` is the right place for client-side init that needs the PlayerState** — not `BeginPlay`, because the PlayerState pointer can still be null there.
- **Order of replicated properties is not guaranteed.** Don't write `OnRep_A` assuming `B` already replicated.

## RPCs

Three flavors, declared with `UFUNCTION(Server|Client|NetMulticast, Reliable|Unreliable)`. The `_Implementation` is the body; the engine generates the dispatcher.

### Server RPC

```cpp
UFUNCTION(Server, Reliable, WithValidation)
void Server_Fire(FVector_NetQuantize HitLocation);
void Server_Fire_Implementation(FVector_NetQuantize HitLocation);
bool Server_Fire_Validate(FVector_NetQuantize HitLocation);
```

- Called on the **owning client**, executes on the **server**.
- Use for "the player wants to do X, the server has to authorize and execute it."
- `WithValidation` requires a `_Validate` returning `bool`; returning false **kicks the client**. Use to sanity-check parameters (location within world bounds, ammo > 0). Skip for fully-trusted parameters.
- **A non-owning client calling a Server RPC is silently dropped.** Set ownership correctly.

### Client RPC

```cpp
UFUNCTION(Client, Reliable)
void Client_ShowHitMarker(FVector HitLocation);
```

- Called on the **server**, executes on the **owning client only**.
- Use for "the server confirmed something happened to this specific player." Damage numbers, "you got the killing blow," personal notifications.

### NetMulticast RPC

```cpp
UFUNCTION(NetMulticast, Unreliable)
void Multicast_PlayHitEffect(FVector HitLocation, FVector_NetNormal Normal);
```

- Called on the **server**, executes on the **server *and* every relevant client**.
- Use for **cosmetic events all clients need to see**: explosion VFX, ragdoll impulses, one-shot SFX at a location.
- **Clients cannot call a Multicast RPC.** Multicast from a client is silently dropped (and `LogNet` warns about it). The pattern when a client wants a multicast effect: client → Server RPC → Multicast.

### Reliable vs Unreliable

- **Reliable** — delivered in order, retried until acknowledged. Limited queue (~64 by default). Don't spam reliables (per-frame reliables WILL disconnect the client). Use for **state changes**: "you took damage," "round started."
- **Unreliable** — fire-and-forget, may be dropped. Use for **cosmetics and high-frequency events**: hit FX, footstep sounds, projectile trails.

**Default to Unreliable** for cosmetic multicasts. A dropped explosion VFX is a much smaller bug than a saturated reliable buffer.

## Replicated subobjects

Components on a replicated actor that are added in C++ via `CreateDefaultSubobject` and have `SetIsReplicated(true)` automatically replicate. **For dynamically-spawned subobjects** (UObjects that aren't Components, or runtime-added Components), you must override:

```cpp
bool AMyActor::ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch,
                                    FReplicationFlags* RepFlags) {
  bool WroteSomething = Super::ReplicateSubobjects(Channel, Bunch, RepFlags);
  for (UInventoryItem* Item : Items)
    WroteSomething |= Channel->ReplicateSubobject(Item, *Bunch, *RepFlags);
  return WroteSomething;
}
```

UE5.1+: prefer the **Subobject List Registration** API (`AddReplicatedSubObject` / `RemoveReplicatedSubObject`) instead of overriding `ReplicateSubobjects`. Check `read_header AActor.h` for `IsUsingRegisteredSubObjectList()` to confirm it's enabled — `bReplicateUsingRegisteredSubObjectList=true` is the opt-in.

## Network relevancy and dormancy

The engine doesn't replicate everything to everyone. Relevancy filters per-connection:

- **`bAlwaysRelevant=true`** — every client gets updates. Use for GameState, GameMode (server-only anyway), big bosses, level streaming volumes. Expensive — use sparingly.
- **Default relevancy** — within `NetCullDistanceSquared` and inside the frustum (for some actor types). Most actors.
- **`bNetUseOwnerRelevancy=true`** — actor is relevant if its `GetNetOwner()` is relevant. Use for components/owned subobjects that should follow their parent's visibility.

**Dormancy** is the bandwidth optimization for actors that rarely change:

- `DORM_Awake` (default) — replicated normally.
- `DORM_DormantAll` — no replication until `FlushNetDormancy()` is called. Use for static-after-spawn actors (e.g. a placed weapon pickup). Call `FlushNetDormancy()` before changing a replicated property, otherwise the change won't propagate.
- `DORM_DormantPartial` — server decides per-connection.

Common bug: replicated variable change on a dormant actor "doesn't replicate." Call `FlushNetDormancy()` before the change.

## Listen server vs dedicated server

- **Listen server** = one player's machine *is* the server. They're both server and a client. `HasAuthority()` is true; they also have a local PlayerController. Most code paths run *twice* for them — once as authority logic, once as local-player logic. Don't write `if (!HasAuthority()) { /* local player stuff */ }` — the listen-server host also needs the local-player stuff.
- **Dedicated server** = no local player, runs headless. `GEngine->GetWorldFromContextObjectChecked(...)->GetNetMode() == NM_DedicatedServer`. UI / audio / VFX should be guarded against running here (they crash or waste CPU).

When in doubt, write the "do the gameplay" path under `HasAuthority()` and the "react to it as a player" path under `IsLocallyControlled()` or `Cast<APlayerController>(Controller)->IsLocalPlayerController()`. The listen-server host hits both.

## Common pitfalls (in rough order of frequency)

1. **Server RPC from a non-owning client.** Silently dropped, no log unless `LogNet` is verbose. Fix: set ownership; verify with `GetOwner()` / `GetInstigatorController()`.
2. **Client calling NetMulticast.** Silently dropped. Fix: client → Server RPC → Multicast.
3. **Per-frame Reliable RPCs.** Saturates the queue, kicks the client. Fix: throttle, batch, or switch to Unreliable.
4. **Expecting `OnRep_X` to fire on the server.** Doesn't. Fix: call the reaction directly in your setter; pass through OnRep on remote.
5. **Spawning UI / playing sound on a dedicated server.** Crashes or wastes CPU. Fix: guard with `GetNetMode() != NM_DedicatedServer` or with `IsLocallyControlled`.
6. **Dormant actor's replicated change doesn't propagate.** Fix: `FlushNetDormancy()` before the change, or set `DORM_Awake`.
7. **`OnRep_X` references a component that doesn't exist yet.** Fix: guard with `IsValid()`; consider deferring to the next tick.
8. **`HasAuthority()` confused with `IsLocallyControlled()` on a listen server.** Both true for the host — leads to double-execution. Fix: separate concerns; write authority-side and local-player-side logic as different functions and call them from the right gates.
9. **Adding replicated UPROPERTY but forgetting `GetLifetimeReplicatedProps`.** Silent — the variable just doesn't replicate. Fix: always pair `Replicated` UPROPERTY with `DOREPLIFETIME`.
10. **Calling `SetOwner()` on the server without it being replicated.** Some downstream code on the client sees `GetOwner() == nullptr` for a frame. Fix: replicate `Owner` via `bReplicates=true` (already the default for AActor), or wait for the next replication frame before relying on ownership-based gating client-side.

## Debugging tools

- **`LogNet` log category** — bump to `Verbose` to see RPC drops, channel events. `Log LogNet Verbose` in the in-editor console.
- **`stat net`** — packet stats overlay.
- **`p.NetShowCorrections=1`** — visualize CharacterMovement reconciliations.
- **`net pktloss N`** / **`net pktorder=1`** — simulate packet loss and reordering for chaos testing.
- **UAgent's `run_console_command` tool** can drive all of these.

## Related UAgent tools

- `read_blueprint` / `read_header` — verify replication settings on a class.
- `find_function` — locate `GetLifetimeReplicatedProps` overrides project-wide.
- `set_actor_property` — toggle `bReplicates`, dormancy, etc. on a placed instance.
- `run_console_command` — toggle the `LogNet` / `p.Net*` debug switches above.

## When *not* to worry about this

Single-player game with no plans for multiplayer: skip replication entirely. `UPROPERTY` without `Replicated`, direct function calls without RPCs. Adding replication "just in case" doubles the code surface and adds nothing to a game that ships solo.
