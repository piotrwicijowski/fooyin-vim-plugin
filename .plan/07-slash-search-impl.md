# Slash Search Implementation (`/`)

## What it does

Unlike the filter (Ctrl+I), slash search does NOT hide non-matching rows. The
full list stays visible. Pressing `/` opens the command line at the bottom of
the window; typing a pattern moves the cursor live to the first matching row.
`Enter` confirms (cursor stays, list unchanged). `Escape` returns the cursor to
where it was before the search started and clears the highlight. `n` / `N` jump
to the next / previous matching row, wrapping around.

---

## Difference from Filter (Ctrl+I)

| Behaviour             | Filter (Ctrl+I)          | Search (/)                     |
|-----------------------|--------------------------|--------------------------------|
| Rows hidden           | Yes — proxy model filter | No — all rows stay visible     |
| Cursor movement       | n/a (filter in-place)    | Jumps to first/next match      |
| Escape behaviour      | Clears filter            | Restores pre-search cursor pos |
| Enter behaviour       | Keeps filter             | Confirms position, clears bar  |
| n / N                 | ±1 row (filtered model)  | Jump between matches           |
| `searchEvent()` used  | Yes                      | No                             |

---

## New state in `VimHandler`

```cpp
// In vimhandler.h private section:

void enterSearch();
void commitSearch();
void cancelSearch();
void onSearchTextChanged(const QString& text);
void buildMatchList(const QString& pattern);
void jumpToMatch(int idx);

int              m_preSearchRow{-1};    // cursor row saved on '/' press
QVector<int>     m_searchMatches;       // row indices of current matches
int              m_searchMatchIdx{-1};  // current position in match list
QString          m_lastSearchPattern;   // for n/N after confirm
QPointer<VimSearchBar> m_searchBar;     // reuses VimSearchBar, label set to "/"
```

`Mode::Search` is added back to the enum (alongside the existing `Mode::Filter`).

---

## VimSearchBar label configurability

`VimSearchBar` currently hard-codes `"Filter: "`. Add a constructor parameter
(or a `setLabel` method) so the same widget can show `"/"` for search:

```cpp
// vimsearchbar.h — add:
void setLabel(const QString& text);

// vimsearchbar.cpp:
void VimSearchBar::setLabel(const QString& text) { m_prefix->setText(text); }
```

In `enterSearch()`, after creating the bar, call `m_searchBar->setLabel("/")`.
In `enterFilter()`, call `m_filterBar->setLabel("Filter: ")` (already the
default from construction, no change needed unless the bar is shared).

The two bars (`m_filterBar`, `m_searchBar`) are separate `VimSearchBar`
instances so they don't share state.

---

## `enterSearch()`

```cpp
void VimHandler::enterSearch()
{
    auto* view = m_viewLocator->activeView();
    if (!view || !view->model()) return;

    // Save cursor position so Escape can restore it.
    m_preSearchRow = view->currentIndex().isValid()
                   ? view->currentIndex().row() : 0;

    if (!m_searchBar) {
        m_searchBar = new VimSearchBar();
        m_searchBar->setLabel(QStringLiteral("/"));
        connect(m_searchBar, &VimSearchBar::textChanged,
                this, &VimHandler::onSearchTextChanged);
        connect(m_searchBar, &VimSearchBar::confirmed,
                this, &VimHandler::commitSearch);
        connect(m_searchBar, &VimSearchBar::cancelled,
                this, &VimHandler::cancelSearch);
    }

    m_searchBar->attachTo(view->window());
    m_searchBar->clear();
    m_searchMatches.clear();
    m_searchMatchIdx = -1;

    m_mode = Mode::Search;
    m_pendingKey = {};
    m_count = 0;
    emit modeChanged(m_mode);

    m_searchBar->show();
    m_searchBar->setFocus();
}
```

---

## `buildMatchList` — finding matching rows

Use `QAbstractItemModel::match()` which searches model data without requiring
access to internal types:

```cpp
void VimHandler::buildMatchList(const QString& pattern)
{
    m_searchMatches.clear();
    m_searchMatchIdx = -1;

    auto* view = m_viewLocator->activeView();
    if (!view || !view->model() || pattern.isEmpty()) return;

    auto* model = view->model();
    const int rows = model->rowCount();
    const int cols = model->columnCount();

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const QString cell = model->data(
                model->index(row, col), Qt::DisplayRole).toString();
            if (cell.contains(pattern, Qt::CaseInsensitive)) {
                m_searchMatches.append(row);
                break; // one match per row is enough
            }
        }
    }
}
```

Alternatively, `QAbstractItemModel::match()` can do this in one call:

```cpp
const QModelIndexList hits = model->match(
    model->index(0, 0), Qt::DisplayRole,
    pattern, -1,
    Qt::MatchContains | Qt::MatchWrap);
```

However `match()` only searches one column (column 0 by default). The manual
loop above searches all columns, which is better for a playlist with
title/artist/album columns. Use the manual approach.

---

## `onSearchTextChanged`

```cpp
void VimHandler::onSearchTextChanged(const QString& text)
{
    buildMatchList(text);
    if (!m_searchMatches.isEmpty()) {
        // Jump to the first match at or after the pre-search row.
        m_searchMatchIdx = 0;
        for (int i = 0; i < m_searchMatches.size(); ++i) {
            if (m_searchMatches[i] >= m_preSearchRow) {
                m_searchMatchIdx = i;
                break;
            }
        }
        jumpToMatch(m_searchMatchIdx);
    } else if (!text.isEmpty()) {
        // No matches — move cursor back to pre-search row.
        moveCursor(m_preSearchRow - currentRow());
    }
}
```

---

## `jumpToMatch`

```cpp
void VimHandler::jumpToMatch(int idx)
{
    auto* view = m_viewLocator->activeView();
    if (!view || !view->model() || m_searchMatches.isEmpty()) return;
    const int row = m_searchMatches[idx];
    const int col = view->currentIndex().isValid()
                  ? view->currentIndex().column() : 0;
    view->selectionModel()->setCurrentIndex(
        view->model()->index(row, col),
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view->scrollTo(view->model()->index(row, col));
}
```

---

## `commitSearch`

```cpp
void VimHandler::commitSearch()
{
    m_lastSearchPattern = m_searchBar ? m_searchBar->text() : QString{};
    if (m_searchBar) m_searchBar->hide();

    auto* view = m_viewLocator->activeView();
    if (view) view->setFocus(Qt::OtherFocusReason);

    m_mode = Mode::Normal;
    m_pendingKey = {};
    m_count = 0;
    emit modeChanged(m_mode);
}
```

The cursor is already on the last match the user navigated to. `m_searchMatches`
and `m_searchMatchIdx` are kept so `n`/`N` continue to work.

---

## `cancelSearch`

```cpp
void VimHandler::cancelSearch()
{
    if (m_searchBar) m_searchBar->hide();
    m_searchMatches.clear();
    m_searchMatchIdx = -1;
    m_lastSearchPattern.clear();

    // Restore cursor to pre-search position.
    auto* view = m_viewLocator->activeView();
    if (view && view->model() && m_preSearchRow >= 0) {
        const int col = view->currentIndex().isValid()
                      ? view->currentIndex().column() : 0;
        view->selectionModel()->setCurrentIndex(
            view->model()->index(m_preSearchRow, col),
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }

    if (view) view->setFocus(Qt::OtherFocusReason);

    m_mode = Mode::Normal;
    m_pendingKey = {};
    m_count = 0;
    emit modeChanged(m_mode);
}
```

---

## `nextMatch` / `prevMatch` — updated

These currently call `moveCursor(±1)` guarded on `m_lastFilter`. They need to
cycle through `m_searchMatches` when a search is active, and fall through to the
filter-mode behaviour otherwise:

```cpp
void VimHandler::nextMatch()
{
    if (!m_searchMatches.isEmpty()) {
        m_searchMatchIdx = (m_searchMatchIdx + 1) % m_searchMatches.size();
        jumpToMatch(m_searchMatchIdx);
        return;
    }
    if (!m_lastFilter.isEmpty())
        moveCursor(+1);
}

void VimHandler::prevMatch()
{
    if (!m_searchMatches.isEmpty()) {
        m_searchMatchIdx = (m_searchMatchIdx - 1 + m_searchMatches.size())
                         % m_searchMatches.size();
        jumpToMatch(m_searchMatchIdx);
        return;
    }
    if (!m_lastFilter.isEmpty())
        moveCursor(-1);
}
```

---

## Key handler changes

**`handleNormalKey`** — add after the existing `if (ch == u'i')` block:
```cpp
if (ch == u'/') { enterSearch(); return true; }
```

**`wouldHandleNormal`** — add to the char checks:
```cpp
if (ch == u'/') return true;
```

**`handleKeyPress`** — add:
```cpp
case Mode::Search:
    return false;   // bar has focus; committed/cancelled via signals
```

**`ShortcutOverride`** in `eventFilter` — already skips `Mode::Search` since
`claim` is only set for Normal/Visual (no change needed).

---

## Files to add / modify

| File | Change |
|---|---|
| `src/vimsearchbar.h` / `.cpp` | Add `setLabel(const QString&)` method |
| `src/vimhandler.h` | Add `Mode::Search`, new members, new method declarations |
| `src/vimhandler.cpp` | Implement `enterSearch`, `commitSearch`, `cancelSearch`, `onSearchTextChanged`, `buildMatchList`, `jumpToMatch`; update `nextMatch`/`prevMatch`; add `/` to `handleNormalKey` and `wouldHandleNormal` |

No new files, no CMakeLists change.
