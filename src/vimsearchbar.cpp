#include "vimsearchbar.h"
#include "vimlog.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>

namespace Fooyin::VimMotions {

VimSearchBar::VimSearchBar()
    : QWidget{nullptr}
    , m_prefix{new QLabel(QStringLiteral("/"), this)}
    , m_edit{new QLineEdit(this)}
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(2);
    layout->addWidget(m_prefix);
    layout->addWidget(m_edit);

    setFocusProxy(m_edit);

    connect(m_edit, &QLineEdit::textChanged, this, &VimSearchBar::textChanged);
}

void VimSearchBar::attachTo(QWidget* anchor)
{
    if (m_anchor)
        m_anchor->removeEventFilter(this);

    m_anchor = anchor;
    setParent(anchor);

    if (m_anchor) {
        m_anchor->installEventFilter(this);
        reposition();
    }
}

void VimSearchBar::clear()
{
    m_edit->clear();
}

void VimSearchBar::prefillText(const QString& text)
{
    m_edit->setText(text);
    m_edit->selectAll();
}

QString VimSearchBar::text() const
{
    return m_edit->text();
}

bool VimSearchBar::eventFilter(QObject* watched, QEvent* ev)
{
    if (watched == m_anchor && ev->type() == QEvent::Resize)
        reposition();
    return false;
}

void VimSearchBar::keyPressEvent(QKeyEvent* ev)
{
    switch (ev->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            emit confirmed();
            return;
        case Qt::Key_Escape:
            emit cancelled();
            return;
        default:
            break;
    }
    QWidget::keyPressEvent(ev);
}

void VimSearchBar::reposition()
{
    if (!m_anchor)
        return;
    const int h = sizeHint().height();
    setGeometry(0, m_anchor->height() - h, m_anchor->width(), h);
    raise();
}

} // namespace Fooyin::VimMotions
