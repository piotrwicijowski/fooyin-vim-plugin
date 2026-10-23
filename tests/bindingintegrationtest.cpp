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
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLineEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>

#include <utils/fypaths.h>
#include <utils/settings/settingsmanager.h>

#include <algorithm>

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

    static const BindingEntry* findBinding(const QList<BindingEntry>& bindings, QChar key, const QString& action)
    {
        for(const auto& binding : bindings) {
            if(binding.actionName != action || binding.keys.size() != 1)
                continue;

            if(binding.keys.front().ch == key)
                return &binding;
        }

        return nullptr;
    }

    static bool hasBinding(const QList<BindingEntry>& bindings, QChar key, const QString& action)
    {
        return findBinding(bindings, key, action) != nullptr;
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
            if(scope == BindingScope::PlaylistPicker) {
                QCOMPARE(modeBox->findData(static_cast<int>(BindingMode::Visual)), -1);
                QVERIFY(modeBox->findData(static_cast<int>(BindingMode::Insert)) >= 0);
                QVERIFY(modeBox->findData(static_cast<int>(BindingMode::Normal)) >= 0);
            }
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
    void testPlaylistPickerLocalDefaultsAndOverrides()
    {
        const auto path = m_tempDir.filePath(QStringLiteral("picker_local.ini"));
        Fooyin::SettingsManager settings{path};
        VimMotionsSettings defaults{&settings};
        VimMotionsBindingBackend backend{&settings, path};
        for(const auto mode : {BindingMode::Insert, BindingMode::Normal}) {
            const auto bindings = scopedBindings(backend.effectiveBindings(), mode, BindingScope::PlaylistPicker);
            QCOMPARE(bindings.size(), mode == BindingMode::Insert ? 7 : 10);
            QVERIFY(std::ranges::none_of(bindings, [](const auto& binding) {
                const auto& key = binding.keys.front();
                return key.modifiers == Qt::ControlModifier && (key.key == Qt::Key_J || key.key == Qt::Key_K);
            }));
            for(const auto& [keys, value] : QList<QPair<QString, QString>>{
                    {QStringLiteral("Ctrl+N"), QStringLiteral("playlistPickerMove:+1")},
                    {QStringLiteral("<Down>"), QStringLiteral("playlistPickerMove:+1")},
                    {QStringLiteral("Ctrl+P"), QStringLiteral("playlistPickerMove:-1")},
                    {QStringLiteral("<Up>"), QStringLiteral("playlistPickerMove:-1")},
                    {QStringLiteral("<CR>"), QStringLiteral("playlistPickerAccept")},
                    {QStringLiteral("<Enter>"), QStringLiteral("playlistPickerAccept")},
                    {QStringLiteral("<Esc>"), mode == BindingMode::Insert ? QStringLiteral("leaveInsertMode")
                                                                          : QStringLiteral("playlistPickerCancel")}}) {
                const auto expected = parseBindingString(keys, value);
                QVERIFY(std::ranges::any_of(bindings, [&expected](const auto& binding) {
                    return binding.keys == expected.keys && binding.actionName == expected.actionName
                        && binding.args == expected.args;
                }));
            }
        }
        auto definitions = backend.bindingDefinitions();
        QVERIFY(backend.addCustomBinding(definitions, BindingScope::PlaylistPicker, BindingMode::Insert,
                                         QStringLiteral("<Esc>"), QStringLiteral("playlistPickerCancel"), {}));
        QVERIFY(backend.unmapBinding(definitions, BindingScope::PlaylistPicker, BindingMode::Insert,
                                     QStringLiteral("Ctrl+N")));
        QVERIFY(backend.addCustomBinding(definitions, BindingScope::PlaylistPicker, BindingMode::Normal,
                                         QStringLiteral("zz"), QStringLiteral("playlistPickerAccept"), {}));
        QVERIFY(backend.saveBindingDefinitions(definitions));
        VimMotionsBindingBackend reloaded{&settings, path};
        auto bindings = scopedBindings(reloaded.effectiveBindings(), BindingMode::Insert, BindingScope::PlaylistPicker);
        QCOMPARE(bindings.size(), 6);
        QVERIFY(std::ranges::any_of(
            bindings, [](const auto& b) { return b.actionName == QStringLiteral("playlistPickerCancel"); }));
        QVERIFY(settings.set<Fooyin::Settings::VimMotions::UseDefaultBindings>(false));
        reloaded.reloadBindings();
        QCOMPARE(scopedBindings(reloaded.effectiveBindings(), BindingMode::Insert, BindingScope::PlaylistPicker).size(),
                 1);
        QCOMPARE(scopedBindings(reloaded.effectiveBindings(), BindingMode::Normal, BindingScope::PlaylistPicker).size(),
                 1);
        definitions = reloaded.bindingDefinitions();
        QVERIFY(reloaded.resetBinding(definitions, BindingScope::PlaylistPicker, BindingMode::Insert,
                                      QStringLiteral("<Esc>")));
        QVERIFY(reloaded.removeCustomBinding(definitions, BindingScope::PlaylistPicker, BindingMode::Normal,
                                             QStringLiteral("zz")));
        QVERIFY(reloaded.saveBindingDefinitions(definitions));
        QVERIFY(
            scopedBindings(reloaded.effectiveBindings(), BindingMode::Insert, BindingScope::PlaylistPicker).isEmpty());
        QVERIFY(
            scopedBindings(reloaded.effectiveBindings(), BindingMode::Normal, BindingScope::PlaylistPicker).isEmpty());
        QVERIFY(settings.set<Fooyin::Settings::VimMotions::UseDefaultBindings>(true));
        reloaded.reloadBindings();
        QCOMPARE(scopedBindings(reloaded.effectiveBindings(), BindingMode::Insert, BindingScope::PlaylistPicker).size(),
                 6);
    }

    void testPlaylistPickerVisualDefinitionsStayDisabled()
    {
        const auto path = m_tempDir.filePath(QStringLiteral("picker_visual.ini"));
        Fooyin::SettingsManager settings{path};
        VimMotionsSettings defaults{&settings};
        VimMotionsBindingBackend backend{&settings, path};
        auto definitions = backend.bindingDefinitions();
        QVERIFY(backend.addCustomBinding(definitions, BindingScope::PlaylistPicker, BindingMode::Visual,
                                         QStringLiteral("x"), QStringLiteral("playlistPickerAccept"), {}));
        QVERIFY(backend.saveBindingDefinitions(definitions));
        VimMotionsBindingBackend reloaded{&settings, path};
        QVERIFY(
            scopedBindings(reloaded.effectiveBindings(), BindingMode::Visual, BindingScope::PlaylistPicker).isEmpty());
        const auto rows = reloaded.bindingRows(reloaded.bindingDefinitions(), true);
        QVERIFY(std::ranges::any_of(rows, [](const auto& row) {
            return row.scope == BindingScope::PlaylistPicker && row.mode == BindingMode::Visual
                && row.keys == QStringLiteral("x") && row.status == BindingRowStatus::Disabled;
        }));
    }

    void testSettingsDialogEditsLocalPickerBindings()
    {
        const auto path = m_tempDir.filePath(QStringLiteral("picker_local_dialog.ini"));
        Fooyin::SettingsManager settings{path};
        VimMotionsSettings defaults{&settings};
        VimMotionsBindingBackend backend{&settings, path};
        VimMotionsSettingsDialog dialog{&settings, &backend};
        auto* tree    = dialog.findChild<QTreeView*>(QStringLiteral("effectiveBindingsTree"));
        auto* buttons = dialog.findChild<QDialogButtonBox*>();
        QVERIFY(tree);
        QVERIFY(buttons);
        selectTreeRow(tree, tree->model(), QStringLiteral("Playlist Picker"), QStringLiteral("Insert"),
                      QStringLiteral("<Esc>"));
        acceptBindingEditor(BindingScope::PlaylistPicker, BindingMode::Insert, QStringLiteral("<Esc>"),
                            QStringLiteral("playlistPickerCancel"));
        dialog.findChild<QPushButton*>(QStringLiteral("addBinding"))->click();
        QVERIFY(hasTreeRow(tree->model(), QStringLiteral("Playlist Picker"), QStringLiteral("Insert"),
                           QStringLiteral("<Esc>"), QStringLiteral("playlistPickerCancel")));
        dialog.findChild<QPushButton*>(QStringLiteral("unmapBinding"))->click();
        QVERIFY(hasTreeRow(tree->model(), QStringLiteral("Playlist Picker"), QStringLiteral("Insert"),
                           QStringLiteral("<Esc>"), QStringLiteral("Unmapped")));
        dialog.findChild<QPushButton*>(QStringLiteral("resetBinding"))->click();
        QVERIFY(hasTreeRow(tree->model(), QStringLiteral("Playlist Picker"), QStringLiteral("Insert"),
                           QStringLiteral("<Esc>"), QStringLiteral("leaveInsertMode")));
        acceptBindingEditor(BindingScope::PlaylistPicker, BindingMode::Normal, QStringLiteral("zz"),
                            QStringLiteral("playlistPickerAccept"));
        dialog.findChild<QPushButton*>(QStringLiteral("addBinding"))->click();
        buttons->button(QDialogButtonBox::Apply)->click();
        VimMotionsBindingBackend reloaded{&settings, path};
        QCOMPARE(scopedBindings(reloaded.effectiveBindings(), BindingMode::Normal, BindingScope::PlaylistPicker).size(),
                 11);
        selectTreeRow(tree, tree->model(), QStringLiteral("Playlist Picker"), QStringLiteral("Normal"),
                      QStringLiteral("zz"));
        acceptBindingEditor(BindingScope::PlaylistPicker, BindingMode::Normal, QStringLiteral("zx"),
                            QStringLiteral("playlistPickerCancel"));
        dialog.findChild<QPushButton*>(QStringLiteral("editBinding"))->click();
        QVERIFY(hasTreeRow(tree->model(), QStringLiteral("Playlist Picker"), QStringLiteral("Normal"),
                           QStringLiteral("zx"), QStringLiteral("playlistPickerCancel")));
        selectTreeRow(tree, tree->model(), QStringLiteral("Playlist Picker"), QStringLiteral("Normal"),
                      QStringLiteral("zx"));
        dialog.findChild<QPushButton*>(QStringLiteral("removeBinding"))->click();
        buttons->button(QDialogButtonBox::Apply)->click();
        reloaded.reloadBindings();
        QCOMPARE(scopedBindings(reloaded.effectiveBindings(), BindingMode::Normal, BindingScope::PlaylistPicker).size(),
                 10);
    }

    void testSettingsDialogEditsPlaylistPickerBinding()
    {
        const auto path = m_tempDir.filePath(QStringLiteral("picker_dialog.ini"));
        Fooyin::SettingsManager settings{path};
        VimMotionsSettings defaults{&settings};
        VimMotionsBindingBackend backend{&settings, path};
        VimMotionsSettingsDialog dialog{&settings, &backend};
        auto* tree = dialog.findChild<QTreeView*>(QStringLiteral("effectiveBindingsTree"));
        QVERIFY(tree);
        QVERIFY(hasTreeRow(tree->model(), QStringLiteral("Global"), QStringLiteral("Normal"),
                           QStringLiteral("<Space>p"), QStringLiteral("findPlaylist")));
        auto* add     = dialog.findChild<QPushButton*>(QStringLiteral("addBinding"));
        auto* edit    = dialog.findChild<QPushButton*>(QStringLiteral("editBinding"));
        auto* buttons = dialog.findChild<QDialogButtonBox*>();
        QVERIFY(add);
        QVERIFY(edit);
        QVERIFY(buttons);
        acceptBindingEditor(BindingScope::Global, BindingMode::Normal, QStringLiteral("Ctrl+P"),
                            QStringLiteral("findPlaylist"));
        add->click();
        selectTreeRow(tree, tree->model(), QStringLiteral("Global"), QStringLiteral("Normal"),
                      QStringLiteral("Ctrl+P"));
        acceptBindingEditor(BindingScope::Global, BindingMode::Normal, QStringLiteral("<Space>z"),
                            QStringLiteral("findPlaylist"));
        edit->click();
        QVERIFY(hasTreeRow(tree->model(), QStringLiteral("Global"), QStringLiteral("Normal"),
                           QStringLiteral("<Space>z"), QStringLiteral("findPlaylist")));
        buttons->button(QDialogButtonBox::Apply)->click();
        VimMotionsBindingBackend reloaded{&settings, path};
        const auto bindings = scopedBindings(reloaded.effectiveBindings(), BindingMode::Normal, BindingScope::Global);
        QVERIFY(std::ranges::any_of(bindings, [](const auto& binding) {
            return binding.actionName == QStringLiteral("findPlaylist") && binding.keys.size() == 2
                && binding.keys.back().ch == u'z';
        }));
    }

    void testPlaylistPickerDefaultsAndCustomBindings()
    {
        const auto path = m_tempDir.filePath(QStringLiteral("picker.ini"));
        Fooyin::SettingsManager settings{path};
        VimMotionsSettings defaults{&settings};
        VimMotionsBindingBackend backend{&settings, path};
        const auto hasPicker = [](const QList<BindingEntry>& bindings) {
            return std::ranges::any_of(bindings, [](const auto& binding) {
                return binding.actionName == QStringLiteral("findPlaylist") && binding.keys.size() == 2
                    && binding.keys.front().key == Qt::Key_Space && binding.keys.back().ch == u'p';
            });
        };
        for(const auto mode : {BindingMode::Normal, BindingMode::Visual})
            QVERIFY(hasPicker(scopedBindings(backend.effectiveBindings(), mode, BindingScope::Global)));

        auto definitions = backend.bindingDefinitions();
        QVERIFY(
            backend.unmapBinding(definitions, BindingScope::Global, BindingMode::Normal, QStringLiteral("<Space>p")));
        QVERIFY(backend.updateCustomBinding(definitions, BindingScope::Global, BindingMode::Visual,
                                            QStringLiteral("<Space>p"), BindingScope::Global, BindingMode::Visual,
                                            QStringLiteral("<Space>p"), QStringLiteral("enterSearchAndExit"), {}));
        QVERIFY(backend.addCustomBinding(definitions, BindingScope::Global, BindingMode::Normal,
                                         QStringLiteral("Ctrl+P"), QStringLiteral("findPlaylist"), {}));
        QVERIFY(backend.saveBindingDefinitions(definitions));
        VimMotionsBindingBackend reloaded{&settings, path};
        QVERIFY(!hasPicker(scopedBindings(reloaded.effectiveBindings(), BindingMode::Normal, BindingScope::Global)));
        QVERIFY(!hasPicker(scopedBindings(reloaded.effectiveBindings(), BindingMode::Visual, BindingScope::Global)));
        QVERIFY(hasBinding(scopedBindings(reloaded.effectiveBindings(), BindingMode::Normal, BindingScope::Global),
                           u'P', QStringLiteral("findPlaylist")));
        QVERIFY(settings.set<Fooyin::Settings::VimMotions::UseDefaultBindings>(false));
        reloaded.reloadBindings();
        QVERIFY(hasBinding(scopedBindings(reloaded.effectiveBindings(), BindingMode::Normal, BindingScope::Global),
                           u'P', QStringLiteral("findPlaylist")));
        QVERIFY(!hasPicker(scopedBindings(reloaded.effectiveBindings(), BindingMode::Visual, BindingScope::Global)));
    }

    void testJumpDefaultsAndOverrides()
    {
        Fooyin::SettingsManager settings{m_tempDir.filePath(QStringLiteral("jump_bindings.ini"))};
        VimMotionsSettings defaults{&settings};
        VimMotionsBindingBackend backend{&settings, m_tempDir.filePath(QStringLiteral("jump_bindings.ini"))};
        for(const auto mode : {BindingMode::Normal, BindingMode::Visual}) {
            const auto bindings = scopedBindings(backend.effectiveBindings(), mode, BindingScope::Global);
            const auto* older   = findBinding(bindings, u'O', QStringLiteral("jumpOlder"));
            const auto* newer   = findBinding(bindings, u'I', QStringLiteral("jumpNewer"));
            QVERIFY(older);
            QVERIFY(newer);
            QCOMPARE(older->keys.front().modifiers, Qt::ControlModifier);
            QCOMPARE(newer->keys.front().modifiers, Qt::ControlModifier);
        }
        const auto normal = scopedBindings(backend.effectiveBindings(), BindingMode::Normal, BindingScope::Global);
        QVERIFY(std::ranges::any_of(normal, [](const auto& binding) {
            return binding.actionName == QStringLiteral("enterFilter") && binding.keys.size() == 2
                && binding.keys.front().ch == u'g' && binding.keys.back().ch == u'i';
        }));

        auto definitions = backend.bindingDefinitions();
        QVERIFY(backend.addCustomBinding(definitions, BindingScope::Global, BindingMode::Normal,
                                         QStringLiteral("Ctrl+I"), QStringLiteral("enterFilter"), {}));
        QVERIFY(backend.addCustomBinding(definitions, BindingScope::Global, BindingMode::Normal, QStringLiteral("zx"),
                                         QStringLiteral("jumpNewer"), {}));
        QVERIFY(backend.saveBindingDefinitions(definitions));
        VimMotionsBindingBackend reloaded{&settings, m_tempDir.filePath(QStringLiteral("jump_bindings.ini"))};
        const auto overridden = scopedBindings(reloaded.effectiveBindings(), BindingMode::Normal, BindingScope::Global);
        QVERIFY(hasBinding(overridden, u'I', QStringLiteral("enterFilter")));
        QVERIFY(!hasBinding(overridden, u'I', QStringLiteral("jumpNewer")));
        QVERIFY(std::ranges::any_of(overridden, [](const auto& binding) {
            return binding.actionName == QStringLiteral("jumpNewer") && binding.keys.size() == 2;
        }));
    }

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

    void testConfigurationPath_data()
    {
        QTest::addColumn<bool>("portable");
        QTest::newRow("installed") << false;
        QTest::newRow("portable") << true;
    }

    void testConfigurationPath()
    {
        QFETCH(bool, portable);
        if(!qEnvironmentVariableIsSet("VIM_TEST_CONFIG_PATH_CHILD")) {
            // Portable detection uses the executable directory. Run a private copy so other tests are unaffected.
            QTemporaryDir sandbox;
            QVERIFY(sandbox.isValid());
            const QString binDir = sandbox.filePath(QStringLiteral("bin"));
            QVERIFY(QDir{}.mkpath(binDir));
            const QString executable
                = QDir{binDir}.filePath(QFileInfo{QCoreApplication::applicationFilePath()}.fileName());
            QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), executable));
            if(portable) {
                QFile marker{QDir{binDir}.filePath(QStringLiteral("PORTABLE"))};
                QVERIFY(marker.open(QIODevice::WriteOnly));
            }

            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert(QStringLiteral("VIM_TEST_CONFIG_PATH_CHILD"), QStringLiteral("1"));
            environment.insert(QStringLiteral("HOME"), sandbox.path());
            environment.insert(QStringLiteral("XDG_CONFIG_HOME"), sandbox.filePath(QStringLiteral("user-config")));
            environment.insert(QStringLiteral("APPDATA"), sandbox.filePath(QStringLiteral("roaming")));
            environment.insert(QStringLiteral("LOCALAPPDATA"), sandbox.filePath(QStringLiteral("local")));
            // Preserve DLL discovery when running the copied executable on Windows.
            environment.insert(QStringLiteral("PATH"), QCoreApplication::applicationDirPath() + QDir::listSeparator()
                                                           + environment.value(QStringLiteral("PATH")));
            QProcess child;
            child.setProcessEnvironment(environment);
            child.setProcessChannelMode(QProcess::MergedChannels);
            child.start(executable, {QStringLiteral("testConfigurationPath:")
                                     + (portable ? QStringLiteral("portable") : QStringLiteral("installed"))});
            QVERIFY2(child.waitForStarted(), qPrintable(child.errorString()));
            if(!child.waitForFinished(30000)) {
                child.kill();
                child.waitForFinished();
                QFAIL("Configuration-path subprocess timed out");
            }
            const QByteArray output = child.readAll();
            QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0, output.constData());
            return;
        }

        QCOMPARE(Fooyin::Utils::isPortable(), portable);
        const QString hostPath = QDir{Fooyin::Utils::configPath()}.filePath(QStringLiteral("fooyin.conf"));
        const QString installedPath
            = QDir{QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)}.filePath(
                QStringLiteral("fooyin.conf"));
        if(portable) {
            QCOMPARE(hostPath,
                     QDir{QCoreApplication::applicationDirPath()}.filePath(QStringLiteral("config/fooyin.conf")));
            QVERIFY(hostPath != installedPath);
            QSettings installed{installedPath, QSettings::IniFormat};
            installed.setValue(QStringLiteral("VimMotions/Bindings/Global/Normal/z"), QStringLiteral("enterVisual"));
            installed.setValue(QStringLiteral("VimMotions/Bindings/Global/Normal/q"), QStringLiteral("enterVisual"));
            installed.sync();
            QCOMPARE(installed.status(), QSettings::NoError);
        }
        else {
            QCOMPARE(hostPath, installedPath);
        }

        Fooyin::SettingsManager settings{hostPath};
        VimMotionsSettings vimSettings{&settings};
        settings.fileSet(QStringLiteral("VimMotions/Bindings/Global/Normal/z"), QStringLiteral("focusNowPlaying"));
        settings.fileSet(QStringLiteral("VimMotions/Bindings/Global/Normal/x"), QStringLiteral("enterVisual"));
        settings.fileSet(QStringLiteral("Unrelated/Setting"), 42);
        // A child QSettings is not a supported way to discover the host's configuration file.
        QSettings decoy{m_tempDir.filePath(QStringLiteral("decoy.ini")), QSettings::IniFormat, &settings};
        decoy.setValue(QStringLiteral("VimMotions/Bindings/Global/Normal/z"), QStringLiteral("enterVisual"));
        VimMotionsBindingBackend backend{&settings};
        const auto bindings = scopedBindings(backend.effectiveBindings(), BindingMode::Normal, BindingScope::Global);
        QVERIFY(hasBinding(bindings, u'z', QStringLiteral("focusNowPlaying")));
        QVERIFY(!hasBinding(bindings, u'q', QStringLiteral("enterVisual")));

        auto definitions = backend.bindingDefinitions();
        QVERIFY(backend.addCustomBinding(definitions, BindingScope::Global, BindingMode::Normal, QStringLiteral("w"),
                                         QStringLiteral("jumpToFirst"), {}));
        QVERIFY(backend.saveBindingDefinitions(definitions));
        QCOMPARE(settings.fileValue(QStringLiteral("VimMotions/Bindings/Global/Normal/w")).toString(),
                 QStringLiteral("jumpToFirst"));
        QCOMPARE(settings.fileValue(QStringLiteral("Unrelated/Setting")).toInt(), 42);
        VimMotionsBindingBackend reloaded{&settings};
        QVERIFY(hasBinding(scopedBindings(reloaded.effectiveBindings(), BindingMode::Normal, BindingScope::Global),
                           u'w', QStringLiteral("jumpToFirst")));
        if(portable) {
            QSettings installed{installedPath, QSettings::IniFormat};
            QCOMPARE(installed.value(QStringLiteral("VimMotions/Bindings/Global/Normal/z")).toString(),
                     QStringLiteral("enterVisual"));
            QCOMPARE(installed.value(QStringLiteral("VimMotions/Bindings/Global/Normal/q")).toString(),
                     QStringLiteral("enterVisual"));
            QVERIFY(!installed.contains(QStringLiteral("VimMotions/Bindings/Global/Normal/w")));
        }

        const QString explicitPath = m_tempDir.filePath(QStringLiteral("explicit.ini"));
        Fooyin::SettingsManager explicitSettings{explicitPath};
        VimMotionsSettings explicitVimSettings{&explicitSettings};
        explicitSettings.fileSet(QStringLiteral("VimMotions/Bindings/Global/Normal/z"), QStringLiteral("enterVisual"));
        VimMotionsBindingBackend explicitBackend{&explicitSettings, explicitPath};
        const auto explicitBindings
            = scopedBindings(explicitBackend.effectiveBindings(), BindingMode::Normal, BindingScope::Global);
        QVERIFY(hasBinding(explicitBindings, u'z', QStringLiteral("enterVisual")));
        QVERIFY(!hasBinding(explicitBindings, u'x', QStringLiteral("enterVisual")));
        auto explicitDefinitions = explicitBackend.bindingDefinitions();
        QVERIFY(explicitBackend.addCustomBinding(explicitDefinitions, BindingScope::Global, BindingMode::Normal,
                                                 QStringLiteral("w"), QStringLiteral("jumpToLast"), {}));
        QVERIFY(explicitBackend.saveBindingDefinitions(explicitDefinitions));
        QCOMPARE(explicitSettings.fileValue(QStringLiteral("VimMotions/Bindings/Global/Normal/w")).toString(),
                 QStringLiteral("jumpToLast"));
        QCOMPARE(settings.fileValue(QStringLiteral("VimMotions/Bindings/Global/Normal/w")).toString(),
                 QStringLiteral("jumpToFirst"));
    }

    void testBindingBackendReloadsCustomBindings()
    {
        Fooyin::SettingsManager settings{m_tempDir.filePath(QStringLiteral("vim_settings.ini"))};
        VimMotionsSettings vimSettings{&settings};
        Q_UNUSED(vimSettings);

        settings.fileSet(QStringLiteral("VimMotions/Bindings/Global/Normal/z"), QStringLiteral("focusNowPlaying"));

        VimMotionsBindingBackend backend{&settings, m_tempDir.filePath(QStringLiteral("vim_settings.ini"))};
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

    void testBindingBackendMatchesExplicitModifiers()
    {
        const auto path = m_tempDir.filePath(QStringLiteral("explicit_modifiers.ini"));
        Fooyin::SettingsManager settings{path};
        VimMotionsSettings defaults{&settings};
        QVERIFY(settings.set<Fooyin::Settings::VimMotions::UseDefaultBindings>(false));

        VimMotionsBindingBackend backend{&settings, path};
        auto definitions = backend.bindingDefinitions();
        QVERIFY(backend.addCustomBinding(definitions, BindingScope::Global, BindingMode::Normal,
                                         QStringLiteral("Ctrl+L"), QStringLiteral("jumpToFirst"), {}));
        QVERIFY(backend.addCustomBinding(definitions, BindingScope::Global, BindingMode::Normal,
                                         QStringLiteral("Ctrl+Shift+L"), QStringLiteral("jumpToLast"), {}));
        QVERIFY(backend.saveBindingDefinitions(definitions));

        VimMotionsBindingBackend reloaded{&settings, path};
        const auto bindings = scopedBindings(reloaded.effectiveBindings(), BindingMode::Normal, BindingScope::Global);
        QCOMPARE(bindings.size(), 2);

        const auto matchingActions = [&bindings](Qt::KeyboardModifiers modifiers) {
            QKeyEvent event{QEvent::KeyPress, Qt::Key_L, modifiers};
            QStringList actions;
            for(const auto& binding : bindings) {
                if(binding.keys.front().matches(&event))
                    actions.push_back(binding.actionName);
            }
            return actions;
        };
        QCOMPARE(matchingActions(Qt::ControlModifier), QStringList{QStringLiteral("jumpToFirst")});
        QCOMPARE(matchingActions(Qt::ControlModifier | Qt::ShiftModifier), QStringList{QStringLiteral("jumpToLast")});
        QVERIFY(matchingActions(Qt::ControlModifier | Qt::AltModifier).isEmpty());
        QVERIFY(matchingActions(Qt::ControlModifier | Qt::ShiftModifier | Qt::MetaModifier).isEmpty());
        QVERIFY(matchingActions(Qt::NoModifier).isEmpty());
    }

    void testBindingBackendLoadsSearchLibraryDialogScope()
    {
        Fooyin::SettingsManager settings{m_tempDir.filePath(QStringLiteral("vim_settings_search_library_scope.ini"))};
        VimMotionsSettings vimSettings{&settings};
        Q_UNUSED(vimSettings);

        settings.fileSet(QStringLiteral("VimMotions/Bindings/SearchLibraryDialog/Normal/Ctrl+J"),
                         QStringLiteral("spatialMoveFocus:down"));

        VimMotionsBindingBackend backend{&settings,
                                         m_tempDir.filePath(QStringLiteral("vim_settings_search_library_scope.ini"))};
        const auto bindings
            = scopedBindings(backend.effectiveBindings(), BindingMode::Normal, BindingScope::SearchLibraryDialog);

        QCOMPARE(bindings.size(), 2);
        const auto* binding = findBinding(bindings, QChar(u'J'), QStringLiteral("spatialMoveFocus"));
        QVERIFY(binding);
        QCOMPARE(binding->args, QStringLiteral("down"));
        QCOMPARE(binding->keys.size(), 1);
        QCOMPARE(binding->keys.front().modifiers, Qt::ControlModifier);
        QCOMPARE(binding->keys.front().ch, QChar(u'J'));
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

        VimMotionsBindingBackend backend{&settings, m_tempDir.filePath(QStringLiteral("vim_settings_dialog.ini"))};
        VimMotionsSettingsDialog dialog{&settings, &backend};
        auto* tree = dialog.findChild<QTreeView*>(QStringLiteral("effectiveBindingsTree"));
        QVERIFY(tree);
        const auto* treeModel = tree->model();
        QVERIFY(treeModel);
        QVERIFY(hasTreeRow(treeModel, QStringLiteral("Global"), QStringLiteral("Normal"), QStringLiteral("z"),
                           QStringLiteral("focusNowPlaying")));
        settings.fileSet(QStringLiteral("VimMotions/Bindings/SearchLibraryDialog/Normal/Ctrl+J"),
                         QStringLiteral("spatialMoveFocus:down"));

        backend.reloadBindings();
        const auto searchLibraryBindings
            = scopedBindings(backend.effectiveBindings(), BindingMode::Normal, BindingScope::SearchLibraryDialog);
        QCOMPARE(searchLibraryBindings.size(), 1);
        const auto* searchLibraryBinding
            = findBinding(searchLibraryBindings, QChar(u'J'), QStringLiteral("spatialMoveFocus"));
        QVERIFY(searchLibraryBinding);
        QCOMPARE(searchLibraryBinding->args, QStringLiteral("down"));
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

        VimMotionsBindingBackend backend{&settings,
                                         m_tempDir.filePath(QStringLiteral("vim_settings_backend_editable.ini"))};
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
            if(rows.at(i).scope == BindingScope::Global && rows.at(i).mode == BindingMode::Normal
               && rows.at(i).keys == QStringLiteral("j"))
                jRow = i;
            if(rows.at(i).scope == BindingScope::Global && rows.at(i).mode == BindingMode::Normal
               && rows.at(i).keys == QStringLiteral("k"))
                kRow = i;
            if(rows.at(i).scope == BindingScope::Global && rows.at(i).mode == BindingMode::Normal
               && rows.at(i).keys == QStringLiteral("z"))
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
            if(row.scope == BindingScope::Global && row.mode == BindingMode::Normal
               && row.keys == QStringLiteral("j")) {
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

        VimMotionsBindingBackend backend{&settings,
                                         m_tempDir.filePath(QStringLiteral("vim_settings_dialog_stage6.ini"))};
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
        acceptBindingEditor(BindingScope::SearchLibraryDialog, BindingMode::Normal, QStringLiteral("Ctrl+K"),
                            QStringLiteral("spatialMoveFocus"), QStringLiteral("up"));
        addButton->click();
        QVERIFY(hasTreeRow(treeModel, QStringLiteral("Search Library Dialog"), QStringLiteral("Normal"),
                           QStringLiteral("Ctrl+K"), QStringLiteral("spatialMoveFocus:up")));

        tree->sortByColumn(0, Qt::AscendingOrder);
        QCOMPARE(treeCell(treeModel, 0, 0), QStringLiteral("Global"));
        tree->sortByColumn(0, Qt::DescendingOrder);
        QCOMPARE(treeCell(treeModel, 0, 0), QStringLiteral("Playlist Picker"));

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
        const auto appliedSearchLibraryBindings
            = scopedBindings(backend.effectiveBindings(), BindingMode::Normal, BindingScope::SearchLibraryDialog);
        QCOMPARE(appliedSearchLibraryBindings.size(), 2);
        const auto* appliedSearchLibraryBinding
            = findBinding(appliedSearchLibraryBindings, QChar(u'K'), QStringLiteral("spatialMoveFocus"));
        QVERIFY(appliedSearchLibraryBinding);
        QCOMPARE(appliedSearchLibraryBinding->args, QStringLiteral("up"));
        QVERIFY(!hasBinding(scopedBindings(backend.effectiveBindings(), BindingMode::Normal, BindingScope::Global),
                            QChar(u'j'), QStringLiteral("moveCursor")));

        QSettings fileSettings(m_tempDir.filePath(QStringLiteral("vim_settings_dialog_stage6.ini")),
                               QSettings::IniFormat);
        QCOMPARE(fileSettings.value(QStringLiteral("VimMotions/Bindings/PlaylistOrganiser/Normal/q")).toString(),
                 QStringLiteral("focusNowPlaying"));
        QCOMPARE(fileSettings.value(QStringLiteral("VimMotions/Bindings/SearchLibraryDialog/Normal/Ctrl+K")).toString(),
                 QStringLiteral("spatialMoveFocus:up"));
        QCOMPARE(fileSettings.value(QStringLiteral("VimMotions/Bindings/Global/Normal/j")).toString(), QString{});
    }
};

QTEST_MAIN(TestConfigFileBindings)
#include "bindingintegrationtest.moc"
