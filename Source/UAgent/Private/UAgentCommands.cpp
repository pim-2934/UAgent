#include "UAgentCommands.h"

#define LOCTEXT_NAMESPACE "FUAgentModule"

void FUAgentCommands::RegisterCommands() {
  UI_COMMAND(OpenPluginWindow, "UAgent", "Bring up UAgent window",
             EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
