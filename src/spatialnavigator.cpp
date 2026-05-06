#include "spatialnavigator.h"

#include <QApplication>
#include <QSplitter>
#include <QWidget>
#include <algorithm>

namespace Fooyin::VimMotions {

SpatialNavigator::SpatialNavigator(QObject* parent)
    : QObject{parent}
{
    connect(qApp, &QApplication::focusChanged, this, &SpatialNavigator::onFocusChanged);
}

void SpatialNavigator::moveFocus(Direction dir)
{
    QWidget* current = QApplication::focusWidget();
    if (!current) return;

    const Qt::Orientation orientation = (dir == Direction::Left || dir == Direction::Right)
                                            ? Qt::Horizontal : Qt::Vertical;
    const int step = (dir == Direction::Right || dir == Direction::Down) ? +1 : -1;

    // Walk up the parent chain. 'child' is always the direct child of 'parent'.
    QWidget* child  = current;
    QWidget* parent = current->parentWidget();

    while (parent) {
        if (auto* splitter = qobject_cast<QSplitter*>(parent)) {
            if (splitter->orientation() == orientation) {
                const int idx    = splitter->indexOf(child);
                const int newIdx = idx + step;
                if (idx >= 0 && newIdx >= 0 && newIdx < splitter->count()) {
                    QWidget* target = resolveLastVisited(splitter->widget(newIdx));
                    if (target) {
                        m_lastVisited[splitter] = newIdx;
                        target->setFocus(Qt::OtherFocusReason);
                        return;
                    }
                }
            }
        }
        child  = parent;
        parent = parent->parentWidget();
    }
    // Reached the top with no valid sibling — already at the edge; do nothing.
}

void SpatialNavigator::onFocusChanged(QWidget* /*old*/, QWidget* now)
{
    if (!now) return;
    // Record the direct-child index in every QSplitter ancestor.
    QWidget* child  = now;
    QWidget* parent = now->parentWidget();
    while (parent) {
        if (auto* splitter = qobject_cast<QSplitter*>(parent)) {
            const int idx = splitter->indexOf(child);
            if (idx >= 0)
                m_lastVisited[splitter] = idx;
        }
        child  = parent;
        parent = parent->parentWidget();
    }
}

QWidget* SpatialNavigator::resolveLastVisited(QWidget* widget)
{
    if (!widget || !widget->isVisible())
        return nullptr;

    // If this node is a splitter, recurse into the last-visited child.
    if (auto* splitter = qobject_cast<QSplitter*>(widget)) {
        const int count = splitter->count();
        if (count == 0) return nullptr;
        const int idx = std::clamp(m_lastVisited.value(splitter, 0), 0, count - 1);
        return resolveLastVisited(splitter->widget(idx));
    }

    // If this widget can receive keyboard focus, it is our target.
    if (widget->focusPolicy() != Qt::NoFocus)
        return widget;

    // Otherwise recurse into visible children.
    for (QObject* obj : widget->children()) {
        if (auto* w = qobject_cast<QWidget*>(obj)) {
            if (QWidget* found = resolveLastVisited(w))
                return found;
        }
    }

    return nullptr;
}

} // namespace Fooyin::VimMotions
