#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class UBlueprint;
class UEdGraph;
class UK2Node;

namespace UAgent::BlueprintAccess {
/**
 * Spawn a new K2 node described by NodeSpec into Graph. NodeSpec grammar:
 *   "function:/Script/Module.Class.Fn"  UK2Node_CallFunction
 *   "variable-get:MyVar"                UK2Node_VariableGet
 *   "variable-set:MyVar"                UK2Node_VariableSet
 *   "event:ReceiveBeginPlay"            UK2Node_Event (on BP->ParentClass)
 *   "node:/Script/Module.ClassName"     raw UK2Node subclass fallback
 * Returns nullptr with OutError set on failure. Caller owns positioning /
 * MarkBlueprintAsModified / CompileBlueprint.
 *
 * The "prefix:payload" dispatch lives behind a registry (see
 * BlueprintNodeFactory.cpp); extending with a new kind is additive —
 * register a new spawner, no existing branches need to change.
 */
UK2Node *SpawnNodeForSpec(UEdGraph *Graph, UBlueprint *BP,
                          const FString &NodeSpec, FString &OutError);

/**
 * Signature of a single-prefix node spawner. Receives the text *after* the
 * ':' separator and fills OutError on failure. Register via
 * RegisterNodeSpawner.
 */
using FNodeSpawner =
    TFunction<UK2Node *(UEdGraph *Graph, UBlueprint *BP, const FString &Payload,
                        FString &OutError)>;

/** Add or replace the spawner for a NodeSpec prefix. */
void RegisterNodeSpawner(const FString &Prefix, FNodeSpawner Spawner);
} // namespace UAgent::BlueprintAccess
