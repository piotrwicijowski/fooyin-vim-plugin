#include "vimbindingparser.h"

#include <QKeyEvent>
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
    void testModifierMatching_data();
    void testModifierMatching();
    void testCharacterMatching_data();
    void testCharacterMatching();
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
    void testCopyAfterCurrentPlayingAction();
    void testMoveAfterCurrentPlayingAction();
    void testJumpBindings_data();
    void testJumpBindings();
    void testFilterSequence();
    void testPlaylistPicker();
    void testPlaylistPickerMappings_data();
    void testPlaylistPickerMappings();
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

void TestParseBinding::testPlaylistPicker()
{
    const auto binding = parseBindingString(QStringLiteral("<Space>p"), QStringLiteral("findPlaylist"));
    QCOMPARE(binding.actionName, QStringLiteral("findPlaylist"));
    QVERIFY(binding.args.isEmpty());
    QCOMPARE(binding.keys.size(), 2);
    QCOMPARE(binding.keys.front().key, int(Qt::Key_Space));
    QCOMPARE(binding.keys.back().ch, QChar(u'p'));
    const auto custom = parseBindingString(QStringLiteral("Ctrl+P"), QStringLiteral("findPlaylist"));
    QCOMPARE(custom.keys.size(), 1);
    QCOMPARE(custom.keys.front().modifiers, Qt::ControlModifier);
    QCOMPARE(custom.actionName, binding.actionName);
}

void TestParseBinding::testPlaylistPickerMappings_data()
{
    QTest::addColumn<QString>("keys");
    QTest::addColumn<QString>("value");
    QTest::addColumn<int>("qtKey");
    QTest::addColumn<QString>("args");
    QTest::newRow("next") << QStringLiteral("Ctrl+N") << QStringLiteral("playlistPickerMove:+1") << int(Qt::Key_N)
                          << QStringLiteral("+1");
    QTest::newRow("previous") << QStringLiteral("<Up>") << QStringLiteral("playlistPickerMove:-1") << int(Qt::Key_Up)
                              << QStringLiteral("-1");
    QTest::newRow("return") << QStringLiteral("<CR>") << QStringLiteral("playlistPickerAccept") << int(Qt::Key_Return)
                            << QString{};
    QTest::newRow("keypad-enter") << QStringLiteral("<Enter>") << QStringLiteral("playlistPickerAccept")
                                  << int(Qt::Key_Enter) << QString{};
    QTest::newRow("cancel") << QStringLiteral("<Esc>") << QStringLiteral("playlistPickerCancel") << int(Qt::Key_Escape)
                            << QString{};
    QTest::newRow("mode") << QStringLiteral("<Esc>") << QStringLiteral("leaveInsertMode") << int(Qt::Key_Escape)
                          << QString{};
}

void TestParseBinding::testPlaylistPickerMappings()
{
    QFETCH(QString, keys);
    QFETCH(QString, value);
    QFETCH(int, qtKey);
    QFETCH(QString, args);
    const auto binding = parseBindingString(keys, value);
    QCOMPARE(binding.keys.size(), 1);
    QCOMPARE(binding.keys.front().key, qtKey);
    QCOMPARE(binding.actionName, value.section(u':', 0, 0));
    QCOMPARE(binding.args, args);
    const auto sequence = parseBindingString(QStringLiteral("zz"), QStringLiteral("playlistPickerAccept"));
    QCOMPARE(sequence.keys.size(), 2);
    QCOMPARE(sequence.actionName, QStringLiteral("playlistPickerAccept"));
}

void TestParseBinding::testJumpBindings_data()
{
    QTest::addColumn<QString>("keys");
    QTest::addColumn<QString>("action");
    QTest::addColumn<QChar>("letter");
    QTest::newRow("older") << QStringLiteral("Ctrl+O") << QStringLiteral("jumpOlder") << QChar(u'O');
    QTest::newRow("newer") << QStringLiteral("Ctrl+I") << QStringLiteral("jumpNewer") << QChar(u'I');
}

void TestParseBinding::testJumpBindings()
{
    QFETCH(QString, keys);
    QFETCH(QString, action);
    QFETCH(QChar, letter);
    const auto binding = parseBindingString(keys, action);
    QCOMPARE(binding.actionName, action);
    QCOMPARE(binding.keys.size(), 1);
    QCOMPARE(binding.keys.front().ch, letter);
    QCOMPARE(binding.keys.front().modifiers, Qt::ControlModifier);
}

void TestParseBinding::testFilterSequence()
{
    const auto binding = parseBindingString(QStringLiteral("gi"), QStringLiteral("enterFilter"));
    QCOMPARE(binding.actionName, QStringLiteral("enterFilter"));
    QCOMPARE(binding.keys.size(), 2);
    QCOMPARE(binding.keys.front().ch, QChar(u'g'));
    QCOMPARE(binding.keys.back().ch, QChar(u'i'));
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

void TestParseBinding::testModifierMatching_data()
{
    QTest::addColumn<QString>("keys");
    QTest::addColumn<Qt::KeyboardModifiers>("modifiers");
    QTest::addColumn<bool>("expected");

    const auto ctrl  = Qt::KeyboardModifiers{Qt::ControlModifier};
    const auto alt   = Qt::KeyboardModifiers{Qt::AltModifier};
    const auto meta  = Qt::KeyboardModifiers{Qt::MetaModifier};
    const auto shift = Qt::KeyboardModifiers{Qt::ShiftModifier};
    QTest::newRow("ctrl-exact") << QStringLiteral("Ctrl+L") << ctrl << true;
    QTest::newRow("ctrl-extra-shift") << QStringLiteral("Ctrl+L") << (ctrl | shift) << false;
    QTest::newRow("ctrl-extra-alt") << QStringLiteral("Ctrl+L") << (ctrl | alt) << false;
    QTest::newRow("ctrl-extra-meta") << QStringLiteral("Ctrl+L") << (ctrl | meta) << false;
    QTest::newRow("ctrl-missing") << QStringLiteral("Ctrl+L") << Qt::KeyboardModifiers{} << false;
    QTest::newRow("ctrl-keypad-ignored") << QStringLiteral("Ctrl+L") << (ctrl | Qt::KeypadModifier) << true;
    QTest::newRow("ctrl-shift-exact") << QStringLiteral("Ctrl+Shift+L") << (ctrl | shift) << true;
    QTest::newRow("ctrl-shift-missing-shift") << QStringLiteral("Ctrl+Shift+L") << ctrl << false;
    QTest::newRow("ctrl-shift-missing-ctrl") << QStringLiteral("Ctrl+Shift+L") << shift << false;
    QTest::newRow("ctrl-shift-extra-alt") << QStringLiteral("Ctrl+Shift+L") << (ctrl | shift | alt) << false;
    QTest::newRow("ctrl-shift-extra-meta") << QStringLiteral("Ctrl+Shift+L") << (ctrl | shift | meta) << false;
    QTest::newRow("alt-exact") << QStringLiteral("Alt+L") << alt << true;
    QTest::newRow("alt-extra-ctrl") << QStringLiteral("Alt+L") << (alt | ctrl) << false;
    QTest::newRow("meta-exact") << QStringLiteral("Meta+L") << meta << true;
    QTest::newRow("meta-extra-shift") << QStringLiteral("Meta+L") << (meta | shift) << false;
    QTest::newRow("shift-exact") << QStringLiteral("Shift+L") << shift << true;
    QTest::newRow("shift-extra-ctrl") << QStringLiteral("Shift+L") << (shift | ctrl) << false;
}

void TestParseBinding::testModifierMatching()
{
    QFETCH(QString, keys);
    QFETCH(Qt::KeyboardModifiers, modifiers);
    QFETCH(bool, expected);

    const auto binding = parseBindingString(keys, QStringLiteral("jumpToLast"));
    QCOMPARE(binding.keys.size(), 1);
    QKeyEvent event{QEvent::KeyPress, Qt::Key_L, modifiers};
    QCOMPARE(binding.keys.front().matches(&event), expected);
}

void TestParseBinding::testCharacterMatching_data()
{
    QTest::addColumn<QString>("keys");
    QTest::addColumn<int>("key");
    QTest::addColumn<Qt::KeyboardModifiers>("modifiers");
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("expected");

    const auto shift = Qt::KeyboardModifiers{Qt::ShiftModifier};
    QTest::newRow("uppercase") << QStringLiteral("G") << int(Qt::Key_G) << shift << QStringLiteral("G") << true;
    QTest::newRow("uppercase-without-shift")
        << QStringLiteral("G") << int(Qt::Key_G) << Qt::KeyboardModifiers{} << QStringLiteral("G") << true;
    QTest::newRow("lowercase-does-not-match-uppercase")
        << QStringLiteral("g") << int(Qt::Key_G) << shift << QStringLiteral("G") << false;
    QTest::newRow("question-mark") << QStringLiteral("?") << int(Qt::Key_Question) << shift << QStringLiteral("?")
                                   << true;
    QTest::newRow("colon") << QStringLiteral(":") << int(Qt::Key_Colon) << shift << QStringLiteral(":") << true;
    QTest::newRow("uppercase-extra-ctrl")
        << QStringLiteral("G") << int(Qt::Key_G) << (shift | Qt::ControlModifier) << QStringLiteral("G") << false;
    QTest::newRow("modified-symbol-needs-explicit-shift")
        << QStringLiteral("Ctrl+?") << int(Qt::Key_Question) << (shift | Qt::ControlModifier) << QStringLiteral("?")
        << false;
    QTest::newRow("modified-symbol-explicit-shift") << QStringLiteral("Ctrl+Shift+?") << int(Qt::Key_Question)
                                                    << (shift | Qt::ControlModifier) << QStringLiteral("?") << true;
}

void TestParseBinding::testCharacterMatching()
{
    QFETCH(QString, keys);
    QFETCH(int, key);
    QFETCH(Qt::KeyboardModifiers, modifiers);
    QFETCH(QString, text);
    QFETCH(bool, expected);

    const auto binding = parseBindingString(keys, QStringLiteral("jumpToLast"));
    QCOMPARE(binding.keys.size(), 1);
    QKeyEvent event{QEvent::KeyPress, key, modifiers, text};
    QCOMPARE(binding.keys.front().matches(&event), expected);
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

void TestParseBinding::testCopyAfterCurrentPlayingAction()
{
    auto e = parseBindingString(QStringLiteral("yc"), QStringLiteral("copyAfterCurrentPlaying"));
    QCOMPARE(e.actionName, QStringLiteral("copyAfterCurrentPlaying"));
    QCOMPARE(e.args, QStringLiteral(""));
    QCOMPARE(e.keys.size(), 2);
    QCOMPARE(e.keys[0].ch, QChar(u'y'));
    QCOMPARE(e.keys[1].ch, QChar(u'c'));
}

void TestParseBinding::testMoveAfterCurrentPlayingAction()
{
    auto e = parseBindingString(QStringLiteral("dc"), QStringLiteral("moveAfterCurrentPlaying"));
    QCOMPARE(e.actionName, QStringLiteral("moveAfterCurrentPlaying"));
    QCOMPARE(e.args, QStringLiteral(""));
    QCOMPARE(e.keys.size(), 2);
    QCOMPARE(e.keys[0].ch, QChar(u'd'));
    QCOMPARE(e.keys[1].ch, QChar(u'c'));
}

QTEST_MAIN(TestParseBinding)
#include "bindingparsertest.moc"
