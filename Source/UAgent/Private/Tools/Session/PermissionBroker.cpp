#include "PermissionBroker.h"

#include "../../Protocol/ACPTypes.h"

namespace UAgent {
FPermissionBroker &FPermissionBroker::Get() {
  static FPermissionBroker Instance;
  return Instance;
}

void FPermissionBroker::SetHandler(FHandler InHandler) {
  Handler = MoveTemp(InHandler);
}

void FPermissionBroker::Request(const FPermissionRequest &Req,
                                TFunction<void(EPermissionOutcome)> Complete) {
  if (!Handler) {
    UE_LOG(LogUAgent, Warning,
           TEXT("PermissionBroker: no handler installed — auto-denying "
                "request for tool '%s'."),
           *Req.ToolTitle);
    if (Complete)
      Complete(EPermissionOutcome::Deny);
    return;
  }
  Handler(Req, MoveTemp(Complete));
}
} // namespace UAgent
