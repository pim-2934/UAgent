#include "BlueprintQueries.h"
#include "../Common/AssetPathUtils.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"

namespace UAgent::BlueprintAccess {
UBlueprint *LoadBlueprintByPath(const FString &InPath, FString &OutError) {
  UObject *Loaded =
      Common::LoadAssetByPath(InPath, UBlueprint::StaticClass(), OutError);
  return Cast<UBlueprint>(Loaded);
}

UEdGraph *FindGraph(UBlueprint *BP, const FString &GraphName) {
  auto Check = [&](const TArray<UEdGraph *> &List) -> UEdGraph * {
    for (UEdGraph *G : List) {
      if (G && G->GetName() == GraphName)
        return G;
    }
    return nullptr;
  };
  if (UEdGraph *G = Check(BP->UbergraphPages))
    return G;
  if (UEdGraph *G = Check(BP->FunctionGraphs))
    return G;
  if (UEdGraph *G = Check(BP->MacroGraphs))
    return G;
  if (UEdGraph *G = Check(BP->IntermediateGeneratedGraphs))
    return G;
  if (UEdGraph *G = Check(BP->DelegateSignatureGraphs))
    return G;
  return nullptr;
}

UEdGraphNode *FindNodeById(UEdGraph *Graph, const FString &NodeIdString) {
  FGuid Target;
  if (!FGuid::Parse(NodeIdString, Target))
    return nullptr;
  for (UEdGraphNode *N : Graph->Nodes) {
    if (N && N->NodeGuid == Target)
      return N;
  }
  return nullptr;
}

UEdGraphPin *FindPinByName(UEdGraphNode *Node, const FString &PinName) {
  for (UEdGraphPin *P : Node->Pins) {
    if (P && P->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase)) {
      return P;
    }
  }
  return nullptr;
}
} // namespace UAgent::BlueprintAccess
