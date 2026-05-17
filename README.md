# UAgent

*AI agent for the Unreal Editor.*

UAgent embeds an **[Agent Client Protocol](https://agentclientprotocol.com) (ACP) client** and a **Model Context Protocol (MCP) server** inside UE5. Pair it with the **Claude CLI** (or any ACP-compatible agent — Gemini CLI, Zed's agent) and chat with an AI that reads and edits your Blueprints, assets, and level from inside the editor.

https://github.com/user-attachments/assets/1e08db26-0494-4bf5-b88e-bbbe2756e5bf

## Features

- **Chat window** dockable as a UE5 editor tab (Window → UAgent).
- **Streaming responses** — agent message chunks appended live.
- **Tool-call cards** — rendered inline with status (pending / in-progress / completed).
- **Auto context** — whatever assets you have open in the editor are attached to each message automatically, so the agent sees what you're working on. Toggle with **Auto-Include Open Assets**.
- **`AGENTS.md` project context** — on the first prompt of each session, if `<ProjectDir>/AGENTS.md` exists its contents are prepended as a project-context block so the agent sees your project's conventions before any per-asset context. UAgent follows the cross-agent `AGENTS.md` convention (also read by Codex / Cursor / Aider); edits between sessions are picked up automatically.
- **Skills** — opinionated markdown guides for UE5 subsystems (GAS, Replication, Enhanced Input) ship with the plugin under `Resources/Skills/`. Their names + one-line descriptions are prepended to every new session; the agent loads a full body on demand via the `invoke_skill` tool only when relevant — so the deep doctrine costs no context budget when you ask about something else. You can also load one yourself by typing `/<skill-name>` in the input (e.g. `/gas how do I stack a damage GE?`) — the slash-command picker lists every registered skill alongside the agent's own commands. Drop your own `.md` files under `<ProjectDir>/UAgent/Skills/` to add project-specific or third-party-framework guides (marketplace plugins, in-house systems, gameplay conventions); a project skill with the same `name:` as a shipped one **overrides** it. Each file needs frontmatter with `name:` and `description:` between `---` delimiters; everything after is the body. Re-scanned on every new session, so edits don't need an editor restart.
- **@-mention** — type `@` in the input to pick any project asset from a live-filtered list. Adds a chip you can remove.
- **Slash commands** — type `/` in the input to pick from the agent's advertised commands (per ACP `availableCommands` / `available_commands_update`) plus UAgent's local skill commands. The popup stays in sync with what the agent currently exposes; skills are merged in from the registered set.
- **Plan strip** — when the agent emits a structured plan (e.g. Claude's `/plan`-style task list), it renders as a sticky strip above the input row and updates in place as tasks complete.
- **Mode picker** — when the agent advertises session modes (Claude's *default* / *acceptEdits* / *plan* / *bypassPermissions*, Codex's *read-only* / *default* / *full-access*), a **Mode:** dropdown appears at the bottom of the chat. The mode set is whatever the backing agent broadcasts in `session/new`; switching sends `session/set_mode` and the agent's `current_mode_update` notification keeps the UI in sync. Your pick is persisted per user and re-applied on subsequent sessions when the mode is still offered.
- **Permission cards** — for mutating tools the agent didn't pre-approve under the current mode, an **Accept / Cancel** card with the raw arguments rendered as Markdown appears inline in the chat; read-only tools auto-allow.
- **Model picker** — when the agent advertises model options (the Claude CLI does), a **Model:** dropdown appears next to the mode dropdown. Your pick is persisted per user and re-applied on subsequent sessions when the model is still offered.
- **Usage indicator** — a small label under the Send button shows the running context-budget % and (when the agent reports it) the per-turn cost from `usage_update` notifications.
- **Session history** — when the agent supports it (Claude does), a **Recent** button in the header lists previous sessions and resumes the picked one via `session/load`.
- **Export transcript** — save the current chat as Markdown via the export button in the header.
- **Tools** — the agent can read/write project files, inspect and edit Blueprints, drive the editor, and more. See [TOOLS.md](TOOLS.md) for the full list.


## Setup

### Prerequisites

- Unreal Engine — tested on 5.7.
- A backing agent — an ACP-compatible CLI such as the Claude CLI, the Codex CLI, the Gemini CLI, or Zed's agent. See [Agent](#agent) below.

### Install the plugin

Drop (or clone) this repo into your project's `Plugins` folder so the layout looks like:

```
<YourProject>/
├── <YourProject>.uproject
└── Plugins/
    └── UAgent/
        └── UAgent.uplugin
```

Create the `Plugins` directory next to your `.uproject` if it doesn't exist yet. On next editor launch UE will prompt to build the module — accept, or build manually from your IDE. Enable the plugin under **Edit → Plugins → UAgent** if it isn't already.

### Agent

You need an ACP-compatible agent backend. The plugin is currently tested with the Claude CLI, but any ACP bridge will do — Gemini CLI, Zed's agent, or a custom one in any language.

#### Claude CLI

Install both components per their official instructions:

- **Claude Code CLI** — [setup docs](https://code.claude.com/docs/en/setup)
- **claude-agent-acp** (ACP adapter that bridges the CLI to this plugin) — [repository](https://github.com/agentclientprotocol/claude-agent-acp)

#### Codex CLI

Install both components per their official instructions:

- **OpenAI Codex CLI** — [installation guide](https://developers.openai.com/codex/cli)
- **codex-acp** (ACP adapter that bridges the CLI to this plugin) — [repository](https://github.com/zed-industries/codex-acp)

### Plugin settings

**Project Settings → Plugins → UAgent:**

| Field | Value |
| --- | --- |
| **Agent Command** | Path to the installed ACP adapter shim — e.g. `C:\nvm4w\nodejs\claude-agent-acp` (Claude) or `C:\nvm4w\nodejs\codex-acp` (Codex). On Windows an extensionless shim is fine — it's auto-upgraded to its `.cmd` sibling. `.js` paths are run via `node` from `PATH`. |
| **Extra Args** | Array of strings appended after the agent command. Usually empty. |
| **Auto-Include Open Assets** | ✓ (default) |
| **Max Blueprint Summary Chars** | `8000` (default) |
| **Request Timeout Seconds** | `300` (default, `0` disables) |
| **Enable MCP Server** | ✓ (default) |
| **MCP Server Port** | `47777` (default) |
| **Developer Mode** | ☐ (default). Exposes the `propose_missing_tool` flow to the agent. Also requires the plugin's `Source/UAgent/Private/Tools/` directory to be writable. Toggling requires an editor restart. See [TOOLS.md → Developer (gated)](TOOLS.md#developer-gated). |
| **Log Agent JSON** | ☐ (default). Logs every ACP JSON-RPC line crossing the transport (both outbound `>>` and inbound `<<`) to the Output Log under `LogUAgent`. Useful for debugging protocol-level issues. Takes effect immediately — no restart needed. |

Session Mode and Model are picked from the dropdowns at the bottom of the chat window (not in Project Settings) and persisted per user under `EditorPerProjectUserSettings.ini` so the choice doesn't leak into the project's shared config.

### First run

1. Open your project in the UE5 editor.
2. Window → **UAgent**.
3. Status label should move through *starting → initializing → creating session → ready*.
4. Type a question in the input. Hit Enter to send (Shift+Enter for a newline).

If startup fails, check **Window → Developer Tools → Output Log** and filter by `LogUAgent` — the adapter's stderr goes there verbatim.
