#pragma once

#include "CoreMinimal.h"

class UActorComponent;
class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class USCS_Node;

namespace UAgent::BlueprintAccess {
/**
 * Load a UBlueprint by asset path. Accepts "/Game/Path/Asset" or
 * "/Game/Path/Asset.Asset"; appends the second half when missing.
 * Returns nullptr with OutError set on failure.
 */
UBlueprint *LoadBlueprintByPath(const FString &InPath, FString &OutError);

/**
 * Find a graph by name across the Blueprint's ubergraph, function, macro,
 * intermediate and delegate signature graph arrays. Returns nullptr if no
 * graph with that name is present.
 */
UEdGraph *FindGraph(UBlueprint *BP, const FString &GraphName);

/** Look up a node inside a graph by its GUID string. */
UEdGraphNode *FindNodeById(UEdGraph *Graph, const FString &NodeIdString);

/** Case-insensitive pin lookup on a node. */
UEdGraphPin *FindPinByName(UEdGraphNode *Node, const FString &PinName);

/** Where a Blueprint component came from in ResolveBlueprintComponent. */
enum class EBlueprintComponentSource : uint8 {
  /** Authored on the Blueprint itself via the SimpleConstructionScript. */
  SCS,
  /** Inherited from a C++ (or Blueprint) parent class; lives on the
   * GeneratedClass CDO as a default subobject. */
  Inherited,
};

/** Result of ResolveBlueprintComponent. Component is null on miss. */
struct FResolvedBlueprintComponent {
  /** The component template (SCS) or CDO subobject (Inherited). Null on miss.
   */
  UActorComponent *Component = nullptr;
  /** SCS source only — the owning USCS_Node, used for Modify() on writes.
   * Null when Source == Inherited. */
  USCS_Node *Node = nullptr;
  /** Inherited source only — the GeneratedClass CDO that owns Component, used
   * for Modify() on writes. Null when Source == SCS. */
  UObject *CDO = nullptr;
  EBlueprintComponentSource Source = EBlueprintComponentSource::SCS;
};

/**
 * Resolve a component on a Blueprint by name. Tries the SCS first; if the
 * name doesn't match an SCS node, walks the GeneratedClass CDO for inherited
 * C++ default subobjects and matches by GetName() or the _GEN_VARIABLE FName
 * suffix that CDO subobjects carry. Returns Component=null on miss.
 *
 * Inherited components are archetypes (RF_ArchetypeObject), so callers can
 * apply the same FProperty mutation + GetArchetypeInstances() propagation
 * loop used for SCS templates.
 */
FResolvedBlueprintComponent
ResolveBlueprintComponent(UBlueprint *BP, const FString &ComponentName);
} // namespace UAgent::BlueprintAccess
