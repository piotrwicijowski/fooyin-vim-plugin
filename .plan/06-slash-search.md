# Slash Search (`/`)

## What it does

In Normal mode, pressing `/` opens a vim-style command-line at the bottom of the
active view. The user types a pattern; the view filters live as they type. `Enter`
confirms (keeps the filter, returns to Normal). `Escape` cancels (clears the
filter, returns to Normal). `n` / `N` in Normal mode navigate to the next /
previous match row.

---

## Relevant fooyin APIs

### `FyWidget` and `searchEvent`

`include/gui/fywidget.h`

Widgets that participate in fooyin's search system set the `FyWidget::Search` (or
`ExclusiveSearch`) feature flag and override `searchEvent(const SearchRequest&)`.
`SearchRequest` carries a `QString text` and an `EmptySearchMode` (Clear or
ShowAll). Calling `searchEvent({})` clears the filter.

PlaylistView and LibraryView both implement `searchEvent`; that is what populates
the filtered model the user sees.

### `SearchController`

`src/gui/search/searchcontroller.h` (internal, but exposed via `GuiPluginContext`)

The controller is a registry + dispatcher. The relevant public surface:

```cpp
void   loadWidgets();                                   // discover FyWidget::Search widgets
void   changeSearch(const Id& sourceId, const SearchRequest&); // dispatch to connected targets
void   setConnectedWidgets(const Id& id, const IdSet&); // establish connection
void   removeConnectedWidgets(const Id& id);            // tear down connection
WidgetList connectedWidgets(const Id& id);              // list targets
```

`SearchController::changeSearch` simply iterates the connections for `sourceId`
and calls `widget->searchEvent(request)` on each target. The plugin can therefore
also call `searchEvent` directly — both paths are equivalent.

### `GuiPluginContext`

`include/gui/plugins/guiplugincontext.h`

`GuiPluginContext::searchController` is the live `SearchController*`.
`VimMotionsPlugin::initialise(const GuiPluginContext&)` currently ignores the
context entirely — storing `searchController` there is a one-liner addition.

---

## Architecture

### Strategy: direct `searchEvent` call

Rather than managing `SearchController` connections (which exist to support the
GUI "connect / disconnect" overlay), the plugin will:

1. At search-start, walk the parent chain of `ViewLocator::activeView()` to find
   the enclosing `FyWidget*` (the first ancestor that is a `FyWidget`).
2. Call `fyWidget->searchEvent(request)` directly on each text change and on
   cancel/confirm.

This is exactly what `SearchController::changeSearch` does internally. It avoids
the overhead of connection management and keeps the plugin self-contained.
`SearchController` is still acquired and stored (useful for `loadWidgets()` if
needed later, and in case we want `n`/`N` to use the connection model), but the
critical dispatch path is direct.

### New file: `vimsearchbar.h` / `vimsearchbar.cpp`

A thin `QWidget` that positions itself at the bottom-left of a given parent
widget (the active view's window or the active view itself):

```
[ / <QLineEdit>____________ ]
```

```cpp
class VimSearchBar : public QWidget {
    Q_OBJECT
public:
    explicit VimSearchBar(QWidget* parent = nullptr);

    void attachTo(QWidget* anchor);  // reparent + reposition
    void clear();
    QString text() const;

signals:
    void textChanged(const QString& text);
    void confirmed();   // Enter pressed
    void cancelled();   // Escape pressed

protected:
    void keyPressEvent(QKeyEvent* ev) override;
    void showEvent(QShowEvent* ev) override;

private:
    QLabel*    m_prefix;   // shows "/"
    QLineEdit* m_edit;
};
```

`attachTo` reparents the bar to the anchor widget and installs a resize-event
filter on the anchor so the bar stays pinned to the bottom edge when the window
is resized.

### Changes to `VimHandler`

**New mode value**

```cpp
enum class Mode { Normal, Visual, Insert, Search };
```

**New members**

```cpp
QString               m_lastSearch;
QPointer<FyWidget*>   m_searchTarget;   // FyWidget receiving searchEvent calls
VimSearchBar*         m_searchBar{nullptr};
SearchController*     m_searchController{nullptr};  // optional, for future n/N
```

**New private methods**

```cpp
void enterSearch();
void commitSearch();    // Enter: keep filter, go Normal
void cancelSearch();    // Escape: clear filter, go Normal

FyWidget* findEnclosingFyWidget(QAbstractItemView* view) const;

void nextMatch();       // n
void prevMatch();       // N
```

**`handleNormalKey` additions**

```cpp
if (ch == u'/') { enterSearch(); return true; }
if (ch == u'n') { nextMatch();   return true; }
if (ch == u'N') { prevMatch();   return true; }
```

`wouldHandleNormal` gains the same three characters so `ShortcutOverride` claims
them.

**`handleKeyPress` in Search mode**

In Search mode the bar's `QLineEdit` has focus, so Qt delivers key events to it
directly. `VimHandler` does not need to forward individual characters. It only
needs to suppress its own event-filter so normal typing reaches the line-edit:

```cpp
case Mode::Search:
    // Let events flow to the search bar; committed/cancelled via signals.
    return false;
```

The bar itself emits `confirmed()` / `cancelled()` signals that `VimHandler`
connects to `commitSearch()` / `cancelSearch()`.

### Changes to `VimMotionsPlugin`

```cpp
// header — add member:
SearchController* m_searchController{nullptr};

// .cpp — store in GUI initialise:
void VimMotionsPlugin::initialise(const GuiPluginContext& context)
{
    m_searchController = context.searchController;
    m_vimHandler = new VimHandler(this);
    m_vimHandler->setPlaylistHandler(m_playlistHandler);
    m_vimHandler->setSearchController(m_searchController);
    qApp->installEventFilter(m_vimHandler);
}
```

---

## Search mode state machine

```
Normal  ──/──>  Search  ──Enter──>  Normal  (filter active, m_lastSearch set)
                        ──Esc──>    Normal  (filter cleared, m_lastSearch cleared)
```

### `enterSearch()`

1. Find `activeView()`; bail if null.
2. `findEnclosingFyWidget(view)` → store in `m_searchTarget`.
3. Create / reuse `m_searchBar`; call `m_searchBar->attachTo(view->window())`.
4. Clear the bar's text.
5. `m_mode = Mode::Search`; emit `modeChanged`.
6. Show the bar and give it focus.
7. Connect bar signals (one-shot or persistent with guard):
   - `textChanged` → lambda: call `m_searchTarget->searchEvent({text, ShowAll})`
   - `confirmed`   → `commitSearch()`
   - `cancelled`   → `cancelSearch()`

### `commitSearch()`

1. `m_lastSearch = m_searchBar->text()`.
2. Hide the bar.
3. `m_mode = Mode::Normal`; emit `modeChanged`.
4. Restore focus to the view.

The filter stays active. The user can clear it by pressing `/`, `Escape`.

### `cancelSearch()`

1. If `m_searchTarget`, call `m_searchTarget->searchEvent({})` (clears filter).
2. `m_lastSearch.clear()`.
3. Hide the bar.
4. `m_mode = Mode::Normal`; emit `modeChanged`.
5. Restore focus to the view.

---

## `n` / `N` navigation

After a search is confirmed the active view's model is already filtered — only
matching rows are visible. `moveCursor(+1)` / `moveCursor(-1)` therefore already
step between matches.

Initial implementation: `nextMatch()` calls `moveCursor(+1)`, `prevMatch()` calls
`moveCursor(-1)`. Only do anything if `!m_lastSearch.isEmpty()`.

This is deliberately simple. If more precise behaviour is needed later (e.g.,
wrapping, or searching an unfiltered model), it can be layered on top without
changing the interface.

---

## `findEnclosingFyWidget`

```cpp
FyWidget* VimHandler::findEnclosingFyWidget(QAbstractItemView* view) const
{
    QWidget* w = view;
    while (w) {
        if (auto* fy = qobject_cast<FyWidget*>(w))
            return fy;
        w = w->parentWidget();
    }
    return nullptr;
}
```

Requires `#include <gui/fywidget.h>` in `vimhandler.cpp`. This header is in the
public include path (`include/gui/fywidget.h`) and is already used transitively
by other fooyin plugin headers.

---

## Edge cases

| Situation | Handling |
|---|---|
| No `FyWidget` ancestor found | `enterSearch()` does nothing (logs warning) |
| Active view changes while search bar is visible | Not a concern: bar is modal-ish (vim is in Search mode); focus stays on bar |
| `m_searchTarget` destroyed while bar is open | `QPointer` nulls; `cancelSearch()` guards on null before calling `searchEvent` |
| `/` pressed while search already active | Re-enter search (pre-populate bar with `m_lastSearch` so user can refine) |
| Escape in Insert mode | Already goes to Normal; does not interact with search |
| `n`/`N` with no prior search | No-op (guard: `m_lastSearch.isEmpty()`) |

---

## Files to add / modify

| File | Change |
|---|---|
| `src/vimsearchbar.h` | New |
| `src/vimsearchbar.cpp` | New |
| `src/vimhandler.h` | Add `Mode::Search`, new members, new method declarations |
| `src/vimhandler.cpp` | Implement `enterSearch`, `commitSearch`, `cancelSearch`, `findEnclosingFyWidget`, `nextMatch`, `prevMatch`; extend `handleNormalKey` and `wouldHandleNormal` |
| `src/vimmotionsplugin.h` | Add `SearchController* m_searchController` |
| `src/vimmotionsplugin.cpp` | Store and forward `searchController` from GUI context |
| `CMakeLists.txt` | Add `vimsearchbar.cpp` to sources |
