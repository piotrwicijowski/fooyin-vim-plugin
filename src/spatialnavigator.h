#pragma once

#include <QMap>
#include <QObject>

class QSplitter;
class QWidget;

namespace Fooyin::VimMotions {

enum class Direction { Up, Down, Left, Right };

class SpatialNavigator : public QObject
{
    Q_OBJECT

public:
    explicit SpatialNavigator(QObject* parent = nullptr);

    void moveFocus(Direction dir);

private:
    void onFocusChanged(QWidget* old, QWidget* now);
    QWidget* resolveLastVisited(QWidget* widget);

    QMap<QSplitter*, int> m_lastVisited;
};

} // namespace Fooyin::VimMotions
