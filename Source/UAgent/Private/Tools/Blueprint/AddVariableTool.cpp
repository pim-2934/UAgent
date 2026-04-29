#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"
#include "../Common/ClassResolver.h"
#include "BlueprintQueries.h"

#include "EdGraphSchema_K2.h"
#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/UserDefinedStruct.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "UAgent"

namespace UAgent {
namespace {
// Map a user-facing type token to an FEdGraphPinType.
// Forms: "bool", "int", "float", "double", "string", "name", "text", "byte",
//        "object:/Script/Module.Class", "class:/Script/Module.Class",
//        "struct:/Script/Module.StructName", "enum:/Script/Module.EnumName"
// Prefix with "array:" or "set:" or "map:<keyType>:" to wrap.
bool BuildPinType(const FString &Spec, FEdGraphPinType &Out, FString &Err) {
  FString Body = Spec;
  if (Body.StartsWith(TEXT("array:"))) {
    Out.ContainerType = EPinContainerType::Array;
    Body.RightChopInline(6);
  } else if (Body.StartsWith(TEXT("set:"))) {
    Out.ContainerType = EPinContainerType::Set;
    Body.RightChopInline(4);
  }

  if (Body == TEXT("bool")) {
    Out.PinCategory = UEdGraphSchema_K2::PC_Boolean;
    return true;
  }
  if (Body == TEXT("int") || Body == TEXT("int32")) {
    Out.PinCategory = UEdGraphSchema_K2::PC_Int;
    return true;
  }
  if (Body == TEXT("int64")) {
    Out.PinCategory = UEdGraphSchema_K2::PC_Int64;
    return true;
  }
  if (Body == TEXT("float")) {
    Out.PinCategory = UEdGraphSchema_K2::PC_Real;
    Out.PinSubCategory = UEdGraphSchema_K2::PC_Float;
    return true;
  }
  if (Body == TEXT("double")) {
    Out.PinCategory = UEdGraphSchema_K2::PC_Real;
    Out.PinSubCategory = UEdGraphSchema_K2::PC_Double;
    return true;
  }
  if (Body == TEXT("string")) {
    Out.PinCategory = UEdGraphSchema_K2::PC_String;
    return true;
  }
  if (Body == TEXT("name")) {
    Out.PinCategory = UEdGraphSchema_K2::PC_Name;
    return true;
  }
  if (Body == TEXT("text")) {
    Out.PinCategory = UEdGraphSchema_K2::PC_Text;
    return true;
  }
  if (Body == TEXT("byte")) {
    Out.PinCategory = UEdGraphSchema_K2::PC_Byte;
    return true;
  }

  auto SplitAfter = [](const FString &Prefix, const FString &S) {
    return S.RightChop(Prefix.Len());
  };

  if (Body.StartsWith(TEXT("object:"))) {
    UClass *C = Common::ResolveClass(SplitAfter(TEXT("object:"), Body), Err);
    if (!C)
      return false;
    Out.PinCategory = UEdGraphSchema_K2::PC_Object;
    Out.PinSubCategoryObject = C;
    return true;
  }
  if (Body.StartsWith(TEXT("class:"))) {
    UClass *C = Common::ResolveClass(SplitAfter(TEXT("class:"), Body), Err);
    if (!C)
      return false;
    Out.PinCategory = UEdGraphSchema_K2::PC_Class;
    Out.PinSubCategoryObject = C;
    return true;
  }
  if (Body.StartsWith(TEXT("struct:"))) {
    const FString Path = SplitAfter(TEXT("struct:"), Body);
    UScriptStruct *S = LoadObject<UScriptStruct>(nullptr, *Path);
    if (!S) {
      Err = FString::Printf(TEXT("struct not found: %s"), *Path);
      return false;
    }
    Out.PinCategory = UEdGraphSchema_K2::PC_Struct;
    Out.PinSubCategoryObject = S;
    return true;
  }
  if (Body.StartsWith(TEXT("enum:"))) {
    const FString Path = SplitAfter(TEXT("enum:"), Body);
    UEnum *E = LoadObject<UEnum>(nullptr, *Path);
    if (!E) {
      Err = FString::Printf(TEXT("enum not found: %s"), *Path);
      return false;
    }
    Out.PinCategory = UEdGraphSchema_K2::PC_Byte;
    Out.PinSubCategoryObject = E;
    return true;
  }

  Err = FString::Printf(TEXT("unknown type spec: %s"), *Spec);
  return false;
}

class FAddVariableTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/add_variable");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT("Add a member variable to a Blueprint. Type grammar: 'bool', "
                "'int', 'float', 'string', 'name', 'object:<Class>', "
                "'class:<Class>', 'struct:<path>', 'enum:<path>'. Prefix with "
                "'array:' or 'set:' for containers.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"blueprintPath": { "type": "string" },
						"name": { "type": "string" },
						"type": { "type": "string", "description": "Type spec (see description)" },
						"defaultValue": { "type": "string", "description": "Default value as FProperty ImportText form" },
						"category": { "type": "string" },
						"saveAsset": { "type": "boolean" }
					},
					"required": ["blueprintPath", "name", "type"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString BPPath, Name, TypeSpec, DefaultValue, Category;
    Params->TryGetStringField(TEXT("blueprintPath"), BPPath);
    Params->TryGetStringField(TEXT("name"), Name);
    Params->TryGetStringField(TEXT("type"), TypeSpec);
    Params->TryGetStringField(TEXT("defaultValue"), DefaultValue);
    Params->TryGetStringField(TEXT("category"), Category);
    if (BPPath.IsEmpty() || Name.IsEmpty() || TypeSpec.IsEmpty()) {
      return FToolResponse::InvalidParams(
          TEXT("blueprintPath, name, type required"));
    }
    bool bSave = true;
    Params->TryGetBoolField(TEXT("saveAsset"), bSave);

    FString Err;
    UBlueprint *BP = BlueprintAccess::LoadBlueprintByPath(BPPath, Err);
    if (!BP)
      return FToolResponse::Fail(-32000, Err);

    FEdGraphPinType PinType;
    if (!BuildPinType(TypeSpec, PinType, Err))
      return FToolResponse::Fail(-32000, Err);

    const FScopedTransaction Tx(LOCTEXT("AddVarTx", "Add Blueprint Variable"));
    BP->Modify();

    const bool bOk = FBlueprintEditorUtils::AddMemberVariable(
        BP, FName(*Name), PinType, DefaultValue);
    if (!bOk)
      return FToolResponse::Fail(
          -32000,
          TEXT("AddMemberVariable failed (variable may already exist)"));

    if (!Category.IsEmpty()) {
      FBlueprintEditorUtils::SetBlueprintVariableCategory(
          BP, FName(*Name), nullptr, FText::FromString(Category));
    }

    FKismetEditorUtilities::CompileBlueprint(BP);
    if (bSave) {
      FString PkgPath, PkgName, PkgErr;
      Common::SplitContentPath(BP->GetPathName(), PkgPath, PkgName, PkgErr);
      UEditorAssetLibrary::SaveAsset(PkgPath, /*bOnlyIfIsDirty=*/false);
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("name"), Name);
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateAddVariableTool() {
  return MakeShared<FAddVariableTool>();
}
} // namespace UAgent

#undef LOCTEXT_NAMESPACE
