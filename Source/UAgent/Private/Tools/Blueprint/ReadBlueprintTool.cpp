#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "BlueprintSerialization.h"

namespace UAgent {
namespace {
class FReadBlueprintTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/read_blueprint");
  }

  virtual bool IsReadOnly() const override { return true; }

  virtual FString GetDescription() const override {
    return TEXT(
        "Dump an Unreal Blueprint asset as JSON: graphs "
        "(ubergraph/function/macro), nodes, pins, and pin connections. Use "
        "this to understand a Blueprint's structure before editing. Large "
        "outputs are flagged with `truncated:true` and `originalLen`.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"assetPath": {
							"type": "string",
							"description": "Path to the Blueprint asset, e.g. /Game/MyFolder/BP_MyActor or /Game/MyFolder/BP_MyActor.BP_MyActor"
						},
						"maxChars": {
							"type": "integer",
							"description": "Maximum serialized length before a truncation marker is set. Defaults to 16000.",
							"minimum": 512
						}
					},
					"required": ["assetPath"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));

    FString AssetPath;
    Params->TryGetStringField(TEXT("assetPath"), AssetPath);
    if (AssetPath.IsEmpty()) {
      return FToolResponse::InvalidParams(TEXT("missing assetPath"));
    }

    int32 MaxChars = 0;
    Params->TryGetNumberField(TEXT("maxChars"), MaxChars);
    if (MaxChars <= 0)
      MaxChars = 16000;

    FString Err;
    TSharedPtr<FJsonObject> Dump =
        BlueprintAccess::BuildBlueprintDump(AssetPath, MaxChars, Err);
    if (!Dump.IsValid()) {
      return FToolResponse::Fail(
          -32000, Err.IsEmpty() ? TEXT("read_blueprint failed") : Err);
    }
    return FToolResponse::Ok(Dump.ToSharedRef());
  }
};
} // namespace

TSharedRef<IACPTool> CreateReadBlueprintTool() {
  return MakeShared<FReadBlueprintTool>();
}
} // namespace UAgent
