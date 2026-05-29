#include "vimhandler.h"
#include "vimmotionsbindingbackend.h"
#include "vimmotionssettings.h"
#include "vimmotionssettingsdialog.h"

#include <utils/actions/actionmanager.h>
#include <utils/settings/settingsmanager.h>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

namespace Fooyin {

class PlaylistView : public QTreeView
{
    Q_OBJECT

public:
    explicit PlaylistView(QWidget* parent = nullptr)
        : QTreeView(parent)
    { }
};

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr)
        : QDialog(parent)
    { }
};

} // namespace Fooyin

namespace {

int letterKey(QChar ch)
{
    Q_ASSERT(ch.isLetter());
    return Qt::Key_A + (ch.toLower().unicode() - u'a');
}

bool dispatchKey(Fooyin::VimMotions::VimHandler& handler, QObject* watched, QChar ch)
{
    const Qt::KeyboardModifiers modifiers = ch.isUpper() ? Qt::ShiftModifier : Qt::NoModifier;
    QKeyEvent event(QEvent::KeyPress, letterKey(ch), modifiers, QString(ch));
    return handler.eventFilter(watched, &event);
}

bool dispatchShortcutOverride(Fooyin::VimMotions::VimHandler& handler, QObject* watched, QChar ch)
{
    const Qt::KeyboardModifiers modifiers = ch.isUpper() ? Qt::ShiftModifier : Qt::NoModifier;
    QKeyEvent event(QEvent::ShortcutOverride, letterKey(ch), modifiers, QString(ch));
    return handler.eventFilter(watched, &event);
}

void focusView(QTreeView* view)
{
    view->show();
    view->setFocus();
    qApp->processEvents();
}

void writeFooyinConfig(const std::function<void(QSettings&)>& configure)
{
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir{}.mkpath(configDir);

    QSettings fileSettings(configDir + QStringLiteral("/fooyin.conf"), QSettings::IniFormat);
    fileSettings.remove(QStringLiteral("VimMotions"));
    configure(fileSettings);
    fileSettings.sync();
}

void acceptBindingEditor(Fooyin::VimMotions::BindingMode mode, const QString& keys, const QString& actionName,
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

QList<Fooyin::VimMotions::BindingEntry> configBindingsFor(const Fooyin::VimMotions::VimHandler& handler,
                                                          Fooyin::VimMotions::VimHandler::Mode mode,
                                                          Fooyin::VimMotions::BindingScope scope)
{
    return handler.configBindings().value(mode).value(scope);
}

} // namespace

using namespace Fooyin;
using namespace Fooyin::VimMotions;

class TestVimHandlerTimeout : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void twoKeyTimeoutClearsPendingSequence();
    void markTimeoutClearsPendingMark();
    void configBindingTriggersFooyinAction();
    void configNormalAmbiguousPrefixPrefersLongerSequence();
    void configNormalAmbiguousPrefixFallsBackAfterTimeout();
    void scopedSequenceOverridesGlobalSingleKey();
    void defaultScopedSequenceAllowsGlobalSequenceCompletion();
    void defaultScopedSequenceFallsBackToGlobalSingleAfterTimeout();
    void configVisualAmbiguousPrefixPrefersLongerSequence();
    void configVisualAmbiguousPrefixFallsBackAfterTimeout();
    void settingsDialogApplyReloadsHandlerBindings();
    void bindingsAreSkippedInSettingsDialogsByDefault();
    void bindingsAreSkippedForFocusedLineEdit();
};

void TestVimHandlerTimeout::twoKeyTimeoutClearsPendingSequence()
{
    SettingsManager settings{QDir::tempPath() + QStringLiteral("/fooyin_vim_timeout_sequence.ini")};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)
    settings.set(QStringLiteral("VimMotions/PendingSequenceTimeout"), 30);

    VimHandler handler;
    handler.setSettingsManager(&settings);

    PlaylistView view;
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("A")));
    model.appendRow(new QStandardItem(QStringLiteral("B")));
    model.appendRow(new QStandardItem(QStringLiteral("C")));
    view.setModel(&model);
    view.setCurrentIndex(model.index(2, 0));
    focusView(&view);

    QVERIFY(dispatchKey(handler, &view, u'g'));
    QTest::qWait(50);
    QVERIFY(dispatchKey(handler, &view, u'g'));
    QCOMPARE(view.currentIndex().row(), 2);
    QVERIFY(dispatchKey(handler, &view, u'g'));
    QCOMPARE(view.currentIndex().row(), 0);
}

void TestVimHandlerTimeout::markTimeoutClearsPendingMark()
{
    SettingsManager settings{QDir::tempPath() + QStringLiteral("/fooyin_vim_timeout_mark.ini")};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)
    settings.set(QStringLiteral("VimMotions/PendingSequenceTimeout"), 30);

    VimHandler handler;
    handler.setSettingsManager(&settings);

    PlaylistView view;
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("A")));
    view.setModel(&model);
    view.setCurrentIndex(model.index(0, 0));
    focusView(&view);

    QVERIFY(dispatchKey(handler, &view, u'm'));
    QVERIFY(dispatchShortcutOverride(handler, &view, u'z'));
    QTest::qWait(50);
    QVERIFY(!dispatchShortcutOverride(handler, &view, u'z'));
}

void TestVimHandlerTimeout::configBindingTriggersFooyinAction()
{
    SettingsManager settings{QDir::tempPath() + QStringLiteral("/fooyin_vim_fooyin_action.ini")};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)
    settings.set(QStringLiteral("VimMotions/UseDefaultBindings"), false);

    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QVERIFY(QDir{}.mkpath(configDir));
    QSettings fileSettings(configDir + QStringLiteral("/fooyin.conf"), QSettings::IniFormat);
    fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Global/Normal/x"),
                          QStringLiteral("fooyinAction:Test.Action"));
    fileSettings.sync();

    QMainWindow window;
    ActionManager actionManager(&settings);
    actionManager.setMainWindow(&window);

    QAction action(QStringLiteral("Test Action"), &window);
    actionManager.registerAction(&action, Id(QStringLiteral("Test.Action")));
    QSignalSpy triggeredSpy(&action, &QAction::triggered);

    VimHandler handler;
    handler.setSettingsManager(&settings);
    handler.setActionManager(&actionManager);

    PlaylistView view;
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("A")));
    view.setModel(&model);
    view.setCurrentIndex(model.index(0, 0));
    focusView(&view);

    QVERIFY(dispatchKey(handler, &view, u'x'));
    QCOMPARE(triggeredSpy.count(), 1);
}

void TestVimHandlerTimeout::configNormalAmbiguousPrefixPrefersLongerSequence()
{
    SettingsManager settings{QDir::tempPath() + QStringLiteral("/fooyin_vim_normal_ambiguous_prefix.ini")};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)
    settings.set(QStringLiteral("VimMotions/UseDefaultBindings"), false);
    settings.set(QStringLiteral("VimMotions/PendingSequenceTimeout"), 30);

    writeFooyinConfig([](QSettings& fileSettings) {
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Global/Normal/x"), QStringLiteral("enterVisual"));
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Global/Normal/xc"), QStringLiteral("beginSetMark"));
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Global/Normal/z"), QStringLiteral("enterVisual"));
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Global/Normal/zc"),
                              QStringLiteral("beginJumpToMark"));
    });

    VimHandler handler;
    handler.setSettingsManager(&settings);

    PlaylistView view;
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("A")));
    view.setModel(&model);
    view.setCurrentIndex(model.index(0, 0));
    focusView(&view);

    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);
    QVERIFY(dispatchKey(handler, &view, u'x'));
    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);
    QVERIFY(dispatchKey(handler, &view, u'c'));
    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);
    QVERIFY(dispatchShortcutOverride(handler, &view, u'a'));
    QVERIFY(dispatchKey(handler, &view, u'a'));
    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);

    QVERIFY(dispatchKey(handler, &view, u'z'));
    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);
    QVERIFY(dispatchKey(handler, &view, u'c'));
    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);
    QVERIFY(dispatchShortcutOverride(handler, &view, u'a'));
}

void TestVimHandlerTimeout::configNormalAmbiguousPrefixFallsBackAfterTimeout()
{
    SettingsManager settings{QDir::tempPath() + QStringLiteral("/fooyin_vim_normal_ambiguous_fallback.ini")};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)
    settings.set(QStringLiteral("VimMotions/UseDefaultBindings"), false);
    settings.set(QStringLiteral("VimMotions/PendingSequenceTimeout"), 30);

    writeFooyinConfig([](QSettings& fileSettings) {
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Global/Normal/x"), QStringLiteral("enterVisual"));
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Global/Normal/xc"), QStringLiteral("beginSetMark"));
    });

    VimHandler handler;
    handler.setSettingsManager(&settings);

    PlaylistView view;
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("A")));
    view.setModel(&model);
    view.setCurrentIndex(model.index(0, 0));
    focusView(&view);

    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);
    QVERIFY(dispatchKey(handler, &view, u'x'));
    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);
    QTest::qWait(50);
    QCOMPARE(handler.mode(), VimHandler::Mode::Visual);
}

void TestVimHandlerTimeout::scopedSequenceOverridesGlobalSingleKey()
{
    SettingsManager settings{QDir::tempPath() + QStringLiteral("/fooyin_vim_scoped_sequence.ini")};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)
    settings.set(QStringLiteral("VimMotions/UseDefaultBindings"), false);
    settings.set(QStringLiteral("VimMotions/PendingSequenceTimeout"), 30);

    writeFooyinConfig([](QSettings& fileSettings) {
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Global/Normal/g"), QStringLiteral("enterVisual"));
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/PlaylistView/Normal/gg"),
                              QStringLiteral("jumpToFirst"));
    });

    VimHandler handler;
    handler.setSettingsManager(&settings);

    PlaylistView view;
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("A")));
    model.appendRow(new QStandardItem(QStringLiteral("B")));
    model.appendRow(new QStandardItem(QStringLiteral("C")));
    view.setModel(&model);
    view.setCurrentIndex(model.index(2, 0));
    focusView(&view);

    QVERIFY(dispatchKey(handler, &view, u'g'));
    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);
    QVERIFY(dispatchKey(handler, &view, u'g'));
    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);
    QCOMPARE(view.currentIndex().row(), 0);
}

void TestVimHandlerTimeout::defaultScopedSequenceAllowsGlobalSequenceCompletion()
{
    SettingsManager settings{QDir::tempPath() + QStringLiteral("/fooyin_vim_default_scoped_global_sequence.ini")};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)
    settings.set(QStringLiteral("VimMotions/UseDefaultBindings"), true);
    settings.set(QStringLiteral("VimMotions/PendingSequenceTimeout"), 30);

    writeFooyinConfig([](QSettings& fileSettings) {
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Global/Normal/yc"), QStringLiteral("enterVisual"));
    });

    VimHandler handler;
    handler.setSettingsManager(&settings);

    PlaylistView view;
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("A")));
    model.appendRow(new QStandardItem(QStringLiteral("B")));
    view.setModel(&model);
    view.setCurrentIndex(model.index(0, 0));
    focusView(&view);

    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);
    QVERIFY(dispatchKey(handler, &view, u'y'));
    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);
    QVERIFY(dispatchKey(handler, &view, u'c'));
    QCOMPARE(handler.mode(), VimHandler::Mode::Visual);
}

void TestVimHandlerTimeout::defaultScopedSequenceFallsBackToGlobalSingleAfterTimeout()
{
    SettingsManager settings{QDir::tempPath() + QStringLiteral("/fooyin_vim_default_scoped_global_single.ini")};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)
    settings.set(QStringLiteral("VimMotions/UseDefaultBindings"), true);
    settings.set(QStringLiteral("VimMotions/PendingSequenceTimeout"), 30);

    writeFooyinConfig([](QSettings& fileSettings) {
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Global/Normal/y"), QStringLiteral("enterVisual"));
    });

    VimHandler handler;
    handler.setSettingsManager(&settings);

    PlaylistView view;
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("A")));
    model.appendRow(new QStandardItem(QStringLiteral("B")));
    view.setModel(&model);
    view.setCurrentIndex(model.index(0, 0));
    focusView(&view);

    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);
    QVERIFY(dispatchKey(handler, &view, u'y'));
    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);
    QTest::qWait(50);
    QCOMPARE(handler.mode(), VimHandler::Mode::Visual);
}

void TestVimHandlerTimeout::configVisualAmbiguousPrefixPrefersLongerSequence()
{
    SettingsManager settings{QDir::tempPath() + QStringLiteral("/fooyin_vim_visual_ambiguous_prefix.ini")};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)
    settings.set(QStringLiteral("VimMotions/UseDefaultBindings"), false);
    settings.set(QStringLiteral("VimMotions/PendingSequenceTimeout"), 30);

    writeFooyinConfig([](QSettings& fileSettings) {
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Global/Visual/x"), QStringLiteral("leaveVisualMode"));
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Global/Visual/xc"), QStringLiteral("beginSetMark"));
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Global/Visual/z"), QStringLiteral("leaveVisualMode"));
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Global/Visual/zc"),
                              QStringLiteral("beginJumpToMark"));
    });

    VimHandler handler;
    handler.setSettingsManager(&settings);

    PlaylistView view;
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("A")));
    model.appendRow(new QStandardItem(QStringLiteral("B")));
    view.setModel(&model);
    view.setCurrentIndex(model.index(0, 0));
    focusView(&view);

    handler.enterVisual();
    QCOMPARE(handler.mode(), VimHandler::Mode::Visual);
    QVERIFY(dispatchKey(handler, &view, u'x'));
    QCOMPARE(handler.mode(), VimHandler::Mode::Visual);
    QVERIFY(dispatchKey(handler, &view, u'c'));
    QCOMPARE(handler.mode(), VimHandler::Mode::Visual);
    QVERIFY(dispatchShortcutOverride(handler, &view, u'z'));
    QVERIFY(dispatchKey(handler, &view, u'z'));
    QCOMPARE(handler.mode(), VimHandler::Mode::Visual);

    handler.enterVisual();
    QCOMPARE(handler.mode(), VimHandler::Mode::Visual);
    QVERIFY(dispatchKey(handler, &view, u'z'));
    QCOMPARE(handler.mode(), VimHandler::Mode::Visual);
    QVERIFY(dispatchKey(handler, &view, u'c'));
    QCOMPARE(handler.mode(), VimHandler::Mode::Visual);
    QVERIFY(dispatchShortcutOverride(handler, &view, u'z'));
}

void TestVimHandlerTimeout::configVisualAmbiguousPrefixFallsBackAfterTimeout()
{
    SettingsManager settings{QDir::tempPath() + QStringLiteral("/fooyin_vim_visual_ambiguous_fallback.ini")};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)
    settings.set(QStringLiteral("VimMotions/UseDefaultBindings"), false);
    settings.set(QStringLiteral("VimMotions/PendingSequenceTimeout"), 30);

    writeFooyinConfig([](QSettings& fileSettings) {
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Global/Visual/x"), QStringLiteral("leaveVisualMode"));
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Global/Visual/xc"), QStringLiteral("beginSetMark"));
    });

    VimHandler handler;
    handler.setSettingsManager(&settings);

    PlaylistView view;
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("A")));
    view.setModel(&model);
    view.setCurrentIndex(model.index(0, 0));
    focusView(&view);

    handler.enterVisual();
    QCOMPARE(handler.mode(), VimHandler::Mode::Visual);
    QVERIFY(dispatchKey(handler, &view, u'x'));
    QCOMPARE(handler.mode(), VimHandler::Mode::Visual);
    QTest::qWait(50);
    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);
}

void TestVimHandlerTimeout::settingsDialogApplyReloadsHandlerBindings()
{
    const QString settingsPath = QDir::tempPath() + QStringLiteral("/fooyin_vim_dialog_runtime_reload.ini");
    QFile::remove(settingsPath);

    SettingsManager settings{settingsPath};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)

    VimMotionsBindingBackend backend{&settings};
    VimHandler handler;
    handler.setSettingsBackend(&backend);
    handler.setSettingsManager(&settings);

    const auto initialNormalBindings = configBindingsFor(handler, VimHandler::Mode::Normal, BindingScope::Global);
    QVERIFY(std::none_of(initialNormalBindings.cbegin(), initialNormalBindings.cend(), [](const auto& binding) {
        return binding.actionName == QStringLiteral("focusNowPlaying") && binding.keys.size() == 2
            && binding.keys.at(0).ch == QChar(u'z') && binding.keys.at(1).ch == QChar(u'x');
    }));

    VimMotionsSettingsDialog dialog{&settings, &backend};
    auto* addButton = dialog.findChild<QPushButton*>(QStringLiteral("addBinding"));
    auto* buttons   = dialog.findChild<QDialogButtonBox*>();
    QVERIFY(addButton);
    QVERIFY(buttons);

    acceptBindingEditor(BindingMode::Normal, QStringLiteral("zx"), QStringLiteral("focusNowPlaying"));
    addButton->click();

    auto* applyButton = buttons->button(QDialogButtonBox::Apply);
    QVERIFY(applyButton);
    applyButton->click();

    const auto normalBindings = configBindingsFor(handler, VimHandler::Mode::Normal, BindingScope::Global);
    QVERIFY(std::any_of(normalBindings.cbegin(), normalBindings.cend(), [](const auto& binding) {
        return binding.actionName == QStringLiteral("focusNowPlaying") && binding.keys.size() == 2
            && binding.keys.at(0).ch == QChar(u'z') && binding.keys.at(1).ch == QChar(u'x');
    }));
}

void TestVimHandlerTimeout::bindingsAreSkippedInSettingsDialogsByDefault()
{
    SettingsManager settings{QDir::tempPath() + QStringLiteral("/fooyin_vim_skip_settings_dialog.ini")};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)
    VimHandler handler;
    handler.setSettingsManager(&settings);

    PlaylistView regularView;
    QStandardItemModel regularModel;
    regularModel.appendRow(new QStandardItem(QStringLiteral("A")));
    regularModel.appendRow(new QStandardItem(QStringLiteral("B")));
    regularView.setModel(&regularModel);
    regularView.setCurrentIndex(regularModel.index(0, 0));
    focusView(&regularView);

    QVERIFY(dispatchKey(handler, &regularView, u'j'));
    QCOMPARE(regularView.currentIndex().row(), 1);

    Fooyin::SettingsDialog settingsDialog;
    auto* settingsPage   = new QWidget(&settingsDialog);
    auto* settingsView   = new PlaylistView(settingsPage);
    auto* settingsLayout = new QVBoxLayout(settingsPage);
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    settingsLayout->addWidget(settingsView);
    QStandardItemModel settingsModel;
    settingsModel.appendRow(new QStandardItem(QStringLiteral("A")));
    settingsModel.appendRow(new QStandardItem(QStringLiteral("B")));
    settingsView->setModel(&settingsModel);
    settingsView->setCurrentIndex(settingsModel.index(0, 0));
    focusView(settingsView);

    QVERIFY(!dispatchShortcutOverride(handler, settingsView, u'j'));
    QVERIFY(!dispatchKey(handler, settingsView, u'j'));

    settings.set(QStringLiteral("VimMotions/UseVimMotionsInSettings"), true);

    QVERIFY(dispatchKey(handler, settingsView, u'j'));

    auto* dialogPage = new QWidget(&settingsDialog);
    QDialog subDialog(dialogPage);
    PlaylistView subDialogView(&subDialog);
    QVBoxLayout subDialogLayout(&subDialog);
    subDialogLayout.setContentsMargins(0, 0, 0, 0);
    subDialogLayout.addWidget(&subDialogView);
    QStandardItemModel subDialogModel;
    subDialogModel.appendRow(new QStandardItem(QStringLiteral("A")));
    subDialogModel.appendRow(new QStandardItem(QStringLiteral("B")));
    subDialogView.setModel(&subDialogModel);
    subDialogView.setCurrentIndex(subDialogModel.index(0, 0));

    settings.set(QStringLiteral("VimMotions/UseVimMotionsInSettings"), false);
    focusView(&subDialogView);

    QVERIFY(!dispatchShortcutOverride(handler, &subDialogView, u'j'));
    QVERIFY(!dispatchKey(handler, &subDialogView, u'j'));
}

void TestVimHandlerTimeout::bindingsAreSkippedForFocusedLineEdit()
{
    SettingsManager settings{QDir::tempPath() + QStringLiteral("/fooyin_vim_skip_line_edit.ini")};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)

    VimHandler handler;
    handler.setSettingsManager(&settings);

    QDialog dialog;
    QVBoxLayout layout(&dialog);
    auto* searchEdit = new QLineEdit(&dialog);
    auto* view       = new PlaylistView(&dialog);
    layout.addWidget(searchEdit);
    layout.addWidget(view);

    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("A")));
    model.appendRow(new QStandardItem(QStringLiteral("B")));
    view->setModel(&model);
    view->setCurrentIndex(model.index(0, 0));

    dialog.show();
    searchEdit->setFocus();
    qApp->processEvents();

    QVERIFY(searchEdit->hasFocus());
    QVERIFY(!dispatchShortcutOverride(handler, searchEdit, u'j'));
    QVERIFY(!dispatchShortcutOverride(handler, &dialog, u'j'));

    qApp->installEventFilter(&handler);
    searchEdit->clear();
    QTest::keyClicks(searchEdit, QStringLiteral("aj"));
    qApp->processEvents();
    qApp->removeEventFilter(&handler);

    QCOMPARE(searchEdit->text(), QStringLiteral("aj"));

    focusView(view);
    QVERIFY(dispatchShortcutOverride(handler, view, u'j'));
    QVERIFY(dispatchKey(handler, view, u'j'));
    QCOMPARE(view->currentIndex().row(), 1);
}

QTEST_MAIN(TestVimHandlerTimeout)
#include "vimhandlertimeouttest.moc"
