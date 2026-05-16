#pragma once

#include "CoreMinimal.h"

class UActorComponent;
class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class USCS_Node;
class UWidget;
class UWidgetBlueprint;
class UInheritableComponentHandler;

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
  /** Inherited from a C++ parent class; lives on the GeneratedClass CDO as a
   * default subobject created via CreateDefaultSubobject. */
  Inherited,
  /** Authored on an ancestor *Blueprint*'s SimpleConstructionScript. Parent-BP
   * SCS components are class metadata (the SCS node tree on the parent BPGC),
   * not CDO subobjects, so the C++ DSO walk doesn't find them. Component
   * points at the effective template — the child's InheritableComponentHandler
   * override if it exists, else the parent's SCS template. */
  InheritedSCS,
};

/** Result of ResolveBlueprintComponent. Component is null on miss. */
struct FResolvedBlueprintComponent {
  /** The effective component template. For SCS, the BP's own template; for
   * Inherited, the CDO subobject; for InheritedSCS, the child's ICH override
   * if present, otherwise the parent BP's SCS template. Null on miss. */
  UActorComponent *Component = nullptr;
  /** SCS source only — the owning USCS_Node on *this* BP, used for Modify()
   * on writes. Null for other sources. */
  USCS_Node *Node = nullptr;
  /** Inherited source only — the GeneratedClass CDO that owns Component, used
   * for Modify() on writes. Null for other sources. */
  UObject *CDO = nullptr;
  /** InheritedSCS source only — the SCS node on the ancestor BP that declares
   * this component. Used to build the FComponentKey for ICH override
   * creation. Null for other sources. */
  USCS_Node *ParentSCSNode = nullptr;
  EBlueprintComponentSource Source = EBlueprintComponentSource::SCS;
};

/**
 * Resolve a component on a Blueprint by name. In order:
 *   1. This BP's own SCS (Source=SCS).
 *   2. Each ancestor BP's SCS, walked via GetSuperClass(); when matched, the
 *      child's InheritableComponentHandler override template is preferred over
 *      the parent's template if one exists (Source=InheritedSCS).
 *   3. The GeneratedClass CDO's components, for inherited C++ default
 *      subobjects (Source=Inherited).
 * Returns Component=null on miss.
 *
 * All three sources yield archetype objects (RF_ArchetypeObject), so callers
 * can apply the same FProperty mutation + GetArchetypeInstances() propagation
 * loop used for SCS templates. Mutating an InheritedSCS result without first
 * creating an ICH override would write the parent BP's template (affecting
 * every descendant). Use EnsureWritableComponentTemplate before mutation.
 */
FResolvedBlueprintComponent
ResolveBlueprintComponent(UBlueprint *BP, const FString &ComponentName);

/**
 * Ensure Resolved.Component is a template owned by InBP that can be safely
 * mutated without affecting siblings:
 *   - SCS / Inherited (C++ DSO): no-op, returns the existing template.
 *   - InheritedSCS: gets-or-creates an InheritableComponentHandler override on
 *     this BP keyed off Resolved.ParentSCSNode, rewrites Resolved.Component to
 *     point at the override, and returns it.
 * Returns nullptr with OutError set on failure. Caller is responsible for
 * Modify() / PostEditChange / Compile / Save around the subsequent mutation.
 */
UActorComponent *EnsureWritableComponentTemplate(
    UBlueprint *InBP, FResolvedBlueprintComponent &Resolved, FString &OutError);

/**
 * Load a UWidgetBlueprint by asset path. Accepts the same path shapes as
 * LoadBlueprintByPath. Returns nullptr with OutError set on miss or when the
 * asset isn't a Widget Blueprint.
 */
UWidgetBlueprint *LoadWidgetBlueprintByPath(const FString &InPath,
                                            FString &OutError);

/**
 * Find a widget inside a Widget Blueprint's WidgetTree by name
 * (case-insensitive GetName() match). Returns nullptr if the BP has no
 * WidgetTree or no widget with that name exists.
 */
UWidget *FindWidgetInTree(UWidgetBlueprint *WBP, const FString &WidgetName);
} // namespace UAgent::BlueprintAccess
