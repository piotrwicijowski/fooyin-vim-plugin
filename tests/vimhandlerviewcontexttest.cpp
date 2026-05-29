#include "spatialnavigator.h"
#include "vimhandler.h"
#include "vimmotionssettings.h"

#include <core/engine/audioloader.h>
#include <core/library/musiclibrary.h>
#include <core/playlist/playlisthandler.h>
#include <core/track.h>
#include <gui/fywidget.h>
#include <gui/playlist/playlistselectionobserver.h>
#include <utils/database/dbconnectionhandler.h>
#include <utils/database/dbconnectionpool.h>
#include <utils/database/dbconnectionprovider.h>
#include <utils/database/dbquery.h>
#include <utils/settings/settingsmanager.h>

#include <QAbstractItemModel>
#include <QApplication>
#include <QDataStream>
#include <QFocusEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTest>
#include <QTreeView>
#include <QVBoxLayout>

#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QSettings>
#include <QTemporaryDir>

#include <memory>

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

using namespace Qt::StringLiterals;

bool createPlaylistTables(const Fooyin::DbConnectionPoolPtr& dbPool)
{
    const Fooyin::DbConnectionProvider dbProvider{dbPool};

    Fooyin::DbQuery createPlaylists{dbProvider.db(), u"CREATE TABLE IF NOT EXISTS Playlists ("
                                                     "PlaylistID INTEGER PRIMARY KEY AUTOINCREMENT, "
                                                     "Name TEXT NOT NULL UNIQUE, "
                                                     "PlaylistIndex INTEGER, "
                                                     "IsAutoPlaylist INTEGER DEFAULT 0, "
                                                     "Query TEXT, "
                                                     "SortQuery TEXT, "
                                                     "ForceSorted INTEGER DEFAULT 1);"_s};
    if(!createPlaylists.exec())
        return false;

    Fooyin::DbQuery createPlaylistTracks{dbProvider.db(), u"CREATE TABLE IF NOT EXISTS PlaylistTracks ("
                                                          "PlaylistID INTEGER NOT NULL, "
                                                          "TrackID INTEGER NOT NULL, "
                                                          "TrackIndex INTEGER NOT NULL);"_s};
    return createPlaylistTracks.exec();
}

class StubMusicLibrary : public Fooyin::MusicLibrary
{
public:
    explicit StubMusicLibrary(QObject* parent = nullptr)
        : Fooyin::MusicLibrary(parent)
    { }

    bool hasLibrary() const override
    {
        return false;
    }
    std::optional<Fooyin::LibraryInfo> libraryInfo(int) const override
    {
        return std::nullopt;
    }
    std::optional<Fooyin::LibraryInfo> libraryForPath(const QString&) const override
    {
        return std::nullopt;
    }
    void loadAllTracks() override { }
    bool isEmpty() const override
    {
        return m_tracks.empty();
    }
    void refreshAll() override { }
    void rescanAll() override { }
    Fooyin::ScanRequest refresh(const Fooyin::LibraryInfo&) override
    {
        return {.type = Fooyin::ScanRequest::Library, .cancel = []() {
                }};
    }
    Fooyin::ScanRequest rescan(const Fooyin::LibraryInfo&) override
    {
        return {.type = Fooyin::ScanRequest::Library, .cancel = []() {
                }};
    }
    void cancelScan(int) override { }
    Fooyin::ScanRequest scanTracks(const Fooyin::TrackList&) override
    {
        return {.type = Fooyin::ScanRequest::Tracks, .cancel = []() {
                }};
    }
    Fooyin::ScanRequest scanModifiedTracks(const Fooyin::TrackList&) override
    {
        return {.type = Fooyin::ScanRequest::Tracks, .cancel = []() {
                }};
    }
    Fooyin::ScanRequest scanFiles(const QList<QUrl>&) override
    {
        return {.type = Fooyin::ScanRequest::Files, .cancel = []() {
                }};
    }
    Fooyin::ScanRequest loadPlaylist(const QList<QUrl>&) override
    {
        return {.type = Fooyin::ScanRequest::Playlist, .cancel = []() {
                }};
    }
    Fooyin::TrackList tracks() const override
    {
        return m_tracks;
    }
    Fooyin::TrackList libraryTracks() const override
    {
        return m_tracks;
    }
    Fooyin::Track trackForId(int id) const override
    {
        for(const auto& track : m_tracks) {
            if(track.id() == id)
                return track;
        }
        return {};
    }
    Fooyin::TrackList tracksForIds(const Fooyin::TrackIds& ids) const override
    {
        Fooyin::TrackList result;
        result.reserve(ids.size());
        for(const int id : ids) {
            if(const Fooyin::Track track = trackForId(id); track.isValid())
                result.emplace_back(track);
        }
        return result;
    }
    std::shared_ptr<Fooyin::TrackMetadataStore> metadataStore() const override
    {
        return {};
    }
    void updateTrack(const Fooyin::Track&) override { }
    void updateTracks(const Fooyin::TrackList&) override { }
    void updateTrackMetadata(const Fooyin::TrackList&) override { }
    Fooyin::WriteRequest writeTrackMetadata(const Fooyin::TrackList&) override
    {
        return {};
    }
    Fooyin::WriteRequest writeTrackCovers(const Fooyin::TrackCoverData&) override
    {
        return {};
    }
    void updateTrackStats(const Fooyin::TrackList&) override { }
    void updateTrackStats(const Fooyin::Track&) override { }
    Fooyin::WriteRequest removeUnavailbleTracks() override
    {
        return {};
    }
    Fooyin::WriteRequest deleteTracks(const Fooyin::TrackList&) override
    {
        return {};
    }

private:
    Fooyin::TrackList m_tracks;
};

struct PlaylistHandlerHarness
{
    explicit PlaylistHandlerHarness(Fooyin::SettingsManager& settings)
        : dbPool{[this]() {
            Fooyin::DbConnection::DbParams params;
            params.type     = u"QSQLITE"_s;
            params.filePath = dbDir.filePath(u"playlisthandler.sqlite"_s);
            return Fooyin::DbConnectionPool::create(params, u"vimplugin_playlisthandler_test"_s);
        }()}
        , dbConnectionHandler{dbPool}
        , dbInitialised{dbConnectionHandler.hasConnection() && createPlaylistTables(dbPool)}
        , audioLoader{std::make_shared<Fooyin::AudioLoader>()}
        , handler{dbPool, audioLoader, &library, &settings}
    { }

    QTemporaryDir dbDir;
    Fooyin::DbConnectionPoolPtr dbPool;
    Fooyin::DbConnectionHandler dbConnectionHandler;
    bool dbInitialised{false};
    std::shared_ptr<Fooyin::AudioLoader> audioLoader;
    StubMusicLibrary library;
    Fooyin::PlaylistHandler handler;
};

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

class FakePlaylistSelectionObserver : public Fooyin::PlaylistSelectionObserver
{
    Q_OBJECT

public:
    explicit FakePlaylistSelectionObserver(QObject* parent = nullptr)
        : Fooyin::PlaylistSelectionObserver(parent)
    { }

    [[nodiscard]] Fooyin::Playlist* currentPlaylist() const override
    {
        return m_currentPlaylist;
    }

    [[nodiscard]] Fooyin::UId currentPlaylistId() const override
    {
        return m_currentPlaylist ? m_currentPlaylist->id() : Fooyin::UId{};
    }

    void changeCurrentPlaylist(const Fooyin::UId& id) override
    {
        if(!m_playlistHandler) {
            return;
        }

        changeCurrentPlaylist(m_playlistHandler->playlistById(id));
    }

    void setPlaylistHandler(Fooyin::PlaylistHandler* playlistHandler)
    {
        m_playlistHandler = playlistHandler;
    }

    void changeCurrentPlaylist(Fooyin::Playlist* playlist)
    {
        auto* previous    = m_currentPlaylist;
        m_currentPlaylist = playlist;
        if(previous != m_currentPlaylist)
            emit currentPlaylistChanged(previous, m_currentPlaylist);
    }

private:
    Fooyin::PlaylistHandler* m_playlistHandler{nullptr};
    Fooyin::Playlist* m_currentPlaylist{nullptr};
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

class MoveAwareTreeModel : public QAbstractItemModel
{
public:
    struct Node
    {
        QString text;
        Node* parent{nullptr};
        std::vector<std::unique_ptr<Node>> children;
    };

    explicit MoveAwareTreeModel(QObject* parent = nullptr)
        : QAbstractItemModel(parent)
    { }

    QModelIndex appendRoot(const QString& text)
    {
        return appendChild({}, text);
    }

    QModelIndex appendChild(const QModelIndex& parent, const QString& text)
    {
        Node* parentNode = nodeForIndex(parent);
        const int row    = static_cast<int>(parentNode->children.size());
        beginInsertRows(parent, row, row);
        auto child    = std::make_unique<Node>();
        child->text   = text;
        child->parent = parentNode;
        Node* raw     = child.get();
        parentNode->children.push_back(std::move(child));
        endInsertRows();
        return createIndex(row, 0, raw);
    }

    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override
    {
        if(column != 0 || row < 0)
            return {};

        const Node* parentNode = nodeForIndex(parent);
        if(row >= static_cast<int>(parentNode->children.size()))
            return {};

        return createIndex(row, column, parentNode->children[static_cast<size_t>(row)].get());
    }

    QModelIndex parent(const QModelIndex& child) const override
    {
        if(!child.isValid())
            return {};

        const Node* node       = nodeForIndex(child);
        const Node* parentNode = node ? node->parent : nullptr;
        if(!parentNode || parentNode == &m_root)
            return {};

        return createIndex(rowOfNode(parentNode), 0, const_cast<Node*>(parentNode));
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return static_cast<int>(nodeForIndex(parent)->children.size());
    }

    int columnCount(const QModelIndex& parent = QModelIndex()) const override
    {
        Q_UNUSED(parent)
        return 1;
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override
    {
        if(!index.isValid() || (role != Qt::DisplayRole && role != Qt::EditRole && role != Qt::ToolTipRole))
            return {};

        return nodeForIndex(index)->text;
    }

    bool moveRows(const QModelIndex& sourceParent, int sourceRow, int count, const QModelIndex& destinationParent,
                  int destinationChild) override
    {
        if(count != 1 || sourceRow < 0 || destinationChild < 0)
            return false;

        Node* sourceParentNode = nodeForIndex(sourceParent);
        Node* destParentNode   = nodeForIndex(destinationParent);
        if(sourceRow >= static_cast<int>(sourceParentNode->children.size())
           || destinationChild > static_cast<int>(destParentNode->children.size())) {
            return false;
        }

        if(sourceParent == destinationParent
           && (destinationChild == sourceRow || destinationChild == sourceRow + count)) {
            return false;
        }

        beginMoveRows(sourceParent, sourceRow, sourceRow + count - 1, destinationParent, destinationChild);

        auto moved = std::move(sourceParentNode->children[static_cast<size_t>(sourceRow)]);
        sourceParentNode->children.erase(sourceParentNode->children.begin() + sourceRow);

        int insertRow = destinationChild;
        if(sourceParent == destinationParent && sourceRow < destinationChild)
            --insertRow;

        moved->parent = destParentNode;
        destParentNode->children.insert(destParentNode->children.begin() + insertRow, std::move(moved));

        endMoveRows();
        return true;
    }

private:
    [[nodiscard]] Node* nodeForIndex(const QModelIndex& index) const
    {
        if(index.isValid())
            return static_cast<Node*>(index.internalPointer());
        return const_cast<Node*>(&m_root);
    }

    [[nodiscard]] int rowOfNode(const Node* node) const
    {
        const Node* parentNode = node ? node->parent : nullptr;
        if(!parentNode)
            return -1;

        for(int row = 0; row < static_cast<int>(parentNode->children.size()); ++row) {
            if(parentNode->children[static_cast<size_t>(row)].get() == node)
                return row;
        }

        return -1;
    }

    Node m_root;
};

void focusTree(QTreeView* tree)
{
    tree->window()->show();
    tree->setFocus();
    qApp->processEvents();
}

void pumpEvents()
{
    qApp->processEvents();
    QTest::qWait(0);
    qApp->processEvents();
}

bool dispatchKey(Fooyin::VimMotions::VimHandler& handler, QObject* watched, QChar ch)
{
    QKeyEvent event(QEvent::KeyPress, Qt::Key_A + (ch.toLower().unicode() - u'a'), Qt::NoModifier, QString(ch));
    return handler.eventFilter(watched, &event);
}

void dispatchFocusIn(Fooyin::VimMotions::VimHandler& handler, QWidget* watched)
{
    QFocusEvent event(QEvent::FocusIn);
    handler.eventFilter(watched, &event);
}

QStandardItem* makeGroupItem(const QString& text)
{
    auto* item = new QStandardItem(text);
    item->setData(true, RecordingTreeModel::GroupRole);
    item->setData(1, Qt::UserRole);
    return item;
}

QStandardItem* makeOrganiserPlaylistItem(const QString& text, Fooyin::Playlist* playlist)
{
    auto* item = new QStandardItem(text);
    item->setData(2, Qt::UserRole);
    item->setData(QVariant::fromValue(playlist), Qt::UserRole + 1);
    return item;
}

void syncPlaylistModel(QStandardItemModel& model, const Fooyin::Playlist* playlist)
{
    model.clear();
    if(!playlist)
        return;

    for(const auto& track : playlist->tracks())
        model.appendRow(new QStandardItem(track.title()));
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
    void organiserInlineEditorSuspendsVimCapture();
    void searchBarTypingKeepsFocus();
    void scopedBindingsPreferActiveViewOverGlobalFallback();
    void pasteTargetsObservedEmptySelectedPlaylist();
    void switchesToNextPlaylist();
    void switchesToPreviousPlaylist();
    void playlistSwitchingClampsAtEnds();
    void playlistSwitchingUsesCount();
    void playlistSwitchingFollowsOrganiserTreeOrder();
    void restoresSavedCursorWhenReturningToPlaylist();
    void clampsRestoredCursorWhenPlaylistShrinks();
    void restoresVisualSelectionWhenReturningToPlaylist();
    void restoresEmptyPlaylistToNormalMode();
    void organiserSearchFindsVisibleChild();
    void organiserSearchSkipsCollapsedChildren();
    void organiserSearchNavigatesVisibleMatches();
    void organiserSearchKeepsEarlierRootCandidatesAfterJump();
    void playlistSearchNavigationTracksReorderedMatches();
    void organiserSearchNavigationTracksReorderedMatches();
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

void TestVimHandlerViewContext::organiserInlineEditorSuspendsVimCapture()
{
    VimHandler handler;
    FakeOrganiserWidget organiser;
    QStandardItemModel model;

    model.appendRow(new QStandardItem(QStringLiteral("Playlist")));
    organiser.view()->setModel(&model);

    focusTree(organiser.view());
    organiser.view()->edit(model.index(0, 0));
    qApp->processEvents();

    auto* editor = organiser.view()->findChild<QLineEdit*>();
    QVERIFY(editor);
    QVERIFY(editor->hasFocus());

    qApp->installEventFilter(&handler);
    editor->setFocus();
    editor->clear();
    QTest::keyClicks(editor, QStringLiteral("aj"));
    qApp->processEvents();
    qApp->removeEventFilter(&handler);

    QCOMPARE(editor->text(), QStringLiteral("aj"));

    QTest::keyClick(editor, Qt::Key_Return);
    qApp->processEvents();
}

void TestVimHandlerViewContext::searchBarTypingKeepsFocus()
{
    VimHandler handler;
    Fooyin::PlaylistView view;
    QStandardItemModel model;

    model.appendRow(new QStandardItem(QStringLiteral("Alpha")));
    model.appendRow(new QStandardItem(QStringLiteral("Beta")));
    view.setModel(&model);
    view.setCurrentIndex(model.index(0, 0));

    qApp->installEventFilter(&handler);
    focusTree(&view);
    handler.enterSearch();
    qApp->processEvents();

    auto* editor = view.window()->findChild<QLineEdit*>();
    QVERIFY(editor);
    QVERIFY(editor->hasFocus());
    QCOMPARE(handler.mode(), VimHandler::Mode::Search);

    QTest::keyClicks(editor, QStringLiteral("a"));
    qApp->processEvents();

    QVERIFY(editor->hasFocus());
    QCOMPARE(handler.mode(), VimHandler::Mode::Search);

    QTest::keyClick(editor, Qt::Key_Escape);
    qApp->processEvents();

    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);
    qApp->removeEventFilter(&handler);
}

void TestVimHandlerViewContext::scopedBindingsPreferActiveViewOverGlobalFallback()
{
    const QString settingsPath = QDir::tempPath() + QStringLiteral("/fooyin_vim_scoped_view_context.ini");
    QFile::remove(settingsPath);

    Fooyin::SettingsManager settings{settingsPath};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)
    settings.set(QStringLiteral("VimMotions/UseDefaultBindings"), false);
    settings.fileSet(QStringLiteral("VimMotions/Bindings/Global/Normal/j"), QStringLiteral("enterVisual"));
    settings.fileSet(QStringLiteral("VimMotions/Bindings/PlaylistView/Normal/j"), QStringLiteral("moveCursor:+1"));
    settings.fileSet(QStringLiteral("VimMotions/Bindings/PlaylistOrganiser/Normal/j"),
                     QStringLiteral("treeOpenOrDescend"));

    VimHandler handler;
    handler.setSettingsManager(&settings);

    Fooyin::PlaylistView playlistView;
    QStandardItemModel playlistModel;
    playlistModel.appendRow(new QStandardItem(QStringLiteral("One")));
    playlistModel.appendRow(new QStandardItem(QStringLiteral("Two")));
    playlistView.setModel(&playlistModel);
    playlistView.setCurrentIndex(playlistModel.index(0, 0));
    focusTree(&playlistView);

    QVERIFY(dispatchKey(handler, &playlistView, u'j'));
    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);
    QCOMPARE(playlistView.currentIndex().row(), 1);

    FakeOrganiserWidget organiser;
    QStandardItemModel organiserModel;
    auto* group = makeGroupItem(QStringLiteral("Group"));
    group->appendRow(new QStandardItem(QStringLiteral("Child")));
    organiserModel.appendRow(group);
    organiser.view()->setModel(&organiserModel);
    organiser.view()->collapse(organiserModel.index(0, 0));
    organiser.view()->setCurrentIndex(organiserModel.index(0, 0));
    focusTree(organiser.view());

    QVERIFY(dispatchKey(handler, organiser.view(), u'j'));
    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);
    QVERIFY(organiser.view()->isExpanded(organiserModel.index(0, 0)));

    FakeFyWidget otherWidget;
    QStandardItemModel otherModel;
    otherModel.appendRow(new QStandardItem(QStringLiteral("Other")));
    otherWidget.view()->setModel(&otherModel);
    otherWidget.view()->setCurrentIndex(otherModel.index(0, 0));
    focusTree(otherWidget.view());

    QVERIFY(dispatchKey(handler, otherWidget.view(), u'j'));
    QCOMPARE(handler.mode(), VimHandler::Mode::Visual);
}

void TestVimHandlerViewContext::pasteTargetsObservedEmptySelectedPlaylist()
{
    QStandardPaths::setTestModeEnabled(true);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());
    qputenv("XDG_DATA_HOME", tempDir.path().toUtf8());

    const QString settingsPath = tempDir.filePath(QStringLiteral("fooyin_vim_empty_playlist_paste.ini"));
    Fooyin::SettingsManager settings{settingsPath};
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);
    auto* playlistHandler = &harness.handler;

    Fooyin::Track sourceTrack{tempDir.filePath(QStringLiteral("source.flac")), 0};
    sourceTrack.setTitle(QStringLiteral("Source"));
    sourceTrack.setId(1);
    sourceTrack.generateHash();

    auto* sourcePlaylist = playlistHandler->createNewPlaylist(QStringLiteral("Source Playlist"), {sourceTrack});
    auto* emptyPlaylist  = playlistHandler->createNewPlaylist(QStringLiteral("Empty Playlist"));
    QVERIFY(sourcePlaylist);
    QVERIFY(emptyPlaylist);
    QCOMPARE(sourcePlaylist->trackCount(), 1);
    QCOMPARE(emptyPlaylist->trackCount(), 0);

    playlistHandler->changeActivePlaylist(sourcePlaylist);

    VimHandler handler;
    handler.setPlaylistHandler(playlistHandler);

    FakePlaylistSelectionObserver observer;
    handler.setPlaylistSelectionObserver(&observer);

    Fooyin::PlaylistView sourceView;
    QStandardItemModel sourceModel;
    sourceModel.appendRow(new QStandardItem(QStringLiteral("Source")));
    sourceView.setModel(&sourceModel);
    sourceView.setCurrentIndex(sourceModel.index(0, 0));

    focusTree(&sourceView);
    observer.changeCurrentPlaylist(sourcePlaylist);
    handler.yankRows(1);

    Fooyin::PlaylistView emptyView;
    QStandardItemModel emptyModel;
    emptyView.setModel(&emptyModel);

    focusTree(&emptyView);
    observer.changeCurrentPlaylist(emptyPlaylist);
    handler.pasteAfter();

    QCOMPARE(sourcePlaylist->trackCount(), 1);
    QCOMPARE(emptyPlaylist->trackCount(), 1);
    QCOMPARE(emptyPlaylist->tracks().front().title(), QStringLiteral("Source"));
}

void TestVimHandlerViewContext::switchesToNextPlaylist()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("next_playlist.ini"))};
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    harness.handler.createNewPlaylist(QStringLiteral("Playlist A"));
    harness.handler.createNewPlaylist(QStringLiteral("Playlist B"));

    const auto playlists = harness.handler.playlists();
    QVERIFY(playlists.size() >= 2);

    auto* currentPlaylist  = playlists[static_cast<size_t>(playlists.size() - 2)];
    auto* expectedPlaylist = playlists.back();
    QVERIFY(currentPlaylist);
    QVERIFY(expectedPlaylist);

    VimHandler handler;
    handler.setPlaylistHandler(&harness.handler);

    FakePlaylistSelectionObserver observer;
    observer.setPlaylistHandler(&harness.handler);
    handler.setPlaylistSelectionObserver(&observer);
    observer.changeCurrentPlaylist(currentPlaylist);

    handler.nextPlaylist();

    QCOMPARE(observer.currentPlaylist(), expectedPlaylist);
    QCOMPARE(observer.currentPlaylistId(), expectedPlaylist->id());
}

void TestVimHandlerViewContext::switchesToPreviousPlaylist()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("previous_playlist.ini"))};
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    harness.handler.createNewPlaylist(QStringLiteral("Playlist A"));
    harness.handler.createNewPlaylist(QStringLiteral("Playlist B"));

    const auto playlists = harness.handler.playlists();
    QVERIFY(playlists.size() >= 2);

    auto* expectedPlaylist = playlists.front();
    auto* currentPlaylist  = playlists[1];
    QVERIFY(currentPlaylist);
    QVERIFY(expectedPlaylist);

    VimHandler handler;
    handler.setPlaylistHandler(&harness.handler);

    FakePlaylistSelectionObserver observer;
    observer.setPlaylistHandler(&harness.handler);
    handler.setPlaylistSelectionObserver(&observer);
    observer.changeCurrentPlaylist(currentPlaylist);

    handler.previousPlaylist();

    QCOMPARE(observer.currentPlaylist(), expectedPlaylist);
    QCOMPARE(observer.currentPlaylistId(), expectedPlaylist->id());
}

void TestVimHandlerViewContext::playlistSwitchingClampsAtEnds()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("playlist_switch_clamp.ini"))};
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    harness.handler.createNewPlaylist(QStringLiteral("Playlist A"));
    harness.handler.createNewPlaylist(QStringLiteral("Playlist B"));

    const auto playlists = harness.handler.playlists();
    QVERIFY(playlists.size() >= 2);

    auto* firstPlaylist = playlists.front();
    auto* lastPlaylist  = playlists.back();
    QVERIFY(firstPlaylist);
    QVERIFY(lastPlaylist);

    VimHandler handler;
    handler.setPlaylistHandler(&harness.handler);

    FakePlaylistSelectionObserver observer;
    observer.setPlaylistHandler(&harness.handler);
    handler.setPlaylistSelectionObserver(&observer);

    observer.changeCurrentPlaylist(firstPlaylist);
    handler.previousPlaylist();
    QCOMPARE(observer.currentPlaylist(), firstPlaylist);

    observer.changeCurrentPlaylist(lastPlaylist);
    handler.nextPlaylist();
    QCOMPARE(observer.currentPlaylist(), lastPlaylist);
}

bool dispatchDigit(Fooyin::VimMotions::VimHandler& handler, QObject* watched, int digit)
{
    const QString text = QString::number(digit);
    QKeyEvent event(QEvent::KeyPress, Qt::Key_0 + digit, Qt::NoModifier, text);
    return handler.eventFilter(watched, &event);
}

void TestVimHandlerViewContext::playlistSwitchingUsesCount()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("playlist_switch_count.ini"))};
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    harness.handler.createNewPlaylist(QStringLiteral("Playlist A"));
    harness.handler.createNewPlaylist(QStringLiteral("Playlist B"));
    harness.handler.createNewPlaylist(QStringLiteral("Playlist C"));

    const auto playlists = harness.handler.playlists();
    QVERIFY(playlists.size() >= 3);

    auto* playlistA = playlists[0];
    auto* playlistC = playlists[2];
    QVERIFY(playlistA);
    QVERIFY(playlistC);

    VimHandler handler;
    handler.setPlaylistHandler(&harness.handler);

    FakePlaylistSelectionObserver observer;
    observer.setPlaylistHandler(&harness.handler);
    handler.setPlaylistSelectionObserver(&observer);

    Fooyin::PlaylistView view;
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("One")));
    view.setModel(&model);
    view.setCurrentIndex(model.index(0, 0));
    focusTree(&view);

    QVERIFY(dispatchDigit(handler, &view, 2));

    observer.changeCurrentPlaylist(playlistA);
    handler.nextPlaylist();
    QCOMPARE(observer.currentPlaylist(), playlistC);

    handler.clearPendingState();
    QVERIFY(dispatchDigit(handler, &view, 2));

    handler.previousPlaylist();
    QCOMPARE(observer.currentPlaylist(), playlistA);
}

void TestVimHandlerViewContext::playlistSwitchingFollowsOrganiserTreeOrder()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("playlist_switch_organiser.ini"))};
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    auto* playlistA = harness.handler.createNewPlaylist(QStringLiteral("Playlist A"));
    auto* playlistB = harness.handler.createNewPlaylist(QStringLiteral("Playlist B"));
    auto* playlistC = harness.handler.createNewPlaylist(QStringLiteral("Playlist C"));
    QVERIFY(playlistA);
    QVERIFY(playlistB);
    QVERIFY(playlistC);

    VimHandler handler;
    handler.setPlaylistHandler(&harness.handler);

    FakePlaylistSelectionObserver observer;
    observer.setPlaylistHandler(&harness.handler);
    handler.setPlaylistSelectionObserver(&observer);

    FakeOrganiserWidget organiser;
    QStandardItemModel model;
    model.appendRow(makeOrganiserPlaylistItem(QStringLiteral("Playlist A"), playlistA));
    auto* group = makeGroupItem(QStringLiteral("Group"));
    group->appendRow(makeOrganiserPlaylistItem(QStringLiteral("Playlist C"), playlistC));
    group->appendRow(makeOrganiserPlaylistItem(QStringLiteral("Playlist B"), playlistB));
    model.appendRow(group);
    organiser.view()->setModel(&model);
    organiser.view()->expand(model.index(1, 0));
    organiser.window()->show();
    qApp->processEvents();

    observer.changeCurrentPlaylist(playlistA);
    handler.nextPlaylist();
    QCOMPARE(observer.currentPlaylist(), playlistC);

    handler.nextPlaylist();
    QCOMPARE(observer.currentPlaylist(), playlistB);

    handler.previousPlaylist();
    QCOMPARE(observer.currentPlaylist(), playlistC);
}

void TestVimHandlerViewContext::restoresSavedCursorWhenReturningToPlaylist()
{
    QStandardPaths::setTestModeEnabled(true);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("cursor_restore.ini"))};
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    const Fooyin::TrackList aTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a1.flac"), 0};
            track.setId(1);
            track.setTitle(QStringLiteral("A1"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a2.flac"), 0};
            track.setId(2);
            track.setTitle(QStringLiteral("A2"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a3.flac"), 0};
            track.setId(3);
            track.setTitle(QStringLiteral("A3"));
            track.generateHash();
            return track;
        }(),
    };
    const Fooyin::TrackList bTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/b1.flac"), 0};
            track.setId(4);
            track.setTitle(QStringLiteral("B1"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/b2.flac"), 0};
            track.setId(5);
            track.setTitle(QStringLiteral("B2"));
            track.generateHash();
            return track;
        }(),
    };

    auto* playlistA = harness.handler.createNewPlaylist(QStringLiteral("Playlist A"), aTracks);
    auto* playlistB = harness.handler.createNewPlaylist(QStringLiteral("Playlist B"), bTracks);
    QVERIFY(playlistA);
    QVERIFY(playlistB);

    VimHandler handler;
    handler.setPlaylistHandler(&harness.handler);
    FakePlaylistSelectionObserver observer;
    handler.setPlaylistSelectionObserver(&observer);
    qApp->installEventFilter(&handler);

    Fooyin::PlaylistView view;
    QStandardItemModel model;
    view.setModel(&model);

    syncPlaylistModel(model, playlistA);
    view.setCurrentIndex(model.index(2, 0));
    focusTree(&view);
    observer.changeCurrentPlaylist(playlistA);

    observer.changeCurrentPlaylist(playlistB);
    syncPlaylistModel(model, playlistB);
    pumpEvents();

    view.setCurrentIndex(model.index(1, 0));

    observer.changeCurrentPlaylist(playlistA);
    syncPlaylistModel(model, playlistA);
    pumpEvents();

    QCOMPARE(view.currentIndex().row(), 2);
}

void TestVimHandlerViewContext::clampsRestoredCursorWhenPlaylistShrinks()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("cursor_clamp.ini"))};
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    const Fooyin::TrackList aTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a1.flac"), 0};
            track.setId(11);
            track.setTitle(QStringLiteral("A1"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a2.flac"), 0};
            track.setId(12);
            track.setTitle(QStringLiteral("A2"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a3.flac"), 0};
            track.setId(13);
            track.setTitle(QStringLiteral("A3"));
            track.generateHash();
            return track;
        }(),
    };
    const Fooyin::TrackList bTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/b1.flac"), 0};
            track.setId(14);
            track.setTitle(QStringLiteral("B1"));
            track.generateHash();
            return track;
        }(),
    };

    auto* playlistA = harness.handler.createNewPlaylist(QStringLiteral("Playlist A"), aTracks);
    auto* playlistB = harness.handler.createNewPlaylist(QStringLiteral("Playlist B"), bTracks);
    QVERIFY(playlistA);
    QVERIFY(playlistB);

    VimHandler handler;
    handler.setPlaylistHandler(&harness.handler);
    FakePlaylistSelectionObserver observer;
    handler.setPlaylistSelectionObserver(&observer);

    Fooyin::PlaylistView view;
    QStandardItemModel model;
    view.setModel(&model);

    syncPlaylistModel(model, playlistA);
    view.setCurrentIndex(model.index(2, 0));
    focusTree(&view);
    observer.changeCurrentPlaylist(playlistA);

    observer.changeCurrentPlaylist(playlistB);
    syncPlaylistModel(model, playlistB);
    pumpEvents();

    harness.handler.replacePlaylistTracks(playlistA->id(), Fooyin::TrackList{aTracks.front()});

    observer.changeCurrentPlaylist(playlistA);
    syncPlaylistModel(model, playlistA);
    pumpEvents();

    QCOMPARE(playlistA->trackCount(), 1);
    QCOMPARE(view.currentIndex().row(), 0);
}

void TestVimHandlerViewContext::restoresVisualSelectionWhenReturningToPlaylist()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("cursor_visual_restore.ini"))};
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    const Fooyin::TrackList aTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a1.flac"), 0};
            track.setId(21);
            track.setTitle(QStringLiteral("A1"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a2.flac"), 0};
            track.setId(22);
            track.setTitle(QStringLiteral("A2"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a3.flac"), 0};
            track.setId(23);
            track.setTitle(QStringLiteral("A3"));
            track.generateHash();
            return track;
        }(),
    };
    const Fooyin::TrackList bTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/b1.flac"), 0};
            track.setId(24);
            track.setTitle(QStringLiteral("B1"));
            track.generateHash();
            return track;
        }(),
    };

    auto* playlistA = harness.handler.createNewPlaylist(QStringLiteral("Playlist A"), aTracks);
    auto* playlistB = harness.handler.createNewPlaylist(QStringLiteral("Playlist B"), bTracks);
    QVERIFY(playlistA);
    QVERIFY(playlistB);

    VimHandler handler;
    handler.setPlaylistHandler(&harness.handler);
    FakePlaylistSelectionObserver observer;
    handler.setPlaylistSelectionObserver(&observer);

    Fooyin::PlaylistView view;
    QStandardItemModel model;
    view.setModel(&model);

    syncPlaylistModel(model, playlistA);
    view.setCurrentIndex(model.index(1, 0));
    focusTree(&view);
    observer.changeCurrentPlaylist(playlistA);

    handler.enterVisual();
    handler.extendVisualCursor(+1);

    observer.changeCurrentPlaylist(playlistB);
    syncPlaylistModel(model, playlistB);
    pumpEvents();

    observer.changeCurrentPlaylist(playlistA);
    syncPlaylistModel(model, playlistA);
    pumpEvents();
    dispatchFocusIn(handler, &view);

    QCOMPARE(handler.mode(), VimHandler::Mode::Visual);
    QCOMPARE(view.currentIndex().row(), 2);
    QCOMPARE(view.selectionModel()->selectedRows().size(), 2);
    QVERIFY(view.selectionModel()->isRowSelected(1, QModelIndex{}));
    QVERIFY(view.selectionModel()->isRowSelected(2, QModelIndex{}));
}

void TestVimHandlerViewContext::restoresEmptyPlaylistToNormalMode()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("cursor_empty_restore.ini"))};
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    const Fooyin::TrackList aTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a1.flac"), 0};
            track.setId(31);
            track.setTitle(QStringLiteral("A1"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a2.flac"), 0};
            track.setId(32);
            track.setTitle(QStringLiteral("A2"));
            track.generateHash();
            return track;
        }(),
    };
    const Fooyin::TrackList bTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/b1.flac"), 0};
            track.setId(33);
            track.setTitle(QStringLiteral("B1"));
            track.generateHash();
            return track;
        }(),
    };

    auto* playlistA = harness.handler.createNewPlaylist(QStringLiteral("Playlist A"), aTracks);
    auto* playlistB = harness.handler.createNewPlaylist(QStringLiteral("Playlist B"), bTracks);
    QVERIFY(playlistA);
    QVERIFY(playlistB);

    VimHandler handler;
    handler.setPlaylistHandler(&harness.handler);
    FakePlaylistSelectionObserver observer;
    handler.setPlaylistSelectionObserver(&observer);

    Fooyin::PlaylistView view;
    QStandardItemModel model;
    view.setModel(&model);

    syncPlaylistModel(model, playlistA);
    view.setCurrentIndex(model.index(0, 0));
    focusTree(&view);
    observer.changeCurrentPlaylist(playlistA);

    handler.enterVisual();
    handler.extendVisualCursor(+1);

    observer.changeCurrentPlaylist(playlistB);
    syncPlaylistModel(model, playlistB);
    pumpEvents();

    harness.handler.replacePlaylistTracks(playlistA->id(), Fooyin::TrackList{});

    observer.changeCurrentPlaylist(playlistA);
    syncPlaylistModel(model, playlistA);
    pumpEvents();
    dispatchFocusIn(handler, &view);

    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(view.selectionModel()->selectedRows().size(), 0);
    QVERIFY(!view.currentIndex().isValid());
}

void TestVimHandlerViewContext::organiserSearchFindsVisibleChild()
{
    VimHandler handler;
    FakeOrganiserWidget organiser;
    QStandardItemModel model;

    auto* group = new QStandardItem(QStringLiteral("Group"));
    group->appendRow(new QStandardItem(QStringLiteral("Needle Child")));
    model.appendRow(new QStandardItem(QStringLiteral("Before")));
    model.appendRow(group);
    model.appendRow(new QStandardItem(QStringLiteral("After")));

    organiser.view()->setModel(&model);
    organiser.view()->expand(model.index(1, 0));
    organiser.view()->setCurrentIndex(model.index(0, 0));

    qApp->installEventFilter(&handler);
    focusTree(organiser.view());
    handler.enterSearch();
    qApp->processEvents();

    auto* editor = organiser.window()->findChild<QLineEdit*>();
    QVERIFY(editor);
    QTest::keyClicks(editor, QStringLiteral("Needle"));
    qApp->processEvents();

    QCOMPARE(organiser.view()->currentIndex().data().toString(), QStringLiteral("Needle Child"));

    QTest::keyClick(editor, Qt::Key_Escape);
    qApp->processEvents();
    qApp->removeEventFilter(&handler);
}

void TestVimHandlerViewContext::organiserSearchSkipsCollapsedChildren()
{
    VimHandler handler;
    FakeOrganiserWidget organiser;
    QStandardItemModel model;

    auto* group = new QStandardItem(QStringLiteral("Group"));
    group->appendRow(new QStandardItem(QStringLiteral("Hidden Needle")));
    model.appendRow(new QStandardItem(QStringLiteral("Start")));
    model.appendRow(group);
    model.appendRow(new QStandardItem(QStringLiteral("Tail")));

    organiser.view()->setModel(&model);
    organiser.view()->collapse(model.index(1, 0));
    organiser.view()->setCurrentIndex(model.index(0, 0));

    qApp->installEventFilter(&handler);
    focusTree(organiser.view());
    handler.enterSearch();
    qApp->processEvents();

    auto* editor = organiser.window()->findChild<QLineEdit*>();
    QVERIFY(editor);
    QTest::keyClicks(editor, QStringLiteral("Needle"));
    qApp->processEvents();

    QCOMPARE(organiser.view()->currentIndex().data().toString(), QStringLiteral("Start"));

    QTest::keyClick(editor, Qt::Key_Escape);
    qApp->processEvents();
    qApp->removeEventFilter(&handler);
}

void TestVimHandlerViewContext::organiserSearchNavigatesVisibleMatches()
{
    VimHandler handler;
    FakeOrganiserWidget organiser;
    QStandardItemModel model;

    model.appendRow(new QStandardItem(QStringLiteral("Needle One")));
    auto* group = new QStandardItem(QStringLiteral("Group"));
    group->appendRow(new QStandardItem(QStringLiteral("Needle Two")));
    model.appendRow(group);
    model.appendRow(new QStandardItem(QStringLiteral("Needle Three")));

    organiser.view()->setModel(&model);
    organiser.view()->expand(model.index(1, 0));
    organiser.view()->setCurrentIndex(model.index(0, 0));

    qApp->installEventFilter(&handler);
    focusTree(organiser.view());
    handler.enterSearch();
    qApp->processEvents();

    auto* editor = organiser.window()->findChild<QLineEdit*>();
    QVERIFY(editor);
    QTest::keyClicks(editor, QStringLiteral("Needle"));
    qApp->processEvents();
    QCOMPARE(organiser.view()->currentIndex().data().toString(), QStringLiteral("Needle One"));

    QTest::keyClick(editor, Qt::Key_Return);
    qApp->processEvents();
    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);

    handler.nextMatch();
    QCOMPARE(organiser.view()->currentIndex().data().toString(), QStringLiteral("Needle Two"));

    handler.nextMatch();
    QCOMPARE(organiser.view()->currentIndex().data().toString(), QStringLiteral("Needle Three"));

    handler.nextMatch();
    QCOMPARE(organiser.view()->currentIndex().data().toString(), QStringLiteral("Needle One"));

    handler.prevMatch();
    QCOMPARE(organiser.view()->currentIndex().data().toString(), QStringLiteral("Needle Three"));

    qApp->removeEventFilter(&handler);
}

void TestVimHandlerViewContext::organiserSearchKeepsEarlierRootCandidatesAfterJump()
{
    VimHandler handler;
    FakeOrganiserWidget organiser;
    QStandardItemModel model;

    model.appendRow(new QStandardItem(QStringLiteral("Library Selection")));
    model.appendRow(new QStandardItem(QStringLiteral("ByTempo")));
    model.appendRow(new QStandardItem(QStringLiteral("test3")));

    organiser.view()->setModel(&model);
    organiser.view()->setCurrentIndex(model.index(0, 0));

    qApp->installEventFilter(&handler);
    focusTree(organiser.view());
    handler.enterSearch();
    qApp->processEvents();

    auto* editor = organiser.window()->findChild<QLineEdit*>();
    QVERIFY(editor);
    QTest::keyClicks(editor, QStringLiteral("t"));
    qApp->processEvents();
    QCOMPARE(organiser.view()->currentIndex().data().toString(), QStringLiteral("Library Selection"));

    QTest::keyClicks(editor, QStringLiteral("e"));
    qApp->processEvents();
    QCOMPARE(organiser.view()->currentIndex().data().toString(), QStringLiteral("ByTempo"));

    editor->clear();
    QTest::keyClicks(editor, QStringLiteral("t"));
    qApp->processEvents();
    QCOMPARE(organiser.view()->currentIndex().data().toString(), QStringLiteral("Library Selection"));

    QTest::keyClick(editor, Qt::Key_Escape);
    qApp->processEvents();
    qApp->removeEventFilter(&handler);
}

void TestVimHandlerViewContext::playlistSearchNavigationTracksReorderedMatches()
{
    VimHandler handler;
    Fooyin::PlaylistView view;
    MoveAwareTreeModel model;

    model.appendRoot(QStringLiteral("Needle One"));
    model.appendRoot(QStringLiteral("Other"));
    model.appendRoot(QStringLiteral("Needle Two"));
    model.appendRoot(QStringLiteral("Needle Three"));
    view.setModel(&model);
    view.setCurrentIndex(model.index(0, 0));

    qApp->installEventFilter(&handler);
    focusTree(&view);
    handler.enterSearch();
    qApp->processEvents();

    auto* editor = view.window()->findChild<QLineEdit*>();
    QVERIFY(editor);
    QTest::keyClicks(editor, QStringLiteral("Needle"));
    qApp->processEvents();
    QCOMPARE(view.currentIndex().data().toString(), QStringLiteral("Needle One"));

    QTest::keyClick(editor, Qt::Key_Return);
    qApp->processEvents();
    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);

    QVERIFY(model.moveRow(QModelIndex(), 2, QModelIndex(), 0));
    view.setCurrentIndex(model.index(1, 0));
    QCOMPARE(view.currentIndex().data().toString(), QStringLiteral("Needle One"));

    handler.nextMatch();
    QCOMPARE(view.currentIndex().data().toString(), QStringLiteral("Needle Three"));

    view.setCurrentIndex(model.index(1, 0));
    handler.prevMatch();
    QCOMPARE(view.currentIndex().data().toString(), QStringLiteral("Needle Two"));

    qApp->removeEventFilter(&handler);
}

void TestVimHandlerViewContext::organiserSearchNavigationTracksReorderedMatches()
{
    VimHandler handler;
    FakeOrganiserWidget organiser;
    MoveAwareTreeModel model;

    model.appendRoot(QStringLiteral("Needle One"));
    const QModelIndex group = model.appendRoot(QStringLiteral("Group"));
    model.appendChild(group, QStringLiteral("Needle Two"));
    model.appendRoot(QStringLiteral("Needle Three"));

    organiser.view()->setModel(&model);
    organiser.view()->expand(model.index(1, 0));
    organiser.view()->setCurrentIndex(model.index(0, 0));

    qApp->installEventFilter(&handler);
    focusTree(organiser.view());
    handler.enterSearch();
    qApp->processEvents();

    auto* editor = organiser.window()->findChild<QLineEdit*>();
    QVERIFY(editor);
    QTest::keyClicks(editor, QStringLiteral("Needle"));
    qApp->processEvents();
    QCOMPARE(organiser.view()->currentIndex().data().toString(), QStringLiteral("Needle One"));

    QTest::keyClick(editor, Qt::Key_Return);
    qApp->processEvents();
    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);

    QVERIFY(model.moveRow(group, 0, QModelIndex(), 0));
    organiser.view()->setCurrentIndex(model.index(1, 0));
    QCOMPARE(organiser.view()->currentIndex().data().toString(), QStringLiteral("Needle One"));

    handler.nextMatch();
    QCOMPARE(organiser.view()->currentIndex().data().toString(), QStringLiteral("Needle Three"));

    organiser.view()->setCurrentIndex(model.index(1, 0));
    handler.prevMatch();
    QCOMPARE(organiser.view()->currentIndex().data().toString(), QStringLiteral("Needle Two"));

    qApp->removeEventFilter(&handler);
}

QTEST_MAIN(TestVimHandlerViewContext)
#include "vimhandlerviewcontexttest.moc"
