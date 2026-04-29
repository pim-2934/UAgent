#include "BlueprintNodeFactory.h"

#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "UObject/UObjectGlobals.h"

namespace UAgent::BlueprintAccess {
namespace {
void FinalizeNode(UK2Node *Node) {
  Node->CreateNewGuid();
  Node->PostPlacedNewNode();
  Node->AllocateDefaultPins();
}

UK2Node *SpawnFunctionCall(UEdGraph *Graph, UBlueprint * /*BP*/,
                           const FString &Payload, FString &OutError) {
  FString ClassPath, FnName;
  if (!Payload.Split(TEXT("."), &ClassPath, &FnName, ESearchCase::CaseSensitive,
                     ESearchDir::FromEnd) ||
      FnName.IsEmpty()) {
    OutError =
        FString::Printf(TEXT("function spec must be "
                             "/Script/Module.Class.FunctionName, got '%s'"),
                        *Payload);
    return nullptr;
  }
  UClass *Cls = FindObject<UClass>(nullptr, *ClassPath);
  if (!Cls) {
    OutError = FString::Printf(TEXT("class '%s' not found"), *ClassPath);
    return nullptr;
  }
  UFunction *Fn = Cls->FindFunctionByName(FName(*FnName));
  if (!Fn) {
    OutError = FString::Printf(TEXT("function '%s' not found on class '%s'"),
                               *FnName, *ClassPath);
    return nullptr;
  }
  UK2Node_CallFunction *Call = NewObject<UK2Node_CallFunction>(Graph);
  Call->SetFromFunction(Fn);
  Graph->AddNode(Call, /*bFromUI=*/false, /*bSelectNewNode=*/false);
  FinalizeNode(Call);
  return Call;
}

template <typename TVarNode>
UK2Node *SpawnVariableNode(UEdGraph *Graph, const FString &VarName) {
  TVarNode *VarNode = NewObject<TVarNode>(Graph);
  VarNode->VariableReference.SetSelfMember(FName(*VarName));
  Graph->AddNode(VarNode, false, false);
  FinalizeNode(VarNode);
  return VarNode;
}

UK2Node *SpawnVariableGet(UEdGraph *Graph, UBlueprint *, const FString &Payload,
                          FString &) {
  return SpawnVariableNode<UK2Node_VariableGet>(Graph, Payload);
}

UK2Node *SpawnVariableSet(UEdGraph *Graph, UBlueprint *, const FString &Payload,
                          FString &) {
  return SpawnVariableNode<UK2Node_VariableSet>(Graph, Payload);
}

UK2Node *SpawnEvent(UEdGraph *Graph, UBlueprint *BP, const FString &Payload,
                    FString &) {
  UK2Node_Event *Event = NewObject<UK2Node_Event>(Graph);
  Event->EventReference.SetExternalMember(FName(*Payload), BP->ParentClass);
  Event->bOverrideFunction = true;
  Graph->AddNode(Event, false, false);
  FinalizeNode(Event);
  return Event;
}

UK2Node *SpawnRawK2Node(UEdGraph *Graph, UBlueprint *, const FString &Payload,
                        FString &OutError) {
  UClass *Cls = FindObject<UClass>(nullptr, *Payload);
  if (!Cls || !Cls->IsChildOf(UK2Node::StaticClass())) {
    OutError = FString::Printf(TEXT("class '%s' not a UK2Node (need full "
                                    "/Script/Module.ClassName path)"),
                               *Payload);
    return nullptr;
  }
  UK2Node *N = NewObject<UK2Node>(Graph, Cls);
  Graph->AddNode(N, false, false);
  FinalizeNode(N);
  return N;
}

TMap<FString, FNodeSpawner> &GetSpawnerMap() {
  static TMap<FString, FNodeSpawner> Map = [] {
    TMap<FString, FNodeSpawner> Initial;
    Initial.Add(TEXT("function"), &SpawnFunctionCall);
    Initial.Add(TEXT("variable-get"), &SpawnVariableGet);
    Initial.Add(TEXT("variable-set"), &SpawnVariableSet);
    Initial.Add(TEXT("event"), &SpawnEvent);
    Initial.Add(TEXT("node"), &SpawnRawK2Node);
    return Initial;
  }();
  return Map;
}
} // namespace

void RegisterNodeSpawner(const FString &Prefix, FNodeSpawner Spawner) {
  GetSpawnerMap().Add(Prefix, MoveTemp(Spawner));
}

UK2Node *SpawnNodeForSpec(UEdGraph *Graph, UBlueprint *BP,
                          const FString &NodeSpec, FString &OutError) {
  FString Prefix, Payload;
  if (!NodeSpec.Split(TEXT(":"), &Prefix, &Payload)) {
    OutError = TEXT("invalid nodeSpec (missing kind:payload separator)");
    return nullptr;
  }

  if (const FNodeSpawner *Spawner = GetSpawnerMap().Find(Prefix)) {
    return (*Spawner)(Graph, BP, Payload, OutError);
  }

  OutError = FString::Printf(TEXT("unknown nodeSpec prefix '%s'"), *Prefix);
  return nullptr;
}
} // namespace UAgent::BlueprintAccess
