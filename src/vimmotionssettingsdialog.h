#pragma once

#include <QDialog>

class QWidget;
class QCheckBox;
class QDialogButtonBox;
class QSpinBox;
class QTreeView;

namespace Fooyin {
class SettingsManager;
}

namespace Fooyin::VimMotions {

class VimMotionsBindingBackend;

class VimMotionsSettingsDialog : public QDialog
{
public:
    explicit VimMotionsSettingsDialog(Fooyin::SettingsManager* settingsManager  = nullptr,
                                      VimMotionsBindingBackend* settingsBackend = nullptr, QWidget* parent = nullptr);

    void accept() override;

private:
    void load();
    void apply();
    void reset();

    Fooyin::SettingsManager* m_settingsManager{nullptr};
    VimMotionsBindingBackend* m_settingsBackend{nullptr};
    QSpinBox* m_pendingSequenceTimeout{nullptr};
    QCheckBox* m_wrapScan{nullptr};
    QCheckBox* m_useDefaultBindings{nullptr};
    QTreeView* m_bindingsTree{nullptr};
    QDialogButtonBox* m_buttons{nullptr};
};

} // namespace Fooyin::VimMotions
