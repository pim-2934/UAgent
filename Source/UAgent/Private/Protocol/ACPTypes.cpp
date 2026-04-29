#include "ACPTypes.h"

namespace UAgent {
DEFINE_LOG_CATEGORY(LogUAgent);

FContentBlock FContentBlock::MakeText(const FString &InText) {
  FContentBlock B;
  B.Kind = EKind::Text;
  B.Text = InText;
  return B;
}

FContentBlock FContentBlock::MakeResourceLink(const FString &InUri,
                                              const FString &InName,
                                              const FString &InMime,
                                              int64 InSize) {
  FContentBlock B;
  B.Kind = EKind::ResourceLink;
  B.LinkUri = InUri;
  B.LinkName = InName;
  B.LinkMimeType = InMime;
  B.LinkSize = InSize;
  return B;
}

FContentBlock FContentBlock::MakeResource(const FString &InUri,
                                          const FString &InMime,
                                          const FString &InText) {
  FContentBlock B;
  B.Kind = EKind::Resource;
  B.ResourceUri = InUri;
  B.ResourceMimeType = InMime;
  B.ResourceText = InText;
  return B;
}

TSharedRef<FJsonObject> FContentBlock::ToJson() const {
  TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
  switch (Kind) {
  case EKind::Text:
    O->SetStringField(TEXT("type"), TEXT("text"));
    O->SetStringField(TEXT("text"), Text);
    break;
  case EKind::Resource: {
    O->SetStringField(TEXT("type"), TEXT("resource"));
    TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
    R->SetStringField(TEXT("uri"), ResourceUri);
    if (!ResourceMimeType.IsEmpty()) {
      R->SetStringField(TEXT("mimeType"), ResourceMimeType);
    }
    if (!ResourceText.IsEmpty()) {
      R->SetStringField(TEXT("text"), ResourceText);
    } else if (!ResourceBlob.IsEmpty()) {
      R->SetStringField(TEXT("blob"), ResourceBlob);
    }
    O->SetObjectField(TEXT("resource"), R);
    break;
  }
  case EKind::ResourceLink:
    O->SetStringField(TEXT("type"), TEXT("resource_link"));
    O->SetStringField(TEXT("uri"), LinkUri);
    if (!LinkName.IsEmpty()) {
      O->SetStringField(TEXT("name"), LinkName);
    }
    if (!LinkMimeType.IsEmpty()) {
      O->SetStringField(TEXT("mimeType"), LinkMimeType);
    }
    if (LinkSize >= 0) {
      O->SetNumberField(TEXT("size"), static_cast<double>(LinkSize));
    }
    break;
  case EKind::Image:
    O->SetStringField(TEXT("type"), TEXT("image"));
    O->SetStringField(TEXT("mimeType"), MediaMimeType);
    O->SetStringField(TEXT("data"), MediaDataBase64);
    break;
  case EKind::Audio:
    O->SetStringField(TEXT("type"), TEXT("audio"));
    O->SetStringField(TEXT("mimeType"), MediaMimeType);
    O->SetStringField(TEXT("data"), MediaDataBase64);
    break;
  }
  return O;
}

bool FContentBlock::FromJson(const TSharedRef<FJsonObject> &Obj,
                             FContentBlock &Out) {
  const FString Type = Obj->GetStringField(TEXT("type"));
  if (Type == TEXT("text")) {
    Out.Kind = EKind::Text;
    Obj->TryGetStringField(TEXT("text"), Out.Text);
    return true;
  }
  if (Type == TEXT("resource")) {
    Out.Kind = EKind::Resource;
    const TSharedPtr<FJsonObject> *R = nullptr;
    if (Obj->TryGetObjectField(TEXT("resource"), R) && R && R->IsValid()) {
      (*R)->TryGetStringField(TEXT("uri"), Out.ResourceUri);
      (*R)->TryGetStringField(TEXT("mimeType"), Out.ResourceMimeType);
      (*R)->TryGetStringField(TEXT("text"), Out.ResourceText);
      (*R)->TryGetStringField(TEXT("blob"), Out.ResourceBlob);
    }
    return true;
  }
  if (Type == TEXT("resource_link")) {
    Out.Kind = EKind::ResourceLink;
    Obj->TryGetStringField(TEXT("uri"), Out.LinkUri);
    Obj->TryGetStringField(TEXT("name"), Out.LinkName);
    Obj->TryGetStringField(TEXT("mimeType"), Out.LinkMimeType);
    double SizeD = -1.0;
    if (Obj->TryGetNumberField(TEXT("size"), SizeD)) {
      Out.LinkSize = static_cast<int64>(SizeD);
    }
    return true;
  }
  if (Type == TEXT("image") || Type == TEXT("audio")) {
    Out.Kind = (Type == TEXT("image")) ? EKind::Image : EKind::Audio;
    Obj->TryGetStringField(TEXT("mimeType"), Out.MediaMimeType);
    Obj->TryGetStringField(TEXT("data"), Out.MediaDataBase64);
    return true;
  }
  return false;
}

bool FConfigOption::FromJson(const TSharedRef<FJsonObject> &Obj,
                             FConfigOption &Out) {
  if (!Obj->TryGetStringField(TEXT("id"), Out.Id) || Out.Id.IsEmpty()) {
    return false;
  }
  Obj->TryGetStringField(TEXT("category"), Out.Category);
  Obj->TryGetStringField(TEXT("currentValue"), Out.CurrentValue);

  const TArray<TSharedPtr<FJsonValue>> *Arr = nullptr;
  if (Obj->TryGetArrayField(TEXT("options"), Arr) && Arr) {
    Out.Options.Reserve(Arr->Num());
    for (const TSharedPtr<FJsonValue> &V : *Arr) {
      const TSharedPtr<FJsonObject> *ChoiceObj = nullptr;
      if (!V->TryGetObject(ChoiceObj) || !ChoiceObj || !ChoiceObj->IsValid()) {
        continue;
      }
      FConfigOptionChoice C;
      if (!(*ChoiceObj)->TryGetStringField(TEXT("value"), C.Value))
        continue;
      (*ChoiceObj)->TryGetStringField(TEXT("name"), C.Name);
      Out.Options.Add(MoveTemp(C));
    }
  }
  return true;
}

void ParseConfigOptions(const TArray<TSharedPtr<FJsonValue>> &In,
                        TArray<FConfigOption> &Out) {
  Out.Reset();
  Out.Reserve(In.Num());
  for (const TSharedPtr<FJsonValue> &V : In) {
    const TSharedPtr<FJsonObject> *Obj = nullptr;
    if (!V->TryGetObject(Obj) || !Obj || !Obj->IsValid())
      continue;
    FConfigOption Opt;
    if (FConfigOption::FromJson(Obj->ToSharedRef(), Opt)) {
      Out.Add(MoveTemp(Opt));
    }
  }
}

EStopReason ParseStopReason(const FString &In) {
  if (In == TEXT("end_turn"))
    return EStopReason::EndTurn;
  if (In == TEXT("max_tokens"))
    return EStopReason::MaxTokens;
  if (In == TEXT("max_turn_requests"))
    return EStopReason::MaxTurnRequests;
  if (In == TEXT("refusal"))
    return EStopReason::Refusal;
  if (In == TEXT("cancelled"))
    return EStopReason::Cancelled;
  return EStopReason::Unknown;
}

const TCHAR *StopReasonToString(EStopReason In) {
  switch (In) {
  case EStopReason::EndTurn:
    return TEXT("end_turn");
  case EStopReason::MaxTokens:
    return TEXT("max_tokens");
  case EStopReason::MaxTurnRequests:
    return TEXT("max_turn_requests");
  case EStopReason::Refusal:
    return TEXT("refusal");
  case EStopReason::Cancelled:
    return TEXT("cancelled");
  default:
    return TEXT("unknown");
  }
}

static bool ParseContentField(const TSharedPtr<FJsonObject> &Parent,
                              FContentBlock &Out) {
  const TSharedPtr<FJsonObject> *ContentObj = nullptr;
  if (Parent->TryGetObjectField(TEXT("content"), ContentObj) && ContentObj &&
      ContentObj->IsValid()) {
    return FContentBlock::FromJson(ContentObj->ToSharedRef(), Out);
  }
  return false;
}

bool FSessionUpdate::FromJson(const TSharedRef<FJsonObject> &Params,
                              FSessionUpdate &Out) {
  Params->TryGetStringField(TEXT("sessionId"), Out.SessionId);
  Out.RawObject = Params;

  const TSharedPtr<FJsonObject> *UpdateObj = nullptr;
  if (!Params->TryGetObjectField(TEXT("update"), UpdateObj) || !UpdateObj ||
      !UpdateObj->IsValid()) {
    return false;
  }

  FString Kind;
  if (!(*UpdateObj)->TryGetStringField(TEXT("sessionUpdate"), Kind)) {
    return false;
  }

  if (Kind == TEXT("user_message_chunk")) {
    Out.Kind = EKind::UserMessageChunk;
    ParseContentField(*UpdateObj, Out.Content);
    return true;
  }
  if (Kind == TEXT("agent_message_chunk")) {
    Out.Kind = EKind::AgentMessageChunk;
    ParseContentField(*UpdateObj, Out.Content);
    return true;
  }
  if (Kind == TEXT("agent_thought_chunk")) {
    Out.Kind = EKind::AgentThoughtChunk;
    ParseContentField(*UpdateObj, Out.Content);
    return true;
  }

  if (Kind == TEXT("tool_call") || Kind == TEXT("tool_call_update")) {
    Out.Kind =
        (Kind == TEXT("tool_call")) ? EKind::ToolCall : EKind::ToolCallUpdate;
    (*UpdateObj)->TryGetStringField(TEXT("toolCallId"), Out.ToolCallId);
    (*UpdateObj)->TryGetStringField(TEXT("title"), Out.ToolCallTitle);
    (*UpdateObj)->TryGetStringField(TEXT("kind"), Out.ToolCallKind);
    (*UpdateObj)->TryGetStringField(TEXT("status"), Out.ToolCallStatus);
    const TArray<TSharedPtr<FJsonValue>> *ContentArr = nullptr;
    if ((*UpdateObj)->TryGetArrayField(TEXT("content"), ContentArr)) {
      for (const TSharedPtr<FJsonValue> &V : *ContentArr) {
        const TSharedPtr<FJsonObject> *BlockOuter = nullptr;
        if (V->TryGetObject(BlockOuter) && BlockOuter &&
            BlockOuter->IsValid()) {
          // Tool-call content entries wrap a content block under
          // "type":"content".
          const TSharedPtr<FJsonObject> *Inner = nullptr;
          if ((*BlockOuter)->TryGetObjectField(TEXT("content"), Inner) &&
              Inner && Inner->IsValid()) {
            FContentBlock CB;
            if (FContentBlock::FromJson(Inner->ToSharedRef(), CB)) {
              Out.ToolCallContent.Add(CB);
            }
          } else {
            FContentBlock CB;
            if (FContentBlock::FromJson(BlockOuter->ToSharedRef(), CB)) {
              Out.ToolCallContent.Add(CB);
            }
          }
        }
      }
    }
    return true;
  }

  if (Kind == TEXT("plan")) {
    Out.Kind = EKind::Plan;
    return true;
  }
  if (Kind == TEXT("available_commands_update")) {
    Out.Kind = EKind::AvailableCommandsUpdate;
    return true;
  }
  if (Kind == TEXT("current_mode_update")) {
    Out.Kind = EKind::CurrentModeUpdate;
    return true;
  }
  if (Kind == TEXT("config_option_update")) {
    Out.Kind = EKind::ConfigOptionUpdate;
    const TArray<TSharedPtr<FJsonValue>> *Arr = nullptr;
    if ((*UpdateObj)->TryGetArrayField(TEXT("configOptions"), Arr) && Arr) {
      ParseConfigOptions(*Arr, Out.ConfigOptions);
    }
    return true;
  }

  Out.Kind = EKind::Raw;
  return true;
}

TArray<TSharedPtr<FJsonValue>>
ContentBlocksToJson(const TArray<FContentBlock> &Blocks) {
  TArray<TSharedPtr<FJsonValue>> Out;
  Out.Reserve(Blocks.Num());
  for (const FContentBlock &B : Blocks) {
    Out.Add(MakeShared<FJsonValueObject>(B.ToJson()));
  }
  return Out;
}
} // namespace UAgent
