#include "viewlocator.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QWidget>

namespace Fooyin::VimMotions {

ViewLocator::ViewLocator(QObject* parent)
    : QObject{parent}
{
    connect(qApp, &QApplication::focusChanged, this, &ViewLocator::onFocusChanged);
}

void ViewLocator::onFocusChanged(QWidget* /*old*/, QWidget* now)
{
    m_cached = nullptr;
    if (!now)
        return;
    // Fast path: walk up the new focus widget's parent chain
    QWidget* w = now;
    while (w) {
        if (auto* view = qobject_cast<QAbstractItemView*>(w)) {
            m_cached = view;
            return;
        }
        w = w->parentWidget();
    }
}

QAbstractItemView* ViewLocator::activeView() const
{
    if (m_cached)
        return m_cached.data();

    // Walk the focus widget's parent chain
    QWidget* w = QApplication::focusWidget();
    while (w) {
        if (auto* view = qobject_cast<QAbstractItemView*>(w)) {
            m_cached = view;
            return view;
        }
        w = w->parentWidget();
    }

    // Fall back: scan all top-level windows for the first visible view
    for (QWidget* top : QApplication::topLevelWidgets()) {
        if (auto* view = findViewUnder(top)) {
            m_cached = view;
            return view;
        }
    }

    return nullptr;
}

QAbstractItemView* ViewLocator::findViewUnder(QWidget* root)
{
    if (!root || !root->isVisible())
        return nullptr;
    if (auto* view = qobject_cast<QAbstractItemView*>(root))
        return view;
    for (QObject* child : root->children()) {
        if (auto* w = qobject_cast<QWidget*>(child)) {
            if (auto* view = findViewUnder(w))
                return view;
        }
    }
    return nullptr;
}

} // namespace Fooyin::VimMotions
