# Plan 11: Playlist organiser yank/paste

## Status

Investigation only. No implementation yet.

`dd` in the playlist organiser is now fixed by routing to fooyin's existing
organiser `Remove` action when focus is on `PlaylistOrganiser`.

This note captures the follow-up findings for adding organiser `yy` / `p`.

## Current state

- The organiser already supports internal item moves through its model drag/drop
  path.
- `PlaylistOrganiserModel::mimeData()` serialises selected organiser indexes as
  `application/x-fooyin-playlistorganiseritems`.
- `PlaylistOrganiserModel::itemsDropped()` restores those indexes and performs
  `beginMoveRows()` / `endMoveRows()` reordering.
- `supportedDragActions()` is `Qt::MoveAction` only.
- `supportedDropActions()` allows `MoveAction | CopyAction`, but copy is for
  track/file drops onto the organiser, not for duplicating organiser items.

Relevant fooyin code:

- `/fooyin/src/gui/playlist/organiser/playlistorganisermodel.cpp:693`
- `/fooyin/src/gui/playlist/organiser/playlistorganisermodel.cpp:703`
- `/fooyin/src/gui/playlist/organiser/playlistorganisermodel.cpp:874`

## Why `dd` was easy but `yy`/`p` is not

`dd` could reuse an existing public action (`Remove`) that already knows how to
delete selected organiser items.

`yy` / `p` needs a definition of what clipboard data means in the organiser:

- move existing organiser nodes
- reinsert previously deleted organiser nodes
- duplicate groups/playlists as new objects

Those are very different behaviours.

## Constraints discovered

### 1. Organiser nodes are not naturally copyable

The organiser model stores nodes in `m_nodes`, keyed by group title or playlist
name.

- Groups use `groupKey(title)`
- Playlists use `playlistKey(name)`

That means the same playlist node cannot simply be inserted twice into the tree.
Any true paste-as-copy feature needs to create new nodes with unique identity.

Relevant fooyin code:

- `/fooyin/src/gui/playlist/organiser/playlistorganisermodel.cpp:105`
- `/fooyin/src/gui/playlist/organiser/playlistorganisermodel.cpp:111`
- `/fooyin/src/gui/playlist/organiser/playlistorganisermodel.cpp:842`

### 2. Playlist paste semantics are unresolved

For a selected playlist, `yy` could mean any of:

- copy only the organiser placement
- duplicate the underlying playlist and its tracks
- duplicate the underlying autoplaylist definition

The first option conflicts with the model's uniqueness assumptions.
The second and third require real playlist cloning semantics.

### 3. Group paste semantics are also non-trivial

Groups are organiser-only structure, but a pasted group may contain playlists.
So group paste either needs:

- recursive organiser-only copy with cloned playlists underneath, or
- a move/reinsert-only model rather than true copy.

### 4. The vim plugin clipboard is track-only today

`VimClipboard` currently stores only `Fooyin::TrackList` plus mark-transfer
metadata for track operations.

Relevant plugin code:

- `src/vimclipboard.h:11`

Supporting organiser yank/paste would need either:

- a second organiser clipboard path in `VimHandler`, or
- a broader clipboard abstraction that can represent both track data and
  organiser data.

## Existing reusable pieces

These may reduce implementation work:

- organiser index serialisation already exists via
  `PlaylistOrganiserModel::saveIndexes()` / `restoreIndexes()`
- organiser model save/restore logic already serialises tree structure using
  playlist db ids and group titles
- organiser model already has insertion helpers:
  `createGroup()`, `createPlaylist()`, `playlistInserted()`

Relevant fooyin code:

- `/fooyin/src/gui/playlist/organiser/playlistorganisermodel.cpp:242`
- `/fooyin/src/gui/playlist/organiser/playlistorganisermodel.cpp:278`
- `/fooyin/src/gui/playlist/organiser/playlistorganisermodel.cpp:302`
- `/fooyin/src/gui/playlist/organiser/playlistorganisermodel.cpp:316`
- `/fooyin/src/gui/playlist/organiser/playlistorganisermodel.cpp:339`

## Realistic implementation options

### Option A: move-style organiser paste

Behaviour:

- `yy` stores selected organiser indexes
- `p` moves those existing items after the current row

Pros:

- smallest implementation
- can likely reuse current organiser mime/index move path

Cons:

- not true vim yank semantics
- effectively acts more like a deferred move than a copy

Effort estimate: small-to-medium

### Option B: delete/cut then paste reinsert

Behaviour:

- organiser delete stores a serialised organiser subtree
- `p` reinserts it elsewhere

Pros:

- useful with `dd`
- avoids immediate need for true duplication semantics

Cons:

- still not true `yy`
- needs explicit restore/insertion path for removed organiser items

Effort estimate: medium

### Option C: true copy semantics

Behaviour:

- `yy` copies selected groups/playlists
- `p` duplicates them after the current location

Pros:

- matches expected yank/paste semantics best

Cons:

- needs organiser clipboard data model
- needs recursive group copy
- needs playlist cloning logic
- autoplaylists likely need special handling

Effort estimate: medium-to-large

## Recommendation

If this is resumed later, the lowest-risk progression is:

1. Decide semantics first.
2. If fast value is preferred, implement Option B or Option A.
3. Only pursue true `yy` / `p` copy semantics if duplicating playlists is an
   explicit requirement.

Recommended default direction: support organiser `dd` / `p` first, or a
move-style organiser paste, before attempting full copy semantics.

## Open questions for future work

1. Should organiser `yy` duplicate playlists, or only remember organiser items
   for later move/reinsert?
2. If a playlist is pasted, should the result be a new playlist with copied
   tracks, or another organiser reference to the same playlist?
3. How should autoplaylists be cloned?
4. Should organiser clipboard data coexist with the current track clipboard, or
   should the plugin maintain separate clipboards per view context?
