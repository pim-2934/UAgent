# Tools

Every tool is exposed over both ACP (as `_ue5/<name>`) and MCP (as `<name>`), backed by a single `IACPTool` implementation in the editor. See [CONTRIBUTING.md](CONTRIBUTING.md) for how to add one.

Each tool classifies itself as read-only or mutating. When the agent asks for permission, read-only tools auto-allow and mutating tools surface an inline **Accept / Cancel** card in the chat. The MCP server also emits the spec's `annotations.readOnlyHint` for read-only tools so external MCP clients (Claude Desktop, Cursor, Zed) can classify them correctly. Agent-side session modes (Claude's *acceptEdits* / *bypassPermissions*, Codex's *full-access*) decide whether the agent calls `session/request_permission` at all — see [Permission classification](CONTRIBUTING.md#permission-classification) in CONTRIBUTING.md.

## Filesystem

- **fs/read_text_file** — Read a text file under the project directory.
- **fs/write_text_file** — Write a text file under the project directory.

## Session

- **session/request_permission** — Auto-allows read-only tools (ACP `kind` of `read` / `search` / `think` / `fetch`, or MCP tools whose registry entry reports `IsReadOnly()`); mutating tools defer to the user via an inline **Accept / Cancel** permission card. The agent's own session mode (Claude's *acceptEdits* / *bypassPermissions*, Codex's *full-access*, etc.) controls whether it asks in the first place.

## Blueprints

- **read_blueprint** — Dump a Blueprint as JSON: graphs, nodes, pins, and connections. Pass optional `nodeId` (with optional `graphName`) to return just one node's pins/links instead of the full dump.
- **create_node** — Spawn a node in a Blueprint graph (function call, variable get/set, event override, multicast-delegate bind/unbind/clear, or raw `UK2Node` subclass); returns the new node's GUID.
- **link_nodes** — Wire two pins, respecting `UEdGraphSchema_K2` type-checks.
- **set_pin_default** — Set the default value on an input pin; schema-validated.
- **delete_node** — Remove a node from a Blueprint graph; breaks its links and destroys it.
- **unlink_nodes** — Break the link between two specific pins (or all links on a pin).
- **create_blueprint** — Create a new Blueprint deriving from a given parent class.
- **compile_blueprint** — Compile a Blueprint and return errors, warnings, and notes.
- **add_component** — Add a component to an Actor Blueprint's construction script.
- **add_variable** — Add a member variable (supports containers and struct/enum/class types).
- **add_function** — Add an empty user function graph to a Blueprint.
- **add_event** — Add a custom event or override a parent-class event.
- **set_default_value** — Set a variable default or CDO property via `FProperty::ImportText`.
- **set_component_property** — Set a property on a component: an SCS template inside an Actor Blueprint, or a live component instance on a placed actor (e.g. `CubeMesh.StaticMesh`).
- **set_component_material** — Assign a material to a mesh component's material slot, either on a Blueprint SCS template or a placed actor's live instance. Wraps `UMeshComponent::SetMaterial`.
- **get_component_properties** — Read properties from a Blueprint's SCS component template, a placed editor-world actor's live component, or a running PIE-world component (pass `pie:true`, optionally with `controllerIndex` for the local player pawn or `actor` for a named PIE actor).
- **add_interface** — Add an interface implementation to a Blueprint (C++ or Blueprint interface).
- **add_widget** — Add a UWidget into a Widget Blueprint's WidgetTree. Without a parent the widget becomes the RootWidget; with a parent it's attached as a child of a named UPanelWidget and a default UPanelSlot is auto-created.
- **set_widget_property** — Set a property on a UWidget inside a Widget Blueprint's WidgetTree (e.g. Border `Background`, Image `ColorAndOpacity`, TextBlock `Text`).
- **set_widget_slot_property** — Set a property on the UPanelSlot of a child widget (anchors, offsets, padding, alignment, fill — slot class depends on the parent panel).
- **bind_component_delegate** — Bind a multicast delegate on a Blueprint's component to a new ComponentBoundEvent handler — the BP-idiomatic equivalent of right-clicking a component's delegate pin in the editor and choosing "Add Event → Bind Event to ...". Works for SCS, parent-BP, and inherited C++ default-subobject components. If a handler already exists for the (component, delegate) pair, returns its GUID instead of duplicating.

## Assets

- **list_assets** — Query the AssetRegistry by class, path prefix, and/or name substring.
- **find_references** — List packages that reference a given asset.
- **find_dependencies** — List packages a given asset depends on.
- **duplicate_asset** — Duplicate an asset to a new path.
- **grep_asset_json** — Regex-match a pattern against serialized JSON dumps of assets.
- **create_material** — Create a new `UMaterial` asset with constants wired into BaseColor/Emissive/Metallic/Roughness/Specular; supports unlit.
- **create_material_instance** — Create a `UMaterialInstanceConstant` parented to an existing material, with scalar/vector/texture parameter overrides.
- **create_data_asset** — Create a `UDataAsset` / `UPrimaryDataAsset` of a given subclass; optional ImportText property map.
- **create_input_action** — Create an Enhanced Input `UInputAction` with a given value type (bool / axis1d / axis2d / axis3d).
- **create_input_mapping_context** — Create a `UInputMappingContext`; optional `mappings` array of `{action, key}` pairs.
- **delete_asset** — Delete an asset by path. Destructive — incoming references aren't checked.
- **rename_asset** — Rename or move an asset; incoming references are updated.

## Reflection

- **get_class_info** — Reflect a UClass: parent, interfaces, properties, functions, and CDO defaults.
- **list_subclasses** — List all native and Blueprint subclasses of a given base class.
- **find_native_class** — Resolve a class name to its declaring header file.
- **read_header** — Resolve a class name and return the header's contents.
- **find_function** — Search UFunctions by name substring across all classes or a specific class.

## Data

- **read_data_table** — Dump rows of a UDataTable as JSON keyed by row name.
- **read_data_asset** — Read a UDataAsset or UPrimaryDataAsset as JSON.
- **list_gameplay_tags** — List registered gameplay tags, optionally filtered by prefix.
- **create_gameplay_tag** — Register a new gameplay tag in the project's tag INIs.
- **read_input_mappings** — List Enhanced Input actions and mapping contexts with their bindings.

## Editor

- **open_asset** — Open an asset in its default editor.
- **focus_in_content_browser** — Highlight an asset in the Content Browser.
- **read_editor_log** — Tail the Output Log, filterable by category and severity. Pass `sincePieStart:true` to clip results to log lines emitted after the most recent Play-In-Editor start.
- **run_console_command** — Execute an editor console command and capture output.
- **play_in_editor** — Start Play-in-Editor; optional `map`, `netMode`, `numPlayers`, `simulate`, `startLocation`, `startRotation`, `separateServer`.
- **stop_pie** — Stop the active Play-in-Editor session.
- **save_asset** — Save an asset by path, the current level, or all dirty levels.
- **capture_viewport** — Capture the editor viewport to a PNG under `Saved/UAgent/Screenshots`; optional base64 inline.

## Level

- **list_level_actors** — List actors in the edited level with class, transform, tags, and components.
- **get_actor_properties** — Dump an actor's properties as JSON. Default reads from the editor world; pass `pie:true` (with optional `controllerIndex` or `actor`) to read the running player pawn or a named PIE actor for live runtime inspection.
- **set_actor_property** — Set a property on a placed actor via `FProperty::ImportText`.
- **spawn_actor** — Place an actor in the edited level from a Blueprint or class; returns its name, label, and path.
- **destroy_actor** — Destroy one or more placed actors in the edited level; accepts `actor` or `actors[]` and reports failures per actor.
- **create_level** — Create a new level asset and open it; optional `template` seeds from an existing level.
- **set_world_settings** — Update the current level's AWorldSettings (convenience `gameMode` + generic `properties` map).

## Config

- **read_config** — Read a config entry via `GConfig`.
- **write_config** — Write a config entry via `GConfig` (project-dir files only).

## Skills

- **invoke_skill** — Load the full body of a named UAgent skill — opinionated markdown guides for UE5 subsystems (GAS, Replication, Enhanced Input) or project-specific topics. The available skills are listed in the system context block prepended to each session's first prompt. Default call (`{name}`) returns `{name, description, body, resources[], fromProject}`; for directory-based skills, `resources[]` enumerates sibling files (manifest, references, …) under the skill directory. Pass `{name, resource: "<rel-path>"}` to load one of those files instead, returning `{name, description, resource, content, fromProject}`. Plugin-shipped skills live in `Resources/Skills/` (flat `.md` or `<name>/SKILL.md` directories) and cover core UE5 only; per-project skills under `<ProjectDir>/UAgent/Skills/` extend with third-party-framework or in-house guides (and can override shipped skills by name). See [Adding a skill](CONTRIBUTING.md#adding-a-skill) in CONTRIBUTING.md.

## Developer (gated)

Only registered when **both** `UUAgentSettings::bDeveloperMode` is true *and* the plugin's `Source/UAgent/Private/Tools/` directory is writable. Off by default. Toggling the setting requires an editor restart to take effect — tool registration runs once in `StartupModule`.

- **propose_missing_tool** — Surface a clearly-missing UE5 editor tool when no existing one fits the user's intent. Pops a Proposal card in the chat (Accept / Skip / Cancel); on Accept, writes a sidecar JSON under `Saved/UAgent/Proposals/` carrying the proposed `name`/`description`/`whyNeeded`/`inputSchema`/`exampleCall` plus the originating user prompt, and tells the agent to halt. After the developer implements the new tool and restarts the editor, the next session shows a "pending tool proposal — retry?" banner that replays the saved prompt with the new tool now in `tools/list`. Cross-agent: portable to Claude, Codex, Gemini, etc. through standard MCP `tools/list` and the prompt-array standing instruction.
