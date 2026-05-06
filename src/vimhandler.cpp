#include "vimhandler.h"
#include "spatialnavigator.h"
#include "viewlocator.h"

#include <QAbstractItemView>
#include <QCoreApplication>
#include <QItemSelection>
#include <QKeyEvent>
#include <algorithm>
#include <utility>

namespace Fooyin::VimMotions {

VimHandler::VimHandler(QObject* parent)
    : QObject{parent}
    , m_viewLocator{new ViewLocator(this)}
    , m_spatialNavigator{new SpatialNavigator(this)}
{ }

VimHandler::Mode VimHandler::mode() const
{
    return m_mode;
}

bool VimHandler::eventFilter(QObject* /*watched*/, QEvent* event)
{
    if (m_suppressFilter || event->type() != QEvent::KeyPress)
        return false;
    return handleKeyPress(static_cast<QKeyEvent*>(event));
}

bool VimHandler::handleKeyPress(QKeyEvent* ev)
{
    switch (m_mode) {
        case Mode::Insert:
            if (ev->key() == Qt::Key_Escape) {
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

    // Ctrl combinations handled first
    if (mods & Qt::ControlModifier) {
        switch (qtKey) {
            case Qt::Key_J: m_spatialNavigator->moveFocus(Direction::Down);  return true;
            case Qt::Key_K: m_spatialNavigator->moveFocus(Direction::Up);    return true;
            case Qt::Key_H: m_spatialNavigator->moveFocus(Direction::Left);  return true;
            case Qt::Key_L: m_spatialNavigator->moveFocus(Direction::Right); return true;
            case Qt::Key_D: moveCursorHalfPage(+1); return true;
            case Qt::Key_U: moveCursorHalfPage(-1); return true;
            default: return true;
        }
    }

    if (qtKey == Qt::Key_Escape) {
        m_pendingKey = {};
        m_count = 0;
        return true;
    }

    const QString text = ev->text();
    if (text.isEmpty())
        return true;
    const QChar ch = text.front();

    // Enter Insert mode
    if (ch == u'i') {
        enterInsert();
        return true;
    }

    // Count accumulation: digits except leading zero
    if (ch.isDigit()) {
        const int digit = ch.digitValue();
        if (m_count > 0 || digit != 0)
            m_count = m_count * 10 + digit;
        return true;
    }

    const bool hadCount = m_count > 0;
    const int count = m_count > 0 ? m_count : 1;
    m_count = 0;

    // Two-key sequence completion
    if (!m_pendingKey.isNull()) {
        const QChar pending = m_pendingKey;
        m_pendingKey = {};

        if (pending == u'g' && ch == u'g') { jumpToFirst();      return true; }
        if (pending == u'd' && ch == u'd') { deleteRows(count);  return true; }
        if (pending == u'y' && ch == u'y') { yankRows(count);    return true; }
        // Incomplete sequence — fall through and process the current key normally
    }

    // Start of two-key sequences (g/d/y all lowercase)
    if (ch == u'g' || ch == u'd' || ch == u'y') {
        m_pendingKey = ch;
        return true;
    }

    // Single-key commands
    if (ch == u'j') { moveCursor(+count);                                         return true; }
    if (ch == u'k') { moveCursor(-count);                                         return true; }
    if (ch == u'G') { hadCount ? jumpToRow(count - 1) : jumpToLast();            return true; }
    if (ch == u'v') { enterVisual();                                              return true; }
    if (ch == u'p') { pasteAfter();                                               return true; }
    if (ch == u'P') { pasteBefore();                                              return true; }

    if (qtKey == Qt::Key_Return || qtKey == Qt::Key_Enter || ch == u'o') {
        activateCurrentRow();
        return true;
    }

    return true; // consume all unrecognised keys in Normal mode
}

bool VimHandler::handleVisualKey(QKeyEvent* ev)
{
    const Qt::KeyboardModifiers mods = ev->modifiers();
    const int qtKey = ev->key();

    if (mods & Qt::ControlModifier)
        return true;

    if (qtKey == Qt::Key_Escape) {
        m_count = 0;
        enterNormal();
        return true;
    }

    const QString text = ev->text();
    if (text.isEmpty())
        return true;
    const QChar ch = text.front();

    // Count accumulation
    if (ch.isDigit()) {
        const int digit = ch.digitValue();
        if (m_count > 0 || digit != 0)
            m_count = m_count * 10 + digit;
        return true;
    }

    const int count = m_count > 0 ? m_count : 1;
    m_count = 0;

    if (ch == u'j') {
        auto* view = m_viewLocator->activeView();
        const int last = (view && view->model()) ? view->model()->rowCount() - 1 : INT_MAX;
        m_visualCursor = std::clamp(m_visualCursor + count, 0, qMax(0, last));
        updateVisualSelection();
        return true;
    }
    if (ch == u'k') {
        m_visualCursor = qMax(0, m_visualCursor - count);
        updateVisualSelection();
        return true;
    }
    if (ch == u'o') { std::swap(m_visualAnchor, m_visualCursor);    updateVisualSelection(); return true; }
    if (ch == u'd') { deleteVisualSelection(); enterNormal();        return true; }
    if (ch == u'y') { yankVisualSelection();   enterNormal();        return true; }

    return true; // consume all unrecognised keys in Visual mode
}

void VimHandler::enterNormal()
{
    m_mode = Mode::Normal;
    m_pendingKey = {};
    m_count = 0;
    m_visualAnchor = -1;
    m_visualCursor = -1;
    emit modeChanged(m_mode);
}

void VimHandler::enterInsert()
{
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
    updateVisualSelection();
    emit modeChanged(m_mode);
}

// ---------------------------------------------------------------------------
// Cursor navigation
// ---------------------------------------------------------------------------

void VimHandler::moveCursor(int delta)
{
    auto* view = m_viewLocator->activeView();
    if (!view || !view->model()) return;
    const int last = view->model()->rowCount() - 1;
    if (last < 0) return;
    const int col  = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
    const int row  = view->currentIndex().isValid() ? view->currentIndex().row() : 0;
    view->setCurrentIndex(view->model()->index(std::clamp(row + delta, 0, last), col));
}

void VimHandler::jumpToFirst()
{
    auto* view = m_viewLocator->activeView();
    if (!view || !view->model() || view->model()->rowCount() == 0) return;
    const int col = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
    view->setCurrentIndex(view->model()->index(0, col));
}

void VimHandler::jumpToLast()
{
    auto* view = m_viewLocator->activeView();
    if (!view || !view->model()) return;
    const int last = view->model()->rowCount() - 1;
    if (last < 0) return;
    const int col = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
    view->setCurrentIndex(view->model()->index(last, col));
}

void VimHandler::jumpToRow(int row)
{
    auto* view = m_viewLocator->activeView();
    if (!view || !view->model()) return;
    const int last = view->model()->rowCount() - 1;
    if (last < 0) return;
    const int col = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
    view->setCurrentIndex(view->model()->index(std::clamp(row, 0, last), col));
}

void VimHandler::moveCursorHalfPage(int direction)
{
    auto* view = m_viewLocator->activeView();
    if (!view) return;
    const QModelIndex cur = view->currentIndex();
    int itemH = cur.isValid() ? view->visualRect(cur).height() : 0;
    if (itemH <= 0) itemH = 20;
    const int halfPage = qMax(1, view->height() / itemH / 2);
    moveCursor(direction * halfPage);
}

void VimHandler::activateCurrentRow()
{
    auto* view = m_viewLocator->activeView();
    if (!view) return;
    const QModelIndex idx = view->currentIndex();
    if (!idx.isValid()) return;
    // Send a synthetic Return key event; m_suppressFilter prevents re-entry
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
    if (!view || !view->model() || !view->selectionModel()) return;
    const int cols = view->model()->columnCount();
    const int top  = qMin(m_visualAnchor, m_visualCursor);
    const int bot  = qMax(m_visualAnchor, m_visualCursor);
    QItemSelection sel;
    sel.select(view->model()->index(top, 0),
               view->model()->index(bot, qMax(0, cols - 1)));
    view->selectionModel()->select(sel, QItemSelectionModel::ClearAndSelect);
    view->setCurrentIndex(view->model()->index(m_visualCursor,
                          view->currentIndex().isValid() ? view->currentIndex().column() : 0));
}

// ---------------------------------------------------------------------------
// Phase 5-6 stubs — yank / delete / paste
// ---------------------------------------------------------------------------
void VimHandler::deleteRows(int /*count*/)          { }
void VimHandler::yankRows(int /*count*/)            { }
void VimHandler::deleteVisualSelection()            { }
void VimHandler::yankVisualSelection()              { }
void VimHandler::pasteAfter()                       { }
void VimHandler::pasteBefore()                      { }

} // namespace Fooyin::VimMotions
