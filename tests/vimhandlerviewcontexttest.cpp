#include "vimhandler.h"

#include <gui/fywidget.h>

#include <QApplication>
#include <QDataStream>
#include <QMimeData>
#include <QStandardItemModel>
#include <QTest>
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

} // namespace Fooyin

namespace {

class FakeOrganiserWidget : public Fooyin::FyWidget
{
    Q_OBJECT

public:
    explicit FakeOrganiserWidget(QWidget* parent = nullptr)
        : Fooyin::FyWidget(parent)
        , m_view(new QTreeView(this))
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(m_view);
    }

    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("Playlist Organiser");
    }

    [[nodiscard]] QString layoutName() const override
    {
        return QStringLiteral("PlaylistOrganiser");
    }

    [[nodiscard]] QTreeView* view() const
    {
        return m_view;
    }

private:
    QTreeView* m_view;
};

class FakeFyWidget : public Fooyin::FyWidget
{
    Q_OBJECT

public:
    explicit FakeFyWidget(QWidget* parent = nullptr)
        : Fooyin::FyWidget(parent)
        , m_view(new QTreeView(this))
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(m_view);
    }

    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("Other Widget");
    }

    [[nodiscard]] QString layoutName() const override
    {
        return QStringLiteral("OtherWidget");
    }

    [[nodiscard]] QTreeView* view() const
    {
        return m_view;
    }

private:
    QTreeView* m_view;
};

class RecordingTreeModel : public QStandardItemModel
{
public:
    static inline constexpr int GroupRole = Qt::UserRole + 100;

    struct DropCall
    {
        int row{-1};
        QString parentLabel;
        int count{0};
    };

    [[nodiscard]] QStringList mimeTypes() const override
    {
        return {kMimeType};
    }

    [[nodiscard]] Qt::DropActions supportedDragActions() const override
    {
        return Qt::MoveAction;
    }

    [[nodiscard]] Qt::DropActions supportedDropActions() const override
    {
        return Qt::MoveAction;
    }

    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override
    {
        return QStandardItemModel::flags(index) | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
    }

    [[nodiscard]] bool hasChildren(const QModelIndex& parent = QModelIndex()) const override
    {
        return QStandardItemModel::hasChildren(parent) || parent.data(GroupRole).toBool();
    }

    [[nodiscard]] QMimeData* mimeData(const QModelIndexList& indexes) const override
    {
        auto* mimeData = new QMimeData();
        QByteArray data;
        QDataStream stream(&data, QIODevice::WriteOnly);
        stream << pathForIndex(indexes.constFirst());
        mimeData->setData(kMimeType, data);
        return mimeData;
    }

    [[nodiscard]] bool canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                                       const QModelIndex& parent) const override
    {
        Q_UNUSED(row)
        Q_UNUSED(column)

        if(action != Qt::MoveAction || !data || !data->hasFormat(kMimeType))
            return false;

        QByteArray payload = data->data(kMimeType);
        QDataStream stream(&payload, QIODevice::ReadOnly);
        QStringList path;
        stream >> path;

        const QModelIndex source = indexForPath(path);
        if(!source.isValid())
            return false;

        for(QModelIndex cursor = parent; cursor.isValid(); cursor = cursor.parent()) {
            if(cursor == source)
                return false;
        }
        return true;
    }

    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                      const QModelIndex& parent) override
    {
        if(!canDropMimeData(data, action, row, column, parent))
            return false;

        Q_UNUSED(column)

        QByteArray payload = data->data(kMimeType);
        QDataStream stream(&payload, QIODevice::ReadOnly);
        QStringList path;
        stream >> path;

        const QModelIndex source = indexForPath(path);
        if(!source.isValid())
            return false;

        auto* sourceParentItem = itemFromIndex(source.parent());
        auto* targetParentItem = itemFromIndex(parent);
        if(!sourceParentItem)
            sourceParentItem = invisibleRootItem();
        if(!targetParentItem)
            targetParentItem = invisibleRootItem();

        m_lastDrop.row         = row;
        m_lastDrop.parentLabel = parent.isValid() ? parent.data().toString() : QStringLiteral("<root>");
        ++m_lastDrop.count;

        int insertRow = row;
        if(insertRow < 0 || insertRow > targetParentItem->rowCount())
            insertRow = targetParentItem->rowCount();

        QList<QStandardItem*> movedRow = sourceParentItem->takeRow(source.row());
        if(source.parent() == parent && source.row() < insertRow)
            --insertRow;
        targetParentItem->insertRow(insertRow, movedRow);
        return true;
    }

    [[nodiscard]] DropCall lastDrop() const
    {
        return m_lastDrop;
    }

private:
    [[nodiscard]] QStringList pathForIndex(QModelIndex index) const
    {
        QStringList path;
        while(index.isValid()) {
            path.prepend(index.data().toString());
            index = index.parent();
        }
        return path;
    }

    [[nodiscard]] QModelIndex indexForPath(const QStringList& path) const
    {
        QModelIndex parent;
        for(const QString& segment : path) {
            bool matched = false;
            for(int row = 0; row < rowCount(parent); ++row) {
                const QModelIndex idx = index(row, 0, parent);
                if(idx.data().toString() == segment) {
                    parent  = idx;
                    matched = true;
                    break;
                }
            }
            if(!matched)
                return {};
        }
        return parent;
    }

    static inline const QString kMimeType = QStringLiteral("application/x-test-visible-move");
    DropCall m_lastDrop;
};

void focusTree(QTreeView* tree)
{
    tree->window()->show();
    tree->setFocus();
    qApp->processEvents();
}

QStandardItem* makeGroupItem(const QString& text)
{
    auto* item = new QStandardItem(text);
    item->setData(true, RecordingTreeModel::GroupRole);
    return item;
}

} // namespace

using namespace Fooyin::VimMotions;

class TestVimHandlerViewContext : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void classifiesNullView();
    void classifiesPlaylistView();
    void classifiesPlaylistOrganiserTree();
    void classifiesOtherView();
    void organiserMoveDownTargetsNextVisibleSibling();
    void organiserMoveDownTargetsExpandedGroupContents();
    void organiserMoveDownSkipsInvalidDescendantTargets();
    void organiserMoveUpTargetsEndOfGroup();
    void organiserMoveDownExitsGroupAfterParent();
    void organiserMoveDownNestsRootGroupIntoNextGroup();
    void organiserMoveDownExitsLastGroupAtEndOfTree();
    void organiserMoveUpPastNestedGroupStaysInOuterGroup();
    void organiserMoveUpIntoNestedGroupWhenSharingOuterParent();
    void organiserMoveDownMovesLastChildGroupOutOfParent();
    void organiserMoveDownIntoEmptyGroup();
    void organiserMoveUpIntoEmptyGroup();
    void organiserMoveUpPastEmptyNestedGroupStaysInOuterGroup();
};

void TestVimHandlerViewContext::classifiesNullView()
{
    VimHandler handler;
    QCOMPARE(handler.viewContext(nullptr), VimHandler::ViewContext::None);
}

void TestVimHandlerViewContext::classifiesPlaylistView()
{
    VimHandler handler;
    Fooyin::PlaylistView view;
    QCOMPARE(handler.viewContext(&view), VimHandler::ViewContext::PlaylistView);
}

void TestVimHandlerViewContext::classifiesPlaylistOrganiserTree()
{
    VimHandler handler;
    FakeOrganiserWidget organiser;
    QCOMPARE(handler.viewContext(organiser.view()), VimHandler::ViewContext::PlaylistOrganiser);
}

void TestVimHandlerViewContext::classifiesOtherView()
{
    VimHandler handler;
    FakeFyWidget widget;
    QCOMPARE(handler.viewContext(widget.view()), VimHandler::ViewContext::Other);
}

void TestVimHandlerViewContext::organiserMoveDownTargetsNextVisibleSibling()
{
    VimHandler handler;
    FakeOrganiserWidget organiser;
    RecordingTreeModel model;

    model.appendRow(new QStandardItem(QStringLiteral("A")));
    model.appendRow(new QStandardItem(QStringLiteral("B")));
    model.appendRow(new QStandardItem(QStringLiteral("C")));
    organiser.view()->setModel(&model);
    organiser.view()->setCurrentIndex(model.index(1, 0));

    focusTree(organiser.view());
    handler.moveRows(+1);

    const auto drop = model.lastDrop();
    QCOMPARE(drop.count, 1);
    QCOMPARE(drop.parentLabel, QStringLiteral("<root>"));
    QCOMPARE(drop.row, 3);
}

void TestVimHandlerViewContext::organiserMoveDownTargetsExpandedGroupContents()
{
    VimHandler handler;
    FakeOrganiserWidget organiser;
    RecordingTreeModel model;

    model.appendRow(new QStandardItem(QStringLiteral("A")));
    auto* group = makeGroupItem(QStringLiteral("Group"));
    group->appendRow(new QStandardItem(QStringLiteral("Child")));
    model.appendRow(group);
    model.appendRow(new QStandardItem(QStringLiteral("Tail")));

    organiser.view()->setModel(&model);
    organiser.view()->expand(model.index(1, 0));
    organiser.view()->setCurrentIndex(model.index(0, 0));

    focusTree(organiser.view());
    handler.moveRows(+1);

    const auto drop = model.lastDrop();
    QCOMPARE(drop.count, 1);
    QCOMPARE(drop.parentLabel, QStringLiteral("Group"));
    QCOMPARE(drop.row, 0);
}

void TestVimHandlerViewContext::organiserMoveDownSkipsInvalidDescendantTargets()
{
    VimHandler handler;
    FakeOrganiserWidget organiser;
    RecordingTreeModel model;

    auto* group = makeGroupItem(QStringLiteral("Group"));
    group->appendRow(new QStandardItem(QStringLiteral("Child1")));
    group->appendRow(new QStandardItem(QStringLiteral("Child2")));
    model.appendRow(group);
    model.appendRow(new QStandardItem(QStringLiteral("Tail")));

    organiser.view()->setModel(&model);
    organiser.view()->expand(model.index(0, 0));
    organiser.view()->setCurrentIndex(model.index(0, 0));

    focusTree(organiser.view());
    handler.moveRows(+1);

    const auto drop = model.lastDrop();
    QCOMPARE(drop.count, 1);
    QCOMPARE(drop.parentLabel, QStringLiteral("<root>"));
    QCOMPARE(drop.row, 2);
}

void TestVimHandlerViewContext::organiserMoveUpTargetsEndOfGroup()
{
    VimHandler handler;
    FakeOrganiserWidget organiser;
    RecordingTreeModel model;

    auto* group = makeGroupItem(QStringLiteral("Group"));
    group->appendRow(new QStandardItem(QStringLiteral("Item 1")));
    group->appendRow(new QStandardItem(QStringLiteral("Item 2")));
    model.appendRow(group);
    model.appendRow(new QStandardItem(QStringLiteral("Item 3")));

    organiser.view()->setModel(&model);
    organiser.view()->expand(model.index(0, 0));
    organiser.view()->setCurrentIndex(model.index(1, 0));

    focusTree(organiser.view());
    handler.moveRows(-1);
    qApp->processEvents();

    const auto drop = model.lastDrop();
    QCOMPARE(drop.count, 1);
    QCOMPARE(drop.parentLabel, QStringLiteral("Group"));
    QCOMPARE(drop.row, 2);
    QCOMPARE(organiser.view()->currentIndex().data().toString(), QStringLiteral("Item 3"));
}

void TestVimHandlerViewContext::organiserMoveDownExitsGroupAfterParent()
{
    VimHandler handler;
    FakeOrganiserWidget organiser;
    RecordingTreeModel model;

    auto* group = makeGroupItem(QStringLiteral("Group"));
    group->appendRow(new QStandardItem(QStringLiteral("Item 1")));
    group->appendRow(new QStandardItem(QStringLiteral("Item 2")));
    model.appendRow(group);
    model.appendRow(new QStandardItem(QStringLiteral("Item 3")));

    organiser.view()->setModel(&model);
    organiser.view()->expand(model.index(0, 0));
    organiser.view()->setCurrentIndex(model.index(1, 0, model.index(0, 0)));

    focusTree(organiser.view());
    handler.moveRows(+1);
    qApp->processEvents();

    const auto drop = model.lastDrop();
    QCOMPARE(drop.count, 1);
    QCOMPARE(drop.parentLabel, QStringLiteral("<root>"));
    QCOMPARE(drop.row, 1);
}

void TestVimHandlerViewContext::organiserMoveDownNestsRootGroupIntoNextGroup()
{
    VimHandler handler;
    FakeOrganiserWidget organiser;
    RecordingTreeModel model;

    auto* group1 = makeGroupItem(QStringLiteral("Group 1"));
    group1->appendRow(new QStandardItem(QStringLiteral("Item 1")));
    auto* group2 = makeGroupItem(QStringLiteral("Group 2"));
    group2->appendRow(new QStandardItem(QStringLiteral("Item 2")));
    model.appendRow(group1);
    model.appendRow(group2);

    organiser.view()->setModel(&model);
    organiser.view()->expand(model.index(0, 0));
    organiser.view()->expand(model.index(1, 0));
    organiser.view()->setCurrentIndex(model.index(0, 0));

    focusTree(organiser.view());
    handler.moveRows(+1);
    qApp->processEvents();

    const auto drop = model.lastDrop();
    QCOMPARE(drop.count, 1);
    QCOMPARE(drop.parentLabel, QStringLiteral("Group 2"));
    QCOMPARE(drop.row, 0);
}

void TestVimHandlerViewContext::organiserMoveDownExitsLastGroupAtEndOfTree()
{
    VimHandler handler;
    FakeOrganiserWidget organiser;
    RecordingTreeModel model;

    auto* group = makeGroupItem(QStringLiteral("Group 1"));
    group->appendRow(new QStandardItem(QStringLiteral("Item 1")));
    group->appendRow(new QStandardItem(QStringLiteral("Item 2")));
    model.appendRow(group);

    organiser.view()->setModel(&model);
    organiser.view()->expand(model.index(0, 0));
    organiser.view()->setCurrentIndex(model.index(1, 0, model.index(0, 0)));

    focusTree(organiser.view());
    handler.moveRows(+1);
    qApp->processEvents();

    const auto drop = model.lastDrop();
    QCOMPARE(drop.count, 1);
    QCOMPARE(drop.parentLabel, QStringLiteral("<root>"));
    QCOMPARE(drop.row, 1);
}

void TestVimHandlerViewContext::organiserMoveUpPastNestedGroupStaysInOuterGroup()
{
    VimHandler handler;
    FakeOrganiserWidget organiser;
    RecordingTreeModel model;

    auto* group1 = makeGroupItem(QStringLiteral("Group 1"));
    group1->appendRow(new QStandardItem(QStringLiteral("Item 1")));
    auto* group2 = makeGroupItem(QStringLiteral("Group 2"));
    group2->appendRow(new QStandardItem(QStringLiteral("Item 2")));
    group2->appendRow(new QStandardItem(QStringLiteral("Item 3")));
    group1->appendRow(group2);
    model.appendRow(group1);
    model.appendRow(new QStandardItem(QStringLiteral("Item 4")));

    organiser.view()->setModel(&model);
    organiser.view()->expand(model.index(0, 0));
    organiser.view()->expand(model.index(1, 0, model.index(0, 0)));
    organiser.view()->setCurrentIndex(model.index(1, 0));

    focusTree(organiser.view());
    handler.moveRows(-1);
    qApp->processEvents();

    const auto drop = model.lastDrop();
    QCOMPARE(drop.count, 1);
    QCOMPARE(drop.parentLabel, QStringLiteral("Group 1"));
    QCOMPARE(drop.row, 2);
}

void TestVimHandlerViewContext::organiserMoveUpIntoNestedGroupWhenSharingOuterParent()
{
    VimHandler handler;
    FakeOrganiserWidget organiser;
    RecordingTreeModel model;

    auto* group1 = makeGroupItem(QStringLiteral("Group 1"));
    group1->appendRow(new QStandardItem(QStringLiteral("Item 1")));
    auto* group2 = makeGroupItem(QStringLiteral("Group 2"));
    group2->appendRow(new QStandardItem(QStringLiteral("Item 2")));
    group2->appendRow(new QStandardItem(QStringLiteral("Item 3")));
    group1->appendRow(group2);
    group1->appendRow(new QStandardItem(QStringLiteral("Item 4")));
    model.appendRow(group1);

    organiser.view()->setModel(&model);
    organiser.view()->expand(model.index(0, 0));
    organiser.view()->expand(model.index(1, 0, model.index(0, 0)));
    organiser.view()->setCurrentIndex(model.index(2, 0, model.index(0, 0)));

    focusTree(organiser.view());
    handler.moveRows(-1);
    qApp->processEvents();

    const auto drop = model.lastDrop();
    QCOMPARE(drop.count, 1);
    QCOMPARE(drop.parentLabel, QStringLiteral("Group 2"));
    QCOMPARE(drop.row, 2);
}

void TestVimHandlerViewContext::organiserMoveDownMovesLastChildGroupOutOfParent()
{
    VimHandler handler;
    FakeOrganiserWidget organiser;
    RecordingTreeModel model;

    auto* group1 = makeGroupItem(QStringLiteral("Group 1"));
    group1->appendRow(new QStandardItem(QStringLiteral("Item 1")));
    auto* group2 = makeGroupItem(QStringLiteral("Group 2"));
    group2->appendRow(new QStandardItem(QStringLiteral("Item 2")));
    group2->appendRow(new QStandardItem(QStringLiteral("Item 3")));
    group1->appendRow(group2);
    model.appendRow(group1);

    organiser.view()->setModel(&model);
    organiser.view()->expand(model.index(0, 0));
    organiser.view()->expand(model.index(1, 0, model.index(0, 0)));
    organiser.view()->setCurrentIndex(model.index(1, 0, model.index(0, 0)));

    focusTree(organiser.view());
    handler.moveRows(+1);
    qApp->processEvents();

    const auto drop = model.lastDrop();
    QCOMPARE(drop.count, 1);
    QCOMPARE(drop.parentLabel, QStringLiteral("<root>"));
    QCOMPARE(drop.row, 1);
}

void TestVimHandlerViewContext::organiserMoveDownIntoEmptyGroup()
{
    VimHandler handler;
    FakeOrganiserWidget organiser;
    RecordingTreeModel model;

    model.appendRow(new QStandardItem(QStringLiteral("Item 1")));
    model.appendRow(makeGroupItem(QStringLiteral("Empty Group")));

    organiser.view()->setModel(&model);
    organiser.view()->setCurrentIndex(model.index(0, 0));

    focusTree(organiser.view());
    handler.moveRows(+1);
    qApp->processEvents();

    const auto drop = model.lastDrop();
    QCOMPARE(drop.count, 1);
    QCOMPARE(drop.parentLabel, QStringLiteral("Empty Group"));
    QCOMPARE(drop.row, 0);
}

void TestVimHandlerViewContext::organiserMoveUpIntoEmptyGroup()
{
    VimHandler handler;
    FakeOrganiserWidget organiser;
    RecordingTreeModel model;

    model.appendRow(makeGroupItem(QStringLiteral("Empty Group")));
    model.appendRow(new QStandardItem(QStringLiteral("Item 1")));

    organiser.view()->setModel(&model);
    organiser.view()->setCurrentIndex(model.index(1, 0));

    focusTree(organiser.view());
    handler.moveRows(-1);
    qApp->processEvents();

    const auto drop = model.lastDrop();
    QCOMPARE(drop.count, 1);
    QCOMPARE(drop.parentLabel, QStringLiteral("Empty Group"));
    QCOMPARE(drop.row, 0);
}

void TestVimHandlerViewContext::organiserMoveUpPastEmptyNestedGroupStaysInOuterGroup()
{
    VimHandler handler;
    FakeOrganiserWidget organiser;
    RecordingTreeModel model;

    auto* group1 = makeGroupItem(QStringLiteral("Group 1"));
    group1->appendRow(new QStandardItem(QStringLiteral("Item 1")));
    group1->appendRow(makeGroupItem(QStringLiteral("Group 2")));
    model.appendRow(group1);
    model.appendRow(new QStandardItem(QStringLiteral("Item 2")));

    organiser.view()->setModel(&model);
    organiser.view()->expand(model.index(0, 0));
    organiser.view()->setCurrentIndex(model.index(1, 0));

    focusTree(organiser.view());
    handler.moveRows(-1);
    qApp->processEvents();

    const auto drop = model.lastDrop();
    QCOMPARE(drop.count, 1);
    QCOMPARE(drop.parentLabel, QStringLiteral("Group 1"));
    QCOMPARE(drop.row, 2);
}

QTEST_MAIN(TestVimHandlerViewContext)
#include "vimhandlerviewcontexttest.moc"
