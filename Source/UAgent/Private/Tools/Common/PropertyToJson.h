#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class FProperty;
class UStruct;

namespace UAgent::Common {
/** Serialize a property's value from Container (UObject* or struct*) to JSON.
 */
TSharedPtr<FJsonValue> PropertyValueToJson(const FProperty *Prop,
                                           const void *Container);

/** Serialize all properties of Struct at Container (UObject* or struct*) to a
 * JSON object. */
TSharedRef<FJsonObject> PropertiesToJsonObject(const UStruct *Struct,
                                               const void *Container);

/** Describe the TYPE of a property (cpp type, container info) for reflection
 * tools. */
TSharedRef<FJsonObject> PropertyTypeToJson(const FProperty *Prop);
} // namespace UAgent::Common
