#include "vimmotionssettingsdialog.h"

#include "vimmotionsbindingbackend.h"
#include "vimmotionssettings.h"

#include <utils/settings/settingsmanager.h>

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace Fooyin::VimMotions {

VimMotionsSettingsDialog::VimMotionsSettingsDialog(Fooyin::SettingsManager* settingsManager,
                                                   VimMotionsBindingBackend* settingsBackend, QWidget* parent)
    : QDialog{parent}
    , m_settingsManager{settingsManager}
    , m_settingsBackend{settingsBackend}
    , m_pendingSequenceTimeout{new QSpinBox(this)}
    , m_wrapScan{new QCheckBox(tr("Wrap scan"), this)}
    , m_useDefaultBindings{new QCheckBox(tr("Use default bindings"), this)}
    , m_bindingsTree{new QTreeView(this)}
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
    m_wrapScan->setChecked(true);

    m_useDefaultBindings->setObjectName(u"useDefaultBindings"_s);
    m_useDefaultBindings->setChecked(true);

    m_bindingsTree->setObjectName(u"effectiveBindingsTree"_s);
    m_bindingsTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_bindingsTree->setSelectionMode(QAbstractItemView::NoSelection);
    m_bindingsTree->setRootIsDecorated(false);
    m_bindingsTree->setUniformRowHeights(true);
    m_bindingsTree->setAlternatingRowColors(true);

    auto* bindingModel = new QStandardItemModel(this);
    bindingModel->setHorizontalHeaderLabels({tr("Mode"), tr("Keys"), tr("Action"), tr("Source")});
    m_bindingsTree->setModel(bindingModel);
    m_bindingsTree->header()->setStretchLastSection(true);

    auto* form = new QFormLayout();
    form->addRow(tr("Pending sequence timeout"), m_pendingSequenceTimeout);

    auto* contentLabel = new QLabel(tr("Effective bindings"), this);
    contentLabel->setBuddy(m_bindingsTree);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_wrapScan);
    layout->addWidget(m_useDefaultBindings);
    layout->addWidget(contentLabel);
    layout->addWidget(m_bindingsTree);
    layout->addWidget(m_buttons);

    QObject::connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    QObject::connect(m_buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
                     &VimMotionsSettingsDialog::apply);
    QObject::connect(m_buttons->button(QDialogButtonBox::Reset), &QPushButton::clicked, this,
                     &VimMotionsSettingsDialog::reset);

    load();
}

void VimMotionsSettingsDialog::accept()
{
    apply();
    QDialog::accept();
}

void VimMotionsSettingsDialog::load()
{
    if(m_settingsBackend) {
        m_settingsBackend->reloadBindings();
    }

    if(m_settingsManager) {
        m_pendingSequenceTimeout->setValue(
            qMax(0, m_settingsManager->value<Settings::VimMotions::PendingSequenceTimeout>()));
        m_wrapScan->setChecked(m_settingsManager->value<Settings::VimMotions::WrapScan>());
        m_useDefaultBindings->setChecked(m_settingsManager->value<Settings::VimMotions::UseDefaultBindings>());
        return;
    }

    m_pendingSequenceTimeout->setValue(0);
    m_wrapScan->setChecked(true);
    m_useDefaultBindings->setChecked(true);
}

void VimMotionsSettingsDialog::apply()
{
    if(m_settingsManager) {
        m_settingsManager->set<Settings::VimMotions::PendingSequenceTimeout>(m_pendingSequenceTimeout->value());
        m_settingsManager->set<Settings::VimMotions::WrapScan>(m_wrapScan->isChecked());
        m_settingsManager->set<Settings::VimMotions::UseDefaultBindings>(m_useDefaultBindings->isChecked());
    }

    if(m_settingsBackend) {
        m_settingsBackend->reloadBindings();
    }

    load();
}

void VimMotionsSettingsDialog::reset()
{
    if(m_settingsManager) {
        m_settingsManager->reset<Settings::VimMotions::PendingSequenceTimeout>();
        m_settingsManager->reset<Settings::VimMotions::WrapScan>();
        m_settingsManager->reset<Settings::VimMotions::UseDefaultBindings>();
    }

    if(m_settingsBackend) {
        m_settingsBackend->reloadBindings();
    }

    load();
}

} // namespace Fooyin::VimMotions
