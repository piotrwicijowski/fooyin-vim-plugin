#include "searchnavigation.h"

#include <QTest>

#include <vector>

using namespace Fooyin::VimMotions;

class TestSearchNavigation : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void firstMatchWrapsByDefault();
    void firstMatchStopsWithoutWrap();
    void nextMatchWrapsByDefault();
    void nextMatchStopsWithoutWrap();
    void nextMatchStaysAtEdgeWithoutWrap();
    void prevMatchWrapsByDefault();
    void prevMatchStopsWithoutWrap();
    void prevMatchStaysAtEdgeWithoutWrap();
};

void TestSearchNavigation::firstMatchWrapsByDefault()
{
    const std::vector<int> matches{1, 3, 5};
    QCOMPARE(firstSearchMatchIndex(matches, 6, true), 0);
}

void TestSearchNavigation::firstMatchStopsWithoutWrap()
{
    const std::vector<int> matches{1, 3, 5};
    QCOMPARE(firstSearchMatchIndex(matches, 6, false), -1);
}

void TestSearchNavigation::nextMatchWrapsByDefault()
{
    QCOMPARE(nextSearchMatchIndex(2, 3, true), 0);
}

void TestSearchNavigation::nextMatchStopsWithoutWrap()
{
    QCOMPARE(nextSearchMatchIndex(2, 3, false), -1);
}

void TestSearchNavigation::nextMatchStaysAtEdgeWithoutWrap()
{
    int currentIdx = 2;
    const int nextIdx = nextSearchMatchIndex(currentIdx, 3, false);
    if (nextIdx >= 0)
        currentIdx = nextIdx;

    QCOMPARE(currentIdx, 2);

    const int secondNextIdx = nextSearchMatchIndex(currentIdx, 3, false);
    if (secondNextIdx >= 0)
        currentIdx = secondNextIdx;

    QCOMPARE(currentIdx, 2);
}

void TestSearchNavigation::prevMatchWrapsByDefault()
{
    QCOMPARE(prevSearchMatchIndex(0, 3, true), 2);
}

void TestSearchNavigation::prevMatchStopsWithoutWrap()
{
    QCOMPARE(prevSearchMatchIndex(0, 3, false), -1);
}

void TestSearchNavigation::prevMatchStaysAtEdgeWithoutWrap()
{
    int currentIdx = 0;
    const int prevIdx = prevSearchMatchIndex(currentIdx, 3, false);
    if (prevIdx >= 0)
        currentIdx = prevIdx;

    QCOMPARE(currentIdx, 0);

    const int secondPrevIdx = prevSearchMatchIndex(currentIdx, 3, false);
    if (secondPrevIdx >= 0)
        currentIdx = secondPrevIdx;

    QCOMPARE(currentIdx, 0);
}

QTEST_MAIN(TestSearchNavigation)
#include "searchnavigationtest.moc"
