# Fuzzy Playlist Picker

## Goal

Add a Telescope-style playlist-name picker to the vim plugin using fooyin's existing public APIs. A user can open the picker, type a fuzzy query, navigate the highlighted results without leaving the input field, and accept a playlist to display it.

**Required interaction: `Ctrl+n` selects the next result and `Ctrl+p` selects the previous result while keyboard focus remains in the query input. The user can immediately continue typing.**

## User-facing decisions

### Opening

- Add the no-argument action `findPlaylist`, available in Normal and Visual modes.
- Default binding: `<Space>p` in `Global`, for both Normal and Visual. Existing scoped/custom overrides and unmaps retain their usual precedence.
- Open one compact, window-modal dialog with a query field above a scrollable result list. Use non-blocking `QDialog::open()` rather than a nested `exec()` loop.
- Focus the query field immediately. Each new invocation starts with an empty query.
- An empty query lists all playlists in playlist-index order and initially highlights the currently displayed playlist, or the first playlist if none is selected.
- Include playlists inside collapsed organiser groups, and ordinary, locked, and automatic playlists returned by the public playlist list. That API excludes temporary playlists; organiser groups themselves are not candidates.
- Numeric counts do not change the action; clear pending sequence/count state when opening.

### Interaction inside the picker

| Key | Behaviour |
|---|---|
| Text, paste, Backspace, Delete, text-editing shortcuts | Edit the query normally and update fuzzy results live |
| `Ctrl+n` | Highlight the next result, scroll it into view, keep query input focused |
| `Ctrl+p` | Highlight the previous result, scroll it into view, keep query input focused |
| Down / Up | Same next / previous behaviour, keeping query input focused |
| `Ctrl+j` / `Ctrl+k` | Next / previous aliases, keeping query input focused |
| Return / keypad Enter | Accept the highlighted playlist |
| Escape / window close | Cancel and restore the invoking widget's focus |

- Navigation wraps from the last result to the first and vice versa. It is a no-op for zero results; one result stays selected.
- Navigation changes the picker highlight only. Typing and navigating never switch the displayed playlist or start playback.
- A changed query highlights the highest-ranked match. Clearing the query restores the empty-query ordering and initial-selection rule.
- Display playlist names and track counts, plus a marker for the currently displayed playlist. Use a secondary stable identifier if duplicate names need disambiguation.
- Show distinct `No playlists` and `No matching playlists` empty states. Enter with no valid result leaves the picker open.
- Keep the query input as the keyboard-focus target; result selection is programmatic and visually apparent even though the list is unfocused. A mouse click may change the highlighted row but returns focus to the input.
- Internal picker keys are dialog-local controls. The opening binding is configurable through the existing binding system; a new binding scope or vim mode is not needed for this feature.

### Accepting and cancelling

- Accept switches the UI-selected playlist through `CurrentPlaylistController::changeCurrentPlaylist(id)`, without changing the active playback playlist.
- After a valid accept, close the picker, focus the main playlist track view when available, restore its saved cursor position, and finish in Normal mode with a single cursor selection. Empty destination playlists remain valid destinations.
- A successful switch records one departure in jump history when a valid source track position exists, so `Ctrl+o` returns to the track left behind and `Ctrl+i` goes forward. Same-playlist accepts, cancellations, invalid accepts, and switches from an empty source add no jump.
- Cancel leaves the selected playlist, track cursor, Visual selection, and mode as they were before opening, unless external application changes have invalidated that state. Opening the picker must not itself collapse a Visual selection.
- A same-playlist accept still closes the picker and focuses the track view in Normal mode.
- Store widget references with `QPointer`; handle an invoking widget or destination view being destroyed gracefully.

## Existing API and architecture

- `VimMotionsPlugin::initialise()` already supplies `PlaylistHandler` and `CurrentPlaylistController` to `VimHandler`.
- Public headers provide all playlist access needed:
  - `<core/playlist/playlisthandler.h>`: `playlists()`, `playlistById()`, playlist change signals.
  - `<core/playlist/playlist.h>`: `id()`, `name()`, `index()`, `trackCount()`.
  - `<gui/playlist/currentplaylistcontroller.h>`: current UI selection and `changeCurrentPlaylist()`.
- Reuse the playlist cursor restoration and jump-history mechanisms in `vimhandler.cpp`; `changePlaylistByOffset()` is the existing example of recording a departure and switching UI playlists.
- `VimHandler` installs an application-wide event filter. Its event filter, focus-change callback, and `ViewLocator::activeView()` can interact with dialogs; the latter can actively focus a playlist as a fallback.
- Production plugin code must compile using installed public fooyin headers alone. Keep all new production includes within that boundary.

## Implementation steps

### 1. Implement a deterministic fuzzy matcher

- Add a small UI-independent helper in `src/playlistfuzzymatcher.h/.cpp`.
- Match the trimmed query as a case-insensitive ordered subsequence of the playlist name. For example, `jzz` matches `Jazz`, and `rckcl` matches `Rock Classics`.
- Rank exact-name matches first, followed by prefix matches, contiguous substring matches, and other subsequence matches.
- Within subsequence matches, favour consecutive characters, word starts, earlier matches, and fewer gaps. Find the best-scoring alignment rather than depending on a greedy first alignment.
- Break equal scores by playlist index, then stable ID, so result order is deterministic and does not flicker.
- Use Qt Unicode-aware string/case handling; preserve original display text. Use a documented, bounded algorithm with no regular-expression interpretation of query punctuation.
- Keep matching synchronous on the GUI thread for playlist-sized collections and precompute reusable candidate data when refreshing the list.

### 2. Build the picker dialog

- Add `src/vimplaylistpickerdialog.h/.cpp`, containing the query `QLineEdit`, result list/model, empty-state label, and a compact key-help footer.
- Keep candidate data as copied values (stable playlist ID, name, index, track count), not retained `Playlist*` pointers.
- Expose the selected playlist ID on valid acceptance; keep actual playlist switching and history management in `VimHandler`.
- Give dialog/input/list stable object names for focused widget tests.
- Route navigation keys from the input to a shared next/previous-selection helper. Update the model selection and call `scrollTo()` without calling `setFocus()` on the list.
- Handle both `QEvent::ShortcutOverride` and `KeyPress` for dialog-owned keys so application shortcuts cannot steal `Ctrl+n`, `Ctrl+p`, their aliases, Enter, or Escape. Claim the shortcut on override and perform the action once on key press; key autorepeat should navigate normally.
- Leave ordinary input editing, clipboard shortcuts, and input-method handling with `QLineEdit`.
- Refresh candidates on public playlist add/remove/rename/index-change signals and population completion. Refresh visible counts on relevant public track-change signals. Track UI-current-playlist changes for the current marker.
- When external updates rebuild results, preserve the highlighted ID if still matched; otherwise highlight the top result. Reapply the active query without clearing input or moving keyboard focus.
- Resolve the selected ID against the current handler immediately before acceptance. If it was removed, refresh and keep the picker open rather than accepting a stale destination.

### 3. Integrate action, focus, and lifecycle handling

- Add `VimHandler::findPlaylist()` and register `findPlaylist` in `vimactions.cpp`.
- Hold a guarded reference to the active dialog and the invoking widget/main playlist view. A repeated invocation raises the existing picker and focuses its input rather than creating another instance.
- Capture the source playlist and save its cursor/mode before opening. Stop pending input/timeouts so no delayed binding executes behind the picker.
- Add a picker-specific exclusion to the application event filter before picker events can trigger generic playlist tracking or binding dispatch. Apply the same ownership guard to focus-change handling where needed.
- Ensure picker descendants are excluded from ordinary view discovery and that opening, filtering, and result navigation do not invoke `activeView()` fallbacks that steal input focus.
- Suspend background vim command handling while the picker owns input, including when `UseVimMotionsInSettings=true`. This is an explicit dialog ownership rule, independent of text-input suppression.
- On acceptance, revalidate the destination, resolve and record the live source track position once, and switch through the public current-playlist controller. Validate that the source playlist is still the one captured on opening; external playlist changes must not create a jump from an unrelated stale source. Resolving the live cursor also accounts for playback-following moving it while the picker is open.
- Reuse existing asynchronous cursor/model restoration hooks. Explicitly make the accepted destination Normal with a single cursor selection, including when that playlist has a remembered Visual selection; do not race a competing restore callback or overwrite other playlists' saved state.
- Restore focus only after closing the modal dialog. Use the captured main playlist view where possible, with a safe visible main-view fallback; never target detached Search Library results.
- On cancellation, remove picker-specific state and restore focus without introducing a mode transition or jump. If external changes occurred, retain the application's live state rather than restoring a stale snapshot over it.
- Clean up the dialog and signal connections on close, handler destruction, and plugin shutdown. Missing playlist services make the action a logged no-op.

### 4. Wire bindings, settings, and build targets

- Add `Bindings/Global/Normal/<Space>p=findPlaylist` and `Bindings/Global/Visual/<Space>p=findPlaylist` in `VimMotionsSettings::defaultBindings()`.
- Add `findPlaylist` to the action help in `vimmotionssettingsdialog.cpp`.
- Add matcher/dialog sources to `PLUGIN_SOURCES` in the root `CMakeLists.txt`.
- Update `tests/CMakeLists.txt` for the new matcher/dialog tests and every existing target that compiles `vimhandler.cpp`, including the view-context and timeout targets.

### 5. Deliver tests

#### Binding unit tests: `tests/bindingparsertest.cpp`

- Parse `<Space>p=findPlaylist` as a two-key, no-argument binding.
- Parse an alternative custom opening shortcut for the same action.

#### Binding integration tests: `tests/bindingintegrationtest.cpp`

- Verify Global Normal and Visual defaults resolve to `findPlaylist`.
- Verify overrides, unmapping, and `UseDefaultBindings=false` behave correctly.
- Verify custom binding save/reload and settings-dialog editing preserve the action and opening sequence.

#### Matcher tests: new `tests/playlistfuzzymatchertest.cpp`

- Cover empty query, case-insensitive matching, ordered subsequences, rejection of out-of-order characters, repeated letters, literal punctuation, and non-ASCII names.
- Assert meaningful ranking relationships: exact over prefix over substring over gapped matches, and compact/boundary alignments over poorer alignments.
- Cover names with multiple possible alignments and deterministic tie ordering.

#### Dialog tests: new `tests/vimplaylistpickerdialogtest.cpp`

- Empty-query list, initial current-playlist highlight, live filtering, top-match selection, and clearing the query.
- **For both `Ctrl+n` and `Ctrl+p`: send the key to the input, assert the highlighted ID changes, assert `QApplication::focusWidget()` is still the input, then type more text and assert it updates the query/results.**
- Exercise repeated navigation, both wrap boundaries, single/zero-result cases, scrolling, and Down/Up plus `Ctrl+j/k` aliases.
- Verify input text, caret, and text selection are unchanged by result navigation.
- Verify unfocused-list selection remains visibly highlighted using the appropriate palette/style state.
- Add conflicting application `QAction`/`QShortcut` shortcuts for `Ctrl+n` and `Ctrl+p`; assert only picker navigation runs and each press advances once.
- Verify regular typing, paste, text-editing keys, both Enter keys, Escape, and window-close cancellation.
- Verify result highlighting never switches the actual selected playlist, and no-match Enter leaves the dialog open.
- Verify rename/add/remove/reorder refreshes preserve query/focus and never accept a deleted playlist ID.

#### Handler integration tests: `tests/vimhandlerviewcontexttest.cpp`

- Use existing fake current-playlist controller and playlist fixtures to exercise opening through real binding dispatch from playlist and organiser panes in both Normal and Visual modes.
- Install the application event filter in these tests. Assert `Ctrl+n` / `Ctrl+p` and subsequent typing retain input focus with the full handler active; dialog-only tests are insufficient to catch view-locator focus stealing.
- Opening and navigation do not mutate background cursors, selected playlist, playback, history, or a Visual selection. Cancelling restores the invoking focus/mode/selection.
- Accept changes only the UI-selected playlist, focuses a main track view, restores its cursor, and finishes Normal, including a remembered Visual destination and an empty playlist.
- Verify playlists in collapsed organiser groups and operation without an organiser widget.
- Verify one accepted switch from a valid source track produces one jump-history departure, and `Ctrl+o` / `Ctrl+i` traverse back/forward. Cancel, no match, same-playlist accept, and an empty source add none.
- Exercise asynchronous model population, repeated invocation, destroyed invoking/destination widgets, missing services, external source changes, and dialog cleanup during shutdown.
- Verify owned shortcuts stay local, generic vim actions cannot reach the background while open, and normal binding dispatch resumes after close.

### 6. Update documentation

- Add a playlist-picker feature section to `README.md` with fuzzy matching, accept/cancel semantics, and a short workflow: `<Space>p`, type, `Ctrl+n` / `Ctrl+p`, Enter.
- State explicitly that `Ctrl+n` / `Ctrl+p` move the highlighted result while the input keeps focus and typing can continue immediately.
- Add `findPlaylist` to Available actions and the Normal/Visual binding tables.
- Add both new default entries to the **Explicit default configuration** INI section, as required by `AGENTS.md`.
- Extend jump-history documentation to include successful picker switches.

### 7. Verification

- Format all touched C++ files using the repository's `.clang-format`.
- Run from the plugin repository, using the agent build directories:

```bash
cmake -B build-agent -G Ninja
cmake --build build-agent
cmake -B build-test-agent -DBUILD_TESTING=ON -G Ninja
cmake --build build-test-agent
QT_QPA_PLATFORM=offscreen ctest --test-dir build-test-agent -V
```

- Confirm new production includes use public installed headers; where an installed fooyin development package is available, also configure/build with `FOOYIN_FORCE_DEV_TREE=OFF`.
- Manually check real-window focus, inactive-list highlight visibility, scrolling, and keyboard autorepeat. Try a long playlist list, collapsed groups, and conflicting configured fooyin shortcuts.

## Delivery checklist

- [x] Deterministic fuzzy matching and ranking
- [x] Picker dialog with input-focused `Ctrl+n` / `Ctrl+p` navigation
- [x] Shortcut conflict handling and application event-filter isolation
- [x] Public-API playlist enumeration, refresh, and switching
- [x] Accept/cancel, cursor, mode, focus, and jump-history integration
- [x] `findPlaylist` action, `<Space>p` defaults, and settings help
- [x] Binding parser and binding integration tests
- [x] Matcher, dialog focus/navigation, and handler integration tests
- [x] README feature, action, binding, and explicit-default documentation
- [x] Formatting, plugin build, test build, and full test run
- [ ] Manual desktop UI check (headless container; Qt widget rendering, focus, scrolling, and autorepeat are covered by offscreen tests)

## Verification results

- Plugin and test builds succeeded in `build-agent/` and `build-test-agent/`.
- All 9 CTest targets passed, including the new matcher and picker targets and the expanded binding/handler tests.
- This container lacks the system Qt SQLite driver. The full test run used the existing temporary driver via `QT_PLUGIN_PATH=/tmp/opencode/qt-sqlite/usr/lib/x86_64-linux-gnu/qt6/plugins`, alongside `QT_QPA_PLATFORM=offscreen` and `QT_LOGGING_RULES='fy.vim.debug=false'`.
- New production code uses public fooyin headers only.
