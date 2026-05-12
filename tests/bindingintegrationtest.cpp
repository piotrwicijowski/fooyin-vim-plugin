#include "vimbindingparser.h"
#include "vimmotionsbindingbackend.h"
#include "vimmotionssettings.h"
#include "vimmotionssettingsdialog.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>

#include <utils/settings/settingsmanager.h>

using namespace Fooyin::VimMotions;

class TestConfigFileBindings : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_tempDir;
    QString m_filePath;

    QStringList loadParsedBindings(const QSet<QString>& skipDefaults = {}, bool skipEmpty = false)
    {
        static const QString group = QStringLiteral("VimMotions");

        QSettings settings(m_filePath, QSettings::IniFormat);
        settings.beginGroup(group);
        const QStringList allKeys = settings.allKeys();
        QStringList result;

        for(const QString& key : allKeys) {
            if(!key.startsWith(QStringLiteral("Bindings/")))
                continue;

            const QString fullKey = group + u'/' + key;
            if(skipDefaults.contains(fullKey))
                continue;

            const QString val = settings.value(key).toString();
            if(skipEmpty && val.isEmpty())
                continue;

            auto entry = parseBindingString(key.section(u'/', -1), val);
            if(entry.actionName.isEmpty())
                continue;

            result.append(u'[' + key.section(u'/', 1, 1) + QStringLiteral("] ") + key.section(u'/', -1)
                          + QStringLiteral(" = ") + entry.actionName);
        }
        settings.endGroup();
        return result;
    }

    int loadPendingSequenceTimeout() const
    {
        QSettings settings(m_filePath, QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("VimMotions"));
        const int timeout = settings.value(QStringLiteral("PendingSequenceTimeout"), 0).toInt();
        settings.endGroup();
        return timeout;
    }

    static bool hasBinding(const QList<BindingEntry>& bindings, QChar key, const QString& action)
    {
        for(const auto& binding : bindings) {
            if(binding.actionName != action || binding.keys.size() != 1)
                continue;

            if(binding.keys.front().ch == key)
                return true;
        }

        return false;
    }

    static bool hasTreeRow(const QStandardItemModel* model, const QString& mode, const QString& keys,
                           const QString& action)
    {
        if(!model)
            return false;

        for(int row = 0; row < model->rowCount(); ++row) {
            if(model->data(model->index(row, 0)).toString() != mode)
                continue;
            if(model->data(model->index(row, 1)).toString() != keys)
                continue;
            if(model->data(model->index(row, 2)).toString() != action)
                continue;
            return true;
        }

        return false;
    }

    static int findTreeRow(const QStandardItemModel* model, const QString& mode, const QString& keys)
    {
        if(!model)
            return -1;

        for(int row = 0; row < model->rowCount(); ++row) {
            if(model->data(model->index(row, 0)).toString() == mode
               && model->data(model->index(row, 1)).toString() == keys) {
                return row;
            }
        }

        return -1;
    }

    static QString treeCell(const QStandardItemModel* model, int row, int column)
    {
        if(!model || row < 0)
            return {};

        return model->data(model->index(row, column)).toString();
    }

    static void selectTreeRow(QTreeView* tree, const QStandardItemModel* model, const QString& mode,
                              const QString& keys)
    {
        QVERIFY(tree);
        const int row = findTreeRow(model, mode, keys);
        QVERIFY(row >= 0);
        tree->setCurrentIndex(model->index(row, 0));
    }

    static void acceptBindingEditor(BindingMode mode, const QString& keys, const QString& actionName,
                                    const QString& args = {})
    {
        QTimer::singleShot(0, [mode, keys, actionName, args]() {
            auto* dialog = qobject_cast<QDialog*>(qApp->activeModalWidget());
            QVERIFY(dialog);

            auto* modeBox    = dialog->findChild<QComboBox*>(QStringLiteral("bindingMode"));
            auto* keysEdit   = dialog->findChild<QLineEdit*>(QStringLiteral("bindingKeys"));
            auto* actionEdit = dialog->findChild<QLineEdit*>(QStringLiteral("bindingActionName"));
            auto* argsEdit   = dialog->findChild<QLineEdit*>(QStringLiteral("bindingArgs"));
            auto* buttons    = dialog->findChild<QDialogButtonBox*>(QStringLiteral("bindingEditButtons"));

            QVERIFY(modeBox);
            QVERIFY(keysEdit);
            QVERIFY(actionEdit);
            QVERIFY(argsEdit);
            QVERIFY(buttons);

            modeBox->setCurrentIndex(modeBox->findData(static_cast<int>(mode)));
            keysEdit->setText(keys);
            actionEdit->setText(actionName);
            argsEdit->setText(args);

            auto* okButton = buttons->button(QDialogButtonBox::Ok);
            QVERIFY(okButton);
            okButton->click();
        });
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tempDir.isValid());
        m_filePath = m_tempDir.filePath(QStringLiteral("test.conf"));

        QFile file(m_filePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("[VimMotions]\n"
                   "Bindings\\Normal\\j=moveCursor:+1\n"
                   "Bindings\\Normal\\k=moveCursor:-1\n"
                   "Bindings\\Normal\\m=beginSetMark\n"
                   "Bindings\\Normal\\'=beginJumpToMark\n"
                   "Bindings\\Normal\\`=beginJumpToMark\n"
                   "Bindings\\Normal\\a=organiserCreatePlaylist\n"
                   "Bindings\\Normal\\A=organiserCreateGroup\n"
                   "Bindings\\Normal\\dd=\n"
                   "Bindings\\Normal\\d=deleteRows\n"
                   "Bindings\\Normal\\f=deleteRows\n"
                   "Bindings\\Normal\\Ctrl+A=selectAll\n"
                   "Bindings\\Normal\\<Space>=fooyinAction:Playback.Next\n"
                   "Bindings\\Normal\\g<Space>=focusNowPlaying\n"
                   "Bindings\\Normal\\yc=copyAfterCurrentPlaying\n"
                   "Bindings\\Normal\\dc=moveAfterCurrentPlaying\n"
                   "Bindings\\Visual\\j=extendCursor:+1\n"
                   "Bindings\\Visual\\v=leaveVisualMode\n"
                   "Bindings\\Visual\\Ctrl+J=spatialMoveFocus:down\n");
        file.close();
    }

    void testAllBindings()
    {
        auto bindings = loadParsedBindings({}, false);
        QCOMPARE(bindings.size(), 17);
        QVERIFY(bindings.contains(QStringLiteral("[Normal] j = moveCursor")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] k = moveCursor")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] m = beginSetMark")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] ' = beginJumpToMark")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] ` = beginJumpToMark")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] a = organiserCreatePlaylist")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] A = organiserCreateGroup")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] d = deleteRows")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] f = deleteRows")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] Ctrl+A = selectAll")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] <Space> = fooyinAction")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] g<Space> = focusNowPlaying")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] yc = copyAfterCurrentPlaying")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] dc = moveAfterCurrentPlaying")));
        QVERIFY(bindings.contains(QStringLiteral("[Visual] j = extendCursor")));
        QVERIFY(bindings.contains(QStringLiteral("[Visual] v = leaveVisualMode")));
        QVERIFY(bindings.contains(QStringLiteral("[Visual] Ctrl+J = spatialMoveFocus")));
    }

    void testSkipEmptyValues()
    {
        auto bindings = loadParsedBindings({}, true);
        QCOMPARE(bindings.size(), 17);
    }

    void testSkipDefaults()
    {
        QSet<QString> defaults;
        defaults.insert(QStringLiteral("VimMotions/Bindings/Normal/j"));

        auto bindings = loadParsedBindings(defaults, true);

        QCOMPARE(bindings.size(), 16);
    }

    void testAllSkipped()
    {
        QSet<QString> skipAll;
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/j"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/k"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/m"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/'"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/`"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/a"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/A"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/dd"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/d"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/f"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/Ctrl+A"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/<Space>"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/g<Space>"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/yc"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/dc"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Visual/j"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Visual/v"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Visual/Ctrl+J"));

        auto bindings = loadParsedBindings(skipAll, true);
        QCOMPARE(bindings.size(), 0);
    }

    void testNonDefaultKeysOnly()
    {
        QSet<QString> defaults;
        defaults.insert(QStringLiteral("VimMotions/Bindings/Normal/j"));
        defaults.insert(QStringLiteral("VimMotions/Bindings/Normal/k"));

        auto bindings = loadParsedBindings(defaults, true);

        QCOMPARE(bindings.size(), 15);
        QVERIFY(bindings.contains(QStringLiteral("[Normal] d = deleteRows")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] f = deleteRows")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] Ctrl+A = selectAll")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] <Space> = fooyinAction")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] g<Space> = focusNowPlaying")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] yc = copyAfterCurrentPlaying")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] dc = moveAfterCurrentPlaying")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] m = beginSetMark")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] ' = beginJumpToMark")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] ` = beginJumpToMark")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] a = organiserCreatePlaylist")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] A = organiserCreateGroup")));
        QVERIFY(bindings.contains(QStringLiteral("[Visual] v = leaveVisualMode")));
        QVERIFY(bindings.contains(QStringLiteral("[Visual] Ctrl+J = spatialMoveFocus")));
    }

    void testPendingSequenceTimeoutDefaultsToZero()
    {
        QCOMPARE(loadPendingSequenceTimeout(), 0);
    }

    void testPendingSequenceTimeoutReadsMilliseconds()
    {
        QSettings settings(m_filePath, QSettings::IniFormat);
        settings.setValue(QStringLiteral("VimMotions/PendingSequenceTimeout"), 250);
        settings.sync();
        QCOMPARE(loadPendingSequenceTimeout(), 250);
    }

    void testSettingsUiFlagDefaultsOff()
    {
        Fooyin::SettingsManager settings{m_tempDir.filePath(QStringLiteral("vim_settings.ini"))};
        VimMotionsSettings vimSettings{&settings};
        Q_UNUSED(vimSettings);

        QCOMPARE(settings.value<Fooyin::Settings::VimMotions::EnableSettingsUi>(), false);
        QCOMPARE(settings.value<Fooyin::Settings::VimMotions::UseVimMotionsInSettings>(), false);
    }

    void testBindingBackendReloadsCustomBindings()
    {
        Fooyin::SettingsManager settings{m_tempDir.filePath(QStringLiteral("vim_settings.ini"))};
        VimMotionsSettings vimSettings{&settings};
        Q_UNUSED(vimSettings);

        settings.fileSet(QStringLiteral("VimMotions/Bindings/Normal/z"), QStringLiteral("focusNowPlaying"));

        VimMotionsBindingBackend backend{&settings};
        const auto initialBindings = backend.effectiveBindings().value(BindingMode::Normal);
        QVERIFY(hasBinding(initialBindings, QChar(u'z'), QStringLiteral("focusNowPlaying")));
        QVERIFY(hasBinding(initialBindings, QChar(u'j'), QStringLiteral("moveCursor")));

        QCOMPARE(settings.set<Fooyin::Settings::VimMotions::UseDefaultBindings>(false), true);
        backend.reloadBindings();

        const auto reloadedBindings = backend.effectiveBindings().value(BindingMode::Normal);
        QVERIFY(hasBinding(reloadedBindings, QChar(u'z'), QStringLiteral("focusNowPlaying")));
        QVERIFY(!hasBinding(reloadedBindings, QChar(u'j'), QStringLiteral("moveCursor")));
    }

    void testSettingsDialogLoadsAppliesAndResets()
    {
        Fooyin::SettingsManager settings{m_tempDir.filePath(QStringLiteral("vim_settings_dialog.ini"))};
        VimMotionsSettings vimSettings{&settings};
        Q_UNUSED(vimSettings);

        settings.set<Fooyin::Settings::VimMotions::PendingSequenceTimeout>(120);
        settings.set<Fooyin::Settings::VimMotions::WrapScan>(false);
        settings.set<Fooyin::Settings::VimMotions::UseVimMotionsInSettings>(true);
        settings.set<Fooyin::Settings::VimMotions::UseDefaultBindings>(false);
        settings.fileSet(QStringLiteral("VimMotions/Bindings/Normal/z"), QStringLiteral("focusNowPlaying"));

        VimMotionsBindingBackend backend{&settings};
        VimMotionsSettingsDialog dialog{&settings, &backend};
        auto* tree = dialog.findChild<QTreeView*>(QStringLiteral("effectiveBindingsTree"));
        QVERIFY(tree);
        auto* treeModel = qobject_cast<QStandardItemModel*>(tree->model());
        QVERIFY(treeModel);
        QVERIFY(
            hasTreeRow(treeModel, QStringLiteral("Normal"), QStringLiteral("z"), QStringLiteral("focusNowPlaying")));
        int jRow = findTreeRow(treeModel, QStringLiteral("Normal"), QStringLiteral("j"));
        QVERIFY(jRow >= 0);
        QCOMPARE(treeCell(treeModel, jRow, 3), QStringLiteral("Default"));
        QCOMPARE(treeCell(treeModel, jRow, 4), QStringLiteral("Disabled"));

        auto* timeout = dialog.findChild<QSpinBox*>(QStringLiteral("pendingSequenceTimeout"));
        QVERIFY(timeout);
        QCOMPARE(timeout->value(), 120);

        auto* wrapScan = dialog.findChild<QCheckBox*>(QStringLiteral("wrapScan"));
        QVERIFY(wrapScan);
        QVERIFY(!wrapScan->isChecked());

        auto* useDefaultBindings = dialog.findChild<QCheckBox*>(QStringLiteral("useDefaultBindings"));
        QVERIFY(useDefaultBindings);
        QVERIFY(!useDefaultBindings->isChecked());

        auto* useVimMotionsInSettings = dialog.findChild<QCheckBox*>(QStringLiteral("useVimMotionsInSettings"));
        QVERIFY(useVimMotionsInSettings);
        QVERIFY(useVimMotionsInSettings->isChecked());

        timeout->setValue(450);
        wrapScan->setChecked(true);
        useVimMotionsInSettings->setChecked(false);
        useDefaultBindings->setChecked(true);

        auto* buttons = dialog.findChild<QDialogButtonBox*>();
        QVERIFY(buttons);
        auto* applyButton = buttons->button(QDialogButtonBox::Apply);
        QVERIFY(applyButton);
        applyButton->click();

        QCOMPARE(settings.value<Fooyin::Settings::VimMotions::PendingSequenceTimeout>(), 450);
        QCOMPARE(settings.value<Fooyin::Settings::VimMotions::WrapScan>(), true);
        QCOMPARE(settings.value<Fooyin::Settings::VimMotions::UseVimMotionsInSettings>(), false);
        QCOMPARE(settings.value<Fooyin::Settings::VimMotions::UseDefaultBindings>(), true);
        QCOMPARE(backend.pendingSequenceTimeout(), 450);
        QCOMPARE(backend.wrapScan(), true);
        QCOMPARE(backend.useDefaultBindings(), true);
        QVERIFY(hasTreeRow(treeModel, QStringLiteral("Normal"), QStringLiteral("j"), QStringLiteral("moveCursor:+1")));
        QVERIFY(
            hasTreeRow(treeModel, QStringLiteral("Normal"), QStringLiteral("z"), QStringLiteral("focusNowPlaying")));
        QVERIFY(hasBinding(backend.effectiveBindings().value(BindingMode::Normal), QChar(u'j'),
                           QStringLiteral("moveCursor")));

        timeout->setValue(30);
        wrapScan->setChecked(false);
        useVimMotionsInSettings->setChecked(true);
        useDefaultBindings->setChecked(false);

        auto* resetButton = buttons->button(QDialogButtonBox::Reset);
        QVERIFY(resetButton);
        resetButton->click();

        QCOMPARE(settings.value<Fooyin::Settings::VimMotions::PendingSequenceTimeout>(), 450);
        QCOMPARE(settings.value<Fooyin::Settings::VimMotions::WrapScan>(), true);
        QCOMPARE(settings.value<Fooyin::Settings::VimMotions::UseVimMotionsInSettings>(), false);
        QCOMPARE(settings.value<Fooyin::Settings::VimMotions::UseDefaultBindings>(), true);
        QCOMPARE(timeout->value(), 0);
        QVERIFY(wrapScan->isChecked());
        QVERIFY(!useVimMotionsInSettings->isChecked());
        QVERIFY(useDefaultBindings->isChecked());
        QCOMPARE(backend.pendingSequenceTimeout(), 450);
        QCOMPARE(backend.wrapScan(), true);
        QCOMPARE(backend.useDefaultBindings(), true);
        QVERIFY(hasTreeRow(treeModel, QStringLiteral("Normal"), QStringLiteral("j"), QStringLiteral("moveCursor:+1")));
        QVERIFY(
            !hasTreeRow(treeModel, QStringLiteral("Normal"), QStringLiteral("z"), QStringLiteral("focusNowPlaying")));
        QVERIFY(hasBinding(backend.effectiveBindings().value(BindingMode::Normal), QChar(u'j'),
                           QStringLiteral("moveCursor")));
        QVERIFY(hasBinding(backend.effectiveBindings().value(BindingMode::Normal), QChar(u'z'),
                           QStringLiteral("focusNowPlaying")));

        applyButton->click();

        QCOMPARE(settings.value<Fooyin::Settings::VimMotions::PendingSequenceTimeout>(), 0);
        QCOMPARE(settings.value<Fooyin::Settings::VimMotions::WrapScan>(), true);
        QCOMPARE(settings.value<Fooyin::Settings::VimMotions::UseVimMotionsInSettings>(), false);
        QCOMPARE(settings.value<Fooyin::Settings::VimMotions::UseDefaultBindings>(), true);
        QCOMPARE(backend.pendingSequenceTimeout(), 0);
        QCOMPARE(backend.wrapScan(), true);
        QCOMPARE(backend.useDefaultBindings(), true);
        QVERIFY(
            !hasTreeRow(treeModel, QStringLiteral("Normal"), QStringLiteral("z"), QStringLiteral("focusNowPlaying")));
        QVERIFY(!hasBinding(backend.effectiveBindings().value(BindingMode::Normal), QChar(u'z'),
                            QStringLiteral("focusNowPlaying")));

        useDefaultBindings->setChecked(false);
        applyButton->click();

        jRow = findTreeRow(treeModel, QStringLiteral("Normal"), QStringLiteral("j"));
        QVERIFY(jRow >= 0);
        QCOMPARE(treeCell(treeModel, jRow, 3), QStringLiteral("Default"));
        QCOMPARE(treeCell(treeModel, jRow, 4), QStringLiteral("Disabled"));
    }

    void testBindingBackendEditableOperations()
    {
        Fooyin::SettingsManager settings{m_tempDir.filePath(QStringLiteral("vim_settings_backend_editable.ini"))};
        VimMotionsSettings vimSettings{&settings};
        Q_UNUSED(vimSettings);

        VimMotionsBindingBackend backend{&settings};
        auto definitions = backend.bindingDefinitions();

        QVERIFY(backend.addCustomBinding(definitions, BindingMode::Normal, QStringLiteral("z"),
                                         QStringLiteral("focusNowPlaying"), {}));
        QVERIFY(backend.updateCustomBinding(definitions, BindingMode::Normal, QStringLiteral("j"), BindingMode::Normal,
                                            QStringLiteral("j"), QStringLiteral("focusNowPlaying"), {}));
        QVERIFY(backend.unmapBinding(definitions, BindingMode::Normal, QStringLiteral("k")));
        QVERIFY(backend.addCustomBinding(definitions, BindingMode::Normal, QStringLiteral("k"),
                                         QStringLiteral("nextMatch"), {}));

        const auto rows = backend.bindingRows(definitions, false);
        int jRow        = -1;
        int kRow        = -1;
        int zRow        = -1;
        for(int i = 0; i < rows.size(); ++i) {
            if(rows.at(i).mode == BindingMode::Normal && rows.at(i).keys == QStringLiteral("j"))
                jRow = i;
            if(rows.at(i).mode == BindingMode::Normal && rows.at(i).keys == QStringLiteral("k"))
                kRow = i;
            if(rows.at(i).mode == BindingMode::Normal && rows.at(i).keys == QStringLiteral("z"))
                zRow = i;
        }

        QVERIFY(jRow >= 0);
        QCOMPARE(rows.at(jRow).source, BindingRowSource::CustomOverride);
        QCOMPARE(rows.at(jRow).status, BindingRowStatus::Active);
        QCOMPARE(rows.at(jRow).actionName, QStringLiteral("focusNowPlaying"));

        QVERIFY(kRow >= 0);
        QCOMPARE(rows.at(kRow).source, BindingRowSource::CustomOverride);
        QCOMPARE(rows.at(kRow).status, BindingRowStatus::Active);
        QCOMPARE(rows.at(kRow).actionName, QStringLiteral("nextMatch"));

        QVERIFY(zRow >= 0);
        QCOMPARE(rows.at(zRow).source, BindingRowSource::Custom);
        QCOMPARE(rows.at(zRow).status, BindingRowStatus::Active);

        QVERIFY(backend.resetBinding(definitions, BindingMode::Normal, QStringLiteral("j")));
        QVERIFY(backend.removeCustomBinding(definitions, BindingMode::Normal, QStringLiteral("z")));

        const auto resetRows = backend.bindingRows(definitions, true);
        bool foundDefaultJ   = false;
        for(const auto& row : resetRows) {
            if(row.mode == BindingMode::Normal && row.keys == QStringLiteral("j")) {
                foundDefaultJ = true;
                QCOMPARE(row.source, BindingRowSource::Default);
                QCOMPARE(row.status, BindingRowStatus::Active);
                QCOMPARE(row.actionName, QStringLiteral("moveCursor"));
                QCOMPARE(row.args, QStringLiteral("+1"));
            }
            QVERIFY(!(row.mode == BindingMode::Normal && row.keys == QStringLiteral("z")));
        }
        QVERIFY(foundDefaultJ);
    }

    void testSettingsDialogStagesAndAppliesBindingEdits()
    {
        Fooyin::SettingsManager settings{m_tempDir.filePath(QStringLiteral("vim_settings_dialog_stage6.ini"))};
        VimMotionsSettings vimSettings{&settings};
        Q_UNUSED(vimSettings);

        settings.fileSet(QStringLiteral("VimMotions/Bindings/Normal/z"), QStringLiteral("focusNowPlaying"));

        VimMotionsBindingBackend backend{&settings};
        VimMotionsSettingsDialog dialog{&settings, &backend};
        auto* tree = dialog.findChild<QTreeView*>(QStringLiteral("effectiveBindingsTree"));
        QVERIFY(tree);
        auto* treeModel = qobject_cast<QStandardItemModel*>(tree->model());
        QVERIFY(treeModel);

        auto* addButton          = dialog.findChild<QPushButton*>(QStringLiteral("addBinding"));
        auto* editButton         = dialog.findChild<QPushButton*>(QStringLiteral("editBinding"));
        auto* removeButton       = dialog.findChild<QPushButton*>(QStringLiteral("removeBinding"));
        auto* unmapButton        = dialog.findChild<QPushButton*>(QStringLiteral("unmapBinding"));
        auto* resetBindingButton = dialog.findChild<QPushButton*>(QStringLiteral("resetBinding"));
        auto* discardButton      = dialog.findChild<QPushButton*>(QStringLiteral("discardBindingChanges"));
        auto* buttons            = dialog.findChild<QDialogButtonBox*>();
        QVERIFY(addButton);
        QVERIFY(editButton);
        QVERIFY(removeButton);
        QVERIFY(unmapButton);
        QVERIFY(resetBindingButton);
        QVERIFY(discardButton);
        QVERIFY(buttons);

        acceptBindingEditor(BindingMode::Normal, QStringLiteral("q"), QStringLiteral("focusNowPlaying"));
        addButton->click();
        QVERIFY(
            hasTreeRow(treeModel, QStringLiteral("Normal"), QStringLiteral("q"), QStringLiteral("focusNowPlaying")));
        QVERIFY(!hasBinding(backend.effectiveBindings().value(BindingMode::Normal), QChar(u'q'),
                            QStringLiteral("focusNowPlaying")));

        selectTreeRow(tree, treeModel, QStringLiteral("Normal"), QStringLiteral("j"));
        QVERIFY(!editButton->isEnabled());
        QVERIFY(unmapButton->isEnabled());
        unmapButton->click();
        int jRow = findTreeRow(treeModel, QStringLiteral("Normal"), QStringLiteral("j"));
        QVERIFY(jRow >= 0);
        QCOMPARE(treeCell(treeModel, jRow, 3), QStringLiteral("Custom override"));
        QCOMPARE(treeCell(treeModel, jRow, 4), QStringLiteral("Unmapped"));
        QVERIFY(!editButton->isEnabled());

        acceptBindingEditor(BindingMode::Normal, QStringLiteral("j"), QStringLiteral("focusNowPlaying"));
        addButton->click();
        jRow = findTreeRow(treeModel, QStringLiteral("Normal"), QStringLiteral("j"));
        QVERIFY(jRow >= 0);
        QCOMPARE(treeCell(treeModel, jRow, 2), QStringLiteral("focusNowPlaying"));
        QCOMPARE(treeCell(treeModel, jRow, 3), QStringLiteral("Custom override"));
        QCOMPARE(treeCell(treeModel, jRow, 4), QStringLiteral("Active"));

        discardButton->click();
        QVERIFY(
            !hasTreeRow(treeModel, QStringLiteral("Normal"), QStringLiteral("q"), QStringLiteral("focusNowPlaying")));
        jRow = findTreeRow(treeModel, QStringLiteral("Normal"), QStringLiteral("j"));
        QVERIFY(jRow >= 0);
        QCOMPARE(treeCell(treeModel, jRow, 3), QStringLiteral("Default"));
        QCOMPARE(treeCell(treeModel, jRow, 4), QStringLiteral("Active"));

        acceptBindingEditor(BindingMode::Normal, QStringLiteral("q"), QStringLiteral("focusNowPlaying"));
        addButton->click();
        selectTreeRow(tree, treeModel, QStringLiteral("Normal"), QStringLiteral("q"));
        QVERIFY(editButton->isEnabled());
        acceptBindingEditor(BindingMode::Normal, QStringLiteral("q"), QStringLiteral("moveCursor"),
                            QStringLiteral("+1"));
        editButton->click();
        QVERIFY(hasTreeRow(treeModel, QStringLiteral("Normal"), QStringLiteral("q"), QStringLiteral("moveCursor:+1")));

        selectTreeRow(tree, treeModel, QStringLiteral("Normal"), QStringLiteral("q"));
        removeButton->click();
        QVERIFY(!hasTreeRow(treeModel, QStringLiteral("Normal"), QStringLiteral("q"), QStringLiteral("moveCursor:+1")));

        acceptBindingEditor(BindingMode::Normal, QStringLiteral("q"), QStringLiteral("focusNowPlaying"));
        addButton->click();
        selectTreeRow(tree, treeModel, QStringLiteral("Normal"), QStringLiteral("j"));
        unmapButton->click();
        selectTreeRow(tree, treeModel, QStringLiteral("Normal"), QStringLiteral("j"));
        resetBindingButton->click();
        jRow = findTreeRow(treeModel, QStringLiteral("Normal"), QStringLiteral("j"));
        QVERIFY(jRow >= 0);
        QCOMPARE(treeCell(treeModel, jRow, 3), QStringLiteral("Default"));
        QCOMPARE(treeCell(treeModel, jRow, 4), QStringLiteral("Active"));

        selectTreeRow(tree, treeModel, QStringLiteral("Normal"), QStringLiteral("j"));
        unmapButton->click();
        auto* applyButton = buttons->button(QDialogButtonBox::Apply);
        QVERIFY(applyButton);
        applyButton->click();

        QVERIFY(hasBinding(backend.effectiveBindings().value(BindingMode::Normal), QChar(u'q'),
                           QStringLiteral("focusNowPlaying")));
        QVERIFY(!hasBinding(backend.effectiveBindings().value(BindingMode::Normal), QChar(u'j'),
                            QStringLiteral("moveCursor")));

        QSettings fileSettings(m_tempDir.filePath(QStringLiteral("vim_settings_dialog_stage6.ini")),
                               QSettings::IniFormat);
        QCOMPARE(fileSettings.value(QStringLiteral("VimMotions/Bindings/Normal/q")).toString(),
                 QStringLiteral("focusNowPlaying"));
        QCOMPARE(fileSettings.value(QStringLiteral("VimMotions/Bindings/Normal/j")).toString(), QString{});
    }
};

QTEST_MAIN(TestConfigFileBindings)
#include "bindingintegrationtest.moc"
