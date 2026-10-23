# Building and installing Vim Motions

[Back to the README](../README.md)

For release ZIP installation through fooyin's settings dialog, follow the
[README installation steps](../README.md#installation). This guide covers source
builds, manual installation, and the release pipeline.

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

For downloaded release ZIPs, use **Settings → Plugins → Install** as described in
the [README](../README.md#installation). The commands below apply to source builds.

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

## GitHub version-tag releases

`.github/workflows/build-tag.yml` builds Linux x86-64 releases for:

| Distribution | Build environment | Archive identifier |
| --- | --- | --- |
| Ubuntu 24.04 | Native `ubuntu-24.04` runner | `ubuntu24.04` |
| Ubuntu 26.04 | Native `ubuntu-26.04` runner | `ubuntu26.04` |
| Arch Linux | Official `archlinux:base-devel` container on `ubuntu-24.04` | `archlinux` |

Each target uses its distribution's default GCC toolchain and library packages.
Arch performs a full `pacman -Syu` update before installing build dependencies.
The workflow pins fooyin v0.14.0 to commit
`4cb697bd19cfde409afca420b84d15bccc1ffb1e` for every target.

Each matrix job validates the tag, installs dependencies, builds fooyin's shared
Core/Gui/Utils libraries, and builds the plugin and all tests in Release mode.
It runs CTest offscreen, stages the plugin, checks shared-library dependencies and
install library paths, then verifies embedded installer metadata and loads the
ZIP payload using `test_pluginload`. These checks use public fooyin and Qt APIs.
Each build has a 90-minute timeout. Matrix failures do not cancel the other builds.

A separate publishing job waits for all targets to pass and publishes their
archives together, avoiding parallel release-creation races. Release files and
distribution-specific failure diagnostics are retained as workflow artifacts for
14 days.

### ZIP bundle format

The ZIP contains these paths directly, without an outer release-name directory:

```text
linux-x64/fyplugin_vimmotions.so
LICENSE
INSTALL.txt
build-metadata.json
dependencies.txt
elf-dynamic.txt
```

fooyin selects `linux-x64/` and strips that prefix when installing. An outer
archive-name directory or the CMake install-prefix layout `lib/fooyin/plugins/`
is rejected. Root-level documentation is allowed. The bundle uses standard
Deflate ZIP compression and regular files.

The installer reads the Qt metadata embedded in the binary; no separate manifest
is needed. It requires a compatible plugin IID, a nonempty version, and an
identifier derived from `Author.Name` that matches `^[a-z0-9][a-z0-9._-]*$` after
lowercasing and removing spaces. Vim Motions supplies its author and uses
`PROJECT_VERSION` for the embedded version. The module-load test checks these
requirements before loading the plugin.

### Publish a release

1. Set `project(FooyinVimMotions VERSION ...)` in `CMakeLists.txt` to the intended
   numeric version. The plugin metadata is generated from this version.
2. Commit and push the release changes, including the workflow and its helpers.
3. Validate, create, and push a version tag. For the current version:

   ```bash
   python3 .github/scripts/release.py validate --source . --ref refs/tags/v0.1.0
   git tag -a v0.1.0 -m "Vim Motions 0.1.0"
   git push origin v0.1.0
   ```

Tags use `vMAJOR.MINOR.PATCH`, with optional SemVer prerelease/build suffixes.
The short form `vMAJOR.MINOR` is also accepted and means patch zero, so `v0.1`
matches CMake version `0.1.0`. Prerelease tags such as `v0.1.0-rc.1` create GitHub
prereleases. Invalid tags and CMake version mismatches fail before building fooyin.

Successful runs create a GitHub Release with one distribution-labelled `.zip`
and `.zip.sha256` checksum per target. Archives contain the plugin, installation
instructions, license, build metadata (both commit SHAs, UTC build time, and
dependency versions), and ELF dependency/library-path reports. Choose the ZIP
for your distribution and matching fooyin version, then install it directly
through **Settings → Plugins → Install** without extracting it. For manual
installation, the binary is under `linux-x64/` inside the ZIP.

Publishing uses the workflow's `GITHUB_TOKEN` with `contents: write` permission.
Reruns replace assets with matching filenames on an existing release. Available
failure diagnostics are retained for 14 days.

### Rerun a tagged build

Use **Actions → Build version tag → Re-run jobs**, or dispatch an existing tag
with GitHub CLI:

```bash
gh workflow run build-tag.yml --ref v0.1.0
```

Manual dispatch requires the workflow on the default branch and at the selected
tag. Branch dispatches are rejected by version validation. Runs use the source,
workflow, and helpers stored at the selected tag: pushing a fix to the default
branch does not update an existing tag or its reruns. Runner/container images and
distribution package updates can change the rebuilt binary; check its build
metadata. Arch archives target a fully updated rolling system at the recorded
build time rather than a fixed distribution release.

To change the fooyin baseline, update `FOOYIN_REF` in the workflow and the
compatibility information here, then verify the build, full CTest suite, and
packaged plugin load check against that revision on every distribution.
