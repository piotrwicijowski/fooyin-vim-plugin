# Mark Indicator Virtual-Column API Plan

## Goal

Allow plugins to display per-playlist-entry UI state, such as vim marks, in playlist views via a public virtual-column API.

## Problem

The vim plugin can track marks by `playlistId` and `entryId`, but the current public plugin API does not let plugins contribute extra playlist columns or per-row display data. Existing playlist model and column types are private GUI internals.

## Proposed Public API

Add a GUI plugin extension point for virtual playlist columns.

Possible shape:

1. Expose a public `PlaylistColumnProvider` interface in installed headers.
2. Let GUI plugins register/unregister providers through `GuiPluginContext`.
3. Each provider declares one or more columns with stable ids, names, width/alignment hints, and optional sort support.
4. Each provider can return display data for a playlist entry using public identity only:
   - `playlistId`
   - `entryId`
   - optionally current row/index as a hint

## Minimum Data Contract

For each visible cell, fooyin should ask the provider for:

1. text
2. optional icon
3. optional tooltip
4. optional foreground/background styling hints

For mark indicators, the vim plugin would return the mark letter for the matching entry and empty data otherwise.

## Integration Requirements

1. Virtual columns must participate in playlist column/preset configuration like built-in columns.
2. Column rendering must not require plugins to depend on private `PlaylistModel` or `PlaylistView` types.
3. Updates should be refreshable by entry id so a plugin can invalidate only affected rows when marks change.

## Vim Mark Use Case

1. Plugin registers a `Mark` column.
2. For an entry marked `a`, the provider returns `"a"`.
3. Moving the track preserves the indicator because it is keyed by `entryId`.
4. Deleting/cutting/pasting updates naturally as plugin mark state changes.

## Notes

1. This API is broader than vim marks and would also support other plugin-owned transient annotations.
2. A narrower row-decoration API would be simpler, but the virtual-column API is the better fit for an explicit mark letter display.
