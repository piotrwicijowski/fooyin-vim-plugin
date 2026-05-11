#include "vimhandler.h"
#include "vimmotionssettings.h"

#include <utils/actions/actionmanager.h>
#include <utils/settings/settingsmanager.h>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QKeyEvent>
#include <QMainWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTest>
#include <QTreeView>

namespace Fooyin {

class PlaylistView : public QTreeView
{
    Q_OBJECT

public:
    explicit PlaylistView(QWidget* parent = nullptr)
        : QTreeView(parent)
    { }
};

} // namespace Fooyin

namespace {

int letterKey(QChar ch)
{
    Q_ASSERT(ch.isLower());
    return Qt::Key_A + (ch.unicode() - u'a');
}

bool dispatchKey(Fooyin::VimMotions::VimHandler& handler, QObject* watched, QChar ch)
{
    QKeyEvent event(QEvent::KeyPress, letterKey(ch), Qt::NoModifier, QString(ch));
    return handler.eventFilter(watched, &event);
}

bool dispatchShortcutOverride(Fooyin::VimMotions::VimHandler& handler, QObject* watched, QChar ch)
{
    QKeyEvent event(QEvent::ShortcutOverride, letterKey(ch), Qt::NoModifier, QString(ch));
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

} // namespace

using namespace Fooyin;
using namespace Fooyin::VimMotions;

class TestVimHandlerTimeout : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void hardcodedTwoKeyTimeoutClearsPendingSequence();
    void hardcodedMarkTimeoutClearsPendingMark();
    void configTwoKeyTimeoutClearsPendingSequence();
    void configMarkTimeoutClearsPendingMark();
    void configBindingTriggersFooyinAction();
    void configNormalAmbiguousPrefixPrefersLongerSequence();
    void configNormalAmbiguousPrefixFallsBackAfterTimeout();
    void configVisualAmbiguousPrefixPrefersLongerSequence();
    void configVisualAmbiguousPrefixFallsBackAfterTimeout();
};

void TestVimHandlerTimeout::hardcodedTwoKeyTimeoutClearsPendingSequence()
{
    SettingsManager settings{QDir::tempPath() + QStringLiteral("/fooyin_vim_timeout_hardcoded_sequence.ini")};
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

void TestVimHandlerTimeout::hardcodedMarkTimeoutClearsPendingMark()
{
    SettingsManager settings{QDir::tempPath() + QStringLiteral("/fooyin_vim_timeout_hardcoded_mark.ini")};
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

void TestVimHandlerTimeout::configTwoKeyTimeoutClearsPendingSequence()
{
    SettingsManager settings{QDir::tempPath() + QStringLiteral("/fooyin_vim_timeout_config_sequence.ini")};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)
    settings.set(QStringLiteral("VimMotions/UseConfigBindings"), true);
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

void TestVimHandlerTimeout::configMarkTimeoutClearsPendingMark()
{
    SettingsManager settings{QDir::tempPath() + QStringLiteral("/fooyin_vim_timeout_config_mark.ini")};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)
    settings.set(QStringLiteral("VimMotions/UseConfigBindings"), true);
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
    settings.set(QStringLiteral("VimMotions/UseConfigBindings"), true);
    settings.set(QStringLiteral("VimMotions/UseDefaultBindings"), false);

    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QVERIFY(QDir{}.mkpath(configDir));
    QSettings fileSettings(configDir + QStringLiteral("/fooyin.conf"), QSettings::IniFormat);
    fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Normal/x"), QStringLiteral("fooyinAction:Test.Action"));
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
    settings.set(QStringLiteral("VimMotions/UseConfigBindings"), true);
    settings.set(QStringLiteral("VimMotions/UseDefaultBindings"), false);
    settings.set(QStringLiteral("VimMotions/PendingSequenceTimeout"), 30);

    writeFooyinConfig([](QSettings& fileSettings) {
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Normal/x"), QStringLiteral("enterVisual"));
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Normal/xc"), QStringLiteral("beginSetMark"));
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Normal/z"), QStringLiteral("enterVisual"));
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Normal/zc"), QStringLiteral("beginJumpToMark"));
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
    settings.set(QStringLiteral("VimMotions/UseConfigBindings"), true);
    settings.set(QStringLiteral("VimMotions/UseDefaultBindings"), false);
    settings.set(QStringLiteral("VimMotions/PendingSequenceTimeout"), 30);

    writeFooyinConfig([](QSettings& fileSettings) {
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Normal/x"), QStringLiteral("enterVisual"));
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Normal/xc"), QStringLiteral("beginSetMark"));
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

void TestVimHandlerTimeout::configVisualAmbiguousPrefixPrefersLongerSequence()
{
    SettingsManager settings{QDir::tempPath() + QStringLiteral("/fooyin_vim_visual_ambiguous_prefix.ini")};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)
    settings.set(QStringLiteral("VimMotions/UseConfigBindings"), true);
    settings.set(QStringLiteral("VimMotions/UseDefaultBindings"), false);
    settings.set(QStringLiteral("VimMotions/PendingSequenceTimeout"), 30);

    writeFooyinConfig([](QSettings& fileSettings) {
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Visual/x"), QStringLiteral("leaveVisualMode"));
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Visual/xc"), QStringLiteral("beginSetMark"));
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Visual/z"), QStringLiteral("leaveVisualMode"));
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Visual/zc"), QStringLiteral("beginJumpToMark"));
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
    settings.set(QStringLiteral("VimMotions/UseConfigBindings"), true);
    settings.set(QStringLiteral("VimMotions/UseDefaultBindings"), false);
    settings.set(QStringLiteral("VimMotions/PendingSequenceTimeout"), 30);

    writeFooyinConfig([](QSettings& fileSettings) {
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Visual/x"), QStringLiteral("leaveVisualMode"));
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Visual/xc"), QStringLiteral("beginSetMark"));
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

QTEST_MAIN(TestVimHandlerTimeout)
#include "vimhandlertimeouttest.moc"
