#include "vimbindingparser.h"
#include "vimmotionsbindingbackend.h"
#include "vimmotionssettings.h"
#include "vimmotionssettingsdialog.h"

#include <QAbstractButton>
#include <QAbstractItemModel>
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

            result.append(u'[' + key.section(u'/', 1, 2) + QStringLiteral("] ") + key.section(u'/', -1)
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

    static QList<BindingEntry> scopedBindings(const EffectiveBindings& bindings, BindingMode mode, BindingScope scope)
    {
        return bindings.value(mode).value(scope);
    }

    static bool hasTreeRow(const QAbstractItemModel* model, const QString& scope, const QString& mode,
                           const QString& keys, const QString& action)
    {
        if(!model)
            return false;

        for(int row = 0; row < model->rowCount(); ++row) {
            if(model->data(model->index(row, 0)).toString() != scope)
                continue;
            if(model->data(model->index(row, 1)).toString() != mode)
                continue;
            if(model->data(model->index(row, 2)).toString() != keys)
                continue;
            if(model->data(model->index(row, 3)).toString() != action)
                continue;
            return true;
        }

        return false;
    }

    static int findTreeRow(const QAbstractItemModel* model, const QString& scope, const QString& mode,
                           const QString& keys)
    {
        if(!model)
            return -1;

        for(int row = 0; row < model->rowCount(); ++row) {
            if(model->data(model->index(row, 0)).toString() == scope
               && model->data(model->index(row, 1)).toString() == mode
               && model->data(model->index(row, 2)).toString() == keys) {
                return row;
            }
        }

        return -1;
    }

    static QString treeCell(const QAbstractItemModel* model, int row, int column)
    {
        if(!model || row < 0)
            return {};

        return model->data(model->index(row, column)).toString();
    }

    static void selectTreeRow(QTreeView* tree, const QAbstractItemModel* model, const QString& scope,
                              const QString& mode, const QString& keys)
    {
        QVERIFY(tree);
        const int row = findTreeRow(model, scope, mode, keys);
        QVERIFY(row >= 0);
        tree->setCurrentIndex(model->index(row, 0));
    }

    static void acceptBindingEditor(BindingScope scope, BindingMode mode, const QString& keys,
                                    const QString& actionName, const QString& args = {})
    {
        QTimer::singleShot(0, [scope, mode, keys, actionName, args]() {
            auto* dialog = qobject_cast<QDialog*>(qApp->activeModalWidget());
            QVERIFY(dialog);

            auto* scopeBox   = dialog->findChild<QComboBox*>(QStringLiteral("bindingScope"));
            auto* modeBox    = dialog->findChild<QComboBox*>(QStringLiteral("bindingMode"));
            auto* keysEdit   = dialog->findChild<QLineEdit*>(QStringLiteral("bindingKeys"));
            auto* actionEdit = dialog->findChild<QLineEdit*>(QStringLiteral("bindingActionName"));
            auto* argsEdit   = dialog->findChild<QLineEdit*>(QStringLiteral("bindingArgs"));
            auto* buttons    = dialog->findChild<QDialogButtonBox*>(QStringLiteral("bindingEditButtons"));

            QVERIFY(scopeBox);
            QVERIFY(modeBox);
            QVERIFY(keysEdit);
            QVERIFY(actionEdit);
            QVERIFY(argsEdit);
            QVERIFY(buttons);

            scopeBox->setCurrentIndex(scopeBox->findData(static_cast<int>(scope)));
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
                   "Bindings\\Global\\Normal\\j=moveCursor:+1\n"
                   "Bindings\\Global\\Normal\\k=moveCursor:-1\n"
                   "Bindings\\Global\\Normal\\m=beginSetMark\n"
                   "Bindings\\Global\\Normal\\'=beginJumpToMark\n"
                   "Bindings\\Global\\Normal\\`=beginJumpToMark\n"
                   "Bindings\\Global\\Normal\\a=organiserCreatePlaylist\n"
                   "Bindings\\Global\\Normal\\A=organiserCreateGroup\n"
                   "Bindings\\Global\\Normal\\dd=\n"
                   "Bindings\\Global\\Normal\\d=deleteRows\n"
                   "Bindings\\Global\\Normal\\f=deleteRows\n"
                   "Bindings\\Global\\Normal\\Ctrl+A=selectAll\n"
                   "Bindings\\Global\\Normal\\<Space>=fooyinAction:Playback.Next\n"
                   "Bindings\\Global\\Normal\\g<Space>=focusNowPlaying\n"
                   "Bindings\\Global\\Normal\\yc=copyAfterCurrentPlaying\n"
                   "Bindings\\Global\\Normal\\dc=moveAfterCurrentPlaying\n"
                   "Bindings\\Global\\Visual\\j=extendCursor:+1\n"
                   "Bindings\\Global\\Visual\\v=leaveVisualMode\n"
                   "Bindings\\Global\\Visual\\Ctrl+J=spatialMoveFocus:down\n");
        file.close();
    }

    void testAllBindings()
    {
        auto bindings = loadParsedBindings({}, false);
        QCOMPARE(bindings.size(), 17);
        QVERIFY(bindings.contains(QStringLiteral("[Global/Normal] j = moveCursor")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Normal] k = moveCursor")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Normal] m = beginSetMark")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Normal] ' = beginJumpToMark")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Normal] ` = beginJumpToMark")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Normal] a = organiserCreatePlaylist")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Normal] A = organiserCreateGroup")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Normal] d = deleteRows")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Normal] f = deleteRows")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Normal] Ctrl+A = selectAll")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Normal] <Space> = fooyinAction")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Normal] g<Space> = focusNowPlaying")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Normal] yc = copyAfterCurrentPlaying")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Normal] dc = moveAfterCurrentPlaying")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Visual] j = extendCursor")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Visual] v = leaveVisualMode")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Visual] Ctrl+J = spatialMoveFocus")));
    }

    void testSkipEmptyValues()
    {
        auto bindings = loadParsedBindings({}, true);
        QCOMPARE(bindings.size(), 17);
    }

    void testSkipDefaults()
    {
        QSet<QString> defaults;
        defaults.insert(QStringLiteral("VimMotions/Bindings/Global/Normal/j"));

        auto bindings = loadParsedBindings(defaults, true);

        QCOMPARE(bindings.size(), 16);
    }

    void testAllSkipped()
    {
        QSet<QString> skipAll;
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Global/Normal/j"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Global/Normal/k"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Global/Normal/m"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Global/Normal/'"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Global/Normal/`"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Global/Normal/a"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Global/Normal/A"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Global/Normal/dd"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Global/Normal/d"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Global/Normal/f"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Global/Normal/Ctrl+A"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Global/Normal/<Space>"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Global/Normal/g<Space>"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Global/Normal/yc"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Global/Normal/dc"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Global/Visual/j"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Global/Visual/v"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Global/Visual/Ctrl+J"));

        auto bindings = loadParsedBindings(skipAll, true);
        QCOMPARE(bindings.size(), 0);
    }

    void testNonDefaultKeysOnly()
    {
        QSet<QString> defaults;
        defaults.insert(QStringLiteral("VimMotions/Bindings/Global/Normal/j"));
        defaults.insert(QStringLiteral("VimMotions/Bindings/Global/Normal/k"));

        auto bindings = loadParsedBindings(defaults, true);

        QCOMPARE(bindings.size(), 15);
        QVERIFY(bindings.contains(QStringLiteral("[Global/Normal] d = deleteRows")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Normal] f = deleteRows")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Normal] Ctrl+A = selectAll")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Normal] <Space> = fooyinAction")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Normal] g<Space> = focusNowPlaying")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Normal] yc = copyAfterCurrentPlaying")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Normal] dc = moveAfterCurrentPlaying")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Normal] m = beginSetMark")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Normal] ' = beginJumpToMark")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Normal] ` = beginJumpToMark")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Normal] a = organiserCreatePlaylist")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Normal] A = organiserCreateGroup")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Visual] v = leaveVisualMode")));
        QVERIFY(bindings.contains(QStringLiteral("[Global/Visual] Ctrl+J = spatialMoveFocus")));
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

    void testSettingsUiFlagDefaultsOn()
    {
        Fooyin::SettingsManager settings{m_tempDir.filePath(QStringLiteral("vim_settings.ini"))};
        VimMotionsSettings vimSettings{&settings};
        Q_UNUSED(vimSettings);

        QCOMPARE(settings.value<Fooyin::Settings::VimMotions::EnableSettingsUi>(), true);
        QCOMPARE(settings.value<Fooyin::Settings::VimMotions::UseVimMotionsInSettings>(), false);
    }

    void testBindingBackendReloadsCustomBindings()
    {
        Fooyin::SettingsManager settings{m_tempDir.filePath(QStringLiteral("vim_settings.ini"))};
        VimMotionsSettings vimSettings{&settings};
        Q_UNUSED(vimSettings);

        settings.fileSet(QStringLiteral("VimMotions/Bindings/Global/Normal/z"), QStringLiteral("focusNowPlaying"));

        VimMotionsBindingBackend backend{&settings};
        const auto initialBindings
            = scopedBindings(backend.effectiveBindings(), BindingMode::Normal, BindingScope::Global);
        QVERIFY(hasBinding(initialBindings, QChar(u'z'), QStringLiteral("focusNowPlaying")));
        QVERIFY(hasBinding(initialBindings, QChar(u'j'), QStringLiteral("moveCursor")));

        QCOMPARE(settings.set<Fooyin::Settings::VimMotions::UseDefaultBindings>(false), true);
        backend.reloadBindings();

        const auto reloadedBindings
            = scopedBindings(backend.effectiveBindings(), BindingMode::Normal, BindingScope::Global);
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
        settings.fileSet(QStringLiteral("VimMotions/Bindings/Global/Normal/z"), QStringLiteral("focusNowPlaying"));

        VimMotionsBindingBackend backend{&settings};
        VimMotionsSettingsDialog dialog{&settings, &backend};
        auto* tree = dialog.findChild<QTreeView*>(QStringLiteral("effectiveBindingsTree"));
        QVERIFY(tree);
        const auto* treeModel = tree->model();
        QVERIFY(treeModel);
        QVERIFY(hasTreeRow(treeModel, QStringLiteral("Global"), QStringLiteral("Normal"), QStringLiteral("z"),
                           QStringLiteral("focusNowPlaying")));
        int jRow = findTreeRow(treeModel, QStringLiteral("Global"), QStringLiteral("Normal"), QStringLiteral("j"));
        QVERIFY(jRow >= 0);
        QCOMPARE(treeCell(treeModel, jRow, 4), QStringLiteral("Default"));
        QCOMPARE(treeCell(treeModel, jRow, 5), QStringLiteral("Disabled"));

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
        QVERIFY(hasTreeRow(treeModel, QStringLiteral("Global"), QStringLiteral("Normal"), QStringLiteral("j"),
                           QStringLiteral("moveCursor:+1")));
        QVERIFY(hasTreeRow(treeModel, QStringLiteral("Global"), QStringLiteral("Normal"), QStringLiteral("z"),
                           QStringLiteral("focusNowPlaying")));
        QVERIFY(hasBinding(scopedBindings(backend.effectiveBindings(), BindingMode::Normal, BindingScope::Global),
                           QChar(u'j'), QStringLiteral("moveCursor")));

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
        QVERIFY(hasTreeRow(treeModel, QStringLiteral("Global"), QStringLiteral("Normal"), QStringLiteral("j"),
                           QStringLiteral("moveCursor:+1")));
        QVERIFY(!hasTreeRow(treeModel, QStringLiteral("Global"), QStringLiteral("Normal"), QStringLiteral("z"),
                            QStringLiteral("focusNowPlaying")));
        QVERIFY(hasBinding(scopedBindings(backend.effectiveBindings(), BindingMode::Normal, BindingScope::Global),
                           QChar(u'j'), QStringLiteral("moveCursor")));
        QVERIFY(hasBinding(scopedBindings(backend.effectiveBindings(), BindingMode::Normal, BindingScope::Global),
                           QChar(u'z'), QStringLiteral("focusNowPlaying")));

        applyButton->click();

        QCOMPARE(settings.value<Fooyin::Settings::VimMotions::PendingSequenceTimeout>(), 0);
        QCOMPARE(settings.value<Fooyin::Settings::VimMotions::WrapScan>(), true);
        QCOMPARE(settings.value<Fooyin::Settings::VimMotions::UseVimMotionsInSettings>(), false);
        QCOMPARE(settings.value<Fooyin::Settings::VimMotions::UseDefaultBindings>(), true);
        QCOMPARE(backend.pendingSequenceTimeout(), 0);
        QCOMPARE(backend.wrapScan(), true);
        QCOMPARE(backend.useDefaultBindings(), true);
        QVERIFY(!hasTreeRow(treeModel, QStringLiteral("Global"), QStringLiteral("Normal"), QStringLiteral("z"),
                            QStringLiteral("focusNowPlaying")));
        QVERIFY(!hasBinding(scopedBindings(backend.effectiveBindings(), BindingMode::Normal, BindingScope::Global),
                            QChar(u'z'), QStringLiteral("focusNowPlaying")));

        useDefaultBindings->setChecked(false);
        applyButton->click();

        jRow = findTreeRow(treeModel, QStringLiteral("Global"), QStringLiteral("Normal"), QStringLiteral("j"));
        QVERIFY(jRow >= 0);
        QCOMPARE(treeCell(treeModel, jRow, 4), QStringLiteral("Default"));
        QCOMPARE(treeCell(treeModel, jRow, 5), QStringLiteral("Disabled"));
    }

    void testBindingBackendEditableOperations()
    {
        Fooyin::SettingsManager settings{m_tempDir.filePath(QStringLiteral("vim_settings_backend_editable.ini"))};
        VimMotionsSettings vimSettings{&settings};
        Q_UNUSED(vimSettings);

        VimMotionsBindingBackend backend{&settings};
        auto definitions = backend.bindingDefinitions();

        QVERIFY(backend.addCustomBinding(definitions, BindingScope::Global, BindingMode::Normal, QStringLiteral("z"),
                                         QStringLiteral("focusNowPlaying"), {}));
        QVERIFY(backend.updateCustomBinding(definitions, BindingScope::Global, BindingMode::Normal, QStringLiteral("j"),
                                            BindingScope::Global, BindingMode::Normal, QStringLiteral("j"),
                                            QStringLiteral("focusNowPlaying"), {}));
        QVERIFY(backend.unmapBinding(definitions, BindingScope::Global, BindingMode::Normal, QStringLiteral("k")));
        QVERIFY(backend.addCustomBinding(definitions, BindingScope::Global, BindingMode::Normal, QStringLiteral("k"),
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

        QVERIFY(backend.resetBinding(definitions, BindingScope::Global, BindingMode::Normal, QStringLiteral("j")));
        QVERIFY(
            backend.removeCustomBinding(definitions, BindingScope::Global, BindingMode::Normal, QStringLiteral("z")));

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

        settings.fileSet(QStringLiteral("VimMotions/Bindings/Global/Normal/z"), QStringLiteral("focusNowPlaying"));

        VimMotionsBindingBackend backend{&settings};
        VimMotionsSettingsDialog dialog{&settings, &backend};
        auto* tree = dialog.findChild<QTreeView*>(QStringLiteral("effectiveBindingsTree"));
        QVERIFY(tree);
        const auto* treeModel = tree->model();
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

        acceptBindingEditor(BindingScope::Global, BindingMode::Normal, QStringLiteral("q"),
                            QStringLiteral("focusNowPlaying"));
        addButton->click();
        QVERIFY(hasTreeRow(treeModel, QStringLiteral("Global"), QStringLiteral("Normal"), QStringLiteral("q"),
                           QStringLiteral("focusNowPlaying")));
        QVERIFY(!hasBinding(scopedBindings(backend.effectiveBindings(), BindingMode::Normal, BindingScope::Global),
                            QChar(u'q'), QStringLiteral("focusNowPlaying")));

        selectTreeRow(tree, treeModel, QStringLiteral("Global"), QStringLiteral("Normal"), QStringLiteral("j"));
        QVERIFY(!editButton->isEnabled());
        QVERIFY(unmapButton->isEnabled());
        unmapButton->click();
        int jRow = findTreeRow(treeModel, QStringLiteral("Global"), QStringLiteral("Normal"), QStringLiteral("j"));
        QVERIFY(jRow >= 0);
        QCOMPARE(treeCell(treeModel, jRow, 4), QStringLiteral("Custom override"));
        QCOMPARE(treeCell(treeModel, jRow, 5), QStringLiteral("Unmapped"));
        QVERIFY(!editButton->isEnabled());

        acceptBindingEditor(BindingScope::Global, BindingMode::Normal, QStringLiteral("j"),
                            QStringLiteral("focusNowPlaying"));
        addButton->click();
        jRow = findTreeRow(treeModel, QStringLiteral("Global"), QStringLiteral("Normal"), QStringLiteral("j"));
        QVERIFY(jRow >= 0);
        QCOMPARE(treeCell(treeModel, jRow, 3), QStringLiteral("focusNowPlaying"));
        QCOMPARE(treeCell(treeModel, jRow, 4), QStringLiteral("Custom override"));
        QCOMPARE(treeCell(treeModel, jRow, 5), QStringLiteral("Active"));

        discardButton->click();
        QVERIFY(!hasTreeRow(treeModel, QStringLiteral("Global"), QStringLiteral("Normal"), QStringLiteral("q"),
                            QStringLiteral("focusNowPlaying")));
        jRow = findTreeRow(treeModel, QStringLiteral("Global"), QStringLiteral("Normal"), QStringLiteral("j"));
        QVERIFY(jRow >= 0);
        QCOMPARE(treeCell(treeModel, jRow, 4), QStringLiteral("Default"));
        QCOMPARE(treeCell(treeModel, jRow, 5), QStringLiteral("Active"));

        acceptBindingEditor(BindingScope::Global, BindingMode::Normal, QStringLiteral("q"),
                            QStringLiteral("focusNowPlaying"));
        addButton->click();
        selectTreeRow(tree, treeModel, QStringLiteral("Global"), QStringLiteral("Normal"), QStringLiteral("q"));
        QVERIFY(editButton->isEnabled());
        acceptBindingEditor(BindingScope::PlaylistView, BindingMode::Normal, QStringLiteral("qq"),
                            QStringLiteral("moveCursor"), QStringLiteral("+1"));
        tree->doubleClicked(tree->currentIndex());
        QVERIFY(hasTreeRow(treeModel, QStringLiteral("Playlist View"), QStringLiteral("Normal"), QStringLiteral("qq"),
                           QStringLiteral("moveCursor:+1")));

        tree->sortByColumn(0, Qt::AscendingOrder);
        QCOMPARE(treeCell(treeModel, 0, 0), QStringLiteral("Global"));
        tree->sortByColumn(0, Qt::DescendingOrder);
        QCOMPARE(treeCell(treeModel, 0, 0), QStringLiteral("Playlist Organiser"));

        selectTreeRow(tree, treeModel, QStringLiteral("Playlist View"), QStringLiteral("Normal"), QStringLiteral("qq"));
        removeButton->click();
        QVERIFY(!hasTreeRow(treeModel, QStringLiteral("Playlist View"), QStringLiteral("Normal"), QStringLiteral("qq"),
                            QStringLiteral("moveCursor:+1")));

        acceptBindingEditor(BindingScope::PlaylistOrganiser, BindingMode::Normal, QStringLiteral("q"),
                            QStringLiteral("focusNowPlaying"));
        addButton->click();
        QVERIFY(hasTreeRow(treeModel, QStringLiteral("Playlist Organiser"), QStringLiteral("Normal"),
                           QStringLiteral("q"), QStringLiteral("focusNowPlaying")));
        selectTreeRow(tree, treeModel, QStringLiteral("Global"), QStringLiteral("Normal"), QStringLiteral("j"));
        unmapButton->click();
        selectTreeRow(tree, treeModel, QStringLiteral("Global"), QStringLiteral("Normal"), QStringLiteral("j"));
        resetBindingButton->click();
        jRow = findTreeRow(treeModel, QStringLiteral("Global"), QStringLiteral("Normal"), QStringLiteral("j"));
        QVERIFY(jRow >= 0);
        QCOMPARE(treeCell(treeModel, jRow, 4), QStringLiteral("Default"));
        QCOMPARE(treeCell(treeModel, jRow, 5), QStringLiteral("Active"));

        selectTreeRow(tree, treeModel, QStringLiteral("Global"), QStringLiteral("Normal"), QStringLiteral("j"));
        unmapButton->click();
        auto* applyButton = buttons->button(QDialogButtonBox::Apply);
        QVERIFY(applyButton);
        applyButton->click();

        QVERIFY(hasBinding(
            scopedBindings(backend.effectiveBindings(), BindingMode::Normal, BindingScope::PlaylistOrganiser),
            QChar(u'q'), QStringLiteral("focusNowPlaying")));
        QVERIFY(!hasBinding(scopedBindings(backend.effectiveBindings(), BindingMode::Normal, BindingScope::Global),
                            QChar(u'j'), QStringLiteral("moveCursor")));

        QSettings fileSettings(m_tempDir.filePath(QStringLiteral("vim_settings_dialog_stage6.ini")),
                               QSettings::IniFormat);
        QCOMPARE(fileSettings.value(QStringLiteral("VimMotions/Bindings/PlaylistOrganiser/Normal/q")).toString(),
                 QStringLiteral("focusNowPlaying"));
        QCOMPARE(fileSettings.value(QStringLiteral("VimMotions/Bindings/Global/Normal/j")).toString(), QString{});
    }
};

QTEST_MAIN(TestConfigFileBindings)
#include "bindingintegrationtest.moc"
