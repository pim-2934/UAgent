#include "PropertyToJson.h"

#include "JsonObjectConverter.h"
#include "UObject/Class.h"
#include "UObject/EnumProperty.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"

namespace UAgent::Common {
namespace {
// CPF_Deprecated is skipped so agents don't see fields the engine hides from
// users.
constexpr uint64 kSkipFlags = CPF_Deprecated | CPF_Transient;
} // namespace

TSharedPtr<FJsonValue> PropertyValueToJson(const FProperty *Prop,
                                           const void *Container) {
  if (!Prop || !Container)
    return MakeShared<FJsonValueNull>();

  const void *Value = Prop->ContainerPtrToValuePtr<void>(Container);
  TSharedPtr<FJsonValue> Out = FJsonObjectConverter::UPropertyToJsonValue(
      const_cast<FProperty *>(Prop), Value, /*CheckFlags=*/0,
      /*SkipFlags=*/kSkipFlags);
  return Out.IsValid() ? Out : MakeShared<FJsonValueNull>();
}

TSharedRef<FJsonObject> PropertiesToJsonObject(const UStruct *Struct,
                                               const void *Container) {
  TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
  if (!Struct || !Container)
    return Out;
  FJsonObjectConverter::UStructToJsonObject(
      Struct, Container, Out, /*CheckFlags=*/0, /*SkipFlags=*/kSkipFlags);
  return Out;
}

TSharedRef<FJsonObject> PropertyTypeToJson(const FProperty *Prop) {
  TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
  if (!Prop) {
    J->SetStringField(TEXT("category"), TEXT("none"));
    return J;
  }

  J->SetStringField(TEXT("cpp"), Prop->GetCPPType());
  J->SetStringField(TEXT("class"), Prop->GetClass()->GetName());

  if (const FObjectPropertyBase *Obj = CastField<FObjectPropertyBase>(Prop)) {
    if (Obj->PropertyClass) {
      J->SetStringField(TEXT("objectClass"), Obj->PropertyClass->GetPathName());
    }
  } else if (const FStructProperty *Struct = CastField<FStructProperty>(Prop)) {
    if (Struct->Struct) {
      J->SetStringField(TEXT("struct"), Struct->Struct->GetPathName());
    }
  } else if (const FEnumProperty *Enum = CastField<FEnumProperty>(Prop)) {
    if (Enum->GetEnum()) {
      J->SetStringField(TEXT("enum"), Enum->GetEnum()->GetPathName());
    }
  } else if (const FByteProperty *Byte = CastField<FByteProperty>(Prop)) {
    if (Byte->Enum) {
      J->SetStringField(TEXT("enum"), Byte->Enum->GetPathName());
    }
  } else if (const FArrayProperty *Arr = CastField<FArrayProperty>(Prop)) {
    J->SetObjectField(TEXT("inner"), PropertyTypeToJson(Arr->Inner));
    J->SetBoolField(TEXT("isArray"), true);
  } else if (const FSetProperty *Set = CastField<FSetProperty>(Prop)) {
    J->SetObjectField(TEXT("inner"), PropertyTypeToJson(Set->ElementProp));
    J->SetBoolField(TEXT("isSet"), true);
  } else if (const FMapProperty *Map = CastField<FMapProperty>(Prop)) {
    J->SetObjectField(TEXT("key"), PropertyTypeToJson(Map->KeyProp));
    J->SetObjectField(TEXT("value"), PropertyTypeToJson(Map->ValueProp));
    J->SetBoolField(TEXT("isMap"), true);
  }

  return J;
}
} // namespace UAgent::Common
