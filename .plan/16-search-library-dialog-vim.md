# Search Library Vim Support

## Goal

Add vim-aware behavior for the `Search Library` modal dialog so that:

- dialog-specific bindings can exist and do not leak elsewhere
- focus can move between the search field and results using vim bindings
- actions like `copyAfterCurrentPlaying` and `moveAfterCurrentPlaying` operate on the dialog selection, not the background playlist view

## Decisions

- Scope target: `Search Library` only
- `copyAfterCurrentPlaying` destination: current playing playlist
- `moveAfterCurrentPlaying` in `Search Library`: unsupported no-op with a log warning
- Search-field capture: allow more dialog-scoped bindings while the line edit is focused, not only `Ctrl+J` / `Ctrl+K`

## Current Findings

- `SearchDialog` is a `QDialog` with a `QLineEdit` and a detached `PlaylistWidget` in a plain `QVBoxLayout`, not a splitter.
- The detached results view is still a `Fooyin::PlaylistView`, so the plugin currently treats it as ordinary `PlaylistView`.
- Binding scopes currently only support `Global`, `PlaylistView`, and `PlaylistOrganiser`.
- `shouldSkipBindings()` suppresses vim handling when focus is inside editable inputs, which blocks dialog bindings from the search field.
- `copyAfterCurrentPlaying` / `moveAfterCurrentPlaying` currently resolve source rows through playlist-view assumptions, so detached search results can map to the wrong underlying playlist.

## Implementation Steps

### 1. Add dialog-specific binding scope

- Extend `BindingScope` with `SearchLibraryDialog`.
- Update:
  - scope parsing/string conversion
  - scope ordering
  - settings dialog scope dropdown
  - README scope documentation
  - tests that enumerate supported scopes
- Keep fallback behavior: dialog scope overrides global scope, global still applies when dialog scope does not define a binding.

### 2. Teach view/context detection about Search Library

- Add a way for `VimHandler` to detect when the active widget/view belongs to the `Search Library` dialog.
- Prefer ancestry/window inspection over class-name-only detection.
- Keep the existing `PlaylistView` behavior for normal playlist widgets unchanged.
- Introduce a dialog-aware active binding scope path so bindings can resolve to `SearchLibraryDialog` even though the inner results widget is still a `PlaylistView`.

### 3. Allow dialog-scoped bindings from the search field

- Refine editable-input suppression so `Search Library` can still receive its scoped bindings while focus is in the dialog `QLineEdit`.
- Do not globally weaken text-field protections elsewhere.
- Boundaries:
  - regular text entry must keep working
  - only bindings that match the active dialog scope should be considered
  - unmatched keys must continue to fall through to normal text editing

### 4. Add explicit focus transfer between field and results

- Because the dialog uses a vertical layout rather than a `QSplitter`, current spatial navigation will not naturally move between the search field and results table.
- Add a `Search Library` special-case focus path:
  - when focus is on the line edit, `Ctrl+J` should focus the results view
  - when focus is on the results view, `Ctrl+K` should focus the line edit
- Decide whether to implement this by:
  - extending `moveSpatialFocus()` with a dialog-specific pre-check, or
  - adding a dedicated dialog-aware focus action
- Prefer the smallest change that preserves existing splitter-based navigation elsewhere.

### 5. Fix source-selection semantics for copy/move-after-current-playing

- Separate "active binding scope/view" from "track source being operated on".
- For `Search Library`, derive the selected tracks from the detached search model rather than from `targetPlaylist()` plus row numbers.
- Destination remains the current playing playlist.
- Likely direction:
  - introduce a helper that extracts selected `PlaylistTrack` / `Track` objects from the active view's model indexes
  - let `copyAfterCurrentPlaying` / `moveAfterCurrentPlaying` use that helper when in `SearchLibraryDialog`
  - preserve current row-range playlist logic for ordinary playlist views

### 6. Implement move semantics for library search

- `copyAfterCurrentPlaying` is straightforward from detached library results.
- `moveAfterCurrentPlaying` should be treated as unsupported in `Search Library` because library results are detached tracks, not rows owned by an editable source playlist.
- Implementation:
  - detect `SearchLibraryDialog` before normal playlist move logic
  - log a clear warning/debug message
  - leave selection and destination playlist unchanged

### 7. Tests

- Binding parser/integration tests:
  - parse/save/read `SearchLibraryDialog` scope
  - settings dialog exposes the new scope
- `vimhandlerviewcontexttest.cpp`:
  - active scope resolves to `SearchLibraryDialog` for views inside the Search Library dialog
  - scoped bindings override global bindings there
  - dialog-scoped bindings can fire from the search field without breaking text entry fallback
  - `Ctrl+J` and `Ctrl+K` move focus between field and results
- Add action-routing tests for:
  - `copyAfterCurrentPlaying` using selected search-library result tracks, not background playlist rows
  - current global playlist behavior remains unchanged
- If feasible, add coverage for `moveAfterCurrentPlaying` once semantics are finalized.

### 8. Documentation

- Update `README.md` for:
  - new `SearchLibraryDialog` binding scope
  - any default bindings added for that scope
  - any action limitations in Search Library
- If new default scoped bindings are introduced in `VimMotionsSettings::defaultBindings()`, also add them to the explicit default configuration section.

### 9. Verification

- Format touched files with `clang-format`.
- Build and run:
  - `cmake -B build-agent -G Ninja`
  - `cmake --build build-agent`
  - `cmake -B build-test-agent -DBUILD_TESTING=ON -G Ninja`
  - `cmake --build build-test-agent`
  - `QT_QPA_PLATFORM=offscreen ctest --test-dir build-test-agent -V`

## Checklist

- [x] Investigated search dialog widget structure
- [x] Investigated binding scope architecture
- [x] Investigated current copy/move-after-current-playing routing
- [x] Collected product decisions for scope target, destination playlist, and search-field capture
- [x] Add `SearchLibraryDialog` to binding scope model
- [x] Detect Search Library dialog in `VimHandler`
- [ ] Route active binding scope to `SearchLibraryDialog`
- [ ] Allow dialog-scoped bindings from the search field
- [ ] Implement field/results focus transfer
- [ ] Fix `copyAfterCurrentPlaying` for detached library results
- [ ] Implement unsupported `moveAfterCurrentPlaying` behavior for detached library results
- [ ] Add parser/integration tests
- [ ] Add view-context/action-routing tests
- [ ] Update README
- [ ] Run formatting
- [ ] Build plugin
- [ ] Build tests
- [ ] Run test suite

## Risks

- Relaxing editable-input suppression too broadly could break normal typing in other dialogs.
- Search result actions need to avoid assuming "row N in active view" means "row N in active playlist".
