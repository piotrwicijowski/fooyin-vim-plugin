# fooyin-vim-plugin

Vim-style keyboard navigation for [fooyin](https://github.com/ludouzi/fooyin). Move through playlists, select and rearrange tracks, search your library, and switch between panes without reaching for the mouse. Customize the keys and behavior through fooyin's settings dialog.

- [Installation](#installation)
- [Getting started](#getting-started)
- [Customizing Vim Motions](#customizing-vim-motions)
- [Features](#features)
- [Key reference](#key-reference)
- [Further documentation](#further-documentation)

## Installation

1. Download the Linux x86-64 **`.zip`** for **Ubuntu 24.04**, **Ubuntu 26.04**, or
   **Arch Linux** from the [release assets](https://github.com/piotrwicijowski/fooyin-vim-plugin/releases).
   Choose the build matching your distribution and fooyin version. Arch builds
   target a fully updated system at build time.
2. In fooyin, open **Settings → Plugins → Install** and select the downloaded ZIP
   directly, **without extracting it**.
3. Restart fooyin when prompted. Select **Vim Motions** in **Settings → Plugins**
   and click **Configure** to customize its settings and bindings.

If you previously copied `fyplugin_vimmotions.so` manually, close fooyin and
remove that loose copy before installing the ZIP to avoid loading two versions.
The release includes a `.zip.sha256` checksum; `build-metadata.json` and
`dependencies.txt` inside the ZIP record the build versions and required libraries.
For installation from source, see the [build and installation guide](docs/BUILDING.md).

## Getting started

1. Focus a playlist. The plugin starts in **Normal** mode: use `j` and `k` to move down and up.
2. Press `/`, type a search, and press `Enter` to keep the result. Use `n` and `N` to jump between matches.
3. Press `v` to select a range, then extend it with `j` or `k`. Press `Esc` to return to Normal.
4. Press `i` when you want ordinary fooyin input, and `Esc` to return to Vim navigation.
5. Press `Space` then `p` to find another playlist by name.

To see the current mode, enable **Layout Editing Mode** in fooyin and add the **Vim Mode Indicator** widget from the **Vim Motions** submenu.

### Modes

| Mode | How to enter | What it does |
|---|---|---|
| Normal | Start-up default / `Esc` from other modes | Configured Vim bindings take precedence; unhandled keys and shortcuts pass through to fooyin |
| Visual | `v` | Select a range of rows |
| Insert | `i` | Ordinary input passes through to fooyin; configured Insert bindings remain active |
| Filter | `gi` | Hide non-matching rows as you type |
| Search | `/` | Jump to matches as you type without hiding rows; wraps by default |

## Customizing Vim Motions

Open **Settings → Plugins → Vim Motions → Configure** to change settings and bindings. Click **Apply** or **OK** to use your changes immediately.

### Changing keys

The **Effective bindings** list shows the defaults, your custom bindings, and any disabled or overridden bindings.

- Select a custom binding and click **Edit** to change its keys or action.
- Click **Add** to create a binding. Choose where it applies (**Scope**), its **Mode**, **Keys**, **Action**, and any **Arguments**. The **Help** buttons explain key notation and available actions.
- Use **Unmap** to disable a default binding, **Remove** to remove a custom entry, or **Reset Binding** to clear a customization and restore the default where one exists.
- Use **Discard Binding Changes** to discard unsaved binding edits.

For example, to give the playlist picker another shortcut, click **Add**, choose **Global** and **Normal**, enter `Ctrl+P` in **Keys** and `findPlaylist` in **Action**, and leave **Arguments** empty. Add a Visual binding too if you want the same keys while selecting tracks. To replace the original shortcut, select its `Space`-then-`p` default and click **Unmap**.

To assign a different action to an existing default key, use **Add** with the same scope, mode, and keys to create an override.

**Scope** means where the binding applies. **Global** bindings are shared across views; a more specific scope, such as **Playlist View** or **Playlist Picker**, takes precedence for the same key sequence.

### Behavior settings

| Setting | What it changes | Default |
|---|---|---|
| Wrap scan | Continue searching from the other end after reaching the first or last match | On |
| Pending sequence timeout | How long to wait between keys in commands such as `gg`, `dd`, or a mark command, in milliseconds; `0` means no timeout | `0` |
| Use vim motions in settings | Keep Vim bindings active inside fooyin's Settings dialog and its sub-dialogs | Off |
| Use default bindings | Start with the built-in keys; turn off to use only the bindings you add yourself | On |

The [action and key-syntax reference](docs/CONFIGURATION.md#available-actions) covers additional commands you can assign through the binding editor, including playlist switching and existing fooyin playback commands.

## Features

### Search and filter

Use `/` to search without hiding rows, or `gi` to narrow the view to matching rows.

| Mode | `Enter` | `Esc` |
|---|---|---|
| Search | Keep the current result and return to Normal | Cancel and restore the cursor's position before the search |
| Filter | Keep the filter active and return to Normal | Clear the filter and return to Normal |

After accepting a search, use `n` / `N` for the next / previous match. Slash search matches case-insensitive text across row columns. To clear an accepted filter, reopen it with `gi` and press `Esc`.

### Fuzzy playlist picker

Press `Space` then `p` in Normal or Visual mode to find a playlist by name:

1. Type a case-insensitive, fuzzy query, for example `rckcl` for **Rock Classics**.
2. Use `Ctrl+n` / `Ctrl+p` or the arrow keys to highlight the next / previous result while keeping focus in the input field.
3. Press `Enter` to open the highlighted playlist and focus its track view, or `Esc` twice to cancel (first switches from Insert to Normal, next one closes the dialog).

Use the **Playlist Picker** scope in the binding editor to customize the key bindings, including whether one Escape should cancel directly from Insert.

### Track-position jump history

`Ctrl+o` goes to an older playlist track position; `Ctrl+i` goes forward again. Both accept a count (for example, press `3` then `Ctrl+o`) and work from Normal or Visual mode. Traversal restores a single cursor selection in Normal mode.

- History is shared across playlists and kept only for the current session, up to 100 positions.
- The jump history navigates between tracks and not absolute positions in the playlist, so the jump history survives reordering and modifying the playlist.
- Search previews while typing do not create entries, only accepting a search does.
- Revisited track positions are deduplicated.
- Deleted or hidden (via filtering) tracks are skipped.
- Deleted playlists are skipped. Collapsed playlist groups are expanded to reveal a destination track.

### Marks in playlist columns

The plugin exposes `%mark%` and `%global_mark%` for playlist scripts. They appear under **Vim Motions** in the Script Editor reference, so you can show a track's mark in a playlist column.

- `%mark%` returns the local mark letter for the playlist entry; `%global_mark%` returns its global mark letter.
- Both return empty for unmarked entries or when there is no specific playlist entry to evaluate.
- If multiple marks of the same kind point at the same entry, the lowest-letter mark is shown.

### Working across views

- Navigation works across playlist, library, file browser, and queue views.
- Editing commands depend on the view: playlist yank, paste, marks, and undo/redo target playlists, while organiser creation commands target the playlist organiser.
- In **Search Library**, the search field temporarily puts Vim into Insert mode. Use `Ctrl+j` to move from the search field to results and `Ctrl+k` to return, with the default focus-navigation bindings. Leaving the field restores the previous Normal or Visual mode.
- In **Search Library**, `yy` and Visual `y` copy result tracks into the plugin clipboard. Paste them into a regular playlist with `p` or `P`.
- `dd` removes playlist rows in a playlist view, or the current item in the playlist organiser.
- Spatial focus (`Ctrl+j/k/h/l`) remembers the last-focused pane in each split. From Visual mode, moving to another pane returns to Normal but keeps the old pane's selection.

## Key reference

These are the default bindings. A count is a number typed before a command: `5j` moves down five rows, and `12G` jumps to row 12. **Global** means a shared binding; view-specific commands still need an appropriate view.

### Normal mode bindings

| Key | Where | Action |
|---|---|---|
| `j` / `k` | Global | Move cursor down / up |
| `[count]j` / `[count]k` | Global | Move cursor N rows |
| `gg` | Global | Jump to first row |
| `G` / `[count]G` | Global | Jump to last row / row N (1-indexed) |
| `o` / `g;` | Global | Focus currently playing item |
| `m` + `a-z` | Playlist view | Set a local mark on the current playlist item |
| `m` + `A-Z` | Playlist view | Set a global mark on the current playlist item |
| `'` + `a-z` | Playlist view | Jump to a local mark in the current playlist |
| `'` + `A-Z` | Playlist view | Jump to a global mark, switching playlists if needed |
| `` ` `` + `a-z` | Playlist view | Jump to a local mark in the current playlist |
| `` ` `` + `A-Z` | Playlist view | Jump to a global mark, switching playlists if needed |
| `Ctrl+d` / `Ctrl+u` | Global | Half page down / up |
| `v` | Global | Enter Visual mode |
| `dd` / `[count]dd` | Global | Delete row(s), or remove the current item in the playlist organiser |
| `yy` / `[count]yy` | Playlist view, Search Library | Yank (copy) row(s) into the plugin clipboard |
| `p` / `P` | Playlist view | Paste yanked rows after / before cursor |
| `a` / `A` | Playlist organiser | Create a new playlist / group |
| `u` / `Ctrl+r` | Playlist view | Undo / redo a playlist change (`dd`, `p`, `P`, `Alt+j/k`) |
| `i` | Global | Enter Insert mode |
| `/` | Global | Enter Search mode |
| `n` / `N` | Global | Next / previous search match (+/-1 row if filter active) |
| `gi` | Global | Enter Filter mode |
| `Space` then `p` | Global | Open fuzzy playlist picker |
| `[count]Ctrl+o` / `[count]Ctrl+i` | Global | Older / newer playlist track position |
| `h` / `l` | Global | Collapse / ascend or expand / descend in tree views |
| `Ctrl+Shift+J` / `Ctrl+Shift+K` | Playlist organiser | Next / previous sibling at the same tree level |
| `Ctrl+j/k/h/l` | Global | Move focus to the view below / above / left / right |
| `Alt+j` / `Alt+k` | Global | Move current row down / up in the playlist; accepts a count |
| `Esc` | Global | Clear a pending key sequence or count |

### Visual mode bindings

| Key | Where | Action |
|---|---|---|
| `j` / `k` | Global | Extend selection down / up; accepts a count |
| `gg` | Global | Extend selection to first row |
| `G` / `[count]G` | Global | Extend selection to last row / row N (1-indexed) |
| `Ctrl+d` / `Ctrl+u` | Global | Extend selection half a page down / up |
| `[count]Ctrl+o` / `[count]Ctrl+i` | Global | Older / newer playlist track position, return to Normal |
| `o` | Global | Toggle anchor; swap cursor between start and end |
| `v` / `Esc` | Global | Exit Visual mode |
| `h` / `l` | Global | Collapse / expand node in tree views |
| `d` | Playlist view | Delete selected range, return to Normal |
| `y` | Playlist view, Search Library | Yank selected range, return to Normal |
| `n` / `N` | Global | Next / previous search match, then return to Normal |
| `/` | Global | Leave Visual mode and enter Search mode |
| `m` + `a-z` / `A-Z` | Playlist view | Set a local / global mark on the current playlist item |
| `'` + `a-z` | Playlist view | Jump to a local mark, extending the selection |
| `'` + `A-Z` | Playlist view | Jump to a global mark, switching playlists if needed |
| `` ` `` + `a-z` | Playlist view | Jump to a local mark, extending the selection |
| `` ` `` + `A-Z` | Playlist view | Jump to a global mark, switching playlists if needed |
| `g;` | Global | Focus currently playing item, then return to Normal |
| `Space` then `p` | Global | Open fuzzy playlist picker; cancel preserves the selection |
| `Ctrl+j/k/h/l` | Global | Move focus between panes, return to Normal, and preserve the old selection |
| `Alt+j` / `Alt+k` | Playlist view | Move entire selection down / up in the playlist; accepts a count |

## Further documentation

- [Building and installing](docs/BUILDING.md) — dependencies, fooyin compatibility, source builds, and installation paths.
- [Manual configuration and action reference](docs/CONFIGURATION.md) — configuration-file examples, action names, key syntax, and the complete default configuration.
- [Debug logging](docs/DEBUGGING.md) — diagnostic output and logging settings.
