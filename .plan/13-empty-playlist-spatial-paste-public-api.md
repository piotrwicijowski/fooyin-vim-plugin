# Empty Playlist Spatial Paste Public API Plan

## Goal

Fix the case where vim spatial navigation can reach the playlist pane for an empty playlist, but paste still targets the wrong playlist because the plugin cannot reliably identify the currently selected playlist when the view has no rows.

The preferred fix is to add a small public fooyin API for the selected/current playlist, then update the vim plugin to consume that API instead of guessing from row counts or playback state.

## Problem statement

Current plugin logic in `src/vimhandler.cpp` resolves the paste target like this:

1. use the selected track selection when available
2. otherwise match the active `PlaylistView` to a playlist by `rowCount()`
3. otherwise fall back to `PlaylistHandler::activePlaylist()`

This fails for an empty playlist:

- there is no selected track
- `rowCount()` is `0`, so the view cannot be matched heuristically
- `activePlaylist()` refers to the playing playlist, which may still be playlist A

Result: after selecting empty playlist B in the organiser and spatially moving back to the playlist pane, `p` may paste into playlist A instead of B.

## Why this needs a public API

Fooyin already has the right semantic source internally: the selected/current playlist tracked by `PlaylistController` and changed by organiser selection.

But the plugin cannot depend on `PlaylistController` directly because it is not part of the installed public API, and `AGENTS.md` explicitly forbids depending on fooyin private headers.

So the correct fix is not more widget introspection. The correct fix is exposing the selected/current playlist through a supported public interface.

## Existing related plan

`.plan/10-upstream-public-playlist-tab-api.md` already outlines a general public API for selected playlist changes.

This plan narrows that direction to the concrete empty-playlist paste bug and defines the end-to-end work needed across fooyin and this plugin.

## Proposed public API

Recommended shape: a minimal public observer/service for the selected current playlist.

Example shape:

```cpp
namespace Fooyin {

class FYGUI_EXPORT PlaylistSelectionObserver : public QObject
{
    Q_OBJECT

public:
    [[nodiscard]] virtual Playlist* currentPlaylist() const = 0;
    [[nodiscard]] virtual UId currentPlaylistId() const = 0;

Q_SIGNALS:
    void currentPlaylistChanged(Fooyin::Playlist* previous, Fooyin::Playlist* current);
};

} // namespace Fooyin
```

Required semantics:

1. `currentPlaylist()` means the selected playlist in the main playlist UI flow.
2. It is distinct from `PlaylistHandler::activePlaylist()`, which represents playback state.
3. It must remain valid even when the selected playlist has zero tracks.
4. The signal may fire before the playlist view model has finished repopulating.

## Fooyin-side implementation plan

### 1. Add installed public header

Add a public header such as:

- `include/gui/playlist/playlistselectionobserver.h`

Contents:

1. forward declarations only for public types already available to plugins
2. export macro and API comments
3. the observer interface with `currentPlaylist()`, `currentPlaylistId()`, and `currentPlaylistChanged(...)`

### 2. Back it with existing current-playlist state

Implement the service inside fooyin GUI using the existing `PlaylistController` current-playlist signal/state.

Requirements:

1. no behaviour change in fooyin UI
2. no new duplicate source of truth
3. simply forward/re-emit the current-playlist semantics already present internally

### 3. Expose it through plugin-facing context

Register the observer in whatever public GUI/plugin context fooyin already uses for plugin services.

Requirements:

1. available from installed public headers only
2. lifetime owned by fooyin
3. reachable during normal plugin GUI initialisation

### 4. Document selected-playlist vs active-playlist semantics

The API documentation must clearly distinguish:

1. selected/current playlist
2. active/playing playlist

This distinction is the core reason the existing plugin fallback fails.

## Vim plugin implementation plan

Once the public API exists, update the plugin in the smallest viable way.

### 1. Discover the new observer during initialisation

Extend plugin setup so `VimHandler` can access the public `PlaylistSelectionObserver` service.

### 2. Cache the current playlist identity explicitly

Add a lightweight cached pointer or `UId` in `VimHandler` representing the last observed selected/current playlist.

Rules:

1. update it from `currentPlaylist()` on startup
2. keep it in sync via `currentPlaylistChanged(...)`
3. treat it as the authoritative target when the active view context is `PlaylistView`

### 3. Replace the empty-playlist fallback in `targetPlaylist()`

Change `targetPlaylist()` so resolution order becomes:

1. selected playlist from `TrackSelectionController`, when available
2. observed current playlist from the new public API
3. heuristic matching only if still needed as a last fallback
4. `activePlaylist()` only as the final fallback for legacy/no-observer cases

The key change is that an empty `PlaylistView` must still resolve to the selected playlist B.

### 4. Keep paste behaviour unchanged otherwise

`pasteAfter()` and `pasteBefore()` already handle invalid current indexes by inserting at `0` or `1`-derived clamped positions. No behavioural redesign is needed there.

The goal is only to provide the correct playlist target.

### 5. Avoid organiser-private coupling

Do not add plugin code that reads organiser model roles, reaches into private organiser classes, or depends on widget naming/layout details to infer the selected playlist.

## Testing plan

Tests are part of the deliverable.

### Fooyin tests

Add fooyin-side tests for the new public API:

1. `currentPlaylist()` matches `PlaylistController::currentPlaylist()`
2. organiser selection changes emit `currentPlaylistChanged(previous, current)` correctly
3. selecting an empty playlist still updates the observer correctly
4. changing the active/playing playlist without changing the selected playlist does not emit the selected-playlist signal

### Vim plugin unit/config tests

`tests/bindingparsertest.cpp`

This feature should not change binding syntax. Add a small regression-only assertion if needed to keep coverage expectations explicit.

### Vim plugin integration tests

`tests/bindingintegrationtest.cpp` and/or a focused `VimHandler` view-context test harness.

Add coverage for:

1. playlist A has tracks and is the playing playlist
2. playlist B is selected/current but empty
3. active view is the playlist pane for B
4. `p` or `P` targets playlist B, not A

If the current harness cannot model the new public observer cleanly, extend the smallest existing test harness rather than introducing broad new test infrastructure.

## Verification

Once implementation is complete:

```bash
cmake --build build
cmake -B build-test -DBUILD_TESTING=ON -G Ninja
cmake --build build-test
QT_QPA_PLATFORM=offscreen ctest --test-dir build-test -V
```

## Risks

1. exposing too much of fooyin internal playlist controller machinery instead of a minimal semantic observer
2. unclear plugin-service discovery path if fooyin's public plugin context needs a small extension first
3. test harness limitations around mocking the new observer in plugin tests
4. preserving behaviour for startup or delayed playlist-model loading when `currentPlaylistChanged(...)` precedes view readiness

## Suggested implementation order

1. upstream the public observer API in fooyin
2. expose and document selected/current playlist semantics
3. add fooyin tests for empty-playlist selection
4. update the vim plugin to consume the observer
5. simplify `targetPlaylist()` around the new authoritative source
6. add plugin regression tests for paste into an empty selected playlist

## Deliverables

1. fooyin public installed header for selected/current playlist observation
2. fooyin implementation and service registration
3. fooyin tests for observer semantics, including empty playlists
4. vim plugin update to use the new observer in `targetPlaylist()`
5. vim plugin regression tests covering paste into an empty selected playlist
