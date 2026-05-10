#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/DeveloperGate.h"
#include "ProposalBroker.h"
#include "UAgent.h"

#include "Internationalization/Regex.h"
#include "Misc/Guid.h"
#include "Modules/ModuleManager.h"

namespace UAgent {
namespace {
// Snake_case, 3–49 chars, leading lowercase letter. Matches the project's
// existing tool-naming convention (read_blueprint, set_component_property,
// list_assets) and rules out names that would collide with ACP/MCP method
// syntax or be hard to read in tools/list.
static const TCHAR *ToolNamePattern = TEXT("^[a-z][a-z0-9_]{2,48}$");

// JSON-RPC-side check: does the registry already expose this tool? If so we
// reject the proposal with InvalidParams; the agent should call the existing
// tool instead of asking us to invent it.
bool IsNameAlreadyRegistered(const FString &SnakeName) {
  if (SnakeName.IsEmpty())
    return false;
  FUAgentModule *Module =
      FModuleManager::GetModulePtr<FUAgentModule>(TEXT("UAgent"));
  if (!Module)
    return false;
  const TSharedPtr<FACPToolRegistry> Reg = Module->GetToolRegistry();
  if (!Reg.IsValid())
    return false;
  return Reg->Contains(FString(TEXT("_ue5/")) + SnakeName);
}

class FProposeMissingToolTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/propose_missing_tool");
  }

  // Read-only: the tool writes a sidecar JSON under Saved/UAgent/Proposals,
  // which is developer-machine scratch (same category as capture_viewport's
  // PNGs being acceptable for read-only adjacents). It mutates no project
  // data. Marking it read-only also keeps Default permission mode from
  // double-prompting on top of the proposal card itself.
  virtual bool IsReadOnly() const override { return true; }

  virtual FString GetDescription() const override {
    return TEXT(
        "Surface a clearly-missing UE5 editor tool. Call ONLY when none of "
        "the registered tools plausibly cover the user's intent and "
        "improvising would produce a worse outcome than halting. After "
        "calling, stop — do not call alternative tools. The developer will "
        "implement the proposed tool and the user will replay the prompt. "
        "If the result includes halt:true, end the turn immediately. Do not "
        "call this more than once per turn.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
      "type": "object",
      "required": ["name", "description", "whyNeeded", "inputSchema"],
      "properties": {
        "name": {
          "type": "string",
          "description": "Snake_case tool name following UAgent conventions, e.g. 'set_landscape_layer_weight'. 3–49 chars, lowercase letters / digits / underscores, leading letter. Must not collide with an existing tool."
        },
        "description": {
          "type": "string",
          "description": "One-line human description in the shape of IACPTool::GetDescription. Aim for 80–160 chars."
        },
        "whyNeeded": {
          "type": "string",
          "description": "What the user asked for, why no existing tool fits, and what improvising around the gap would look like. The developer reads this to decide whether to implement."
        },
        "inputSchema": {
          "type": "object",
          "description": "Proposed JSON Schema for the new tool's arguments — same shape IACPTool::GetInputSchema would return. Must be a valid JSON Schema object with type:object."
        },
        "exampleCall": {
          "type": "object",
          "description": "An example arguments object the agent would pass on the next turn, conforming to inputSchema. Used by the developer as a concrete test case.",
          "additionalProperties": true
        },
        "isReadOnly": {
          "type": "boolean",
          "description": "Proposed IsReadOnly() classification. The developer can override.",
          "default": false
        }
      }
    })JSON");
  }

  virtual void ExecuteAsync(const TSharedPtr<FJsonObject> &Params,
                            FToolResponseCallback Complete) override {
    // The registration gate already ensures we only run when developer
    // tooling is enabled, but a stale module-level registration could in
    // principle leak through after a setting flip. Re-check defensively.
    if (!Common::IsDeveloperToolingEnabled()) {
      Complete(FToolResponse::Fail(
          -32601,
          TEXT("propose_missing_tool: developer tooling is disabled")));
      return;
    }

    if (!Params.IsValid()) {
      Complete(FToolResponse::InvalidParams(
          TEXT("propose_missing_tool: missing params")));
      return;
    }

    FString Name, Description, WhyNeeded;
    if (!Params->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty()) {
      Complete(FToolResponse::InvalidParams(
          TEXT("propose_missing_tool: 'name' is required")));
      return;
    }
    if (!Params->TryGetStringField(TEXT("description"), Description) ||
        Description.Len() < 16) {
      Complete(FToolResponse::InvalidParams(
          TEXT("propose_missing_tool: 'description' is required (>=16 chars)")));
      return;
    }
    if (!Params->TryGetStringField(TEXT("whyNeeded"), WhyNeeded) ||
        WhyNeeded.Len() < 16) {
      Complete(FToolResponse::InvalidParams(
          TEXT("propose_missing_tool: 'whyNeeded' is required (>=16 chars) — "
               "explain what the user wanted, why no existing tool fits, and "
               "what improvising would look like")));
      return;
    }

    {
      const FRegexPattern Pattern(ToolNamePattern);
      FRegexMatcher Matcher(Pattern, Name);
      if (!Matcher.FindNext()) {
        Complete(FToolResponse::InvalidParams(FString::Printf(
            TEXT("propose_missing_tool: 'name' must match %s (got '%s')"),
            ToolNamePattern, *Name)));
        return;
      }
    }

    if (IsNameAlreadyRegistered(Name)) {
      Complete(FToolResponse::InvalidParams(FString::Printf(
          TEXT("propose_missing_tool: '%s' is already registered — call it "
               "directly via _ue5/%s instead of proposing it"),
          *Name, *Name)));
      return;
    }

    const TSharedPtr<FJsonObject> *InputSchemaPtr = nullptr;
    if (!Params->TryGetObjectField(TEXT("inputSchema"), InputSchemaPtr) ||
        !InputSchemaPtr || !InputSchemaPtr->IsValid()) {
      Complete(FToolResponse::InvalidParams(
          TEXT("propose_missing_tool: 'inputSchema' is required and must be "
               "an object")));
      return;
    }
    {
      FString SchemaType;
      (*InputSchemaPtr)->TryGetStringField(TEXT("type"), SchemaType);
      if (!SchemaType.Equals(TEXT("object"))) {
        Complete(FToolResponse::InvalidParams(
            TEXT("propose_missing_tool: 'inputSchema.type' must be \"object\"")));
        return;
      }
    }

    TSharedPtr<FJsonObject> ExampleCall;
    {
      const TSharedPtr<FJsonObject> *ExamplePtr = nullptr;
      if (Params->TryGetObjectField(TEXT("exampleCall"), ExamplePtr) &&
          ExamplePtr && ExamplePtr->IsValid()) {
        ExampleCall = *ExamplePtr;
      }
    }

    bool bIsReadOnly = false;
    Params->TryGetBoolField(TEXT("isReadOnly"), bIsReadOnly);

    FProposalRequest Req;
    Req.Id = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
    Req.Name = MoveTemp(Name);
    Req.Description = MoveTemp(Description);
    Req.WhyNeeded = MoveTemp(WhyNeeded);
    Req.InputSchema = *InputSchemaPtr;
    Req.ExampleCall = ExampleCall;
    Req.bIsReadOnly = bIsReadOnly;

    const FString ProposalId = Req.Id;
    FProposalBroker::Get().Request(
        Req, [Complete = MoveTemp(Complete),
              ProposalId](EProposalOutcome Outcome) mutable {
          TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
          R->SetStringField(TEXT("proposalId"), ProposalId);
          switch (Outcome) {
          case EProposalOutcome::Accepted:
            R->SetBoolField(TEXT("halt"), true);
            R->SetStringField(TEXT("status"), TEXT("proposal_accepted"));
            R->SetStringField(
                TEXT("message"),
                TEXT("Proposal recorded. The developer will implement this "
                     "tool and the user will replay their prompt. STOP — do "
                     "not call any further tools or attempt to improvise."));
            break;
          case EProposalOutcome::Skipped:
            R->SetBoolField(TEXT("halt"), false);
            R->SetStringField(TEXT("status"), TEXT("proposal_skipped"));
            R->SetStringField(
                TEXT("message"),
                TEXT("Developer declined the proposal. Continue with the "
                     "existing registered tools."));
            break;
          case EProposalOutcome::Cancelled:
          default:
            R->SetBoolField(TEXT("halt"), true);
            R->SetStringField(TEXT("status"), TEXT("proposal_cancelled"));
            R->SetStringField(
                TEXT("message"),
                TEXT("Developer cancelled the proposal. Stop and wait for "
                     "instructions."));
            break;
          }
          Complete(FToolResponse::Ok(R));
        });
  }
};
} // namespace

TSharedRef<IACPTool> CreateProposeMissingToolTool() {
  return MakeShared<FProposeMissingToolTool>();
}
} // namespace UAgent
