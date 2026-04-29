#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UEdGraphNode;
class UEdGraphPin;

namespace UAgent::BlueprintAccess {
/** Serialize a pin to a JSON object (direction, category, linkedTo, …). */
TSharedRef<FJsonObject> PinToJson(UEdGraphPin *Pin);

/** Serialize a node to a JSON object (id, class, title, position, pins). */
TSharedRef<FJsonObject> NodeToJson(UEdGraphNode *Node);

/**
 * Load the Blueprint at AssetPath and produce a JSON dump of its graphs,
 * nodes, pins, and connections. When the serialized size exceeds MaxChars,
 * the result object has "truncated":true and "originalLen":N set.
 * Returns nullptr with OutError set on failure.
 */
TSharedPtr<FJsonObject> BuildBlueprintDump(const FString &AssetPath,
                                           int32 MaxChars, FString &OutError);
} // namespace UAgent::BlueprintAccess
