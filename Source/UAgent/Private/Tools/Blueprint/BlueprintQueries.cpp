#include "BlueprintQueries.h"
#include "../Common/AssetPathUtils.h"

#include "Blueprint/WidgetTree.h"
#include "Components/ActorComponent.h"
#include "Components/Widget.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "WidgetBlueprint.h"

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

  const FName Target(*ComponentName);

  // 1. SCS — components authored on this Blueprint.
  if (USimpleConstructionScript *SCS = BP->SimpleConstructionScript) {
    for (USCS_Node *N : SCS->GetAllNodes()) {
      if (N && N->GetVariableName() == Target) {
        Out.Component = N->ComponentTemplate;
        Out.Node = N;
        Out.Source = EBlueprintComponentSource::SCS;
        return Out;
      }
    }
  }

  // 2. Parent-BP SCS — components authored on an ancestor BP. These live in
  // the parent BPGC's SCS node tree (class metadata), not on the child CDO's
  // OwnedComponents, so step 3's CDO walk would miss them. When the child
  // already has an InheritableComponentHandler override for this slot, return
  // that as the effective template; otherwise return the parent's template.
  UBlueprintGeneratedClass *ChildBPGC =
      Cast<UBlueprintGeneratedClass>(BP->GeneratedClass);
  for (UClass *Walk = BP->ParentClass; Walk; Walk = Walk->GetSuperClass()) {
    UBlueprintGeneratedClass *ParentBPGC = Cast<UBlueprintGeneratedClass>(Walk);
    if (!ParentBPGC)
      break; // Hit a native class — only DSOs from here on; let step 3 handle.
    USimpleConstructionScript *PSCS = ParentBPGC->SimpleConstructionScript;
    if (!PSCS)
      continue;
    for (USCS_Node *N : PSCS->GetAllNodes()) {
      if (!N || N->GetVariableName() != Target)
        continue;
      Out.ParentSCSNode = N;
      Out.Source = EBlueprintComponentSource::InheritedSCS;
      if (ChildBPGC) {
        if (UInheritableComponentHandler *ICH =
                ChildBPGC->GetInheritableComponentHandler(
                    /*bCreateIfNecessary=*/false)) {
          if (UActorComponent *Override =
                  ICH->GetOverridenComponentTemplate(FComponentKey(N))) {
            Out.Component = Override;
            return Out;
          }
        }
      }
      Out.Component = N->ComponentTemplate;
      return Out;
    }
  }

  // 3. Inherited C++ DSO — walk the GeneratedClass CDO's OwnedComponents.
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

UActorComponent *
EnsureWritableComponentTemplate(UBlueprint *InBP,
                                FResolvedBlueprintComponent &Resolved,
                                FString &OutError) {
  if (!InBP || !Resolved.Component) {
    OutError = TEXT("invalid resolved component");
    return nullptr;
  }

  if (Resolved.Source != EBlueprintComponentSource::InheritedSCS) {
    // SCS / C++ DSO templates are already owned by this BP's class (the
    // BPGC for SCS, the CDO for DSOs); writing them is safe.
    return Resolved.Component;
  }

  // InheritedSCS — the resolved template either is the parent's SCS template
  // (writing it leaks across every descendant) or an existing child ICH
  // override. Promote to a child override in either case so the write is
  // scoped to this BP.
  UBlueprintGeneratedClass *ChildBPGC =
      Cast<UBlueprintGeneratedClass>(InBP->GeneratedClass);
  if (!ChildBPGC) {
    OutError = TEXT("Blueprint has no generated class — compile it first");
    return nullptr;
  }
  if (!Resolved.ParentSCSNode) {
    OutError = TEXT("InheritedSCS resolution is missing the parent SCS node");
    return nullptr;
  }
  UInheritableComponentHandler *ICH =
      ChildBPGC->GetInheritableComponentHandler(/*bCreateIfNecessary=*/true);
  if (!ICH) {
    OutError = TEXT("could not create InheritableComponentHandler on BPGC");
    return nullptr;
  }
  UActorComponent *Override = ICH->CreateOverridenComponentTemplate(
      FComponentKey(Resolved.ParentSCSNode));
  if (!Override) {
    OutError =
        TEXT("InheritableComponentHandler refused to create override template");
    return nullptr;
  }
  Resolved.Component = Override;
  return Override;
}

UWidgetBlueprint *LoadWidgetBlueprintByPath(const FString &InPath,
                                            FString &OutError) {
  UObject *Loaded = Common::LoadAssetByPath(
      InPath, UWidgetBlueprint::StaticClass(), OutError);
  UWidgetBlueprint *WBP = Cast<UWidgetBlueprint>(Loaded);
  if (!WBP && OutError.IsEmpty()) {
    OutError = FString::Printf(TEXT("asset at '%s' is not a Widget Blueprint"),
                               *InPath);
  }
  return WBP;
}

UWidget *FindWidgetInTree(UWidgetBlueprint *WBP, const FString &WidgetName) {
  if (!WBP || !WBP->WidgetTree || WidgetName.IsEmpty())
    return nullptr;
  UWidget *Match = nullptr;
  WBP->WidgetTree->ForEachWidget([&](UWidget *W) {
    if (Match || !W)
      return;
    if (W->GetName().Equals(WidgetName, ESearchCase::IgnoreCase))
      Match = W;
  });
  return Match;
}
} // namespace UAgent::BlueprintAccess
