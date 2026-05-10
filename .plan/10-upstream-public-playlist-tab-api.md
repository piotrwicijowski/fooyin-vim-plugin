# Public API for selected playlist tab changes

## Goal

Expose a small public fooyin API that allows plugins built only against installed headers to observe selected main-playlist tab changes and restore playlist-local UI state safely.

This plan does **not** implement the feature. It describes how to upstream the minimum fooyin-side API needed so the vim plugin can later implement per-playlist cursor restore without depending on private GUI headers or fragile Qt widget introspection.

## Problem statement

The vim plugin needs to know when the **selected** main playlist changes.

Current public/plugin-visible APIs are insufficient:

- `PlaylistHandler::activePlaylistChanged(...)` tracks the playing playlist, not the selected tab
- installed fooyin headers do not expose a supported signal for selected playlist tab changes in the main playlist UI flow
- generic Qt widget introspection is not reliable across fooyin versions and layouts

The most stable source today is `PlaylistController::currentPlaylistChanged(...)`, but the plugin cannot safely rely on that unless fooyin exposes it through a supported public interface.

## Design principles

- keep the upstream API minimal and focused on plugin needs
- prefer exposing an already-existing semantic event rather than inventing new duplicate state
- do not expose implementation details like widget classes, tab containers, or internal layouts
- make the API usable from installed headers only
- preserve current fooyin behaviour and ownership boundaries
- avoid forcing plugins to include private GUI headers

## Recommended API shape

### Option A: expose current-playlist selection through a public GUI-facing service

Recommended approach.

Add a small public interface in fooyin that plugins can query from the plugin context, for example:

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

Implementation notes:

- this can be backed internally by `PlaylistController`
- the interface should live in installed public headers
- the plugin should only depend on the interface, not on `PlaylistTabs`, `PlaylistWidget`, or other internal GUI classes

Why this is best:

- smallest semantic surface
- directly matches the vim plugin need
- no tab-widget assumptions leak into plugins
- future UI refactors inside fooyin do not break plugins as long as “current selected playlist” semantics remain stable

### Option B: expose a public signal on an already-public controller

Alternative if fooyin maintainers prefer not to add a new service object.

Possible approach:

- make the relevant selected-playlist observer API available through an already-installed public class reachable from plugin context
- ensure the selected-playlist signal is documented as referring to the selected main playlist tab, not playback state

This is acceptable, but only if the class is already intended as plugin-facing public surface.

### Option C: expose a tab-level GUI abstraction

Not recommended.

Examples:

- a public `PlaylistTabs` API
- a public “tab changed” signal tied to widgets or tab bars

Why not:

- exposes UI implementation details instead of the semantic event plugins actually need
- more fragile if fooyin reorganises the GUI
- wider API surface than necessary

## Proposed fooyin changes

### 1. Add a public installed header

Add a new public header under fooyin installed includes, for example:

- `include/gui/playlist/playlistselectionobserver.h`

Contents:

- forward declarations for `Playlist` and `UId`
- the small `QObject`-based observer interface
- export macro and minimal documentation comments

### 2. Provide the implementation in fooyin GUI

Add an internal implementation object, for example:

- `src/gui/playlist/playlistselectionobserverimpl.h/.cpp`

Responsibilities:

- hold a pointer/reference to `PlaylistController`
- forward `currentPlaylist()` and `currentPlaylistId()`
- re-emit `PlaylistController::currentPlaylistChanged(...)`

### 3. Register the observer in plugin-accessible context

Expose the observer via the plugin context/service registry that plugins already use.

Requirements:

- the vim plugin must be able to obtain it from installed public interfaces only
- lifetime should be owned by fooyin GUI/application
- the service should be available during normal plugin GUI initialisation

Examples of acceptable patterns:

- register in `CorePluginContext` / GUI plugin context as an object service
- expose through a public application service locator already intended for plugins

### 4. Document the semantics precisely

The API contract must explicitly state:

- `currentPlaylistChanged(previous, current)` refers to the **selected** playlist in the main playlist UI flow
- this is distinct from the active/playing playlist
- the signal may fire before all playlist rows are populated in the view model
- `current` may be `nullptr` if fooyin supports a “no playlist selected” state

That last point matters for plugins doing delayed restoration.

## Vim plugin follow-up plan once API exists

After the fooyin API is upstreamed, the vim plugin implementation would be:

1. query the new observer service during GUI initialisation
2. connect to `currentPlaylistChanged(previous, current)`
3. keep the in-memory per-playlist vim state map keyed by playlist ID
4. save row/column/visual state for `previous`
5. restore row/column/visual state for `current` after the active view/model is ready
6. keep playlist-specific state session-only

No private fooyin headers would be needed.

## Edge cases the upstream API should support

- current playlist changes before the playlist view finishes asynchronous model population
- playlist removal while it is current or while cached plugin state still references it
- startup restoration where the selected playlist is set before the plugin fully initialises
- layouts with multiple playlist-related widgets, where the semantic selected playlist still has a single authoritative meaning
- detached or auxiliary playlist-like views that should not redefine the main selected playlist semantics unless fooyin explicitly wants them to

## Testing plan for fooyin upstream work

Testing should happen in fooyin, not in the vim plugin at this stage.

### Unit/API-level coverage

Add tests that verify:

- observer reports the same current playlist ID as `PlaylistController`
- observer emits exactly one `currentPlaylistChanged(previous, current)` per actual selected-playlist change
- no emission occurs when reselecting the already-current playlist

### Integration/UI coverage

Add tests that verify:

- switching playlist tabs emits the public observer signal with correct previous/current playlists
- changing the playing playlist without changing the selected tab does not emit the selected-playlist signal
- startup current-playlist restoration results in the observer exposing the correct current playlist

## Compatibility and migration

Expected migration path:

1. upstream and release the new fooyin public API
2. keep existing internal fooyin implementation details unchanged behind the new interface
3. update the vim plugin to consume only the new public observer
4. remove plugin dependence on private fooyin headers

Potential versioning note:

- the vim plugin may need a compile-time or runtime minimum fooyin version check once it adopts the new API

## Risks

- exposing too much of `PlaylistController` instead of a minimal semantic interface
- ambiguity over whether “current playlist” means selected tab or playing playlist
- initialisation ordering between plugin startup and GUI service registration
- edge cases around multiple playlist-related UI surfaces if fooyin semantics are not documented tightly

## Suggested implementation order in fooyin

1. define the minimal public observer interface and document semantics
2. implement internal forwarding from `PlaylistController`
3. register/expose the observer through plugin-accessible public context
4. add fooyin unit/integration tests for selection semantics
5. release the API
6. update the vim plugin in a follow-up change to consume the new observer

## Deliverables

For the fooyin upstream change:

- installed public header for selected-playlist observation
- internal implementation backed by current fooyin playlist-selection logic
- plugin-accessible service registration
- tests
- documentation/comments describing selected-playlist semantics clearly
