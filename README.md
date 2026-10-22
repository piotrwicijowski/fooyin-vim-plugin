# fooyin-vim-plugin

Vim-style keybindings for [fooyin](https://github.com/ludouzi/fooyin). Adds Normal, Visual, Insert, Filter, and Search modes to every list view in fooyin (playlist, library browser, file browser, queue, etc.). It also provides an optional `Vim Mode Indicator` layout widget that displays the current mode.

## Features

### Modes

| Mode | How to enter | What it does |
|---|---|---|
| Normal | Start-up default / `Esc` from Insert, Search, Filter | All vim keys active; fooyin shortcuts suppressed |
| Visual | `v` | Linewise range selection |
| Insert | `i` | Vim off; all keys pass through to fooyin normally |
| Filter | `gi` | Live incremental filter — hides non-matching rows |
| Search | `/` | Live search — all rows visible, cursor jumps to matches; wraps by default |

### Mode indicator widget

Enable **Layout Editing Mode** in fooyin and add the `Vim Mode Indicator` widget from the `Vim Motions` submenu to place a compact current-mode display anywhere in your layout.

### Playlist-local cursor restore

When you switch between main playlist tabs, the plugin remembers the last vim cursor position for each playlist during the current session.

- Returning to a playlist restores the previous cursor row and column when possible.
- If you left that playlist in Visual mode, the previous visual selection is restored when possible.
- If the playlist shrank while you were away, the restored cursor and selection are clamped to the remaining rows.
- If the playlist is now empty, the plugin restores Normal mode without an invalid selection.

### Fuzzy playlist picker

Press **`Space` then `p`** in Normal or Visual mode to find a playlist by name:

1. Type a case insensitive, fuzzy query, for example `rckcl` for **Rock Classics**.
2. Use **`Ctrl+n` / `Ctrl+p`** to highlight the next / previous result retaining focus on input field.
3. Press **Enter** to open the highlighted playlist and focus its track view, or **Esc** twice to cancel (Insert → Normal → close).

The opening action is `findPlaylist`. To use a different key, configure it through the plugin settings or `fooyin.conf`:

```ini
[VimMotions]
Bindings\Global\Normal\Ctrl+P=findPlaylist
```

The picker uses the `PlaylistPicker` scope in the same binding editor and configuration format as other vim bindings. Each invocation starts in a picker-local **Insert** mode for typing.

| Picker mode | Default keys | Action |
|---|---|---|
| Insert, Normal | `Ctrl+n`, Down | Next result, keeping query focus |
| Insert, Normal | `Ctrl+p`, Up | Previous result, keeping query focus |
| Insert, Normal | Return, keypad Enter | Open highlighted playlist |
| Insert | `Esc` | Enter picker-local Normal mode |
| Normal | `Esc` | Cancel |
| Normal | `j` / `k` | Next / previous result; accepts a count |
| Normal | `i` | Resume typing in Insert |


To cancel with one Escape directly from Insert instead:

```ini
[VimMotions]
Bindings\PlaylistPicker\Insert\<Esc>=playlistPickerCancel
```

### Track-position jump history

`Ctrl+o` goes to an older playlist track position; `Ctrl+i` goes forward again. Both accept a count
(for example, `3Ctrl+o`) and work from Normal or Visual mode. Traversal restores a single cursor selection in Normal mode.

- History is shared across playlists and kept for the current session, up to 100 positions.
- A position identifies a **playlist and a particular track occurrence**. Duplicate copies of the same song are distinct;
  reordering or inserting tracks does not change the identity of a remembered position. The cursor column is restored,
  but changing columns alone does not create another history stop.
- The position **before** a successful jump is recorded: `gg`, `G`, `[count]G`, custom `jumpToRow` actions,
  accepted `/` searches, `n`/`N` search jumps, local/global mark jumps, `o`/`g;`, and the plugin's
  `nextPlaylist`/`previousPlaylist` actions and accepted `findPlaylist` switches. Their Visual-mode equivalents also record departures.
- Each next/previous-playlist action is a separate jump: after visiting playlists 1 → 2 → 3 → 4,
  `Ctrl+o` returns to the track left in playlist 3, then 2, then 1. One counted playlist-switch action records one departure.
- The first backward traversal preserves the current track position so `Ctrl+i` can return to it.
- Pane-focus changes, ordinary `j`/`k` movement (including counts), half-page scrolling, tree navigation, editing,
  filter changes, and automatic playback/model updates do not record jumps. `n`/`N` acting as row movement in a filtered
  view does not record jumps either. History contains only main-playlist track positions, not library, queue,
  organiser, or detached Search Library result positions.
- Search previews while typing do not create entries. Accepting a search records its starting position once if it moved;
  cancelling or failing a search records nothing.
- Revisited track positions are deduplicated. A new jump after going back discards the old forward branch
  (browser-style, like Vim's `jumpoptions=stack`). Failed and same-position jumps do not add entries.
- Deleted tracks/playlists are skipped. Hidden destinations in the current playlist are skipped without changing filters;
  collapsed ancestors are expanded when revealing a grouped track.

`gi` replaces the old Filter default on `Ctrl+i`. Existing explicit custom bindings continue to take precedence;
remove or update an old `Ctrl+I=enterFilter` override to use forward history traversal there.

### Script variables

The plugin exposes `%mark%` and `%global_mark%` for playlist script evaluation.

- `%mark%` resolves to the local vim mark letter for the current playlist entry, or empty when the entry is unmarked.
- `%global_mark%` resolves to the global vim mark letter for the current playlist entry, or empty when the entry is unmarked.
- If multiple marks of the same kind point at the same entry, the variable returns the lowest-letter mark.
- Both variables appear under **Vim Motions** in the Script Editor reference. They return empty when evaluation has no
  specific playlist entry context.
- Mark changes refresh the affected playlist rows in every view displaying those entries.

This feature uses fooyin's upstream shared `ScriptRegistry` and `PlaylistInteractor` refresh APIs (commits `29c0d84`
and `67f2735`, or later); a custom fooyin branch is no longer required.

### Normal mode bindings

Default bindings are installed per scope. `Global` bindings apply unless a narrower scope overrides them.

| Key | Scope | Action |
|---|---|---|
| `j` / `k` | Global | Move cursor down / up |
| `[count]j` / `[count]k` | Global | Move cursor N rows |
| `gg` | Global | Jump to first row |
| `G` / `[count]G` | Global | Jump to last row / row N (1-indexed) |
| `o` | Global | Focus currently playing item |
| `g;` | Global | Focus currently playing item |
| `m` + `a-z` | Playlist view | Set a local mark on the current playlist item |
| `m` + `A-Z` | Playlist view | Set a global mark on the current playlist item |
| `'` + `a-z` | Playlist view | Jump to a local mark in the current playlist |
| `'` + `A-Z` | Playlist view | Jump to a global mark, switching playlists if needed |
| `` ` `` + `a-z` | Playlist view | Jump to a local mark in the current playlist |
| `` ` `` + `A-Z` | Playlist view | Jump to a global mark, switching playlists if needed |
| `Ctrl+d` | Global | Half page down |
| `Ctrl+u` | Global | Half page up |
| `v` | Global | Enter Visual mode |
| `dd` / `[count]dd` | Global | Delete row(s), or remove the current item in the playlist organiser |
| `yy` / `[count]yy` | Playlist view | Yank row(s) into vim clipboard |
| `p` / `P` | Playlist view | Paste yanked rows after / before cursor |
| `a` | Playlist organiser | Create a new playlist |
| `A` | Playlist organiser | Create a new group |
| `u` | Playlist view | Undo last playlist change (`dd`, `p`, `P`, `Alt+j/k`) |
| `Ctrl+r` | Playlist view | Redo last undone change |
| `i` | Global | Enter Insert mode |
| `/` | Global | Enter Search mode |
| `n` / `N` | Global | Next / previous search match (+/-1 row if filter active) |
| `gi` | Global | Enter Filter mode |
| `<Space>p` | Global | Open fuzzy playlist picker |
| `[count]Ctrl+o` / `[count]Ctrl+i` | Global | Older / newer playlist track position |
| `h` | Global | Collapse node / ascend to parent (tree views) |
| `l` | Global | Expand node / descend to first child (tree views) |
| `Ctrl+Shift+J` | Playlist organiser | Next sibling at same level (tree views) |
| `Ctrl+Shift+K` | Playlist organiser | Previous sibling at same level (tree views) |
| `Ctrl+j/k/h/l` | Global | Move focus to the view below / above / left / right |
| `Alt+j` / `Alt+k` | Global | Move current row down / up in the playlist |
| `[count]Alt+j` / `[count]Alt+k` | Global | Move current row N positions down / up |

### Visual mode bindings

| Key | Scope | Action |
|---|---|---|
| `j` / `k` | Global | Extend selection down / up |
| `[count]j` / `[count]k` | Global | Extend selection N rows |
| `gg` | Global | Extend selection to first row |
| `G` / `[count]G` | Global | Extend selection to last row / row N (1-indexed) |
| `Ctrl+d` | Global | Extend selection half page down |
| `Ctrl+u` | Global | Extend selection half page up |
| `[count]Ctrl+o` / `[count]Ctrl+i` | Global | Older / newer playlist track position, return to Normal |
| `o` | Global | Toggle anchor; swap cursor between start and end |
| `v` | Global | Exit Visual mode |
| `h` / `l` | Global | Collapse / expand node (tree views) |
| `d` | Playlist view | Delete selected range, return to Normal |
| `y` | Playlist view | Yank selected range, return to Normal |
| `n` / `N` | Global | Next / previous search match, then return to Normal |
| `/` | Global | Enter Search mode, then return to Normal |
| `m` + `a-z` | Playlist view | Set a local mark on the current playlist item |
| `m` + `A-Z` | Playlist view | Set a global mark on the current playlist item |
| `'` + `a-z` | Playlist view | Jump to a local mark, extending the selection |
| `'` + `A-Z` | Playlist view | Jump to a global mark, switching playlists if needed |
| `` ` `` + `a-z` | Playlist view | Jump to a local mark, extending the selection |
| `` ` `` + `A-Z` | Playlist view | Jump to a global mark, switching playlists if needed |
| `g;` | Global | Focus currently playing item, then return to Normal |
| `<Space>p` | Global | Open fuzzy playlist picker; cancel preserves the selection |
| `Alt+j` / `Alt+k` | Playlist view | Move entire selection down / up in the playlist |
| `[count]Alt+j` / `[count]Alt+k` | Playlist view | Move entire selection N positions down / up |
| `Esc` | Global | Cancel selection, return to Normal |

Plugin-specific bindings are configured through **Settings → Plugins → Vim Motions → Configure** or `fooyin.conf`.

`selectAll` is available for custom config bindings, but it is not part of the default bindings because there is no close standard Vim equivalent.

## Requirements

- fooyin with plugin API `/2` (upstream commit `06c2b7e1d` or newer; source tree or installed with `-DINSTALL_HEADERS=ON`)
- Qt 6.2 or later (Widgets, Sql modules)
- CMake 3.19+
- A C++23 compiler (GCC 12+, Clang 15+)

## Building

### Option A — dev tree (sibling checkout, no install needed)

This is the fastest path during development. It assumes fooyin's source and build directories sit alongside this repository:

```
workspace/
  fooyin/           ← fooyin source
  fooyin-vim-plugin/← this repo
```

**Step 1 — Build fooyin** (only needed once; only core and gui libraries are required):

```bash
cd /path/to/fooyin
cmake -B build -G Ninja -DBUILD_TRANSLATIONS=OFF
ninja -C build fooyin_core fooyin_gui
```

**Step 2 — Build the plugin**:

```bash
cd /path/to/fooyin-vim-plugin
cmake --preset dev        # configures into ./build using sibling fooyin tree
cmake --build build
```

Output: `build/fyplugin_vimmotions.so`

For a faster local loop, the repository also includes a `Makefile` that wraps the common dev-tree workflow:

```bash
make dev             # configure/build fooyin, build plugin, copy plugin into fooyin build tree
make run             # launch fooyin (set FOOYIN_BIN if auto-detection does not match your layout)
make run-debug       # same, with fy.vim debug logging enabled
```

Useful overrides:

```bash
make dev FOOYIN_SRC_DIR=/path/to/fooyin FOOYIN_BUILD_DIR=/path/to/fooyin/build
make run FOOYIN_BIN=/path/to/fooyin/build/run/fooyin
XDG_CONFIG_HOME=/tmp/fooyin-dev-config make run-debug FOOYIN_BIN=/path/to/fooyin/build/run/fooyin
```

If your fooyin checkout is not a sibling directory, pass the paths explicitly:

```bash
cmake -B build \
  -DFOOYIN_SRC_DIR=/path/to/fooyin \
  -DFOOYIN_BUILD_DIR=/path/to/fooyin/build
cmake --build build
```

A release build:

```bash
cmake --preset dev-release
cmake --build build
```

### Option B — installed fooyin

If fooyin is installed system-wide with headers (`-DINSTALL_HEADERS=ON` at fooyin build time):

```bash
cd /path/to/fooyin-vim-plugin
cmake -B build -G Ninja
cmake --build build
```

`find_package(Fooyin)` will be used automatically and the official `create_fooyin_plugin()` macro handles output naming and install rules.

## Installation

Copy the built `.so` into fooyin's plugin directory. The exact path depends on your system:

```bash
# Typical system install
sudo cp build/fyplugin_vimmotions.so /usr/lib/fooyin/plugins/

# Local user install (adjust path as needed)
cp build/fyplugin_vimmotions.so ~/.local/lib/fooyin/plugins/
```

Or use CMake's install target (sets `FOOYIN_PLUGIN_INSTALL_DIR` automatically):

```bash
cmake --install build --prefix /usr
```

After copying, restart fooyin. The plugin will appear in **Settings → Plugins** and load automatically on next startup.
Rebuild the plugin after upgrading fooyin across a plugin API version change; older plugin binaries are rejected by fooyin.

## License

This repository is licensed under the GNU General Public License v3.0. See `LICENSE` for the full text.

## Debug logging

The plugin uses Qt's categorized logging under the `fy.vim` category, matching fooyin's own logging convention. Three levels are emitted:

| Level | What is logged |
|---|---|
| `Info` | Plugin lifecycle (init, shutdown), mode transitions (Normal / Visual / Insert) |
| `Debug` | Every key event and the action it triggered, count accumulation, navigation from/to rows, ViewLocator cache hits/misses and focus-chain traversal, SpatialNavigator splitter traversal and last-visited updates, all playlist operations (playlist name, row ranges, track counts) |
| `Warning` | Error conditions: no active view, no PlaylistHandler, no active playlist, selection out of range |

### Enabling debug output

**Environment variable** (one-shot):

```bash
QT_LOGGING_RULES="fy.vim.debug=true" fooyin
```

**Persistent** — add to `~/.config/QtProject/qtlogging.ini`:

```ini
[Rules]
fy.vim.debug=true
```

To see only Info and Warning without the per-keystroke noise:

```ini
[Rules]
fy.vim.debug=false
```

Info and Warning are on by default once the `fy.vim` category is mentioned; omitting the ini file entirely shows them as well when Qt's default message handler is active.

### Output format

Qt prints categorized messages to stderr in the form:

```
fy.vim.debug: Normal key: text= "j" qtKey= 74 mods= QFlags<Qt::KeyboardModifier>() pendingKey= QChar(0x0) accumCount= 0
fy.vim.info: Mode → Normal (from 1)
fy.vim.warning: yankRows: no active playlist
```

Pipe through `grep` to focus on a specific subsystem:

```bash
QT_LOGGING_RULES="fy.vim.debug=true" fooyin 2>&1 | grep "fy.vim"
```

## Configurable bindings

The plugin reads and writes vim bindings in fooyin's config file, located using fooyin's public configuration-path helper.
For a normal Linux installation this is typically `~/.config/fooyin/fooyin.conf` (or under `XDG_CONFIG_HOME` when set).
In portable mode, it uses `config/fooyin.conf` beside the executable. Portable bindings are kept separate from those of a
normal installation.

By default, all built-in bindings are active through this config-driven system. Use `UseDefaultBindings` (see below) to start from a clean slate.

You can also manage vim settings and bindings in **Settings -> Plugins -> Vim Motions -> Configure**. Changes made there apply on `Apply` or `OK` without restarting fooyin.

Direct edits to `fooyin.conf` are still only picked up on startup, so restart fooyin after changing the file manually.

### Settings UI

The plugin's settings dialog includes:

- `PendingSequenceTimeout` with an `ms` suffix
- `WrapScan`
- `UseVimMotionsInSettings`
- `UseDefaultBindings`
- an effective bindings tree with `Add`, `Edit`, `Remove`, `Unmap`, `Reset binding`, and `Discard unsaved bindings`

The bindings tree shows the effective state that will be applied, including default rows, custom rows, custom overrides, disabled defaults, and explicit unmaps.

### Search options

Slash-search wraparound is controlled by `WrapScan`, similar to Vim's `wrapscan` option. It defaults to `true`, which preserves the current behavior: `/`, `n`, and `N` wrap to the start or end when needed.

Set it to `false` to stop at the last or first match instead:

```ini
[VimMotions]
WrapScan=false
```

Pending multi-key input timeout is controlled by `PendingSequenceTimeout`. The value is in milliseconds and applies to both two-key bindings like `gg`/`dd` and mark-pending actions like `m` followed by a letter.

If the setting is omitted, it defaults to `0`. A value of `0` disables the timeout and preserves the current behavior.

```ini
[VimMotions]
PendingSequenceTimeout=300
```

### Binding format

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

Supported scopes are `Global`, `PlaylistView`, `PlaylistOrganiser`, `SearchLibraryDialog`, and `PlaylistPicker`.

`SearchLibraryDialog` applies only inside fooyin's `Search Library` dialog. It lets dialog-specific bindings override ordinary playlist-view bindings without leaking into normal playlist tabs. When the Search Library line edit gains focus, the plugin temporarily switches to Insert mode and restores the previous Normal or Visual mode when focus leaves that line edit.

`PlaylistPicker` applies only inside the fuzzy playlist picker, in its local Insert or Normal mode, with the usual Global fallback. Its local mode is independent of the invoking view's mode. See [Fuzzy playlist picker](#fuzzy-playlist-picker) for configuration examples.

**Key combo syntax:**
- Single character: `j`, `k`, `G`, `;`, `'`, `` ` ``
- Special keys: `<Esc>`, `<CR>`, `<Enter>`, `<Tab>`, `<BS>`, `<Space>`, `<Del>`, `<Insert>`, `<Home>`, `<End>`, `<PageUp>`, `<PageDown>`, `<Left>`, `<Right>`, `<Up>`, `<Down>`
- Special punctuation keys that need explicit names in config paths: `<Slash>`, `<Bslash>`, `<Bar>`, `<Lt>`
- Modifier combos: `Ctrl+J`, `Alt+J`, `Meta+J`, `Ctrl+Shift+K`
- Multi-key sequences: `gg`, `dd`, `yy`, `g;`, `g<Space>`, `<Space><Space>`
- Operator-pending prefixes: `m`, `'`, `` ` `` can be bound to mark actions that consume the next letter; lowercase uses local marks and uppercase uses global marks

**Action name + args:**
- No args: `undo`, `enterInsert`, `jumpToFirst`
- With args: `moveCursor:+1`, `spatialMoveFocus:down`, `treeMoveSibling:-1`
- Trigger any fooyin command by ID: `fooyinAction:Playback.Next`, `fooyinAction:View.ShowNowPlaying`
- The arg string is passed to the action handler; each action parses its own args

### Available actions

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
| `copyAfterCurrentPlaying` | — | Normal, Visual | Copy the current row or visual selection to immediately after the currently playing track without jumping there |
| `moveAfterCurrentPlaying` | — | Normal, Visual | Move the current row or visual selection to immediately after the currently playing track without jumping there |
| `undo` | — | Normal | Undo last playlist change |
| `redo` | — | Normal | Redo last undone change |
| `focusNowPlaying` | — | Normal | Focus currently playing track |
| `focusNowPlayingAndExit` | — | Visual | Return to Normal, then focus the currently playing track |
| `nextPlaylist` | — | Normal, Visual | Switch the selected playlist forward by one, or by `[count]` playlists; if a playlist organiser tree is open, follow its grouped tree order |
| `previousPlaylist` | — | Normal, Visual | Switch the selected playlist backward by one, or by `[count]` playlists; if a playlist organiser tree is open, follow its grouped tree order |
| `findPlaylist` | — | Normal, Visual | Open the fuzzy playlist-name picker; `Ctrl+n/p` navigate while input keeps focus; accepting returns to Normal |
| `playlistPickerMove` | signed delta | Picker Insert, Normal | Move the picker highlight by N results, wrapping; Normal counts multiply the delta |
| `playlistPickerAccept` | — | Picker Insert, Normal | Open the highlighted playlist; no match keeps the picker open |
| `playlistPickerCancel` | — | Picker Insert, Normal | Cancel and restore invoking focus |
| `fooyinAction` | fooyin action ID | Normal, Visual, Insert | Trigger any registered fooyin action by ID via `ActionManager` |
| `beginSetMark` | — | Normal, Visual | Begin setting a mark; the next lowercase letter chooses a local mark and the next uppercase letter chooses a global mark |
| `beginJumpToMark` | — | Normal, Visual | Begin jumping to a mark; the next lowercase letter chooses a local mark and the next uppercase letter chooses a global mark |
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

### Runtime changes

Changes made through **Settings -> Plugins -> Vim Motions -> Configure** apply immediately on `Apply` or `OK`.

If you edit `fooyin.conf` directly, those vim-binding changes are still read only on startup, so restart fooyin to pick them up.

### Binding fooyin actions directly

Use `fooyinAction:<Action.Id>` when you want a vim binding to trigger an existing fooyin command without adding a plugin-specific wrapper:

```ini
[VimMotions]
Bindings\Global\Normal\<Space>=fooyinAction:Playback.PlayPause
Bindings\Global\Normal\x=fooyinAction:Playback.Next
Bindings\Global\Normal\z=fooyinAction:View.ShowNowPlaying
```

There is no default binding for `fooyinAction`; it is only available for custom config bindings.

`nextPlaylist` and `previousPlaylist` are also available for custom config bindings only. They do not have default bindings.

You can discover action IDs in **Settings -> Shortcuts**. The command ID is shown alongside each shortcut entry, for example `Playback.Next` or `View.ShowNowPlaying`.

### UseDefaultBindings

By default (`UseDefaultBindings=true`), all built-in bindings are active. You can still override or unmap individual bindings on top of those defaults.

Set `UseDefaultBindings=false` to start from a **clean slate** — no default bindings are loaded. Only bindings you explicitly configure in `fooyin.conf` will take effect:

```ini
[VimMotions]
UseDefaultBindings=false
Bindings\Global\Normal\j=moveCursor:+1
Bindings\Global\Normal\k=moveCursor:-1
Bindings\Global\Normal\gg=jumpToFirst
Bindings\Global\Normal\G=jumpToLast
```

With this config, only `j`, `k`, `gg`, and `G` are active in Normal mode — all other defaults are ignored. See the [Explicit default configuration](#explicit-default-configuration) section for the full list of all default bindings.

If you make the same change through the settings dialog instead, it applies immediately. Manual `fooyin.conf` edits still require a restart (see [Runtime changes](#runtime-changes)).

### UseVimMotionsInSettings

By default (`UseVimMotionsInSettings=false`), vim bindings are ignored while focus is inside fooyin's main **Settings** dialog and any sub-dialog opened from it.

Set `UseVimMotionsInSettings=true` to preserve the old behaviour and keep vim motions active there too:

```ini
[VimMotions]
UseVimMotionsInSettings=true
```

### Unmapping bindings

To remove (unmap) a default binding, set its value to an empty string:

```ini
[VimMotions]
Bindings\Global\Normal\j=
Bindings\Global\Visual\<Esc>=
```

This removes the binding in that scope/mode; a matching Global binding can still apply. Ordinary unhandled input falls through to fooyin. In the playlist picker, Escape/Enter without an effective binding cannot trigger implicit dialog actions, and unmapped Normal-mode command keys are consumed.

### Explicit default configuration

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

## Notes

- Default bindings are scoped where actions are view-specific. For example, mark, yank, paste, and playlist undo/redo defaults are playlist-view-only, while organiser creation and sibling-move defaults are organiser-only.
- Yank/delete/paste (`dd`, `yy`, `p`, `P`) are not all scoped identically: `yy` is available in playlist views and `Search Library`, `p` and `P` remain playlist-view-only, while `dd` remains active in both playlist view and playlist organiser because it deletes playlist rows in the former and routes to organiser remove in the latter.
- Bindings are stored under explicit scopes: `Bindings\<Scope>\<Mode>\<Keys>`. `Global` applies unless a narrower scope, such as `PlaylistView`, `PlaylistOrganiser`, `SearchLibraryDialog`, or `PlaylistPicker`, defines the same key sequence for that mode.
- In `Search Library`, the search field temporarily puts vim into Insert mode while it has focus. `Ctrl+j` can move focus from the search field to results and `Ctrl+k` can move focus back to the search field when those bindings resolve to `spatialMoveFocus:down` / `spatialMoveFocus:up`.
- In `Search Library`, `yy` and visual `y` yank detached result tracks into the plugin clipboard so they can be pasted into a regular playlist later with `p` or `P`.
- In `Search Library`, `copyAfterCurrentPlaying` copies the detached result selection into the currently playing playlist. `moveAfterCurrentPlaying` is unsupported there and intentionally does nothing except log a warning.
- `gg`, `dd`, and `yy` are two-keystroke sequences handled internally by the key parser; they do not appear as individual entries in Settings → Shortcuts.
- Spatial focus (`Ctrl+j/k/h/l`) follows fooyin's `QSplitter` layout tree and remembers the last-focused pane per splitter, so returning to a split lands on the same widget you left.
- From Visual mode, spatial focus exits to Normal on the newly focused pane but preserves the original selection in the old pane.
