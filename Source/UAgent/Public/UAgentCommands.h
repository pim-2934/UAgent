#pragma once

#include "Framework/Commands/Commands.h"
#include "UAgentStyle.h"

class FUAgentCommands : public TCommands<FUAgentCommands> {
public:
  FUAgentCommands()
      : TCommands<FUAgentCommands>(
            TEXT("UAgent"), NSLOCTEXT("Contexts", "UAgent", "UAgent Plugin"),
            NAME_None, FUAgentStyle::GetStyleSetName()) {}

  // TCommands<> interface
  virtual void RegisterCommands() override;

public:
  TSharedPtr<FUICommandInfo> OpenPluginWindow;
};