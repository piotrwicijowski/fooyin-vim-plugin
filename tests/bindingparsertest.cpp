#include "vimbindingparser.h"

#include <QTest>
#include <Qt>

using namespace Fooyin::VimMotions;

class TestParseBinding : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testSimpleKey();
    void testKeyWithArgs();
    void testTwoKeySequence();
    void testModifierCombo();
    void testCtrlR();
    void testCtrlShiftJ();
    void testAltJ();
    void testVisualCtrlJ();
    void testVisualV();
    void testNamedKey();
    void testSlashKey();
    void testSemicolon();
    void testApostrophe();
    void testBacktick();
    void testSpaceSequence();
    void testOrganiserAddPlaylistAction();
    void testOrganiserAddGroupAction();
    void testSelectAllAction();
    void testFooyinAction();
};

void TestParseBinding::testSimpleKey()
{
    auto e = parseBindingString(QStringLiteral("j"), QStringLiteral("moveCursor:+1"));
    QCOMPARE(e.actionName, QStringLiteral("moveCursor"));
    QCOMPARE(e.args, QStringLiteral("+1"));
    QCOMPARE(e.keys.size(), 1);
    QVERIFY(!e.keys[0].ch.isNull());
    QCOMPARE(e.keys[0].ch, QChar(u'j'));
    QCOMPARE(e.keys[0].modifiers, Qt::NoModifier);
}

void TestParseBinding::testKeyWithArgs()
{
    auto e = parseBindingString(QStringLiteral("k"), QStringLiteral("moveCursor:-1"));
    QCOMPARE(e.actionName, QStringLiteral("moveCursor"));
    QCOMPARE(e.args, QStringLiteral("-1"));
    QCOMPARE(e.keys.size(), 1);
}

void TestParseBinding::testTwoKeySequence()
{
    auto e = parseBindingString(QStringLiteral("gg"), QStringLiteral("jumpToFirst"));
    QCOMPARE(e.actionName, QStringLiteral("jumpToFirst"));
    QCOMPARE(e.keys.size(), 2);
    QCOMPARE(e.keys[0].ch, QChar(u'g'));
    QCOMPARE(e.keys[1].ch, QChar(u'g'));
}

void TestParseBinding::testModifierCombo()
{
    auto e = parseBindingString(QStringLiteral("Ctrl+J"), QStringLiteral("spatialMoveFocus:down"));
    QCOMPARE(e.actionName, QStringLiteral("spatialMoveFocus"));
    QCOMPARE(e.args, QStringLiteral("down"));
    QCOMPARE(e.keys.size(), 1);
    QCOMPARE(e.keys[0].modifiers, Qt::ControlModifier);
    QCOMPARE(e.keys[0].ch, QChar(u'J'));
}

void TestParseBinding::testCtrlR()
{
    auto e = parseBindingString(QStringLiteral("Ctrl+R"), QStringLiteral("redo"));
    QCOMPARE(e.actionName, QStringLiteral("redo"));
    QCOMPARE(e.keys.size(), 1);
    QCOMPARE(e.keys[0].modifiers, Qt::ControlModifier);
}

void TestParseBinding::testCtrlShiftJ()
{
    auto e = parseBindingString(QStringLiteral("Ctrl+Shift+J"), QStringLiteral("treeMoveSibling:+1"));
    QCOMPARE(e.actionName, QStringLiteral("treeMoveSibling"));
    QCOMPARE(e.args, QStringLiteral("+1"));
    QCOMPARE(e.keys[0].modifiers, Qt::ControlModifier | Qt::ShiftModifier);
}

void TestParseBinding::testAltJ()
{
    auto e = parseBindingString(QStringLiteral("Alt+J"), QStringLiteral("moveRows:+1"));
    QCOMPARE(e.actionName, QStringLiteral("moveRows"));
    QCOMPARE(e.args, QStringLiteral("+1"));
    QCOMPARE(e.keys[0].modifiers, Qt::AltModifier);
}

void TestParseBinding::testVisualCtrlJ()
{
    auto e = parseBindingString(QStringLiteral("Ctrl+J"), QStringLiteral("spatialMoveFocus:down"));
    QCOMPARE(e.actionName, QStringLiteral("spatialMoveFocus"));
    QCOMPARE(e.args, QStringLiteral("down"));
    QCOMPARE(e.keys[0].modifiers, Qt::ControlModifier);
}

void TestParseBinding::testVisualV()
{
    auto e = parseBindingString(QStringLiteral("v"), QStringLiteral("leaveVisualMode"));
    QCOMPARE(e.actionName, QStringLiteral("leaveVisualMode"));
    QCOMPARE(e.args, QStringLiteral(""));
    QCOMPARE(e.keys.size(), 1);
    QCOMPARE(e.keys[0].ch, QChar(u'v'));
    QCOMPARE(e.keys[0].modifiers, Qt::NoModifier);
}

void TestParseBinding::testNamedKey()
{
    auto e = parseBindingString(QStringLiteral("<Esc>"), QStringLiteral("clearPending"));
    QCOMPARE(e.actionName, QStringLiteral("clearPending"));
    QCOMPARE(e.keys.size(), 1);
    QCOMPARE(e.keys[0].key, Qt::Key_Escape);
}

void TestParseBinding::testSlashKey()
{
    auto e = parseBindingString(QStringLiteral("<Slash>"), QStringLiteral("enterSearch"));
    QCOMPARE(e.actionName, QStringLiteral("enterSearch"));
    QCOMPARE(e.keys.size(), 1);
    QCOMPARE(e.keys[0].key, Qt::Key_Slash);
}

void TestParseBinding::testSemicolon()
{
    auto e = parseBindingString(QStringLiteral("g;"), QStringLiteral("focusNowPlaying"));
    QCOMPARE(e.keys.size(), 2);
    QCOMPARE(e.keys[1].key, Qt::Key_Semicolon);
}

void TestParseBinding::testApostrophe()
{
    auto e = parseBindingString(QStringLiteral("'"), QStringLiteral("beginJumpToMark"));
    QCOMPARE(e.actionName, QStringLiteral("beginJumpToMark"));
    QCOMPARE(e.keys[0].key, Qt::Key_Apostrophe);
}

void TestParseBinding::testBacktick()
{
    auto e = parseBindingString(QStringLiteral("`"), QStringLiteral("beginJumpToMark"));
    QCOMPARE(e.actionName, QStringLiteral("beginJumpToMark"));
    QCOMPARE(e.keys[0].key, Qt::Key_QuoteLeft);
}

void TestParseBinding::testSpaceSequence()
{
    auto e = parseBindingString(QStringLiteral("g<Space>"), QStringLiteral("focusNowPlaying"));
    QCOMPARE(e.keys.size(), 2);
    QCOMPARE(e.keys[0].ch, QChar(u'g'));
    QCOMPARE(e.keys[1].key, Qt::Key_Space);
}

void TestParseBinding::testOrganiserAddPlaylistAction()
{
    auto e = parseBindingString(QStringLiteral("a"), QStringLiteral("organiserCreatePlaylist"));
    QCOMPARE(e.actionName, QStringLiteral("organiserCreatePlaylist"));
    QCOMPARE(e.args, QStringLiteral(""));
    QCOMPARE(e.keys.size(), 1);
    QCOMPARE(e.keys[0].ch, QChar(u'a'));
    QCOMPARE(e.keys[0].modifiers, Qt::NoModifier);
}

void TestParseBinding::testOrganiserAddGroupAction()
{
    auto e = parseBindingString(QStringLiteral("A"), QStringLiteral("organiserCreateGroup"));
    QCOMPARE(e.actionName, QStringLiteral("organiserCreateGroup"));
    QCOMPARE(e.args, QStringLiteral(""));
    QCOMPARE(e.keys.size(), 1);
    QCOMPARE(e.keys[0].ch, QChar(u'A'));
    QCOMPARE(e.keys[0].modifiers, Qt::NoModifier);
}

void TestParseBinding::testSelectAllAction()
{
    auto e = parseBindingString(QStringLiteral("Ctrl+A"), QStringLiteral("selectAll"));
    QCOMPARE(e.actionName, QStringLiteral("selectAll"));
    QCOMPARE(e.args, QStringLiteral(""));
    QCOMPARE(e.keys.size(), 1);
    QCOMPARE(e.keys[0].modifiers, Qt::ControlModifier);
    QCOMPARE(e.keys[0].ch, QChar(u'A'));
}

void TestParseBinding::testFooyinAction()
{
    auto e = parseBindingString(QStringLiteral("<Space>"), QStringLiteral("fooyinAction:Playback.Next"));
    QCOMPARE(e.actionName, QStringLiteral("fooyinAction"));
    QCOMPARE(e.args, QStringLiteral("Playback.Next"));
    QCOMPARE(e.keys.size(), 1);
    QCOMPARE(e.keys[0].key, Qt::Key_Space);
}

QTEST_MAIN(TestParseBinding)
#include "bindingparsertest.moc"
