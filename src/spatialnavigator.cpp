#include "spatialnavigator.h"
#include "vimlog.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QSplitter>
#include <QWidget>
#include <algorithm>

namespace Fooyin::VimMotions {

SpatialNavigator::SpatialNavigator(QObject* parent)
    : QObject{parent}
{
    connect(qApp, &QApplication::focusChanged, this, &SpatialNavigator::onFocusChanged);
    qCDebug(VIM_LOG) << "SpatialNavigator created";
}

void SpatialNavigator::moveFocus(Direction dir, QWidget* startFrom)
{
    QWidget* current = startFrom ? startFrom : QApplication::focusWidget();
    if(!current) {
        qCWarning(VIM_LOG) << "SpatialNavigator::moveFocus: no starting widget";
        return;
    }

    const Qt::Orientation orientation
        = (dir == Direction::Left || dir == Direction::Right) ? Qt::Horizontal : Qt::Vertical;
    const int step = (dir == Direction::Right || dir == Direction::Down) ? +1 : -1;

    qCDebug(VIM_LOG) << "SpatialNavigator::moveFocus: dir=" << static_cast<int>(dir)
                     << "orientation=" << (orientation == Qt::Horizontal ? "H" : "V") << "step=" << step
                     << "from=" << current->metaObject()->className()
                     << "(startFrom=" << (startFrom ? startFrom->metaObject()->className() : "null") << ")";

    // Walk up the parent chain. 'child' is always the direct child of 'parent'.
    QWidget* child  = current;
    QWidget* parent = current->parentWidget();

    while(parent) {
        if(auto* splitter = qobject_cast<QSplitter*>(parent)) {
            if(splitter->orientation() == orientation) {
                const int idx    = splitter->indexOf(child);
                const int newIdx = idx + step;
                qCDebug(VIM_LOG) << "SpatialNavigator: found matching splitter" << splitter->metaObject()->className()
                                 << "childIdx=" << idx << "targetIdx=" << newIdx
                                 << "splitterCount=" << splitter->count();
                if(idx >= 0 && newIdx >= 0 && newIdx < splitter->count()) {
                    QWidget* target = resolveLastVisited(splitter->widget(newIdx));
                    if(target) {
                        qCDebug(VIM_LOG) << "SpatialNavigator: focusing" << target->metaObject()->className()
                                         << "(lastVisited[splitter]=" << newIdx << ")";
                        m_lastVisited[splitter] = newIdx;
                        target->setFocus(Qt::OtherFocusReason);
                        return;
                    }
                    qCDebug(VIM_LOG) << "SpatialNavigator: resolveLastVisited returned null for idx" << newIdx;
                }
                else {
                    qCDebug(VIM_LOG) << "SpatialNavigator: at edge in this splitter, continuing up";
                }
            }
            else {
                qCDebug(VIM_LOG) << "SpatialNavigator: splitter orientation mismatch, skipping";
            }
        }
        child  = parent;
        parent = parent->parentWidget();
    }

    qCDebug(VIM_LOG) << "SpatialNavigator::moveFocus: reached top of tree, already at edge";
}

void SpatialNavigator::onFocusChanged(QWidget* /*old*/, QWidget* now)
{
    if(!now)
        return;

    // Record the direct-child index in every QSplitter ancestor.
    QWidget* child  = now;
    QWidget* parent = now->parentWidget();
    while(parent) {
        if(auto* splitter = qobject_cast<QSplitter*>(parent)) {
            const int idx = splitter->indexOf(child);
            if(idx >= 0) {
                qCDebug(VIM_LOG) << "SpatialNavigator: lastVisited[" << splitter->metaObject()->className()
                                 << "] =" << idx << "(focus →" << now->metaObject()->className() << ")";
                m_lastVisited[splitter] = idx;
            }
        }
        child  = parent;
        parent = parent->parentWidget();
    }
}

QWidget* SpatialNavigator::resolveLastVisited(QWidget* widget)
{
    if(!widget || !widget->isVisible())
        return nullptr;

    // If this node is a splitter, recurse into the last-visited child.
    if(auto* splitter = qobject_cast<QSplitter*>(widget)) {
        const int count = splitter->count();
        if(count == 0)
            return nullptr;
        const int idx = std::clamp(m_lastVisited.value(splitter, 0), 0, count - 1);
        qCDebug(VIM_LOG) << "SpatialNavigator::resolveLastVisited: descending into splitter child" << idx << "/"
                         << count;
        return resolveLastVisited(splitter->widget(idx));
    }

    qCDebug(VIM_LOG) << "SpatialNavigator::resolveLastVisited: examining" << widget->metaObject()->className()
                     << "focusPolicy=" << widget->focusPolicy() << "visible=" << widget->isVisible()
                     << "children=" << widget->children().count();

    // Prefer a QAbstractItemView: return immediately if this IS one.
    // This avoids surfacing viewport or other internal children of the view.
    if(qobject_cast<QAbstractItemView*>(widget)) {
        qCDebug(VIM_LOG) << "SpatialNavigator::resolveLastVisited: view target =" << widget->metaObject()->className();
        return widget;
    }

    // Recurse into visible children first so that a QAbstractItemView nested
    // inside a focusable container (e.g. EditableTabWidget) is found before
    // the container itself is returned as a fallback.
    for(QObject* obj : widget->children()) {
        if(auto* w = qobject_cast<QWidget*>(obj)) {
            if(QWidget* found = resolveLastVisited(w))
                return found;
        }
    }

    // Fall back: return this widget if it can receive keyboard focus.
    if(widget->focusPolicy() != Qt::NoFocus) {
        qCDebug(VIM_LOG) << "SpatialNavigator::resolveLastVisited: focusable fallback ="
                         << widget->metaObject()->className();
        return widget;
    }

    return nullptr;
}

} // namespace Fooyin::VimMotions
