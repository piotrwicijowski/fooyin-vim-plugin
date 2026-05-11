#include "vimmotionssettingsdialog.h"

#include <QApplication>

namespace Fooyin::VimMotions {

VimMotionsSettingsDialog::VimMotionsSettingsDialog(QWidget* parent)
    : QDialog{parent}
{
    setWindowTitle(QApplication::translate("VimMotionsSettingsDialog", "Vim Motions Settings"));
    setModal(true);
}

} // namespace Fooyin::VimMotions
