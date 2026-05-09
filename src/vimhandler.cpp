#include "vimhandler.h"
#include "spatialnavigator.h"
#include "vimsearchbar.h"
#include "viewlocator.h"
#include "vimbindingparser.h"
#include "vimmotionssettings.h"
#include "vimlog.h"

#include <core/playlist/playlist.h>
#include <core/playlist/playlisthandler.h>
#include <core/plugins/coreplugincontext.h>
#include <gui/fywidget.h>
#include <gui/guiconstants.h>
#include <utils/actions/actionmanager.h>
#include <utils/actions/command.h>
#include <utils/settings/settingsmanager.h>

#include <QAbstractItemView>
#include <QCoreApplication>
#include <QItemSelection>
#include <QKeyEvent>
#include <QPointer>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QTreeView>
#include <algorithm>
#include <utility>

Q_LOGGING_CATEGORY(VIM_LOG, "fy.vim")

namespace Fooyin::VimMotions {

static QTreeView* asTreeView(QAbstractItemView* v)
{
    return qobject_cast<QTreeView*>(v);
}

VimHandler::VimHandler(QObject* parent)
    : QObject{parent}
    , m_viewLocator{new ViewLocator(this)}
    , m_spatialNavigator{new SpatialNavigator(this)}
{
    m_actions.registerAll();
    qCDebug(VIM_LOG) << "VimHandler created";
}

VimHandler::~VimHandler()
{
    delete m_filterBar;
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
    // and delivers the normal KeyPress instead. We only claim keys that vim
    // actually handles so unrecognised shortcuts pass through normally.
    if (type == QEvent::ShortcutOverride) {
        auto* kev = static_cast<QKeyEvent*>(event);
        qCDebug(VIM_LOG) << "eventFilter ShortcutOverride: key=" << kev->key()
                         << "text=" << kev->text() << "mods=" << kev->modifiers()
                         << "mode=" << static_cast<int>(m_mode)
                         << "watched=" << watched->metaObject()->className();
        bool claim = false;
        if (m_useConfigBindings) {
            if (m_mode == Mode::Normal || m_mode == Mode::Visual) {
                claim = wouldHandleFromConfig(kev, m_mode);
            } else if (m_mode == Mode::Insert) {
                const auto key = kev->key();
                const auto mods = kev->modifiers();
                if (key == Qt::Key_Escape && mods == Qt::NoModifier) {
                    claim = true;
                } else if (key != Qt::Key_Escape
                           && !m_configBindings.value(Mode::Insert).isEmpty()
                           && wouldHandleFromConfig(kev, Mode::Insert)) {
                    claim = true;
                }
            }
        } else {
            if (m_mode == Mode::Normal)
                claim = wouldHandleNormal(kev);
            else if (m_mode == Mode::Visual)
                claim = wouldHandleVisual(kev);
        }
        if (claim) {
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
    if (m_useConfigBindings) {
        switch (m_mode) {
            case Mode::Insert:
                return dispatchFromConfig(ev, Mode::Insert);
            case Mode::Normal:
                return dispatchFromConfig(ev, Mode::Normal);
            case Mode::Visual:
                return dispatchFromConfig(ev, Mode::Visual);
            case Mode::Filter:
            case Mode::Search:
                return false;
        }
        return false;
    }

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
        case Mode::Filter:
            return false;
        case Mode::Search:
            return false;
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

    // Ctrl+Shift combinations — must come before the plain-Ctrl block
    if ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier)) {
        switch (qtKey) {
            case Qt::Key_J:
                qCDebug(VIM_LOG) << "Normal: Ctrl+Shift+J → treeMoveSibling +1";
                treeMoveSibling(+1); return true;
            case Qt::Key_K:
                qCDebug(VIM_LOG) << "Normal: Ctrl+Shift+K → treeMoveSibling -1";
                treeMoveSibling(-1); return true;
            default:
                return false;
        }
    }

    // Ctrl combinations — consumed before count accumulation
    if (mods & Qt::ControlModifier) {
        switch (qtKey) {
            case Qt::Key_J:
                qCDebug(VIM_LOG) << "Normal: Ctrl+J → focus Down";
                m_spatialNavigator->moveFocus(Direction::Down,  m_viewLocator->activeView()); return true;
            case Qt::Key_K:
                qCDebug(VIM_LOG) << "Normal: Ctrl+K → focus Up";
                m_spatialNavigator->moveFocus(Direction::Up,    m_viewLocator->activeView()); return true;
            case Qt::Key_H:
                qCDebug(VIM_LOG) << "Normal: Ctrl+H → focus Left";
                m_spatialNavigator->moveFocus(Direction::Left,  m_viewLocator->activeView()); return true;
            case Qt::Key_L:
                qCDebug(VIM_LOG) << "Normal: Ctrl+L → focus Right";
                m_spatialNavigator->moveFocus(Direction::Right, m_viewLocator->activeView()); return true;
            case Qt::Key_D:
                qCDebug(VIM_LOG) << "Normal: Ctrl+D → half page down";
                moveCursorHalfPage(+1); return true;
            case Qt::Key_U:
                qCDebug(VIM_LOG) << "Normal: Ctrl+U → half page up";
                moveCursorHalfPage(-1); return true;
            case Qt::Key_R:
                qCDebug(VIM_LOG) << "Normal: Ctrl+R → redo";
                redo(); return true;
            case Qt::Key_I:
                qCDebug(VIM_LOG) << "Normal: Ctrl+I → enterFilter";
                enterFilter(); return true;
            default:
                qCDebug(VIM_LOG) << "Normal: unrecognised Ctrl combo qtKey=" << qtKey << ", passing through";
                return false;
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
                qCDebug(VIM_LOG) << "Visual: unrecognised Alt combo qtKey=" << qtKey << ", passing through";
                return false;
    }
    }

    if (ch.isNull()) return false;

    // Two-key sequence completion
    if (!m_pendingKey.isNull()) {
        const QChar pending = m_pendingKey;
        m_pendingKey = {};
        qCDebug(VIM_LOG) << "Normal: completing two-key seq '" << pending << "' + '" << ch << "'";
        if (pending == u'g' && ch == u'g') { qCDebug(VIM_LOG) << "Normal: gg → jumpToFirst"; jumpToFirst();            return true; }
        if (pending == u'g' && ch == u';') { qCDebug(VIM_LOG) << "Normal: g; → focusNowPlaying"; focusNowPlaying();      return true; }
        if (pending == u'd' && ch == u'd') { qCDebug(VIM_LOG) << "Normal: dd count=" << count; deleteRows(count);       return true; }
        if (pending == u'y' && ch == u'y') { qCDebug(VIM_LOG) << "Normal: yy count=" << count; yankRows(count);         return true; }
        qCDebug(VIM_LOG) << "Normal: incomplete two-key seq (pending='" << pending << "'), processing '" << ch << "' standalone";
    }

    // Start of two-key sequences
    if (ch == u'g' || ch == u'd' || ch == u'y') {
        qCDebug(VIM_LOG) << "Normal: first key of two-key seq: '" << ch << "'";
        m_pendingKey = ch;
        return true;
    }

    if (ch == u'j') { qCDebug(VIM_LOG) << "Normal: 'j' → moveCursor +" << count; moveCursor(+count); return true; }
    if (ch == u'k') { qCDebug(VIM_LOG) << "Normal: 'k' → moveCursor -" << count; moveCursor(-count); return true; }
    if (ch == u'l') {
        if (asTreeView(m_viewLocator->activeView())) {
            qCDebug(VIM_LOG) << "Normal: 'l' → treeOpenOrDescend";
            treeOpenOrDescend();
            return true;
        }
        return false;
    }
    if (ch == u'h') {
        if (asTreeView(m_viewLocator->activeView())) {
            qCDebug(VIM_LOG) << "Normal: 'h' → treeCloseOrAscend";
            treeCloseOrAscend();
            return true;
        }
        return false;
    }
    if (ch == u'G') {
        if (hadCount) { qCDebug(VIM_LOG) << "Normal: 'G' → jumpToRow" << (count - 1); jumpToRow(count - 1); }
        else          { qCDebug(VIM_LOG) << "Normal: 'G' → jumpToLast"; jumpToLast(); }
        return true;
    }

    if (ch == u'o') { qCDebug(VIM_LOG) << "Normal: 'o' → focusNowPlaying"; focusNowPlaying(); return true; }
    if (ch == u'v') { qCDebug(VIM_LOG) << "Normal: 'v' → Visual mode";  enterVisual(); return true; }
    if (ch == u'p') { qCDebug(VIM_LOG) << "Normal: 'p' → pasteAfter";   pasteAfter();  return true; }
    if (ch == u'P') { qCDebug(VIM_LOG) << "Normal: 'P' → pasteBefore";  pasteBefore(); return true; }
    if (ch == u'u') { qCDebug(VIM_LOG) << "Normal: 'u' → undo";         undo();        return true; }

    if (ch == u'u') { qCDebug(VIM_LOG) << "Normal: 'u' → undo"; undo(); return true; }

    if (ch == u'/') { qCDebug(VIM_LOG) << "Normal: '/' → enterSearch"; enterSearch(); return true; }
    if (ch == u'n') { qCDebug(VIM_LOG) << "Normal: 'n' → nextMatch"; nextMatch(); return true; }
    if (ch == u'N') { qCDebug(VIM_LOG) << "Normal: 'N' → prevMatch"; prevMatch(); return true; }

    qCDebug(VIM_LOG) << "Normal: unrecognised key '" << text << "' (qtKey=" << qtKey << "), passing through";
    return false;
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

    // Ctrl combinations — Ctrl+D/U for half-page scroll extending selection
    if (mods & Qt::ControlModifier) {
        switch (qtKey) {
            case Qt::Key_D: {
                qCDebug(VIM_LOG) << "Visual: Ctrl+D → half page down";
                auto* view = m_viewLocator->activeView();
                if (view && view->model()) {
                    const int last = view->model()->rowCount() - 1;
                    m_visualCursor = std::clamp(m_visualCursor + halfPageDelta(), 0, qMax(0, last));
                    updateVisualSelection();
                }
                return true;
            }
            case Qt::Key_U: {
                qCDebug(VIM_LOG) << "Visual: Ctrl+U → half page up";
                m_visualCursor = qMax(0, m_visualCursor - halfPageDelta());
                updateVisualSelection();
                return true;
            }
            default:
                qCDebug(VIM_LOG) << "Visual: unrecognised Ctrl combo qtKey=" << qtKey << ", passing through";
                return false;
        }
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
    const bool hadCount = m_count > 0;
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
                qCDebug(VIM_LOG) << "Visual: unrecognised Alt combo qtKey=" << qtKey << ", passing through";
                return false;
        }
    }

    if (ch.isNull()) {
        qCDebug(VIM_LOG) << "Visual: empty text, passing through";
        return false;
    }

    // Two-key sequence completion
    if (!m_pendingKey.isNull()) {
        const QChar pending = m_pendingKey;
        m_pendingKey = {};
        qCDebug(VIM_LOG) << "Visual: completing two-key seq '" << pending << "' + '" << ch << "'";
        if (pending == u'g' && ch == u'g') {
            qCDebug(VIM_LOG) << "Visual: gg → extend selection to first row";
            m_visualCursor = 0;
            updateVisualSelection();
            return true;
        }
        if (pending == u'g' && ch == u';') {
            qCDebug(VIM_LOG) << "Visual: g; → focusNowPlaying";
            enterNormal();
            focusNowPlaying();
            return true;
        }
        qCDebug(VIM_LOG) << "Visual: incomplete two-key seq (pending='" << pending << "'), ignoring";
        return true;
    }

    // Start of two-key sequences
    if (ch == u'g') {
        qCDebug(VIM_LOG) << "Visual: first key of two-key seq: '" << ch << "'";
        m_pendingKey = ch;
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
    if (ch == u'G') {
        auto* view = m_viewLocator->activeView();
        if (view && view->model()) {
            const int last = view->model()->rowCount() - 1;
            if (last >= 0) {
                m_visualCursor = hadCount ? std::clamp(count - 1, 0, last) : last;
                qCDebug(VIM_LOG) << "Visual: 'G' → cursor" << m_visualCursor << "(last=" << last << ")";
                updateVisualSelection();
            }
        }
        return true;
    }
    if (ch == u'l') {
        if (asTreeView(m_viewLocator->activeView())) {
            qCDebug(VIM_LOG) << "Visual: 'l' → treeOpenOrDescend";
            treeOpenOrDescend();
            return true;
        }
        return false;
    }
    if (ch == u'h') {
        if (asTreeView(m_viewLocator->activeView())) {
            qCDebug(VIM_LOG) << "Visual: 'h' → treeCloseOrAscend";
            treeCloseOrAscend();
            return true;
        }
        return false;
    }
    if (ch == u'n') {
        qCDebug(VIM_LOG) << "Visual: 'n' → nextMatch (exit visual)";
        enterNormal();
        nextMatch();
        return true;
    }
    if (ch == u'N') {
        qCDebug(VIM_LOG) << "Visual: 'N' → prevMatch (exit visual)";
        enterNormal();
        prevMatch();
        return true;
    }
    if (ch == u'/') {
        qCDebug(VIM_LOG) << "Visual: '/' → enterSearch (exit visual)";
        m_count = 0;
        enterNormal();
        enterSearch();
        return true;
    }
    if (ch == u'd') {
        qCDebug(VIM_LOG) << "Visual: 'd' → deleteVisualSelection → Normal";
        deleteVisualSelection();
        return true;
    }
    if (ch == u'y') {
        qCDebug(VIM_LOG) << "Visual: 'y' → yankVisualSelection [" << qMin(m_visualAnchor, m_visualCursor)
                         << "," << qMax(m_visualAnchor, m_visualCursor) << "] → Normal";
        yankVisualSelection();
        enterNormal();
        return true;
    }

    qCDebug(VIM_LOG) << "Visual: unrecognised key '" << text << "' (qtKey=" << qtKey << "), passing through";
    return false;
}

// ---------------------------------------------------------------------------
// ShortcutOverride predicates — mirror handleNormal/VisualKey without side effects
// ---------------------------------------------------------------------------

bool VimHandler::wouldHandleNormal(QKeyEvent* kev) const
{
    const Qt::KeyboardModifiers mods = kev->modifiers();
    const int key = kev->key();
    const QChar ch = kev->text().isEmpty() ? QChar{} : kev->text().front();

    if (mods & Qt::ControlModifier)
        return key == Qt::Key_J || key == Qt::Key_K || key == Qt::Key_H
            || key == Qt::Key_L || key == Qt::Key_D || key == Qt::Key_U || key == Qt::Key_R
            || key == Qt::Key_I;

    if (key == Qt::Key_Escape) return true;
    if (ch == u'i') return true;
    if (!ch.isNull() && ch.isDigit() && !(mods & ~Qt::KeypadModifier)) return true;

    if (mods & Qt::AltModifier)
        return key == Qt::Key_J || key == Qt::Key_K;

    if (ch.isNull()) return false;

    // Any printable char while a two-key sequence is pending completes it.
    if (!m_pendingKey.isNull()) return true;

    if (ch == u'g' || ch == u'd' || ch == u'y') return true;
    if (ch == u'j' || ch == u'k' || ch == u'G' || ch == u'o' || ch == u'v') return true;
    if (ch == u'l' || ch == u'h') return true;
    if (ch == u'p' || ch == u'P') return true;
    if (ch == u'u') return true;
    if (ch == u'/' || ch == u'n' || ch == u'N') return true;

    return false;
}

bool VimHandler::wouldHandleVisual(QKeyEvent* kev) const
{
    const Qt::KeyboardModifiers mods = kev->modifiers();
    const int key = kev->key();
    const QChar ch = kev->text().isEmpty() ? QChar{} : kev->text().front();

    if (mods & Qt::ControlModifier)
        return key == Qt::Key_D || key == Qt::Key_U;

    if (key == Qt::Key_Escape) return true;
    if (!ch.isNull() && ch.isDigit() && !(mods & ~Qt::KeypadModifier)) return true;

    if (mods & Qt::AltModifier)
        return key == Qt::Key_J || key == Qt::Key_K;

    if (ch.isNull()) return false;

    if (!m_pendingKey.isNull()) return true;

    if (ch == u'g' || ch == u'd' || ch == u'y') return true;
    if (ch == u'j' || ch == u'k' || ch == u'o') return true;
    if (ch == u'G' || ch == u'l' || ch == u'h') return true;
    if (ch == u'n' || ch == u'N') return true;
    if (ch == u'/') return true;

    return false;
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
            qCDebug(VIM_LOG) << "enterNormal (from Visual): collapsing selection to row" << row << "col" << col;
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

    if (auto* tree = asTreeView(view)) {
        treeMoveCursor(tree, delta);
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
    view->selectionModel()->setCurrentIndex(
        view->model()->index(dest, col),
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
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
    view->selectionModel()->setCurrentIndex(
        view->model()->index(0, col),
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

void VimHandler::jumpToLast()
{
    auto* view = m_viewLocator->activeView();
    if (!view || !view->model()) {
        qCWarning(VIM_LOG) << "jumpToLast: no active view or model";
        return;
    }

    if (auto* tree = asTreeView(view)) {
        QModelIndex cur = tree->model()->index(0, 0);
        if (!cur.isValid()) return;
        while (true) {
            const QModelIndex next = tree->indexBelow(cur);
            if (!next.isValid()) break;
            cur = next;
        }
        qCDebug(VIM_LOG) << "jumpToLast (tree): last visible item row=" << cur.row();
        tree->selectionModel()->setCurrentIndex(
            cur, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
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
    view->selectionModel()->setCurrentIndex(
        view->model()->index(last, col),
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
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
    view->selectionModel()->setCurrentIndex(
        view->model()->index(dest, col),
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

int VimHandler::halfPageDelta() const
{
    auto* view = m_viewLocator->activeView();
    if (!view) return 1;
    const QModelIndex cur = view->currentIndex();
    int itemH = cur.isValid() ? view->visualRect(cur).height() : 0;
    if (itemH <= 0) itemH = 20;
    return qMax(1, view->height() / itemH / 2);
}

void VimHandler::moveCursorHalfPage(int direction)
{
    const int halfPage = halfPageDelta();
    qCDebug(VIM_LOG) << "moveCursorHalfPage: direction=" << direction
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
// Tree navigation (PlaylistOrganiser)
// ---------------------------------------------------------------------------

void VimHandler::treeMoveCursor(QTreeView* tree, int delta)
{
    QModelIndex cur = tree->currentIndex();
    if (!cur.isValid())
        cur = tree->model()->index(0, 0);
    if (!cur.isValid()) return;

    const int steps = std::abs(delta);
    for (int i = 0; i < steps; ++i) {
        const QModelIndex next = (delta > 0) ? tree->indexBelow(cur)
                                             : tree->indexAbove(cur);
        if (!next.isValid()) break;
        cur = next;
    }
    qCDebug(VIM_LOG) << "treeMoveCursor: delta=" << delta << "→ row=" << cur.row();
    tree->selectionModel()->setCurrentIndex(
        cur, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

void VimHandler::treeMoveSibling(int delta)
{
    auto* tree = asTreeView(m_viewLocator->activeView());
    if (!tree) return;

    const QModelIndex cur = tree->currentIndex();
    if (!cur.isValid()) return;

    const int targetRow = cur.row() + delta;
    if (targetRow < 0) return;

    const QModelIndex sibling = tree->model()->index(targetRow, 0, cur.parent());
    if (!sibling.isValid()) return;

    qCDebug(VIM_LOG) << "treeMoveSibling: delta=" << delta
                     << "row" << cur.row() << "->" << targetRow;
    tree->selectionModel()->setCurrentIndex(
        sibling, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

void VimHandler::treeOpenOrDescend()
{
    auto* tree = asTreeView(m_viewLocator->activeView());
    if (!tree) return;

    const QModelIndex cur = tree->currentIndex();
    if (!cur.isValid()) return;

    if (!tree->isExpanded(cur)) {
        qCDebug(VIM_LOG) << "treeOpenOrDescend: expanding row=" << cur.row();
        tree->expand(cur);
    } else {
        const QModelIndex child = tree->model()->index(0, 0, cur);
        if (child.isValid()) {
            qCDebug(VIM_LOG) << "treeOpenOrDescend: descending to first child";
            tree->selectionModel()->setCurrentIndex(
                child, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        } else {
            qCDebug(VIM_LOG) << "treeOpenOrDescend: already expanded leaf, no-op";
        }
    }
}

void VimHandler::treeCloseOrAscend()
{
    auto* tree = asTreeView(m_viewLocator->activeView());
    if (!tree) return;

    const QModelIndex cur = tree->currentIndex();
    if (!cur.isValid()) return;

    if (tree->isExpanded(cur)) {
        qCDebug(VIM_LOG) << "treeCloseOrAscend: collapsing row=" << cur.row();
        tree->collapse(cur);
    } else {
        const QModelIndex parent = cur.parent();
        if (parent.isValid()) {
            qCDebug(VIM_LOG) << "treeCloseOrAscend: ascending to parent";
            tree->selectionModel()->setCurrentIndex(
                parent, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        } else {
            qCDebug(VIM_LOG) << "treeCloseOrAscend: already at root, no-op";
        }
    }
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

void VimHandler::setActionManager(Fooyin::ActionManager* manager)
{
    qCDebug(VIM_LOG) << "setActionManager:" << (manager ? "set" : "cleared");
    m_actionManager = manager;
}

void VimHandler::focusNowPlaying()
{
    qCDebug(VIM_LOG) << "focusNowPlaying";
    if (!m_actionManager) {
        qCWarning(VIM_LOG) << "focusNowPlaying: no ActionManager";
        return;
    }

    Fooyin::Command* cmd = m_actionManager->command(Fooyin::Id(Constants::Actions::ShowNowPlaying));
    if (!cmd || !cmd->action()) {
        qCWarning(VIM_LOG) << "focusNowPlaying: ShowNowPlaying action not found";
        return;
    }

    cmd->action()->trigger();
}

Fooyin::Playlist* VimHandler::targetPlaylist() const
{
    if (!m_playlistHandler) return nullptr;

    // Prefer the playlist the active view is currently displaying, identified
    // by matching its row count. This ensures yank/paste target what the user
    // sees, not whatever happens to be playing in the background.
    auto* view = m_viewLocator->activeView();
    const int viewRows = (view && view->model()) ? view->model()->rowCount() : -1;

    if (viewRows > 0) {
        for (auto* p : m_playlistHandler->playlists()) {
            if (p && p->trackCount() == viewRows) {
                qCDebug(VIM_LOG) << "targetPlaylist: matched by row count (" << viewRows
                                 << "):" << p->name();
                return p;
            }
        }
    }

    // Fall back: currently playing playlist.
    if (auto* p = m_playlistHandler->activePlaylist()) {
        qCDebug(VIM_LOG) << "targetPlaylist: active (playing):" << p->name();
        return p;
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

    qCDebug(VIM_LOG) << "scheduleIndexRestore: scheduling restore to row=" << row
                     << "col=" << col << "expectedRowCount=" << expectedRowCount
                     << "view=" << view->metaObject()->className();

    auto tryRestore = [viewPtr, row, col]() -> bool {
        if (!viewPtr || !viewPtr->model() || !viewPtr->selectionModel()) return false;
        if (viewPtr->model()->rowCount() <= row) return false;
        const QModelIndex idx = viewPtr->model()->index(row, col);
        if (!idx.isValid()) return false;
        const int currentRow = viewPtr->currentIndex().isValid() ? viewPtr->currentIndex().row() : -1;
        qCDebug(VIM_LOG) << "scheduleIndexRestore: cursor restoring row" << row
                         << "(current was" << currentRow << ")";
        viewPtr->selectionModel()->setCurrentIndex(
            idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        qCDebug(VIM_LOG) << "scheduleIndexRestore: cursor restored to row" << row;
        return true;
    };

    // Attempt 1: after the sync in-memory modelReset's handlers complete.
    QTimer::singleShot(0, this, [tryRestore]() { tryRestore(); });

    // Attempt 2: fooyin often defers the model reset (resetModelThrottled) and
    // populates the model asynchronously via a populator thread.  The model emits
    // modelReset (0 rows), then rowsInserted as groups are populated.  Wait for
    // rowCount to reach expectedRowCount before restoring.
    auto restoreAfterPopulation = [viewPtr, tryRestore, expectedRowCount]() {
        if (!viewPtr || !viewPtr->model()) return;
        qCDebug(VIM_LOG) << "scheduleIndexRestore: modelReset caught, waiting for rowsInserted"
                         << "target rowCount=" << expectedRowCount;

        auto guard = std::make_shared<bool>(false);
        QObject::connect(viewPtr->model(), &QAbstractItemModel::rowsInserted,
                         viewPtr, [viewPtr, tryRestore, expectedRowCount, guard]() {
            if (!viewPtr || !viewPtr->model() || *guard) return;
            const int rc = viewPtr->model()->rowCount();
            if (rc < expectedRowCount) return;
            qCDebug(VIM_LOG) << "scheduleIndexRestore: rowsInserted, rowCount=" << rc
                             << "view currentIndex row="
                             << (viewPtr->currentIndex().isValid() ? QString::number(viewPtr->currentIndex().row()) : QStringLiteral("invalid"));
            tryRestore();
            *guard = true;
        });
    };

    QObject::connect(view->model(), &QAbstractItemModel::modelReset,
                     view->model(), restoreAfterPopulation, Qt::SingleShotConnection);
}

// ---------------------------------------------------------------------------
// Undo / redo
// ---------------------------------------------------------------------------

void VimHandler::pushUndoEntry(Fooyin::UId playlistId, Fooyin::PlaylistTrackList before,
                                Fooyin::PlaylistTrackList after, int cursorBefore, int cursorAfter, int col)
{
    if (m_undoIndex + 1 < static_cast<int>(m_undoStack.size()))
        m_undoStack.resize(static_cast<size_t>(m_undoIndex + 1));
    m_undoStack.push_back({std::move(playlistId), std::move(before), std::move(after),
                           cursorBefore, cursorAfter, col});
    m_undoIndex = static_cast<int>(m_undoStack.size()) - 1;
    qCDebug(VIM_LOG) << "pushUndoEntry: undoIndex=" << m_undoIndex
                     << "stackSize=" << m_undoStack.size();
}

void VimHandler::undo()
{
    if (!m_playlistHandler) {
        qCWarning(VIM_LOG) << "undo: no PlaylistHandler";
        return;
    }
    if (m_undoIndex < 0) {
        qCDebug(VIM_LOG) << "undo: stack empty";
        return;
    }
    const UndoEntry& entry = m_undoStack[static_cast<size_t>(m_undoIndex)];
    qCDebug(VIM_LOG) << "undo: index=" << m_undoIndex
                     << "trackCount=" << entry.before.size()
                     << "cursorBefore=" << entry.cursorBefore;
    m_playlistHandler->replacePlaylistTracks(
        entry.playlistId, entry.before, Fooyin::PlaylistTrackChangeSource::History);
    auto* view = m_viewLocator->activeView();
    if (view && entry.cursorBefore >= 0) {
        scheduleIndexRestore(view, entry.cursorBefore, entry.col,
                             static_cast<int>(entry.before.size()));
    }
    --m_undoIndex;
}

void VimHandler::redo()
{
    if (!m_playlistHandler) {
        qCWarning(VIM_LOG) << "redo: no PlaylistHandler";
        return;
    }
    if (m_undoIndex + 1 >= static_cast<int>(m_undoStack.size())) {
        qCDebug(VIM_LOG) << "redo: nothing to redo";
        return;
    }
    ++m_undoIndex;
    const UndoEntry& entry = m_undoStack[static_cast<size_t>(m_undoIndex)];
    qCDebug(VIM_LOG) << "redo: index=" << m_undoIndex
                     << "trackCount=" << entry.after.size()
                     << "cursorAfter=" << entry.cursorAfter;
    m_playlistHandler->replacePlaylistTracks(
        entry.playlistId, entry.after, Fooyin::PlaylistTrackChangeSource::History);
    auto* view = m_viewLocator->activeView();
    if (view && entry.cursorAfter >= 0) {
        scheduleIndexRestore(view, entry.cursorAfter, entry.col,
                             static_cast<int>(entry.after.size()));
    }
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

    Fooyin::PlaylistTrackList snapshotBefore = playlist->playlistTracks();

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

    Fooyin::PlaylistTrackList snapshotAfter = snapshotBefore;
    snapshotAfter.erase(snapshotAfter.begin() + row, snapshotAfter.begin() + end);
    pushUndoEntry(playlist->id(), std::move(snapshotBefore), std::move(snapshotAfter),
                  row, restoreRow, col);

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

    auto* view    = m_viewLocator->activeView();
    const int col = (view && view->currentIndex().isValid()) ? view->currentIndex().column() : 0;
    const int numDeleted   = std::min(bot, size - 1) - top + 1;
    const int expectedRows = size - numDeleted;
    const int restoreRow   = (expectedRows > 0) ? qMin(top, expectedRows - 1) : 0;

    Fooyin::PlaylistTrackList snapshotBefore = playlist->playlistTracks();

    qCDebug(VIM_LOG) << "deleteVisualSelection: playlist=" << playlist->name()
                     << "rows [" << top << "," << qMin(bot, size - 1) << "]";
    yankVisualSelection();
    std::vector<int> indices;
    for (int i = top; i <= bot && i < size; ++i)
        indices.push_back(i);
    m_playlistHandler->removePlaylistTracks(playlist->id(), indices);

    Fooyin::PlaylistTrackList snapshotAfter = snapshotBefore;
    snapshotAfter.erase(snapshotAfter.begin() + top,
                        snapshotAfter.begin() + std::min(bot + 1, size));
    pushUndoEntry(playlist->id(), std::move(snapshotBefore), std::move(snapshotAfter),
                  m_visualCursor, restoreRow, col);

    enterNormal();

    if (view && expectedRows > 0) {
        scheduleIndexRestore(view, restoreRow, col, expectedRows);
    }
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
    Fooyin::PlaylistTrackList snapshotBefore = playlist->playlistTracks();
    Fooyin::PlaylistTrackList all = snapshotBefore;
    const int insertPos   = std::clamp(targetRow, 0, static_cast<int>(all.size()));
    const auto newEntries = Fooyin::PlaylistTrack::fromTracks(m_clipboard.tracks(), playlist->id());
    qCDebug(VIM_LOG) << "pasteAfter: playlist=" << playlist->name()
                     << "originalRow=" << originalRow << "insertPos=" << insertPos
                     << "trackCount=" << newEntries.size();
    all.insert(all.begin() + insertPos, newEntries.begin(), newEntries.end());
    m_playlistHandler->replacePlaylistTracks(playlist->id(), all);
    const int afterSize = static_cast<int>(all.size());
    const int currentRowAfterReplace = view->currentIndex().isValid() ? view->currentIndex().row() : -1;
    qCDebug(VIM_LOG) << "pasteAfter: after replacePlaylistTracks, currentIndex row ="
                     << currentRowAfterReplace << "(expected originalRow=" << originalRow << ")";
    pushUndoEntry(playlist->id(), std::move(snapshotBefore), std::move(all),
                  originalRow, originalRow, col);
    scheduleIndexRestore(view, originalRow, col, afterSize);
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
    Fooyin::PlaylistTrackList snapshotBefore = playlist->playlistTracks();
    Fooyin::PlaylistTrackList all = snapshotBefore;
    const int insertPos   = std::clamp(originalRow, 0, static_cast<int>(all.size()));
    const auto newEntries = Fooyin::PlaylistTrack::fromTracks(m_clipboard.tracks(), playlist->id());
    qCDebug(VIM_LOG) << "pasteBefore: playlist=" << playlist->name()
                     << "originalRow=" << originalRow << "insertPos=" << insertPos
                     << "trackCount=" << newEntries.size();
    all.insert(all.begin() + insertPos, newEntries.begin(), newEntries.end());
    m_playlistHandler->replacePlaylistTracks(playlist->id(), all);
    const int afterSize = static_cast<int>(all.size());
    const int currentRowAfterReplace = view->currentIndex().isValid() ? view->currentIndex().row() : -1;
    qCDebug(VIM_LOG) << "pasteBefore: after replacePlaylistTracks, currentIndex row ="
                     << currentRowAfterReplace << "(expected originalRow=" << originalRow << ")";
    pushUndoEntry(playlist->id(), std::move(snapshotBefore), std::move(all),
                  originalRow, originalRow, col);
    scheduleIndexRestore(view, originalRow, col, afterSize);
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

    Fooyin::PlaylistTrackList snapshotBefore = playlist->playlistTracks();
    Fooyin::PlaylistTrackList all = snapshotBefore;
    const Fooyin::PlaylistTrack moved = all[static_cast<size_t>(row)];
    all.erase(all.begin() + row);
    const int insertPos = std::clamp(row + delta, 0, static_cast<int>(all.size()));
    qCDebug(VIM_LOG) << "moveRows: playlist=" << playlist->name()
                     << "row" << row << "->" << insertPos
                     << "(delta=" << delta << ")"
                     << "track=" << moved.track.title();
    all.insert(all.begin() + insertPos, moved);

    m_playlistHandler->replacePlaylistTracks(playlist->id(), all);
    const int afterSize = static_cast<int>(all.size());
    pushUndoEntry(playlist->id(), std::move(snapshotBefore), std::move(all),
                  row, insertPos, col);
    scheduleIndexRestore(view, insertPos, col, afterSize);
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

    Fooyin::PlaylistTrackList snapshotBefore = playlist->playlistTracks();
    Fooyin::PlaylistTrackList all = snapshotBefore;
    if (bot >= static_cast<int>(all.size())) {
        qCWarning(VIM_LOG) << "moveVisualSelection: selection out of range"
                           << "(bot=" << bot << "size=" << all.size() << ")";
        return;
    }

    Fooyin::PlaylistTrackList movedTracks(all.begin() + top, all.begin() + bot + 1);
    all.erase(all.begin() + top, all.begin() + bot + 1);
    const int insertPos = std::clamp(top + delta, 0, static_cast<int>(all.size()));
    qCDebug(VIM_LOG) << "moveVisualSelection: playlist=" << playlist->name()
                     << "rows [" << top << "," << bot << "] (size=" << selSize << ")"
                     << "->" << insertPos << "(delta=" << delta << ")";
    all.insert(all.begin() + insertPos, movedTracks.begin(), movedTracks.end());

    m_playlistHandler->replacePlaylistTracks(playlist->id(), all);

    const bool anchorFirst = (m_visualAnchor <= m_visualCursor);
    m_visualAnchor = anchorFirst ? insertPos : insertPos + selSize - 1;
    m_visualCursor = anchorFirst ? insertPos + selSize - 1 : insertPos;
    qCDebug(VIM_LOG) << "moveVisualSelection: new anchor=" << m_visualAnchor
                     << "cursor=" << m_visualCursor;
    pushUndoEntry(playlist->id(), std::move(snapshotBefore), std::move(all),
                  top, insertPos, 0);
    updateVisualSelection();
}

// ---------------------------------------------------------------------------
// Filter (Ctrl+I)
// ---------------------------------------------------------------------------

void VimHandler::enterFilter()
{
    auto* view = m_viewLocator->activeView();
    if (!view) {
        qCWarning(VIM_LOG) << "enterFilter: no active view";
        return;
    }

    auto* target = findEnclosingFyWidget(view);
    if (!target) {
        qCWarning(VIM_LOG) << "enterFilter: no enclosing FyWidget found";
        return;
    }
    m_filterTarget = target;

    if (!m_filterBar) {
        m_filterBar = new VimSearchBar();
        connect(m_filterBar, &VimSearchBar::textChanged, this, &VimHandler::onFilterTextChanged);
        connect(m_filterBar, &VimSearchBar::confirmed,   this, &VimHandler::commitFilter);
        connect(m_filterBar, &VimSearchBar::cancelled,   this, &VimHandler::cancelFilter);
    }

    m_filterBar->attachTo(view->window());
    m_filterBar->clear();
    if (!m_lastFilter.isEmpty())
        m_filterBar->prefillText(m_lastFilter);

    m_mode = Mode::Filter;
    m_pendingKey = {};
    m_count = 0;
    emit modeChanged(m_mode);

    m_filterBar->show();
    m_filterBar->setFocus();
    qCInfo(VIM_LOG) << "Mode → Filter";
}

void VimHandler::commitFilter()
{
    if (!m_filterBar)
        return;
    m_lastFilter = m_filterBar->text();
    m_filterBar->hide();

    if (m_filterTarget)
        m_filterTarget->setFocus(Qt::OtherFocusReason);

    m_mode = Mode::Normal;
    m_pendingKey = {};
    m_count = 0;
    emit modeChanged(m_mode);
    qCInfo(VIM_LOG) << "Filter committed: '" << m_lastFilter << "' → Normal";
}

void VimHandler::cancelFilter()
{
    if (m_filterTarget)
        m_filterTarget->searchEvent(Fooyin::SearchRequest{});
    m_lastFilter.clear();

    if (m_filterBar)
        m_filterBar->hide();

    if (m_filterTarget)
        m_filterTarget->setFocus(Qt::OtherFocusReason);

    m_mode = Mode::Normal;
    m_pendingKey = {};
    m_count = 0;
    emit modeChanged(m_mode);
    qCInfo(VIM_LOG) << "Filter cancelled → Normal";
}

void VimHandler::nextMatch()
{
    if (!m_searchMatches.empty()) {
        m_searchMatchIdx = (m_searchMatchIdx + 1) % static_cast<int>(m_searchMatches.size());
        qCDebug(VIM_LOG) << "nextMatch: search match" << m_searchMatchIdx
                         << "row=" << m_searchMatches[static_cast<size_t>(m_searchMatchIdx)];
        jumpToMatch(m_searchMatchIdx);
        return;
    }
    if (!m_lastFilter.isEmpty()) {
        qCDebug(VIM_LOG) << "nextMatch: filter mode, moveCursor +1";
        moveCursor(+1);
    }
}

void VimHandler::prevMatch()
{
    if (!m_searchMatches.empty()) {
        const int sz = static_cast<int>(m_searchMatches.size());
        m_searchMatchIdx = (m_searchMatchIdx - 1 + sz) % sz;
        qCDebug(VIM_LOG) << "prevMatch: search match" << m_searchMatchIdx
                         << "row=" << m_searchMatches[static_cast<size_t>(m_searchMatchIdx)];
        jumpToMatch(m_searchMatchIdx);
        return;
    }
    if (!m_lastFilter.isEmpty()) {
        qCDebug(VIM_LOG) << "prevMatch: filter mode, moveCursor -1";
        moveCursor(-1);
    }
}

void VimHandler::onFilterTextChanged(const QString& text)
{
    if (!m_filterTarget)
        return;
    Fooyin::SearchRequest req;
    req.text      = text;
    req.emptyMode = Fooyin::EmptySearchMode::ShowAll;
    m_filterTarget->searchEvent(req);
}

Fooyin::FyWidget* VimHandler::findEnclosingFyWidget(QAbstractItemView* view) const
{
    QWidget* w = view;
    while (w) {
        if (auto* fy = qobject_cast<Fooyin::FyWidget*>(w))
            return fy;
        w = w->parentWidget();
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Search (/)
// ---------------------------------------------------------------------------

void VimHandler::enterSearch()
{
    auto* view = m_viewLocator->activeView();
    if (!view || !view->model()) {
        qCWarning(VIM_LOG) << "enterSearch: no active view or model";
        return;
    }

    // Save the view now — once the search bar gains focus, activeView() would
    // lose its cache and its fallback would steal focus back to the view.
    m_searchView = view;
    m_preSearchRow = view->currentIndex().isValid() ? view->currentIndex().row() : 0;

    if (!m_searchBar) {
        m_searchBar = new VimSearchBar();
        m_searchBar->setLabel(QStringLiteral("/"));
        connect(m_searchBar, &VimSearchBar::textChanged, this, &VimHandler::onSearchTextChanged);
        connect(m_searchBar, &VimSearchBar::confirmed,   this, &VimHandler::commitSearch);
        connect(m_searchBar, &VimSearchBar::cancelled,   this, &VimHandler::cancelSearch);
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
    qCInfo(VIM_LOG) << "Mode → Search, preSearchRow=" << m_preSearchRow;
}

void VimHandler::commitSearch()
{
    m_lastSearchPattern = m_searchBar ? m_searchBar->text() : QString{};
    if (m_searchBar)
        m_searchBar->hide();

    if (m_searchView)
        m_searchView->setFocus(Qt::OtherFocusReason);

    m_mode = Mode::Normal;
    m_pendingKey = {};
    m_count = 0;
    emit modeChanged(m_mode);
    qCInfo(VIM_LOG) << "Search committed: '" << m_lastSearchPattern
                    << "' matchCount=" << m_searchMatches.size();
}

void VimHandler::cancelSearch()
{
    if (m_searchBar)
        m_searchBar->hide();

    m_searchMatches.clear();
    m_searchMatchIdx = -1;
    m_lastSearchPattern.clear();

    auto* view = m_searchView.data();
    if (view && view->model() && m_preSearchRow >= 0) {
        const int col = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
        view->selectionModel()->setCurrentIndex(
            view->model()->index(m_preSearchRow, col),
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }

    if (view)
        view->setFocus(Qt::OtherFocusReason);

    m_mode = Mode::Normal;
    m_pendingKey = {};
    m_count = 0;
    emit modeChanged(m_mode);
    qCInfo(VIM_LOG) << "Search cancelled, cursor restored to row" << m_preSearchRow;
}

void VimHandler::buildMatchList(const QString& pattern)
{
    m_searchMatches.clear();
    m_searchMatchIdx = -1;

    if (pattern.isEmpty())
        return;

    auto* view = m_searchView.data();
    if (!view || !view->model())
        return;

    auto* model = view->model();
    const int rows = model->rowCount();
    const int cols = model->columnCount();

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const QModelIndex mi = model->index(row, col);
            // PlaylistModel returns formatted track text via ToolTipRole;
            // Qt::DisplayRole is only used for the RatingEditor column.
            QString cell = model->data(mi, Qt::ToolTipRole).toString();
            if (cell.isEmpty())
                cell = model->data(mi, Qt::DisplayRole).toString();
            if (!cell.isEmpty() && cell.contains(pattern, Qt::CaseInsensitive)) {
                m_searchMatches.push_back(row);
                break;
            }
        }
    }
    qCDebug(VIM_LOG) << "buildMatchList: pattern='" << pattern
                     << "' matches=" << m_searchMatches.size();
}

void VimHandler::onSearchTextChanged(const QString& text)
{
    buildMatchList(text);

    if (!m_searchMatches.empty()) {
        m_searchMatchIdx = 0;
        for (int i = 0; i < static_cast<int>(m_searchMatches.size()); ++i) {
            if (m_searchMatches[static_cast<size_t>(i)] >= m_preSearchRow) {
                m_searchMatchIdx = i;
                break;
            }
        }
        jumpToMatch(m_searchMatchIdx);
    } else if (!text.isEmpty()) {
        auto* view = m_searchView.data();
        if (view && view->model()) {
            const int col = view->currentIndex().isValid()
                          ? view->currentIndex().column() : 0;
            view->selectionModel()->setCurrentIndex(
                view->model()->index(m_preSearchRow, col),
                QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
    }
}

void VimHandler::jumpToMatch(int idx)
{
    auto* view = m_searchView.data();
    if (!view || !view->model() || m_searchMatches.empty())
        return;
    const int row = m_searchMatches[static_cast<size_t>(idx)];
    const int col = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
    const QModelIndex mi = view->model()->index(row, col);
    view->selectionModel()->setCurrentIndex(
        mi, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view->scrollTo(mi);
    qCDebug(VIM_LOG) << "jumpToMatch: idx=" << idx << "row=" << row;
}

// ---------------------------------------------------------------------------
// Config-binding implementation
// ---------------------------------------------------------------------------

void VimHandler::setSettingsManager(Fooyin::SettingsManager* manager)
{
    m_settingsManager = manager;
    if (!manager) return;

    m_useConfigBindings = manager->value(QStringLiteral("VimMotions/UseConfigBindings")).toBool();
    m_useDefaultBindings = manager->value(QStringLiteral("VimMotions/UseDefaultBindings")).toBool();

    using namespace Settings::VimMotions;
    manager->subscribe<UseConfigBindings>(this, [this](bool val) {
        m_useConfigBindings = val;
        if (val) rebuildBindings();
    });

    manager->subscribe<UseDefaultBindings>(this, [this](bool val) {
        m_useDefaultBindings = val;
    if (m_useConfigBindings) rebuildBindings();
    });

    for (const auto& b : VimMotionsSettings::defaultBindings()) {
        manager->subscribe(QString::fromLatin1(b.key), this, [this](const QVariant&) {
            if (m_useConfigBindings) rebuildBindings();
        });
    }

    if (m_useConfigBindings) rebuildBindings();
}

int VimHandler::currentCount()
{
    if (m_count > 0) {
        m_hadExplicitCount = true;
        m_dispatchCount = m_count;
        m_count = 0;
    }
    return m_dispatchCount;
}

bool VimHandler::hadExplicitCount() const
{
    return m_hadExplicitCount;
}

void VimHandler::clearPendingState()
{
    m_count = 0;
    m_pendingKey = {};
}

void VimHandler::moveSpatialFocus(Direction dir)
{
    m_spatialNavigator->moveFocus(dir, m_viewLocator->activeView());
}

void VimHandler::extendVisualCursor(int delta)
{
    auto* view = m_viewLocator->activeView();
    const int last = (view && view->model()) ? view->model()->rowCount() - 1 : INT_MAX;
    m_visualCursor = std::clamp(m_visualCursor + delta, 0, qMax(0, last));
    updateVisualSelection();
}

void VimHandler::extendVisualToFirst()
{
    m_visualCursor = 0;
    updateVisualSelection();
}

void VimHandler::extendVisualToEnd()
{
    auto* view = m_viewLocator->activeView();
    if (view && view->model()) {
        const int last = view->model()->rowCount() - 1;
        if (last >= 0) {
            m_visualCursor = hadExplicitCount()
                ? std::clamp(currentCount() - 1, 0, last)
                : last;
            updateVisualSelection();
        }
    }
}

void VimHandler::extendVisualToRow(int row)
{
    auto* view = m_viewLocator->activeView();
    if (view && view->model()) {
        const int last = view->model()->rowCount() - 1;
        m_visualCursor = std::clamp(row, 0, qMax(0, last));
        updateVisualSelection();
    }
}

void VimHandler::extendVisualHalfPage(int direction)
{
    const int delta = halfPageDelta();
    m_visualCursor = std::clamp(m_visualCursor + direction * delta, 0,
                                (m_viewLocator->activeView() && m_viewLocator->activeView()->model())
                                    ? qMax(0, m_viewLocator->activeView()->model()->rowCount() - 1)
                                    : 0);
    updateVisualSelection();
}

void VimHandler::swapVisualAnchor()
{
    std::swap(m_visualAnchor, m_visualCursor);
    updateVisualSelection();
}

// ---------------------------------------------------------------------------
// Binding rebuild
// ---------------------------------------------------------------------------

void VimHandler::rebuildBindings()
{
    m_configBindings.clear();
    if (!m_settingsManager) return;

    // Track default keys so we skip them during the custom-key scan
    QSet<QString> seenDefaultKeys;

    for (const auto& b : VimMotionsSettings::defaultBindings()) {
        const QString fullKey = QString::fromLatin1(b.key);
        seenDefaultKeys.insert(fullKey);

        // When UseDefaultBindings is false, only include bindings
        // that the user has explicitly set in their config file
        if (!m_useDefaultBindings) {
            if (!m_settingsManager->fileContains(fullKey)) {
                continue;
            }
        }

        const QString val = m_settingsManager->value(fullKey).toString();

        // Empty value = user wants to unmap this default binding
        if (val.isEmpty()) continue;

        const QStringList parts = fullKey.split(u'/');
        if (parts.size() < 4) continue;

        const QString modeStr = parts[parts.size() - 2];
        const QString keyComboStr = parts[parts.size() - 1];

        Mode mode = Mode::Normal;
        if (modeStr == QStringLiteral("Visual")) mode = Mode::Visual;
        else if (modeStr == QStringLiteral("Insert")) mode = Mode::Insert;

        auto entry = parseBindingString(keyComboStr, val);
        m_configBindings[mode].push_back(std::move(entry));
    }

    // Scan the settings file for user-defined custom bindings
    // that are not in the defaults list (e.g. "d" or "f" in Normal mode)
    {
        const QString settingsPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                                     + QStringLiteral("/fooyin.conf");
        QSettings fileSettings{settingsPath, QSettings::IniFormat};
        fileSettings.beginGroup(QStringLiteral("VimMotions"));
        const QStringList allKeys = fileSettings.allKeys();

        qCDebug(VIM_LOG) << "Custom binding scan:" << allKeys.size() << "keys under VimMotions in" << settingsPath;

        for (const QString& key : allKeys) {
            if (!key.startsWith(QStringLiteral("Bindings/"))) continue;

            const QString fullKey = QStringLiteral("VimMotions/") + key;
            if (seenDefaultKeys.contains(fullKey)) {
                qCDebug(VIM_LOG) << "  custom-scan: skip (default key)" << key;
                continue;
            }

            const QString val = fileSettings.value(key).toString();
            qCDebug(VIM_LOG) << "  custom-scan:" << key << "=" << val << "(empty? " << val.isEmpty() << ")";
            if (val.isEmpty()) continue;

            const QStringList parts = fullKey.split(u'/');
            if (parts.size() < 4) continue;

            const QString modeStr = parts[parts.size() - 2];
            const QString keyComboStr = parts[parts.size() - 1];

            Mode mode = Mode::Normal;
            if (modeStr == QStringLiteral("Visual")) mode = Mode::Visual;
            else if (modeStr == QStringLiteral("Insert")) mode = Mode::Insert;

            auto entry = parseBindingString(keyComboStr, val);
            m_configBindings[mode].push_back(std::move(entry));
            qCDebug(VIM_LOG) << "  custom-scan: added" << key << "->" << val << "in" << modeStr << "mode";
        }
        fileSettings.endGroup();
    }

    qCInfo(VIM_LOG) << "Rebuilt config bindings:"
                    << m_configBindings[Mode::Normal].size() << "normal,"
                    << m_configBindings[Mode::Visual].size() << "visual,"
                    << m_configBindings[Mode::Insert].size() << "insert";
}

// ---------------------------------------------------------------------------
// Action execution
// ---------------------------------------------------------------------------

void VimHandler::executeAction(const BindingEntry& entry)
{
    auto handler = m_actions.find(entry.actionName);
    if (handler) {
        handler(*this, QStringView{entry.args});
    } else {
        qCWarning(VIM_LOG) << "executeAction: unknown action" << entry.actionName;
    }
}

// ---------------------------------------------------------------------------
// Config-driven dispatch
// ---------------------------------------------------------------------------

bool VimHandler::dispatchFromConfig(QKeyEvent* ev, Mode mode)
{
    const Qt::KeyboardModifiers mods = ev->modifiers();
    const int qtKey = ev->key();
    const QString text = ev->text();
    const QChar ch = text.isEmpty() ? QChar{} : text.front();

    // Ignore bare modifier key presses (Alt, Ctrl, Shift, Meta) so
    // they don't consume the accumulated count.  They will be claimed
    // again as part of the full modifier+key combo.
    switch (qtKey) {
        case Qt::Key_Shift:
        case Qt::Key_Control:
        case Qt::Key_Alt:
        case Qt::Key_Meta:
            return false;
        default:
            break;
    }

    if (mode == Mode::Normal || mode == Mode::Visual) {
        if (qtKey == Qt::Key_Escape && mods == Qt::NoModifier) {
            qCDebug(VIM_LOG) << "ConfigDispatch: Esc → clear";
            m_pendingKey = {};
            m_count = 0;
            if (mode == Mode::Visual) enterNormal();
            return true;
        }

        if (!ch.isNull() && ch.isDigit() && !(mods & ~Qt::KeypadModifier)) {
            const int digit = ch.digitValue();
            if (m_count > 0 || digit != 0) {
                m_count = m_count * 10 + digit;
                qCDebug(VIM_LOG) << "ConfigDispatch: digit" << digit << "→ count=" << m_count;
            }
            return true;
        }
    }

    m_hadExplicitCount = m_count > 0;
    m_dispatchCount = m_count > 0 ? m_count : 1;
    m_count = 0;

    if (!m_pendingKey.isNull()) {
        QChar pending = m_pendingKey;
        m_pendingKey = {};

        const auto& bindings = m_configBindings[mode];
        for (const auto& b : bindings) {
            if (b.isTwoKey && b.firstKey.ch == pending && b.secondKey.matches(ev)) {
                qCDebug(VIM_LOG) << "ConfigDispatch: two-key complete '" << pending << ch << "'";
                executeAction(b);
                return true;
            }
        }
    }

    const auto& bindings = m_configBindings[mode];

    const BindingEntry* best = nullptr;
    int bestModCount = -1;

    for (const auto& b : bindings) {
        if (b.isTwoKey) continue;
        if (b.firstKey.matches(ev)) {
            int mc = 0;
            auto m = b.firstKey.modifiers;
            if (m & Qt::ControlModifier) ++mc;
            if (m & Qt::AltModifier) ++mc;
            if (m & Qt::ShiftModifier) ++mc;
            if (m & Qt::MetaModifier) ++mc;
            if (mc > bestModCount) {
                bestModCount = mc;
                best = &b;
            }
        }
    }

    if (!best) {
        for (const auto& b : bindings) {
            if (!b.isTwoKey) continue;
            if (b.firstKey.matches(ev)) {
                best = &b;
                bestModCount = -2;
                break;
            }
        }
    }

    if (!best) {
        qCDebug(VIM_LOG) << "ConfigDispatch: no binding for key=" << qtKey << "text=" << text;
        return false;
    }

    if (best->isTwoKey) {
        m_pendingKey = best->firstKey.ch;
        qCDebug(VIM_LOG) << "ConfigDispatch: two-key start '" << m_pendingKey << "'";
        return true;
    }

    qCDebug(VIM_LOG) << "ConfigDispatch: action" << best->actionName << "args=" << best->args;
    executeAction(*best);
    return true;
}

// ---------------------------------------------------------------------------
// Config-driven wouldHandle (ShortcutOverride predicate)
// ---------------------------------------------------------------------------

bool VimHandler::wouldHandleFromConfig(QKeyEvent* ev, Mode mode) const
{
    if (mode == Mode::Normal || mode == Mode::Visual) {
        const int qtKey = ev->key();
        const auto mods = ev->modifiers();
        if (qtKey == Qt::Key_Escape && mods == Qt::NoModifier) return true;

        const QChar ch = ev->text().isEmpty() ? QChar{} : ev->text().front();
        if (!ch.isNull() && ch.isDigit() && !(mods & ~Qt::KeypadModifier)) return true;
    }

    const auto& bindings = m_configBindings.value(mode);

    if (!m_pendingKey.isNull()) {
        for (const auto& b : bindings) {
            if (b.isTwoKey && b.firstKey.ch == m_pendingKey && b.secondKey.matches(ev))
                return true;
        }
    }

    for (const auto& b : bindings) {
        if (b.isTwoKey) {
            if (b.firstKey.matches(ev)) return true;
        } else {
            if (b.firstKey.matches(ev)) return true;
        }
    }

    return false;
}

} // namespace Fooyin::VimMotions
