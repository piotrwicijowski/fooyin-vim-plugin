#include "vimhandler.h"

#include <QKeyEvent>
#include <utility>

namespace Fooyin::VimMotions {

VimHandler::VimHandler(QObject* parent)
    : QObject{parent}
{ }

VimHandler::Mode VimHandler::mode() const
{
    return m_mode;
}

bool VimHandler::eventFilter(QObject* /*watched*/, QEvent* event)
{
    if (event->type() != QEvent::KeyPress)
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
            case Qt::Key_J: /* Phase 4: spatial focus down  */ return true;
            case Qt::Key_K: /* Phase 4: spatial focus up    */ return true;
            case Qt::Key_H: /* Phase 4: spatial focus left  */ return true;
            case Qt::Key_L: /* Phase 4: spatial focus right */ return true;
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

    if (ch == u'j') { m_visualCursor += count;                      updateVisualSelection(); return true; }
    if (ch == u'k') { m_visualCursor -= count;                      updateVisualSelection(); return true; }
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
    // Phase 3: seed anchor/cursor from ViewLocator's current row
    m_visualAnchor = 0;
    m_visualCursor = 0;
    emit modeChanged(m_mode);
}

// ---------------------------------------------------------------------------
// Phase 3 stubs — cursor navigation
// ---------------------------------------------------------------------------
void VimHandler::moveCursor(int /*delta*/)          { }
void VimHandler::jumpToFirst()                      { }
void VimHandler::jumpToLast()                       { }
void VimHandler::jumpToRow(int /*row*/)             { }
void VimHandler::moveCursorHalfPage(int /*dir*/)    { }
void VimHandler::activateCurrentRow()               { }

// ---------------------------------------------------------------------------
// Phase 5-6 stubs — yank / delete / paste
// ---------------------------------------------------------------------------
void VimHandler::deleteRows(int /*count*/)          { }
void VimHandler::yankRows(int /*count*/)            { }
void VimHandler::deleteVisualSelection()            { }
void VimHandler::yankVisualSelection()              { }
void VimHandler::pasteAfter()                       { }
void VimHandler::pasteBefore()                      { }
void VimHandler::updateVisualSelection()            { }

} // namespace Fooyin::VimMotions
