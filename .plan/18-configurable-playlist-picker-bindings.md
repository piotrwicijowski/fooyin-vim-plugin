# Configurable Playlist Picker Bindings

## Goal

Make the playlist picker's navigation, acceptance, cancellation, and mode transitions configurable through the existing binding backend, settings editor, and `Bindings\{Scope}\{Mode}\{Keys}` configuration format.

This plan supersedes the dialog-local, hardcoded-key decision in `.plan/17-fuzzy-playlist-picker.md`. Implementation and automated verification are complete; the manual desktop check is pending.

## Recommended interaction contract

### Scope and modes

- Add `BindingScope::PlaylistPicker`, serialized as `PlaylistPicker` and labelled **Playlist Picker** in settings.
- Give the picker a local mode using the existing `BindingMode::Insert` and `BindingMode::Normal` values. Each new invocation starts in Insert, independently of the invoking view's Normal or Visual mode.
- Insert: matched picker bindings run; unmatched input continues to the query field for ordinary typing, editing, clipboard operations, and input-method composition.
- Normal: bindings navigate results or change modes; unmatched command keys are consumed instead of editing the query. Keep keyboard focus in the query field, with editing disabled while Normal is active.
- Visual is not a picker mode: the picker accepts one playlist and has no range-selection operation. Limit the settings editor's mode choices to Insert and Normal when this scope is selected. Handwritten Visual entries may round-trip through the backend but are inactive.
- Preserve the invoking view's stored mode and Visual selection throughout local mode transitions. Cancellation returns to its live state; acceptance retains the existing Normal-mode destination behavior.
- Report the effective local mode through `mode()` / `modeChanged` for the existing mode indicator, and show the mode inside the picker. Keep the underlying `m_mode` and playlist-persistence state separate from this presentation of the active input mode.

### Scope inheritance

- Resolve `PlaylistPicker` mappings first, with the ordinary **Global fallback for the same mode**. Use the existing default/custom override, unmap, reset, and `UseDefaultBindings` rules.
- Opening in local Insert keeps ordinary Global Normal mappings from consuming query text. Custom Global Insert mappings apply while typing. Unmapping a picker binding exposes a matching Global binding, consistently with other scopes.
- Action execution remains picker-local: unsupported view actions do not operate on the background playlist, regardless of which scope supplied the mapping.
- Reuse the existing key syntax, matching rules, sequence resolution, and timeout setting. Do not create a second picker-only configuration system.
- The opening action remains `findPlaylist`, with its current Global Normal and Visual defaults. Picker mappings describe interaction after opening, rather than changing where `findPlaylist` can be invoked.

### Actions

| Action | Arguments | Picker behavior |
|---|---|---|
| `playlistPickerMove` | signed integer delta | Move the highlight by the delta, wrapping and scrolling, without leaving the query field |
| `playlistPickerAccept` | none | Request the highlighted playlist through the existing validated acceptance path |
| `playlistPickerCancel` | none | Reject the picker through the existing cancellation path |
| `enterInsert` | none | Change the picker-local mode to Insert |
| `leaveInsertMode` | none | Change the picker-local mode to Normal |
| `clearPending` | none | Clear only pending picker command input and counts |

- Register the new actions in `VimActions`. Outside the picker, picker-only actions are no-ops.
- Route the supported existing mode/state actions locally before they can invoke view-oriented implementations.
- A picker-scoped mapping to an unsupported action is consumed and logged as unsupported, without calling the generic background action handler. Document the supported action set in settings help and the README.
- Normal-mode counts multiply `playlistPickerMove`'s delta; Insert-mode digits remain query text unless explicitly mapped. Accept/cancel/mode changes ignore counts and clear pending state.
- Movement remains a no-op with zero results and keeps the only row selected with one result. Acceptance without a valid highlighted playlist keeps the dialog open.

### Defaults and compatibility

Preserve the immediate-typing workflow and adopt Telescope-style Escape handling: Insert → Normal → close. Navigation and both Return and keypad Enter become ordinary default bindings:

| Mode | Keys | Action |
|---|---|---|
| Insert, Normal | `Ctrl+N`, `<Down>` | `playlistPickerMove:+1` |
| Insert, Normal | `Ctrl+P`, `<Up>` | `playlistPickerMove:-1` |
| Insert, Normal | `<CR>`, `<Enter>` | `playlistPickerAccept` |
| Insert | `<Esc>` | `leaveInsertMode` |
| Normal | `<Esc>` | `playlistPickerCancel` |
| Normal | `j` | `playlistPickerMove:+1` |
| Normal | `k` | `playlistPickerMove:-1` |
| Normal | `i` | `enterInsert` |

Escape enters Normal, `j`/`k` navigate, `i` resumes typing, Enter accepts, and Escape in Normal cancels. Users who prefer one-press cancellation from Insert can override:

```ini
[VimMotions]
Bindings\PlaylistPicker\Insert\<Esc>=playlistPickerCancel
```

Other examples:

```ini
[VimMotions]
Bindings\PlaylistPicker\Insert\Ctrl+F=playlistPickerMove:+1
Bindings\PlaylistPicker\Insert\Ctrl+B=playlistPickerMove:-1
Bindings\PlaylistPicker\Insert\Ctrl+N=
Bindings\PlaylistPicker\Normal\zz=playlistPickerAccept
```

- `UseDefaultBindings=false` disables the new defaults too. Existing explicit-config users must add the picker bindings they want; supply the complete entries in the README's Explicit default configuration section.
- Do not retain hardcoded navigation/accept/cancel fallback bindings. Escape and either Enter key with no effective scoped or Global mapping must not trigger implicit Qt dialog actions.
- Window-manager close remains a cancellation mechanism even if all cancellation shortcuts are unmapped.

## Starting implementation constraints

- `vimplaylistpickerdialog.cpp` hardcodes navigation in `navigationDelta()` and accept/cancel in its input event filter; its help footer also hardcodes the shortcuts.
- `VimHandler::eventFilter()` currently returns immediately whenever `m_playlistPicker` exists. A picker-specific route must replace that return before generic view tracking or editable-input suppression runs.
- `dispatchFromConfig()` and `wouldHandleFromConfig()` special-case Normal/Visual Escape before resolving mappings. Picker Escape must resolve normally while retaining the shared scoped/Global resolver and count/sequence behavior.
- Use that same dispatcher for the picker; remove the unused duplicate `dispatchFromBindings()` path rather than maintaining a separate picker resolver.
- `executeAction()` runs jump-restoration work before invoking the action. Picker dispatch must bypass that work, including for mode changes and timeout fallbacks.
- The pending-sequence timer executes fallback actions. Its ownership must follow the picker lifecycle so no deferred command executes after cancellation or acceptance.
- Keep the existing picker exclusions in `ViewLocator` and the handler's focus-change callback. The results list must not become an ordinary playlist view or trigger fallback focus activation.

## Implementation steps

### 1. Add the scope and defaults to configuration

Update:

- `src/vimmotionsbindingbackend.h`: append the enum value.
- `src/vimmotionsbindingbackend.cpp`: parsing, string conversion, and deterministic scope ordering. Audit every exhaustive scope switch.
- `src/vimmotionssettings.cpp`: add each binding from the defaults table as a separate `VimMotions/Bindings/PlaylistPicker/...` entry.
- `src/vimmotionssettingsdialog.cpp`: scope label/dropdown, relevant mode choices, and action help. Preserve existing custom-row editing, unmap/reset controls, and save/reload behavior.

Keep all definitions in the existing backend, and make them available to the active picker when bindings are reloaded.

### 2. Expose semantic dialog operations

In `src/vimplaylistpickerdialog.h/.cpp`:

- Expose selection movement and local editing-mode presentation; reuse the existing `accept()` and rejection lifecycle.
- Remove `navigationDelta()` and the fixed-key-to-action dispatch. The dialog owns widgets and result state, while the handler owns mapping resolution.
- Preserve selected-ID validation, wrapping, scroll behavior, result highlight styling, fuzzy refresh, and input focus/caret/selection behavior during navigation.
- Suppress Qt's implicit Escape rejection and Return/Enter acceptance when those events are not mapped. Cover propagation from the input to `QDialog`, rather than relying only on the application filter. These keys may be inert when unmapped, but must not invoke the former action implicitly.
- Keep window-close cancellation working. Ensure Tab or mouse interactions cannot leave command input on an unintended widget.
- Remove the shortcut-help footer. Keep the local mode indicator and document configurable mappings in the README and settings editor.

### 3. Add explicit picker input routing

In `src/vimhandler.h/.cpp` and `src/vimactions.cpp`:

- Introduce picker-local mode state and a dedicated route for `ShortcutOverride` and `KeyPress`, selected before ordinary view discovery and background state tracking.
- Verify the event receiver/focused widget belongs to this picker. A nested modal dialog or unrelated widget must not accidentally receive picker commands. Continue suspending background dispatch while the picker exists.
- Resolve active scope as `PlaylistPicker` and dispatch using its local mode through the ordinary scoped-first/Global-fallback matcher. Bypass the main dispatcher's hardcoded Escape behavior and view-dependent action preamble for this context.
- Keep shortcut prediction and actual dispatch consistent, including prefixes, pending-sequence mismatches, counts, and supported-action checks. `ShortcutOverride` claims the key; only `KeyPress` performs the action, once.
- Reuse the existing pending-sequence machinery while the picker is the exclusive input owner. Clear all pending keys, sequences, fallbacks, mark operations, counts, and timers before opening and on every close/mode-change boundary. Invalidate pending input on binding reload as well.
- Associate delayed fallbacks with the originating picker session/mode; ignore stale callbacks after teardown. Use the same `PendingSequenceTimeout` and prefix/fallback semantics as ordinary mappings. Consumed Insert-mode sequence prefixes are command input, not buffered query text; timeout/mismatch does not replay them into the field.
- In Normal, handle numeric counts and consume unmatched command input locally. In Insert, return unmatched editable input to `QLineEdit`. Do not pass a matched or failed pending command through to background dispatch.
- Route picker actions before `executeAction()`'s jump restore/cancel preamble, and before generic mode-transition functions can save/collapse a background Visual selection.
- `mode()` / mode notifications should describe active picker input while open, then the live underlying mode after closing. Do not persist local Insert/Normal into the invoking playlist's cursor state.

### 4. Preserve lifecycle behavior and support reload

- Opening still captures the invoking widget and source playlist, clears pending input, and creates one window-modal, non-blocking picker. Repeated invocation raises the existing dialog without resetting its mode/query.
- Query changes and selection navigation must not switch playlists, affect playback, or add jump-history entries.
- Acceptance still revalidates the ID, performs the existing UI-selected-playlist switch and one departure-history update, restores the destination cursor, and finishes Normal.
- Cancellation restores invoking focus and leaves its live mode/Visual selection intact. Handle external source changes and destroyed widgets through the existing guarded paths.
- Clear picker dispatch state before restoring focus, including normal rejection, successful acceptance, destruction, and handler shutdown.
- On backend reload while open, cancel pending command state and replace effective mappings without clearing query, highlight, caret, or focus.
- Continue using only installed public fooyin headers in production code.

## Test deliverables

### Unit tests: `tests/bindingparsertest.cpp`

- Parse modifier mappings to `playlistPickerMove:+1` and `:-1`, verifying signed args.
- Parse `<CR>`, `<Enter>`, and `<Esc>` picker actions with correct Qt key identities; distinguish Return from keypad Enter and retain keypad-modifier matching.
- Parse a Normal multi-key accept mapping and a mode-transition mapping using the existing syntax.

Scope storage belongs to backend/integration tests: `parseBindingString()` does not parse scope paths.

### Integration tests: `tests/bindingintegrationtest.cpp`

- Load and save `PlaylistPicker` Insert/Normal definitions through the real backend and configuration file.
- Verify every new default, custom override, explicit unmap, reset, removal, and behavior with `UseDefaultBindings=false`.
- Exercise Add/Edit/Unmap/Reset and Apply/reload through the settings dialog with the new scope; check the mode choices and displayed source/status.
- Confirm handwritten Visual entries remain inactive and do not change other scopes' behavior.

### Dialog tests: `tests/vimplaylistpickerdialogtest.cpp`

- Keep direct dialog tests for semantic movement, empty/single/multiple results, wrapping, scrolling, refresh, selected-ID preservation, and inactive-list selection rendering.
- Verify navigation operations preserve query focus, text, caret, and text selection; Insert/Normal editing presentation changes do not clear the query.
- Verify accept requests still require a valid highlighted ID and window close still rejects.
- Verify bare/unmapped Escape, Return, and keypad Enter cannot trigger Qt dialog fallback behavior. Update old fixed-key tests to use configured handler routing where appropriate.

### Full routing tests: `tests/vimhandlerviewcontexttest.cpp`

Install the real application event filter and open through `findPlaylist`:

- Defaults retain Ctrl+n/p, arrows, and both Enter keys, with focus remaining in the input and typing continuing immediately. Ctrl+j/k have no picker defaults; they retain their Global spatial-navigation mappings. Escape enters Normal, then cancels on the second press.
- Remap next/previous/accept/cancel; verify scoped precedence, Global fallback after scoped unmaps, and inert Escape/Enter after both scopes are unmapped.
- Default Insert Escape enters local Normal; `j`/`k` and counts navigate, text is not inserted, and `i` returns to typing. Digits in Insert remain query text.
- Open from Visual, change local modes, navigate, then cancel: origin mode, cursor, selection, and playlist are preserved. Verify effective mode notifications on open, local transitions, accept, and cancel.
- Conflicting application `QAction`/`QShortcut` bindings lose to configured picker mappings; each key press performs exactly one action, including autorepeat.
- Global mappings apply only for the active local mode, including single-key and multi-key fallback. Other view scopes do not apply. Unsupported actions cannot mutate background views or trigger jump restoration.
- Nested modal input is not captured by the picker. Closing restores ordinary dispatch.
- Apply bindings while open: new mappings take effect immediately, pending old sequences are discarded, and query/highlight/focus are preserved.
- Retain existing accepted-switch, history, stale-ID, asynchronous population, external-change, missing-service, repeated-invocation, and widget-destruction coverage.

### Timeout tests: `tests/vimhandlerviewcontexttest.cpp`

Picker timeout tests reuse the existing playlist/controller fixture in the view-context target rather than duplicating its database-backed setup in `vimhandlertimeouttest.cpp`. The existing timeout target continues to cover ordinary non-picker dispatch.

- Exercise picker sequences with completion, mismatch, ambiguous single-key fallback, disabled timeout, and finite timeout.
- Verify shortcut claiming agrees with dispatch during pending sequences so application shortcuts cannot steal a consumed continuation/mismatch.
- Closing, changing mode, reloading bindings, destroying the picker/handler, and reopening cannot execute a stale fallback or carry a count into another context.
- Keep existing non-picker sequence behavior covered when extracting shared matching helpers.

Update `tests/CMakeLists.txt` and root source lists if helpers are extracted. Keep dialog-only targets lightweight; full configuration/dispatch assertions belong in handler tests.

## Documentation and verification

- Update `README.md`: picker scope and mode contract, supported actions, complete defaults, override/unmap examples, double-Escape workflow, and ordinary Global inheritance.
- Add **every** new default entry to the Explicit default configuration INI section, including aliases and separate `<CR>` / `<Enter>` entries. Explain the additional entries needed by existing `UseDefaultBindings=false` users.
- Format touched C++ files with the repository's `.clang-format`.
- Run the repository-required verification from the plugin directory:

```bash
cmake -B build-agent -G Ninja
cmake --build build-agent
cmake -B build-test-agent -DBUILD_TESTING=ON -G Ninja
cmake --build build-test-agent
QT_QPA_PLATFORM=offscreen ctest --test-dir build-test-agent -V
```

- Resolve any Qt SQLite driver requirement in the test environment if necessary; record actual verification results at implementation time.
- Manually check real-window focus, IME/paste, Normal/Insert presentation, conflicting application shortcuts, autorepeat, and window-close cancellation.

## Delivery checklist

- [x] Scope parsing, storage, settings editor, and defaults
- [x] Semantic dialog operations and suppression of implicit keyboard fallback
- [x] Local Insert/Normal routing with shared sequence matching
- [x] Picker action dispatch, effective mode display, and background-state preservation
- [x] Lifecycle/reload/timeout isolation
- [x] Binding parser and binding integration tests
- [x] Dialog, handler-routing, and timeout tests
- [x] README and complete explicit default configuration
- [x] Formatting, plugin build, test build, and full test run
- [ ] Manual desktop interaction check

## Verification results

- Formatted all touched C++ sources and headers using the repository's `.clang-format`.
- Configured and built the plugin in `build-agent/`, and the plugin/test targets in `build-test-agent/`.
- All 9 CTest targets passed with `QT_QPA_PLATFORM=offscreen ctest --test-dir build-test-agent -V`.
- The container's missing Qt SQLite driver was supplied through `QT_PLUGIN_PATH=/tmp/opencode/qt-sqlite/usr/lib/x86_64-linux-gnu/qt6/plugins`; `QT_LOGGING_RULES='fy.vim.debug=false;fy.vim.info=false'` reduced log output.
- Final verification includes the revised ordinary Global fallback and double-Escape defaults, with all 9 targets passing. The picker now uses the same `dispatchFromConfig()` path as other scopes; the unused duplicate dispatcher was removed.
- Handler tests cover configured routing, scoped precedence and Global fallback, local-mode/Visual-selection preservation, inactive Visual definitions, Escape/Enter unmaps, shortcut conflicts, reload, external playlist changes, and sequence timeout/session cleanup.
- `git diff --check` passed. Production changes add no fooyin header dependencies.
- Real desktop/IME interaction has not been checked in this headless environment.
