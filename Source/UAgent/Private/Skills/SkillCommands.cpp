#include "SkillCommands.h"
#include "../Protocol/ACPTypes.h"
#include "SkillRegistry.h"

namespace UAgent {

TArray<FAvailableCommand> GetLocalSlashCommands() {
  const TArray<FSkillEntry> &Entries = FSkillRegistry::Get().GetAll();
  TArray<FAvailableCommand> Out;
  Out.Reserve(Entries.Num());
  for (const FSkillEntry &E : Entries) {
    FAvailableCommand C;
    C.Name = E.Name;
    C.Description = E.Description;
    // InputHint left empty — skills take a free-form question, no specific
    // argument shape worth advertising in the picker chrome.
    Out.Add(MoveTemp(C));
  }
  return Out;
}

bool TryExpandSkillCommand(FString &InOutText,
                           TArray<FContentBlock> &OutBlocks) {
  // First non-whitespace char must be `/`. Match the picker's own slash-
  // detection in SACPChatWindow::OnInputTextChanged so anything that opens
  // the popup also expands here.
  int32 SlashIdx = INDEX_NONE;
  for (int32 i = 0; i < InOutText.Len(); ++i) {
    const TCHAR Ch = InOutText[i];
    if (FChar::IsWhitespace(Ch))
      continue;
    if (Ch == TEXT('/'))
      SlashIdx = i;
    break;
  }
  if (SlashIdx == INDEX_NONE)
    return false;

  // Slug runs from the char after `/` up to the next whitespace. Bare `/`
  // (no slug) is not a match — leave it for the agent.
  const int32 SlugStart = SlashIdx + 1;
  int32 SlugEnd = SlugStart;
  while (SlugEnd < InOutText.Len() && !FChar::IsWhitespace(InOutText[SlugEnd]))
    ++SlugEnd;
  if (SlugEnd == SlugStart)
    return false;

  const FString Slug = InOutText.Mid(SlugStart, SlugEnd - SlugStart);

  FSkillRegistry &Reg = FSkillRegistry::Get();
  const FSkillEntry *Entry = Reg.Find(Slug);
  if (!Entry)
    return false;

  // From here on the slug is consumed — strip it from InOutText regardless
  // of whether the body read succeeds, so the agent never sees an
  // unhandled `/<slug>` literal.
  InOutText = InOutText.Mid(SlugEnd).TrimStart();

  FString Body;
  if (!Reg.LoadBody(*Entry, Body))
    return true; // strip-only; caller proceeds without doctrine block

  // Frame the body so the agent can distinguish user-loaded doctrine from
  // per-asset auto-context. Mirrors the catalog block's voice — terse
  // header, body verbatim.
  const FString Framed = FString::Printf(
      TEXT("User invoked the '%s' skill via /%s — opinionated guide loaded "
           "below. Treat as authoritative doctrine for the question that "
           "follows.\n\n%s"),
      *Entry->Name, *Entry->Name, *Body);

  OutBlocks.Add(FContentBlock::MakeText(Framed));
  return true;
}

} // namespace UAgent
