#include "BlueprintQueries.h"
#include "../Common/AssetPathUtils.h"

#include "Components/ActorComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"

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

FResolvedBlueprintComponent
ResolveBlueprintComponent(UBlueprint *BP, const FString &ComponentName) {
  FResolvedBlueprintComponent Out;
  if (!BP || ComponentName.IsEmpty())
    return Out;

  // SCS first — components authored on this Blueprint.
  if (USimpleConstructionScript *SCS = BP->SimpleConstructionScript) {
    const FName Target(*ComponentName);
    for (USCS_Node *N : SCS->GetAllNodes()) {
      if (N && N->GetVariableName() == Target) {
        Out.Component = N->ComponentTemplate;
        Out.Node = N;
        Out.Source = EBlueprintComponentSource::SCS;
        return Out;
      }
    }
  }

  // Inherited fallback — walk the GeneratedClass CDO's components.
  // CDO subobjects carry an FName of "<MemberName>_GEN_VARIABLE"; the
  // human-facing GetName() drops the suffix. Match either form so callers
  // can pass the C++ member variable name.
  UClass *GenClass = BP->GeneratedClass;
  if (!GenClass)
    return Out;
  AActor *CDOActor = Cast<AActor>(GenClass->GetDefaultObject());
  if (!CDOActor)
    return Out;
  TArray<UActorComponent *> Comps;
  CDOActor->GetComponents(Comps);
  for (UActorComponent *C : Comps) {
    if (!C)
      continue;
    if (C->GetName().Equals(ComponentName, ESearchCase::IgnoreCase) ||
        C->GetFName().ToString().Equals(ComponentName + TEXT("_GEN_VARIABLE"),
                                        ESearchCase::IgnoreCase)) {
      Out.Component = C;
      Out.CDO = CDOActor;
      Out.Source = EBlueprintComponentSource::Inherited;
      return Out;
    }
  }
  return Out;
}
} // namespace UAgent::BlueprintAccess
