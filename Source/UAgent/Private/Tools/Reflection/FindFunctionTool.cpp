#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/ClassResolver.h"

#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace UAgent {
namespace {
FString BuildSignature(const UFunction *Fn) {
  TStringBuilder<256> Sig;
  Sig << Fn->GetName() << TEXT("(");
  bool bFirst = true;
  FProperty *RetProp = nullptr;
  for (TFieldIterator<FProperty> It(Fn); It; ++It) {
    FProperty *P = *It;
    if (!P->HasAnyPropertyFlags(CPF_Parm))
      continue;
    if (P->HasAnyPropertyFlags(CPF_ReturnParm)) {
      RetProp = P;
      continue;
    }
    if (!bFirst)
      Sig << TEXT(", ");
    bFirst = false;
    if (P->HasAnyPropertyFlags(CPF_OutParm) &&
        !P->HasAnyPropertyFlags(CPF_ConstParm)) {
      Sig << TEXT("out ");
    }
    Sig << P->GetName() << TEXT(": ") << P->GetCPPType();
  }
  Sig << TEXT(")");
  if (RetProp)
    Sig << TEXT(" -> ") << RetProp->GetCPPType();
  return FString(Sig.ToString());
}

TSharedRef<FJsonObject> HitToJson(const UClass *C, const UFunction *Fn) {
  TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
  J->SetStringField(TEXT("class"), C->GetName());
  J->SetStringField(TEXT("classPath"), C->GetPathName());
  J->SetStringField(TEXT("name"), Fn->GetName());
  J->SetStringField(TEXT("signature"), BuildSignature(Fn));
  J->SetNumberField(TEXT("flags"), static_cast<double>(Fn->FunctionFlags));
  const FString Tooltip = Fn->GetMetaData(TEXT("ToolTip"));
  if (!Tooltip.IsEmpty())
    J->SetStringField(TEXT("tooltip"), Tooltip);
  const FString Category = Fn->GetMetaData(TEXT("Category"));
  if (!Category.IsEmpty())
    J->SetStringField(TEXT("category"), Category);
  return J;
}

class FFindFunctionTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/find_function");
  }

  virtual bool IsReadOnly() const override { return true; }

  virtual FString GetDescription() const override {
    return TEXT("Search UFunctions by name substring. Omit 'class' to scan "
                "every loaded UClass — useful when you don't know where a "
                "Blueprint-callable function lives (e.g. Multiply_DoubleDouble "
                "on UKismetMathLibrary). Returns class, name, signature, and "
                "metadata. Results are deduplicated per declaring class "
                "(inherited functions are not re-listed).");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"nameContains": { "type": "string", "description": "Case-insensitive substring matched against the function name." },
						"class": { "type": "string", "description": "Optional. Restrict the search to this class (and its inherited members)." },
						"onlyBlueprintCallable": { "type": "boolean", "description": "Default true. When true, filters to FUNC_BlueprintCallable | FUNC_BlueprintPure | FUNC_BlueprintEvent." },
						"limit": { "type": "integer", "minimum": 1 }
					},
					"required": ["nameContains"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString NameContains, ClassName;
    Params->TryGetStringField(TEXT("nameContains"), NameContains);
    Params->TryGetStringField(TEXT("class"), ClassName);
    if (NameContains.IsEmpty()) {
      return FToolResponse::InvalidParams(TEXT("nameContains required"));
    }
    bool bOnlyBP = true;
    Params->TryGetBoolField(TEXT("onlyBlueprintCallable"), bOnlyBP);
    int32 Limit = 50;
    Params->TryGetNumberField(TEXT("limit"), Limit);
    if (Limit <= 0)
      Limit = 50;

    const EFunctionFlags BpMask =
        FUNC_BlueprintCallable | FUNC_BlueprintPure | FUNC_BlueprintEvent;

    auto MatchesFilter = [&](const UFunction *Fn) -> bool {
      if (!Fn->GetName().Contains(NameContains, ESearchCase::IgnoreCase))
        return false;
      if (bOnlyBP && !Fn->HasAnyFunctionFlags(BpMask))
        return false;
      return true;
    };

    TArray<TSharedPtr<FJsonValue>> Hits;
    int32 Scanned = 0;
    bool bTruncated = false;

    auto AddHit = [&](const UClass *C, const UFunction *Fn) -> bool {
      if (Hits.Num() >= Limit) {
        bTruncated = true;
        return false;
      }
      Hits.Add(MakeShared<FJsonValueObject>(HitToJson(C, Fn)));
      return true;
    };

    if (!ClassName.IsEmpty()) {
      FString Err;
      UClass *C = Common::ResolveClass(ClassName, Err);
      if (!C)
        return FToolResponse::Fail(-32000, Err);

      // Walk with IncludeSuper so a narrow query still finds inherited helpers.
      for (TFieldIterator<UFunction> It(C, EFieldIterationFlags::IncludeSuper);
           It; ++It) {
        ++Scanned;
        UFunction *Fn = *It;
        if (!MatchesFilter(Fn))
          continue;
        if (!AddHit(Fn->GetOwnerClass(), Fn))
          break;
      }
    } else {
      // Cross-class search. Iterate every loaded UClass and look at its
      // directly-owned functions only — inherited functions surface on the
      // class that declares them.
      for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt) {
        UClass *C = *ClassIt;
        if (!C || C->HasAnyClassFlags(CLASS_NewerVersionExists))
          continue;

        for (TFieldIterator<UFunction> It(C, EFieldIterationFlags::None); It;
             ++It) {
          ++Scanned;
          UFunction *Fn = *It;
          if (!MatchesFilter(Fn))
            continue;
          if (!AddHit(C, Fn))
            break;
        }
        if (bTruncated)
          break;
      }
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("functions"), Hits);
    Result->SetNumberField(TEXT("scanned"), Scanned);
    Result->SetBoolField(TEXT("truncated"), bTruncated);
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateFindFunctionTool() {
  return MakeShared<FFindFunctionTool>();
}
} // namespace UAgent
