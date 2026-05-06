# fooyin-vim-plugin — Draft Plan

> Status: decisions locked. Ready to begin implementation.

---

## Decisions summary

| # | Decision |
|---|---|
| Scope | All list views (not just playlist). No transport controls. |
| Count prefixes | Yes — `5j`, `10G`, `3dd`, etc. |
| Build | Out-of-tree (`fooyin-vim-plugin/` as standalone CMake project) |
| Command mode | Out of scope for now |
| Cursor driving | Direct `setCurrentIndex()` on the active `QAbstractItemView` |
| Mode toggle | `i` → Insert mode (vim off, pass-through); `Esc` → Normal mode |
| Shortcut conflicts | Vim event filter overrides fooyin's shortcuts in Normal/Visual modes |

---

## 1. Scope

The plugin treats every list-like view in fooyin (playlist, library browser, file browser, etc.) as a vim buffer. It also adds spatial focus navigation between views. No transport controls are in scope.

---

## 2. Modes

| Mode | What happens |
|---|---|
| **Normal** | All vim keys active; fooyin shortcuts suppressed |
| **Visual** | Range selection in progress |
| **Insert** | Vim off; all keys pass through to fooyin normally |

### Mode transitions

```
                i                      Esc
Normal ──────────────► Insert ───────────────► Normal
  │
  │ v
  ▼
Visual ──────────────────────────────────────► Normal
           Esc / y / d
```

Event filter return value:
- **Normal / Visual** → `true` (consume) for handled keys; also consume unknown keys so fooyin shortcuts don't fire accidentally.
- **Insert** → `false` for everything (full pass-through).

---

## 3. Key bindings

### 3.1 Normal mode

| Key(s) | Action |
|---|---|
| `j` / `k` | Move cursor down / up one row |
| `[count]j` / `[count]k` | Move cursor N rows |
| `gg` | Jump to first row |
| `G` / `[count]G` | Jump to last row / row N (1-indexed) |
| `Ctrl+d` | Move cursor half a screen down |
| `Ctrl+u` | Move cursor half a screen up |
| `Enter` / `o` | Activate / play item under cursor |
| `v` | Enter Visual mode |
| `dd` / `[count]dd` | Delete current row (or N rows) from the view |
| `yy` / `[count]yy` | Yank current row (or N rows) into vim clipboard |
| `p` | Paste yanked rows **after** cursor |
| `P` | Paste yanked rows **before** cursor |
| `i` | Enter Insert mode |
| `Ctrl+j` | Focus the view spatially **below** current view |
| `Ctrl+k` | Focus the view spatially **above** current view |
| `Ctrl+h` | Focus the view spatially **to the left** |
| `Ctrl+l` | Focus the view spatially **to the right** |

### 3.2 Visual mode

| Key(s) | Action |
|---|---|
| `j` / `k` | Extend selection down / up |
| `[count]j` / `[count]k` | Extend selection N rows |
| `o` | Toggle anchor — swap cursor between start and end of selection |
| `d` | Delete selected range, return to Normal |
| `y` | Yank selected range, return to Normal |
| `Esc` | Cancel selection, return to Normal |

---

## 4. Project layout (out-of-tree)

```
fooyin-vim-plugin/
  CMakeLists.txt
  vimmotions.json.in
  src/
    vimmotionsplugin.h/.cpp     ← QObject + Plugin + CorePlugin + GuiPlugin
    vimhandler.h/.cpp           ← event filter, mode state machine, key parser
    vimclipboard.h/.cpp         ← yank buffer (QList<Track> or generic row data)
    viewlocator.h/.cpp          ← finds the currently focused QAbstractItemView
    spatialnavigator.h/.cpp     ← Ctrl+j/k/h/l spatial focus switching
```

`CMakeLists.txt` outline:
```cmake
cmake_minimum_required(VERSION 3.19)
project(FooyinVimMotions)

find_package(Fooyin REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_library(vimmotions MODULE
    src/vimmotionsplugin.cpp
    src/vimhandler.cpp
    src/vimclipboard.cpp
    src/viewlocator.cpp
    src/spatialnavigator.cpp
)

target_link_libraries(vimmotions PRIVATE Fooyin::Gui Qt6::Widgets)
```

> **Unknown**: fooyin may not yet export a `FooyinConfig.cmake`. If not, the build will need manual include/lib path fallbacks. Verify during Phase 1.

---

## 5. Plugin class

`VimMotionsPlugin` implements all three fooyin interfaces:

| Interface | What it does |
|---|---|
| `CorePlugin::initialise` | Stores `PlaylistHandler*` for track manipulation |
| `GuiPlugin::initialise` | Stores `ActionManager*`, `WindowController*`; constructs and installs `VimHandler` |
| `Plugin::shutdown` | Removes event filter, releases resources |

`PlayerController` is not stored (transport is out of scope).

---

## 6. Key parser

`VimHandler` maintains:

- `m_mode` — `Normal`, `Visual`, `Insert`
- `m_count` — integer count prefix accumulator
- `m_pendingKey` — tracks the first key of a two-key sequence (`g` in `gg`, first `d` in `dd`, first `y` in `yy`)
- `m_visualAnchor` / `m_visualCursor` — row indices; `o` swaps which end the cursor is on

### Dispatch pseudocode (Normal mode)

```
on QKeyEvent(key, modifiers):

  // Spatial navigation — checked before anything else
  if Ctrl+j/k/h/l → spatialNavigator.moveFocus(direction); return true

  // Insert mode toggle
  if key == 'i' → enterInsert(); return true
  if key == Esc → clearPending(); return true

  // Count accumulation
  if key is digit AND (m_count > 0 OR key != '0'):
      m_count = m_count * 10 + digit; return true

  count = max(1, m_count); m_count = 0

  // Two-key sequences
  if m_pendingKey == 'g' and key == 'g' → jumpToFirst(); m_pendingKey = 0; return true
  if m_pendingKey == 'd' and key == 'd' → deleteRows(cursor, count); m_pendingKey = 0; return true
  if m_pendingKey == 'y' and key == 'y' → yankRows(cursor, count); m_pendingKey = 0; return true

  if key in {'g','d','y'} → m_pendingKey = key; return true   // wait for second key

  // Clear pending on any unrelated key
  m_pendingKey = 0

  switch key:
    'j'     → moveCursor(+count)
    'k'     → moveCursor(-count)
    'G'     → (m_count was set) ? jumpToRow(count-1) : jumpToLast()
    Ctrl+d  → moveCursorHalfPage(+1)
    Ctrl+u  → moveCursorHalfPage(-1)
    Enter/'o' → activateCurrentRow()
    'v'     → enterVisual()
    'p'     → pasteAfter()
    'P'     → pasteBefore()
    else    → return true   // consume unknown keys in Normal mode
```

### Visual mode key handling addition

```
  'o' → swap(m_visualAnchor, m_visualCursor); updateSelection()
```

---

## 7. ViewLocator

`ViewLocator` finds and caches the currently focused `QAbstractItemView` anywhere in the main window's widget tree.

Strategy:
1. Walk `QApplication::focusWidget()` up the parent chain; if it or an ancestor is a `QAbstractItemView`, that's the current view.
2. If no focused view, walk the main window's entire widget tree and return the first visible `QAbstractItemView`.
3. Cache the result and invalidate on `QApplication::focusChanged` signal.

This works for any view type fooyin uses (playlist, library, file browser, queue), with no hardcoded class names. The only assumption is that these widgets are `QAbstractItemView` subclasses, which is standard Qt.

> **Action before Phase 3**: confirm fooyin's list widgets actually subclass `QAbstractItemView` (check `fooyin/src/gui/playlist/` and browser widget sources).

---

## 8. SpatialNavigator

`SpatialNavigator` handles `Ctrl+j/k/h/l` focus switching between views.

Fooyin's layout is a tree of `QSplitter` nodes (horizontal and vertical splits) with focusable leaf widgets. Navigation follows the split tree structurally — no pixel geometry needed.

### Data

```
QMap<QSplitter*, int> m_lastVisited
```

Maps each `QSplitter` to the index of the child it most recently had focus inside. Updated via `QApplication::focusChanged`.

### Algorithm

`moveFocus(direction)`:

1. Start from the currently focused widget. Walk up its parent chain looking for a `QSplitter` whose orientation matches the requested direction (`Horizontal` for `h`/`l`, `Vertical` for `j`/`k`).
2. At each matching splitter, check whether moving in the requested direction (next or previous sibling index) is valid:
   - If yes: call `resolveLastVisited(sibling)` to get the leaf to focus, update `m_lastVisited` for this splitter, call `setFocus()`, done.
   - If no (at the edge): continue walking up the parent chain — the current widget might be nested inside a split that has a sibling at a higher level.
3. If the top of the tree is reached with no valid sibling, do nothing (already at the edge).

`resolveLastVisited(widget)`:
- If `widget` is a `QSplitter`: recurse into `widget->widget(m_lastVisited.value(widget, 0))`.
- If `widget` is a focusable leaf (`QAbstractItemView` or similar): return it.
- Otherwise: walk down to the first visible child recursively.

### Example walkthrough

Layout tree:
```
HorizontalSplitter
├── Playlist
└── VerticalSplitter
    ├── Library         (index 0)
    ├── PlaylistSwitcher (index 1)
    └── Details          (index 2)
```

- Focus on Playlist, press `Ctrl+l`:
  - Walk up → find `HorizontalSplitter`, direction=Right, sibling index = 1 = `VerticalSplitter`
  - `resolveLastVisited(VerticalSplitter)` → `m_lastVisited` has no entry → default index 0 → Library
  - Focus Library. `m_lastVisited[HorizontalSplitter] = 1`.

- Press `Ctrl+j` (now in Library):
  - Walk up → find `VerticalSplitter`, direction=Down, sibling index = 1 = PlaylistSwitcher
  - Focus PlaylistSwitcher. `m_lastVisited[VerticalSplitter] = 1`.

- Press `Ctrl+h` (now in PlaylistSwitcher):
  - Walk up → `VerticalSplitter` is Vertical, skip. Continue up → `HorizontalSplitter`, direction=Left, sibling index = 0 = Playlist
  - Focus Playlist. `m_lastVisited[HorizontalSplitter] = 0`.

- Press `Ctrl+l` again (back in Playlist):
  - Sibling = `VerticalSplitter`. `resolveLastVisited` → `m_lastVisited[VerticalSplitter] = 1` → PlaylistSwitcher
  - Focus PlaylistSwitcher directly. Memory preserved.

### Tracking last visited

Connect to `QApplication::focusChanged(QWidget* old, QWidget* now)`. Walk `now`'s parent chain; for each `QSplitter` ancestor, record the index of the child subtree that contains `now` in `m_lastVisited`.

---

## 9. Yank buffer (`VimClipboard`)

Stores yanked data as `QVariantList` of row data (not `Track` objects directly, to work across different view types). For playlist views the data is `QList<Fooyin::Track>`; for other views it may be file paths or other model data.

`paste(view, row, before)` inserts the stored data into the view's model at the target row. For playlist views this calls `PlaylistHandler`'s insert API; for other views it's TBD.

> **Unknown**: confirm `PlaylistHandler` exposes an insert-at-position method before implementing paste.

---

## 10. Action registration

All operations registered with `ActionManager` under category `"Vim Motions"`:

```
VimMotions.CursorDown         (j)
VimMotions.CursorUp           (k)
VimMotions.CursorTop          (gg)
VimMotions.CursorBottom       (G)
VimMotions.HalfPageDown       (Ctrl+d)
VimMotions.HalfPageUp         (Ctrl+u)
VimMotions.Activate           (Enter)
VimMotions.VisualMode         (v)
VimMotions.DeleteLine         (dd)
VimMotions.YankLine           (yy)
VimMotions.PasteAfter         (p)
VimMotions.PasteBefore        (P)
VimMotions.InsertMode         (i)
VimMotions.FocusDown          (Ctrl+j)
VimMotions.FocusUp            (Ctrl+k)
VimMotions.FocusLeft          (Ctrl+h)
VimMotions.FocusRight         (Ctrl+l)
```

Count prefixes and two-key sequences (`gg`, `dd`, `yy`) are parser-internal and not individually registered.

Visual-mode `j`/`k` are handled inside the state machine (not separate registrations) — they extend selection rather than move the cursor, which is a mode-dependent behavior difference not expressible as a single `QAction`.

---

## 11. Implementation phases

### Phase 1 — Skeleton + build verification
- `CMakeLists.txt` with `find_package(Fooyin)` (or fallback include paths)
- `VimMotionsPlugin` with all three interfaces, empty bodies
- Plugin appears in fooyin's plugin list and unloads cleanly
- Resolves the `FooyinConfig.cmake` unknown

### Phase 2 — Event filter + mode switching
- `VimHandler` installed on `QApplication::instance()`
- `i` → Insert, `Esc` → Normal
- Verify fooyin shortcuts are suppressed in Normal mode and restored in Insert mode

### Phase 3 — ViewLocator + cursor navigation
- Confirm `QAbstractItemView` subclassing in fooyin's widgets
- `ViewLocator` implemented
- `j` / `k` / `gg` / `G` / `Ctrl+d` / `Ctrl+u` / count prefix all working
- `Enter` activates current row

### Phase 4 — Spatial focus navigation
- `SpatialNavigator` implemented
- `Ctrl+j/k/h/l` switches focus between visible list views

### Phase 5 — Visual mode
- `v` enters Visual, `o` toggles anchor, `j`/`k` extend selection
- `d` deletes, `y` yanks, `Esc` cancels

### Phase 6 — Line-wise operations in Normal mode
- `dd` / `[count]dd`, `yy` / `[count]yy`
- `p` / `P` paste via `VimClipboard`
- `PlaylistHandler` insert API confirmed and wired up

### Phase 7 — Action registration
- All actions registered with `ActionManager`
- Key parser reads shortcuts from `Command` objects
- All actions visible in Settings → Shortcuts

---

## Remaining unknowns

| # | Unknown | Status |
|---|---|---|
| 1 | Does fooyin install a `FooyinConfig.cmake`? | **Resolved.** `cmake/FooyinConfig.cmake.in` exists. Installed config requires ALSA/FFmpeg/Taglib. Dev-tree fallback in `CMakeLists.txt` bypasses these. |
| 2 | Do fooyin's list widgets subclass `QAbstractItemView`? | **Resolved.** `PlaylistView → ExpandedTreeView → QAbstractItemView`. `qobject_cast<QAbstractItemView*>` will work. |
| 3 | Does `PlaylistHandler` have an insert-at-position method? | **Resolved (no).** Only `appendToPlaylist()` and `replacePlaylistTracks()` exist. Paste (`p`/`P`) must: read current track list → splice at position → call `replacePlaylistTracks()`. |
| 4 | Are there non-`QAbstractItemView` list-like widgets in fooyin that should also be supported? | Open — to be checked when implementing Phase 3. |

---

## Session checkpoint — 2026-05-06

### What has been implemented

**Phases 1 and 2 are complete.** The following files exist and compile:

```
fooyin-vim-plugin/
  CMakeLists.txt          — dual-mode build; dev-tree fallback looks in
                            build/run/lib/fooyin/ for libfooyin_core.so / libfooyin_gui.so
  CMakePresets.json       — "dev" and "dev-release" presets
  vimmotions.json.in      — plugin metadata
  src/
    vimmotionsplugin.h/.cpp  — plugin entry point; installs VimHandler on qApp
    vimhandler.h/.cpp        — event filter + Normal/Visual/Insert state machine
                               (Phase 3+ action methods are stubs)
```

### Build instructions

```bash
# Step 1 — Build fooyin (one-time; only fooyin_core + fooyin_gui are required)
cd /workspace/fooyin
cmake -B build -G Ninja
ninja -C build fooyin_core fooyin_gui

# Step 2 — Build the plugin
cd /workspace/fooyin-vim-plugin
cmake --preset dev        # FOOYIN_SRC_DIR=../fooyin, FOOYIN_BUILD_DIR=../fooyin/build
cmake --build build
# output: build/fyplugin_vimmotions.so
```

### What to do next — Phase 3

Implement `ViewLocator` and wire cursor-navigation actions into `VimHandler`.

**Files to create:**
```
src/viewlocator.h
src/viewlocator.cpp
```

**Add to `PLUGIN_SOURCES` in `CMakeLists.txt`.**

**`ViewLocator` API:**
```cpp
class ViewLocator : public QObject {
    Q_OBJECT
public:
    explicit ViewLocator(QObject* parent = nullptr);
    QAbstractItemView* activeView() const;   // may return nullptr
private:
    // invalidate on focusChanged
    mutable QPointer<QAbstractItemView> m_cached;
};
```

Strategy (from §7 of this plan):
1. Walk `QApplication::focusWidget()` up parent chain looking for `QAbstractItemView`.
2. If none, walk main window's widget tree for first visible `QAbstractItemView`.
3. Cache and invalidate on `QApplication::focusChanged`.

**Wire into `VimHandler`:**
- Add `ViewLocator* m_viewLocator` member.
- Construct it in `VimHandler`'s constructor (or pass from plugin).
- Fill in the stub action methods using `m_viewLocator->activeView()`:

```cpp
void VimHandler::moveCursor(int delta) {
    auto* view = m_viewLocator->activeView();
    if (!view) return;
    auto* model = view->model();
    int row = view->currentIndex().row();
    int newRow = std::clamp(row + delta, 0, model->rowCount() - 1);
    view->setCurrentIndex(model->index(newRow, 0));
}

void VimHandler::jumpToFirst() {
    auto* view = m_viewLocator->activeView();
    if (!view) return;
    view->setCurrentIndex(view->model()->index(0, 0));
}

void VimHandler::jumpToLast() {
    auto* view = m_viewLocator->activeView();
    if (!view) return;
    int last = view->model()->rowCount() - 1;
    view->setCurrentIndex(view->model()->index(last, 0));
}

void VimHandler::jumpToRow(int row) {
    auto* view = m_viewLocator->activeView();
    if (!view) return;
    int clamped = std::clamp(row, 0, view->model()->rowCount() - 1);
    view->setCurrentIndex(view->model()->index(clamped, 0));
}

void VimHandler::moveCursorHalfPage(int direction) {
    auto* view = m_viewLocator->activeView();
    if (!view) return;
    int visible = view->height() / view->sizeHintForRow(0);  // approx
    moveCursor(direction * (visible / 2));
}

void VimHandler::activateCurrentRow() {
    auto* view = m_viewLocator->activeView();
    if (!view) return;
    view->activated(view->currentIndex());
}
```

Also seed `m_visualAnchor`/`m_visualCursor` from `activeView()->currentIndex().row()` in `enterVisual()`.
