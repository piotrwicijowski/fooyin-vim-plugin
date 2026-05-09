# Configurable Bindings

> Status: in-progress — chunks implemented sequentially.

## Progress

| Chunk | Status |
|---|---|
| 1. VimBindingParser | Done |
| 2. VimActions | Done |
| 3. VimMotionsSettings | Done |
| 4. CMakeLists.txt | Done |
| 5. VimHandler integration | Done |
| 6. wouldHandle predicates | Done (integrated into Chunk 5) |
| 7. Runtime subscribe | Not started |
| 8. README update | Not started |

## Decisions summary

| # | Decision |
|---|---|
| Config format | Compound key→value strings via fooyin's `SettingsManager` (Option C) |
| Two-key sequences | Flat two-char keys (`gg`, `dd`, `yy`, `g;`) |
| Feature flag | `VimMotions/UseConfigBindings` (Bool, default `false`) — config setting |
| Action registry | String action IDs mapping to `std::function` handlers |
| Runtime updates | `SettingsManager::subscribe` per binding — no restart needed |
| `/` key encoding | `slash` in the config key (e.g., `Binding/Normal/slash`) |

---

## 1. Overview

Currently all key→action mappings are hardcoded in `vimhandler.cpp` in `handleNormalKey()` and `handleVisualKey()`. This plan introduces a config-driven binding layer that coexists with the existing code behind a feature flag.

The old code is left untouched. When `VimMotions/UseConfigBindings` is `false` (default), behaviour is identical to today. When `true`, the handler reads bindings from `SettingsManager` and dispatches through a new action registry.

---

## 2. Config key scheme

Each binding is stored as a `String`-typed setting in fooyin's `fooyin.conf`:

```ini
[VimMotions]
Bindings\Normal\j=moveCursor:+1
Bindings\Normal\k=moveCursor:-1
Bindings\Normal\gg=jumpToFirst
Bindings\Normal\G=jumpToLast
Bindings\Normal\dd=deleteRows
Bindings\Normal\yy=yankRows
Bindings\Normal\i=enterInsert
Bindings\Normal\v=enterVisual
Bindings\Normal\u=undo
Bindings\Normal\Ctrl\+R=redo
Bindings\Normal\h=treeCloseOrAscend
Bindings\Normal\l=treeOpenOrDescend
Bindings\Normal\o=focusNowPlaying
Bindings\Normal\p=pasteAfter
Bindings\Normal\P=pasteBefore
Bindings\Normal\slash=enterSearch
Bindings\Normal\n=nextMatch
Bindings\Normal\N=prevMatch
Bindings\Normal\Escape=clearPending
Bindings\Normal\Ctrl\+Shift\+J=treeMoveSibling:+1
Bindings\Normal\Ctrl\+Shift\+K=treeMoveSibling:-1
Bindings\Normal\Ctrl\+J=spatialMoveFocus:down
Bindings\Normal\Ctrl\+K=spatialMoveFocus:up
Bindings\Normal\Ctrl\+H=spatialMoveFocus:left
Bindings\Normal\Ctrl\+L=spatialMoveFocus:right
Bindings\Normal\Ctrl\+D=moveCursorHalfPage:+1
Bindings\Normal\Ctrl\+U=moveCursorHalfPage:-1
Bindings\Normal\Ctrl\+R=redo
Bindings\Normal\Ctrl\+I=enterFilter
Bindings\Normal\Alt\+J=moveRows:+1
Bindings\Normal\Alt\+K=moveRows:-1

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
Bindings\Visual\Ctrl\+D=extendHalfPage:+1
Bindings\Visual\Ctrl\+U=extendHalfPage:-1
Bindings\Visual\Alt\+J=moveVisualSelection:+1
Bindings\Visual\Alt\+K=moveVisualSelection:-1
Bindings\Visual\h=treeCloseOrAscend
Bindings\Visual\l=treeOpenOrDescend

Bindings\Insert\Escape=leaveInsertMode
```

The flat `SettingsManager` key for reading is:
```
VimMotions/Bindings/{Mode}/{KeyCombo}
```

The parser splits the final segment of the key into key-combo and modifiers:
- `j` → key=`j`, modifiers=none
- `Ctrl+J` → key=`J` (Qt::Key_J), modifiers=Ctrl
- `Ctrl+Shift+J` → key=`J`, modifiers=Ctrl+Shift
- `Alt+J` → key=`J`, modifiers=Alt
- `gg` → key=`gg` (two-char sequence), modifiers=none
- `Escape` → key=`Escape` (named key)
- `slash` → key=`/`

### Value format

```
ActionName[:param1[,param2...]]
```

- If no params are needed, omit the colon entirely: `undo`, `enterInsert`
- Single param: `moveCursor:+1`, `treeMoveSibling:-1`
- Future params could be comma-separated

---

## 3. Feature flag

### Setting

- **Key**: `VimMotions/UseConfigBindings`
- **Type**: `Bool`
- **Default**: `false`
- **Effect**: When `false`, the existing hardcoded `handleNormalKey`/`handleVisualKey`/`wouldHandleNormal`/`wouldHandleVisual` run as-is. When `true`, the new config-driven dispatch is used.

The flag is checked at the entry points in `VimHandler::handleNormalKey()`, `handleVisualKey()`, `wouldHandleNormal()`, `wouldHandleVisual()`. The dispatch splits:

```
handleNormalKey(ev)
  └── if UseConfigBindings
        └─→ dispatchFromConfig(ev, Mode::Normal)
      else
        └─→ existing hardcoded logic
```

### Registration

Registered in a new `VimMotionsSettings` class that the plugin creates during `initialise()`:

```cpp
auto* settings = new VimMotionsSettings(settingsManager);
```

`VimMotionsSettings` creates all the binding settings + the feature flag in `SettingsManager`.

---

## 4. New source files

| File | Purpose |
|---|---|
| `src/vimactions.h` / `src/vimactions.cpp` | Action registry — maps string IDs to handler functions |
| `src/vimbindingparser.h` / `src/vimbindingparser.cpp` | Parses config strings into key-combo + action structs |
| `src/vimmotionssettings.h` / `src/vimmotionssettings.cpp` | Settings enum, registrar, defaults |

---

## 5. Action registry (`VimActions`)

Central registry mapping action name → callable:

```cpp
class VimActions {
public:
    using Handler = std::function<void(VimHandler&, const QStringView& args)>;

    void registerAction(const QString& name, Handler handler);
    Handler find(const QString& name) const;

    // Built-in action definitions
    static VimActions& defaults();
    void registerAll();

private:
    QHash<QString, Handler> m_actions;
};
```

Registered actions include:

| Action string | Parameter | Handler calls |
|---|---|---|
| `moveCursor` | `+1` / `-1` | `handler.moveCursor(delta)` |
| `jumpToFirst` | (none) | `handler.jumpToFirst()` |
| `jumpToLast` | (none) | `handler.jumpToLast()` |
| `deleteRows` | (none) | `handler.deleteRows(count)` |
| `yankRows` | (none) | `handler.yankRows(count)` |
| `enterInsert` | (none) | `handler.enterInsert()` |
| `enterVisual` | (none) | `handler.enterVisual()` |
| `undo` | (none) | `handler.undo()` |
| `redo` | (none) | `handler.redo()` |
| `treeOpenOrDescend` | (none) | `handler.treeOpenOrDescend()` |
| `treeCloseOrAscend` | (none) | `handler.treeCloseOrAscend()` |
| `focusNowPlaying` | (none) | `handler.focusNowPlaying()` |
| `pasteAfter` | (none) | `handler.pasteAfter()` |
| `pasteBefore` | (none) | `handler.pasteBefore()` |
| `enterSearch` | (none) | `handler.enterSearch()` |
| `nextMatch` | (none) | `handler.nextMatch()` |
| `prevMatch` | (none) | `handler.prevMatch()` |
| `enterFilter` | (none) | `handler.enterFilter()` |
| `spatialMoveFocus` | `up`/`down`/`left`/`right` | `handler.spatialNavigator()->moveFocus(...)` |
| `moveCursorHalfPage` | `+1` / `-1` | `handler.moveCursorHalfPage(direction)` |
| `treeMoveSibling` | `+1` / `-1` | `handler.treeMoveSibling(delta)` |
| `moveRows` | `+1` / `-1` | `handler.moveRows(delta)` |
| `clearPending` | (none) | Clear `m_count` and `m_pendingKey` |
| `extendCursor` | `+1` / `-1` | Visual: extend selection |
| `extendToFirst` | (none) | Visual: extend to first row |
| `extendToEnd` | (none) | Visual: extend to last |
| `swapAnchor` | (none) | Visual: swap anchor/cursor |
| `deleteSelection` | (none) | Visual: delete + yank |
| `yankSelection` | (none) | Visual: yank |
| `leaveVisualMode` | (none) | Visual → Normal |
| `extendHalfPage` | `+1` / `-1` | Visual: half-page extend |
| `moveVisualSelection` | `+1` / `-1` | Visual: move rows |
| `nextMatchAndExit` | (none) | Visual: next match, exit visual |
| `prevMatchAndExit` | (none) | Visual: prev match, exit visual |
| `enterSearchAndExit` | (none) | Visual: search, exit visual |
| `leaveInsertMode` | (none) | Insert → Normal |

---

## 6. Binding parser (`VimBindingParser`)

Parses a config key (the last path segment) and value into a `BindingEntry`:

```cpp
struct KeyCombo {
    Qt::Key key;
    Qt::KeyboardModifiers modifiers;
};

struct BindingEntry {
    KeyCombo firstKey;
    bool     isTwoKey{false};
    KeyCombo secondKey{};       // valid only when isTwoKey is true
    QString  actionName;
    QString  args;              // the part after ':'
};

BindingEntry parseBinding(const QString& keyComboStr, const QString& valueStr);
```

The parser handles:
- `j` → `firstKey=Key_J`, `modifiers=NoModifier`, `isTwoKey=false`
- `gg` → `firstKey=Key_G`, `isTwoKey=true`, `secondKey=Key_G`
- `Ctrl+J` → `firstKey=Key_J`, `modifiers=ControlModifier`
- `g;` → `firstKey=Key_G`, `isTwoKey=true`, `secondKey=Key_Semicolon`
- `Escape` → `firstKey=Key_Escape`
- `slash` → `firstKey=Key_Slash`

---

## 7. Runtime binding map

`VimHandler` gains a new data structure (used only when the feature flag is active):

```cpp
// Key: mode → (firstKey → list of BindingEntry)
// List because two-key sequences share a firstKey with their single-key counterpart.
// E.g., both 'g' and 'gg' start with 'g'.
QHash<Mode, QHash<quint32 /*key+mods*/, std::vector<BindingEntry>>> m_configBindings;
```

On feature-flag activation, all `VimMotions/Bindings/{Mode}/*` settings are read and parsed into this map. Two-key sequences are stored alongside single-key entries for the same first key — when `m_pendingKey` is set, only entries with matching `secondKey` are considered.

### Dispatch flow

```
handleNormalKey(ev):
  1. If !UseConfigBindings → old code
  2. Accumulate count (same as today — hardcoded)
  3. If pendingKey is set:
     a. Look up BindingEntries for Mode::Normal with firstKey == pendingKey
     b. Filter by secondKey matching current key
     c. If match → invoke action, clear pending
     d. If no match → clear pending, fall through to normal lookup
  4. Look up BindingEntries for Mode::Normal with firstKey == current key
  5. If any entry has isTwoKey=true → set pendingKey, return true
  6. If single-key match → invoke action
  7. No match → return false (pass through)
```

### wouldHandleNormal / wouldHandleVisual

These predicates now iterate the binding map for the current mode to determine whether a key would be claimed:

```cpp
bool wouldHandleNormal(QKeyEvent* ev) const {
    if (!m_useConfigBindings) return oldWouldHandleNormal(ev);
    // Check if any binding in Normal mode matches this key
    return hasBindingFor(Mode::Normal, ev);
}
```

---

## 8. Settings registration

### Header: `vimmotionssettings.h`

```cpp
namespace Fooyin::Settings::VimMotions {
Q_NAMESPACE
enum VimMotionsSetting : uint32_t
{
    UseConfigBindings = 0 | Type::Bool,
    BindingStart      = 1,  // bindings start after this — stored as individual String settings
};
Q_ENUM_NS(VimMotionsSetting)
}

namespace Fooyin::VimMotions {
class VimMotionsSettings
{
public:
    explicit VimMotionsSettings(SettingsManager* settingsManager);
    void registerDefaults(SettingsManager* settingsManager);
};
}
```

### Source: `vimmotionssettings.cpp`

- Creates `UseConfigBindings` (Bool, default false)
- Registers all default binding values as `String`-typed settings under `VimMotions/Bindings/{Mode}/{KeyCombo}`

The defaults are the current hardcoded mappings — this means flipping the flag to `true` with no user config changes produces identical behaviour.

---

## 9. Plugin integration

### `vimmotionsplugin.cpp` changes

```cpp
void VimMotionsPlugin::initialise(const CorePluginContext& context)
{
    m_settingsManager = context.settingsManager;
    m_playlistHandler = context.playlistHandler;
    m_settings = new VimMotionsSettings(m_settingsManager);
    // ...
}

void VimMotionsPlugin::initialise(const GuiPluginContext& context)
{
    m_vimHandler = new VimHandler(this);
    m_vimHandler->setSettingsManager(m_settingsManager);
    // ...
}
```

### `VimHandler` changes

- Gains `setSettingsManager(SettingsManager*)` 
- Stores pointer, subscribes to `UseConfigBindings` changes
- On change: (re)builds binding map from config, or reverts to hardcoded logic
- Subscribes to each `VimMotions/Bindings/*` setting so binding changes are picked up without restart

---

## 10. Runtime binding change flow

```
User edits fooyin.conf:
  [VimMotions]
  Bindings\Normal\j=moveCursor:-1   # was +1

SettingsManager detects change via QSettings sync
  ↓
subscribe callback fires on "VimMotions/Bindings/Normal/j"
  ↓
VimHandler::onBindingChanged()
  ↓
Rebuilds the BindingEntry map for that mode
  ↓
Next keypress uses the new binding
```

This is handled by subscribing to the `SettingsManager` setting change signal for the `VimMotions/Bindings/*` settings. The subscription is on the individual setting key.

Actually, to avoid subscribing to every single binding individually, we can subscribe to the broader mechanism: either subscribe to the feature flag change and reload all bindings, or use `SettingsManager::subscribe` on a per-binding basis.

Since we register each binding as a separate `String` setting via `SettingsManager::createSetting`, each can be independently subscribed. But for simplicity in the initial implementation, we can reload all bindings whenever any binding-related setting changes. This can be done by watching `UseConfigBindings` and by a manual reload triggered from `VimHandler`.

Simpler approach: since `SettingsManager::subscribe` allows per-key callbacks, register all bindings and subscribe each. When any single binding changes, reload that one entry in the map. This avoids a full rebuild.

Even simpler v1 approach: just call `rebuildBindings()` when the feature flag flips, and when any binding changes. The map is small (< 50 entries) so rebuilding is negligible.

---

## 11. Implementation order

1. Create `VimBindingParser` — parse config key strings into `KeyCombo` structs
2. Create `VimActions` — action registry with all current actions registered
3. Create `VimMotionsSettings` — settings enum + registrar with feature flag and default bindings
4. Add `rebuildBindings()` to `VimHandler` — reads settings, parses, populates binding map
5. Add `dispatchFromConfig()` to `VimHandler` — handles count, pending keys, action dispatch
6. Add `setSettingsManager()` and feature flag plumbing
7. Update `eventFilter`/`handleNormalKey`/`handleVisualKey` to branch on the flag
8. Update `wouldHandleNormal`/`wouldHandleVisual` for the new system
9. Subscribe to setting changes for runtime reload
10. Update `CMakeLists.txt` with new source files
11. Update `README.md`

---

## 12. Backward compatibility

- Old code path is completely unchanged — when `UseConfigBindings=false`, the binary behaves identically to today
- Default bindings in the settings match the current hardcoded ones exactly
- Users who never touch config get the same experience
- If a binding is missing from config (e.g., user deleted a line), the handler returns `false` for that key (pass-through), same as today's unrecognised-key behaviour
