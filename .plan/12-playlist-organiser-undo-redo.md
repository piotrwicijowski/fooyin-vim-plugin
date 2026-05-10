# Playlist Organiser Undo/Redo

## Summary

The vim plugin currently supports `dd` delete inside the playlist organiser, but its `u` / `Ctrl+R`
undo stack does not cover organiser mutations.

This is not a small wiring change. The organiser uses a different mutation path from playlist track
edits, and fooyin does not appear to expose an existing organiser undo/redo stack that the plugin can
reuse.

## Current plugin behaviour

- Playlist-view edits (`dd`, visual delete, `p`, `P`, `Alt+j/k`) use the plugin's internal snapshot
  undo stack in `src/vimhandler.cpp`.
- Playlist organiser `dd` does not push an undo entry.
- In organiser context, `deleteRows()` just triggers the current context `Remove` action and returns.

Relevant code:

- `/workspace/src/vimhandler.cpp:1499-1550` - plugin undo/redo implementation
- `/workspace/src/vimhandler.cpp:1587-1593` - organiser delete path
- `/workspace/src/vimhandler.cpp:1812-1938` - organiser move path

## Fooyin organiser behaviour

The fooyin playlist organiser does direct model mutations for remove and move.

- `PlaylistOrganiser` connects the Remove action directly to `PlaylistOrganiserModel::removeItems()`.
- `PlaylistOrganiserModel::removeItems()` removes tree rows immediately.
- For playlist nodes, deletion also calls `PlaylistHandler::removePlaylist(id)`.
- Organiser moves are performed through `mimeData()` + `dropMimeData()` on the organiser model.

Relevant code:

- `/fooyin/src/gui/playlist/organiser/playlistorganiser.cpp:269-270`
- `/fooyin/src/gui/playlist/organiser/playlistorganisermodel.cpp:748-775`
- `/fooyin/src/gui/playlist/organiser/playlistorganisermodel.cpp:847-872`
- `/fooyin/src/gui/playlist/organiser/playlistorganisermodel.cpp:703-746`

## Existing fooyin undo/redo support

Fooyin already has a `QUndoStack`-based playlist-track undo system, but it is specific to playlist
track edits, not playlist organiser structure.

Relevant code:

- `/fooyin/src/gui/playlist/playlistcontroller.h:93-97`
- `/fooyin/src/gui/playlist/playlistcontroller.cpp:217-257`
- `/fooyin/src/gui/playlist/playlistcommands.h`
- `/fooyin/src/gui/playlist/playlistcommands.cpp`

This existing history is not used by organiser remove/move operations.

## Public API constraints

The public `PlaylistHandler` API can create, rename, and remove playlists, including auto playlists,
but it does not appear to expose a way to recreate a removed playlist with the exact original internal
identity.

Relevant code:

- `/fooyin/include/core/playlist/playlisthandler.h:67-129`
- `/fooyin/include/core/playlist/playlist.h:127-137`

Implication:

- Functional undo is feasible by recreating playlists and rebuilding organiser structure.
- Exact identity-preserving undo is likely not possible from plugin code alone.

## Effort estimate

### Delete-only organiser undo/redo (`dd`)

Estimated effort: medium to high, roughly 1-2 days.

Work required:

- Add a separate organiser undo stack in the plugin.
- Capture enough organiser state to restore deleted groups/playlists at the original parent/row.
- For playlists, capture playlist metadata and tracks needed to recreate them.
- Restore cursor/selection after undo/redo.
- Add tests.

### Full organiser undo/redo

Estimated effort: roughly 3-6 days.

This would need to cover:

- delete
- move (`Alt+j/k` in organiser)
- group creation
- playlist creation/import paths if desired
- rename if desired
- mixed group + playlist subtree operations

### Unified global undo/redo with fooyin Edit menu/history

Estimated effort: larger upstream + plugin change, roughly 4-8 days across both repos.

This would likely require new public fooyin APIs for organiser history similar to the planned playlist
history exposure in `/workspace/.plan/04-undo-redo-fooyin-pr.md`.

## Main risks

- Recreated playlists may not keep the exact same internal identity after undo.
- Group deletion can remove nested playlists and subgroups, so subtree snapshots are required.
- Organiser move undo needs tree-path snapshots, not just visible row numbers.
- Mixed selection delete needs careful ordering so restore produces the same structure.

## Recommended minimal implementation

If the goal is only to make organiser `dd` undoable with acceptable behaviour:

1. Add a plugin-owned organiser undo stack separate from the existing playlist-track undo stack.
2. Represent organiser delete undo entries as subtree snapshots.
3. On undo, recreate groups and playlists and reinsert them at the original parent/row.
4. On redo, remove the recreated subtree again.
5. Limit the first version to organiser delete only.

## Open decision

Decide whether the target is:

1. organiser delete undo/redo only, or
2. full organiser mutation undo/redo including moves.

That choice strongly affects scope.
