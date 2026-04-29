#include "SACPChatWindow.h"

#include "AssetContextRegistry.h"
#include "ChatMessageLog.h"
#include "Protocol/ACPClient.h"
#include "Protocol/ACPTypes.h"
#include "SACPContextStrip.h"
#include "SACPMentionPicker.h"
#include "SACPMessageList.h"
#include "Tools/Session/PermissionBroker.h"
#include "UAgent.h"
#include "UAgentSettings.h"

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
          this, &SACPChatWindow::OnPermissionRowDecided));

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
  // Drop the broker handler first so a destruction-time agent request can't
  // re-enter into a half-destroyed window.
  UAgent::FPermissionBroker::Get().SetHandler({});

  // Anything still pending is left unanswered — the agent treats no response
  // as cancelled when the transport dies, but be explicit so we never leak.
  for (auto &KV : PendingPermissions) {
    if (KV.Value)
      KV.Value(UAgent::EPermissionOutcome::Deny);
  }
  PendingPermissions.Reset();

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
  // Stable backing storage for SComboBox — its OptionsSource and
  // InitiallySelectedItem hold raw pointers to these TSharedPtr<FString>s.
  struct FModeEntry {
    EACPPermissionMode Mode;
    const TCHAR *Label;
  };
  static const FModeEntry EntryTable[] = {
      {EACPPermissionMode::ReadOnly, TEXT("Read Only")},
      {EACPPermissionMode::Default, TEXT("Default")},
      {EACPPermissionMode::FullAccess, TEXT("Full Access")},
  };
  static TArray<TSharedPtr<FString>> ModeStrings = []() {
    TArray<TSharedPtr<FString>> Out;
    for (const FModeEntry &E : EntryTable)
      Out.Add(MakeShared<FString>(E.Label));
    return Out;
  }();

  auto IndexForMode = [](EACPPermissionMode M) -> int32 {
    for (int32 i = 0; i < UE_ARRAY_COUNT(EntryTable); ++i) {
      if (EntryTable[i].Mode == M)
        return i;
    }
    return UE_ARRAY_COUNT(EntryTable) - 1; // FullAccess fallback
  };
  auto ModeForLabel = [](const FString &Label) -> EACPPermissionMode {
    for (const FModeEntry &E : EntryTable) {
      if (Label.Equals(E.Label))
        return E.Mode;
    }
    return EACPPermissionMode::FullAccess;
  };

  const int32 InitialIdx = IndexForMode(UAgent::PermissionModeStore::Load());

  // Agent-advertised dropdowns (currently just Model) live in this container,
  // populated lazily by RefreshAgentSettings. Empty when nothing's advertised.
  TSharedRef<SHorizontalBox> AgentBox = SNew(SHorizontalBox);
  AgentSettingsContainer = AgentBox;

  return SNew(SBorder)
      .BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
      .Padding(FMargin(6, 3))
          [SNew(SHorizontalBox) +
           SHorizontalBox::Slot()
               .AutoWidth()
               .VAlign(VAlign_Center)
               .Padding(0, 0, 6,
                        0)[SNew(STextBlock)
                               .Text(LOCTEXT("PermissionModeLabel", "Mode:"))] +
           SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
               [SNew(SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&ModeStrings)
                    .InitiallySelectedItem(ModeStrings[InitialIdx])
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) {
                      return SNew(STextBlock)
                          .Text(Item.IsValid() ? FText::FromString(*Item)
                                               : FText::GetEmpty());
                    })
                    .OnSelectionChanged_Lambda(
                        [ModeForLabel](TSharedPtr<FString> NewVal,
                                       ESelectInfo::Type) {
                          if (!NewVal.IsValid())
                            return;
                          const EACPPermissionMode New = ModeForLabel(*NewVal);
                          if (UAgent::PermissionModeStore::Load() == New)
                            return;
                          UAgent::PermissionModeStore::Save(New);
                        })[SNew(STextBlock).Text_Lambda([]() {
                      const EACPPermissionMode M =
                          UAgent::PermissionModeStore::Load();
                      for (const FModeEntry &E : EntryTable) {
                        if (E.Mode == M)
                          return FText::FromString(E.Label);
                      }
                      return FText::FromString(
                          EntryTable[UE_ARRAY_COUNT(EntryTable) - 1].Label);
                    })]] +
           SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[AgentBox]];
}

void SACPChatWindow::RefreshAgentSettings() {
  if (!AgentSettingsContainer.IsValid() || !Client.IsValid())
    return;

  // Wholesale rebuild — agents may legitimately change advertised sets and
  // dropdown counts at runtime via config_option_update.
  AgentSettingsContainer->ClearChildren();

  // One dropdown per advertised model config option. Other categories
  // (mode/thought_level/...) are intentionally ignored — only the "model"
  // selector is surfaced here.
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
  // Resolve any in-flight permission prompts before resetting the log — the
  // outgoing transport is about to die and the agent will never get our
  // answer anyway, but if the callback ever fires it must not address an
  // index in the freshly-reset log.
  for (auto &KV : PendingPermissions) {
    if (KV.Value)
      KV.Value(UAgent::EPermissionOutcome::Deny);
  }
  PendingPermissions.Reset();

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

  TArray<UAgent::FContentBlock> Blocks = BuildContextBlocks();
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

  // Once the session is established, push the user's saved model preference
  // to the agent if it advertised a model option that offers it. Skipped
  // silently when there's no preference, no model option, or the saved value
  // isn't in the option's list (different agent, dropped model, etc.).
  if (NewState == EClientState::Ready && Client.IsValid()) {
    const FString Saved = UAgent::ModelStore::Load();
    if (Saved.IsEmpty())
      return;
    for (const UAgent::FConfigOption &Opt : Client->GetConfigOptions()) {
      if (Opt.Category != TEXT("model"))
        continue;
      if (Opt.CurrentValue == Saved)
        return;
      const bool bAvailable = Opt.Options.ContainsByPredicate(
          [&Saved](const UAgent::FConfigOptionChoice &C) {
            return C.Value == Saved;
          });
      if (bAvailable) {
        Client->SetConfigOption(Opt.Id, Saved);
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

#undef LOCTEXT_NAMESPACE
