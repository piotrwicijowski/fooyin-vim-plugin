# fooyin-vim-plugin

Vim-style keybindings for [fooyin](https://github.com/ludouzi/fooyin). Adds Normal, Visual, Insert, Filter, and Search modes to every list view in fooyin (playlist, library browser, file browser, queue, etc.).

## Features

### Modes

| Mode | How to enter | What it does |
|---|---|---|---|
| Normal | Start-up default / `Esc` from Insert, Search, Filter | All vim keys active; fooyin shortcuts suppressed |
| Visual | `v` | Linewise range selection |
| Insert | `i` | Vim off; all keys pass through to fooyin normally |
| Filter | `Ctrl+i` | Live incremental filter — hides non-matching rows |
| Search | `/` | Live search — all rows visible, cursor jumps to matches; wraps by default |

### Normal mode bindings

| Key | Action |
|---|---|
| `j` / `k` | Move cursor down / up |
| `[count]j` / `[count]k` | Move cursor N rows |
| `gg` | Jump to first row |
| `G` / `[count]G` | Jump to last row / row N (1-indexed) |
| `o` | Focus currently playing item |
| `g;` | Focus currently playing item |
| `m` + `a-z` | Set a local mark on the current playlist item |
| `'` + `a-z` | Jump to a local mark in the current playlist |
| `` ` `` + `a-z` | Jump to a local mark in the current playlist |
| `Ctrl+d` | Half page down |
| `Ctrl+u` | Half page up |
| `v` | Enter Visual mode |
| `dd` / `[count]dd` | Delete row(s) from playlist |
| `yy` / `[count]yy` | Yank row(s) into vim clipboard |
| `p` / `P` | Paste yanked rows after / before cursor |
| `u` | Undo last playlist change (dd, p, P, Alt+j/k) |
| `Ctrl+r` | Redo last undone change |
| `i` | Enter Insert mode |
| `/` | Enter Search mode |
| `n` / `N` | Next / previous search match (+/-1 row if filter active) |
| `Ctrl+i` | Enter Filter mode |
| `h` | Collapse node / ascend to parent (tree views) |
| `l` | Expand node / descend to first child (tree views) |
| `Ctrl+Shift+J` | Next sibling at same level (tree views) |
| `Ctrl+Shift+K` | Previous sibling at same level (tree views) |
| `Ctrl+j/k/h/l` | Move focus to the view below / above / left / right |
| `Alt+j` / `Alt+k` | Move current row down / up in the playlist |
| `[count]Alt+j` / `[count]Alt+k` | Move current row N positions down / up |

### Visual mode bindings

| Key | Action |
|---|---|
| `j` / `k` | Extend selection down / up |
| `[count]j` / `[count]k` | Extend selection N rows |
| `gg` | Extend selection to first row |
| `G` / `[count]G` | Extend selection to last row / row N (1-indexed) |
| `Ctrl+d` | Extend selection half page down |
| `Ctrl+u` | Extend selection half page up |
| `o` | Toggle anchor — swap cursor between start and end |
| `h` / `l` | Collapse/expand node (tree views) |
| `d` | Delete selected range, return to Normal |
| `y` | Yank selected range, return to Normal |
| `n` / `N` | Next / previous search match (return to Normal) |
| `/` | Enter Search mode (return to Normal) |
| `g;` | Focus currently playing item (return to Normal) |
| `Alt+j` / `Alt+k` | Move entire selection down / up in the playlist |
| `[count]Alt+j` / `[count]Alt+k` | Move entire selection N positions down / up |
| `Esc` | Cancel selection, return to Normal |

All actions are registered in **Settings → Shortcuts** under the "Vim Motions" category and can be rebound there.

## Requirements

- fooyin (source tree or installed with `-DINSTALL_HEADERS=ON`)
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

## Configurable bindings (experimental)

The plugin supports user-defined key bindings stored in fooyin's config file (`~/.config/fooyin/fooyin.conf`). This feature is **off by default** — the hardcoded bindings above are used unless you opt in.

### Enabling

Add (or change) these lines in `fooyin.conf`:

```ini
[VimMotions]
UseConfigBindings=true
```

When `true`, the plugin reads all keys under the `VimMotions/Bindings` group and dispatches key events through the config-driven system instead of the hardcoded handlers.

By default, all built-in bindings are active when `UseConfigBindings=true`. Use `UseDefaultBindings` (see below) to start from a clean slate.

After changing `fooyin.conf`, restart fooyin before expecting vim-binding changes to apply.

### Search options

Slash-search wraparound is controlled by `WrapScan`, similar to Vim's `wrapscan` option. It defaults to `true`, which preserves the current behavior: `/`, `n`, and `N` wrap to the start or end when needed.

Set it to `false` to stop at the last or first match instead:

```ini
[VimMotions]
WrapScan=false
```

### Binding format

Each binding is a single INI entry:

```ini
[VimMotions]
Bindings\Normal\j=moveCursor:+1
Bindings\Normal\k=moveCursor:-1
Bindings\Normal\gg=jumpToFirst
Bindings\Normal\G=jumpToLast
Bindings\Normal\dd=deleteRows
Bindings\Visual\j=extendCursor:+1
Bindings\Insert\Escape=leaveInsertMode
```

The key path is `Bindings\{Mode}\{KeyCombo}` and the value is `ActionName[:args]`.

**Key combo syntax:**
- Single character: `j`, `k`, `G`, `/` (use `slash` for the `/` key to avoid INI group separator issues)
- Encoded punctuation keys: `slash`, `semicolon`, `apostrophe`, `backtick`
- Named keys: `Escape`, `Return`, `Tab`, `Space`, `Home`, `End`, `PageUp`, `PageDown`, `Left`, `Right`, `Up`, `Down`
- Modifier combos: `Ctrl+J`, `Alt+J`, `Ctrl+Shift+K`
- Two-key sequences: `gg`, `dd`, `yy`, `g;`
- Operator-pending prefixes: `m`, `apostrophe`, `backtick` can be bound to mark actions that consume the next lowercase letter

**Action name + args:**
- No args: `undo`, `enterInsert`, `jumpToFirst`
- With args: `moveCursor:+1`, `spatialMoveFocus:down`, `treeMoveSibling:-1`
- The arg string is passed to the action handler; each action parses its own args

### Available actions

| Action | Args | Modes | Description |
|---|---|---|---|
| `moveCursor` | `+1` / `-1` | Normal | Move cursor by N rows |
| `jumpToFirst` | — | Normal | Jump to first row |
| `jumpToLast` | — | Normal | Jump to last row |
| `jumpToRow` | `N` | Normal | Jump to row N (0-indexed) |
| `moveCursorHalfPage` | `+1` / `-1` | Normal | Move cursor by half page |
| `activateCurrentRow` | — | Normal | Activate (Enter) current row |
| `treeMoveSibling` | `+1` / `-1` | Normal | Move to next/prev tree sibling |
| `treeOpenOrDescend` | — | Normal, Visual | Expand node or descend to first child |
| `treeCloseOrAscend` | — | Normal, Visual | Collapse node or ascend to parent |
| `enterInsert` | — | Normal | Switch to Insert mode |
| `enterVisual` | — | Normal | Switch to Visual mode |
| `enterNormal` | — | Visual, Insert | Return to Normal mode |
| `enterFilter` | — | Normal | Open incremental filter bar |
| `enterSearch` | — | Normal | Open `/` search bar |
| `nextMatch` | — | Normal | Jump to next search match |
| `prevMatch` | — | Normal | Jump to previous search match |
| `deleteRows` | — | Normal | Delete N rows from playlist |
| `yankRows` | — | Normal | Yank N rows into vim clipboard |
| `pasteAfter` | — | Normal | Paste after cursor |
| `pasteBefore` | — | Normal | Paste before cursor |
| `undo` | — | Normal | Undo last playlist change |
| `redo` | — | Normal | Redo last undone change |
| `focusNowPlaying` | — | Normal | Focus currently playing track |
| `beginSetMark` | — | Normal | Begin setting a local mark; the next lowercase letter chooses the mark |
| `beginJumpToMark` | — | Normal | Begin jumping to a local mark; the next lowercase letter chooses the mark |
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

### Runtime changes

Bindings are read on startup. If you edit `fooyin.conf`, restart fooyin for those vim-binding changes to apply.

### UseDefaultBindings

By default (`UseDefaultBindings=true`), all built-in bindings are active when `UseConfigBindings=true`. You can still override or unmap individual bindings on top of those defaults.

Set `UseDefaultBindings=false` to start from a **clean slate** — no default bindings are loaded. Only bindings you explicitly configure in `fooyin.conf` will take effect:

```ini
[VimMotions]
UseConfigBindings=true
UseDefaultBindings=false
Bindings\Normal\j=moveCursor:+1
Bindings\Normal\k=moveCursor:-1
Bindings\Normal\gg=jumpToFirst
Bindings\Normal\G=jumpToLast
```

With this config, only `j`, `k`, `gg`, and `G` are active in Normal mode — all other defaults are ignored. See the [Explicit default configuration](#explicit-default-configuration) section for the full list of all default bindings.

After updating `fooyin.conf`, restart fooyin (see [Runtime changes](#runtime-changes)).

### Unmapping bindings

To remove (unmap) a default binding, set its value to an empty string:

```ini
[VimMotions]
Bindings\Normal\j=
Bindings\Visual\Escape=
```

This prevents the key from triggering any action in that mode. The key will fall through to fooyin's normal shortcut handling.

### Explicit default configuration

To opt into only the bindings you need, set `UseDefaultBindings=false` and copy the desired lines below into your config. The full set of default bindings is:

```ini
[VimMotions]
UseConfigBindings=true
UseDefaultBindings=false

; -- Normal mode --
Bindings\Normal\j=moveCursor:+1
Bindings\Normal\k=moveCursor:-1
Bindings\Normal\gg=jumpToFirst
Bindings\Normal\G=jumpToLast
Bindings\Normal\dd=deleteRows
Bindings\Normal\yy=yankRows
Bindings\Normal\i=enterInsert
Bindings\Normal\v=enterVisual
Bindings\Normal\u=undo
Bindings\Normal\Ctrl+R=redo
Bindings\Normal\h=treeCloseOrAscend
Bindings\Normal\l=treeOpenOrDescend
Bindings\Normal\o=focusNowPlaying
Bindings\Normal\g;=focusNowPlaying
Bindings\Normal\m=beginSetMark
Bindings\Normal\apostrophe=beginJumpToMark
Bindings\Normal\backtick=beginJumpToMark
Bindings\Normal\p=pasteAfter
Bindings\Normal\P=pasteBefore
Bindings\Normal\slash=enterSearch
Bindings\Normal\n=nextMatch
Bindings\Normal\N=prevMatch
Bindings\Normal\Escape=clearPending
Bindings\Normal\Ctrl+J=spatialMoveFocus:down
Bindings\Normal\Ctrl+K=spatialMoveFocus:up
Bindings\Normal\Ctrl+H=spatialMoveFocus:left
Bindings\Normal\Ctrl+L=spatialMoveFocus:right
Bindings\Normal\Ctrl+D=moveCursorHalfPage:+1
Bindings\Normal\Ctrl+U=moveCursorHalfPage:-1
Bindings\Normal\Ctrl+I=enterFilter
Bindings\Normal\Ctrl+Shift+J=treeMoveSibling:+1
Bindings\Normal\Ctrl+Shift+K=treeMoveSibling:-1
Bindings\Normal\Alt+J=moveRows:+1
Bindings\Normal\Alt+K=moveRows:-1

; -- Visual mode --
Bindings\Visual\j=extendCursor:+1
Bindings\Visual\k=extendCursor:-1
Bindings\Visual\gg=extendToFirst
Bindings\Visual\G=extendToEnd
Bindings\Visual\o=swapAnchor
Bindings\Visual\d=deleteSelection
Bindings\Visual\y=yankSelection
Bindings\Visual\Escape=leaveVisualMode
Bindings\Visual\n=nextMatchAndExit
Bindings\Visual\N=prevMatchAndExit
Bindings\Visual\slash=enterSearchAndExit
Bindings\Visual\Ctrl+D=extendHalfPage:+1
Bindings\Visual\Ctrl+U=extendHalfPage:-1
Bindings\Visual\Ctrl+J=spatialMoveFocus:down
Bindings\Visual\Ctrl+K=spatialMoveFocus:up
Bindings\Visual\Ctrl+H=spatialMoveFocus:left
Bindings\Visual\Ctrl+L=spatialMoveFocus:right
Bindings\Visual\Alt+J=moveVisualSelection:+1
Bindings\Visual\Alt+K=moveVisualSelection:-1
Bindings\Visual\h=treeCloseOrAscend
Bindings\Visual\l=treeOpenOrDescend
Bindings\Visual\g;=focusNowPlayingAndExit

; -- Insert mode --
Bindings\Insert\Escape=leaveInsertMode
```

When adding new default configurable bindings to the plugin code, the entries above must also be added to this section to keep documentation in sync.

## Notes

- Yank/delete/paste (`dd`, `yy`, `p`, `P`) operate on playlist views only. They read and write tracks via fooyin's `PlaylistHandler`. Other views (library browser, file browser) are read-only for these operations but support all cursor-navigation and spatial-focus bindings.
- `gg`, `dd`, and `yy` are two-keystroke sequences handled internally by the key parser; they do not appear as individual entries in Settings → Shortcuts.
- Spatial focus (`Ctrl+j/k/h/l`) follows fooyin's `QSplitter` layout tree and remembers the last-focused pane per splitter, so returning to a split lands on the same widget you left.
- From Visual mode, spatial focus exits to Normal on the newly focused pane but preserves the original selection in the old pane.
