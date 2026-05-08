#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;

namespace Fooyin::VimMotions {

class VimSearchBar : public QWidget
{
    Q_OBJECT

public:
    explicit VimSearchBar();

    // Reparent and pin to the bottom edge of anchor.
    void attachTo(QWidget* anchor);
    void setLabel(const QString& text);
    void clear();
    void prefillText(const QString& text);
    QString text() const;

signals:
    void textChanged(const QString& text);
    void confirmed();
    void cancelled();

protected:
    bool eventFilter(QObject* watched, QEvent* ev) override;
    void keyPressEvent(QKeyEvent* ev) override;

private:
    void reposition();

    QLabel*    m_prefix;
    QLineEdit* m_edit;
    QWidget*   m_anchor{nullptr};
};

} // namespace Fooyin::VimMotions
