#pragma once

#include <QObject>
#include <QPointer>

class QAbstractItemView;
class QWidget;

namespace Fooyin::VimMotions {

class ViewLocator : public QObject
{
    Q_OBJECT

public:
    explicit ViewLocator(QObject* parent = nullptr);

    // Returns the currently active/focused QAbstractItemView, or nullptr.
    QAbstractItemView* activeView() const;

private:
    void onFocusChanged(QWidget* old, QWidget* now);
    static QAbstractItemView* findViewUnder(QWidget* root);
    static QAbstractItemView* findPlaylistViewUnder(QWidget* root);

    mutable QPointer<QAbstractItemView> m_cached;
};

} // namespace Fooyin::VimMotions
