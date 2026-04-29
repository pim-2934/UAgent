#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/ClassResolver.h"

#include "Editor.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "UAgent"

namespace UAgent {
namespace {
// Accepts either a JSON object { "x": …, "y": …, "z": … } or a "x,y,z" string.
// Writes into OutA/OutB/OutC in that declaration order (x/y/z for vectors, or
// pitch/yaw/roll for rotators — callers pick the meaning).
bool ParseTriple(const TSharedPtr<FJsonObject> &Params, const FString &Field,
                 const FString &KeyA, const FString &KeyB, const FString &KeyC,
                 double &OutA, double &OutB, double &OutC) {
  if (!Params.IsValid())
    return false;
  const TSharedPtr<FJsonValue> Val = Params->TryGetField(Field);
  if (!Val.IsValid())
    return false;

  if (Val->Type == EJson::Object) {
    const TSharedPtr<FJsonObject> &Obj = Val->AsObject();
    if (!Obj.IsValid())
      return false;
    Obj->TryGetNumberField(KeyA, OutA);
    Obj->TryGetNumberField(KeyB, OutB);
    Obj->TryGetNumberField(KeyC, OutC);
    return true;
  }
  if (Val->Type == EJson::String) {
    TArray<FString> Parts;
    Val->AsString().ParseIntoArray(Parts, TEXT(","));
    if (Parts.Num() >= 1)
      OutA = FCString::Atod(*Parts[0].TrimStartAndEnd());
    if (Parts.Num() >= 2)
      OutB = FCString::Atod(*Parts[1].TrimStartAndEnd());
    if (Parts.Num() >= 3)
      OutC = FCString::Atod(*Parts[2].TrimStartAndEnd());
    return true;
  }
  return false;
}

class FSpawnActorTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/spawn_actor");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT("Place an actor in the currently edited level. Accepts a "
                "Blueprint asset path, class name, or script path. Optional "
                "'properties' applies an ImportText value per property after "
                "spawn (same form as set_actor_property). Returns the new "
                "actor's name, label, path, and any failed property writes.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"actorClass": { "type": "string", "description": "Blueprint asset path (e.g. '/Game/BP_Foo.BP_Foo'), class name, or script path. An Actor subclass is required." },
						"location": {
							"description": "World location. Object {x,y,z} or 'x,y,z' string. Defaults to (0,0,0).",
							"oneOf": [
								{ "type": "object", "properties": { "x": {"type":"number"}, "y": {"type":"number"}, "z": {"type":"number"} } },
								{ "type": "string" }
							]
						},
						"rotation": {
							"description": "World rotation. Object {pitch,yaw,roll} or 'pitch,yaw,roll' string. Defaults to (0,0,0).",
							"oneOf": [
								{ "type": "object", "properties": { "pitch": {"type":"number"}, "yaw": {"type":"number"}, "roll": {"type":"number"} } },
								{ "type": "string" }
							]
						},
						"label": { "type": "string", "description": "Outliner display name. Optional." },
						"properties": {
							"type": "object",
							"description": "Optional map of propertyName -> ImportText string, applied to the new actor after spawn.",
							"additionalProperties": { "type": "string" }
						}
					},
					"required": ["actorClass"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString ActorClass, Label;
    Params->TryGetStringField(TEXT("actorClass"), ActorClass);
    Params->TryGetStringField(TEXT("label"), Label);
    if (ActorClass.IsEmpty()) {
      return FToolResponse::InvalidParams(TEXT("actorClass required"));
    }

    FString Err;
    UClass *Cls = Common::ResolveClass(ActorClass, Err);
    if (!Cls)
      return FToolResponse::Fail(-32000, Err);
    if (!Cls->IsChildOf(AActor::StaticClass())) {
      return FToolResponse::Fail(
          -32000,
          FString::Printf(TEXT("%s is not an Actor subclass"), *ActorClass));
    }
    if (Cls->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated |
                              CLASS_NewerVersionExists)) {
      return FToolResponse::Fail(
          -32000,
          FString::Printf(TEXT("%s cannot be spawned (abstract/deprecated)"),
                          *Cls->GetName()));
    }

    double Lx = 0.0, Ly = 0.0, Lz = 0.0;
    double Rp = 0.0, Ry = 0.0, Rr = 0.0;
    ParseTriple(Params, TEXT("location"), TEXT("x"), TEXT("y"), TEXT("z"), Lx,
                Ly, Lz);
    ParseTriple(Params, TEXT("rotation"), TEXT("pitch"), TEXT("yaw"),
                TEXT("roll"), Rp, Ry, Rr);
    const FVector Location(Lx, Ly, Lz);
    const FRotator Rotation(Rp, Ry, Rr);

    UEditorActorSubsystem *Sub =
        GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>()
                : nullptr;
    if (!Sub)
      return FToolResponse::Fail(-32000,
                                 TEXT("EditorActorSubsystem unavailable"));

    const FScopedTransaction Tx(LOCTEXT("SpawnActorTx", "Spawn Actor"));
    AActor *Spawned =
        Sub->SpawnActorFromClass(Cls, Location, Rotation, /*bTransient=*/false);
    if (!Spawned)
      return FToolResponse::Fail(-32000,
                                 TEXT("SpawnActorFromClass returned null"));

    if (!Label.IsEmpty()) {
      Spawned->SetActorLabel(Label);
    }

    TArray<TSharedPtr<FJsonValue>> FailedProps;
    const TSharedPtr<FJsonObject> *PropMap = nullptr;
    if (Params->TryGetObjectField(TEXT("properties"), PropMap) && PropMap &&
        PropMap->IsValid()) {
      Spawned->Modify();
      for (const auto &Pair : (*PropMap)->Values) {
        const FString &PropName = Pair.Key;
        FString PropValue;
        if (!Pair.Value.IsValid() || !Pair.Value->TryGetString(PropValue)) {
          TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
          F->SetStringField(TEXT("property"), PropName);
          F->SetStringField(TEXT("error"),
                            TEXT("value must be a string (ImportText form)"));
          FailedProps.Add(MakeShared<FJsonValueObject>(F));
          continue;
        }

        FProperty *Prop =
            Spawned->GetClass()->FindPropertyByName(FName(*PropName));
        if (!Prop) {
          TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
          F->SetStringField(TEXT("property"), PropName);
          F->SetStringField(TEXT("error"),
                            FString::Printf(TEXT("not found on %s"),
                                            *Spawned->GetClass()->GetName()));
          FailedProps.Add(MakeShared<FJsonValueObject>(F));
          continue;
        }

        void *Addr = Prop->ContainerPtrToValuePtr<void>(Spawned);
        if (!Prop->ImportText_Direct(*PropValue, Addr, Spawned, PPF_None)) {
          TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
          F->SetStringField(TEXT("property"), PropName);
          F->SetStringField(TEXT("error"), TEXT("ImportText failed"));
          FailedProps.Add(MakeShared<FJsonValueObject>(F));
          continue;
        }

        FPropertyChangedEvent Evt(Prop, EPropertyChangeType::ValueSet);
        Spawned->PostEditChangeProperty(Evt);
      }
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("name"), Spawned->GetName());
    Result->SetStringField(TEXT("label"), Spawned->GetActorLabel());
    Result->SetStringField(TEXT("path"), Spawned->GetPathName());
    if (FailedProps.Num() > 0) {
      Result->SetArrayField(TEXT("failedProperties"), FailedProps);
    }
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateSpawnActorTool() {
  return MakeShared<FSpawnActorTool>();
}
} // namespace UAgent

#undef LOCTEXT_NAMESPACE
