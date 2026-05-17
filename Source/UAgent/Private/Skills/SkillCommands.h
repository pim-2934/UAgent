#pragma once

#include "CoreMinimal.h"

namespace UAgent {

struct FAvailableCommand;
struct FContentBlock;

/**
 * UAgent's local slash commands — entries surfaced into the chat window's
 * command picker that are NOT advertised by the backing ACP agent.
 *
 * Today there is exactly one source: each registered FSkillRegistry entry
 * projects into one local slash command. Typing `/<skill-slug> <question>`
 * and pressing send strips the prefix and injects the skill's markdown body
 * as a context block — the same body `invoke_skill` would return if the
 * agent had decided to call it autonomously. The two surfaces (agent-pull
 * tool + user-push slash command) share the same registry, the same body,
 * and the same framing. This header is just the user-push half.
 *
 * Collision rule: if a local command shares a name with one the agent
 * advertises, the local command wins (TryExpandSkillCommand matches first,
 * so the prompt never reaches the agent as `/<slug>` literal). UE5 subsystem
 * slugs (`gas`, `replication`, …) are unlikely to collide with agent
 * commands (`/plan`, `/compact`, `/init`) in practice.
 */

/** Project each registered skill into one slash-command entry. The picker
 *  doesn't distinguish local from agent-advertised — same row template,
 *  same OnCommandPicked path. Empty when no skills are registered. */
TArray<FAvailableCommand> GetLocalSlashCommands();

/**
 * If InOutText starts with `/<slug>` (optionally preceded by whitespace) and
 * <slug> names a registered skill, strip the prefix and emit a framed skill
 * body block.
 *
 * Returns true on a slug match. On false both arguments are untouched and
 * the caller forwards InOutText to the agent unchanged (so an unrecognized
 * `/foo` still reaches the agent as a literal — that's how agent-advertised
 * commands work over ACP).
 *
 * Edge cases:
 *  - Slug matched but body read failed (file vanished between scan and send):
 *    InOutText is stripped, OutBlocks is left empty, return true. The agent
 *    sees the user's bare question without doctrine rather than an unhandled
 *    `/<slug>` token.
 *  - Match leaves InOutText empty (e.g. user submitted bare `/<slug>`): we
 *    still return true and append the body. The caller is expected to detect
 *    the empty-text case and drop the turn — matches the existing empty-
 *    input behaviour in OnSendClicked.
 */
bool TryExpandSkillCommand(FString &InOutText,
                           TArray<FContentBlock> &OutBlocks);

} // namespace UAgent
