# Undo/Redo — Fooyin upstream PR + plugin implementation

## Background

fooyin already has a full `QUndoStack`-based playlist undo system:

- `PlaylistController` (`src/gui/playlist/playlistcontroller.h`) owns the stack and exposes
  `addToHistory(QUndoCommand*)`, `canUndo()`, `canRedo()`, `undoPlaylistChanges()`,
  `redoPlaylistChanges()`.
- `playlistcommands.h` defines `RemoveTracks`, `MoveTracks`, `InsertTracks`, `ResetTracks`
  (all `QUndoCommand` subclasses); their `undo()`/`redo()` call
  `replacePlaylistTracks(..., PlaylistTrackChangeSource::History)`.
- fooyin's own playlist widget pushes commands via `addToHistory` before mutating tracks.
  The handler itself does **not** auto-register mutations on the stack.

`PlaylistController` is a **private** `src/` type — it is not in `include/` and is not exposed
in `GuiPluginContext`. Therefore the vim plugin's direct `removePlaylistTracks` /
`replacePlaylistTracks` calls currently bypass the undo stack entirely.

## Current plugin status (Option A — own snapshot stack)

While the fooyin PR is being prepared/reviewed, the plugin implements its own snapshot-based
undo/redo stack (`u` / `Ctrl+R`) that covers all vim mutations:
`dd`, visual `d`, `p`, `P`, `Alt+j/k` (normal and visual).

Each entry stores the playlist ID, the full `TrackList` before and after, and the cursor row for
both directions. Undo calls `replacePlaylistTracks(..., History)`; redo does the same.
This stack is separate from fooyin's own undo stack; they govern different mutation sources
(vim motions vs. fooyin UI).

## Fooyin upstream PR — expose PlaylistHistory in GuiPluginContext

### Goal

Allow third-party plugins to push `QUndoCommand` objects onto fooyin's playlist undo stack
and to trigger undo/redo programmatically, without depending on private `src/` headers.

### Files to change (5 files, ~60-80 lines)

1. **`include/gui/playlist/playlisthistory.h`** — new file

   Minimal abstract interface:

   ```cpp
   class FYGUI_EXPORT PlaylistHistory
   {
   public:
       virtual ~PlaylistHistory() = default;

       virtual void addToHistory(QUndoCommand* command) = 0;
       [[nodiscard]] virtual bool canUndo() const = 0;
       [[nodiscard]] virtual bool canRedo() const = 0;
       virtual void undoPlaylistChanges() = 0;
       virtual void redoPlaylistChanges() = 0;
   };
   ```

2. **`include/gui/plugins/guiplugincontext.h`**

   - Forward-declare `PlaylistHistory`
   - Add `PlaylistHistory* playlistHistory` member
   - Add parameter to constructor

3. **`src/gui/playlist/playlistcontroller.h`**

   - Inherit from `PlaylistHistory` (add `public PlaylistHistory` to class declaration)
   - Mark the five methods `override`

4. **`src/gui/guiapplication.cpp`**

   - In the `m_guiPluginContext` initialiser, pass `m_playlistController.get()` as the new
     `playlistHistory` argument — one line change

5. **`CMakeLists.txt`** (gui install rules)

   - Install `include/gui/playlist/playlisthistory.h` — one line

### Why this is a good upstream PR

- Purely additive — zero risk of regression for existing plugins or fooyin internals
- Minimal, purposeful interface (5 methods, no private-type leakage)
- Enables any plugin to integrate with fooyin's undo history correctly
- `PlaylistController` itself stays private; only the interface moves to `include/`

## Plugin migration (after fooyin PR lands)

Once `PlaylistHistory` is available in `GuiPluginContext`:

1. Store `PlaylistHistory*` in `VimHandler` (passed from `VimMotionsPlugin::initialise`).
2. Replace the plugin's own `UndoEntry` snapshot stack with `QUndoCommand` subclass
   (`VimPlaylistCommand`) that stores before/after `TrackList` snapshots and calls
   `replacePlaylistTracks(..., History)` in `undo()`/`redo()`.
3. Each vim mutation pushes a `VimPlaylistCommand` via `playlistHistory->addToHistory(...)`.
4. `u` calls `playlistHistory->undoPlaylistChanges()`; `Ctrl+R` calls `redoPlaylistChanges()`.
5. Remove the plugin-internal `UndoEntry` / `m_undoStack` / `m_undoIndex` machinery.

Result: vim mutations and fooyin UI mutations share one unified undo history, fully consistent
with the existing `Edit.Undo` / `Edit.Redo` shortcuts.
