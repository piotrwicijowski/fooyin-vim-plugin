# Per-playlist cursor position

## Goal

Save and restore vim cursor state per playlist when switching between the main playlist tabs.

Confirmed scope:

- session-only state; no persistence across restarts
- main playlist tabs only
- clamp restored rows to the last available row if the playlist shrank
- restore full visual state when returning to a playlist

## Current state

The plugin currently owns cursor movement and visual selection state inside `src/vimhandler.cpp`, but it does not remember any playlist-local navigation state.

Relevant code paths:

- `VimHandler` stores visual-mode state in `m_visualAnchor`, `m_visualCursor`, and `m_mode`
- `ViewLocator` resolves the currently active `QAbstractItemView`
- playlist edit/search/reset flows already rely on delayed index restoration via `scheduleIndexRestore()`

Relevant fooyin behavior:

- main playlist switching is driven by `PlaylistController::currentPlaylistChanged(...)`
- `PlaylistHandler::activePlaylistChanged(...)` is not sufficient because it tracks the playing playlist, not the selected playlist tab
- fooyin already preserves per-playlist scroll/top-of-view state, but not the current row

## Expected implementation

### 1. Add playlist-local vim state storage

Add a small in-memory cache in `VimHandler`, keyed by playlist ID.

Suggested stored fields:

- current row
- current column
- mode at time of switch
- visual anchor row
- visual cursor row

Notes:

- session-only means this stays in memory only
- this should be independent from undo/redo state and search state
- visual state should only be considered valid when the saved mode is `Visual`

### 2. Identify the current playlist widget/controller from the active view

Add a helper in `VimHandler` to walk from the active `QAbstractItemView` to the enclosing `Fooyin::PlaylistWidget`.

Use that to access:

- the `PlaylistController`
- the current playlist ID before and after tab switches

This should be limited to playlist-backed views that are part of the main playlist widget flow.

### 3. Subscribe to current-playlist changes

Wire `VimHandler` to `PlaylistController::currentPlaylistChanged(Playlist* prevPlaylist, Playlist* playlist)` for the active main playlist widget.

Implementation requirements:

- avoid duplicate signal connections as focus moves around
- tolerate the active view changing before the plugin has seen a playlist widget
- disconnect or replace the tracked controller connection when the active playlist widget changes

### 4. Save state before switching away

On `currentPlaylistChanged(...)`, save state for `prevPlaylist`.

Save behavior:

- if there is a valid current index, store its row and column
- if there is no valid current index, store row `0` and current column `0`
- if in visual mode, also store `m_visualAnchor` and `m_visualCursor`
- if not in visual mode, clear any saved visual selection payload for that playlist

### 5. Restore state after switching to the new playlist

When switching to the new playlist:

- look up saved state for the incoming playlist ID
- if none exists, leave current behavior unchanged
- if state exists, restore after the view/model is ready

Restore behavior:

- clamp saved row to `0..lastRow`
- clamp visual anchor/cursor independently to `0..lastRow`
- use `scheduleIndexRestore()` or a sibling helper so restore survives async playlist model population
- restore the current index first
- if saved mode was `Visual`, then restore `m_visualAnchor`, `m_visualCursor`, set mode to `Visual`, and call `updateVisualSelection()`
- if the playlist is empty, restore to normal mode and clear visual state

### 6. Preserve existing mode transitions safely

Review mode-changing methods for interactions with restored state:

- `enterVisual()` currently seeds anchor/cursor from the current row; restored visual state must not be overwritten by a fresh seed
- `enterNormal()` clears visual state; make sure delayed playlist restore does not race with an explicit normal-mode exit
- search and paste flows that already manipulate current index should continue to work after a playlist switch

This likely means adding a dedicated restore path rather than trying to reuse `enterVisual()` directly.

### 7. Update documentation

Update `README.md` to document the behavior change:

- switching back to a playlist restores the previous vim cursor position
- if the playlist was left in visual mode, the previous visual selection is restored when possible

## Edge cases to handle

- switching to an empty playlist
- switching back to a playlist that now has fewer rows than the saved cursor/selection
- switching while in visual mode, then returning after tracks were deleted or reordered
- switching focus away from the main playlist widget and back again without losing the cached state
- multiple playlist widgets existing at once: only the main playlist tab flow is in scope for this feature

## Testing plan

Tests are part of the deliverable.

### Unit tests: `tests/bindingparsertest.cpp`

This feature does not change binding syntax, but the plan should still add a small regression test in `tests/bindingparsertest.cpp` to confirm there are no binding-level changes required for playlist switching behavior.

Candidate coverage:

- existing navigation/visual bindings used by this feature (`j`, `k`, `v`, `Esc`) still parse exactly as before
- no new configurable binding is introduced accidentally as part of the feature

If this feels too indirect once implementation starts, that is a signal the repo may need a more suitable pure-unit test target for `VimHandler` state helpers.

### Integration tests: `tests/bindingintegrationtest.cpp`

Extend `tests/bindingintegrationtest.cpp` with a focused integration-style regression around the feature, potentially by expanding the existing test harness or adding a lightweight Qt object harness inside the file.

Target coverage:

- save row N for playlist A, switch to playlist B, switch back to playlist A, restore row N
- restore clamps to last row when the playlist shrinks
- save visual mode state for playlist A, switch away, switch back, restore anchor/cursor selection and visual mode
- empty playlist restore falls back to normal mode without invalid selection state

If the existing `bindingintegrationtest.cpp` is too config-focused for this, extend that test file with the minimal Qt-side harness needed to exercise the new `VimHandler` playlist-state logic.

## Verification

Once code-complete:

```bash
cmake --build build
cmake -B build-test -DBUILD_TESTING=ON -G Ninja
cmake --build build-test
QT_QPA_PLATFORM=offscreen ctest --test-dir build-test -V
```

## Risk assessment

Low-to-moderate implementation risk.

Main risks:

- connecting to the correct playlist-switch signal from plugin code without broadening scope unintentionally
- restoring visual state after async model reloads without fighting fooyin's own selection/reset behavior
- adding meaningful automated coverage with the current test layout

## Suggested implementation order

1. Add playlist-widget/controller discovery helpers in `VimHandler`
2. Add the in-memory per-playlist state map and save/restore data structures
3. Subscribe to `currentPlaylistChanged(...)` and save outgoing state
4. Restore incoming cursor state with clamping
5. Layer visual-state restoration on top of cursor restore
6. Add tests in `tests/bindingparsertest.cpp` and `tests/bindingintegrationtest.cpp`
7. Update `README.md`
8. Build and run the full verification commands
