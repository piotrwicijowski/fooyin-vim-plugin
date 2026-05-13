# Binding Scopes

## Goal

Add a binding-scope feature so the same key mapping can resolve to different actions in different parts of fooyin.

V1 decisions already confirmed:

- Scope model: built-in named scopes
- Matching rule: most specific scope wins, with global fallback
- Compatibility: breaking change is acceptable; old binding-format compatibility is not required
- V1 coverage: `Global`, `PlaylistView`, `PlaylistOrganiser`
- Settings UI: add a scope column and scope selector in add/edit flows

## Problem Summary

Today bindings are keyed only by mode and key sequence.

- Persistence is currently grouped as `VimMotions/Bindings/{Mode}/{Keys}` in `src/vimmotionsbindingbackend.cpp`
- Effective runtime bindings are stored as `QHash<BindingMode, QList<BindingEntry>>`
- `VimHandler` dispatches only against `m_configBindings[mode]`
- Runtime already knows view context via `VimHandler::viewContext()` with at least:
  - `PlaylistView`
  - `PlaylistOrganiser`
  - `Other`

This means bindings that are meaningful only in one view still participate everywhere.

## Non-Goals

- Arbitrary user-defined scope strings in V1
- Raw Qt class-name matching in config
- Regex or inheritance-based scope selection
- Expanding V1 beyond playlist view and playlist organiser

## User-Facing Design

### Config shape

Use a single explicit scoped format for all bindings:

```ini
[VimMotions]
Bindings\Global\Normal\j=moveCursor:+1
Bindings\PlaylistView\Normal\j=moveCursor:+1
Bindings\PlaylistOrganiser\Normal\j=treeMoveSibling:+1
Bindings\PlaylistOrganiser\Normal\a=organiserCreatePlaylist
```

Meaning:

- `Bindings\{Scope}\{Mode}\{Keys}`: only supported form

### Built-in scope names

- `Global`
- `PlaylistView`
- `PlaylistOrganiser`

Persistence behavior:

- All bindings, including global ones, are stored with an explicit scope
- `Global` is a first-class persisted scope, not an implicit config representation
- Old scope-less binding entries are not supported by the new format

### Resolution rule

For a given key event and mode:

1. Resolve the active runtime scope from the focused/active view.
2. Search bindings for the exact active scope.
3. Fall back to `Global` if no exact-scope binding matches.
4. If both exact-scope and global bindings could match the same prefix/sequence, the exact-scope binding owns the sequence.

Examples:

- `j` in `PlaylistView` can mean `moveCursor:+1`
- `j` in `PlaylistOrganiser` can mean `treeMoveSibling:+1`
- `Ctrl+J` can remain global if desired
- `a` can exist only in `PlaylistOrganiser` and be inert elsewhere

## Runtime Scope Model

Introduce a binding-scope enum in the binding backend layer, separate from `VimHandler::ViewContext`.

Suggested shape:

```cpp
enum class BindingScope {
    Global,
    PlaylistView,
    PlaylistOrganiser,
};
```

Why keep it separate:

- `ViewContext` is a runtime classification helper in `VimHandler`
- `BindingScope` is persisted user configuration
- keeping them separate avoids leaking `Other` or `None` into persisted settings

Add a small conversion helper in `VimHandler`:

- `activeBindingScope()`
- `bindingScopeForView(QAbstractItemView*)`

Mapping in V1:

- `ViewContext::PlaylistView` -> `BindingScope::PlaylistView`
- `ViewContext::PlaylistOrganiser` -> `BindingScope::PlaylistOrganiser`
- anything else -> `BindingScope::Global`

That last rule is important: V1 does not expose `Other` as a configurable scope, so non-covered views naturally use global bindings only.

## Data Model Changes

### `BindingDefinition`

Extend `BindingDefinition` with `BindingScope scope{BindingScope::Global};`.

Current uniqueness is effectively `(mode, keys)`.
It should become `(scope, mode, keys)`.

All helpers in `src/vimmotionsbindingbackend.cpp` need to follow that key:

- `findDefinition()`
- add/update/remove/reset/unmap operations
- sorting

### `BindingRow`

Extend `BindingRow` with `BindingScope scope` so the settings tree can display it.

Add user-facing scope text alongside existing mode/source/status text.

### Effective bindings

Current shape:

```cpp
QHash<BindingMode, QList<BindingEntry>>
```

Suggested V1 shape:

```cpp
QHash<BindingMode, QHash<BindingScope, QList<BindingEntry>>>
```

This keeps dispatch simple and avoids repeatedly filtering a flat list on every keypress.

## Backend Loading and Saving

### Parsing persisted keys

`bindingDefinitions()` and default-binding loading should support exactly one path shape:

- 5 segments: `VimMotions/Bindings/{Scope}/{Mode}/{Keys}`

Parsing rules should reject unknown scope names cleanly and skip invalid rows with a log warning.

### Default bindings

Convert existing defaults to the explicit scoped format in V1.

Keep their effective behavior global unless there is a deliberate reason to move a specific default into a narrower scoped default.

Why:

- keeps behavior simple and internally consistent
- avoids carrying two persistence formats through backend, UI, docs, and tests
- still preserves current runtime behavior where defaults remain global

Possible future refinement:

- move organiser-only defaults like `a` / `A` into `PlaylistOrganiser`
- move track-only defaults into `PlaylistView`

That should not be part of the initial binding-scope feature unless explicitly chosen, because it changes shipped behavior.

### Saving definitions

`saveBindingDefinitions()` should:

- remove existing binding keys from the config as it already does
- write all bindings as `Bindings/{Scope}/{Mode}/{Keys}`

This gives one canonical on-disk representation.

## Dispatch Changes

### Apply backend bindings

`VimHandler::applyBackendBindings()` currently converts backend bindings into `m_configBindings` keyed only by mode.

Update it to store per-scope bindings.

Suggested member shape:

```cpp
QHash<Mode, QHash<BindingScope, QList<BindingEntry>>> m_configBindings;
```

### Dispatch lookup

Both `dispatchFromConfig()` and `wouldHandleFromConfig()` must become scope-aware.

At dispatch time:

1. Compute `BindingScope activeScope`
2. Build the candidate binding sets in priority order:
   - active scope
   - global
3. Resolve pending sequence matching, full matches, and prefix detection using that priority

Important sequence rule:

- If scoped `gg` exists and global `g` exists, typing `g` inside that scope must keep the scoped sequence alive instead of prematurely executing the global fallback.

The simplest safe approach is:

- evaluate the active scope binding list first for exact/prefix matches
- only consult global bindings if the active scope has neither a full match nor a viable prefix for the current state

This applies to:

- initial key matching
- pending multi-key continuation
- `ShortcutOverride` claiming through `wouldHandleFromConfig()`

### Pending-state ownership

Pending sequence state is currently global to the handler.

V1 should also record which scope started the pending sequence, so continuation stays consistent if focus shifts unexpectedly mid-sequence.

Suggested addition:

```cpp
std::optional<BindingScope> m_pendingConfigScope;
```

Behavior:

- set when a scoped or global sequence begins
- use the recorded scope while the sequence is pending
- clear with the rest of pending input state

This avoids prefix ambiguity across scope changes.

## Settings UI Changes

### Tree view

Add a `Scope` column to the bindings tree.

Suggested column order:

- `Scope`
- `Mode`
- `Keys`
- `Action`
- `Source`
- `Status`

Stable metadata roles should include scope, similar to current mode/key roles.

### Add/Edit dialog

Add a scope selector to `BindingEditDialog`.

Choices in V1:

- `Global`
- `Playlist View`
- `Playlist Organiser`

Backend validation for duplicate detection should use `(scope, mode, keys)`.

### Button behaviors

Existing operations still make sense, but they act on scoped identity:

- add custom binding
- edit custom binding
- remove custom binding
- unmap default binding
- reset binding

Reset/unmap rules should be unchanged conceptually, only scope-aware.

## README Updates

When implemented, `README.md` should be updated because this changes binding behavior and config structure.

Document:

- the new scoped config format
- the built-in scope names
- precedence rules: exact scope before global
- the fact that `Global` is now explicit in config
- examples showing one key doing different things in playlist view vs organiser

If any default binding is later moved from global to a scope-specific default, also update the explicit default configuration section accordingly.

## Implementation Stages

### Stage 1: Binding-scope types and persistence parsing

Deliverables:

- add `BindingScope` enum and string conversion helpers
- extend `BindingDefinition` and `BindingRow` with scope
- support reading the explicit scoped key format only
- support writing the explicit scoped key format only
- update sorting, lookup, and duplicate detection to use `(scope, mode, keys)`

Acceptance criteria:

- bindings load only from the explicit scoped format
- global and non-global rows load distinctly by scope
- saving round-trips definitions through the single canonical format correctly

### Stage 2: Effective binding map and dispatch

Deliverables:

- make backend `effectiveBindings()` scope-aware
- update `VimHandler::applyBackendBindings()` storage shape
- add runtime scope resolution helper(s)
- update `dispatchFromConfig()` and `wouldHandleFromConfig()` to respect scope precedence
- track pending-sequence scope during multi-key dispatch

Acceptance criteria:

- a key can map differently in playlist view vs organiser
- a scoped binding does not leak into other views
- global bindings still work everywhere not covered by scoped overrides
- multi-key sequences behave correctly with scoped/global overlap

### Stage 3: Settings UI support

Deliverables:

- add scope column to the effective bindings tree
- add scope selector to add/edit dialog
- update tree selection metadata roles to include scope
- update all binding edit operations to use scope-aware identity

Acceptance criteria:

- users can inspect scope in the tree
- users can create and edit scoped bindings from the dialog
- duplicate validation works per scope

### Stage 4: Documentation and polish

Deliverables:

- update `README.md` with scoped config examples and precedence rules
- review default bindings for whether any should be moved to scoped defaults in a separate follow-up
- add targeted log messages for skipped invalid scope names and scope-based dispatch decisions

Acceptance criteria:

- docs match shipped behavior
- troubleshooting scoped bindings is practical from logs and UI

## Testing Plan

Per repo guidance, tests are part of the feature plan.

### Unit tests: `tests/bindingparsertest.cpp`

Extend parser/backend-adjacent coverage for:

- scope-name parsing helper behavior if implemented near parser/backend utilities
- round-trip handling of explicit scoped binding-path helpers
- rejection of unknown scope names

Note: key-sequence parsing itself likely remains unchanged, but scope-path parsing needs focused tests somewhere in unit coverage.

### Integration tests: `tests/bindingintegrationtest.cpp`

Add coverage for:

- loading explicit `Global` bindings alongside narrower scoped bindings
- saving scoped custom bindings under `Bindings/{Scope}/{Mode}/{Keys}`
- saving global bindings under `Bindings/Global/{Mode}/{Keys}`
- duplicate handling for same `(mode, keys)` across different scopes being allowed
- duplicate handling for same `(scope, mode, keys)` being rejected
- settings dialog tree showing the correct scope text/column
- add/edit/remove/unmap/reset operations honoring scope

### Integration/runtime tests: `tests/bindingintegrationtest.cpp` or `tests/vimhandlerviewcontexttest.cpp`

Add behavioral tests for dispatch:

- `j` in playlist view triggers playlist-scoped action
- `j` in playlist organiser triggers organiser-scoped action
- a global binding remains active when no scoped binding exists
- a scoped binding does not trigger in the wrong view
- a scoped sequence beats a global fallback when both share a prefix
- pending sequence completion remains stable even if focus changes during the timeout window

`tests/vimhandlerviewcontexttest.cpp` is already the natural home for focused view-context-driven behavior, so it is the best place for runtime scope dispatch tests.

## Risks and Watchpoints

- Sequence precedence is the trickiest part; prefix handling must not regress global multi-key bindings.
- `ViewContext::Other` should not become a persisted scope accidentally in V1.
- If organiser-only defaults are moved into scoped defaults later, that is a behavior change and should be reviewed separately from the core feature.

## Recommended First Implementation Boundary

For the first PR, stop after Stage 2 plus tests if needed.

Why:

- it proves the feature works end-to-end in config
- it keeps the runtime semantics reviewable
- it isolates the tricky dispatch work from the UI edits

The settings UI can then land as a follow-up if you want a smaller review surface.
