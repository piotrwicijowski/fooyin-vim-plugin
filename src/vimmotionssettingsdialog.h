#pragma once

#include "vimmotionsbindingbackend.h"

#include <QDialog>
#include <QList>

class QWidget;
class QCheckBox;
class QDialogButtonBox;
class QPushButton;
class QSortFilterProxyModel;
class QSpinBox;
class QStandardItemModel;
class QTreeView;

namespace Fooyin {
class SettingsManager;
}

namespace Fooyin::VimMotions {

class VimMotionsSettingsDialog : public QDialog
{
public:
    explicit VimMotionsSettingsDialog(Fooyin::SettingsManager* settingsManager  = nullptr,
                                      VimMotionsBindingBackend* settingsBackend = nullptr, QWidget* parent = nullptr);

    void accept() override;

private:
    void refreshBindingsTree();
    void updateBindingButtons();
    void loadPersistedBindings();
    void loadDefaultBindings();
    void discardUnsavedBindingChanges();
    void addBinding();
    void editBinding();
    void removeBinding();
    void unmapBinding();
    void resetSelectedBinding();

    void load();
    void apply();
    void reset();

    Fooyin::SettingsManager* m_settingsManager{nullptr};
    VimMotionsBindingBackend* m_settingsBackend{nullptr};
    QList<BindingDefinition> m_bindingDefinitions;
    QSpinBox* m_pendingSequenceTimeout{nullptr};
    QCheckBox* m_wrapScan{nullptr};
    QCheckBox* m_useVimMotionsInSettings{nullptr};
    QCheckBox* m_useDefaultBindings{nullptr};
    QTreeView* m_bindingsTree{nullptr};
    QStandardItemModel* m_bindingsModel{nullptr};
    QSortFilterProxyModel* m_bindingsProxyModel{nullptr};
    QPushButton* m_addBindingButton{nullptr};
    QPushButton* m_editBindingButton{nullptr};
    QPushButton* m_removeBindingButton{nullptr};
    QPushButton* m_unmapBindingButton{nullptr};
    QPushButton* m_resetBindingButton{nullptr};
    QPushButton* m_discardBindingChangesButton{nullptr};
    QDialogButtonBox* m_buttons{nullptr};
};

} // namespace Fooyin::VimMotions
