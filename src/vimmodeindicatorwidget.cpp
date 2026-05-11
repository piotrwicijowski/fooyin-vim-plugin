#include "vimmodeindicatorwidget.h"

#include <QHBoxLayout>
#include <QLabel>

namespace Fooyin::VimMotions {

VimModeIndicatorWidget::VimModeIndicatorWidget(QWidget* parent)
    : FyWidget{parent}
    , m_label{new QLabel(this)}
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 2, 6, 2);
    layout->addWidget(m_label);

    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_label->setTextInteractionFlags(Qt::NoTextInteraction);
    m_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    setModeText(tr("NORMAL"));
}

QString VimModeIndicatorWidget::name() const
{
    return tr("Vim Mode Indicator");
}

QString VimModeIndicatorWidget::layoutName() const
{
    return QStringLiteral("VimModeIndicator");
}

void VimModeIndicatorWidget::setModeText(const QString& text)
{
    const QString trimmed = text.trimmed();
    m_label->setText(trimmed.isEmpty() ? tr("NORMAL") : trimmed);
}

} // namespace Fooyin::VimMotions
