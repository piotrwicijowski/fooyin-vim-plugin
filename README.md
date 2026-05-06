# fooyin-vim-plugin

Vim-style keybindings for [fooyin](https://github.com/ludouzi/fooyin). Adds Normal, Visual, and Insert modes to every list view in fooyin (playlist, library browser, file browser, queue, etc.).

## Features

### Modes

| Mode | How to enter | What it does |
|---|---|---|
| Normal | Start-up default / `Esc` from Insert | All vim keys active; fooyin shortcuts suppressed |
| Visual | `v` | Linewise range selection |
| Insert | `i` | Vim off; all keys pass through to fooyin normally |

### Normal mode bindings

| Key | Action |
|---|---|
| `j` / `k` | Move cursor down / up |
| `[count]j` / `[count]k` | Move cursor N rows |
| `gg` | Jump to first row |
| `G` / `[count]G` | Jump to last row / row N (1-indexed) |
| `Ctrl+d` | Half page down |
| `Ctrl+u` | Half page up |
| `Enter` | Activate / play item under cursor |
| `v` | Enter Visual mode |
| `dd` / `[count]dd` | Delete row(s) from playlist |
| `yy` / `[count]yy` | Yank row(s) into vim clipboard |
| `p` / `P` | Paste yanked rows after / before cursor |
| `i` | Enter Insert mode |
| `Ctrl+j/k/h/l` | Move focus to the view below / above / left / right |

### Visual mode bindings

| Key | Action |
|---|---|
| `j` / `k` | Extend selection down / up |
| `o` | Toggle anchor — swap cursor between start and end |
| `d` | Delete selected range, return to Normal |
| `y` | Yank selected range, return to Normal |
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

## Notes

- Yank/delete/paste (`dd`, `yy`, `p`, `P`) operate on playlist views only. They read and write tracks via fooyin's `PlaylistHandler`. Other views (library browser, file browser) are read-only for these operations but support all cursor-navigation and spatial-focus bindings.
- `gg`, `dd`, and `yy` are two-keystroke sequences handled internally by the key parser; they do not appear as individual entries in Settings → Shortcuts.
- Spatial focus (`Ctrl+j/k/h/l`) follows fooyin's `QSplitter` layout tree and remembers the last-focused pane per splitter, so returning to a split lands on the same widget you left.
