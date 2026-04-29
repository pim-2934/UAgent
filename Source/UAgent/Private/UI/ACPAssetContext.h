#pragma once

// Built-in asset-context builders. Currently registers a UBlueprint-subclass
// summarizer (inline resource block with the graph dump) against the registry.
//
// Adding a new kind: add a builder function here, then one line in
// RegisterBuiltinAssetContextBuilders (see AssetContextRegistry.h/.cpp for the
// registry itself).

#include "AssetContextRegistry.h"
