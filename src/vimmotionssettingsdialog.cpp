#include "vimmotionssettingsdialog.h"

#include "vimmotionsbindingbackend.h"
#include "vimmotionssettings.h"

#include <utils/settings/settingsmanager.h>

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace Fooyin::VimMotions {

namespace {

enum BindingRoles
{
    ModeRole = Qt::UserRole + 1,
    KeysRole,
    SourceRole,
    StatusRole,
};

QString modeText(BindingMode mode)
{
    switch(mode) {
        case BindingMode::Normal:
            return QApplication::translate("VimMotionsSettingsDialog", "Normal");
        case BindingMode::Visual:
            return QApplication::translate("VimMotionsSettingsDialog", "Visual");
        case BindingMode::Insert:
            return QApplication::translate("VimMotionsSettingsDialog", "Insert");
    }

    return QApplication::translate("VimMotionsSettingsDialog", "Normal");
}

QString sourceText(BindingRowSource source)
{
    switch(source) {
        case BindingRowSource::Default:
            return QApplication::translate("VimMotionsSettingsDialog", "Default");
        case BindingRowSource::Custom:
            return QApplication::translate("VimMotionsSettingsDialog", "Custom");
        case BindingRowSource::CustomOverride:
            return QApplication::translate("VimMotionsSettingsDialog", "Custom override");
    }

    return QApplication::translate("VimMotionsSettingsDialog", "Default");
}

QString statusText(BindingRowStatus status)
{
    switch(status) {
        case BindingRowStatus::Active:
            return QApplication::translate("VimMotionsSettingsDialog", "Active");
        case BindingRowStatus::Disabled:
            return QApplication::translate("VimMotionsSettingsDialog", "Disabled");
        case BindingRowStatus::Unmapped:
            return QApplication::translate("VimMotionsSettingsDialog", "Unmapped");
    }

    return QApplication::translate("VimMotionsSettingsDialog", "Active");
}

QString bindingActionText(const BindingRow& row)
{
    if(row.status == BindingRowStatus::Unmapped)
        return QApplication::translate("VimMotionsSettingsDialog", "Unmapped");

    if(row.args.isEmpty())
        return row.actionName;

    return row.actionName + u':' + row.args;
}

void populateBindingsTree(QStandardItemModel* model, const QList<BindingRow>& rows, const QPalette& palette)
{
    if(!model)
        return;

    model->clear();
    model->setHorizontalHeaderLabels({QObject::tr("Mode"), QObject::tr("Keys"), QObject::tr("Action"),
                                      QObject::tr("Source"), QObject::tr("Status")});

    const auto inactiveBrush = palette.brush(QPalette::Disabled, QPalette::Text);

    for(const auto& row : rows) {
        QList<QStandardItem*> items;
        items.reserve(5);

        auto* modeItem   = new QStandardItem(modeText(row.mode));
        auto* keysItem   = new QStandardItem(row.keys);
        auto* actionItem = new QStandardItem(bindingActionText(row));
        auto* sourceItem = new QStandardItem(sourceText(row.source));
        auto* statusItem = new QStandardItem(statusText(row.status));

        for(auto* item : {modeItem, keysItem, actionItem, sourceItem, statusItem}) {
            item->setEditable(false);
            item->setData(static_cast<int>(row.mode), ModeRole);
            item->setData(row.keys, KeysRole);
            item->setData(static_cast<int>(row.source), SourceRole);
            item->setData(static_cast<int>(row.status), StatusRole);

            if(row.status != BindingRowStatus::Active)
                item->setForeground(inactiveBrush);

            items.push_back(item);
        }

        model->appendRow(items);
    }
}

class BindingEditDialog : public QDialog
{
public:
    explicit BindingEditDialog(QWidget* parent = nullptr)
        : QDialog(parent)
        , m_mode(new QComboBox(this))
        , m_keys(new QLineEdit(this))
        , m_actionName(new QLineEdit(this))
        , m_args(new QLineEdit(this))
        , m_buttons(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
    {
        setModal(true);
        setWindowTitle(QApplication::translate("VimMotionsSettingsDialog", "Edit binding"));

        m_mode->setObjectName(u"bindingMode"_s);
        m_mode->addItem(modeText(BindingMode::Normal), static_cast<int>(BindingMode::Normal));
        m_mode->addItem(modeText(BindingMode::Visual), static_cast<int>(BindingMode::Visual));
        m_mode->addItem(modeText(BindingMode::Insert), static_cast<int>(BindingMode::Insert));

        m_keys->setObjectName(u"bindingKeys"_s);
        m_actionName->setObjectName(u"bindingActionName"_s);
        m_args->setObjectName(u"bindingArgs"_s);
        m_buttons->setObjectName(u"bindingEditButtons"_s);

        auto* form = new QFormLayout(this);
        form->addRow(QApplication::translate("VimMotionsSettingsDialog", "Mode"), m_mode);
        form->addRow(QApplication::translate("VimMotionsSettingsDialog", "Keys"), m_keys);
        form->addRow(QApplication::translate("VimMotionsSettingsDialog", "Action"), m_actionName);
        form->addRow(QApplication::translate("VimMotionsSettingsDialog", "Arguments"), m_args);
        form->addRow(m_buttons);

        QObject::connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        QObject::connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    void setBinding(BindingMode mode, const QString& keys, const QString& actionName, const QString& args)
    {
        m_mode->setCurrentIndex(m_mode->findData(static_cast<int>(mode)));
        m_keys->setText(keys);
        m_actionName->setText(actionName);
        m_args->setText(args);
    }

    [[nodiscard]] BindingMode mode() const
    {
        return static_cast<BindingMode>(m_mode->currentData().toInt());
    }

    [[nodiscard]] QString keys() const
    {
        return m_keys->text().trimmed();
    }

    [[nodiscard]] QString actionName() const
    {
        return m_actionName->text().trimmed();
    }

    [[nodiscard]] QString args() const
    {
        return m_args->text().trimmed();
    }

private:
    QComboBox* m_mode;
    QLineEdit* m_keys;
    QLineEdit* m_actionName;
    QLineEdit* m_args;
    QDialogButtonBox* m_buttons;
};

} // namespace

VimMotionsSettingsDialog::VimMotionsSettingsDialog(Fooyin::SettingsManager* settingsManager,
                                                   VimMotionsBindingBackend* settingsBackend, QWidget* parent)
    : QDialog{parent}
    , m_settingsManager{settingsManager}
    , m_settingsBackend{settingsBackend}
    , m_pendingSequenceTimeout{new QSpinBox(this)}
    , m_wrapScan{new QCheckBox(tr("Wrap scan"), this)}
    , m_useDefaultBindings{new QCheckBox(tr("Use default bindings"), this)}
    , m_bindingsTree{new QTreeView(this)}
    , m_addBindingButton{new QPushButton(tr("Add"), this)}
    , m_editBindingButton{new QPushButton(tr("Edit"), this)}
    , m_removeBindingButton{new QPushButton(tr("Remove"), this)}
    , m_unmapBindingButton{new QPushButton(tr("Unmap"), this)}
    , m_resetBindingButton{new QPushButton(tr("Reset Binding"), this)}
    , m_discardBindingChangesButton{new QPushButton(tr("Discard Binding Changes"), this)}
    , m_buttons{new QDialogButtonBox(
          QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Reset | QDialogButtonBox::Cancel, this)}
{
    setWindowTitle(QApplication::translate("VimMotionsSettingsDialog", "Vim Motions Settings"));
    setModal(true);

    m_pendingSequenceTimeout->setObjectName(u"pendingSequenceTimeout"_s);
    m_pendingSequenceTimeout->setRange(0, 60000);
    m_pendingSequenceTimeout->setSingleStep(50);
    m_pendingSequenceTimeout->setSuffix(u" ms"_s);

    m_wrapScan->setObjectName(u"wrapScan"_s);
    m_useDefaultBindings->setObjectName(u"useDefaultBindings"_s);

    m_bindingsTree->setObjectName(u"effectiveBindingsTree"_s);
    m_bindingsTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_bindingsTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_bindingsTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_bindingsTree->setRootIsDecorated(false);
    m_bindingsTree->setUniformRowHeights(true);
    m_bindingsTree->setAlternatingRowColors(true);

    for(auto* button : {m_addBindingButton, m_editBindingButton, m_removeBindingButton, m_unmapBindingButton,
                        m_resetBindingButton, m_discardBindingChangesButton}) {
        button->setAutoDefault(false);
    }

    m_addBindingButton->setObjectName(u"addBinding"_s);
    m_editBindingButton->setObjectName(u"editBinding"_s);
    m_removeBindingButton->setObjectName(u"removeBinding"_s);
    m_unmapBindingButton->setObjectName(u"unmapBinding"_s);
    m_resetBindingButton->setObjectName(u"resetBinding"_s);
    m_discardBindingChangesButton->setObjectName(u"discardBindingChanges"_s);

    auto* bindingModel = new QStandardItemModel(this);
    m_bindingsTree->setModel(bindingModel);
    m_bindingsTree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_bindingsTree->header()->setStretchLastSection(true);

    auto* form = new QFormLayout();
    form->addRow(tr("Pending sequence timeout"), m_pendingSequenceTimeout);

    auto* contentLabel = new QLabel(tr("Effective bindings"), this);
    contentLabel->setBuddy(m_bindingsTree);

    auto* bindingsButtonsLayout = new QVBoxLayout();
    bindingsButtonsLayout->addWidget(m_addBindingButton);
    bindingsButtonsLayout->addWidget(m_editBindingButton);
    bindingsButtonsLayout->addWidget(m_removeBindingButton);
    bindingsButtonsLayout->addWidget(m_unmapBindingButton);
    bindingsButtonsLayout->addWidget(m_resetBindingButton);
    bindingsButtonsLayout->addWidget(m_discardBindingChangesButton);
    bindingsButtonsLayout->addStretch();

    auto* bindingsLayout = new QHBoxLayout();
    bindingsLayout->addWidget(m_bindingsTree, 1);
    bindingsLayout->addLayout(bindingsButtonsLayout);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_wrapScan);
    layout->addWidget(m_useDefaultBindings);
    layout->addWidget(contentLabel);
    layout->addLayout(bindingsLayout);
    layout->addWidget(m_buttons);

    load();

    QObject::connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    QObject::connect(m_buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
                     &VimMotionsSettingsDialog::apply);
    QObject::connect(m_buttons->button(QDialogButtonBox::Reset), &QPushButton::clicked, this,
                     &VimMotionsSettingsDialog::reset);
    QObject::connect(m_bindingsTree->selectionModel(), &QItemSelectionModel::selectionChanged, this,
                     [this]() { updateBindingButtons(); });
    QObject::connect(m_useDefaultBindings, &QCheckBox::toggled, this, [this]() { refreshBindingsTree(); });
    QObject::connect(m_addBindingButton, &QPushButton::clicked, this, &VimMotionsSettingsDialog::addBinding);
    QObject::connect(m_editBindingButton, &QPushButton::clicked, this, &VimMotionsSettingsDialog::editBinding);
    QObject::connect(m_removeBindingButton, &QPushButton::clicked, this, &VimMotionsSettingsDialog::removeBinding);
    QObject::connect(m_unmapBindingButton, &QPushButton::clicked, this, &VimMotionsSettingsDialog::unmapBinding);
    QObject::connect(m_resetBindingButton, &QPushButton::clicked, this,
                     &VimMotionsSettingsDialog::resetSelectedBinding);
    QObject::connect(m_discardBindingChangesButton, &QPushButton::clicked, this,
                     &VimMotionsSettingsDialog::discardUnsavedBindingChanges);
}

void VimMotionsSettingsDialog::accept()
{
    apply();
    QDialog::accept();
}

void VimMotionsSettingsDialog::refreshBindingsTree()
{
    auto* bindingModel = qobject_cast<QStandardItemModel*>(m_bindingsTree->model());
    if(!bindingModel)
        return;

    const QModelIndex currentIndex = m_bindingsTree->currentIndex();
    const int currentMode          = currentIndex.data(ModeRole).toInt();
    const QString currentKeys      = currentIndex.data(KeysRole).toString();

    populateBindingsTree(bindingModel,
                         m_settingsBackend
                             ? m_settingsBackend->bindingRows(m_bindingDefinitions, m_useDefaultBindings->isChecked())
                             : QList<BindingRow>{},
                         palette());

    if(!currentKeys.isEmpty()) {
        for(int row = 0; row < bindingModel->rowCount(); ++row) {
            const QModelIndex index = bindingModel->index(row, 0);
            if(index.data(ModeRole).toInt() == currentMode && index.data(KeysRole).toString() == currentKeys) {
                m_bindingsTree->setCurrentIndex(index);
                break;
            }
        }
    }

    updateBindingButtons();
}

void VimMotionsSettingsDialog::updateBindingButtons()
{
    const QModelIndex currentIndex = m_bindingsTree->currentIndex();
    const bool hasSelection        = currentIndex.isValid();
    const auto mode                = static_cast<BindingMode>(currentIndex.data(ModeRole).toInt());
    const QString keys             = currentIndex.data(KeysRole).toString();
    const auto status              = static_cast<BindingRowStatus>(currentIndex.data(StatusRole).toInt());

    const auto definitionIt = hasSelection ? std::find_if(m_bindingDefinitions.cbegin(), m_bindingDefinitions.cend(),
                                                          [mode, &keys](const auto& candidate) {
                                                              return candidate.mode == mode && candidate.keys == keys;
                                                          })
                                           : m_bindingDefinitions.cend();
    const BindingDefinition* definition = definitionIt == m_bindingDefinitions.cend() ? nullptr : &(*definitionIt);

    const bool canEdit   = definition && !definition->isDefaultBinding();
    const bool canRemove = definition && !definition->isDefaultBinding();
    const bool canUnmap  = definition && definition->isDefaultBinding() && status != BindingRowStatus::Unmapped;
    const bool canReset  = definition && (definition->customValue.has_value() || !definition->isDefaultBinding());

    m_editBindingButton->setEnabled(canEdit);
    m_removeBindingButton->setEnabled(canRemove);
    m_unmapBindingButton->setEnabled(canUnmap);
    m_resetBindingButton->setEnabled(canReset);
    m_discardBindingChangesButton->setEnabled(true);

    m_removeBindingButton->setText(tr("Remove"));
}

void VimMotionsSettingsDialog::loadPersistedBindings()
{
    m_bindingDefinitions = m_settingsBackend ? m_settingsBackend->bindingDefinitions() : QList<BindingDefinition>{};
}

void VimMotionsSettingsDialog::loadDefaultBindings()
{
    m_bindingDefinitions
        = m_settingsBackend ? m_settingsBackend->defaultBindingDefinitions() : QList<BindingDefinition>{};
}

void VimMotionsSettingsDialog::discardUnsavedBindingChanges()
{
    loadPersistedBindings();
    refreshBindingsTree();
}

void VimMotionsSettingsDialog::addBinding()
{
    if(!m_settingsBackend)
        return;

    BindingEditDialog dialog(this);
    dialog.setBinding(BindingMode::Normal, {}, {}, {});

    if(dialog.exec() != QDialog::Accepted)
        return;

    if(!m_settingsBackend->addCustomBinding(m_bindingDefinitions, dialog.mode(), dialog.keys(), dialog.actionName(),
                                            dialog.args())) {
        QMessageBox::warning(this, windowTitle(), tr("Could not add binding. Check for duplicate or invalid keys."));
        return;
    }

    refreshBindingsTree();
}

void VimMotionsSettingsDialog::editBinding()
{
    if(!m_settingsBackend || !m_bindingsTree->currentIndex().isValid())
        return;

    const QModelIndex index        = m_bindingsTree->currentIndex();
    const BindingMode originalMode = static_cast<BindingMode>(index.data(ModeRole).toInt());
    const QString originalKeys     = index.data(KeysRole).toString();

    auto definitionIt = std::find_if(m_bindingDefinitions.begin(), m_bindingDefinitions.end(),
                                     [originalMode, &originalKeys](const auto& definition) {
                                         return definition.mode == originalMode && definition.keys == originalKeys;
                                     });
    if(definitionIt == m_bindingDefinitions.end())
        return;

    if(definitionIt->isDefaultBinding())
        return;

    const QString value
        = definitionIt->customValue.has_value() ? *definitionIt->customValue : definitionIt->defaultValue;
    const BindingEntry entry = parseBindingString(definitionIt->keys, value);

    BindingEditDialog dialog(this);
    dialog.setBinding(definitionIt->mode, definitionIt->keys, entry.actionName, entry.args);
    if(dialog.exec() != QDialog::Accepted)
        return;

    if(!m_settingsBackend->updateCustomBinding(m_bindingDefinitions, originalMode, originalKeys, dialog.mode(),
                                               dialog.keys(), dialog.actionName(), dialog.args())) {
        QMessageBox::warning(this, windowTitle(), tr("Could not update binding. Check for duplicate or invalid keys."));
        return;
    }

    refreshBindingsTree();
}

void VimMotionsSettingsDialog::removeBinding()
{
    if(!m_settingsBackend || !m_bindingsTree->currentIndex().isValid())
        return;

    const QModelIndex index = m_bindingsTree->currentIndex();
    if(m_settingsBackend->removeCustomBinding(m_bindingDefinitions,
                                              static_cast<BindingMode>(index.data(ModeRole).toInt()),
                                              index.data(KeysRole).toString())) {
        refreshBindingsTree();
    }
}

void VimMotionsSettingsDialog::unmapBinding()
{
    if(!m_settingsBackend || !m_bindingsTree->currentIndex().isValid())
        return;

    const QModelIndex index = m_bindingsTree->currentIndex();
    if(!m_settingsBackend->unmapBinding(m_bindingDefinitions, static_cast<BindingMode>(index.data(ModeRole).toInt()),
                                        index.data(KeysRole).toString())) {
        return;
    }

    refreshBindingsTree();
}

void VimMotionsSettingsDialog::resetSelectedBinding()
{
    if(!m_settingsBackend || !m_bindingsTree->currentIndex().isValid())
        return;

    const QModelIndex index = m_bindingsTree->currentIndex();
    if(m_settingsBackend->resetBinding(m_bindingDefinitions, static_cast<BindingMode>(index.data(ModeRole).toInt()),
                                       index.data(KeysRole).toString())) {
        refreshBindingsTree();
    }
}

void VimMotionsSettingsDialog::load()
{
    if(m_settingsBackend)
        m_settingsBackend->reloadBindings();

    loadPersistedBindings();

    if(m_settingsManager) {
        m_pendingSequenceTimeout->setValue(
            qMax(0, m_settingsManager->value<Settings::VimMotions::PendingSequenceTimeout>()));
        m_wrapScan->setChecked(m_settingsManager->value<Settings::VimMotions::WrapScan>());
        m_useDefaultBindings->setChecked(m_settingsManager->value<Settings::VimMotions::UseDefaultBindings>());
    }
    else {
        m_pendingSequenceTimeout->setValue(0);
        m_wrapScan->setChecked(true);
        m_useDefaultBindings->setChecked(true);
    }

    refreshBindingsTree();
}

void VimMotionsSettingsDialog::apply()
{
    if(m_settingsManager) {
        m_settingsManager->set<Settings::VimMotions::PendingSequenceTimeout>(m_pendingSequenceTimeout->value());
        m_settingsManager->set<Settings::VimMotions::WrapScan>(m_wrapScan->isChecked());
        m_settingsManager->set<Settings::VimMotions::UseDefaultBindings>(m_useDefaultBindings->isChecked());
    }

    if(m_settingsBackend) {
        if(!m_settingsBackend->saveBindingDefinitions(m_bindingDefinitions)) {
            QMessageBox::warning(this, windowTitle(), tr("Could not save Vim Motions bindings."));
            return;
        }
        m_settingsBackend->reloadBindings();
    }

    load();
}

void VimMotionsSettingsDialog::reset()
{
    m_pendingSequenceTimeout->setValue(0);
    m_wrapScan->setChecked(true);
    m_useDefaultBindings->setChecked(true);
    loadDefaultBindings();
    refreshBindingsTree();
}

} // namespace Fooyin::VimMotions
