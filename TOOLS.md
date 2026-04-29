# Tools

Every tool is exposed over both ACP (as `_ue5/<name>`) and MCP (as `<name>`), backed by a single `IACPTool` implementation in the editor. See [CONTRIBUTING.md](CONTRIBUTING.md) for how to add one.

Each tool classifies itself as read-only or mutating. The chat window's permission dropdown uses this classification: **Read Only** mode rejects mutating tools, **Default** mode auto-allows reads and prompts on mutations, **Full Access** allows everything. The MCP server also emits the spec's `annotations.readOnlyHint` for read-only tools so external MCP clients can classify them correctly. See [Permission classification](CONTRIBUTING.md#permission-classification) in CONTRIBUTING.md.

## Filesystem

- **fs/read_text_file** — Read a text file under the project directory.
- **fs/write_text_file** — Write a text file under the project directory.

## Session

- **session/request_permission** — Answered based on the **Mode** dropdown at the bottom of the chat window. *Full Access* always allows; *Read Only* allows reads and rejects mutations; *Default* auto-allows reads and surfaces a permission card with **Accept / Cancel** for mutations.

## Blueprints

- **read_blueprint** — Dump a Blueprint as JSON: graphs, nodes, pins, and connections.
- **create_node** — Spawn a node in a Blueprint graph; returns the new node's GUID.
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
- **get_component_properties** — Read properties from a Blueprint's SCS component template or a placed actor's live component.
- **add_interface** — Add an interface implementation to a Blueprint (C++ or Blueprint interface).

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
- **read_editor_log** — Tail the Output Log, filterable by category and severity.
- **run_console_command** — Execute an editor console command and capture output.
- **play_in_editor** — Start Play-in-Editor; optional `map`, `netMode`, `numPlayers`, `simulate`, `startLocation`, `startRotation`, `separateServer`.
- **stop_pie** — Stop the active Play-in-Editor session.
- **save_asset** — Save an asset by path, the current level, or all dirty levels.
- **capture_viewport** — Capture the editor viewport to a PNG under `Saved/UAgent/Screenshots`; optional base64 inline.

## Level

- **list_level_actors** — List actors in the edited level with class, transform, tags, and components.
- **get_actor_properties** — Dump a placed actor's properties as JSON.
- **set_actor_property** — Set a property on a placed actor via `FProperty::ImportText`.
- **spawn_actor** — Place an actor in the edited level from a Blueprint or class; returns its name, label, and path.
- **destroy_actor** — Destroy one or more placed actors in the edited level; accepts `actor` or `actors[]` and reports failures per actor.
- **create_level** — Create a new level asset and open it; optional `template` seeds from an existing level.
- **set_world_settings** — Update the current level's AWorldSettings (convenience `gameMode` + generic `properties` map).

## Config

- **read_config** — Read a config entry via `GConfig`.
- **write_config** — Write a config entry via `GConfig` (project-dir files only).
