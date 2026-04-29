#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/ClassResolver.h"
#include "../Common/PropertyToJson.h"

#include "UObject/Class.h"
#include "UObject/Interface.h"
#include "UObject/UnrealType.h"

namespace UAgent {
namespace {
TSharedRef<FJsonObject> FunctionToJson(const UFunction *Fn) {
  TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
  J->SetStringField(TEXT("name"), Fn->GetName());
  J->SetNumberField(TEXT("flags"), static_cast<double>(Fn->FunctionFlags));

  TArray<TSharedPtr<FJsonValue>> Params;
  TSharedPtr<FJsonObject> ReturnJson;
  for (TFieldIterator<FProperty> It(Fn); It; ++It) {
    FProperty *P = *It;
    if (!P->HasAnyPropertyFlags(CPF_Parm))
      continue;

    TSharedRef<FJsonObject> PJ = MakeShared<FJsonObject>();
    PJ->SetStringField(TEXT("name"), P->GetName());
    PJ->SetObjectField(TEXT("type"), Common::PropertyTypeToJson(P));
    PJ->SetBoolField(TEXT("out"), P->HasAnyPropertyFlags(CPF_OutParm) &&
                                      !P->HasAnyPropertyFlags(CPF_ConstParm));

    if (P->HasAnyPropertyFlags(CPF_ReturnParm))
      ReturnJson = PJ;
    else
      Params.Add(MakeShared<FJsonValueObject>(PJ));
  }
  J->SetArrayField(TEXT("params"), Params);
  if (ReturnJson)
    J->SetObjectField(TEXT("returns"), ReturnJson.ToSharedRef());
  return J;
}

class FGetClassInfoTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/get_class_info");
  }

  virtual bool IsReadOnly() const override { return true; }

  virtual FString GetDescription() const override {
    return TEXT(
        "Reflect a UClass (C++ or Blueprint) and return parent, interfaces, "
        "properties, functions, and (optionally) CDO default values. Use this "
        "instead of reading .uasset binary files. Accepts short names "
        "('Character'), script paths, or Blueprint paths.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"class": { "type": "string", "description": "Class name or path, e.g. 'Character', '/Script/Engine.Character', '/Game/Foo/BP_MyThing'" },
						"includeDefaults": { "type": "boolean", "description": "Include CDO property values (default true)" },
						"includeInherited": { "type": "boolean", "description": "Include fields inherited from parent classes (default false)" }
					},
					"required": ["class"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString ClassName;
    Params->TryGetStringField(TEXT("class"), ClassName);
    if (ClassName.IsEmpty())
      return FToolResponse::InvalidParams(TEXT("missing 'class'"));

    bool bIncludeDefaults = true, bIncludeInherited = false;
    Params->TryGetBoolField(TEXT("includeDefaults"), bIncludeDefaults);
    Params->TryGetBoolField(TEXT("includeInherited"), bIncludeInherited);

    FString Err;
    UClass *C = Common::ResolveClass(ClassName, Err);
    if (!C)
      return FToolResponse::Fail(-32000, Err);

    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("path"), C->GetPathName());
    Root->SetStringField(TEXT("name"), C->GetName());
    if (C->GetSuperClass()) {
      Root->SetStringField(TEXT("parentClass"),
                           C->GetSuperClass()->GetPathName());
    }
    Root->SetNumberField(TEXT("classFlags"),
                         static_cast<double>(C->ClassFlags));

    TArray<TSharedPtr<FJsonValue>> Interfaces;
    for (const FImplementedInterface &I : C->Interfaces) {
      if (I.Class)
        Interfaces.Add(MakeShared<FJsonValueString>(I.Class->GetPathName()));
    }
    Root->SetArrayField(TEXT("interfaces"), Interfaces);

    const UObject *CDO =
        bIncludeDefaults ? C->GetDefaultObject(false) : nullptr;

    TArray<TSharedPtr<FJsonValue>> Props;
    for (TFieldIterator<FProperty> It(
             C, bIncludeInherited ? EFieldIterationFlags::IncludeSuper
                                  : EFieldIterationFlags::None);
         It; ++It) {
      FProperty *P = *It;
      TSharedRef<FJsonObject> PJ = MakeShared<FJsonObject>();
      PJ->SetStringField(TEXT("name"), P->GetName());
      PJ->SetObjectField(TEXT("type"), Common::PropertyTypeToJson(P));
      const FString Category = P->GetMetaData(TEXT("Category"));
      if (!Category.IsEmpty())
        PJ->SetStringField(TEXT("category"), Category);
      const FString Tooltip = P->GetMetaData(TEXT("ToolTip"));
      if (!Tooltip.IsEmpty())
        PJ->SetStringField(TEXT("tooltip"), Tooltip);
      PJ->SetNumberField(TEXT("flags"), static_cast<double>(P->PropertyFlags));
      if (CDO) {
        PJ->SetField(TEXT("defaultValue"), Common::PropertyValueToJson(P, CDO));
      }
      Props.Add(MakeShared<FJsonValueObject>(PJ));
    }
    Root->SetArrayField(TEXT("properties"), Props);

    TArray<TSharedPtr<FJsonValue>> Funcs;
    for (TFieldIterator<UFunction> It(
             C, bIncludeInherited ? EFieldIterationFlags::IncludeSuper
                                  : EFieldIterationFlags::None);
         It; ++It) {
      Funcs.Add(MakeShared<FJsonValueObject>(FunctionToJson(*It)));
    }
    Root->SetArrayField(TEXT("functions"), Funcs);

    return FToolResponse::Ok(Root);
  }
};
} // namespace

TSharedRef<IACPTool> CreateGetClassInfoTool() {
  return MakeShared<FGetClassInfoTool>();
}
} // namespace UAgent
