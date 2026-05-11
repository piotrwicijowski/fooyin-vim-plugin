#pragma once

#include <QDialog>

class QWidget;
class QCheckBox;
class QDialogButtonBox;
class QSpinBox;
class QTreeView;

namespace Fooyin::VimMotions {

class VimMotionsSettingsDialog : public QDialog
{
public:
    explicit VimMotionsSettingsDialog(QWidget* parent = nullptr);

private:
    QSpinBox* m_pendingSequenceTimeout{nullptr};
    QCheckBox* m_wrapScan{nullptr};
    QCheckBox* m_useDefaultBindings{nullptr};
    QTreeView* m_bindingsTree{nullptr};
    QDialogButtonBox* m_buttons{nullptr};
};

} // namespace Fooyin::VimMotions
