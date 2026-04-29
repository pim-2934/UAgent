# UAgent

*AI agent for the Unreal Editor.*

UAgent embeds an **[Agent Client Protocol](https://agentclientprotocol.com) (ACP) client** and a **Model Context Protocol (MCP) server** inside UE5. Pair it with the **Claude CLI** (or any ACP-compatible agent — Gemini CLI, Zed's agent) and chat with an AI that reads and edits your Blueprints, assets, and level from inside the editor.

https://github.com/user-attachments/assets/1e08db26-0494-4bf5-b88e-bbbe2756e5bf

## Features

- **Chat window** dockable as a UE5 editor tab (Window → UAgent).
- **Streaming responses** — agent message chunks appended live.
- **Tool-call cards** — rendered inline with status (pending / in-progress / completed).
- **Auto context** — whatever assets you have open in the editor are attached to each message automatically, so the agent sees what you're working on. Toggle with **Auto-Include Open Assets**.
- **@-mention** — type `@` in the input to pick any project asset from a live-filtered list. Adds a chip you can remove.
- **Permission gating** — pick **Full Access**, **Read Only**, or **Default** from the dropdown at the bottom of the chat. In **Default** mode, mutating tool calls surface a permission card with an **Accept / Cancel** prompt and the raw arguments the agent wants to pass; read-only tools auto-allow. The chosen mode is persisted per user.
- **Model picker** — when the agent advertises model options (the Claude CLI does), a **Model:** dropdown appears next to the mode dropdown. Your pick is persisted per user and re-applied on subsequent sessions when the model is still offered.
- **Export transcript** — save the current chat as Markdown via the export button in the header.
- **Tools** — the agent can read/write project files, inspect and edit Blueprints, drive the editor, and more. See [TOOLS.md](TOOLS.md) for the full list.


## Setup

### Prerequisites

- Unreal Engine — tested on 5.7.
- A backing agent — an ACP-compatible CLI such as the Claude CLI (recommended), the Codex CLI, the Gemini CLI, or Zed's agent. See [Agent](#agent) below.

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

#### Claude CLI (recommended)

Install the Claude CLI:

```
npm install -g @anthropic-ai/claude-code
```

Authenticate once — run `claude login` and follow the prompt, or set `ANTHROPIC_API_KEY` in your environment.

Install the ACP adapter that bridges the CLI to this plugin:

```
npm install -g @agentclientprotocol/claude-agent-acp
```

#### Codex CLI

Install the Codex CLI:

```
npm install -g @openai/codex
```

Authenticate once — run `codex login` and follow the prompt, or set `OPENAI_API_KEY` (or `CODEX_API_KEY`) in your environment.

Install the ACP adapter that bridges the CLI to this plugin:

```
npm install -g @zed-industries/codex-acp
```

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

Permission Mode and Model are picked from the dropdowns at the bottom of the chat window (not in Project Settings) and persisted per user under `EditorPerProjectUserSettings.ini` so the choice doesn't leak into the project's shared config.

### First run

1. Open your project in the UE5 editor.
2. Window → **UAgent**.
3. Status label should move through *starting → initializing → creating session → ready*.
4. Type a question in the input. Hit Enter to send (Shift+Enter for a newline).

If startup fails, check **Window → Developer Tools → Output Log** and filter by `LogUAgent` — the adapter's stderr goes there verbatim.
