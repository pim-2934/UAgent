#include "BlueprintSerialization.h"
#include "BlueprintQueries.h"

#include "AnimGraphNode_StateMachineBase.h"
#include "AnimStateNodeBase.h"
#include "AnimStateTransitionNode.h"
#include "Animation/AnimBlueprint.h"
#include "AnimationStateMachineGraph.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UAgent::BlueprintAccess {
static const TCHAR *DirectionString(EEdGraphPinDirection Dir) {
  return Dir == EGPD_Input ? TEXT("input") : TEXT("output");
}

TSharedRef<FJsonObject> PinToJson(UEdGraphPin *Pin) {
  TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
  P->SetStringField(TEXT("name"), Pin->PinName.ToString());
  P->SetStringField(TEXT("direction"), DirectionString(Pin->Direction));
  P->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
  P->SetStringField(TEXT("subCategory"),
                    Pin->PinType.PinSubCategory.ToString());
  if (UObject *SubObj = Pin->PinType.PinSubCategoryObject.Get()) {
    P->SetStringField(TEXT("subCategoryObject"), SubObj->GetPathName());
  }
  P->SetBoolField(TEXT("isArray"), Pin->PinType.IsArray());
  P->SetStringField(TEXT("defaultValue"), Pin->DefaultValue);

  TArray<TSharedPtr<FJsonValue>> Linked;
  for (UEdGraphPin *L : Pin->LinkedTo) {
    if (!L || !L->GetOwningNode())
      continue;
    TSharedRef<FJsonObject> LL = MakeShared<FJsonObject>();
    LL->SetStringField(TEXT("nodeId"), L->GetOwningNode()->NodeGuid.ToString());
    LL->SetStringField(TEXT("pinName"), L->PinName.ToString());
    Linked.Add(MakeShared<FJsonValueObject>(LL));
  }
  P->SetArrayField(TEXT("linkedTo"), Linked);
  return P;
}

TSharedRef<FJsonObject> NodeToJson(UEdGraphNode *Node) {
  TSharedRef<FJsonObject> N = MakeShared<FJsonObject>();
  N->SetStringField(TEXT("id"), Node->NodeGuid.ToString());
  N->SetStringField(TEXT("class"), Node->GetClass()->GetName());
  N->SetStringField(TEXT("title"),
                    Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
  N->SetNumberField(TEXT("posX"), Node->NodePosX);
  N->SetNumberField(TEXT("posY"), Node->NodePosY);

  if (UK2Node_CallFunction *Call = Cast<UK2Node_CallFunction>(Node)) {
    if (UFunction *Fn = Call->GetTargetFunction()) {
      N->SetStringField(TEXT("function"), Fn->GetPathName());
    }
  }

  TArray<TSharedPtr<FJsonValue>> Pins;
  for (UEdGraphPin *Pin : Node->Pins) {
    if (!Pin)
      continue;
    Pins.Add(MakeShared<FJsonValueObject>(PinToJson(Pin)));
  }
  N->SetArrayField(TEXT("pins"), Pins);
  return N;
}

static TSharedRef<FJsonObject> PinTypeToJson(const FEdGraphPinType &T) {
  TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
  J->SetStringField(TEXT("category"), T.PinCategory.ToString());
  J->SetStringField(TEXT("subCategory"), T.PinSubCategory.ToString());
  if (UObject *SubObj = T.PinSubCategoryObject.Get()) {
    J->SetStringField(TEXT("subCategoryObject"), SubObj->GetPathName());
  }
  J->SetBoolField(TEXT("isArray"), T.IsArray());
  J->SetBoolField(TEXT("isSet"), T.IsSet());
  J->SetBoolField(TEXT("isMap"), T.IsMap());
  J->SetBoolField(TEXT("isReference"), T.bIsReference);
  return J;
}

static void WalkScsNodes(USCS_Node *Node, const FString &ParentName,
                         TArray<TSharedPtr<FJsonValue>> &Out) {
  if (!Node)
    return;
  TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
  J->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
  if (Node->ComponentClass)
    J->SetStringField(TEXT("class"), Node->ComponentClass->GetPathName());
  if (!ParentName.IsEmpty())
    J->SetStringField(TEXT("attachParent"), ParentName);
  if (!Node->AttachToName.IsNone())
    J->SetStringField(TEXT("attachSocket"), Node->AttachToName.ToString());
  Out.Add(MakeShared<FJsonValueObject>(J));

  for (USCS_Node *Child : Node->GetChildNodes()) {
    WalkScsNodes(Child, Node->GetVariableName().ToString(), Out);
  }
}

TSharedPtr<FJsonObject> BuildBlueprintDump(const FString &AssetPath,
                                           int32 MaxChars, FString &OutError) {
  UBlueprint *BP = LoadBlueprintByPath(AssetPath, OutError);
  if (!BP)
    return nullptr;

  TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
  Root->SetStringField(TEXT("path"), BP->GetPathName());
  Root->SetStringField(TEXT("name"), BP->GetName());
  if (BP->ParentClass) {
    Root->SetStringField(TEXT("parentClass"), BP->ParentClass->GetPathName());
  }
  Root->SetStringField(TEXT("blueprintType"),
                       UEnum::GetValueAsString(BP->BlueprintType));

  // Interfaces.
  TArray<TSharedPtr<FJsonValue>> Interfaces;
  for (const FBPInterfaceDescription &I : BP->ImplementedInterfaces) {
    if (I.Interface) {
      Interfaces.Add(MakeShared<FJsonValueString>(I.Interface->GetPathName()));
    }
  }
  Root->SetArrayField(TEXT("interfaces"), Interfaces);

  // Component tree (SCS).
  TArray<TSharedPtr<FJsonValue>> Components;
  if (BP->SimpleConstructionScript) {
    for (USCS_Node *Root_ : BP->SimpleConstructionScript->GetRootNodes()) {
      WalkScsNodes(Root_, FString(), Components);
    }
  }
  Root->SetArrayField(TEXT("componentTree"), Components);

  // Variables.
  TArray<TSharedPtr<FJsonValue>> Vars;
  for (const FBPVariableDescription &V : BP->NewVariables) {
    TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
    J->SetStringField(TEXT("name"), V.VarName.ToString());
    J->SetObjectField(TEXT("type"), PinTypeToJson(V.VarType));
    if (!V.Category.IsEmpty())
      J->SetStringField(TEXT("category"), V.Category.ToString());
    if (!V.DefaultValue.IsEmpty())
      J->SetStringField(TEXT("defaultValue"), V.DefaultValue);
    J->SetNumberField(TEXT("propertyFlags"),
                      static_cast<double>(V.PropertyFlags));
    Vars.Add(MakeShared<FJsonValueObject>(J));
  }
  Root->SetArrayField(TEXT("variables"), Vars);

  // Functions (flat list).
  TArray<TSharedPtr<FJsonValue>> Funcs;
  for (UEdGraph *G : BP->FunctionGraphs) {
    if (!G)
      continue;
    TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
    J->SetStringField(TEXT("name"), G->GetName());
    Funcs.Add(MakeShared<FJsonValueObject>(J));
  }
  Root->SetArrayField(TEXT("functions"), Funcs);

  // Events (flat list from ubergraphs).
  TArray<TSharedPtr<FJsonValue>> Events;
  for (UEdGraph *G : BP->UbergraphPages) {
    if (!G)
      continue;
    for (UEdGraphNode *N : G->Nodes) {
      if (UK2Node_Event *EvNode = Cast<UK2Node_Event>(N)) {
        TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
        J->SetStringField(TEXT("name"), EvNode->GetFunctionName().ToString());
        J->SetStringField(TEXT("nodeId"), EvNode->NodeGuid.ToString());
        J->SetBoolField(TEXT("custom"), EvNode->IsA<UK2Node_CustomEvent>());
        Events.Add(MakeShared<FJsonValueObject>(J));
      }
    }
  }
  Root->SetArrayField(TEXT("events"), Events);

  TArray<TSharedPtr<FJsonValue>> Graphs;

  auto SerializeGraphs = [&](const TArray<UEdGraph *> &InGraphs,
                             const TCHAR *Kind) {
    for (UEdGraph *G : InGraphs) {
      if (!G)
        continue;
      TSharedRef<FJsonObject> GJ = MakeShared<FJsonObject>();
      GJ->SetStringField(TEXT("name"), G->GetName());
      GJ->SetStringField(TEXT("kind"), Kind);
      TArray<TSharedPtr<FJsonValue>> Nodes;
      for (UEdGraphNode *N : G->Nodes) {
        if (!N)
          continue;
        Nodes.Add(MakeShared<FJsonValueObject>(NodeToJson(N)));
      }
      GJ->SetArrayField(TEXT("nodes"), Nodes);
      Graphs.Add(MakeShared<FJsonValueObject>(GJ));
    }
  };

  SerializeGraphs(BP->UbergraphPages, TEXT("ubergraph"));
  SerializeGraphs(BP->FunctionGraphs, TEXT("function"));
  SerializeGraphs(BP->MacroGraphs, TEXT("macro"));

  Root->SetArrayField(TEXT("graphs"), Graphs);

  // AnimBlueprint — surface state machines, states, transitions.
  if (UAnimBlueprint *ABP = Cast<UAnimBlueprint>(BP)) {
    Root->SetBoolField(TEXT("isAnimBlueprint"), true);
    if (ABP->TargetSkeleton) {
      Root->SetStringField(TEXT("targetSkeleton"),
                           ABP->TargetSkeleton->GetPathName());
    }

    TArray<TSharedPtr<FJsonValue>> StateMachines;
    auto VisitGraph = [&StateMachines](UEdGraph *G) {
      if (!G)
        return;
      for (UEdGraphNode *N : G->Nodes) {
        UAnimGraphNode_StateMachineBase *SM =
            Cast<UAnimGraphNode_StateMachineBase>(N);
        if (!SM || !SM->EditorStateMachineGraph)
          continue;

        TSharedRef<FJsonObject> SMJson = MakeShared<FJsonObject>();
        SMJson->SetStringField(TEXT("name"), SM->GetStateMachineName());
        SMJson->SetStringField(TEXT("nodeId"), SM->NodeGuid.ToString());

        TArray<TSharedPtr<FJsonValue>> States, Transitions;
        for (UEdGraphNode *SMN : SM->EditorStateMachineGraph->Nodes) {
          if (UAnimStateTransitionNode *Tr =
                  Cast<UAnimStateTransitionNode>(SMN)) {
            TSharedRef<FJsonObject> T = MakeShared<FJsonObject>();
            T->SetStringField(TEXT("id"), Tr->NodeGuid.ToString());
            if (UAnimStateNodeBase *Prev = Tr->GetPreviousState())
              T->SetStringField(TEXT("from"), Prev->GetStateName());
            if (UAnimStateNodeBase *Next = Tr->GetNextState())
              T->SetStringField(TEXT("to"), Next->GetStateName());
            Transitions.Add(MakeShared<FJsonValueObject>(T));
          } else if (UAnimStateNodeBase *St = Cast<UAnimStateNodeBase>(SMN)) {
            TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
            J->SetStringField(TEXT("id"), St->NodeGuid.ToString());
            J->SetStringField(TEXT("name"), St->GetStateName());
            J->SetStringField(TEXT("class"), St->GetClass()->GetName());
            States.Add(MakeShared<FJsonValueObject>(J));
          }
        }
        SMJson->SetArrayField(TEXT("states"), States);
        SMJson->SetArrayField(TEXT("transitions"), Transitions);
        StateMachines.Add(MakeShared<FJsonValueObject>(SMJson));
      }
    };
    for (UEdGraph *G : BP->FunctionGraphs)
      VisitGraph(G);
    for (UEdGraph *G : BP->UbergraphPages)
      VisitGraph(G);
    Root->SetArrayField(TEXT("animStateMachines"), StateMachines);
  }

  FString Serialized;
  TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
  FJsonSerializer::Serialize(Root, Writer);
  if (Serialized.Len() > MaxChars) {
    Root->SetBoolField(TEXT("truncated"), true);
    Root->SetNumberField(TEXT("originalLen"), Serialized.Len());
  }

  return Root;
}
} // namespace UAgent::BlueprintAccess
