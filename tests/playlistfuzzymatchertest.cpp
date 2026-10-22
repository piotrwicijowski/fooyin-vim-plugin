#include "playlistfuzzymatcher.h"

#include <QTest>

using namespace Fooyin::VimMotions;
using namespace Qt::StringLiterals;

class TestPlaylistFuzzyMatcher : public QObject
{
    Q_OBJECT

private slots:
    void matches_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("query");
        QTest::addColumn<bool>("expected");
        QTest::newRow("empty") << u"Jazz"_s << QString{} << true;
        QTest::newRow("empty-name") << QString{} << u"a"_s << false;
        QTest::newRow("case") << u"JAZZ"_s << u"jAzZ"_s << true;
        QTest::newRow("subsequence") << u"Rock Classics"_s << u"rckcl"_s << true;
        QTest::newRow("order") << u"Jazz"_s << u"zj"_s << false;
        QTest::newRow("repeated") << u"Jazz"_s << u"jzz"_s << true;
        QTest::newRow("too-many-repeats") << u"Jazz"_s << u"zzz"_s << false;
        QTest::newRow("literal") << u"[a.*]"_s << u"[.*"_s << true;
        QTest::newRow("not-regex") << u"Jazz"_s << u".*"_s << false;
        QTest::newRow("unicode") << u"ÉTÉ 日本"_s << u"été日"_s << true;
        QTest::newRow("unicode-case") << u"Straße"_s << u"straße"_s << true;
    }

    void matches()
    {
        QFETCH(QString, name);
        QFETCH(QString, query);
        QFETCH(bool, expected);
        QCOMPARE(playlistFuzzyScore(name.toCaseFolded(), query.toCaseFolded()).has_value(), expected);
    }

    void ranksMatches()
    {
        const auto score = [](const QString& name, const QString& query) {
            return playlistFuzzyScore(name, query).value();
        };
        QVERIFY(score(u"rock"_s, u"rock"_s) > score(u"rock classics"_s, u"rock"_s));
        QVERIFY(score(u"rock classics"_s, u"rock"_s) > score(u"classic rock"_s, u"rock"_s));
        QVERIFY(score(u"classic rock"_s, u"rock"_s) > score(u"r o c k"_s, u"rock"_s));
        QVERIFY(score(u"rock classics"_s, u"rc"_s) > score(u"rxxxxxxxxc"_s, u"rc"_s));
        // A later compact alignment beats the greedy first-character alignment.
        QVERIFY(score(u"a________ab"_s, u"ab"_s) > score(u"a_________b"_s, u"ab"_s));
        QVERIFY(score(u"a____ab_c"_s, u"abc"_s) > score(u"a_____b_c"_s, u"abc"_s));
    }
};

QTEST_GUILESS_MAIN(TestPlaylistFuzzyMatcher)
#include "playlistfuzzymatchertest.moc"
