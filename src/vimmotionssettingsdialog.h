#pragma once

#include <QDialog>

namespace Fooyin::VimMotions {

class VimMotionsSettingsDialog : public QDialog
{
public:
    explicit VimMotionsSettingsDialog(QWidget* parent = nullptr);
};

} // namespace Fooyin::VimMotions
