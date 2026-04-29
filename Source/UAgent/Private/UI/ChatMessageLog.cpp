#include "ChatMessageLog.h"

#include "../Protocol/ACPTypes.h"
#include "Misc/Guid.h"

void FChatMessageLog::CloseAgentTurnIfOpen() {
  if (CurrentAgentMessageIndex != INDEX_NONE &&
      Messages.IsValidIndex(CurrentAgentMessageIndex)) {
    Messages[CurrentAgentMessageIndex]->bTurnComplete = true;
    CurrentAgentMessageIndex = INDEX_NONE;
    OnAgentTurnEnded.Broadcast();
  } else {
    CurrentAgentMessageIndex = INDEX_NONE;
  }
}

void FChatMessageLog::AppendUser(const FString &Text,
                                 const TArray<FAssetData> &Contexts) {
  CloseAgentTurnIfOpen();
  TSharedRef<FACPChatMessageItem> M = MakeShared<FACPChatMessageItem>();
  M->Role = FACPChatMessageItem::ERole::User;
  M->Text = Text;
  M->Contexts = Contexts;
  Messages.Add(M);
  OnChanged.Broadcast();
}

void FChatMessageLog::AppendAgentChunk(const FString &Text) {
  if (CurrentAgentMessageIndex == INDEX_NONE ||
      !Messages.IsValidIndex(CurrentAgentMessageIndex) ||
      Messages[CurrentAgentMessageIndex]->Role !=
          FACPChatMessageItem::ERole::Agent) {
    TSharedRef<FACPChatMessageItem> M = MakeShared<FACPChatMessageItem>();
    M->Role = FACPChatMessageItem::ERole::Agent;
    M->Text = Text;
    M->bTurnComplete = false;
    Messages.Add(M);
    CurrentAgentMessageIndex = Messages.Num() - 1;
  } else {
    Messages[CurrentAgentMessageIndex]->Text += Text;
  }
  OnChanged.Broadcast();
}

void FChatMessageLog::AppendTool(const UAgent::FSessionUpdate &Update) {
  CloseAgentTurnIfOpen();
  TSharedRef<FACPChatMessageItem> M = MakeShared<FACPChatMessageItem>();
  M->Role = FACPChatMessageItem::ERole::Tool;
  M->ToolCallId = Update.ToolCallId;
  M->ToolCallTitle = Update.ToolCallTitle;
  M->ToolCallStatus =
      Update.ToolCallStatus.IsEmpty() ? TEXT("pending") : Update.ToolCallStatus;

  FString Body;
  for (const UAgent::FContentBlock &C : Update.ToolCallContent) {
    if (!Body.IsEmpty())
      Body += TEXT("\n");
    switch (C.Kind) {
    case UAgent::FContentBlock::EKind::Text:
      Body += C.Text;
      break;
    case UAgent::FContentBlock::EKind::Resource:
      Body += C.ResourceText;
      break;
    case UAgent::FContentBlock::EKind::ResourceLink:
      Body += FString::Printf(TEXT("→ %s"), *C.LinkUri);
      break;
    default:
      break;
    }
  }
  M->Text = Body;

  Messages.Add(M);
  ToolCallIndexById.Add(Update.ToolCallId, Messages.Num() - 1);
  OnChanged.Broadcast();
}

void FChatMessageLog::UpdateTool(const UAgent::FSessionUpdate &Update) {
  const int32 *Idx = ToolCallIndexById.Find(Update.ToolCallId);
  if (!Idx || !Messages.IsValidIndex(*Idx)) {
    // Never saw the initial tool_call — synthesize one.
    AppendTool(Update);
    return;
  }
  FACPChatMessageItem &M = Messages[*Idx].Get();
  if (!Update.ToolCallStatus.IsEmpty())
    M.ToolCallStatus = Update.ToolCallStatus;
  if (!Update.ToolCallTitle.IsEmpty())
    M.ToolCallTitle = Update.ToolCallTitle;

  FString Extra;
  for (const UAgent::FContentBlock &C : Update.ToolCallContent) {
    if (!Extra.IsEmpty())
      Extra += TEXT("\n");
    switch (C.Kind) {
    case UAgent::FContentBlock::EKind::Text:
      Extra += C.Text;
      break;
    case UAgent::FContentBlock::EKind::Resource:
      Extra += C.ResourceText;
      break;
    default:
      break;
    }
  }
  if (!Extra.IsEmpty()) {
    if (!M.Text.IsEmpty())
      M.Text += TEXT("\n");
    M.Text += Extra;
  }
  OnChanged.Broadcast();
}

void FChatMessageLog::AppendSystem(const FString &Text,
                                   const FLinearColor &Tint) {
  TSharedRef<FACPChatMessageItem> M = MakeShared<FACPChatMessageItem>();
  M->Role = FACPChatMessageItem::ERole::System;
  M->Text = Text;
  M->Tint = Tint;
  Messages.Add(M);
  OnChanged.Broadcast();
}

FString FChatMessageLog::AppendPermission(const FString &ToolTitle,
                                          const FString &ToolKind,
                                          const FString &ArgsPreview) {
  // Close any in-flight agent turn so the prompt card reads as a discrete
  // step, mirroring AppendTool's behavior.
  CloseAgentTurnIfOpen();
  TSharedRef<FACPChatMessageItem> M = MakeShared<FACPChatMessageItem>();
  M->Role = FACPChatMessageItem::ERole::Permission;
  M->PermissionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
  M->ToolCallTitle = ToolTitle;
  M->PermissionToolKind = ToolKind;
  M->PermissionArgsPreview = ArgsPreview;
  M->PermissionState = FACPChatMessageItem::EPermissionState::Pending;
  Messages.Add(M);
  OnChanged.Broadcast();
  return M->PermissionId;
}

void FChatMessageLog::SetPermissionState(
    const FString &PermissionId,
    FACPChatMessageItem::EPermissionState NewState) {
  if (PermissionId.IsEmpty())
    return;
  for (const FACPChatMessageItemRef &Item : Messages) {
    if (Item->Role == FACPChatMessageItem::ERole::Permission &&
        Item->PermissionId == PermissionId) {
      Item->PermissionState = NewState;
      OnChanged.Broadcast();
      return;
    }
  }
}

void FChatMessageLog::ApplySessionUpdate(const UAgent::FSessionUpdate &Update) {
  using UAgent::FSessionUpdate;
  switch (Update.Kind) {
  case FSessionUpdate::EKind::AgentMessageChunk:
  case FSessionUpdate::EKind::AgentThoughtChunk:
    if (Update.Content.Kind == UAgent::FContentBlock::EKind::Text) {
      AppendAgentChunk(Update.Content.Text);
    }
    break;
  case FSessionUpdate::EKind::ToolCall:
    AppendTool(Update);
    break;
  case FSessionUpdate::EKind::ToolCallUpdate:
    UpdateTool(Update);
    break;
  default:
    break;
  }
}

void FChatMessageLog::EndAgentTurn() { CloseAgentTurnIfOpen(); }

void FChatMessageLog::Reset() {
  Messages.Reset();
  ToolCallIndexById.Reset();
  CurrentAgentMessageIndex = INDEX_NONE;
  OnChanged.Broadcast();
}
