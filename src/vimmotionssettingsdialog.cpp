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

namespace {

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

QString namedKeyText(Qt::Key key)
{
    switch(key) {
        case Qt::Key_Escape:
            return QStringLiteral("<Esc>");
        case Qt::Key_Return:
            return QStringLiteral("<CR>");
        case Qt::Key_Enter:
            return QStringLiteral("<Enter>");
        case Qt::Key_Tab:
            return QStringLiteral("<Tab>");
        case Qt::Key_Backspace:
            return QStringLiteral("<BS>");
        case Qt::Key_Space:
            return QStringLiteral("<Space>");
        case Qt::Key_Delete:
            return QStringLiteral("<Del>");
        case Qt::Key_Insert:
            return QStringLiteral("<Insert>");
        case Qt::Key_Home:
            return QStringLiteral("<Home>");
        case Qt::Key_End:
            return QStringLiteral("<End>");
        case Qt::Key_PageUp:
            return QStringLiteral("<PageUp>");
        case Qt::Key_PageDown:
            return QStringLiteral("<PageDown>");
        case Qt::Key_Left:
            return QStringLiteral("<Left>");
        case Qt::Key_Right:
            return QStringLiteral("<Right>");
        case Qt::Key_Up:
            return QStringLiteral("<Up>");
        case Qt::Key_Down:
            return QStringLiteral("<Down>");
        case Qt::Key_Slash:
            return QStringLiteral("<Slash>");
        case Qt::Key_Backslash:
            return QStringLiteral("<Bslash>");
        case Qt::Key_Bar:
            return QStringLiteral("<Bar>");
        case Qt::Key_Less:
            return QStringLiteral("<Lt>");
        default:
            return {};
    }
}

QString comboText(const KeyCombo& combo)
{
    QString text;

    if(combo.modifiers & Qt::ControlModifier)
        text += QStringLiteral("Ctrl+");
    if(combo.modifiers & Qt::AltModifier)
        text += QStringLiteral("Alt+");
    if(combo.modifiers & Qt::ShiftModifier)
        text += QStringLiteral("Shift+");
    if(combo.modifiers & Qt::MetaModifier)
        text += QStringLiteral("Meta+");

    if(!combo.ch.isNull() && combo.modifiers == Qt::NoModifier) {
        text += combo.ch;
        return text;
    }

    if(!combo.ch.isNull() && combo.modifiers != Qt::NoModifier) {
        text += combo.ch.toUpper();
        return text;
    }

    const QString namedText = namedKeyText(combo.key);
    if(!namedText.isEmpty()) {
        text += namedText;
        return text;
    }

    text += QStringLiteral("<Unknown>");
    return text;
}

QString bindingKeysText(const BindingEntry& entry)
{
    QString text;
    for(const auto& combo : entry.keys)
        text += comboText(combo);
    return text;
}

QString bindingActionText(const BindingEntry& entry)
{
    if(entry.args.isEmpty())
        return entry.actionName;

    return entry.actionName + u':' + entry.args;
}

void populateBindingsTree(QStandardItemModel* model, const QHash<BindingMode, QList<BindingEntry>>& bindings)
{
    if(!model)
        return;

    model->clear();
    model->setHorizontalHeaderLabels(
        {QObject::tr("Mode"), QObject::tr("Keys"), QObject::tr("Action"), QObject::tr("Source")});

    const QList<BindingMode> modeOrder{BindingMode::Normal, BindingMode::Visual, BindingMode::Insert};

    for(const BindingMode mode : modeOrder) {
        const auto modeBindings = bindings.value(mode);
        for(const auto& entry : modeBindings) {
            QList<QStandardItem*> row;
            row.reserve(4);

            auto* modeItem   = new QStandardItem(modeText(mode));
            auto* keysItem   = new QStandardItem(bindingKeysText(entry));
            auto* actionItem = new QStandardItem(bindingActionText(entry));
            auto* sourceItem = new QStandardItem();

            modeItem->setEditable(false);
            keysItem->setEditable(false);
            actionItem->setEditable(false);
            sourceItem->setEditable(false);

            row.push_back(modeItem);
            row.push_back(keysItem);
            row.push_back(actionItem);
            row.push_back(sourceItem);

            model->appendRow(row);
        }
    }
}

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

    if(auto* bindingModel = qobject_cast<QStandardItemModel*>(m_bindingsTree->model())) {
        populateBindingsTree(bindingModel, m_settingsBackend ? m_settingsBackend->effectiveBindings()
                                                             : QHash<BindingMode, QList<BindingEntry>>{});
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
