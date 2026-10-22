# Debug logging

[Back to the README](../README.md)

The plugin uses Qt's categorized logging under the `fy.vim` category, matching fooyin's own logging convention. Three levels are emitted:

| Level | What is logged |
|---|---|
| `Info` | Plugin lifecycle (init, shutdown), mode transitions (Normal / Visual / Insert) |
| `Debug` | Every key event and the action it triggered, count accumulation, navigation from/to rows, ViewLocator cache hits/misses and focus-chain traversal, SpatialNavigator splitter traversal and last-visited updates, all playlist operations (playlist name, row ranges, track counts) |
| `Warning` | Error conditions: no active view, no PlaylistHandler, no active playlist, selection out of range |

## Enabling debug output

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

## Output format

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

The [build guide](BUILDING.md) also describes `make run-debug` for local development.
