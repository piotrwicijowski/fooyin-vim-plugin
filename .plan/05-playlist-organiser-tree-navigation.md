# Plan 05: PlaylistOrganiser tree navigation

Supersedes the tree-navigation bullet in `03-future-improvements.md`.

## Goal

When the active view is a `QTreeView` (i.e. `PlaylistOrganiser`), give the
vim layer two distinct navigation modes:

| Key             | Behaviour                                                  |
|-----------------|------------------------------------------------------------|
| `j` / `k`       | Move to next/previous **visible** item (depth-first order) |
| `Ctrl+Shift+J/K`| Jump to next/previous **sibling** (current `j`/`k` behaviour) |
| `l`             | Expand node if collapsed; move into first child if already expanded |
| `h`             | Collapse node if expanded; move to parent if collapsed or leaf |

All other keys (`G`, `gg`, `Ctrl+D/U`, `v`, etc.) keep their existing
semantics, but `moveCursor` will be tree-aware so half-page and count
prefixes work correctly on the visible-item sequence.

---

## Detection

Add a small helper (free function or private method):

```cpp
static QTreeView* asTreeView(QAbstractItemView* v)
{
    return qobject_cast<QTreeView*>(v);
}
```

Call it at the top of each navigation function.  When it returns non-null,
use the tree path; otherwise fall through to the existing flat-list logic.
The helper keeps every callsite readable and the condition is checked only
once per keypress.

---

## Key routing changes

### `handleNormalKey` / `wouldHandleNormal`

**Ctrl block** — add before the existing `Ctrl+J/K/H/L` cases:

```cpp
if ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier)) {
    switch (qtKey) {
        case Qt::Key_J: treeMoveSibling(+count); return true;
        case Qt::Key_K: treeMoveSibling(-count); return true;
    }
}
```

Place this check *before* the plain-Ctrl block so `Ctrl+Shift+J` does not
accidentally match `Ctrl+J`.

**Bare-key block** — add after the `j`/`k` lines:

```cpp
if (ch == u'l') {
    if (asTreeView(m_viewLocator->activeView())) { treeOpenOrDescend(); return true; }
    return false;   // pass through on flat views
}
if (ch == u'h') {
    if (asTreeView(m_viewLocator->activeView())) { treeCloseOrAscend(); return true; }
    return false;
}
```

`wouldHandleNormal` must mirror both additions so `ShortcutOverride` is
claimed correctly.  For `l`/`h` the predicate should only return `true` when
the active view is a tree view.

---

## New / modified methods

### `moveCursor(int delta)` — make tree-aware

Replace the current flat-row arithmetic with a loop when the view is a tree:

```cpp
void VimHandler::moveCursor(int delta)
{
    auto* view = m_viewLocator->activeView();
    if (!view || !view->model()) { /* warn */ return; }

    if (auto* tree = asTreeView(view)) {
        treeMoveCursor(tree, delta);
        return;
    }

    // existing flat-list code unchanged ...
}
```

```cpp
void VimHandler::treeMoveCursor(QTreeView* tree, int delta)
{
    QModelIndex cur = tree->currentIndex();
    if (!cur.isValid()) {
        // seed to first visible item
        cur = tree->model()->index(0, 0);
    }
    const int steps = std::abs(delta);
    for (int i = 0; i < steps; ++i) {
        const QModelIndex next = (delta > 0) ? tree->indexBelow(cur)
                                             : tree->indexAbove(cur);
        if (!next.isValid()) break;
        cur = next;
    }
    tree->selectionModel()->setCurrentIndex(
        cur, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}
```

`QTreeView::indexBelow` / `indexAbove` respect the current expand/collapse
state, so this automatically treats the visible sequence as contiguous.

### `treeMoveSibling(int delta)`

```cpp
void VimHandler::treeMoveSibling(int delta)
{
    auto* tree = asTreeView(m_viewLocator->activeView());
    if (!tree) return;

    QModelIndex cur = tree->currentIndex();
    if (!cur.isValid()) return;

    const int targetRow = cur.row() + delta;
    if (targetRow < 0) return;

    const QModelIndex sibling = tree->model()->index(targetRow, 0, cur.parent());
    if (!sibling.isValid()) return;

    tree->selectionModel()->setCurrentIndex(
        sibling, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}
```

Count prefix works naturally: `3Ctrl+Shift+J` skips 3 siblings.

### `treeOpenOrDescend()` — implements `l`

```cpp
void VimHandler::treeOpenOrDescend()
{
    auto* tree = asTreeView(m_viewLocator->activeView());
    if (!tree) return;

    const QModelIndex cur = tree->currentIndex();
    if (!cur.isValid()) return;

    if (!tree->isExpanded(cur)) {
        tree->expand(cur);          // first press: open the group
    } else {
        const QModelIndex child = tree->model()->index(0, 0, cur);
        if (child.isValid()) {      // second press: descend
            tree->selectionModel()->setCurrentIndex(
                child, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
    }
}
```

If the node has no children, `expand` is a no-op and descend finds no valid
child, so `l` is silently ignored — matching vim's behaviour on a leaf.

### `treeCloseOrAscend()` — implements `h`

```cpp
void VimHandler::treeCloseOrAscend()
{
    auto* tree = asTreeView(m_viewLocator->activeView());
    if (!tree) return;

    const QModelIndex cur = tree->currentIndex();
    if (!cur.isValid()) return;

    if (tree->isExpanded(cur)) {
        tree->collapse(cur);        // node is open: close it
    } else {
        const QModelIndex parent = cur.parent();
        if (parent.isValid()) {     // node is closed/leaf: go to parent
            tree->selectionModel()->setCurrentIndex(
                parent, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
    }
}
```

---

## Header changes (`vimhandler.h`)

Add the four new private methods and include `<QTreeView>` (or forward-declare
it and include in the .cpp):

```cpp
void treeMoveCursor(QTreeView* tree, int delta);
void treeMoveSibling(int delta);
void treeOpenOrDescend();
void treeCloseOrAscend();
```

---

## `jumpToFirst` / `jumpToLast` / `jumpToRow`

All navigation — `gg`, `G`, `Ctrl+D`, `Ctrl+U`, `j`, `k` — must only move
among **currently visible** items.  No expand or collapse may happen as a
side-effect of navigation.

- `gg` → first visible item (row 0 at root) — already correct; no change needed.
- `G` → last visible item.  Requires walking forward with `indexBelow` until
  the next index is invalid:

```cpp
void VimHandler::jumpToLast()
{
    auto* view = m_viewLocator->activeView();
    if (!view || !view->model()) { /* warn */ return; }

    if (auto* tree = asTreeView(view)) {
        QModelIndex cur = tree->model()->index(0, 0);
        if (!cur.isValid()) return;
        while (true) {
            const QModelIndex next = tree->indexBelow(cur);
            if (!next.isValid()) break;
            cur = next;
        }
        tree->selectionModel()->setCurrentIndex(
            cur, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        return;
    }

    // existing flat-list code unchanged ...
}
```

- `jumpToRow` with a count prefix in tree mode: **deferred** (see below).

---

## Open questions / follow-ups

- **Visual mode in tree**: leave existing flat-row visual mode as-is.
  Dealing with selections that span parent/child boundaries is deferred to a
  later iteration.
- **`jumpToRow` (`nG`) in tree mode**: mapping a flat row number to a visible
  tree index is non-trivial.  Deferred.
- **`03-future-improvements.md`**: mark the tree-navigation bullet as done
  after implementation.
