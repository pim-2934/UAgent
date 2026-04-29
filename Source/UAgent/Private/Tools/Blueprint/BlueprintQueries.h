#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;

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
} // namespace UAgent::BlueprintAccess
