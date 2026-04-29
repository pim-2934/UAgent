#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Misc/Optional.h"
#include "Templates/SharedPointer.h"

namespace UAgent {
/** JSON-RPC-style error payload returned by a tool. */
struct FToolError {
  int32 Code = -32000;
  FString Message;
};

/** Either Result is set (success) or Error is set (failure). */
struct FToolResponse {
  TSharedPtr<FJsonObject> Result;
  TOptional<FToolError> Error;

  static FToolResponse Ok(const TSharedRef<FJsonObject> &InResult) {
    FToolResponse R;
    R.Result = InResult;
    return R;
  }

  static FToolResponse Ok() { return Ok(MakeShared<FJsonObject>()); }

  static FToolResponse Fail(int32 Code, FString Message) {
    FToolResponse R;
    R.Error = FToolError{Code, MoveTemp(Message)};
    return R;
  }

  /** JSON-RPC "Invalid params" (-32602). */
  static FToolResponse InvalidParams(FString Message) {
    return Fail(-32602, MoveTemp(Message));
  }
};

/** Completion callback for IACPTool::ExecuteAsync. */
using FToolResponseCallback = TFunction<void(FToolResponse)>;

/**
 * Agent→client tool handler. One instance per JSON-RPC method.
 * Execute() runs on the game thread (dispatched from FACPTransport's ticker,
 * or from the HTTPServer tick when invoked via the MCP bridge).
 *
 * Most tools are synchronous: override Execute() and ignore ExecuteAsync().
 * Tools that need to wait on user input (e.g. session/request_permission in
 * Default mode) override ExecuteAsync() and call the supplied callback when
 * the response is ready. The default ExecuteAsync() implementation just
 * forwards to Execute() and invokes the callback synchronously.
 */
class IACPTool {
public:
  virtual ~IACPTool() = default;

  /** JSON-RPC method name, e.g. "fs/read_text_file" or "_ue5/read_blueprint".
   */
  virtual FString GetMethod() const = 0;

  /** Handle the request synchronously. Params may be null. Most tools override
   * this. */
  virtual FToolResponse Execute(const TSharedPtr<FJsonObject> &Params) {
    return FToolResponse::Fail(-32603, TEXT("tool has no synchronous Execute"));
  }

  /**
   * Async-capable entry point. Default forwards to Execute() and fires
   * Complete synchronously. Tools that need to defer (e.g. waiting on UI)
   * should override this, capture Complete, and invoke it later from the
   * game thread when the response is ready.
   */
  virtual void ExecuteAsync(const TSharedPtr<FJsonObject> &Params,
                            FToolResponseCallback Complete) {
    Complete(Execute(Params));
  }

  // ─── Optional MCP metadata ──────────────────────────────────────────
  // Only consumed by FMCPServer when exposing tools to external agents.
  // Tools that return an empty description *and* null schema are treated
  // as internal (e.g. ACP-native fs/* and session/* handlers) and hidden
  // from tools/list.

  /** One-line human description, used as the MCP `description` field. */
  virtual FString GetDescription() const { return FString(); }

  /** JSON Schema describing this tool's `arguments`. Null = internal/no schema.
   */
  virtual TSharedPtr<FJsonObject> GetInputSchema() const { return nullptr; }

  /**
   * True when the tool only reads or queries state — it does not create,
   * mutate, or destroy project data, levels, or assets. Drives two things:
   *   1. ReadOnly permission mode auto-allows these and rejects everything
   *      else; Default mode auto-allows these and prompts on mutations.
   *   2. The MCP server emits `annotations.readOnlyHint` in tools/list so
   *      external MCP clients that respect the spec can also classify them
   *      correctly.
   *
   * PURE VIRTUAL — every IACPTool subclass must explicitly classify itself.
   * No default; forgetting to override fails to compile, so a new tool can't
   * ship in an unclassified state and silently bypass ReadOnly mode.
   */
  virtual bool IsReadOnly() const = 0;
};

/**
 * Module-wide map of method name → tool. FACPClient routes inbound requests
 * through this; adding a new agent→client method only requires registering
 * a new IACPTool here.
 */
class FACPToolRegistry {
public:
  void Register(const TSharedRef<IACPTool> &Tool);
  void Unregister(const FString &Method);

  TSharedPtr<IACPTool> Find(const FString &Method) const;
  bool Contains(const FString &Method) const;

  /** Sorted list of all registered method names. */
  TArray<FString> GetMethodNames() const;

private:
  TMap<FString, TSharedRef<IACPTool>> Tools;
};

/** Helper for declaring JSON Schemas inline as string literals in
 * IACPTool::GetInputSchema. */
TSharedPtr<FJsonObject> ParseJsonObject(const FString &Json);
} // namespace UAgent
