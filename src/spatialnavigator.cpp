#include "spatialnavigator.h"
#include "vimlog.h"

#include <gui/fywidget.h>
#include <gui/widgetcontainer.h>

#include <QAbstractItemView>
#include <QApplication>
#include <QWidget>
#include <algorithm>

namespace Fooyin::VimMotions {

namespace {

bool isSplitter(const Fooyin::WidgetContainer* container)
{
    if(!container)
        return false;

    const QString layoutName = container->layoutName();
    return layoutName == QStringLiteral("SplitterHorizontal") || layoutName == QStringLiteral("SplitterVertical");
}

Fooyin::FyWidget* enclosingFyWidget(QWidget* widget)
{
    while(widget) {
        if(auto* fyWidget = qobject_cast<Fooyin::FyWidget*>(widget))
            return fyWidget;
        widget = widget->parentWidget();
    }

    return nullptr;
}

int childIndex(const Fooyin::WidgetContainer* container, const Fooyin::FyWidget* child)
{
    if(!container || !child)
        return -1;

    const Fooyin::WidgetList children = container->widgets();
    for(int index = 0; index < static_cast<int>(children.size()); ++index) {
        if(children[static_cast<size_t>(index)] == child)
            return index;
    }

    return -1;
}

} // namespace

SpatialNavigator::SpatialNavigator(QObject* parent)
    : QObject{parent}
{
    connect(qApp, &QApplication::focusChanged, this, &SpatialNavigator::onFocusChanged);
    qCDebug(VIM_LOG) << "SpatialNavigator created";
}

void SpatialNavigator::moveFocus(Direction dir, QWidget* startFrom)
{
    QWidget* startWidget = startFrom ? startFrom : QApplication::focusWidget();
    if(!startWidget) {
        qCWarning(VIM_LOG) << "SpatialNavigator::moveFocus: no starting widget";
        return;
    }

    auto* current = enclosingFyWidget(startWidget);
    if(!current) {
        qCDebug(VIM_LOG) << "SpatialNavigator::moveFocus: starting widget is outside the editable layout";
        return;
    }

    const Qt::Orientation orientation
        = (dir == Direction::Left || dir == Direction::Right) ? Qt::Horizontal : Qt::Vertical;
    const int step = (dir == Direction::Right || dir == Direction::Down) ? +1 : -1;

    qCDebug(VIM_LOG) << "SpatialNavigator::moveFocus: dir=" << static_cast<int>(dir)
                     << "orientation=" << (orientation == Qt::Horizontal ? "H" : "V") << "step=" << step
                     << "from=" << startWidget->metaObject()->className()
                     << "(startFrom=" << (startFrom ? startFrom->metaObject()->className() : "null") << ")";

    // Walk the logical layout tree so implementation widgets such as FySplitter
    // do not form part of the navigation contract.
    while(auto* parentWidget = current->findParent()) {
        if(auto* container = qobject_cast<Fooyin::WidgetContainer*>(parentWidget); isSplitter(container)) {
            if(container->orientation() == orientation) {
                const Fooyin::WidgetList children = container->widgets();
                const int idx                     = childIndex(container, current);
                const int newIdx                  = idx + step;
                qCDebug(VIM_LOG) << "SpatialNavigator: found matching logical splitter" << container->layoutName()
                                 << "childIdx=" << idx << "targetIdx=" << newIdx << "splitterCount=" << children.size();
                if(idx >= 0 && newIdx >= 0 && newIdx < static_cast<int>(children.size())) {
                    QWidget* target = resolveLastVisited(children[static_cast<size_t>(newIdx)]);
                    if(target) {
                        qCDebug(VIM_LOG) << "SpatialNavigator: focusing" << target->metaObject()->className()
                                         << "(lastVisited[splitter]=" << newIdx << ")";
                        m_lastVisited[container] = newIdx;
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
        current = parentWidget;
    }

    qCDebug(VIM_LOG) << "SpatialNavigator::moveFocus: reached top of tree, already at edge";
}

void SpatialNavigator::onFocusChanged(QWidget* /*old*/, QWidget* now)
{
    if(!now)
        return;

    auto* current = enclosingFyWidget(now);
    while(current) {
        auto* parentWidget = current->findParent();
        if(auto* container = qobject_cast<Fooyin::WidgetContainer*>(parentWidget); isSplitter(container)) {
            const int idx = childIndex(container, current);
            if(idx >= 0) {
                qCDebug(VIM_LOG) << "SpatialNavigator: lastVisited[" << container->layoutName() << "] =" << idx
                                 << "(focus →" << now->metaObject()->className() << ")";
                m_lastVisited[container] = idx;
            }
        }
        current = parentWidget;
    }
}

QWidget* SpatialNavigator::resolveLastVisited(QWidget* widget)
{
    if(!widget || !widget->isVisible())
        return nullptr;

    // Descend into a nested logical splitter through its remembered child.
    if(auto* container = qobject_cast<Fooyin::WidgetContainer*>(widget); isSplitter(container)) {
        const Fooyin::WidgetList children = container->widgets();
        if(children.empty())
            return nullptr;
        const int idx = std::clamp(m_lastVisited.value(container, 0), 0, static_cast<int>(children.size()) - 1);
        qCDebug(VIM_LOG) << "SpatialNavigator::resolveLastVisited: descending into splitter child" << idx << "/"
                         << children.size();
        return resolveLastVisited(children[static_cast<size_t>(idx)]);
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
