#include "ACPToolRegistry.h"
#include "ACPTypes.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UAgent {
TSharedPtr<FJsonObject> ParseJsonObject(const FString &Json) {
  TSharedPtr<FJsonObject> Out;
  const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
  if (!FJsonSerializer::Deserialize(Reader, Out) || !Out.IsValid()) {
    UE_LOG(LogUAgent, Error, TEXT("ParseJsonObject: failed to parse: %s"),
           *Json);
  }
  return Out;
}

void FACPToolRegistry::Register(const TSharedRef<IACPTool> &Tool) {
  const FString Method = Tool->GetMethod();
  if (Method.IsEmpty()) {
    UE_LOG(LogUAgent, Error,
           TEXT("FACPToolRegistry::Register: tool returned empty method name"));
    return;
  }
  if (Tools.Contains(Method)) {
    UE_LOG(
        LogUAgent, Warning,
        TEXT("FACPToolRegistry::Register: replacing existing handler for '%s'"),
        *Method);
  }
  Tools.Add(Method, Tool);
}

void FACPToolRegistry::Unregister(const FString &Method) {
  Tools.Remove(Method);
}

TSharedPtr<IACPTool> FACPToolRegistry::Find(const FString &Method) const {
  if (const TSharedRef<IACPTool> *Found = Tools.Find(Method)) {
    return *Found;
  }
  return nullptr;
}

bool FACPToolRegistry::Contains(const FString &Method) const {
  return Tools.Contains(Method);
}

TArray<FString> FACPToolRegistry::GetMethodNames() const {
  TArray<FString> Out;
  Tools.GenerateKeyArray(Out);
  Out.Sort();
  return Out;
}
} // namespace UAgent
