#include "vimhandler.h"
#include "vimmotionssettings.h"

#include <utils/settings/settingsmanager.h>

#include <QApplication>
#include <QDir>
#include <QKeyEvent>
#include <QStandardItemModel>
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

QTEST_MAIN(TestVimHandlerTimeout)
#include "vimhandlertimeouttest.moc"
