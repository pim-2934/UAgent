#include "ProposalBroker.h"

#include "../../Protocol/ACPTypes.h"

namespace UAgent {
FProposalBroker &FProposalBroker::Get() {
  static FProposalBroker Instance;
  return Instance;
}

void FProposalBroker::SetHandler(FHandler InHandler) {
  Handler = MoveTemp(InHandler);
}

void FProposalBroker::Request(const FProposalRequest &Req,
                              TFunction<void(EProposalOutcome)> Complete) {
  if (bProposedThisTurn) {
    UE_LOG(
        LogUAgent, Warning,
        TEXT("ProposalBroker: agent attempted to propose a second tool ('%s')"
             " in the same turn — auto-cancelling. The standing instruction"
             " forbids more than one proposal per turn."),
        *Req.Name);
    if (Complete)
      Complete(EProposalOutcome::Cancelled);
    return;
  }

  if (!Handler) {
    UE_LOG(LogUAgent, Warning,
           TEXT("ProposalBroker: no handler installed — auto-cancelling "
                "proposal for tool '%s'."),
           *Req.Name);
    if (Complete)
      Complete(EProposalOutcome::Cancelled);
    return;
  }

  bProposedThisTurn = true;
  Handler(Req, MoveTemp(Complete));
}

void FProposalBroker::ResetTurn() { bProposedThisTurn = false; }
} // namespace UAgent
