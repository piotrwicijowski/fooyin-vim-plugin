#include "viewlocator.h"
#include "vimlog.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QWidget>

namespace Fooyin::VimMotions {

ViewLocator::ViewLocator(QObject* parent)
    : QObject{parent}
{
    connect(qApp, &QApplication::focusChanged, this, &ViewLocator::onFocusChanged);
    qCDebug(VIM_LOG) << "ViewLocator created";
}

void ViewLocator::onFocusChanged(QWidget* old, QWidget* now)
{
    Q_UNUSED(old)
    m_cached = nullptr;

    if(!now) {
        qCDebug(VIM_LOG) << "ViewLocator: focus changed to nullptr, cache cleared";
        return;
    }

    // Fast path: walk up the new focus widget's parent chain
    QWidget* w = now;
    while(w) {
        if(auto* view = qobject_cast<QAbstractItemView*>(w)) {
            qCDebug(VIM_LOG) << "ViewLocator: focus →" << view->metaObject()->className() << "(cached)";
            m_cached = view;
            return;
        }
        w = w->parentWidget();
    }

    qCDebug(VIM_LOG) << "ViewLocator: focus → " << now->metaObject()->className() << "(not a view, cache remains null)";
}

QAbstractItemView* ViewLocator::activeView() const
{
    if(m_cached) {
        qCDebug(VIM_LOG) << "ViewLocator::activeView: cache hit →" << m_cached.data()->metaObject()->className();
        return m_cached.data();
    }

    // Walk the focus widget's parent chain
    QWidget* w = QApplication::focusWidget();
    if(!w)
        qCDebug(VIM_LOG) << "ViewLocator::activeView: no focused widget";

    while(w) {
        if(auto* view = qobject_cast<QAbstractItemView*>(w)) {
            qCDebug(VIM_LOG) << "ViewLocator::activeView: found via focus chain →" << view->metaObject()->className();
            m_cached = view;
            return view;
        }
        w = w->parentWidget();
    }

    // Fall back: prefer a Fooyin::PlaylistView and activate it so the first vim
    // action after startup lands on the playlist rather than a random widget.
    qCDebug(VIM_LOG) << "ViewLocator::activeView: no focused view, scanning for PlaylistView";
    for(QWidget* top : QApplication::topLevelWidgets()) {
        if(auto* view = findPlaylistViewUnder(top)) {
            qCDebug(VIM_LOG) << "ViewLocator::activeView: activating PlaylistView";
            view->setFocus(Qt::OtherFocusReason);
            m_cached = view;
            return view;
        }
    }

    // Last resort: any visible QAbstractItemView
    qCDebug(VIM_LOG) << "ViewLocator::activeView: no PlaylistView found, scanning for any view";
    for(QWidget* top : QApplication::topLevelWidgets()) {
        if(auto* view = findViewUnder(top)) {
            qCDebug(VIM_LOG) << "ViewLocator::activeView: found via scan →" << view->metaObject()->className();
            m_cached = view;
            return view;
        }
    }

    qCWarning(VIM_LOG) << "ViewLocator::activeView: no QAbstractItemView found anywhere";
    return nullptr;
}

QAbstractItemView* ViewLocator::findViewUnder(QWidget* root)
{
    if(!root || !root->isVisible())
        return nullptr;
    if(auto* view = qobject_cast<QAbstractItemView*>(root))
        return view;
    for(QObject* child : root->children()) {
        if(auto* w = qobject_cast<QWidget*>(child)) {
            if(auto* view = findViewUnder(w))
                return view;
        }
    }
    return nullptr;
}

QAbstractItemView* ViewLocator::findPlaylistViewUnder(QWidget* root)
{
    if(!root || !root->isVisible())
        return nullptr;
    if(auto* view = qobject_cast<QAbstractItemView*>(root)) {
        if(QLatin1String(root->metaObject()->className()) == QLatin1String("Fooyin::PlaylistView"))
            return view;
    }
    for(QObject* child : root->children()) {
        if(auto* w = qobject_cast<QWidget*>(child)) {
            if(auto* view = findPlaylistViewUnder(w))
                return view;
        }
    }
    return nullptr;
}

} // namespace Fooyin::VimMotions
