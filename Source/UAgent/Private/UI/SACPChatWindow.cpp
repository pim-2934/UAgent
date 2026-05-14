#include "SACPChatWindow.h"

#include "AssetContextRegistry.h"
#include "ChatMessageLog.h"
#include "Protocol/ACPClient.h"
#include "Protocol/ACPTypes.h"
#include "SACPContextStrip.h"
#include "SACPMentionPicker.h"
#include "SACPMessageList.h"
#include "Tools/Common/DeveloperGate.h"
#include "Tools/Developer/ProposalBroker.h"
#include "Tools/Session/PermissionBroker.h"
#include "UAgent.h"
#include "UAgentSettings.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/Guid.h"
#include "Serialization/JsonReader.h"

#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "UAgent"

namespace {
/** Stable backing for one agent-config-option SComboBox. Slate holds its
 * OptionsSource by raw pointer, so the labels TArray must outlive the combo —
 * we keep the state on the heap and let the combo's lambdas keep it alive
 * via shared-ref capture. */
struct FAgentDropdownState {
  TArray<TSharedPtr<FString>> Labels;
  TArray<FString> Values;
};
} // namespace

// ─── Construction ───────────────────────────────────────────────────────────

void SACPChatWindow::Construct(const FArguments &InArgs) {
  MessageLog = MakeShared<FChatMessageLog>();

  Client = MakeShared<UAgent::FACPClient>();
  Client->SetToolRegistry(
      FModuleManager::LoadModuleChecked<FUAgentModule>("UAgent")
          .GetToolRegistry());
  Client->OnStateChanged.AddSP(this, &SACPChatWindow::OnClientStateChanged);
  Client->OnSessionUpdate.AddSP(this, &SACPChatWindow::OnSessionUpdateReceived);
  Client->OnPromptCompleted.AddSP(this, &SACPChatWindow::OnPromptDone);
  Client->OnError.AddSP(this, &SACPChatWindow::OnClientError);
  Client->OnAgentSettingsChanged.AddSP(this,
                                       &SACPChatWindow::RefreshAgentSettings);

  SAssignNew(ContextStrip, SACPContextStrip)
      .OnAddClicked_Lambda([this]() {
        if (MentionPicker.IsValid()) {
          PendingAtPosition = INDEX_NONE;
          MentionPicker->RefreshResults(FString());
          MentionPicker->Open();
        }
      })
      .OnChipRemoved_Lambda(
          [this](const FAssetData &A) { RemoveContextChip(A); });

  SAssignNew(MessageList, SACPMessageList, MessageLog.ToSharedRef())
      .OnPermissionDecided(FOnPermissionDecided::CreateSP(
          this, &SACPChatWindow::OnPermissionRowDecided))
      .OnProposalDecided(FOnProposalDecided::CreateSP(
          this, &SACPChatWindow::OnProposalRowDecided))
      .OnProposalReplayDecided(FOnProposalReplayDecided::CreateSP(
          this, &SACPChatWindow::OnProposalReplayDecided));

  // Route the agent's permission requests through the chat UI. Unbound on
  // teardown so a closed tab can't dangle the agent.
  UAgent::FPermissionBroker::Get().SetHandler(
      [WeakSelf = TWeakPtr<SACPChatWindow>(SharedThis(this))](
          const UAgent::FPermissionRequest &Req,
          TFunction<void(UAgent::EPermissionOutcome)> Complete) {
        if (TSharedPtr<SACPChatWindow> Self = WeakSelf.Pin()) {
          Self->OnPermissionRequested(Req, MoveTemp(Complete));
        } else if (Complete) {
          // Tab gone — deny rather than silently approving.
          Complete(UAgent::EPermissionOutcome::Deny);
        }
      });

  // Same shape for proposals — installed unconditionally so a flip of
  // bDeveloperMode mid-session doesn't leave a broker without a handler.
  // The tool itself is registration-gated, so when the gate is closed the
  // broker is never asked.
  UAgent::FProposalBroker::Get().SetHandler(
      [WeakSelf = TWeakPtr<SACPChatWindow>(SharedThis(this))](
          const UAgent::FProposalRequest &Req,
          TFunction<void(UAgent::EProposalOutcome)> Complete) {
        if (TSharedPtr<SACPChatWindow> Self = WeakSelf.Pin()) {
          Self->OnProposalRequested(Req, MoveTemp(Complete));
        } else if (Complete) {
          Complete(UAgent::EProposalOutcome::Cancelled);
        }
      });

  ChildSlot[SNew(SVerticalBox) +
            SVerticalBox::Slot().AutoHeight()[BuildHeader()] +
            SVerticalBox::Slot().AutoHeight()[SNew(SSeparator)] +
            SVerticalBox::Slot().FillHeight(1.0f)[MessageList.ToSharedRef()] +
            SVerticalBox::Slot().AutoHeight()[SNew(SSeparator)] +
            SVerticalBox::Slot().AutoHeight()[ContextStrip.ToSharedRef()] +
            SVerticalBox::Slot().AutoHeight()[BuildInputRow()] +
            SVerticalBox::Slot().AutoHeight()[BuildPermissionModeRow()]];

  RebuildContextStrip();

  StartSession();
}

SACPChatWindow::~SACPChatWindow() {
  // Drop the broker handlers first so a destruction-time agent request can't
  // re-enter into a half-destroyed window.
  UAgent::FPermissionBroker::Get().SetHandler({});
  UAgent::FProposalBroker::Get().SetHandler({});

  // Anything still pending is left unanswered — the agent treats no response
  // as cancelled when the transport dies, but be explicit so we never leak.
  for (auto &KV : PendingPermissions) {
    if (KV.Value)
      KV.Value(UAgent::EPermissionOutcome::Deny);
  }
  PendingPermissions.Reset();

  for (auto &KV : PendingProposals) {
    if (KV.Value.Complete)
      KV.Value.Complete(UAgent::EProposalOutcome::Cancelled);
  }
  PendingProposals.Reset();

  if (Client.IsValid()) {
    Client->Stop();
  }
}

TSharedRef<SWidget> SACPChatWindow::BuildHeader() {
  return SNew(SBorder)
      .BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
      .Padding(FMargin(6))
          [SNew(SHorizontalBox) +
           SHorizontalBox::Slot()
               .AutoWidth()
               .VAlign(VAlign_Center)
               .Padding(0, 0, 8, 0)[SNew(STextBlock)
                                        .Text(LOCTEXT("Title", "UAgent"))
                                        .Font(FAppStyle::Get().GetFontStyle(
                                            TEXT("HeadingSmall")))] +
           SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
               [SAssignNew(StatusLabel, STextBlock)
                    .Text_Lambda([this] { return GetStatusText(); })
                    .ColorAndOpacity(FLinearColor(0.65f, 0.65f, 0.65f, 1.0f))] +
           SHorizontalBox::Slot().AutoWidth().Padding(
               4,
               0)[SNew(SButton)
                      .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                      .ContentPadding(FMargin(0))
                      .HAlign(HAlign_Center)
                      .VAlign(VAlign_Center)
                      .ToolTipText(LOCTEXT("NewSessionTip", "New Session"))
                      .OnClicked(this, &SACPChatWindow::OnNewSessionClicked)
                          [SNew(SBox)
                               .WidthOverride(24.f)
                               .HeightOverride(24.f)
                               .HAlign(HAlign_Center)
                               .VAlign(VAlign_Center)
                                   [SNew(SImage)
                                        .Image(FAppStyle::Get().GetBrush(
                                            "Icons.Plus"))
                                        .DesiredSizeOverride(FVector2D(16, 16))
                                        .ColorAndOpacity(
                                            FSlateColor::UseForeground())]]] +
           SHorizontalBox::Slot().AutoWidth().Padding(
               4, 0)[SNew(SButton)
                         .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                         .ContentPadding(FMargin(0))
                         .HAlign(HAlign_Center)
                         .VAlign(VAlign_Center)
                         .ToolTipText(LOCTEXT(
                             "ExportTip",
                             "Save the current chat transcript as Markdown."))
                         .OnClicked(this, &SACPChatWindow::OnExportClicked)
                         .IsEnabled_Lambda([this]() {
                           return MessageLog.IsValid() &&
                                  MessageLog->GetMessages().Num() > 0;
                         })[SNew(SBox)
                                .WidthOverride(24.f)
                                .HeightOverride(24.f)
                                .HAlign(HAlign_Center)
                                .VAlign(VAlign_Center)
                                    [SNew(SImage)
                                         .Image(FAppStyle::Get().GetBrush(
                                             "FontEditor.ExportPage"))
                                         .DesiredSizeOverride(FVector2D(16, 16))
                                         .ColorAndOpacity(
                                             FSlateColor::UseForeground())]]]];
}

TSharedRef<SWidget> SACPChatWindow::BuildInputRow() {
  using UAgent::EClientState;

  return SNew(SBorder)
      .BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
      .Padding(FMargin(4))
          [SNew(SHorizontalBox) +
           SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 4, 0)
               [SAssignNew(MentionPicker, SACPMentionPicker)
                    .OnAssetPicked_Lambda([this](const FAssetData &A) {
                      OnMentionPicked(A);
                    })[SNew(SBox)
                           // Grow with content up to 30 lines, then let the
                           // textbox scroll vertically.
                           .MaxDesiredHeight_Lambda([]() -> FOptionalSize {
                             const FSlateFontInfo Font =
                                 FAppStyle::Get()
                                     .GetWidgetStyle<FEditableTextBoxStyle>(
                                         "NormalEditableTextBox")
                                     .TextStyle.Font;
                             const TSharedRef<FSlateFontMeasure> FM =
                                 FSlateApplication::Get()
                                     .GetRenderer()
                                     ->GetFontMeasureService();
                             return 30.0f * FM->GetMaxCharacterHeight(Font);
                           })[SAssignNew(InputBox, SMultiLineEditableTextBox)
                                  .HintText(LOCTEXT(
                                      "InputHint",
                                      "Ask anything — Enter to send, "
                                      "Shift+Enter for newline, @ to add "
                                      "context"))
                                  .OnKeyDownHandler(this,
                                                    &SACPChatWindow::OnInputKey)
                                  .OnTextChanged(
                                      this, &SACPChatWindow::OnInputTextChanged)
                                  .AllowMultiLine(true)
                                  .AutoWrapText(true)
                                  .AlwaysShowScrollbars(false)]]] +
           SHorizontalBox::Slot().AutoWidth().VAlign(
               VAlign_Bottom)[SNew(SButton)
                                  .Text_Lambda([this]() -> FText {
                                    return (Client.IsValid() &&
                                            Client->GetState() ==
                                                EClientState::Prompting)
                                               ? LOCTEXT("Cancel", "Cancel")
                                               : LOCTEXT("Send", "Send");
                                  })
                                  .IsEnabled_Lambda([this]() {
                                    if (!Client.IsValid())
                                      return false;
                                    if (Client->GetState() ==
                                        EClientState::Prompting)
                                      return true;
                                    return IsSendEnabled();
                                  })
                                  .OnClicked_Lambda([this]() -> FReply {
                                    if (Client.IsValid() &&
                                        Client->GetState() ==
                                            EClientState::Prompting) {
                                      Client->CancelPrompt();
                                      return FReply::Handled();
                                    }
                                    return OnSendClicked();
                                  })]];
}

TSharedRef<SWidget> SACPChatWindow::BuildPermissionModeRow() {
  // The bottom strip is now entirely agent-advertised — modes and models are
  // both populated from session/new by RefreshAgentSettings. Agents that
  // don't advertise either leave this container empty (Slate collapses the
  // border to its 3-px padding height).
  TSharedRef<SHorizontalBox> AgentBox = SNew(SHorizontalBox);
  AgentSettingsContainer = AgentBox;

  return SNew(SBorder)
      .BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
      .Padding(FMargin(6, 3))[AgentBox];
}

void SACPChatWindow::RefreshAgentSettings() {
  if (!AgentSettingsContainer.IsValid() || !Client.IsValid())
    return;

  // Wholesale rebuild — agents may legitimately change advertised sets and
  // dropdown counts at runtime via config_option_update / current_mode_update.
  AgentSettingsContainer->ClearChildren();

  // Mode dropdown — agent-broadcast via the `modes` field in session/new.
  // Different agents advertise wildly different mode sets (Claude's
  // default/acceptEdits/plan/bypassPermissions; Codex's read-only/default/
  // full-access). Hidden when the agent doesn't support modes.
  if (Client->GetAvailableModes().Num() > 0) {
    TSharedRef<FAgentDropdownState> State = MakeShared<FAgentDropdownState>();
    const TArray<UAgent::FSessionMode> &Modes = Client->GetAvailableModes();
    const FString &Current = Client->GetCurrentModeId();
    State->Labels.Reserve(Modes.Num());
    State->Values.Reserve(Modes.Num());
    int32 InitialIdx = 0;
    for (int32 i = 0; i < Modes.Num(); ++i) {
      const UAgent::FSessionMode &M = Modes[i];
      const FString DisplayName = M.Name.IsEmpty() ? M.Id : M.Name;
      State->Labels.Add(MakeShared<FString>(DisplayName));
      State->Values.Add(M.Id);
      if (M.Id == Current)
        InitialIdx = i;
    }

    TSharedPtr<UAgent::FACPClient> ClientCopy = Client;
    AgentSettingsContainer->AddSlot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(0, 0, 4, 0)
            [SNew(STextBlock).Text(LOCTEXT("ModeDropdownLabel", "Mode:"))];
    AgentSettingsContainer->AddSlot().AutoWidth().VAlign(VAlign_Center)
        [SNew(SComboBox<TSharedPtr<FString>>)
             .OptionsSource(&State->Labels)
             .InitiallySelectedItem(State->Labels[InitialIdx])
             .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) {
               return SNew(STextBlock)
                   .Text(Item.IsValid() ? FText::FromString(*Item)
                                        : FText::GetEmpty());
             })
             .OnSelectionChanged_Lambda([ClientCopy, State](
                                            TSharedPtr<FString> NewVal,
                                            ESelectInfo::Type) {
               if (!NewVal.IsValid() || !ClientCopy.IsValid())
                 return;
               const int32 Idx = State->Labels.IndexOfByPredicate(
                   [&](const TSharedPtr<FString> &P) {
                     return P.IsValid() && *P == *NewVal;
                   });
               if (Idx == INDEX_NONE)
                 return;
               const FString &Picked = State->Values[Idx];
               UAgent::SessionModeStore::Save(Picked);
               ClientCopy->SetSessionMode(Picked);
             })[SNew(STextBlock).Text_Lambda([this] {
               if (!Client.IsValid())
                 return FText::GetEmpty();
               const FString &Cur = Client->GetCurrentModeId();
               for (const UAgent::FSessionMode &M :
                    Client->GetAvailableModes()) {
                 if (M.Id == Cur) {
                   return FText::FromString(M.Name.IsEmpty() ? M.Id : M.Name);
                 }
               }
               return FText::FromString(Cur);
             })]];
  }

  // One dropdown per advertised model config option. Other categories
  // (thought_level/...) are intentionally ignored here — only the "model"
  // selector is surfaced.
  for (const UAgent::FConfigOption &Opt : Client->GetConfigOptions()) {
    if (Opt.Category != TEXT("model"))
      continue;
    TSharedRef<FAgentDropdownState> State = MakeShared<FAgentDropdownState>();
    State->Labels.Reserve(Opt.Options.Num());
    State->Values.Reserve(Opt.Options.Num());
    int32 InitialIdx = 0;
    for (int32 i = 0; i < Opt.Options.Num(); ++i) {
      const UAgent::FConfigOptionChoice &C = Opt.Options[i];
      const FString DisplayName = C.Name.IsEmpty() ? C.Value : C.Name;
      State->Labels.Add(MakeShared<FString>(DisplayName));
      State->Values.Add(C.Value);
      if (C.Value == Opt.CurrentValue)
        InitialIdx = i;
    }
    if (State->Labels.Num() == 0)
      continue;

    const FString ConfigId = Opt.Id;
    TSharedPtr<UAgent::FACPClient> ClientCopy = Client;
    AgentSettingsContainer->AddSlot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(
            8, 0, 4,
            0)[SNew(STextBlock).Text(LOCTEXT("ModelDropdownLabel", "Model:"))];
    AgentSettingsContainer->AddSlot().AutoWidth().VAlign(VAlign_Center)
        [SNew(SComboBox<TSharedPtr<FString>>)
             .OptionsSource(&State->Labels)
             .InitiallySelectedItem(State->Labels[InitialIdx])
             .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) {
               return SNew(STextBlock)
                   .Text(Item.IsValid() ? FText::FromString(*Item)
                                        : FText::GetEmpty());
             })
             .OnSelectionChanged_Lambda(
                 [ClientCopy, State, ConfigId](TSharedPtr<FString> NewVal,
                                               ESelectInfo::Type) {
                   if (!NewVal.IsValid() || !ClientCopy.IsValid())
                     return;
                   const int32 Idx = State->Labels.IndexOfByPredicate(
                       [&](const TSharedPtr<FString> &P) {
                         return P.IsValid() && *P == *NewVal;
                       });
                   if (Idx == INDEX_NONE)
                     return;
                   const FString &Picked = State->Values[Idx];
                   UAgent::ModelStore::Save(Picked);
                   ClientCopy->SetConfigOption(ConfigId, Picked);
                 })[SNew(STextBlock).Text_Lambda([this, ConfigId] {
               if (!Client.IsValid())
                 return FText::GetEmpty();
               for (const UAgent::FConfigOption &O :
                    Client->GetConfigOptions()) {
                 if (O.Id != ConfigId)
                   continue;
                 for (const UAgent::FConfigOptionChoice &C : O.Options) {
                   if (C.Value == O.CurrentValue) {
                     return FText::FromString(C.Name.IsEmpty() ? C.Value
                                                               : C.Name);
                   }
                 }
                 return FText::FromString(O.CurrentValue);
               }
               return FText::GetEmpty();
             })]];
  }
}

// ─── Status / enablement ────────────────────────────────────────────────────

FText SACPChatWindow::GetStatusText() const {
  if (!Client.IsValid())
    return LOCTEXT("NoClient", "no client");
  using UAgent::EClientState;
  switch (Client->GetState()) {
  case EClientState::Disconnected:
    return LOCTEXT("StateDisconnected", "disconnected");
  case EClientState::Starting:
    return LOCTEXT("StateStarting", "starting agent…");
  case EClientState::Initializing:
    return LOCTEXT("StateInit", "initializing…");
  case EClientState::CreatingSession:
    return LOCTEXT("StateNew", "creating session…");
  case EClientState::Ready:
    return FText::Format(LOCTEXT("StateReady", "ready · {0}"),
                         FText::FromString(Client->GetSessionId().Left(12)));
  case EClientState::Prompting:
    return LOCTEXT("StatePrompting", "thinking…");
  case EClientState::Error:
    return FText::Format(LOCTEXT("StateError", "error · {0}"),
                         FText::FromString(Client->GetLastError()));
  }
  return FText::GetEmpty();
}

bool SACPChatWindow::IsSendEnabled() const {
  return Client.IsValid() &&
         Client->GetState() == UAgent::EClientState::Ready &&
         InputBox.IsValid() && !InputBox->GetText().IsEmpty();
}

// ─── Session control ────────────────────────────────────────────────────────

void SACPChatWindow::StartSession() {
  // Resolve any in-flight permission/proposal prompts before resetting the
  // log — the outgoing transport is about to die and the agent will never
  // get our answer anyway, but if the callback ever fires it must not
  // address an index in the freshly-reset log.
  for (auto &KV : PendingPermissions) {
    if (KV.Value)
      KV.Value(UAgent::EPermissionOutcome::Deny);
  }
  PendingPermissions.Reset();

  for (auto &KV : PendingProposals) {
    if (KV.Value.Complete)
      KV.Value.Complete(UAgent::EProposalOutcome::Cancelled);
  }
  PendingProposals.Reset();
  UAgent::FProposalBroker::Get().ResetTurn();

  // New session — re-send AGENTS.md on the first prompt so edits between
  // sessions are picked up.
  bProjectContextSent = false;

  if (MessageLog.IsValid())
    MessageLog->Reset();

  const UUAgentSettings *Settings = GetDefault<UUAgentSettings>();
  if (!Settings || Settings->AgentCommand.IsEmpty()) {
    MessageLog->AppendSystem(
        TEXT("Set Agent Command in Project Settings → Plugins → UAgent."),
        FLinearColor(1.0f, 0.5f, 0.5f));
    return;
  }

  FString Resolved = Settings->AgentCommand;
  FString Ext = FPaths::GetExtension(Resolved).ToLower();

#if PLATFORM_WINDOWS
  // npm-style shims: the extensionless file on Windows is a bash script that
  // CreateProcess can't launch; the sibling .cmd is the Windows-native entry.
  if (Ext.IsEmpty()) {
    const TCHAR *Candidates[] = {TEXT(".cmd"), TEXT(".bat"), TEXT(".exe")};
    for (const TCHAR *Suffix : Candidates) {
      FString Candidate = Resolved + Suffix;
      if (FPaths::FileExists(Candidate)) {
        Resolved = MoveTemp(Candidate);
        Ext = FString(Suffix).RightChop(1).ToLower();
        break;
      }
    }
  }
#endif

  FString LaunchCommand;
  TArray<FString> LaunchArgs;
  if (Ext == TEXT("cmd") || Ext == TEXT("bat")) {
    LaunchCommand = TEXT("cmd.exe");
    LaunchArgs.Add(TEXT("/c"));
    LaunchArgs.Add(Resolved);
  } else if (Ext == TEXT("js")) {
    LaunchCommand = TEXT("node");
    LaunchArgs.Add(Resolved);
  } else {
    LaunchCommand = Resolved;
  }
  LaunchArgs.Append(Settings->AgentArgs);

  FString McpUrl;
  if (Settings->bEnableMCPServer) {
    McpUrl = FString::Printf(TEXT("http://127.0.0.1:%d/mcp"),
                             Settings->MCPServerPort);
  }
  Client->SetMcpServerUrl(McpUrl);

  Client->Start(LaunchCommand, LaunchArgs,
                FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));

  // Surface any pending proposals from prior sessions. Banners are appended
  // to the now-empty log so they layer above the agent's first message.
  ScanForPendingProposals();
}

FReply SACPChatWindow::OnNewSessionClicked() {
  StartSession();
  return FReply::Handled();
}

FReply SACPChatWindow::OnExportClicked() {
  if (!MessageLog.IsValid() || MessageLog->GetMessages().Num() == 0) {
    return FReply::Handled();
  }

  IDesktopPlatform *DP = FDesktopPlatformModule::Get();
  if (!DP) {
    MessageLog->AppendSystem(
        TEXT("Export failed: desktop platform unavailable."),
        FLinearColor(1.0f, 0.5f, 0.5f));
    return FReply::Handled();
  }

  const FString DefaultName = FString::Printf(
      TEXT("UAgent-%s.md"), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")));
  const void *ParentHandle =
      FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared());

  TArray<FString> Picked;
  const bool bOk = DP->SaveFileDialog(
      ParentHandle, TEXT("Export chat transcript"), FPaths::ProjectSavedDir(),
      DefaultName, TEXT("Markdown (*.md)|*.md|All Files (*.*)|*.*"),
      EFileDialogFlags::None, Picked);
  if (!bOk || Picked.Num() == 0)
    return FReply::Handled();

  FString Md;
  Md.Reserve(4096);
  Md += FString::Printf(TEXT("# UAgent — chat export\n\n_%s_\n\n"),
                        *FDateTime::Now().ToString());

  for (const FACPChatMessageItemRef &Item : MessageLog->GetMessages()) {
    switch (Item->Role) {
    case FACPChatMessageItem::ERole::User:
      Md += TEXT("## You\n\n");
      if (Item->Contexts.Num() > 0) {
        Md += TEXT("**Context:**\n\n");
        for (const FAssetData &A : Item->Contexts) {
          Md +=
              FString::Printf(TEXT("- `%s` (`%s`)\n"), *A.AssetName.ToString(),
                              *A.PackageName.ToString());
        }
        Md += TEXT("\n");
      }
      Md += Item->Text;
      Md += TEXT("\n\n");
      break;

    case FACPChatMessageItem::ERole::Agent:
      Md += TEXT("## Agent\n\n");
      Md += Item->Text;
      Md += TEXT("\n\n");
      break;

    case FACPChatMessageItem::ERole::Tool:
      Md +=
          FString::Printf(TEXT("### Tool: %s _(%s)_\n\n"),
                          Item->ToolCallTitle.IsEmpty() ? TEXT("(untitled)")
                                                        : *Item->ToolCallTitle,
                          *Item->ToolCallStatus);
      if (!Item->Text.IsEmpty()) {
        Md += TEXT("```\n");
        Md += Item->Text;
        if (!Item->Text.EndsWith(TEXT("\n")))
          Md += TEXT("\n");
        Md += TEXT("```\n\n");
      }
      break;

    case FACPChatMessageItem::ERole::System:
      Md += TEXT("> _system:_ ");
      Md += Item->Text;
      Md += TEXT("\n\n");
      break;
    }
  }

  FString OutPath = Picked[0];
  if (FPaths::GetExtension(OutPath).IsEmpty()) {
    OutPath += TEXT(".md");
  }

  if (FFileHelper::SaveStringToFile(
          Md, *OutPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) {
    MessageLog->AppendSystem(
        FString::Printf(TEXT("Exported transcript to %s"), *OutPath),
        FLinearColor(0.5f, 0.8f, 0.5f));
  } else {
    MessageLog->AppendSystem(
        FString::Printf(TEXT("Export failed: could not write %s"), *OutPath),
        FLinearColor(1.0f, 0.5f, 0.5f));
  }

  return FReply::Handled();
}

// ─── Input handling ─────────────────────────────────────────────────────────

FReply SACPChatWindow::OnInputKey(const FGeometry &, const FKeyEvent &Key) {
  if (Key.GetKey() == EKeys::Enter && !Key.IsShiftDown()) {
    // While the @-mention popup is open, Enter confirms the top match
    // (OnMentionPicked strips the @query and adds the chip) instead of sending.
    if (MentionPicker.IsValid() && MentionPicker->IsOpen()) {
      if (!MentionPicker->ConfirmTopResult()) {
        MentionPicker->Close();
      }
      return FReply::Handled();
    }
    OnSendClicked();
    return FReply::Handled();
  }
  if (Key.GetKey() == EKeys::Escape && MentionPicker.IsValid() &&
      MentionPicker->IsOpen()) {
    MentionPicker->Close();
    return FReply::Handled();
  }
  return FReply::Unhandled();
}

void SACPChatWindow::OnInputTextChanged(const FText &NewText) {
  const FString S = NewText.ToString();

  // Reconcile token-backed chips against @[...] tokens still present in the
  // text.
  if (TokenBackedChipPackages.Num() > 0) {
    TSet<FName> Mentioned;
    int32 Scan = 0;
    while (Scan < S.Len()) {
      const int32 At = S.Find(TEXT("@["), ESearchCase::CaseSensitive,
                              ESearchDir::FromStart, Scan);
      if (At == INDEX_NONE)
        break;
      if (At > 0 && !FChar::IsWhitespace(S[At - 1])) {
        Scan = At + 1;
        continue;
      }
      const int32 End = S.Find(TEXT("]"), ESearchCase::CaseSensitive,
                               ESearchDir::FromStart, At + 2);
      if (End == INDEX_NONE)
        break;
      const FString Name = S.Mid(At + 2, End - (At + 2));
      if (!Name.IsEmpty())
        Mentioned.Add(FName(*Name));
      Scan = End + 1;
    }

    bool bChipsChanged = false;
    for (auto It = TokenBackedChipPackages.CreateIterator(); It; ++It) {
      const FAssetData *Chip = ContextChips.Find(*It);
      if (!Chip || !Mentioned.Contains(Chip->AssetName)) {
        if (Chip)
          ContextChips.Remove(*It);
        It.RemoveCurrent();
        bChipsChanged = true;
      }
    }
    if (bChipsChanged)
      RebuildContextStrip();
  }

  // Find the last '@' not followed by whitespace — opens/updates the mention
  // popup with the suffix as query. Skip '@[' so cursor editing inside an
  // existing token doesn't reopen the picker.
  int32 LastAt = INDEX_NONE;
  for (int32 i = S.Len() - 1; i >= 0; --i) {
    if (S[i] == TEXT('@')) {
      const bool bIsCompletedToken = (i + 1 < S.Len()) && S[i + 1] == TEXT('[');
      if (!bIsCompletedToken && (i == 0 || FChar::IsWhitespace(S[i - 1]))) {
        LastAt = i;
      }
      break;
    }
    if (FChar::IsWhitespace(S[i]))
      break;
  }

  if (!MentionPicker.IsValid()) {
    return;
  }

  if (LastAt == INDEX_NONE) {
    MentionPicker->Close();
    PendingAtPosition = INDEX_NONE;
    return;
  }

  PendingAtPosition = LastAt;
  MentionPicker->RefreshResults(S.Mid(LastAt + 1));
  MentionPicker->Open();
}

// ─── Sending ────────────────────────────────────────────────────────────────

FReply SACPChatWindow::OnSendClicked() {
  if (!IsSendEnabled())
    return FReply::Handled();

  const FString UserText = InputBox->GetText().ToString().TrimStartAndEnd();
  if (UserText.IsEmpty())
    return FReply::Handled();

  // New turn — clear the per-turn proposal guard and stamp a fresh id so any
  // sidecars written during this turn can be grouped together for replay.
  UAgent::FProposalBroker::Get().ResetTurn();
  CurrentTurnId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
  LastUserPromptText = UserText;

  TArray<UAgent::FContentBlock> Blocks = BuildContextBlocks();

  // In developer mode, prepend the propose_missing_tool standing instruction.
  // Index-0 placement keeps the policy ahead of large auto-context blocks
  // (Blueprint summaries can run thousands of chars), so the agent can't
  // lose it to mid-context attention drop. Suppressed when the gate is
  // closed — same condition that suppresses the tool's registration.
  if (UAgent::Common::IsDeveloperToolingEnabled()) {
    static const FString StandingInstruction = TEXT(
        "Before acting on the user's request, check whether the registered "
        "tools (visible in tools/list) cover the intent. If a clearly-"
        "missing tool would make the task tractable AND no existing tool "
        "plausibly fits, call propose_missing_tool with name, description, "
        "inputSchema, whyNeeded, and exampleCall — then STOP. Do not "
        "improvise around the gap or call alternative tools. Bias toward "
        "existing tools: only propose when improvising would produce a "
        "worse outcome than halting. Never propose a tool that already "
        "exists. Do not call propose_missing_tool more than once per turn. "
        "If its result includes halt:true, end the turn immediately.");
    Blocks.Insert(UAgent::FContentBlock::MakeText(StandingInstruction),
                  /*Index=*/0);
  }

  // Project context — AGENTS.md from the UE5 project root, once per session.
  // Inserted after the dev-mode policy (which must stay near index 0 to
  // survive mid-context attention drop) but before per-asset context, so it
  // frames every later block as "for this project". The flag flips to true
  // whether or not the file exists — absent AGENTS.md means we silently skip
  // for the rest of the session rather than stat the disk every turn.
  if (!bProjectContextSent) {
    const FString AgentsMdPath = FPaths::ProjectDir() / TEXT("AGENTS.md");
    FString AgentsMd;
    if (FFileHelper::LoadFileToString(AgentsMd, *AgentsMdPath)) {
      const FString Wrapped = FString::Printf(
          TEXT("Project context (AGENTS.md):\n\n%s"), *AgentsMd);
      const int32 InsertIdx =
          UAgent::Common::IsDeveloperToolingEnabled() ? 1 : 0;
      Blocks.Insert(UAgent::FContentBlock::MakeText(Wrapped), InsertIdx);
    }
    bProjectContextSent = true;
  }

  Blocks.Add(UAgent::FContentBlock::MakeText(UserText));

  TArray<FAssetData> VisibleContexts;
  for (auto &KV : ContextChips)
    VisibleContexts.Add(KV.Value);
  for (const FAssetData &A : CollectOpenAssets())
    VisibleContexts.AddUnique(A);

  MessageLog->AppendUser(UserText, VisibleContexts);
  InputBox->SetText(FText::GetEmpty());
  ContextChips.Reset();
  TokenBackedChipPackages.Reset();
  RebuildContextStrip();

  if (!Client->SendPrompt(Blocks)) {
    MessageLog->AppendSystem(TEXT("Cannot send — client not ready."),
                             FLinearColor(1.0f, 0.5f, 0.5f));
  }

  return FReply::Handled();
}

// ─── Mention → chip ─────────────────────────────────────────────────────────

void SACPChatWindow::OnMentionPicked(const FAssetData &Asset) {
  AddContextChip(Asset);

  // Replace the typed @query with a visible @[AssetName] token so the user
  // keeps a reference to what they attached. Trailing space prevents
  // OnInputTextChanged from re-opening the picker against the new token. Only
  // runs for @-typed flows; the + button leaves PendingAtPosition at
  // INDEX_NONE.
  if (PendingAtPosition != INDEX_NONE && InputBox.IsValid()) {
    const FString Cur = InputBox->GetText().ToString();
    const FString Prefix =
        (PendingAtPosition < Cur.Len()) ? Cur.Left(PendingAtPosition) : Cur;
    const FString Token =
        FString::Printf(TEXT("@[%s] "), *Asset.AssetName.ToString());
    InputBox->SetText(FText::FromString(Prefix + Token));
    PendingAtPosition = INDEX_NONE;
    TokenBackedChipPackages.Add(Asset.PackageName);
  }

  if (MentionPicker.IsValid())
    MentionPicker->Close();
  FSlateApplication::Get().SetKeyboardFocus(InputBox);
}

// ─── Context chips ──────────────────────────────────────────────────────────

void SACPChatWindow::AddContextChip(const FAssetData &Asset) {
  ContextChips.Add(Asset.PackageName, Asset);
  RebuildContextStrip();
}

void SACPChatWindow::RemoveContextChip(const FAssetData &Asset) {
  ContextChips.Remove(Asset.PackageName);
  TokenBackedChipPackages.Remove(Asset.PackageName);
  RebuildContextStrip();
}

void SACPChatWindow::RebuildContextStrip() {
  if (!ContextStrip.IsValid())
    return;

  TArray<FAssetData> Auto;
  const UUAgentSettings *Settings = GetDefault<UUAgentSettings>();
  if (Settings && Settings->bAutoIncludeOpenAssets) {
    Auto = CollectOpenAssets();
  }
  ContextStrip->SetAutoChips(Auto);

  TArray<FAssetData> Manual;
  Manual.Reserve(ContextChips.Num());
  for (const TPair<FName, FAssetData> &KV : ContextChips) {
    Manual.Add(KV.Value);
  }
  ContextStrip->SetManualChips(Manual);
}

TArray<FAssetData> SACPChatWindow::CollectOpenAssets() const {
  TArray<FAssetData> Out;
  if (!GEditor)
    return Out;
  UAssetEditorSubsystem *AES =
      GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
  if (!AES)
    return Out;
  TArray<UObject *> All = AES->GetAllEditedAssets();
  for (UObject *Obj : All) {
    if (!Obj)
      continue;
    Out.Add(FAssetData(Obj));
  }
  return Out;
}

TArray<UAgent::FContentBlock> SACPChatWindow::BuildContextBlocks() {
  using UAgent::FContentBlock;
  TArray<FContentBlock> Out;

  const UUAgentSettings *Settings = GetDefault<UUAgentSettings>();
  const int32 MaxChars = Settings ? Settings->MaxBlueprintSummaryChars : 8000;

  const TSharedPtr<UAgent::FAssetContextBuilderRegistry> Builders =
      FModuleManager::LoadModuleChecked<FUAgentModule>("UAgent")
          .GetAssetContextRegistry();
  if (!Builders.IsValid())
    return Out;

  TSet<FName> Emitted;

  auto EmitOnce = [&](const FAssetData &A) {
    if (Emitted.Contains(A.PackageName))
      return;
    Emitted.Add(A.PackageName);
    Out.Add(Builders->Build(A, MaxChars));
  };

  if (Settings && Settings->bAutoIncludeOpenAssets) {
    for (const FAssetData &A : CollectOpenAssets()) {
      EmitOnce(A);
    }
  }
  for (auto &KV : ContextChips) {
    EmitOnce(KV.Value);
  }

  return Out;
}

// ─── ACPClient event handlers ───────────────────────────────────────────────

void SACPChatWindow::OnClientStateChanged(UAgent::EClientState NewState) {
  using UAgent::EClientState;
  if (NewState == EClientState::Error) {
    MessageLog->AppendSystem(
        FString::Printf(TEXT("Error: %s"), *Client->GetLastError()),
        FLinearColor(1.0f, 0.5f, 0.5f));
    return;
  }

  // Once the session is established, push the user's saved preferences to the
  // agent. Each is skipped silently when there's no preference or the saved
  // value isn't in the agent's advertised set (different agent, dropped
  // option, etc.).
  if (NewState == EClientState::Ready && Client.IsValid()) {
    // Saved session mode.
    const FString SavedMode = UAgent::SessionModeStore::Load();
    if (!SavedMode.IsEmpty() && SavedMode != Client->GetCurrentModeId()) {
      const bool bAvailable = Client->GetAvailableModes().ContainsByPredicate(
          [&SavedMode](const UAgent::FSessionMode &M) {
            return M.Id == SavedMode;
          });
      if (bAvailable) {
        Client->SetSessionMode(SavedMode);
      }
    }

    // Saved model.
    const FString SavedModel = UAgent::ModelStore::Load();
    if (SavedModel.IsEmpty())
      return;
    for (const UAgent::FConfigOption &Opt : Client->GetConfigOptions()) {
      if (Opt.Category != TEXT("model"))
        continue;
      if (Opt.CurrentValue == SavedModel)
        return;
      const bool bAvailable = Opt.Options.ContainsByPredicate(
          [&SavedModel](const UAgent::FConfigOptionChoice &C) {
            return C.Value == SavedModel;
          });
      if (bAvailable) {
        Client->SetConfigOption(Opt.Id, SavedModel);
      }
      return;
    }
  }
}

void SACPChatWindow::OnSessionUpdateReceived(
    const UAgent::FSessionUpdate &Update) {
  if (MessageLog.IsValid())
    MessageLog->ApplySessionUpdate(Update);
}

void SACPChatWindow::OnPromptDone(UAgent::EStopReason /*Reason*/,
                                  FString ErrorOrEmpty) {
  MessageLog->EndAgentTurn();
  // Defensive — the per-turn guard should already be cleared by the next
  // OnSendClicked, but resetting at end-of-turn keeps the invariant that a
  // proposal from a previous turn never leaks into a new one.
  UAgent::FProposalBroker::Get().ResetTurn();
  if (!ErrorOrEmpty.IsEmpty()) {
    MessageLog->AppendSystem(
        FString::Printf(TEXT("Prompt failed: %s"), *ErrorOrEmpty),
        FLinearColor(1.0f, 0.5f, 0.5f));
  }
}

void SACPChatWindow::OnClientError(const FString &Message) {
  MessageLog->AppendSystem(Message, FLinearColor(1.0f, 0.5f, 0.5f));
}

// ─── Permission prompt routing ──────────────────────────────────────────────

void SACPChatWindow::OnPermissionRequested(
    const UAgent::FPermissionRequest &Req,
    TFunction<void(UAgent::EPermissionOutcome)> Complete) {
  if (!MessageLog.IsValid()) {
    if (Complete)
      Complete(UAgent::EPermissionOutcome::Deny);
    return;
  }

  // Render the agent's raw arguments as an indented JSON snippet so the user
  // sees exactly what's about to be invoked. Truncate hard so a 50KB blob
  // doesn't blow up the chat row.
  FString ArgsPreview;
  if (Req.RawToolCall.IsValid()) {
    TSharedRef<TJsonWriter<>> Writer =
        TJsonWriterFactory<>::Create(&ArgsPreview);
    FJsonSerializer::Serialize(Req.RawToolCall.ToSharedRef(), Writer);
    constexpr int32 MaxChars = 1500;
    if (ArgsPreview.Len() > MaxChars) {
      ArgsPreview = ArgsPreview.Left(MaxChars) + TEXT("\n…(truncated)");
    }
  }

  const FString Title =
      Req.ToolTitle.IsEmpty() ? TEXT("(unnamed tool)") : Req.ToolTitle;
  const FString PermissionId =
      MessageLog->AppendPermission(Title, Req.ToolKind, ArgsPreview);
  PendingPermissions.Add(PermissionId, MoveTemp(Complete));
}

void SACPChatWindow::OnPermissionRowDecided(const FString &PermissionId,
                                            bool bAllow) {
  TFunction<void(UAgent::EPermissionOutcome)> Complete;
  if (!PendingPermissions.RemoveAndCopyValue(PermissionId, Complete)) {
    return;
  }
  if (MessageLog.IsValid()) {
    MessageLog->SetPermissionState(
        PermissionId, bAllow ? FACPChatMessageItem::EPermissionState::Allowed
                             : FACPChatMessageItem::EPermissionState::Denied);
  }
  if (Complete) {
    Complete(bAllow ? UAgent::EPermissionOutcome::Allow
                    : UAgent::EPermissionOutcome::Deny);
  }
}

// ─── Proposal flow ──────────────────────────────────────────────────────────

namespace {
// Render a JsonObject as compact JSON, truncated for display in a chat row.
FString JsonForPreview(const TSharedPtr<FJsonObject> &Obj, int32 MaxChars) {
  if (!Obj.IsValid())
    return FString();
  FString Out;
  TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
  FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
  if (Out.Len() > MaxChars) {
    Out = Out.Left(MaxChars) + TEXT("\n…(truncated)");
  }
  return Out;
}

// Convert an EProposalRowDecision (from SACPMessageList) into the
// UAgent::EProposalOutcome the broker callback expects.
UAgent::EProposalOutcome DecisionToOutcome(EProposalRowDecision D) {
  switch (D) {
  case EProposalRowDecision::Accepted:
    return UAgent::EProposalOutcome::Accepted;
  case EProposalRowDecision::Skipped:
    return UAgent::EProposalOutcome::Skipped;
  case EProposalRowDecision::Cancelled:
  default:
    return UAgent::EProposalOutcome::Cancelled;
  }
}

// Sidecar filename: "<UTC-timestamp>-<sanitized-name>.json", timestamp first
// for natural lexicographic ordering when the chat scans pending proposals.
FString SanitizeForFilename(const FString &In) {
  FString Out;
  Out.Reserve(In.Len());
  for (TCHAR C : In) {
    if (FChar::IsAlnum(C) || C == TEXT('_') || C == TEXT('-')) {
      Out.AppendChar(C);
    }
  }
  return Out.IsEmpty() ? TEXT("tool") : Out;
}

bool LoadJsonFile(const FString &Path, TSharedPtr<FJsonObject> &OutObj) {
  FString Json;
  if (!FFileHelper::LoadFileToString(Json, *Path)) {
    return false;
  }
  TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
  return FJsonSerializer::Deserialize(Reader, OutObj) && OutObj.IsValid();
}

bool SaveJsonFile(const FString &Path, const TSharedRef<FJsonObject> &Obj) {
  FString Json;
  TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
  FJsonSerializer::Serialize(Obj, Writer);
  return FFileHelper::SaveStringToFile(
      Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

// Update the `status` field of a sidecar in-place. Returns true on success.
bool MarkSidecarStatus(const FString &Path, const FString &NewStatus) {
  TSharedPtr<FJsonObject> Obj;
  if (!LoadJsonFile(Path, Obj))
    return false;
  Obj->SetStringField(TEXT("status"), NewStatus);
  return SaveJsonFile(Path, Obj.ToSharedRef());
}
} // namespace

void SACPChatWindow::OnProposalRequested(
    const UAgent::FProposalRequest &Req,
    TFunction<void(UAgent::EProposalOutcome)> Complete) {
  if (!MessageLog.IsValid()) {
    if (Complete)
      Complete(UAgent::EProposalOutcome::Cancelled);
    return;
  }

  FString ArgsPreview;
  if (Req.InputSchema.IsValid()) {
    ArgsPreview =
        FString::Printf(TEXT("inputSchema:\n%s"),
                        *JsonForPreview(Req.InputSchema, /*MaxChars=*/1200));
  }
  if (Req.ExampleCall.IsValid()) {
    if (!ArgsPreview.IsEmpty())
      ArgsPreview += TEXT("\n\n");
    ArgsPreview +=
        FString::Printf(TEXT("exampleCall:\n%s"),
                        *JsonForPreview(Req.ExampleCall, /*MaxChars=*/600));
  }

  MessageLog->AppendProposal(Req.Id, Req.Name, Req.Description, Req.WhyNeeded,
                             ArgsPreview);

  FPendingProposal Pending;
  Pending.Complete = MoveTemp(Complete);
  Pending.InputSchema = Req.InputSchema;
  Pending.ExampleCall = Req.ExampleCall;
  Pending.bIsReadOnly = Req.bIsReadOnly;
  PendingProposals.Add(Req.Id, MoveTemp(Pending));
}

void SACPChatWindow::OnProposalRowDecided(const FString &ProposalId,
                                          EProposalRowDecision Decision) {
  FPendingProposal Pending;
  if (!PendingProposals.RemoveAndCopyValue(ProposalId, Pending)) {
    return;
  }

  // On Accept, write the sidecar before invoking the broker callback so a
  // successful agent response means the file is on disk.
  if (Decision == EProposalRowDecision::Accepted) {
    // Pull display fields off the chat row; pull schema/example off the
    // pending struct (which holds the original FProposalRequest payload —
    // the row never stores those parsed JSON objects).
    FString ProposedName, Description, WhyNeeded;
    for (const FACPChatMessageItemRef &Item : MessageLog->GetMessages()) {
      if (Item->Role == FACPChatMessageItem::ERole::Proposal &&
          Item->ProposalId == ProposalId) {
        ProposedName = Item->ProposedName;
        Description = Item->ProposedDescription;
        WhyNeeded = Item->ProposalWhyNeeded;
        break;
      }
    }

    const FString Timestamp =
        FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"));
    const FString FileName = FString::Printf(
        TEXT("%s-%s.json"), *Timestamp, *SanitizeForFilename(ProposedName));
    const FString OutPath = UAgent::Common::GetProposalsDir() / FileName;

    TSharedRef<FJsonObject> Sidecar = MakeShared<FJsonObject>();
    Sidecar->SetNumberField(TEXT("schemaVersion"), 1);
    Sidecar->SetStringField(TEXT("id"), ProposalId);
    Sidecar->SetStringField(TEXT("turnId"), CurrentTurnId);
    Sidecar->SetStringField(TEXT("createdAtUtc"),
                            FDateTime::UtcNow().ToIso8601());

    TSharedRef<FJsonObject> Proposal = MakeShared<FJsonObject>();
    Proposal->SetStringField(TEXT("name"), ProposedName);
    Proposal->SetStringField(TEXT("description"), Description);
    Proposal->SetStringField(TEXT("whyNeeded"), WhyNeeded);
    Proposal->SetBoolField(TEXT("isReadOnly"), Pending.bIsReadOnly);
    if (Pending.InputSchema.IsValid()) {
      Proposal->SetObjectField(TEXT("inputSchema"), Pending.InputSchema);
    }
    if (Pending.ExampleCall.IsValid()) {
      Proposal->SetObjectField(TEXT("exampleCall"), Pending.ExampleCall);
    }
    Sidecar->SetObjectField(TEXT("proposal"), Proposal);

    TSharedRef<FJsonObject> Origin = MakeShared<FJsonObject>();
    Origin->SetStringField(TEXT("text"), LastUserPromptText);
    Sidecar->SetObjectField(TEXT("originatingPrompt"), Origin);

    Sidecar->SetStringField(TEXT("status"), TEXT("pending"));

    if (!SaveJsonFile(OutPath, Sidecar)) {
      UE_LOG(UAgent::LogUAgent, Warning,
             TEXT("propose_missing_tool: failed to write sidecar to '%s' — "
                  "downgrading Accept to Cancel."),
             *OutPath);
      MessageLog->SetProposalState(
          ProposalId, FACPChatMessageItem::EProposalState::Cancelled);
      if (Pending.Complete)
        Pending.Complete(UAgent::EProposalOutcome::Cancelled);
      return;
    }

    MessageLog->AppendSystem(
        FString::Printf(TEXT("Proposal saved: %s"), *OutPath),
        FLinearColor(0.5f, 0.85f, 0.5f, 1.0f));
  }

  FACPChatMessageItem::EProposalState NewState =
      FACPChatMessageItem::EProposalState::Cancelled;
  switch (Decision) {
  case EProposalRowDecision::Accepted:
    NewState = FACPChatMessageItem::EProposalState::Accepted;
    break;
  case EProposalRowDecision::Skipped:
    NewState = FACPChatMessageItem::EProposalState::Skipped;
    break;
  case EProposalRowDecision::Cancelled:
  default:
    NewState = FACPChatMessageItem::EProposalState::Cancelled;
    break;
  }
  MessageLog->SetProposalState(ProposalId, NewState);

  if (Pending.Complete) {
    Pending.Complete(DecisionToOutcome(Decision));
  }
}

void SACPChatWindow::OnProposalReplayDecided(const FString &ProposalId,
                                             bool bRetry) {
  // Find the row to grab the sidecar path + saved prompt.
  FString SidecarPath;
  FString SavedPrompt;
  for (const FACPChatMessageItemRef &Item : MessageLog->GetMessages()) {
    if (Item->Role == FACPChatMessageItem::ERole::ProposalReplay &&
        Item->ProposalId == ProposalId) {
      SidecarPath = Item->ProposalSidecarPath;
      SavedPrompt = Item->Text;
      break;
    }
  }
  if (SidecarPath.IsEmpty())
    return;

  const FString TerminalStatus = bRetry ? TEXT("replayed") : TEXT("discarded");
  if (!MarkSidecarStatus(SidecarPath, TerminalStatus)) {
    UE_LOG(UAgent::LogUAgent, Warning,
           TEXT("propose_missing_tool: failed to mark sidecar '%s' as '%s'"),
           *SidecarPath, *TerminalStatus);
  }

  MessageLog->SetProposalState(
      ProposalId, bRetry ? FACPChatMessageItem::EProposalState::Replayed
                         : FACPChatMessageItem::EProposalState::Dismissed);

  if (bRetry && InputBox.IsValid() && !SavedPrompt.IsEmpty()) {
    InputBox->SetText(FText::FromString(SavedPrompt));
    // The developer can edit the prompt before submitting if they want; we
    // intentionally don't auto-send so they can inspect tool availability
    // first via /tools or by glancing at the chat.
    FSlateApplication::Get().SetKeyboardFocus(InputBox);
  }
}

void SACPChatWindow::ScanForPendingProposals() {
  if (!UAgent::Common::IsDeveloperToolingEnabled()) {
    return;
  }
  if (!MessageLog.IsValid())
    return;

  const FString Dir = UAgent::Common::GetProposalsDir();
  TArray<FString> Files;
  IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.json")), /*Files=*/true,
                                /*Dirs=*/false);

  // Files are sorted lexicographically by name; our timestamp-prefixed
  // filenames make that chronological order. Scan the whole list, pick the
  // pending entries, and (cheaply) prune terminal-state entries beyond the
  // most recent 50 to keep the directory bounded over time.
  Files.Sort();

  TArray<FString> TerminalEntries;
  int32 PendingShown = 0;

  for (const FString &Name : Files) {
    const FString FullPath = Dir / Name;
    TSharedPtr<FJsonObject> Obj;
    if (!LoadJsonFile(FullPath, Obj)) {
      continue;
    }
    int32 Schema = 0;
    Obj->TryGetNumberField(TEXT("schemaVersion"), Schema);
    if (Schema != 1)
      continue;

    FString Status;
    Obj->TryGetStringField(TEXT("status"), Status);
    if (Status != TEXT("pending")) {
      TerminalEntries.Add(FullPath);
      continue;
    }

    FString Id;
    Obj->TryGetStringField(TEXT("id"), Id);
    const TSharedPtr<FJsonObject> *ProposalObj = nullptr;
    Obj->TryGetObjectField(TEXT("proposal"), ProposalObj);
    FString ProposedName;
    if (ProposalObj && ProposalObj->IsValid())
      (*ProposalObj)->TryGetStringField(TEXT("name"), ProposedName);

    const TSharedPtr<FJsonObject> *OriginObj = nullptr;
    Obj->TryGetObjectField(TEXT("originatingPrompt"), OriginObj);
    FString OriginText;
    if (OriginObj && OriginObj->IsValid())
      (*OriginObj)->TryGetStringField(TEXT("text"), OriginText);

    if (Id.IsEmpty() || OriginText.IsEmpty()) {
      // Defensive — incomplete sidecar, skip rather than crash.
      continue;
    }

    if (PendingShown == 0) {
      MessageLog->AppendSystem(
          TEXT("Pending tool proposals from a prior session — Retry to "
               "replay the prompt with the new tool, or Dismiss."),
          FLinearColor(0.85f, 0.85f, 0.45f, 1.0f));
    }
    MessageLog->AppendProposalReplay(Id, ProposedName, OriginText, FullPath);
    ++PendingShown;
  }

  // LRU prune of terminal-state files beyond N=50 most recent. Files
  // sorted ascending by timestamp → drop the oldest. Cheap; runs once per
  // session start.
  constexpr int32 KeepTerminal = 50;
  if (TerminalEntries.Num() > KeepTerminal) {
    const int32 ToDelete = TerminalEntries.Num() - KeepTerminal;
    for (int32 i = 0; i < ToDelete; ++i) {
      IFileManager::Get().Delete(*TerminalEntries[i],
                                 /*RequireExists=*/false,
                                 /*EvenReadOnly=*/true, /*Quiet=*/true);
    }
  }
}

#undef LOCTEXT_NAMESPACE
