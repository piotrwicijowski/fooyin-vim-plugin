# Building and installing Vim Motions

[Back to the README](../README.md)

This guide covers building the plugin from source and installing the resulting library. For everyday use and configuration through the settings dialog, see the [README](../README.md).

## Requirements

- A compatible fooyin installation with development headers (`-DINSTALL_HEADERS=ON` at fooyin build time), or a fooyin source/build tree (see below)
- Qt development packages (Widgets and Sql modules), compatible with the Qt build used by fooyin. Current upstream fooyin requires Qt 6.4+ on Linux and 6.8+ on Windows; building fooyin itself also requires its additional dependencies.
- CMake 3.21+ for the included presets (schema version 3), or 3.19+ when configuring without presets
- A C++23 compiler (GCC 12+, Clang 15+)
- Ninja for commands using `-G Ninja` or `ninja`; Make for the optional `make` workflow

### fooyin compatibility

The plugin requires fooyin's plugin API `/2` and public APIs including the following upstream changes. Use a fooyin revision containing all three commits:

| Upstream change | Commit |
|---|---|
| Plugin API versioning (`/2`) | [`06c2b7e1d`](https://github.com/fooyin/fooyin/commit/06c2b7e1d) |
| Shared `ScriptRegistry` and playlist-entry evaluation contexts | [`29c0d84cf`](https://github.com/fooyin/fooyin/commit/29c0d84cf) |
| `PlaylistInteractor` playlist-row refresh API | [`67f2735a2`](https://github.com/fooyin/fooyin/commit/67f2735a2) |

Plugin API `/2` alone is not sufficient if the later scripting and row-refresh APIs are missing.

## Building

### Option A — installed fooyin

Use this option to build against your installed fooyin without downloading its source. The installation must include development headers (`-DINSTALL_HEADERS=ON` at fooyin build time).

```bash
cd /path/to/fooyin-vim-plugin
cmake -B build -G Ninja -DFOOYIN_FORCE_DEV_TREE=OFF
cmake --build build
```

`FOOYIN_FORCE_DEV_TREE` defaults to `ON`; setting it to `OFF` enables `find_package(Fooyin)`. When the installed package is found, the official `create_fooyin_plugin()` macro handles output naming and install rules. For a non-standard install prefix, also pass `-DCMAKE_PREFIX_PATH=/path/to/prefix`. If the package is not found, configuration falls back to the dev-tree workflow.

### Option B — dev tree (sibling checkout, no install needed)

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

Launch overrides:

```bash
make run FOOYIN_BIN=/path/to/fooyin/build/run/fooyin
XDG_CONFIG_HOME=/tmp/fooyin-dev-config make run-debug FOOYIN_BIN=/path/to/fooyin/build/run/fooyin
```

The Makefile's `FOOYIN_SRC_DIR` and `FOOYIN_BUILD_DIR` overrides affect its fooyin build, plugin-copy, and launch steps, but are not forwarded to plugin configuration. `make dev` configures the plugin through the `dev` preset, which uses the sibling paths shown above.

For a non-sibling checkout or a different fooyin build directory, first build fooyin at your chosen paths, then configure the plugin explicitly from this repository:

```bash
cmake -S . -B build -G Ninja \
  -DFOOYIN_FORCE_DEV_TREE=ON \
  -DFOOYIN_SRC_DIR=/path/to/fooyin \
  -DFOOYIN_BUILD_DIR=/path/to/fooyin/build
cmake --build build
make install-plugin FOOYIN_BUILD_DIR=/path/to/fooyin/build
```

For a release build with custom paths, add `-DCMAKE_BUILD_TYPE=RelWithDebInfo` to that configure command. For the sibling layout, use the release preset:

```bash
cmake --preset dev-release
cmake --build build
```

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

For diagnostic output during development, see [Debug logging](DEBUGGING.md).
