#pragma once

#include <QMap>
#include <QObject>

class QSplitter;
class QWidget;

namespace Fooyin::VimMotions {

enum class Direction
{
    Up,
    Down,
    Left,
    Right
};

class SpatialNavigator : public QObject
{
    Q_OBJECT

public:
    explicit SpatialNavigator(QObject* parent = nullptr);

    // startFrom: the widget to treat as the current location (falls back to
    // QApplication::focusWidget() when null). Pass the active view from
    // ViewLocator so navigation works even when Qt focus is on a toolbar button.
    void moveFocus(Direction dir, QWidget* startFrom = nullptr);

private:
    void onFocusChanged(QWidget* old, QWidget* now);
    QWidget* resolveLastVisited(QWidget* widget);

    QMap<QSplitter*, int> m_lastVisited;
};

} // namespace Fooyin::VimMotions
