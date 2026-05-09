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
    void testNamedKey();
    void testEncodedKey();
    void testSlashKey();
    void testSemicolon();
};

void TestParseBinding::testSimpleKey()
{
    auto e = parseBindingString(QStringLiteral("j"), QStringLiteral("moveCursor:+1"));
    QCOMPARE(e.actionName, QStringLiteral("moveCursor"));
    QCOMPARE(e.args, QStringLiteral("+1"));
    QCOMPARE(e.isTwoKey, false);
    QVERIFY(!e.firstKey.ch.isNull());
    QCOMPARE(e.firstKey.ch, QChar(u'j'));
    QCOMPARE(e.firstKey.modifiers, Qt::NoModifier);
}

void TestParseBinding::testKeyWithArgs()
{
    auto e = parseBindingString(QStringLiteral("k"), QStringLiteral("moveCursor:-1"));
    QCOMPARE(e.actionName, QStringLiteral("moveCursor"));
    QCOMPARE(e.args, QStringLiteral("-1"));
    QCOMPARE(e.isTwoKey, false);
}

void TestParseBinding::testTwoKeySequence()
{
    auto e = parseBindingString(QStringLiteral("gg"), QStringLiteral("jumpToFirst"));
    QCOMPARE(e.actionName, QStringLiteral("jumpToFirst"));
    QCOMPARE(e.isTwoKey, true);
    QCOMPARE(e.firstKey.ch, QChar(u'g'));
    QCOMPARE(e.secondKey.ch, QChar(u'g'));
}

void TestParseBinding::testModifierCombo()
{
    auto e = parseBindingString(QStringLiteral("Ctrl+J"), QStringLiteral("spatialMoveFocus:down"));
    QCOMPARE(e.actionName, QStringLiteral("spatialMoveFocus"));
    QCOMPARE(e.args, QStringLiteral("down"));
    QCOMPARE(e.isTwoKey, false);
    QCOMPARE(e.firstKey.modifiers, Qt::ControlModifier);
    QCOMPARE(e.firstKey.ch, QChar(u'J'));
}

void TestParseBinding::testCtrlR()
{
    auto e = parseBindingString(QStringLiteral("Ctrl+R"), QStringLiteral("redo"));
    QCOMPARE(e.actionName, QStringLiteral("redo"));
    QCOMPARE(e.isTwoKey, false);
    QCOMPARE(e.firstKey.modifiers, Qt::ControlModifier);
}

void TestParseBinding::testCtrlShiftJ()
{
    auto e = parseBindingString(QStringLiteral("Ctrl+Shift+J"), QStringLiteral("treeMoveSibling:+1"));
    QCOMPARE(e.actionName, QStringLiteral("treeMoveSibling"));
    QCOMPARE(e.args, QStringLiteral("+1"));
    QCOMPARE(e.firstKey.modifiers, Qt::ControlModifier | Qt::ShiftModifier);
}

void TestParseBinding::testAltJ()
{
    auto e = parseBindingString(QStringLiteral("Alt+J"), QStringLiteral("moveRows:+1"));
    QCOMPARE(e.actionName, QStringLiteral("moveRows"));
    QCOMPARE(e.args, QStringLiteral("+1"));
    QCOMPARE(e.firstKey.modifiers, Qt::AltModifier);
}

void TestParseBinding::testNamedKey()
{
    auto e = parseBindingString(QStringLiteral("Escape"), QStringLiteral("clearPending"));
    QCOMPARE(e.actionName, QStringLiteral("clearPending"));
    QCOMPARE(e.firstKey.key, Qt::Key_Escape);
}

void TestParseBinding::testEncodedKey()
{
    auto e = parseBindingString(QStringLiteral("slash"), QStringLiteral("enterSearch"));
    QCOMPARE(e.actionName, QStringLiteral("enterSearch"));
    QCOMPARE(e.firstKey.key, Qt::Key_Slash);
}

void TestParseBinding::testSlashKey()
{
    auto e = parseBindingString(QStringLiteral("g;"), QStringLiteral("focusNowPlaying"));
    QCOMPARE(e.actionName, QStringLiteral("focusNowPlaying"));
    QCOMPARE(e.isTwoKey, true);
}

void TestParseBinding::testSemicolon()
{
    auto e = parseBindingString(QStringLiteral("semicolon"), QStringLiteral("enterSearch"));
    QCOMPARE(e.firstKey.key, Qt::Key_Semicolon);
}

QTEST_MAIN(TestParseBinding)
#include "bindingparsertest.moc"
