#include "vimhandler.h"
#include "spatialnavigator.h"
#include "viewlocator.h"
#include "vimlog.h"

#include <core/playlist/playlist.h>
#include <core/playlist/playlisthandler.h>

#include <QAbstractItemView>
#include <QCoreApplication>
#include <QItemSelection>
#include <QKeyEvent>
#include <QPointer>
#include <QTimer>
#include <algorithm>
#include <utility>

Q_LOGGING_CATEGORY(VIM_LOG, "fy.vim")

namespace Fooyin::VimMotions {

VimHandler::VimHandler(QObject* parent)
    : QObject{parent}
    , m_viewLocator{new ViewLocator(this)}
    , m_spatialNavigator{new SpatialNavigator(this)}
{
    qCDebug(VIM_LOG) << "VimHandler created";
}

VimHandler::Mode VimHandler::mode() const
{
    return m_mode;
}

bool VimHandler::eventFilter(QObject* watched, QEvent* event)
{
    if (m_suppressFilter)
        return false;

    const auto type = event->type();

    // Qt sends ShortcutOverride before it would activate a matching QShortcut.
    // If the receiver (or its filter) returns true here, Qt skips the shortcut
    // and delivers the normal KeyPress instead. In Normal/Visual mode we claim
    // every key so no registered shortcut can steal it from our handler.
    if (type == QEvent::ShortcutOverride) {
        auto* kev = static_cast<QKeyEvent*>(event);
        qCDebug(VIM_LOG) << "eventFilter ShortcutOverride: key=" << kev->key()
                         << "text=" << kev->text() << "mods=" << kev->modifiers()
                         << "mode=" << static_cast<int>(m_mode)
                         << "watched=" << watched->metaObject()->className();
        if (m_mode == Mode::Normal || m_mode == Mode::Visual) {
            kev->accept();
            return true;
        }
        return false;
    }

    if (type != QEvent::KeyPress)
        return false;

    return handleKeyPress(static_cast<QKeyEvent*>(event));
}

bool VimHandler::handleKeyPress(QKeyEvent* ev)
{
    switch (m_mode) {
        case Mode::Insert:
            if (ev->key() == Qt::Key_Escape) {
                qCDebug(VIM_LOG) << "Insert: Esc → Normal";
                enterNormal();
                return true;
            }
            return false;
        case Mode::Normal:
            return handleNormalKey(ev);
        case Mode::Visual:
            return handleVisualKey(ev);
    }
    return false;
}

bool VimHandler::handleNormalKey(QKeyEvent* ev)
{
    const Qt::KeyboardModifiers mods = ev->modifiers();
    const int qtKey = ev->key();
    const QString text = ev->text();
    const QChar ch = text.isEmpty() ? QChar{} : text.front();

    qCDebug(VIM_LOG) << "Normal key: text=" << text << "qtKey=" << qtKey
                     << "mods=" << mods << "pendingKey=" << m_pendingKey
                     << "accumCount=" << m_count;

    // Ctrl combinations — consumed before count accumulation
    if (mods & Qt::ControlModifier) {
        switch (qtKey) {
            case Qt::Key_J:
                qCDebug(VIM_LOG) << "Normal: Ctrl+J → focus Down";
                m_spatialNavigator->moveFocus(Direction::Down);  return true;
            case Qt::Key_K:
                qCDebug(VIM_LOG) << "Normal: Ctrl+K → focus Up";
                m_spatialNavigator->moveFocus(Direction::Up);    return true;
            case Qt::Key_H:
                qCDebug(VIM_LOG) << "Normal: Ctrl+H → focus Left";
                m_spatialNavigator->moveFocus(Direction::Left);  return true;
            case Qt::Key_L:
                qCDebug(VIM_LOG) << "Normal: Ctrl+L → focus Right";
                m_spatialNavigator->moveFocus(Direction::Right); return true;
            case Qt::Key_D:
                qCDebug(VIM_LOG) << "Normal: Ctrl+D → half page down";
                moveCursorHalfPage(+1); return true;
            case Qt::Key_U:
                qCDebug(VIM_LOG) << "Normal: Ctrl+U → half page up";
                moveCursorHalfPage(-1); return true;
            default:
                qCDebug(VIM_LOG) << "Normal: unrecognised Ctrl combo qtKey=" << qtKey << ", consuming";
                return true;
        }
    }

    if (qtKey == Qt::Key_Escape) {
        qCDebug(VIM_LOG) << "Normal: Esc → clear pending/count (was pendingKey="
                         << m_pendingKey << "count=" << m_count << ")";
        m_pendingKey = {};
        m_count = 0;
        return true;
    }

    // Insert mode (before digit check so 'i' is never swallowed as a count)
    if (ch == u'i') {
        qCDebug(VIM_LOG) << "Normal: 'i' → Insert mode";
        enterInsert();
        return true;
    }

    // Count accumulation: bare digits only (no modifier other than Keypad)
    if (!ch.isNull() && ch.isDigit() && !(mods & ~Qt::KeypadModifier)) {
        const int digit = ch.digitValue();
        if (m_count > 0 || digit != 0) {
            m_count = m_count * 10 + digit;
            qCDebug(VIM_LOG) << "Normal: digit" << digit << "→ accumCount=" << m_count;
        } else {
            qCDebug(VIM_LOG) << "Normal: leading zero ignored";
        }
        return true;
    }

    const bool hadCount = m_count > 0;
    const int count = m_count > 0 ? m_count : 1;
    m_count = 0;
    qCDebug(VIM_LOG) << "Normal: effective count=" << count << "(hadCount=" << hadCount << ")";

    // Alt combinations — checked after count so prefix works (Alt+text is empty on X11)
    if (mods & Qt::AltModifier) {
        switch (qtKey) {
            case Qt::Key_J:
                qCDebug(VIM_LOG) << "Normal: Alt+J → moveRows +" << count;
                moveRows(+count); return true;
            case Qt::Key_K:
                qCDebug(VIM_LOG) << "Normal: Alt+K → moveRows -" << count;
                moveRows(-count); return true;
            default:
                qCDebug(VIM_LOG) << "Normal: unrecognised Alt combo qtKey=" << qtKey << ", consuming";
                return true;
        }
    }

    if (ch.isNull()) {
        qCDebug(VIM_LOG) << "Normal: empty text (modifier-only or unhandled key), consuming";
        return true;
    }

    // Two-key sequence completion
    if (!m_pendingKey.isNull()) {
        const QChar pending = m_pendingKey;
        m_pendingKey = {};
        qCDebug(VIM_LOG) << "Normal: completing two-key seq '" << pending << "' + '" << ch << "'";

        if (pending == u'g' && ch == u'g') { qCDebug(VIM_LOG) << "Normal: gg → jumpToFirst"; jumpToFirst();     return true; }
        if (pending == u'd' && ch == u'd') { qCDebug(VIM_LOG) << "Normal: dd count=" << count; deleteRows(count); return true; }
        if (pending == u'y' && ch == u'y') { qCDebug(VIM_LOG) << "Normal: yy count=" << count; yankRows(count);   return true; }

        qCDebug(VIM_LOG) << "Normal: incomplete two-key seq (pending='" << pending
                         << "'), processing '" << ch << "' standalone";
    }

    // Start of two-key sequences
    if (ch == u'g' || ch == u'd' || ch == u'y') {
        qCDebug(VIM_LOG) << "Normal: first key of two-key seq: '" << ch << "'";
        m_pendingKey = ch;
        return true;
    }

    // Single-key commands
    if (ch == u'j') { qCDebug(VIM_LOG) << "Normal: 'j' → moveCursor +" << count; moveCursor(+count); return true; }
    if (ch == u'k') { qCDebug(VIM_LOG) << "Normal: 'k' → moveCursor -" << count; moveCursor(-count); return true; }
    if (ch == u'G') {
        if (hadCount) { qCDebug(VIM_LOG) << "Normal: 'G' → jumpToRow" << (count - 1); jumpToRow(count - 1); }
        else          { qCDebug(VIM_LOG) << "Normal: 'G' → jumpToLast"; jumpToLast(); }
        return true;
    }
    if (ch == u'v') { qCDebug(VIM_LOG) << "Normal: 'v' → Visual mode";  enterVisual(); return true; }
    if (ch == u'p') { qCDebug(VIM_LOG) << "Normal: 'p' → pasteAfter";   pasteAfter();  return true; }
    if (ch == u'P') { qCDebug(VIM_LOG) << "Normal: 'P' → pasteBefore";  pasteBefore(); return true; }

    if (qtKey == Qt::Key_Return || qtKey == Qt::Key_Enter || ch == u'o') {
        qCDebug(VIM_LOG) << "Normal: Enter/o → activateCurrentRow";
        activateCurrentRow();
        return true;
    }

    qCDebug(VIM_LOG) << "Normal: unrecognised key '" << text << "' (qtKey=" << qtKey << "), consuming";
    return true;
}

bool VimHandler::handleVisualKey(QKeyEvent* ev)
{
    const Qt::KeyboardModifiers mods = ev->modifiers();
    const int qtKey = ev->key();
    const QString text = ev->text();
    const QChar ch = text.isEmpty() ? QChar{} : text.front();

    qCDebug(VIM_LOG) << "Visual key: text=" << text << "qtKey=" << qtKey
                     << "mods=" << mods << "accumCount=" << m_count
                     << "anchor=" << m_visualAnchor << "cursor=" << m_visualCursor;

    if (mods & Qt::ControlModifier) {
        qCDebug(VIM_LOG) << "Visual: Ctrl combo, consuming";
        return true;
    }

    if (qtKey == Qt::Key_Escape) {
        qCDebug(VIM_LOG) << "Visual: Esc → Normal";
        m_count = 0;
        enterNormal();
        return true;
    }

    // Count accumulation: bare digits only
    if (!ch.isNull() && ch.isDigit() && !(mods & ~Qt::KeypadModifier)) {
        const int digit = ch.digitValue();
        if (m_count > 0 || digit != 0) {
            m_count = m_count * 10 + digit;
            qCDebug(VIM_LOG) << "Visual: digit" << digit << "→ accumCount=" << m_count;
        }
        return true;
    }

    const int count = m_count > 0 ? m_count : 1;
    m_count = 0;
    qCDebug(VIM_LOG) << "Visual: effective count=" << count;

    // Alt combinations — move the whole selection
    if (mods & Qt::AltModifier) {
        switch (qtKey) {
            case Qt::Key_J:
                qCDebug(VIM_LOG) << "Visual: Alt+J → moveVisualSelection +" << count;
                moveVisualSelection(+count); return true;
            case Qt::Key_K:
                qCDebug(VIM_LOG) << "Visual: Alt+K → moveVisualSelection -" << count;
                moveVisualSelection(-count); return true;
            default:
                qCDebug(VIM_LOG) << "Visual: unrecognised Alt combo qtKey=" << qtKey << ", consuming";
                return true;
        }
    }

    if (ch.isNull()) {
        qCDebug(VIM_LOG) << "Visual: empty text, consuming";
        return true;
    }

    if (ch == u'j') {
        auto* view = m_viewLocator->activeView();
        const int last = (view && view->model()) ? view->model()->rowCount() - 1 : INT_MAX;
        const int newCursor = std::clamp(m_visualCursor + count, 0, qMax(0, last));
        qCDebug(VIM_LOG) << "Visual: 'j' → cursor" << m_visualCursor << "->" << newCursor
                         << "(last=" << last << ")";
        m_visualCursor = newCursor;
        updateVisualSelection();
        return true;
    }
    if (ch == u'k') {
        const int newCursor = qMax(0, m_visualCursor - count);
        qCDebug(VIM_LOG) << "Visual: 'k' → cursor" << m_visualCursor << "->" << newCursor;
        m_visualCursor = newCursor;
        updateVisualSelection();
        return true;
    }
    if (ch == u'o') {
        qCDebug(VIM_LOG) << "Visual: 'o' → swap anchor/cursor ("
                         << m_visualAnchor << "<->" << m_visualCursor << ")";
        std::swap(m_visualAnchor, m_visualCursor);
        updateVisualSelection();
        return true;
    }
    if (ch == u'd') {
        const int top = qMin(m_visualAnchor, m_visualCursor);
        const int bot = qMax(m_visualAnchor, m_visualCursor);
        const int numDeleted  = bot - top + 1;
        auto* delView = m_viewLocator->activeView();
        const int delCol = delView && delView->currentIndex().isValid()
                           ? delView->currentIndex().column() : 0;
        auto* delPlaylist = targetPlaylist();
        const int expectedRows = delPlaylist ? delPlaylist->trackCount() - numDeleted : 0;

        qCDebug(VIM_LOG) << "Visual: 'd' → deleteVisualSelection ["
                         << top << "," << bot << "] → Normal, expectedRows=" << expectedRows;
        deleteVisualSelection();
        enterNormal();

        if (delView && expectedRows > 0) {
            const int restoreRow = qMin(top, expectedRows - 1);
            scheduleIndexRestore(delView, restoreRow, delCol, expectedRows);
        }
        return true;
    }
    if (ch == u'y') {
        qCDebug(VIM_LOG) << "Visual: 'y' → yankVisualSelection [" << qMin(m_visualAnchor, m_visualCursor)
                         << "," << qMax(m_visualAnchor, m_visualCursor) << "] → Normal";
        yankVisualSelection();
        enterNormal();
        return true;
    }

    qCDebug(VIM_LOG) << "Visual: unrecognised key '" << text << "' (qtKey=" << qtKey << "), consuming";
    return true;
}

// ---------------------------------------------------------------------------
// Mode transitions
// ---------------------------------------------------------------------------

void VimHandler::enterNormal()
{
    qCInfo(VIM_LOG) << "Mode → Normal (from" << static_cast<int>(m_mode) << ")";
    if (m_mode == Mode::Visual) {
        // Collapse the visual selection to the cursor row so the paste target
        // remains highlighted after leaving Visual mode.
        auto* view = m_viewLocator->activeView();
        if (view && view->model() && view->selectionModel()) {
            const int row = qMax(0, m_visualCursor);
            const int col = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
            const QModelIndex idx = view->model()->index(row, col);
            view->selectionModel()->setCurrentIndex(
                idx.isValid() ? idx : view->currentIndex(),
                QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
    }
    m_mode = Mode::Normal;
    m_pendingKey = {};
    m_count = 0;
    m_visualAnchor = -1;
    m_visualCursor = -1;
    emit modeChanged(m_mode);
}

void VimHandler::enterInsert()
{
    qCInfo(VIM_LOG) << "Mode → Insert";
    m_mode = Mode::Insert;
    m_pendingKey = {};
    m_count = 0;
    emit modeChanged(m_mode);
}

void VimHandler::enterVisual()
{
    m_mode = Mode::Visual;
    auto* view = m_viewLocator->activeView();
    if (view && view->currentIndex().isValid()) {
        m_visualAnchor = view->currentIndex().row();
        m_visualCursor = m_visualAnchor;
    } else {
        m_visualAnchor = 0;
        m_visualCursor = 0;
    }
    qCInfo(VIM_LOG) << "Mode → Visual, anchor/cursor seeded at row" << m_visualAnchor;
    updateVisualSelection();
    emit modeChanged(m_mode);
}

// ---------------------------------------------------------------------------
// Cursor navigation
// ---------------------------------------------------------------------------

void VimHandler::moveCursor(int delta)
{
    auto* view = m_viewLocator->activeView();
    if (!view || !view->model()) {
        qCWarning(VIM_LOG) << "moveCursor: no active view or model";
        return;
    }
    const int last = view->model()->rowCount() - 1;
    if (last < 0) {
        qCDebug(VIM_LOG) << "moveCursor: view is empty";
        return;
    }
    const int col  = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
    const int row  = view->currentIndex().isValid() ? view->currentIndex().row() : 0;
    const int dest = std::clamp(row + delta, 0, last);
    qCDebug(VIM_LOG) << "moveCursor: row" << row << "->" << dest
                     << "(delta=" << delta << ", last=" << last << ")"
                     << "view=" << view->metaObject()->className();
    view->setCurrentIndex(view->model()->index(dest, col));
}

void VimHandler::jumpToFirst()
{
    auto* view = m_viewLocator->activeView();
    if (!view || !view->model() || view->model()->rowCount() == 0) {
        qCWarning(VIM_LOG) << "jumpToFirst: no active view or model is empty";
        return;
    }
    const int fromRow = view->currentIndex().isValid() ? view->currentIndex().row() : -1;
    const int col = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
    qCDebug(VIM_LOG) << "jumpToFirst: row" << fromRow << "-> 0";
    view->setCurrentIndex(view->model()->index(0, col));
}

void VimHandler::jumpToLast()
{
    auto* view = m_viewLocator->activeView();
    if (!view || !view->model()) {
        qCWarning(VIM_LOG) << "jumpToLast: no active view or model";
        return;
    }
    const int last = view->model()->rowCount() - 1;
    if (last < 0) {
        qCDebug(VIM_LOG) << "jumpToLast: view is empty";
        return;
    }
    const int fromRow = view->currentIndex().isValid() ? view->currentIndex().row() : -1;
    const int col = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
    qCDebug(VIM_LOG) << "jumpToLast: row" << fromRow << "->" << last;
    view->setCurrentIndex(view->model()->index(last, col));
}

void VimHandler::jumpToRow(int row)
{
    auto* view = m_viewLocator->activeView();
    if (!view || !view->model()) {
        qCWarning(VIM_LOG) << "jumpToRow: no active view or model";
        return;
    }
    const int last = view->model()->rowCount() - 1;
    if (last < 0) {
        qCDebug(VIM_LOG) << "jumpToRow: view is empty";
        return;
    }
    const int fromRow = view->currentIndex().isValid() ? view->currentIndex().row() : -1;
    const int col  = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
    const int dest = std::clamp(row, 0, last);
    qCDebug(VIM_LOG) << "jumpToRow: requested=" << row << "clamped=" << dest
                     << "from=" << fromRow;
    view->setCurrentIndex(view->model()->index(dest, col));
}

void VimHandler::moveCursorHalfPage(int direction)
{
    auto* view = m_viewLocator->activeView();
    if (!view) {
        qCWarning(VIM_LOG) << "moveCursorHalfPage: no active view";
        return;
    }
    const QModelIndex cur = view->currentIndex();
    int itemH = cur.isValid() ? view->visualRect(cur).height() : 0;
    if (itemH <= 0) itemH = 20;
    const int halfPage = qMax(1, view->height() / itemH / 2);
    qCDebug(VIM_LOG) << "moveCursorHalfPage: direction=" << direction
                     << "viewHeight=" << view->height()
                     << "itemHeight=" << itemH
                     << "halfPage=" << halfPage
                     << "effective delta=" << (direction * halfPage);
    moveCursor(direction * halfPage);
}

void VimHandler::activateCurrentRow()
{
    auto* view = m_viewLocator->activeView();
    if (!view) {
        qCWarning(VIM_LOG) << "activateCurrentRow: no active view";
        return;
    }
    const QModelIndex idx = view->currentIndex();
    if (!idx.isValid()) {
        qCWarning(VIM_LOG) << "activateCurrentRow: no valid current index";
        return;
    }
    qCDebug(VIM_LOG) << "activateCurrentRow: row=" << idx.row()
                     << "view=" << view->metaObject()->className();
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    m_suppressFilter = true;
    QCoreApplication::sendEvent(view, &ev);
    m_suppressFilter = false;
}

// ---------------------------------------------------------------------------
// Visual selection
// ---------------------------------------------------------------------------

void VimHandler::updateVisualSelection()
{
    auto* view = m_viewLocator->activeView();
    if (!view || !view->model() || !view->selectionModel()) {
        qCWarning(VIM_LOG) << "updateVisualSelection: no active view / model / selectionModel";
        return;
    }
    const int cols = view->model()->columnCount();
    const int top  = qMin(m_visualAnchor, m_visualCursor);
    const int bot  = qMax(m_visualAnchor, m_visualCursor);
    qCDebug(VIM_LOG) << "updateVisualSelection: rows [" << top << "," << bot << "]"
                     << "anchor=" << m_visualAnchor << "cursor=" << m_visualCursor
                     << "cols=" << cols;
    QItemSelection sel;
    sel.select(view->model()->index(top, 0),
               view->model()->index(bot, qMax(0, cols - 1)));
    view->selectionModel()->select(sel, QItemSelectionModel::ClearAndSelect);
    // Use NoUpdate so moving the current-item indicator does not implicitly
    // clear the multi-row selection we just applied.
    const int cursorCol = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
    view->selectionModel()->setCurrentIndex(
        view->model()->index(m_visualCursor, cursorCol),
        QItemSelectionModel::NoUpdate);
}

// ---------------------------------------------------------------------------
// PlaylistHandler wiring
// ---------------------------------------------------------------------------

void VimHandler::setPlaylistHandler(Fooyin::PlaylistHandler* handler)
{
    qCDebug(VIM_LOG) << "setPlaylistHandler:" << (handler ? "set" : "cleared");
    m_playlistHandler = handler;
}

Fooyin::Playlist* VimHandler::targetPlaylist() const
{
    if (!m_playlistHandler) return nullptr;

    // Prefer the currently playing playlist.
    if (auto* p = m_playlistHandler->activePlaylist()) {
        qCDebug(VIM_LOG) << "targetPlaylist: active (playing):" << p->name();
        return p;
    }

    // Fall back: find a playlist whose track count matches the view's row count.
    auto* view = m_viewLocator->activeView();
    const int viewRows = (view && view->model()) ? view->model()->rowCount() : -1;

    for (auto* p : m_playlistHandler->playlists()) {
        if (p && (viewRows < 0 || p->trackCount() == viewRows)) {
            qCDebug(VIM_LOG) << "targetPlaylist: matched by row count (" << viewRows
                             << "):" << p->name();
            return p;
        }
    }

    // Last resort: first available playlist.
    const auto all = m_playlistHandler->playlists();
    if (!all.empty()) {
        qCDebug(VIM_LOG) << "targetPlaylist: using first available:" << all.front()->name();
        return all.front();
    }

    qCWarning(VIM_LOG) << "targetPlaylist: no playlist found";
    return nullptr;
}

void VimHandler::scheduleIndexRestore(QAbstractItemView* view, int row, int col,
                                      int expectedRowCount)
{
    if (!view || !view->model() || !view->selectionModel()) return;

    QPointer<QAbstractItemView> viewPtr{view};

    // Restore the cursor once the model reaches expectedRowCount rows.
    // The row-count guard prevents firing prematurely if a stale signal arrives.
    auto restore = [viewPtr, row, col, expectedRowCount]() {
        if (!viewPtr || !viewPtr->model()) return;
        if (viewPtr->model()->rowCount() != expectedRowCount) {
            qCDebug(VIM_LOG) << "scheduleIndexRestore: rowCount"
                             << viewPtr->model()->rowCount() << "!= expected"
                             << expectedRowCount << ", skipping";
            return;
        }
        const QModelIndex idx = viewPtr->model()->index(row, col);
        if (!idx.isValid()) return;
        viewPtr->selectionModel()->setCurrentIndex(
            idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        qCDebug(VIM_LOG) << "scheduleIndexRestore: cursor restored to row" << row;
    };

    // Attempt 1: after the sync in-memory modelReset's handlers complete.
    QTimer::singleShot(0, this, restore);

    // Attempt 2: fooyin emits a second modelReset when the async DB write finishes.
    // Catch it once and defer the restore by one more event-loop tick so the view
    // finishes processing the reset before we set the index.
    QObject::connect(view->model(), &QAbstractItemModel::modelReset,
        view->model(),
        [restore, viewPtr]() mutable {
            qCDebug(VIM_LOG) << "scheduleIndexRestore: caught async modelReset, deferring restore";
            QTimer::singleShot(0, viewPtr, restore);
        },
        Qt::SingleShotConnection);
}

// ---------------------------------------------------------------------------
// Yank / delete / paste  (playlist views only; no-op for other view types)
// ---------------------------------------------------------------------------

void VimHandler::yankRows(int count)
{
    auto* view = m_viewLocator->activeView();
    if (!view || !m_playlistHandler) {
        qCWarning(VIM_LOG) << "yankRows: no active view or no PlaylistHandler";
        return;
    }
    auto* playlist = targetPlaylist();
    if (!playlist) {
        qCWarning(VIM_LOG) << "yankRows: no playlist found";
        return;
    }
    const int row = view->currentIndex().isValid() ? view->currentIndex().row() : 0;
    const auto& all = playlist->tracks();
    const int end = std::clamp(row + count, 0, static_cast<int>(all.size()));
    const int actualCount = end - row;
    qCDebug(VIM_LOG) << "yankRows: playlist=" << playlist->name()
                     << "startRow=" << row << "requestedCount=" << count
                     << "actualCount=" << actualCount;
    m_clipboard.yank(Fooyin::TrackList(all.begin() + row, all.begin() + end));

    // Re-assert the selection so the cursor row stays highlighted as the paste target.
    if (view->selectionModel() && view->currentIndex().isValid())
        view->selectionModel()->setCurrentIndex(
            view->currentIndex(),
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

void VimHandler::deleteRows(int count)
{
    auto* view = m_viewLocator->activeView();
    if (!view || !m_playlistHandler) {
        qCWarning(VIM_LOG) << "deleteRows: no active view or no PlaylistHandler";
        return;
    }
    auto* playlist = targetPlaylist();
    if (!playlist) {
        qCWarning(VIM_LOG) << "deleteRows: no playlist found";
        return;
    }
    const int row         = view->currentIndex().isValid() ? view->currentIndex().row() : 0;
    const int col         = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
    const int beforeCount = playlist->trackCount();
    const int end         = std::min(row + count, beforeCount);
    const int numDeleted  = end - row;
    const int expectedRows = beforeCount - numDeleted;
    const int restoreRow  = (expectedRows > 0) ? qMin(row, expectedRows - 1) : 0;

    qCDebug(VIM_LOG) << "deleteRows: playlist=" << playlist->name()
                     << "rows [" << row << "," << (end - 1) << "]"
                     << "count=" << numDeleted
                     << "restoreRow=" << restoreRow;
    yankRows(count);
    std::vector<int> indices;
    indices.reserve(static_cast<size_t>(numDeleted));
    for (int i = row; i < end; ++i)
        indices.push_back(i);
    m_playlistHandler->removePlaylistTracks(playlist->id(), indices);

    if (expectedRows > 0)
        scheduleIndexRestore(view, restoreRow, col, expectedRows);
}

void VimHandler::yankVisualSelection()
{
    if (!m_playlistHandler) {
        qCWarning(VIM_LOG) << "yankVisualSelection: no PlaylistHandler";
        return;
    }
    auto* playlist = targetPlaylist();
    if (!playlist) {
        qCWarning(VIM_LOG) << "yankVisualSelection: no playlist found";
        return;
    }
    const int top = qMin(m_visualAnchor, m_visualCursor);
    const int bot = qMax(m_visualAnchor, m_visualCursor);
    const auto& all = playlist->tracks();
    const int end = std::min(bot + 1, static_cast<int>(all.size()));
    if (top >= static_cast<int>(all.size())) {
        qCWarning(VIM_LOG) << "yankVisualSelection: selection out of range"
                           << "(top=" << top << "size=" << all.size() << ")";
        return;
    }
    qCDebug(VIM_LOG) << "yankVisualSelection: playlist=" << playlist->name()
                     << "rows [" << top << "," << (end - 1) << "]"
                     << "trackCount=" << (end - top);
    m_clipboard.yank(Fooyin::TrackList(all.begin() + top, all.begin() + end));
}

void VimHandler::deleteVisualSelection()
{
    if (!m_playlistHandler) {
        qCWarning(VIM_LOG) << "deleteVisualSelection: no PlaylistHandler";
        return;
    }
    auto* playlist = targetPlaylist();
    if (!playlist) {
        qCWarning(VIM_LOG) << "deleteVisualSelection: no active playlist";
        return;
    }
    const int top = qMin(m_visualAnchor, m_visualCursor);
    const int bot = qMax(m_visualAnchor, m_visualCursor);
    const int size = playlist->trackCount();
    qCDebug(VIM_LOG) << "deleteVisualSelection: playlist=" << playlist->name()
                     << "rows [" << top << "," << qMin(bot, size - 1) << "]";
    yankVisualSelection();
    std::vector<int> indices;
    for (int i = top; i <= bot && i < size; ++i)
        indices.push_back(i);
    m_playlistHandler->removePlaylistTracks(playlist->id(), indices);
}

void VimHandler::pasteAfter()
{
    if (!m_clipboard.hasData() || !m_playlistHandler) {
        qCWarning(VIM_LOG) << "pasteAfter: clipboard empty or no PlaylistHandler";
        return;
    }
    auto* view = m_viewLocator->activeView();
    if (!view) {
        qCWarning(VIM_LOG) << "pasteAfter: no active view";
        return;
    }
    auto* playlist = targetPlaylist();
    if (!playlist) {
        qCWarning(VIM_LOG) << "pasteAfter: no active playlist";
        return;
    }
    const int originalRow = view->currentIndex().isValid() ? view->currentIndex().row() : 0;
    const int col         = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
    const int targetRow   = originalRow + 1;
    Fooyin::TrackList all = playlist->tracks();
    const int insertPos   = std::clamp(targetRow, 0, static_cast<int>(all.size()));
    qCDebug(VIM_LOG) << "pasteAfter: playlist=" << playlist->name()
                     << "originalRow=" << originalRow << "insertPos=" << insertPos
                     << "trackCount=" << m_clipboard.tracks().size();
    all.insert(all.begin() + insertPos,
               m_clipboard.tracks().begin(), m_clipboard.tracks().end());
    m_playlistHandler->replacePlaylistTracks(playlist->id(), all);
    scheduleIndexRestore(view, originalRow, col, static_cast<int>(all.size()));
}

void VimHandler::pasteBefore()
{
    if (!m_clipboard.hasData() || !m_playlistHandler) {
        qCWarning(VIM_LOG) << "pasteBefore: clipboard empty or no PlaylistHandler";
        return;
    }
    auto* view = m_viewLocator->activeView();
    if (!view) {
        qCWarning(VIM_LOG) << "pasteBefore: no active view";
        return;
    }
    auto* playlist = targetPlaylist();
    if (!playlist) {
        qCWarning(VIM_LOG) << "pasteBefore: no active playlist";
        return;
    }
    const int originalRow = view->currentIndex().isValid() ? view->currentIndex().row() : 0;
    const int col         = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
    Fooyin::TrackList all = playlist->tracks();
    const int insertPos   = std::clamp(originalRow, 0, static_cast<int>(all.size()));
    qCDebug(VIM_LOG) << "pasteBefore: playlist=" << playlist->name()
                     << "originalRow=" << originalRow << "insertPos=" << insertPos
                     << "trackCount=" << m_clipboard.tracks().size();
    all.insert(all.begin() + insertPos,
               m_clipboard.tracks().begin(), m_clipboard.tracks().end());
    m_playlistHandler->replacePlaylistTracks(playlist->id(), all);
    scheduleIndexRestore(view, originalRow, col, static_cast<int>(all.size()));
}

// ---------------------------------------------------------------------------
// Row / selection move (playlist views only)
// ---------------------------------------------------------------------------

void VimHandler::moveRows(int delta)
{
    if (!m_playlistHandler) {
        qCWarning(VIM_LOG) << "moveRows: no PlaylistHandler";
        return;
    }
    auto* view = m_viewLocator->activeView();
    if (!view || !view->model()) {
        qCWarning(VIM_LOG) << "moveRows: no active view";
        return;
    }
    auto* playlist = targetPlaylist();
    if (!playlist) {
        qCWarning(VIM_LOG) << "moveRows: no active playlist";
        return;
    }

    const int rowCount = view->model()->rowCount();
    if (rowCount == 0) {
        qCDebug(VIM_LOG) << "moveRows: view is empty";
        return;
    }
    const int row = view->currentIndex().isValid() ? view->currentIndex().row() : 0;
    const int col = view->currentIndex().isValid() ? view->currentIndex().column() : 0;

    Fooyin::TrackList all = playlist->tracks();
    const Fooyin::Track moved = all[static_cast<size_t>(row)];
    all.erase(all.begin() + row);
    const int insertPos = std::clamp(row + delta, 0, static_cast<int>(all.size()));
    qCDebug(VIM_LOG) << "moveRows: playlist=" << playlist->name()
                     << "row" << row << "->" << insertPos
                     << "(delta=" << delta << ")"
                     << "track=" << moved.title();
    all.insert(all.begin() + insertPos, moved);

    m_playlistHandler->replacePlaylistTracks(playlist->id(), all);
    scheduleIndexRestore(view, insertPos, col, static_cast<int>(all.size()));
}

void VimHandler::moveVisualSelection(int delta)
{
    if (!m_playlistHandler) {
        qCWarning(VIM_LOG) << "moveVisualSelection: no PlaylistHandler";
        return;
    }
    auto* view = m_viewLocator->activeView();
    if (!view || !view->model()) {
        qCWarning(VIM_LOG) << "moveVisualSelection: no active view";
        return;
    }
    auto* playlist = targetPlaylist();
    if (!playlist) {
        qCWarning(VIM_LOG) << "moveVisualSelection: no active playlist";
        return;
    }

    const int top = qMin(m_visualAnchor, m_visualCursor);
    const int bot = qMax(m_visualAnchor, m_visualCursor);
    const int selSize = bot - top + 1;

    Fooyin::TrackList all = playlist->tracks();
    if (bot >= static_cast<int>(all.size())) {
        qCWarning(VIM_LOG) << "moveVisualSelection: selection out of range"
                           << "(bot=" << bot << "size=" << all.size() << ")";
        return;
    }

    Fooyin::TrackList moved(all.begin() + top, all.begin() + bot + 1);
    all.erase(all.begin() + top, all.begin() + bot + 1);
    const int insertPos = std::clamp(top + delta, 0, static_cast<int>(all.size()));
    qCDebug(VIM_LOG) << "moveVisualSelection: playlist=" << playlist->name()
                     << "rows [" << top << "," << bot << "] (size=" << selSize << ")"
                     << "->" << insertPos << "(delta=" << delta << ")";
    all.insert(all.begin() + insertPos, moved.begin(), moved.end());

    m_playlistHandler->replacePlaylistTracks(playlist->id(), all);

    const bool anchorFirst = (m_visualAnchor <= m_visualCursor);
    m_visualAnchor = anchorFirst ? insertPos : insertPos + selSize - 1;
    m_visualCursor = anchorFirst ? insertPos + selSize - 1 : insertPos;
    qCDebug(VIM_LOG) << "moveVisualSelection: new anchor=" << m_visualAnchor
                     << "cursor=" << m_visualCursor;
    updateVisualSelection();
}

} // namespace Fooyin::VimMotions
