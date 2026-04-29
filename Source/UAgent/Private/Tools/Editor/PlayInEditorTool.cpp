#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"

#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UObject/UObjectGlobals.h"

namespace UAgent {
namespace {
bool ParseTripleField(const TSharedPtr<FJsonObject> &Params,
                      const FString &Field, const FString &KeyA,
                      const FString &KeyB, const FString &KeyC, double &OutA,
                      double &OutB, double &OutC) {
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

bool ResolveNetMode(const FString &In, EPlayNetMode &Out, FString &OutError) {
  if (In.Equals(TEXT("standalone"), ESearchCase::IgnoreCase)) {
    Out = EPlayNetMode::PIE_Standalone;
    return true;
  }
  if (In.Equals(TEXT("listen"), ESearchCase::IgnoreCase) ||
      In.Equals(TEXT("listenserver"), ESearchCase::IgnoreCase) ||
      In.Equals(TEXT("listen_server"), ESearchCase::IgnoreCase)) {
    Out = EPlayNetMode::PIE_ListenServer;
    return true;
  }
  if (In.Equals(TEXT("client"), ESearchCase::IgnoreCase)) {
    Out = EPlayNetMode::PIE_Client;
    return true;
  }
  OutError = FString::Printf(
      TEXT("unknown netMode '%s' (expected standalone, listen, client)"), *In);
  return false;
}

class FPlayInEditorTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/play_in_editor");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT("Start Play-in-Editor. Fire-and-forget — poll read_editor_log "
                "to see when PIE is live and surface any startup errors. "
                "Optional knobs override the default play session: map, "
                "netMode, numPlayers, simulate, startLocation, startRotation. "
                "When netMode/numPlayers/separateServer are set, a duplicated "
                "ULevelEditorPlaySettings is used so the user's saved settings "
                "aren't mutated.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"map":      { "type": "string", "description": "Map asset path, e.g. '/Game/Maps/L_Test'. Overrides both offline and server maps." },
						"netMode":  { "type": "string", "enum": ["standalone", "listen", "client"], "description": "PIE net mode. Default: user's saved setting." },
						"numPlayers":      { "type": "integer", "minimum": 1, "description": "Number of PIE client windows. Default: user's saved setting." },
						"separateServer":  { "type": "boolean", "description": "Launch a separate dedicated server process alongside. Only meaningful with netMode!=standalone." },
						"simulate":        { "type": "boolean", "description": "Start a Simulate-in-Editor session instead of PIE." },
						"startLocation": {
							"description": "Player start override. {x,y,z} or 'x,y,z'.",
							"oneOf": [ { "type": "object" }, { "type": "string" } ]
						},
						"startRotation": {
							"description": "Player start rotation. {pitch,yaw,roll} or 'pitch,yaw,roll'.",
							"oneOf": [ { "type": "object" }, { "type": "string" } ]
						}
					}
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!GEditor)
      return FToolResponse::Fail(-32000, TEXT("GEditor unavailable"));

    FRequestPlaySessionParams P;

    FString Map, NetModeStr;
    bool bSimulate = false, bSeparateServer = false;
    int32 NumPlayers = 0;
    if (Params.IsValid()) {
      Params->TryGetStringField(TEXT("map"), Map);
      Params->TryGetStringField(TEXT("netMode"), NetModeStr);
      Params->TryGetBoolField(TEXT("simulate"), bSimulate);
      Params->TryGetBoolField(TEXT("separateServer"), bSeparateServer);
      Params->TryGetNumberField(TEXT("numPlayers"), NumPlayers);
    }

    if (!Map.IsEmpty()) {
      P.GlobalMapOverride = Map;
    }
    if (bSimulate) {
      P.WorldType = EPlaySessionWorldType::SimulateInEditor;
    }

    double Lx = 0.0, Ly = 0.0, Lz = 0.0;
    double Rp = 0.0, Ry = 0.0, Rr = 0.0;
    if (ParseTripleField(Params, TEXT("startLocation"), TEXT("x"), TEXT("y"),
                         TEXT("z"), Lx, Ly, Lz)) {
      P.StartLocation = FVector(Lx, Ly, Lz);
    }
    if (ParseTripleField(Params, TEXT("startRotation"), TEXT("pitch"),
                         TEXT("yaw"), TEXT("roll"), Rp, Ry, Rr)) {
      P.StartRotation = FRotator(Rp, Ry, Rr);
    }

    const bool bHasNetMode = !NetModeStr.IsEmpty();
    const bool bHasNumPlayers = NumPlayers > 0;
    const bool bHasSeparateServer =
        Params.IsValid() && Params->HasField(TEXT("separateServer"));
    if (bHasNetMode || bHasNumPlayers || bHasSeparateServer) {
      ULevelEditorPlaySettings *Settings =
          DuplicateObject<ULevelEditorPlaySettings>(
              GetDefault<ULevelEditorPlaySettings>(), GetTransientPackage());
      if (!Settings)
        return FToolResponse::Fail(
            -32000, TEXT("could not duplicate ULevelEditorPlaySettings"));

      if (bHasNetMode) {
        EPlayNetMode Mode = EPlayNetMode::PIE_Standalone;
        FString NetErr;
        if (!ResolveNetMode(NetModeStr, Mode, NetErr)) {
          return FToolResponse::InvalidParams(NetErr);
        }
        Settings->SetPlayNetMode(Mode);
      }
      if (bHasNumPlayers)
        Settings->SetPlayNumberOfClients(NumPlayers);
      if (bHasSeparateServer)
        Settings->bLaunchSeparateServer = bSeparateServer;

      P.EditorPlaySettings = Settings;
    }

    GEditor->RequestPlaySession(P);

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("queued"), true);
    if (!Map.IsEmpty())
      Result->SetStringField(TEXT("map"), Map);
    if (bHasNetMode)
      Result->SetStringField(TEXT("netMode"), NetModeStr);
    if (bHasNumPlayers)
      Result->SetNumberField(TEXT("numPlayers"), NumPlayers);
    if (bSimulate)
      Result->SetBoolField(TEXT("simulate"), true);
    return FToolResponse::Ok(Result);
  }
};

class FStopPieTool : public IACPTool {
public:
  virtual FString GetMethod() const override { return TEXT("_ue5/stop_pie"); }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT("Stop the active Play-in-Editor session.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(
        LR"JSON({ "type": "object", "properties": {} })JSON");
  }

  virtual FToolResponse Execute(const TSharedPtr<FJsonObject> &) override {
    if (!GEditor)
      return FToolResponse::Fail(-32000, TEXT("GEditor unavailable"));
    GEditor->RequestEndPlayMap();
    return FToolResponse::Ok();
  }
};
} // namespace

TSharedRef<IACPTool> CreatePlayInEditorTool() {
  return MakeShared<FPlayInEditorTool>();
}
TSharedRef<IACPTool> CreateStopPieTool() { return MakeShared<FStopPieTool>(); }
} // namespace UAgent
