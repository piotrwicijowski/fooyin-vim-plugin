# Global Marks API Plan

## Goal

Support uppercase vim marks that can jump to a marked item in another playlist without relying on fooyin private GUI headers.

## Problem

The plugin can store a global mark with public playlist data today (`playlistId` + `entryId`), but it cannot fully restore that mark because the public plugin API does not expose a way to switch the UI's current/visible playlist. The available public `PlaylistHandler::changeActivePlaylist()` controls playback-active state, not the playlist currently shown in the playlist UI.

## Proposed Public API Extension

Expose a public GUI-level playlist switching surface to plugins, for example one of:

1. Add `PlaylistController` access to `GuiPluginContext`, with a public header and stable API.
2. Add a narrower public interface in `GuiPluginContext`, such as `CurrentPlaylistController`, with methods like:
   - `currentPlaylistId() const`
   - `changeCurrentPlaylist(const Fooyin::UId& id)`
3. Add a generic action/command surface that can switch the visible playlist by id.

## Minimum Plugin Flow After API Exists

1. Resolve the stored global mark's `playlistId` and `entryId`.
2. Switch the UI to the marked playlist via the new public API.
3. Resolve the row with `Playlist::indexOfTrackEntry(entryId)`.
4. Focus and scroll to that row in the playlist view.

## Notes

1. Mark storage can remain fully in the plugin.
2. Lowercase local marks do not need this API.
3. Tests for the eventual feature should cover cross-playlist jumps and missing/deleted marked entries.
