#pragma once

#include <gui/fywidget.h>

class QLabel;

namespace Fooyin::VimMotions {

class VimModeIndicatorWidget : public FyWidget
{
    Q_OBJECT

public:
    explicit VimModeIndicatorWidget(QWidget* parent = nullptr);

    [[nodiscard]] QString name() const override;
    [[nodiscard]] QString layoutName() const override;

    void setModeText(const QString& text);

private:
    QLabel* m_label{nullptr};
};

} // namespace Fooyin::VimMotions
