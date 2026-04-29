#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"

#include "Components/ActorComponent.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "Subsystems/EditorActorSubsystem.h"

namespace UAgent {
namespace {
class FListLevelActorsTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/list_level_actors");
  }

  virtual bool IsReadOnly() const override { return true; }

  virtual FString GetDescription() const override {
    return TEXT("List actors in the currently edited level. Returns class, "
                "name, transform, tags, and component class list for each.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"classFilter": { "type": "string", "description": "Only actors whose class name contains this substring (case-insensitive)" },
						"nameFilter": { "type": "string", "description": "Only actors whose label contains this substring (case-insensitive)" },
						"limit": { "type": "integer", "minimum": 1 }
					}
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    FString ClassFilter, NameFilter;
    int32 Limit = 500;
    if (Params.IsValid()) {
      Params->TryGetStringField(TEXT("classFilter"), ClassFilter);
      Params->TryGetStringField(TEXT("nameFilter"), NameFilter);
      Params->TryGetNumberField(TEXT("limit"), Limit);
    }
    if (Limit <= 0)
      Limit = 500;

    UEditorActorSubsystem *Sub =
        GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>()
                : nullptr;
    if (!Sub)
      return FToolResponse::Fail(-32000,
                                 TEXT("EditorActorSubsystem unavailable"));

    TArray<AActor *> All = Sub->GetAllLevelActors();
    TArray<TSharedPtr<FJsonValue>> Out;

    for (AActor *A : All) {
      if (Out.Num() >= Limit)
        break;
      if (!A)
        continue;
      const FString ClassName = A->GetClass()->GetName();
      if (!ClassFilter.IsEmpty() &&
          !ClassName.Contains(ClassFilter, ESearchCase::IgnoreCase))
        continue;
      const FString Label = A->GetActorLabel();
      if (!NameFilter.IsEmpty() &&
          !Label.Contains(NameFilter, ESearchCase::IgnoreCase))
        continue;

      TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
      J->SetStringField(TEXT("name"), A->GetName());
      J->SetStringField(TEXT("label"), Label);
      J->SetStringField(TEXT("class"), A->GetClass()->GetPathName());
      J->SetStringField(TEXT("path"), A->GetPathName());

      const FTransform T = A->GetActorTransform();
      TSharedRef<FJsonObject> Tr = MakeShared<FJsonObject>();
      const FVector L = T.GetLocation();
      const FRotator R = T.Rotator();
      const FVector S = T.GetScale3D();
      Tr->SetStringField(TEXT("location"),
                         FString::Printf(TEXT("%f,%f,%f"), L.X, L.Y, L.Z));
      Tr->SetStringField(
          TEXT("rotation"),
          FString::Printf(TEXT("%f,%f,%f"), R.Pitch, R.Yaw, R.Roll));
      Tr->SetStringField(TEXT("scale"),
                         FString::Printf(TEXT("%f,%f,%f"), S.X, S.Y, S.Z));
      J->SetObjectField(TEXT("transform"), Tr);

      TArray<TSharedPtr<FJsonValue>> Tags;
      for (const FName &T2 : A->Tags)
        Tags.Add(MakeShared<FJsonValueString>(T2.ToString()));
      J->SetArrayField(TEXT("tags"), Tags);

      TArray<UActorComponent *> Comps;
      A->GetComponents(Comps);
      TArray<TSharedPtr<FJsonValue>> CompNames;
      for (UActorComponent *C : Comps) {
        if (C)
          CompNames.Add(MakeShared<FJsonValueString>(FString::Printf(
              TEXT("%s (%s)"), *C->GetName(), *C->GetClass()->GetName())));
      }
      J->SetArrayField(TEXT("components"), CompNames);

      Out.Add(MakeShared<FJsonValueObject>(J));
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("actors"), Out);
    Result->SetNumberField(TEXT("total"), All.Num());
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateListLevelActorsTool() {
  return MakeShared<FListLevelActorsTool>();
}
} // namespace UAgent
