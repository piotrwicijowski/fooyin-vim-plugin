# Manual configuration and action reference

[Back to the README](../README.md)

Settings and bindings can be edited through **Settings → Plugins → Vim Motions → Configure**; see [Customizing Vim Motions](../README.md#customizing-vim-motions). This guide is for editing configuration files directly and looking up key syntax, actions, and the complete default configuration. The same scopes, key syntax, action names, and arguments are available in the dialog's binding editor.

## Configuration file

The plugin reads and writes vim bindings in fooyin's config file, located using fooyin's public configuration-path helper.
For a normal Linux installation this is typically `~/.config/fooyin/fooyin.conf` (or under `XDG_CONFIG_HOME` when set).
In portable mode, it uses `config/fooyin.conf` beside the executable. Portable bindings are kept separate from those of a
normal installation.

By default, all built-in bindings are active. Use [UseDefaultBindings](#usedefaultbindings) to start from a clean slate.

Changes made through the settings dialog apply on **Apply** or **OK** without restarting fooyin. Direct edits to `fooyin.conf` are only picked up on startup, so restart fooyin after changing the file manually.

## Search and input options

Slash-search wraparound is controlled by `WrapScan`, similar to Vim's `wrapscan` option. It defaults to `true`: `/`, `n`, and `N` wrap to the start or end when needed.

Set it to `false` to stop at the last or first match instead:

```ini
[VimMotions]
WrapScan=false
```

Pending multi-key input timeout is controlled by `PendingSequenceTimeout`. The value is in milliseconds and applies to both two-key bindings like `gg`/`dd` and mark-pending actions like `m` followed by a letter.

If the setting is omitted, it defaults to `0`. A value of `0` disables the timeout.

```ini
[VimMotions]
PendingSequenceTimeout=300
```

## Binding format

Each binding is a single INI entry:

```ini
[VimMotions]
Bindings\Global\Normal\j=moveCursor:+1
Bindings\Global\Normal\k=moveCursor:-1
Bindings\Global\Normal\gg=jumpToFirst
Bindings\Global\Normal\G=jumpToLast
Bindings\Global\Normal\dd=deleteRows
Bindings\Global\Visual\j=extendCursor:+1
Bindings\Global\Insert\<Esc>=leaveInsertMode
```

The key path is `Bindings\{Scope}\{Mode}\{KeyCombo}` and the value is `ActionName[:args]`.

### Percent-encoding in saved keys

fooyin saves binding keys in `fooyin.conf` using URL-style percent-encoding: `<` becomes `%3C`, `>` becomes `%3E`, and `+` becomes `%2B`. For example, these readable entries:

```ini
[VimMotions]
Bindings\Global\Normal\<CR><CR>=activateCurrentRow
Bindings\Global\Normal\<Space><Space>=fooyinAction:Playback.PlayPause
Bindings\Global\Normal\Ctrl+A=selectAll
```

appear in the saved file as:

```ini
[VimMotions]
Bindings\Global\Normal\%3CCR%3E%3CCR%3E=activateCurrentRow
Bindings\Global\Normal\%3CSpace%3E%3CSpace%3E=fooyinAction:Playback.PlayPause
Bindings\Global\Normal\Ctrl%2BA=selectAll
```

Both forms work when editing the file manually. You can write an unencoded key such as `<Space>p`; fooyin will rewrite it as `%3CSpace%3Ep` when it next runs and saves the configuration. This changes only the stored representation, not the binding's behavior. The examples and [complete default configuration](#explicit-default-configuration) in this guide use the readable, unencoded form. Use that same readable form in the settings dialog's **Keys** field.

### Scopes and key syntax

Supported scopes are `Global`, `PlaylistView`, `PlaylistOrganiser`, `SearchLibraryDialog`, and `PlaylistPicker`. `Global` applies unless a narrower scope defines the same key sequence for that mode.

`SearchLibraryDialog` applies only inside fooyin's `Search Library` dialog. It lets dialog-specific bindings override ordinary playlist-view bindings without leaking into normal playlist tabs. When the Search Library line edit gains focus, the plugin temporarily switches to Insert mode and restores the previous Normal or Visual mode when focus leaves that line edit.

`PlaylistPicker` applies only inside the fuzzy playlist picker, in its local Insert or Normal mode, with the usual Global fallback. Its local mode is independent of the invoking view's mode. See [Playlist picker examples](#playlist-picker-examples).

**Key combo syntax:**
- Single character: `j`, `k`, `G`, `;`, `'`, `` ` ``
- Special keys: `<Esc>`, `<CR>`, `<Enter>`, `<Tab>`, `<BS>`, `<Space>`, `<Del>`, `<Insert>`, `<Home>`, `<End>`, `<PageUp>`, `<PageDown>`, `<Left>`, `<Right>`, `<Up>`, `<Down>`
- Special punctuation keys that need explicit names in config paths: `<Slash>`, `<Bslash>`, `<Bar>`, `<Lt>`
- Modifier combos: `Ctrl+J`, `Alt+J`, `Meta+J`, `Ctrl+Shift+K`
- Multi-key sequences: `gg`, `dd`, `yy`, `g;`, `g<Space>`, `<Space><Space>`
- Operator-pending prefixes: `m`, `'`, `` ` `` can be bound to mark actions that consume the next letter; lowercase uses local marks and uppercase uses global marks

Modifier combos require exactly the configured Ctrl, Alt, Shift, and Meta modifiers. For example, `Ctrl+L` does not match `Ctrl+Shift+L`; bind both explicitly if both should run the same action. Keypad status is ignored. Plain character bindings such as `G`, `?`, and `:` match the typed character, including when Shift is needed to produce it. For modifier combos involving shifted symbols, include Shift explicitly when your keyboard layout requires it (for example, `Ctrl+Shift+?`).

**Action name + args:**
- No args: `undo`, `enterInsert`, `jumpToFirst`
- With args: `moveCursor:+1`, `spatialMoveFocus:down`, `treeMoveSibling:-1`
- Trigger any fooyin command by ID: `fooyinAction:Playback.Next`, `fooyinAction:View.ShowNowPlaying`
- The arg string is passed to the action handler; each action parses its own args

## Available actions

| Action | Args | Modes | Description |
|---|---|---|---|
| `moveCursor` | `+1` / `-1` | Normal | Move cursor by N rows |
| `jumpToFirst` | — | Normal | Jump to first row |
| `jumpToLast` | — | Normal | Jump to last row |
| `jumpToRow` | `N` | Normal | Jump to row N (0-indexed) |
| `jumpOlder` | — | Normal, Visual | Go to an older playlist track position, using `[count]` if given; return to Normal |
| `jumpNewer` | — | Normal, Visual | Go to a newer playlist track position, using `[count]` if given; return to Normal |
| `moveCursorHalfPage` | `+1` / `-1` | Normal | Move cursor by half page |
| `activateCurrentRow` | — | Normal | Activate (Enter) current row |
| `treeMoveSibling` | `+1` / `-1` | Normal | Move to next/prev tree sibling |
| `treeOpenOrDescend` | — | Normal, Visual | Expand node or descend to first child |
| `treeCloseOrAscend` | — | Normal, Visual | Collapse node or ascend to parent |
| `organiserCreatePlaylist` | — | Normal | Create a new playlist in the playlist organiser |
| `organiserCreateGroup` | — | Normal | Create a new group in the playlist organiser |
| `enterInsert` | — | Normal | Switch to Insert mode |
| `enterVisual` | — | Normal | Switch to Visual mode |
| `selectAll` | — | Normal, Visual | Enter Visual mode and select the full row range, leaving the cursor on the last row |
| `leaveInsertMode` | — | Insert | Return to Normal mode |
| `leaveVisualMode` | — | Visual | Return to Normal mode |
| `enterFilter` | — | Normal | Open incremental filter bar |
| `enterSearch` | — | Normal | Open `/` search bar |
| `nextMatch` | — | Normal | Jump to next search match |
| `prevMatch` | — | Normal | Jump to previous search match |
| `deleteRows` | — | Normal | Delete N rows from playlist |
| `yankRows` | — | Normal | Yank N rows into vim clipboard |
| `pasteAfter` | — | Normal | Paste after cursor |
| `pasteBefore` | — | Normal | Paste before cursor |
| `copyAfterCurrentPlaying` | — | Normal, Visual | Copy the current row/visual selection to immediately after the currently playing track |
| `moveAfterCurrentPlaying` | — | Normal, Visual | Move the current row/visual selection to immediately after the currently playing track |
| `undo` | — | Normal | Undo last playlist change |
| `redo` | — | Normal | Redo last undone change |
| `focusNowPlaying` | — | Normal | Focus currently playing track |
| `focusNowPlayingAndExit` | — | Visual | Return to Normal, then focus the currently playing track |
| `nextPlaylist` | — | Normal, Visual | Switch the selected playlist forward by one, or by `[count]` playlists |
| `previousPlaylist` | — | Normal, Visual | Switch the selected playlist backward by one, or by `[count]` playlists |
| `findPlaylist` | — | Normal, Visual | Open the fuzzy playlist-name picker |
| `playlistPickerMove` | signed delta | Picker Insert, Normal | Move the picker highlight by N results, wrapping |
| `playlistPickerAccept` | — | Picker Insert, Normal | Open the highlighted playlist |
| `playlistPickerCancel` | — | Picker Insert, Normal | Cancel and restore invoking focus |
| `fooyinAction` | fooyin action ID | Normal, Visual, Insert | Trigger any registered fooyin action by ID via `ActionManager` |
| `beginSetMark` | — | Normal, Visual | Begin setting a mark; the next letter chooses a mark |
| `beginJumpToMark` | — | Normal, Visual | Begin jumping to a mark; the next letter chooses a mark |
| `moveRows` | `+1` / `-1` | Normal | Move current row in playlist |
| `extendCursor` | `+1` / `-1` | Visual | Extend visual selection by N rows |
| `extendToFirst` | — | Visual | Extend selection to first row |
| `extendToEnd` | — | Visual | Extend selection to last row |
| `extendToRow` | `N` | Visual | Extend selection to row N |
| `extendHalfPage` | `+1` / `-1` | Visual | Extend selection by half page |
| `swapAnchor` | — | Visual | Swap visual anchor and cursor |
| `deleteSelection` | — | Visual | Delete visual range, return to Normal |
| `yankSelection` | — | Visual | Yank visual range, return to Normal |
| `moveVisualSelection` | `+1` / `-1` | Visual | Move selected rows in playlist |
| `nextMatchAndExit` | — | Visual | Next match, return to Normal |
| `prevMatchAndExit` | — | Visual | Previous match, return to Normal |
| `enterSearchAndExit` | — | Visual | Open search, return to Normal |
| `spatialMoveFocus` | `up`/`down`/`left`/`right` | Normal, Visual | Move focus between panes; from Visual, keep the old selection and return to Normal |
| `clearPending` | — | Normal | Clear count / pending two-key key |

Inside `PlaylistPicker`, `enterInsert`, `leaveInsertMode`, and `clearPending` affect only local picker input, not the invoking playlist's mode or selection.

`selectAll`, `copyAfterCurrentPlaying`, and `moveAfterCurrentPlaying` are available for custom bindings, but have no default bindings. In `Search Library`, `copyAfterCurrentPlaying` copies detached result tracks into the currently playing playlist; `moveAfterCurrentPlaying` is unsupported there and only logs a warning.

## Playlist picker examples

To open the [fuzzy playlist picker](../README.md#fuzzy-playlist-picker) with a different key:

```ini
[VimMotions]
Bindings\Global\Normal\Ctrl+P=findPlaylist
```

Each invocation starts in picker-local Insert mode. To cancel with one Escape directly from Insert, instead of switching to Normal first:

```ini
[VimMotions]
Bindings\PlaylistPicker\Insert\<Esc>=playlistPickerCancel
```

## Binding fooyin actions directly

Use `fooyinAction:<Action.Id>` when you want a vim binding to trigger an existing fooyin command without adding a plugin-specific wrapper:

```ini
[VimMotions]
Bindings\Global\Normal\<Space>=fooyinAction:Playback.PlayPause
Bindings\Global\Normal\x=fooyinAction:Playback.Next
Bindings\Global\Normal\z=fooyinAction:View.ShowNowPlaying
```

There is no default binding for `fooyinAction`; it is available for custom bindings in the dialog or configuration file.

`nextPlaylist` and `previousPlaylist` are also available for custom bindings only. They do not have default bindings.

## UseDefaultBindings

By default (`UseDefaultBindings=true`), all built-in bindings are active. You can still override or unmap individual bindings on top of those defaults.

Set `UseDefaultBindings=false` to start from a **clean slate** — no default bindings are loaded. Only bindings you explicitly configure will take effect:

```ini
[VimMotions]
UseDefaultBindings=false
Bindings\Global\Normal\j=moveCursor:+1
Bindings\Global\Normal\k=moveCursor:-1
Bindings\Global\Normal\gg=jumpToFirst
Bindings\Global\Normal\G=jumpToLast
```

With this config, only `j`, `k`, `gg`, and `G` are active in Normal mode — all other defaults are ignored. See [Explicit default configuration](#explicit-default-configuration) for the full list of default bindings.

## UseVimMotionsInSettings

By default (`UseVimMotionsInSettings=false`), vim bindings are ignored while focus is inside fooyin's main **Settings** dialog and any sub-dialog opened from it.

Set `UseVimMotionsInSettings=true` to keep vim motions active there too:

```ini
[VimMotions]
UseVimMotionsInSettings=true
```

## Unmapping bindings

To remove (unmap) a default binding, set its value to an empty string:

```ini
[VimMotions]
Bindings\Global\Normal\j=
Bindings\Global\Visual\<Esc>=
```

This removes the binding in that scope/mode; a matching Global binding can still apply. Ordinary unhandled input falls through to fooyin. In the playlist picker, Escape/Enter without an effective binding cannot trigger implicit dialog actions, and unmapped Normal-mode command keys are consumed.

## Explicit default configuration

To opt into only the bindings you need, set `UseDefaultBindings=false` and copy the desired lines below into your config. The full set of default bindings is:

```ini
[VimMotions]
UseDefaultBindings=false

; -- Global / Normal mode --
Bindings\Global\Normal\j=moveCursor:+1
Bindings\Global\Normal\k=moveCursor:-1
Bindings\Global\Normal\gg=jumpToFirst
Bindings\Global\Normal\G=jumpToLast
Bindings\Global\Normal\dd=deleteRows
Bindings\Global\Normal\i=enterInsert
Bindings\Global\Normal\v=enterVisual
Bindings\Global\Normal\h=treeCloseOrAscend
Bindings\Global\Normal\l=treeOpenOrDescend
Bindings\Global\Normal\o=focusNowPlaying
Bindings\Global\Normal\g;=focusNowPlaying
Bindings\Global\Normal\<Slash>=enterSearch
Bindings\Global\Normal\n=nextMatch
Bindings\Global\Normal\N=prevMatch
Bindings\Global\Normal\<Esc>=clearPending
Bindings\Global\Normal\Ctrl+J=spatialMoveFocus:down
Bindings\Global\Normal\Ctrl+K=spatialMoveFocus:up
Bindings\Global\Normal\Ctrl+H=spatialMoveFocus:left
Bindings\Global\Normal\Ctrl+L=spatialMoveFocus:right
Bindings\Global\Normal\Ctrl+D=moveCursorHalfPage:+1
Bindings\Global\Normal\Ctrl+U=moveCursorHalfPage:-1
Bindings\Global\Normal\gi=enterFilter
Bindings\Global\Normal\<Space>p=findPlaylist
Bindings\Global\Normal\Ctrl+O=jumpOlder
Bindings\Global\Normal\Ctrl+I=jumpNewer
Bindings\Global\Normal\Alt+J=moveRows:+1
Bindings\Global\Normal\Alt+K=moveRows:-1

; -- Playlist View / Normal mode --
Bindings\PlaylistView\Normal\yy=yankRows
Bindings\PlaylistView\Normal\u=undo
Bindings\PlaylistView\Normal\Ctrl+R=redo
Bindings\PlaylistView\Normal\m=beginSetMark
Bindings\PlaylistView\Normal\'=beginJumpToMark
Bindings\PlaylistView\Normal\`=beginJumpToMark
Bindings\PlaylistView\Normal\p=pasteAfter
Bindings\PlaylistView\Normal\P=pasteBefore

; -- Search Library Dialog / Normal mode --
Bindings\SearchLibraryDialog\Normal\yy=yankRows

; -- Playlist Organiser / Normal mode --
Bindings\PlaylistOrganiser\Normal\a=organiserCreatePlaylist
Bindings\PlaylistOrganiser\Normal\A=organiserCreateGroup
Bindings\PlaylistOrganiser\Normal\Ctrl+Shift+J=treeMoveSibling:+1
Bindings\PlaylistOrganiser\Normal\Ctrl+Shift+K=treeMoveSibling:-1

; -- Global / Visual mode --
Bindings\Global\Visual\j=extendCursor:+1
Bindings\Global\Visual\k=extendCursor:-1
Bindings\Global\Visual\gg=extendToFirst
Bindings\Global\Visual\G=extendToEnd
Bindings\Global\Visual\o=swapAnchor
Bindings\Global\Visual\v=leaveVisualMode
Bindings\Global\Visual\<Space>p=findPlaylist
Bindings\Global\Visual\<Esc>=leaveVisualMode
Bindings\Global\Visual\n=nextMatchAndExit
Bindings\Global\Visual\N=prevMatchAndExit
Bindings\Global\Visual\<Slash>=enterSearchAndExit
Bindings\Global\Visual\Ctrl+D=extendHalfPage:+1
Bindings\Global\Visual\Ctrl+U=extendHalfPage:-1
Bindings\Global\Visual\Ctrl+O=jumpOlder
Bindings\Global\Visual\Ctrl+I=jumpNewer
Bindings\Global\Visual\Ctrl+J=spatialMoveFocus:down
Bindings\Global\Visual\Ctrl+K=spatialMoveFocus:up
Bindings\Global\Visual\Ctrl+H=spatialMoveFocus:left
Bindings\Global\Visual\Ctrl+L=spatialMoveFocus:right
Bindings\Global\Visual\h=treeCloseOrAscend
Bindings\Global\Visual\l=treeOpenOrDescend
Bindings\Global\Visual\g;=focusNowPlayingAndExit

; -- Playlist View / Visual mode --
Bindings\PlaylistView\Visual\d=deleteSelection
Bindings\PlaylistView\Visual\y=yankSelection
Bindings\PlaylistView\Visual\m=beginSetMark
Bindings\PlaylistView\Visual\'=beginJumpToMark
Bindings\PlaylistView\Visual\`=beginJumpToMark
Bindings\PlaylistView\Visual\Alt+J=moveVisualSelection:+1
Bindings\PlaylistView\Visual\Alt+K=moveVisualSelection:-1

; -- Search Library Dialog / Visual mode --
Bindings\SearchLibraryDialog\Visual\y=yankSelection

; -- Global / Insert mode --
Bindings\Global\Insert\<Esc>=leaveInsertMode

; -- Search Library Dialog / Insert mode --
Bindings\SearchLibraryDialog\Insert\Ctrl+J=spatialMoveFocus:down
Bindings\SearchLibraryDialog\Insert\Ctrl+K=spatialMoveFocus:up

; -- Playlist Picker / Insert mode --
Bindings\PlaylistPicker\Insert\Ctrl+N=playlistPickerMove:+1
Bindings\PlaylistPicker\Insert\<Down>=playlistPickerMove:+1
Bindings\PlaylistPicker\Insert\Ctrl+P=playlistPickerMove:-1
Bindings\PlaylistPicker\Insert\<Up>=playlistPickerMove:-1
Bindings\PlaylistPicker\Insert\<CR>=playlistPickerAccept
Bindings\PlaylistPicker\Insert\<Enter>=playlistPickerAccept
Bindings\PlaylistPicker\Insert\<Esc>=leaveInsertMode

; -- Playlist Picker / Normal mode --
Bindings\PlaylistPicker\Normal\Ctrl+N=playlistPickerMove:+1
Bindings\PlaylistPicker\Normal\<Down>=playlistPickerMove:+1
Bindings\PlaylistPicker\Normal\Ctrl+P=playlistPickerMove:-1
Bindings\PlaylistPicker\Normal\<Up>=playlistPickerMove:-1
Bindings\PlaylistPicker\Normal\<CR>=playlistPickerAccept
Bindings\PlaylistPicker\Normal\<Enter>=playlistPickerAccept
Bindings\PlaylistPicker\Normal\<Esc>=playlistPickerCancel
Bindings\PlaylistPicker\Normal\j=playlistPickerMove:+1
Bindings\PlaylistPicker\Normal\k=playlistPickerMove:-1
Bindings\PlaylistPicker\Normal\i=enterInsert
```
