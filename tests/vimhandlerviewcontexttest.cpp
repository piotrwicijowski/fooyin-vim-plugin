#include "spatialnavigator.h"
#include "vimhandler.h"
#include "vimmarkvariableprovider.h"
#include "vimmotionssettings.h"
#include "vimplaylistpickerdialog.h"

#include <core/engine/audioloader.h>
#include <core/library/musiclibrary.h>
#include <core/playlist/playlisthandler.h>
#include <core/scripting/scriptparser.h>
#include <core/scripting/scriptregistry.h>
#include <core/track.h>
#include <gui/fywidget.h>
#include <gui/guiconstants.h>
#include <gui/playlist/currentplaylistcontroller.h>
#include <gui/playlist/playlistinteractor.h>
#include <gui/widgetcontainer.h>
#include <utils/actions/actionmanager.h>
#include <utils/database/dbconnectionhandler.h>
#include <utils/database/dbconnectionpool.h>
#include <utils/database/dbconnectionprovider.h>
#include <utils/database/dbquery.h>
#include <utils/settings/settingsmanager.h>

#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QDataStream>
#include <QDialog>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMimeData>
#include <QShortcut>
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

#include <latch>
#include <memory>
#include <thread>

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

constexpr int PlaylistItemDataRole = Qt::UserRole + 19;

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
                                                     "ForceSorted INTEGER DEFAULT 1, "
                                                     "ExtraProperties BLOB);"_s};
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
    Fooyin::PendingTrackCoverProvider* pendingTrackCoverProvider() const override
    {
        return nullptr;
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
    void updateTrackStats(const Fooyin::TrackList&, Fooyin::Track::Stats) override { }
    void updateTrackStats(const Fooyin::Track&, Fooyin::Track::Stats) override { }
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

class FakePlaylistWidget : public Fooyin::FyWidget
{
    Q_OBJECT

public:
    explicit FakePlaylistWidget(QWidget* parent = nullptr)
        : Fooyin::FyWidget(parent)
        , m_view(new Fooyin::PlaylistView(this))
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(m_view);
    }

    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("Playlist");
    }

    [[nodiscard]] QString layoutName() const override
    {
        return QStringLiteral("Playlist");
    }

    [[nodiscard]] Fooyin::PlaylistView* view() const
    {
        return m_view;
    }

private:
    Fooyin::PlaylistView* m_view;
};

class FakeSplitterWidget : public Fooyin::WidgetContainer
{
    Q_OBJECT

public:
    explicit FakeSplitterWidget(QWidget* parent = nullptr)
        : Fooyin::WidgetContainer(nullptr, nullptr, parent)
        , m_layout(new QHBoxLayout(this))
    {
        m_layout->setContentsMargins(0, 0, 0, 0);
    }

    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("Splitter (Left/Right)");
    }

    [[nodiscard]] QString layoutName() const override
    {
        return QStringLiteral("SplitterHorizontal");
    }

    [[nodiscard]] Qt::Orientation orientation() const override
    {
        return Qt::Horizontal;
    }

    [[nodiscard]] bool canAddWidget() const override
    {
        return true;
    }

    [[nodiscard]] bool canInsertWidget(int index) const override
    {
        return index >= 0 && index <= widgetCount();
    }

    [[nodiscard]] bool canMoveWidget(int index, int newIndex) const override
    {
        return index >= 0 && index < static_cast<int>(m_widgets.size()) && newIndex >= 0
            && newIndex <= static_cast<int>(m_widgets.size());
    }

    [[nodiscard]] int widgetIndex(const Fooyin::Id& id) const override
    {
        for(int index = 0; index < static_cast<int>(m_widgets.size()); ++index) {
            if(m_widgets[static_cast<size_t>(index)]->id() == id)
                return index;
        }

        return -1;
    }

    [[nodiscard]] Fooyin::FyWidget* widgetAtId(const Fooyin::Id& id) const override
    {
        const int index = widgetIndex(id);
        return index >= 0 ? m_widgets[static_cast<size_t>(index)] : nullptr;
    }

    [[nodiscard]] Fooyin::FyWidget* widgetAtIndex(int index) const override
    {
        return index >= 0 && index < static_cast<int>(m_widgets.size()) ? m_widgets[static_cast<size_t>(index)]
                                                                        : nullptr;
    }

    [[nodiscard]] int widgetCount() const override
    {
        return static_cast<int>(m_widgets.size());
    }

    [[nodiscard]] Fooyin::WidgetList widgets() const override
    {
        return m_widgets;
    }

    int addWidget(Fooyin::FyWidget* widget) override
    {
        const int index = static_cast<int>(m_widgets.size());
        insertWidget(index, widget);
        return index;
    }

    void insertWidget(int index, Fooyin::FyWidget* widget) override
    {
        if(!widget || index < 0 || index > static_cast<int>(m_widgets.size()))
            return;

        m_widgets.insert(m_widgets.begin() + index, widget);
        m_layout->insertWidget(index, widget);
    }

    void removeWidget(int index) override
    {
        if(index < 0 || index >= static_cast<int>(m_widgets.size()))
            return;

        m_widgets.erase(m_widgets.begin() + index);
    }

    Fooyin::FyWidget* takeWidget(int index) override
    {
        auto* widget = widgetAtIndex(index);
        if(!widget)
            return nullptr;
        m_widgets.erase(m_widgets.begin() + index);
        m_layout->removeWidget(widget);
        widget->hide();
        widget->setParent(nullptr);
        return widget;
    }

    Fooyin::FyWidget* exchangeWidgetImpl(int index, Fooyin::FyWidget* widget) override
    {
        if(!widget || index < 0 || index >= static_cast<int>(m_widgets.size()))
            return nullptr;

        auto* oldWidget = m_widgets[static_cast<size_t>(index)];
        m_layout->replaceWidget(oldWidget, widget);
        m_widgets[static_cast<size_t>(index)] = widget;
        return oldWidget;
    }

    void moveWidget(int index, int newIndex) override
    {
        if(!canMoveWidget(index, newIndex))
            return;

        auto* widget = m_widgets[static_cast<size_t>(index)];
        m_widgets.erase(m_widgets.begin() + index);
        m_widgets.insert(m_widgets.begin() + newIndex, widget);
        m_layout->insertWidget(newIndex, widget);
    }

private:
    QHBoxLayout* m_layout;
    Fooyin::WidgetList m_widgets;
};

class SearchDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SearchDialog(QWidget* parent = nullptr)
        : QDialog(parent)
        , m_searchBar(new QLineEdit(this))
        , m_view(new Fooyin::PlaylistView(this))
    {
        setWindowTitle(QStringLiteral("Search Library"));

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(m_searchBar);
        layout->addWidget(m_view);
    }

    [[nodiscard]] QLineEdit* searchBar() const
    {
        return m_searchBar;
    }

    [[nodiscard]] Fooyin::PlaylistView* view() const
    {
        return m_view;
    }

private:
    QLineEdit* m_searchBar;
    Fooyin::PlaylistView* m_view;
};

class FakeCurrentPlaylistController : public Fooyin::CurrentPlaylistController
{
    Q_OBJECT

public:
    explicit FakeCurrentPlaylistController(QObject* parent = nullptr)
        : Fooyin::CurrentPlaylistController(parent)
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

    void selectTracks(const Fooyin::TrackList&) override { }

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
    const Qt::KeyboardModifiers modifiers = ch.isUpper() ? Qt::ShiftModifier : Qt::NoModifier;
    QKeyEvent event(QEvent::KeyPress, Qt::Key_A + (ch.toLower().unicode() - u'a'), modifiers, QString(ch));
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

Fooyin::Track markTestTrack()
{
    Fooyin::Track track{u"/tmp/mark.flac"_s, 0};
    track.setId(61);
    track.setTitle(u"Mark test"_s);
    track.generateHash();
    return track;
}

struct MarkScriptHarness
{
    MarkScriptHarness()
        : settings{tempDir.filePath(u"mark_script.ini"_s)}
        , playlists{settings}
        , interactor{&playlists.handler, nullptr, nullptr, &settings}
        , provider{&handler}
    {
        handler.setPlaylistHandler(&playlists.handler);
        handler.setPlaylistInteractor(&interactor);
        observer.setPlaylistHandler(&playlists.handler);
        handler.setCurrentPlaylistController(&observer);
        view.setModel(&model);
        for(const auto& variable : provider.variables())
            registered = registry.registerVariable(variable) && registered;
        registry.finalise();
    }

    void select(Fooyin::Playlist* playlist, int row)
    {
        syncPlaylistModel(model, playlist);
        focusTree(&view);
        observer.changeCurrentPlaylist(playlist);
        view.setCurrentIndex(model.index(row, 0));
    }

    bool mark(QChar letter)
    {
        handler.beginSetMark();
        return dispatchKey(handler, &view, letter);
    }

    QTemporaryDir tempDir;
    Fooyin::SettingsManager settings;
    PlaylistHandlerHarness playlists;
    Fooyin::PlaylistInteractor interactor;
    Fooyin::VimMotions::VimHandler handler;
    FakeCurrentPlaylistController observer;
    QStandardItemModel model;
    Fooyin::PlaylistView view;
    Fooyin::VimMotions::VimMarkVariableProvider provider;
    Fooyin::ScriptRegistry registry;
    bool registered{true};
};

struct JumpHarness
{
    JumpHarness(int playlistCount = 4, int trackCount = 6)
        : settings{tempDir.filePath(u"jumps.ini"_s)}
        , playlists{settings}
        , backend{&settings, tempDir.filePath(u"jumps.ini"_s)}
    {
        Fooyin::VimMotions::VimMotionsSettings defaults{&settings};
        handler.setSettingsBackend(&backend);
        handler.setSettingsManager(&settings);
        handler.setPlaylistHandler(&playlists.handler);
        observer.setPlaylistHandler(&playlists.handler);
        handler.setCurrentPlaylistController(&observer);
        view.setModel(&model);
        for(int p = 0; p < playlistCount; ++p) {
            Fooyin::TrackList tracks;
            for(int t = 0; t < trackCount; ++t) {
                auto track = markTestTrack();
                track.setTitle(u"Track %1"_s.arg(t));
                tracks.push_back(track);
            }
            lists.push_back(playlists.handler.createNewPlaylist(u"Playlist %1"_s.arg(p), tracks));
        }
        QObject::connect(&observer, &Fooyin::CurrentPlaylistController::currentPlaylistChanged, &view,
                         [this](Fooyin::Playlist*, Fooyin::Playlist* current) {
                             if(!autoPopulate) {
                                 model.clear();
                                 return;
                             }
                             populate(current);
                         });
        focusTree(&view);
        observer.changeCurrentPlaylist(lists.front());
        pumpEvents();
        select(0);
    }

    void populate(Fooyin::Playlist* playlist)
    {
        model.clear();
        for(const auto& track : playlist->playlistTracks()) {
            auto* item = new QStandardItem(track.track.title());
            item->setData(QVariant::fromValue(track), PlaylistItemDataRole);
            model.appendRow({item, new QStandardItem(u"Column 2"_s)});
        }
    }

    void select(int row, int col = 0)
    {
        view.selectionModel()->setCurrentIndex(model.index(row, col),
                                               QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        dispatchFocusIn(handler, &view);
    }

    bool ctrl(int key)
    {
        QKeyEvent event{QEvent::KeyPress, key, Qt::ControlModifier};
        const bool handled = handler.eventFilter(&view, &event);
        pumpEvents();
        return handled;
    }

    QTemporaryDir tempDir;
    Fooyin::SettingsManager settings;
    PlaylistHandlerHarness playlists;
    FakeCurrentPlaylistController observer;
    QStandardItemModel model;
    Fooyin::PlaylistView view;
    Fooyin::VimMotions::VimMotionsBindingBackend backend;
    Fooyin::VimMotions::VimHandler handler;
    std::vector<Fooyin::Playlist*> lists;
    bool autoPopulate{true};
};

} // namespace

using namespace Fooyin::VimMotions;

class TestVimHandlerViewContext : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_configDir;

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_configDir.isValid());
        QStandardPaths::setTestModeEnabled(true);
        qputenv("XDG_CONFIG_HOME", m_configDir.path().toUtf8());
    }

    void classifiesNullView();
    void classifiesPlaylistView();
    void classifiesPlaylistOrganiserTree();
    void detectsSearchLibraryDialogView();
    void detectsSearchLibraryDialogLineEdit();
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
    void searchAndFilterKeepFocusBeforePlaylistSelection_data();
    void searchAndFilterKeepFocusBeforePlaylistSelection();
    void scopedBindingsPreferActiveViewOverGlobalFallback();
    void searchLibraryScopedBindingsOverridePlaylistViewAndFallbackToGlobal();
    void searchLibrarySearchFieldDispatchesScopedBindingAndFallsBackToTyping();
    void searchLibrarySearchFieldFallsBackToGlobalBinding();
    void searchLibraryCtrlJMovesFocusToResults();
    void searchLibraryCtrlKMovesFocusToSearchField();
    void searchLibraryLineEditAutoEntersInsertAndRestoresNormal();
    void searchLibraryLineEditRestoresVisualModeOnBlur();
    void searchLibraryResultsDoNotInheritPlaylistVisualMode();
    void searchLibraryVisualModeDoesNotLeakBackToPlaylist();
    void searchLibraryCloseRestoresPlaylistVisualModeFromSearchBox();
    void searchLibraryCloseRestoresPlaylistVisualModeAfterResults();
    void searchLibraryDialogDoesNotRestoreOrOverwritePlaylistState();
    void copyAfterCurrentPlayingUsesSearchLibrarySelectionTracks();
    void yankRowsUsesSearchLibrarySelectionTracks();
    void yankVisualSelectionUsesSearchLibrarySelectionTracks();
    void moveAfterCurrentPlayingIsNoOpForSearchLibrarySelection();
    void pasteTargetsObservedEmptySelectedPlaylist();
    void switchesToNextPlaylist();
    void switchesToPreviousPlaylist();
    void unseenPlaylistDefaultsToNormalWithoutSelection();
    void playlistSwitchingClampsAtEnds();
    void playlistSwitchingUsesCount();
    void playlistSwitchingFollowsOrganiserTreeOrder();
    void jumpsToGlobalMarkInAnotherPlaylist();
    void clearsStaleGlobalMarkWhenMarkedEntryIsDeleted();
    void exposesMarksForScriptContext();
    void refreshesMarksAfterReassignment();
    void refreshesTransferredMarks_data();
    void refreshesTransferredMarks();
    void evaluatesMarksWhileTheyChange();
    void restoresSavedCursorWhenReturningToPlaylist();
    void clampsRestoredCursorWhenPlaylistShrinks();
    void restoresVisualSelectionWhenReturningToPlaylist();
    void restoresVisualSelectionWhenRefocusingSamePlaylistViaOrganiser();
    void restoresNormalCursorWhenRefocusingAfterLeavingVisualViaOrganiser();
    void restoresVisualSelectionWhenRefocusingAfterSpatialFocusMoveViaOrganiser();
    void preservesVisualSelectionWhenSwitchingViaOrganiser();
    void restoresEmptyPlaylistToNormalMode();
    void restoresLatestCursorPositionAfterPlaylistRefocus();
    void focusNowPlayingRestoresPlayingRowInsteadOfSavedRow();
    void organiserSearchFindsVisibleChild();
    void organiserSearchSkipsCollapsedChildren();
    void organiserSearchNavigatesVisibleMatches();
    void organiserSearchKeepsEarlierRootCandidatesAfterJump();
    void playlistSearchAdvancesFromMatchInNonzeroColumn();
    void playlistSearchNavigationTracksReorderedMatches();
    void organiserSearchNavigationTracksReorderedMatches();
    void jumpHistoryRoundTripAndCounts();
    void jumpHistoryPlaylistSwitchesAreSeparateStops();
    void jumpHistoryIgnoresOrdinaryMovesAndPaneChanges();
    void jumpHistoryBranchesAndDeduplicates();
    void jumpHistoryTracksEntriesThroughReorderingAndDeletion();
    void jumpHistorySkipsDeletedPlaylistsAndEmptyPositions();
    void jumpHistorySearchCommitAndCancel();
    void jumpHistoryMarksAndVisualMode();
    void jumpHistoryWaitsForPopulationAndCancelsStaleRestores();
    void jumpHistoryRestoresGroupedTracksAndSkipsHiddenRows();
    void jumpHistoryIsBounded();
    void filterUsesGiInsteadOfCtrlI();
    void playlistPickerKeepsFocusAndCancels_data();
    void playlistPickerKeepsFocusAndCancels();
    void playlistPickerAcceptRestoresCursorAndHistory();
    void playlistPickerWaitsForPopulation();
    void playlistPickerRefreshesPublicPlaylists();
    void playlistPickerSameAndEmptyDestinations();
    void playlistPickerHandlesExternalSourceChange();
    void playlistPickerCleansUpWithHandler();
    void playlistPickerHandlesDestroyedWidgets();
};

void TestVimHandlerViewContext::playlistPickerKeepsFocusAndCancels_data()
{
    QTest::addColumn<bool>("visual");
    QTest::addColumn<bool>("organiser");
    QTest::newRow("normal-playlist") << false << false;
    QTest::newRow("visual-playlist") << true << false;
    QTest::newRow("normal-organiser") << false << true;
    QTest::newRow("visual-organiser") << true << true;
}

void TestVimHandlerViewContext::playlistPickerKeepsFocusAndCancels()
{
    QFETCH(bool, visual);
    QFETCH(bool, organiser);
    JumpHarness h;
    h.select(2, 1);
    if(visual) {
        h.handler.enterVisual();
        h.handler.extendVisualCursor(2);
    }
    const auto selection = h.view.selectionModel()->selection();
    const auto cursor    = h.view.currentIndex();
    const auto playing   = h.playlists.handler.activePlaylist();
    FakeOrganiserWidget organiserWidget;
    QStandardItemModel organiserModel;
    auto* group = makeGroupItem(u"Collapsed"_s);
    group->appendRow(makeOrganiserPlaylistItem(u"Hidden playlist"_s, h.lists[1]));
    organiserModel.appendRow(group);
    organiserWidget.view()->setModel(&organiserModel);
    QWidget* invoking = &h.view;
    if(organiser) {
        focusTree(organiserWidget.view());
        invoking = organiserWidget.view();
    }
    qApp->installEventFilter(&h.handler);
    QTest::keyClick(invoking, Qt::Key_Space);
    QTest::keyClick(invoking, Qt::Key_P);
    auto* picker = qobject_cast<VimPlaylistPickerDialog*>(QApplication::activeModalWidget());
    QVERIFY(picker);
    QPointer<VimPlaylistPickerDialog> guard = picker;
    auto* input                             = picker->findChild<QLineEdit*>();
    QTRY_COMPARE(QApplication::focusWidget(), input);
    int applicationCommands = 0;
    auto* nextAction        = new QAction(picker);
    nextAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_N));
    nextAction->setShortcutContext(Qt::ApplicationShortcut);
    picker->addAction(nextAction);
    connect(nextAction, &QAction::triggered, picker, [&]() { ++applicationCommands; });
    auto* previousShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_P), picker);
    previousShortcut->setContext(Qt::ApplicationShortcut);
    connect(previousShortcut, &QShortcut::activated, picker, [&]() { ++applicationCommands; });
    QCOMPARE(picker->selectedPlaylistId(), h.lists[0]->id());
    h.handler.findPlaylist(); // Repeated invocations reuse the same dialog.
    QCOMPARE(QApplication::activeModalWidget(), picker);
    QTest::keyClick(input, Qt::Key_N, Qt::ControlModifier);
    QCOMPARE(picker->selectedPlaylistId(), h.lists[1]->id());
    QCOMPARE(QApplication::focusWidget(), input);
    QTest::keyClick(input, Qt::Key_P, Qt::ControlModifier);
    QCOMPARE(picker->selectedPlaylistId(), h.lists[0]->id());
    QCOMPARE(QApplication::focusWidget(), input);
    QCOMPARE(applicationCommands, 0);
    QTest::keyClicks(input, "pl1");
    QCOMPARE(input->text(), u"pl1"_s);
    QCOMPARE(picker->selectedPlaylistId(), h.lists[1]->id());
    QCOMPARE(h.observer.currentPlaylist(), h.lists[0]);
    QCOMPARE(h.playlists.handler.activePlaylist(), playing);
    QCOMPARE(h.view.currentIndex(), cursor);
    QCOMPARE(h.view.selectionModel()->selection(), selection);
    QCOMPARE(h.handler.mode(), visual ? VimHandler::Mode::Visual : VimHandler::Mode::Normal);
    QTest::keyClick(input, Qt::Key_Escape);
    QTRY_COMPARE(QApplication::focusWidget(), invoking);
    QCOMPARE(h.view.currentIndex(), cursor);
    QCOMPARE(h.view.selectionModel()->selection(), selection);
    QCOMPARE(h.handler.mode(), visual ? VimHandler::Mode::Visual : VimHandler::Mode::Normal);
    QTRY_VERIFY(guard.isNull());
    qApp->removeEventFilter(&h.handler);
    h.view.activateWindow();
    focusTree(&h.view);
    h.ctrl(Qt::Key_O);
    QCOMPARE(h.observer.currentPlaylist(), h.lists[0]);
    QCOMPARE(h.view.currentIndex(), cursor);
}

void TestVimHandlerViewContext::playlistPickerAcceptRestoresCursorAndHistory()
{
    JumpHarness h;
    // Remember a Visual cursor on the destination, then return to the source.
    h.observer.changeCurrentPlaylist(h.lists[1]);
    pumpEvents();
    h.select(3, 1);
    h.handler.enterVisual();
    h.handler.extendVisualCursor(1);
    h.observer.changeCurrentPlaylist(h.lists[0]);
    pumpEvents();
    h.select(2, 1);
    h.handler.enterVisual();
    h.handler.extendVisualCursor(1);
    qApp->installEventFilter(&h.handler);
    h.handler.findPlaylist();
    auto* picker = qobject_cast<VimPlaylistPickerDialog*>(QApplication::activeModalWidget());
    QVERIFY(picker);
    auto* input = picker->findChild<QLineEdit*>();
    QTest::keyClicks(input, "pl1");
    QTest::keyClick(input, Qt::Key_Return);
    QTRY_COMPARE(QApplication::focusWidget(), &h.view);
    QTRY_COMPARE(h.view.currentIndex().row(), 4);
    QCOMPARE(h.view.currentIndex().column(), 1);
    QCOMPARE(h.handler.mode(), VimHandler::Mode::Normal);
    QCOMPARE(h.view.selectionModel()->selectedRows().size(), 1);
    QCOMPARE(h.observer.currentPlaylist(), h.lists[1]);
    qApp->removeEventFilter(&h.handler);
    QVERIFY(h.ctrl(Qt::Key_O));
    QCOMPARE(h.observer.currentPlaylist(), h.lists[0]);
    QCOMPARE(h.view.currentIndex().row(), 3);
    QCOMPARE(h.view.currentIndex().column(), 1);
    QVERIFY(h.ctrl(Qt::Key_I));
    QCOMPARE(h.observer.currentPlaylist(), h.lists[1]);
    QCOMPARE(h.view.currentIndex().row(), 4);
}

void TestVimHandlerViewContext::playlistPickerWaitsForPopulation()
{
    JumpHarness h;
    h.observer.changeCurrentPlaylist(h.lists[1]);
    pumpEvents();
    h.select(4);
    h.observer.changeCurrentPlaylist(h.lists[0]);
    pumpEvents();
    h.select(2);
    h.autoPopulate = false;
    h.handler.findPlaylist();
    auto* picker = qobject_cast<VimPlaylistPickerDialog*>(QApplication::activeModalWidget());
    QVERIFY(picker);
    auto* input = picker->findChild<QLineEdit*>();
    QTest::keyClicks(input, "pl1");
    QTest::keyClick(input, Qt::Key_Enter, Qt::KeypadModifier);
    pumpEvents();
    QCOMPARE(h.observer.currentPlaylist(), h.lists[1]);
    QVERIFY(!h.view.currentIndex().isValid());
    h.populate(h.lists[1]);
    QTRY_COMPARE(h.view.currentIndex().row(), 4);
    QCOMPARE(h.handler.mode(), VimHandler::Mode::Normal);
}

void TestVimHandlerViewContext::playlistPickerRefreshesPublicPlaylists()
{
    JumpHarness h;
    qApp->installEventFilter(&h.handler);
    h.handler.findPlaylist();
    auto* picker = qobject_cast<VimPlaylistPickerDialog*>(QApplication::activeModalWidget());
    QVERIFY(picker);
    auto* input = picker->findChild<QLineEdit*>();
    auto* list  = picker->findChild<QListWidget*>();
    QTRY_COMPARE(QApplication::focusWidget(), input);
    const int count = list->count();
    h.playlists.handler.createNewTempPlaylist(u"Temporary"_s);
    QCOMPARE(list->count(), count);
    auto* added = h.playlists.handler.createNewPlaylist(u"Jazz"_s);
    QCOMPARE(list->count(), count + 1);
    QTest::keyClicks(input, "jzz");
    QCOMPARE(picker->selectedPlaylistId(), added->id());
    h.playlists.handler.renamePlaylist(added->id(), u"Jazzy"_s);
    QCOMPARE(picker->selectedPlaylistId(), added->id());
    h.playlists.handler.changePlaylistIndex(added->id(), 0);
    QCOMPARE(picker->selectedPlaylistId(), added->id());
    h.playlists.handler.appendToPlaylist(added->id(), {markTestTrack()});
    QVERIFY(list->currentItem()->text().contains(u"1 track"_s));
    h.playlists.handler.removePlaylist(added->id());
    QVERIFY(!picker->selectedPlaylistId().isValid());
    QTest::keyClick(input, Qt::Key_Return);
    QVERIFY(picker->isVisible());
    QCOMPARE(h.observer.currentPlaylist(), h.lists[0]);
    QCOMPARE(input->text(), u"jzz"_s);
    QCOMPARE(QApplication::focusWidget(), input);
    picker->close();
    pumpEvents();
}

void TestVimHandlerViewContext::playlistPickerSameAndEmptyDestinations()
{
    JumpHarness h;
    h.select(3);
    h.handler.enterVisual();
    h.handler.extendVisualCursor(1);
    h.handler.findPlaylist();
    auto* picker = qobject_cast<VimPlaylistPickerDialog*>(QApplication::activeModalWidget());
    QVERIFY(picker);
    picker->accept();
    pumpEvents();
    QCOMPARE(h.view.currentIndex().row(), 4);
    QCOMPARE(h.handler.mode(), VimHandler::Mode::Normal);
    QCOMPARE(h.view.selectionModel()->selectedRows().size(), 1);
    h.ctrl(Qt::Key_O);
    QCOMPARE(h.view.currentIndex().row(), 4);

    auto* empty = h.playlists.handler.createNewPlaylist(u"Empty destination"_s);
    h.handler.findPlaylist();
    picker = qobject_cast<VimPlaylistPickerDialog*>(QApplication::activeModalWidget());
    QVERIFY(picker);
    QTest::keyClicks(picker->findChild<QLineEdit*>(), "empty");
    picker->accept();
    pumpEvents();
    QCOMPARE(h.observer.currentPlaylist(), empty);
    QCOMPARE(h.handler.mode(), VimHandler::Mode::Normal);
    QVERIFY(!h.view.currentIndex().isValid());
    h.handler.findPlaylist();
    picker = qobject_cast<VimPlaylistPickerDialog*>(QApplication::activeModalWidget());
    QVERIFY(picker);
    QTest::keyClicks(picker->findChild<QLineEdit*>(), "pl1");
    picker->accept();
    pumpEvents();
    QCOMPARE(h.observer.currentPlaylist(), h.lists[1]);
    // Empty source creates no new stop; the older valid departure remains.
    h.ctrl(Qt::Key_O);
    QCOMPARE(h.observer.currentPlaylist(), h.lists[0]);
    QCOMPARE(h.view.currentIndex().row(), 4);
}

void TestVimHandlerViewContext::playlistPickerHandlesExternalSourceChange()
{
    JumpHarness h;
    h.select(2);
    h.handler.findPlaylist();
    auto* picker = qobject_cast<VimPlaylistPickerDialog*>(QApplication::activeModalWidget());
    QVERIFY(picker);
    h.observer.changeCurrentPlaylist(h.lists[2]);
    pumpEvents();
    auto* input = picker->findChild<QLineEdit*>();
    QTest::keyClicks(input, "pl1");
    picker->accept();
    pumpEvents();
    QCOMPARE(h.observer.currentPlaylist(), h.lists[1]);
    h.ctrl(Qt::Key_O);
    QCOMPARE(h.observer.currentPlaylist(), h.lists[1]);

    h.select(1);
    h.handler.findPlaylist();
    picker = qobject_cast<VimPlaylistPickerDialog*>(QApplication::activeModalWidget());
    QVERIFY(picker);
    // Simulate playback-following moving the source cursor while the dialog is open.
    h.select(3);
    QTest::keyClicks(picker->findChild<QLineEdit*>(), "pl2");
    picker->accept();
    pumpEvents();
    h.ctrl(Qt::Key_O);
    QCOMPARE(h.observer.currentPlaylist(), h.lists[1]);
    QCOMPARE(h.view.currentIndex().row(), 3);
}

void TestVimHandlerViewContext::playlistPickerCleansUpWithHandler()
{
    JumpHarness h;
    auto handler = std::make_unique<VimHandler>();
    handler->setPlaylistHandler(&h.playlists.handler);
    handler->setCurrentPlaylistController(&h.observer);
    handler->findPlaylist();
    QPointer<VimPlaylistPickerDialog> picker
        = qobject_cast<VimPlaylistPickerDialog*>(QApplication::activeModalWidget());
    QVERIFY(picker);
    handler.reset();
    QVERIFY(picker.isNull());
    VimHandler missingServices;
    missingServices.findPlaylist();
    QVERIFY(!qobject_cast<VimPlaylistPickerDialog*>(QApplication::activeModalWidget()));
}

void TestVimHandlerViewContext::playlistPickerHandlesDestroyedWidgets()
{
    JumpHarness h;
    auto organiser = std::make_unique<FakeOrganiserWidget>();
    focusTree(organiser->view());
    h.handler.findPlaylist();
    QPointer<VimPlaylistPickerDialog> picker
        = qobject_cast<VimPlaylistPickerDialog*>(QApplication::activeModalWidget());
    QVERIFY(picker);
    organiser.reset(); // Destroying the invoking window also destroys its dialog.
    QVERIFY(picker.isNull());
    h.view.activateWindow();
    focusTree(&h.view);
    h.handler.findPlaylist();
    picker = qobject_cast<VimPlaylistPickerDialog*>(QApplication::activeModalWidget());
    QVERIFY(picker);
    picker->reject();
    pumpEvents();

    // The remembered track view may disappear while the picker is open. Use a
    // different invoking window so that the picker itself survives its deletion.
    auto destination = std::make_unique<Fooyin::PlaylistView>();
    destination->setModel(&h.model);
    focusTree(destination.get());
    dispatchFocusIn(h.handler, destination.get());
    organiser = std::make_unique<FakeOrganiserWidget>();
    focusTree(organiser->view());
    h.handler.findPlaylist();
    picker = qobject_cast<VimPlaylistPickerDialog*>(QApplication::activeModalWidget());
    QVERIFY(picker);
    destination.reset();
    QTest::keyClicks(picker->findChild<QLineEdit*>(), "pl1");
    picker->accept();
    pumpEvents();
    QCOMPARE(h.observer.currentPlaylist(), h.lists[1]);
    QTRY_COMPARE(QApplication::focusWidget(), &h.view);
}

void TestVimHandlerViewContext::jumpHistoryRoundTripAndCounts()
{
    JumpHarness h;
    QVERIFY(h.playlists.dbInitialised);
    h.select(1, 1);
    QVERIFY(dispatchKey(h.handler, &h.view, u'G'));
    QCOMPARE(h.view.currentIndex().row(), 5);
    QVERIFY(dispatchKey(h.handler, &h.view, u'g'));
    QVERIFY(dispatchKey(h.handler, &h.view, u'g'));
    QCOMPARE(h.view.currentIndex().row(), 0);
    QKeyEvent count{QEvent::KeyPress, Qt::Key_2, Qt::NoModifier, u"2"_s};
    QVERIFY(h.handler.eventFilter(&h.view, &count));
    QVERIFY(h.ctrl(Qt::Key_O));
    QCOMPARE(h.view.currentIndex().row(), 1);
    QCOMPARE(h.view.currentIndex().column(), 1);
    QVERIFY(h.handler.eventFilter(&h.view, &count));
    QVERIFY(h.ctrl(Qt::Key_I));
    QCOMPARE(h.view.currentIndex().row(), 0);
    QCOMPARE(h.view.currentIndex().column(), 0); // G's tree motion selects column zero.
    QVERIFY(h.ctrl(Qt::Key_I));
    QCOMPARE(h.view.currentIndex().row(), 0);
}

void TestVimHandlerViewContext::jumpHistoryPlaylistSwitchesAreSeparateStops()
{
    JumpHarness h;
    h.select(1);
    for(int p = 1; p < 4; ++p) {
        h.handler.nextPlaylist();
        pumpEvents();
        h.select(p + 1);
    }
    QCOMPARE(h.observer.currentPlaylist(), h.lists[3]);
    for(int p = 2; p >= 0; --p) {
        QVERIFY(h.ctrl(Qt::Key_O));
        QCOMPARE(h.observer.currentPlaylist(), h.lists[p]);
        QCOMPARE(h.view.currentIndex().row(), p + 1);
    }
    for(int p = 1; p < 4; ++p) {
        QVERIFY(h.ctrl(Qt::Key_I));
        QCOMPARE(h.observer.currentPlaylist(), h.lists[p]);
        QCOMPARE(h.view.currentIndex().row(), p + 1);
    }
    h.handler.previousPlaylist();
    pumpEvents();
    h.select(0);
    QVERIFY(h.ctrl(Qt::Key_O));
    QCOMPARE(h.observer.currentPlaylist(), h.lists[3]);
    QCOMPARE(h.view.currentIndex().row(), 4);
}

void TestVimHandlerViewContext::jumpHistoryIgnoresOrdinaryMovesAndPaneChanges()
{
    JumpHarness h;
    h.handler.moveCursor(2);
    h.handler.moveCursorHalfPage(+1);
    const int row = h.view.currentIndex().row();
    QTreeView library;
    QStandardItemModel libraryModel(4, 1);
    library.setModel(&libraryModel);
    focusTree(&library);
    h.handler.jumpToLast();
    h.view.activateWindow();
    focusTree(&h.view);
    QVERIFY(h.ctrl(Qt::Key_O));
    QCOMPARE(h.view.currentIndex().row(), row);

    h.select(1);
    h.handler.jumpToLast();
    h.handler.moveCursor(-2);
    QVERIFY(h.ctrl(Qt::Key_O));
    QCOMPARE(h.view.currentIndex().row(), 1);
    QVERIFY(h.ctrl(Qt::Key_I));
    QCOMPARE(h.view.currentIndex().row(), 3);
}

void TestVimHandlerViewContext::jumpHistoryBranchesAndDeduplicates()
{
    JumpHarness h;
    h.handler.jumpToRow(2);
    h.handler.jumpToRow(4);
    QVERIFY(h.ctrl(Qt::Key_O));
    QCOMPARE(h.view.currentIndex().row(), 2);
    h.handler.jumpToRow(3);
    QVERIFY(h.ctrl(Qt::Key_I));
    QCOMPARE(h.view.currentIndex().row(), 3); // The old forward branch (row 4) is gone.
    h.handler.jumpToRow(2);
    h.handler.jumpToRow(2); // No-op jumps do not record another stop.
    QVERIFY(h.ctrl(Qt::Key_O));
    QCOMPARE(h.view.currentIndex().row(), 3);
    QVERIFY(h.ctrl(Qt::Key_O));
    QCOMPARE(h.view.currentIndex().row(), 0); // Row 2 is deduplicated.
}

void TestVimHandlerViewContext::jumpHistoryTracksEntriesThroughReorderingAndDeletion()
{
    JumpHarness h;
    h.select(1, 1);
    const auto remembered = h.lists[0]->playlistTrack(1)->entryId;
    h.handler.jumpToRow(5);
    auto tracks = h.lists[0]->playlistTracks();
    std::swap(tracks[1], tracks[3]);
    h.playlists.handler.replacePlaylistTracks(h.lists[0]->id(), tracks);
    h.populate(h.lists[0]);
    h.select(5);
    QVERIFY(h.ctrl(Qt::Key_O));
    QCOMPARE(h.view.currentIndex().row(), 3);
    QCOMPARE(h.view.currentIndex().column(), 1);
    QCOMPARE(h.view.currentIndex().siblingAtColumn(0).data(PlaylistItemDataRole).value<Fooyin::PlaylistTrack>().entryId,
             remembered);
    h.handler.jumpToRow(2);
    tracks.erase(tracks.begin() + 3);
    h.playlists.handler.replacePlaylistTracks(h.lists[0]->id(), tracks);
    h.populate(h.lists[0]);
    h.select(2);
    QVERIFY(h.ctrl(Qt::Key_O));
    QCOMPARE(h.view.currentIndex().row(), 2); // Deleted occurrence is skipped, not replaced by its old row.
}

void TestVimHandlerViewContext::jumpHistorySkipsDeletedPlaylistsAndEmptyPositions()
{
    JumpHarness h;
    h.select(1);
    h.handler.nextPlaylist();
    pumpEvents();
    h.select(2);
    h.handler.nextPlaylist();
    pumpEvents();
    h.select(3);
    h.playlists.handler.removePlaylist(h.lists[1]->id());
    QVERIFY(h.ctrl(Qt::Key_O));
    QCOMPARE(h.observer.currentPlaylist(), h.lists[0]);
    QCOMPARE(h.view.currentIndex().row(), 1);

    h.playlists.handler.replacePlaylistTracks(h.lists[2]->id(), Fooyin::PlaylistTrackList{});
    h.handler.nextPlaylist();
    pumpEvents();
    QVERIFY(!h.view.currentIndex().isValid());
    h.handler.nextPlaylist();
    pumpEvents();
    h.select(0);
    QVERIFY(h.ctrl(Qt::Key_O));
    QCOMPARE(h.observer.currentPlaylist(), h.lists[0]);
    QCOMPARE(h.view.currentIndex().row(), 1);
}

void TestVimHandlerViewContext::jumpHistorySearchCommitAndCancel()
{
    JumpHarness h;
    h.select(1);
    h.handler.enterSearch();
    auto* edit = h.view.findChild<QLineEdit*>();
    QVERIFY(edit);
    edit->setText(u"Track 2"_s);
    QCOMPARE(h.view.currentIndex().row(), 2);
    edit->setText(u"Track 4"_s);
    QCOMPARE(h.view.currentIndex().row(), 4);
    QTest::keyClick(edit, Qt::Key_Return);
    QVERIFY(h.ctrl(Qt::Key_O));
    QCOMPARE(h.view.currentIndex().row(), 1);
    QVERIFY(h.ctrl(Qt::Key_I));
    QCOMPARE(h.view.currentIndex().row(), 4);
    h.handler.enterSearch();
    edit->setText(u"Track 0"_s);
    QTest::keyClick(edit, Qt::Key_Escape);
    QCOMPARE(h.view.currentIndex().row(), 4);
    QVERIFY(h.ctrl(Qt::Key_O));
    QCOMPARE(h.view.currentIndex().row(), 1);

    h.handler.enterSearch();
    edit->setText(u"Track"_s);
    QTest::keyClick(edit, Qt::Key_Return);
    h.handler.nextMatch();
    QCOMPARE(h.view.currentIndex().row(), 2);
    h.handler.prevMatch();
    QCOMPARE(h.view.currentIndex().row(), 1);
    QVERIFY(h.ctrl(Qt::Key_O));
    QCOMPARE(h.view.currentIndex().row(), 2);
}

void TestVimHandlerViewContext::jumpHistoryMarksAndVisualMode()
{
    JumpHarness h;
    h.select(1);
    h.handler.beginSetMark();
    QVERIFY(dispatchKey(h.handler, &h.view, u'a'));
    h.handler.beginSetMark();
    QVERIFY(dispatchKey(h.handler, &h.view, u'A'));
    h.select(4);
    h.handler.beginJumpToMark();
    QVERIFY(dispatchKey(h.handler, &h.view, u'a'));
    QVERIFY(h.ctrl(Qt::Key_O));
    QCOMPARE(h.view.currentIndex().row(), 4);
    h.handler.nextPlaylist();
    pumpEvents();
    h.select(2);
    h.handler.beginJumpToMark();
    QVERIFY(dispatchKey(h.handler, &h.view, u'A'));
    pumpEvents();
    QCOMPARE(h.observer.currentPlaylist(), h.lists[0]);
    QCOMPARE(h.view.currentIndex().row(), 1);
    QVERIFY(h.ctrl(Qt::Key_O));
    QCOMPARE(h.observer.currentPlaylist(), h.lists[1]);
    QCOMPARE(h.view.currentIndex().row(), 2);
    h.handler.enterVisual();
    h.handler.extendVisualToEnd();
    QVERIFY(h.ctrl(Qt::Key_O));
    QCOMPARE(h.view.currentIndex().row(), 2);
    QCOMPARE(h.handler.mode(), VimHandler::Mode::Normal);
    QCOMPARE(h.view.selectionModel()->selectedRows().size(), 1);
}

void TestVimHandlerViewContext::jumpHistoryWaitsForPopulationAndCancelsStaleRestores()
{
    JumpHarness h;
    h.select(1);
    h.handler.nextPlaylist();
    pumpEvents();
    h.select(4);
    h.autoPopulate = false;
    QVERIFY(h.ctrl(Qt::Key_O));
    QCOMPARE(h.observer.currentPlaylist(), h.lists[0]);
    h.model.clear();
    pumpEvents();
    h.populate(h.lists[0]);
    pumpEvents();
    QCOMPARE(h.view.currentIndex().row(), 1);
    QVERIFY(h.ctrl(Qt::Key_I)); // Restore is pending for playlist 2.
    QVERIFY(h.ctrl(Qt::Key_O)); // Supersede it before its model is populated.
    h.populate(h.lists[0]);
    pumpEvents();
    QCOMPARE(h.observer.currentPlaylist(), h.lists[0]);
    QCOMPARE(h.view.currentIndex().row(), 1);
    h.handler.moveCursor(+1);
    h.model.setData(h.model.index(0, 0), u"Changed"_s);
    pumpEvents();
    QCOMPARE(h.view.currentIndex().row(), 2);
}

void TestVimHandlerViewContext::jumpHistoryRestoresGroupedTracksAndSkipsHiddenRows()
{
    JumpHarness h;
    h.select(1);
    h.handler.jumpToRow(4);
    auto* group = new QStandardItem(u"Group"_s);
    group->appendRow(h.model.takeRow(1));
    h.model.appendRow(group);
    h.select(3); // The original row 4 moved up when row 1 was grouped.
    QVERIFY(h.ctrl(Qt::Key_O));
    QCOMPARE(h.view.currentIndex().parent(), group->index());
    QCOMPARE(h.view.currentIndex().row(), 0);
    QVERIFY(h.view.isExpanded(group->index()));
    QVERIFY(h.ctrl(Qt::Key_I));
    h.view.setRowHidden(0, group->index(), true);
    QVERIFY(h.ctrl(Qt::Key_O));
    QCOMPARE(h.view.currentIndex().row(), 3);
    QVERIFY(!h.view.currentIndex().parent().isValid());
}

void TestVimHandlerViewContext::jumpHistoryIsBounded()
{
    JumpHarness h{1, 110};
    for(int row = 1; row < 110; ++row)
        h.handler.jumpToRow(row);
    QKeyEvent count{QEvent::KeyPress, Qt::Key_9, Qt::NoModifier, u"9"_s};
    for(int i = 0; i < 3; ++i)
        QVERIFY(h.handler.eventFilter(&h.view, &count));
    QVERIFY(h.ctrl(Qt::Key_O));
    QCOMPARE(h.view.currentIndex().row(), 10); // 100 positions, including the return position.
}

void TestVimHandlerViewContext::filterUsesGiInsteadOfCtrlI()
{
    JumpHarness h;
    FakeFyWidget pane;
    QStandardItemModel model(3, 1);
    pane.view()->setModel(&model);
    focusTree(pane.view());
    QVERIFY(dispatchKey(h.handler, pane.view(), u'g'));
    QVERIFY(dispatchKey(h.handler, pane.view(), u'i'));
    QCOMPARE(h.handler.mode(), VimHandler::Mode::Filter);
    auto* edit = pane.findChild<QLineEdit*>();
    QVERIFY(edit);
    QTest::keyClick(edit, Qt::Key_Escape);
    QCOMPARE(h.handler.mode(), VimHandler::Mode::Normal);
    QKeyEvent forward{QEvent::KeyPress, Qt::Key_I, Qt::ControlModifier};
    QVERIFY(h.handler.eventFilter(pane.view(), &forward));
    QCOMPARE(h.handler.mode(), VimHandler::Mode::Normal);
}

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

void TestVimHandlerViewContext::detectsSearchLibraryDialogView()
{
    VimHandler handler;
    SearchDialog dialog;

    QVERIFY(handler.isSearchLibraryDialogWidget(dialog.view()));
    QCOMPARE(handler.viewContext(dialog.view()), VimHandler::ViewContext::PlaylistView);
}

void TestVimHandlerViewContext::detectsSearchLibraryDialogLineEdit()
{
    VimHandler handler;
    SearchDialog dialog;

    QVERIFY(handler.isSearchLibraryDialogWidget(dialog.searchBar()));
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

void TestVimHandlerViewContext::searchAndFilterKeepFocusBeforePlaylistSelection_data()
{
    QTest::addColumn<bool>("filter");
    QTest::newRow("search") << false;
    QTest::newRow("filter") << true;
}

void TestVimHandlerViewContext::searchAndFilterKeepFocusBeforePlaylistSelection()
{
    QFETCH(bool, filter);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("startup_focus.ini"))};
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    VimHandler handler;
    handler.setPlaylistHandler(&harness.handler);
    FakeCurrentPlaylistController observer;
    observer.setPlaylistHandler(&harness.handler);
    handler.setCurrentPlaylistController(&observer);

    FakePlaylistWidget playlistWidget;
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("Track")));
    playlistWidget.view()->setModel(&model);
    playlistWidget.view()->setCurrentIndex(model.index(0, 0));

    qApp->installEventFilter(&handler);
    focusTree(playlistWidget.view());
    // The GUI has not reported an initial selected playlist yet.
    if(filter)
        handler.enterFilter();
    else
        handler.enterSearch();
    qApp->processEvents();

    auto* editor = playlistWidget.window()->findChild<QLineEdit*>();
    QVERIFY(editor);
    QVERIFY(editor->isVisible());
    QVERIFY(editor->hasFocus());

    playlistWidget.view()->setFocus();
    QVERIFY(playlistWidget.view()->hasFocus());
    QTest::mouseClick(editor, Qt::LeftButton);
    QVERIFY(editor->hasFocus());

    QTest::keyClicks(editor, QStringLiteral("foo"));
    QCOMPARE(editor->text(), QStringLiteral("foo"));

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

    VimMotionsBindingBackend backend{&settings, settingsPath};
    VimHandler handler;
    handler.setSettingsBackend(&backend);
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

void TestVimHandlerViewContext::searchLibraryScopedBindingsOverridePlaylistViewAndFallbackToGlobal()
{
    const QString settingsPath = QDir::tempPath() + QStringLiteral("/fooyin_vim_search_library_scope.ini");
    QFile::remove(settingsPath);

    Fooyin::SettingsManager settings{settingsPath};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)
    settings.set(QStringLiteral("VimMotions/UseDefaultBindings"), false);
    settings.fileSet(QStringLiteral("VimMotions/Bindings/Global/Normal/j"), QStringLiteral("enterVisual"));
    settings.fileSet(QStringLiteral("VimMotions/Bindings/PlaylistView/Normal/j"), QStringLiteral("moveCursor:+1"));
    settings.fileSet(QStringLiteral("VimMotions/Bindings/SearchLibraryDialog/Normal/j"),
                     QStringLiteral("clearPending"));
    settings.fileSet(QStringLiteral("VimMotions/Bindings/Global/Normal/k"), QStringLiteral("enterVisual"));
    settings.fileSet(QStringLiteral("VimMotions/Bindings/PlaylistView/Normal/k"), QStringLiteral("moveCursor:-1"));

    VimMotionsBindingBackend backend{&settings, settingsPath};
    VimHandler handler;
    handler.setSettingsBackend(&backend);
    handler.setSettingsManager(&settings);

    SearchDialog dialog;
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("One")));
    model.appendRow(new QStandardItem(QStringLiteral("Two")));
    dialog.view()->setModel(&model);
    dialog.view()->setCurrentIndex(model.index(0, 0));
    focusTree(dialog.view());

    QVERIFY(dispatchKey(handler, dialog.view(), u'j'));
    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);
    QCOMPARE(dialog.view()->currentIndex().row(), 0);

    QVERIFY(dispatchKey(handler, dialog.view(), u'k'));
    QCOMPARE(handler.mode(), VimHandler::Mode::Visual);
    QCOMPARE(dialog.view()->currentIndex().row(), 0);
}

void TestVimHandlerViewContext::searchLibrarySearchFieldDispatchesScopedBindingAndFallsBackToTyping()
{
    const QString settingsPath = QDir::tempPath() + QStringLiteral("/fooyin_vim_search_library_field_scope.ini");
    QFile::remove(settingsPath);

    Fooyin::SettingsManager settings{settingsPath};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)
    settings.set(QStringLiteral("VimMotions/UseDefaultBindings"), false);
    settings.fileSet(QStringLiteral("VimMotions/Bindings/SearchLibraryDialog/Insert/Ctrl+J"),
                     QStringLiteral("spatialMoveFocus:down"));

    VimMotionsBindingBackend backend{&settings, settingsPath};
    VimHandler handler;
    handler.setSettingsBackend(&backend);
    handler.setSettingsManager(&settings);

    SearchDialog dialog;
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("One")));
    dialog.view()->setModel(&model);
    dialog.show();
    dialog.searchBar()->setFocus();
    pumpEvents();
    QCOMPARE(handler.mode(), VimHandler::Mode::Insert);

    qApp->installEventFilter(&handler);

    QTest::keyClick(dialog.searchBar(), Qt::Key_J, Qt::ControlModifier);
    pumpEvents();
    QVERIFY(dialog.view()->hasFocus());
    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);
    QCOMPARE(dialog.searchBar()->text(), QString());

    dialog.searchBar()->setFocus();
    pumpEvents();
    dialog.searchBar()->clear();
    QCOMPARE(handler.mode(), VimHandler::Mode::Insert);
    QTest::keyClick(dialog.searchBar(), Qt::Key_X);
    pumpEvents();
    QCOMPARE(handler.mode(), VimHandler::Mode::Insert);
    QCOMPARE(dialog.searchBar()->text(), QStringLiteral("x"));

    qApp->removeEventFilter(&handler);
}

void TestVimHandlerViewContext::searchLibrarySearchFieldFallsBackToGlobalBinding()
{
    const QString settingsPath = QDir::tempPath() + QStringLiteral("/fooyin_vim_search_library_field_global.ini");
    QFile::remove(settingsPath);

    Fooyin::SettingsManager settings{settingsPath};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)
    settings.set(QStringLiteral("VimMotions/UseDefaultBindings"), false);
    settings.fileSet(QStringLiteral("VimMotions/Bindings/Global/Insert/<Esc>"), QStringLiteral("leaveInsertMode"));

    VimMotionsBindingBackend backend{&settings, settingsPath};
    VimHandler handler;
    handler.setSettingsBackend(&backend);
    handler.setSettingsManager(&settings);

    SearchDialog dialog;
    dialog.show();
    dialog.searchBar()->setFocus();
    pumpEvents();
    QCOMPARE(handler.mode(), VimHandler::Mode::Insert);

    qApp->installEventFilter(&handler);

    QTest::keyClick(dialog.searchBar(), Qt::Key_Escape);
    pumpEvents();
    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);
    QCOMPARE(dialog.searchBar()->text(), QString());

    qApp->removeEventFilter(&handler);
}

void TestVimHandlerViewContext::searchLibraryCtrlJMovesFocusToResults()
{
    const QString settingsPath = QDir::tempPath() + QStringLiteral("/fooyin_vim_search_library_focus_down.ini");
    QFile::remove(settingsPath);

    Fooyin::SettingsManager settings{settingsPath};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)
    settings.set(QStringLiteral("VimMotions/UseDefaultBindings"), false);
    settings.fileSet(QStringLiteral("VimMotions/Bindings/SearchLibraryDialog/Insert/Ctrl+J"),
                     QStringLiteral("spatialMoveFocus:down"));

    VimMotionsBindingBackend backend{&settings, settingsPath};
    VimHandler handler;
    handler.setSettingsBackend(&backend);
    handler.setSettingsManager(&settings);

    SearchDialog dialog;
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("One")));
    dialog.view()->setModel(&model);
    dialog.show();
    dialog.searchBar()->setFocus();
    pumpEvents();

    qApp->installEventFilter(&handler);
    QTest::keyClick(dialog.searchBar(), Qt::Key_J, Qt::ControlModifier);
    pumpEvents();

    QVERIFY(dialog.view()->hasFocus());
    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);
    qApp->removeEventFilter(&handler);
}

void TestVimHandlerViewContext::searchLibraryCtrlKMovesFocusToSearchField()
{
    const QString settingsPath = QDir::tempPath() + QStringLiteral("/fooyin_vim_search_library_focus_up.ini");
    QFile::remove(settingsPath);

    Fooyin::SettingsManager settings{settingsPath};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)
    settings.set(QStringLiteral("VimMotions/UseDefaultBindings"), false);
    settings.fileSet(QStringLiteral("VimMotions/Bindings/SearchLibraryDialog/Normal/Ctrl+K"),
                     QStringLiteral("spatialMoveFocus:up"));

    VimMotionsBindingBackend backend{&settings, settingsPath};
    VimHandler handler;
    handler.setSettingsBackend(&backend);
    handler.setSettingsManager(&settings);

    SearchDialog dialog;
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("One")));
    dialog.view()->setModel(&model);
    dialog.show();
    focusTree(dialog.view());

    qApp->installEventFilter(&handler);
    QTest::keyClick(dialog.view(), Qt::Key_K, Qt::ControlModifier);
    pumpEvents();

    QVERIFY(dialog.searchBar()->hasFocus());
    QCOMPARE(handler.mode(), VimHandler::Mode::Insert);
    qApp->removeEventFilter(&handler);
}

void TestVimHandlerViewContext::searchLibraryLineEditAutoEntersInsertAndRestoresNormal()
{
    VimHandler handler;

    SearchDialog dialog;
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("One")));
    dialog.view()->setModel(&model);
    dialog.show();

    focusTree(dialog.view());
    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);

    dialog.searchBar()->setFocus();
    pumpEvents();
    QCOMPARE(handler.mode(), VimHandler::Mode::Insert);

    dialog.view()->setFocus();
    pumpEvents();
    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);
}

void TestVimHandlerViewContext::searchLibraryLineEditRestoresVisualModeOnBlur()
{
    VimHandler handler;

    SearchDialog dialog;
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("One")));
    model.appendRow(new QStandardItem(QStringLiteral("Two")));
    dialog.view()->setModel(&model);
    dialog.show();

    focusTree(dialog.view());
    handler.enterVisual();
    QCOMPARE(handler.mode(), VimHandler::Mode::Visual);

    dialog.searchBar()->setFocus();
    pumpEvents();
    QCOMPARE(handler.mode(), VimHandler::Mode::Insert);

    dialog.view()->setFocus();
    pumpEvents();
    QCOMPARE(handler.mode(), VimHandler::Mode::Visual);
}

void TestVimHandlerViewContext::searchLibraryResultsDoNotInheritPlaylistVisualMode()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("search_library_no_playlist_visual.ini"))};
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    const Fooyin::TrackList aTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a1.flac"), 0};
            track.setId(111);
            track.setTitle(QStringLiteral("A1"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a2.flac"), 0};
            track.setId(112);
            track.setTitle(QStringLiteral("A2"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a3.flac"), 0};
            track.setId(113);
            track.setTitle(QStringLiteral("A3"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a4.flac"), 0};
            track.setId(114);
            track.setTitle(QStringLiteral("A4"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a5.flac"), 0};
            track.setId(115);
            track.setTitle(QStringLiteral("A5"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a6.flac"), 0};
            track.setId(116);
            track.setTitle(QStringLiteral("A6"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a7.flac"), 0};
            track.setId(117);
            track.setTitle(QStringLiteral("A7"));
            track.generateHash();
            return track;
        }(),
    };

    auto* playlistA = harness.handler.createNewPlaylist(QStringLiteral("Playlist A"), aTracks);
    QVERIFY(playlistA);

    VimHandler handler;
    handler.setPlaylistHandler(&harness.handler);

    FakeCurrentPlaylistController observer;
    observer.setPlaylistHandler(&harness.handler);
    handler.setCurrentPlaylistController(&observer);

    Fooyin::PlaylistView view;
    QStandardItemModel playlistModel;
    view.setModel(&playlistModel);

    syncPlaylistModel(playlistModel, playlistA);
    view.setCurrentIndex(playlistModel.index(3, 0));
    focusTree(&view);
    observer.changeCurrentPlaylist(playlistA);

    handler.enterVisual();
    handler.extendVisualCursor(+3);
    QCOMPARE(handler.mode(), VimHandler::Mode::Visual);

    SearchDialog dialog;
    QStandardItemModel dialogModel;
    for(int i = 0; i < 10; ++i)
        dialogModel.appendRow(new QStandardItem(QStringLiteral("Result %1").arg(i + 1)));
    dialog.view()->setModel(&dialogModel);
    dialog.show();

    dialog.searchBar()->setFocus();
    pumpEvents();
    focusTree(dialog.view());
    pumpEvents();

    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);
    QVERIFY(dialog.view()->selectionModel()->selectedRows().size() <= 1);
    QVERIFY(!dialog.view()->selectionModel()->isRowSelected(3, QModelIndex{}));
    QVERIFY(!dialog.view()->selectionModel()->isRowSelected(4, QModelIndex{}));
    QVERIFY(!dialog.view()->selectionModel()->isRowSelected(5, QModelIndex{}));
    QVERIFY(!dialog.view()->selectionModel()->isRowSelected(6, QModelIndex{}));
}

void TestVimHandlerViewContext::searchLibraryVisualModeDoesNotLeakBackToPlaylist()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("search_library_visual_no_leak.ini"))};
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    const Fooyin::TrackList aTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a1.flac"), 0};
            track.setId(121);
            track.setTitle(QStringLiteral("A1"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a2.flac"), 0};
            track.setId(122);
            track.setTitle(QStringLiteral("A2"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a3.flac"), 0};
            track.setId(123);
            track.setTitle(QStringLiteral("A3"));
            track.generateHash();
            return track;
        }(),
    };

    auto* playlistA = harness.handler.createNewPlaylist(QStringLiteral("Playlist A"), aTracks);
    QVERIFY(playlistA);

    VimHandler handler;
    handler.setPlaylistHandler(&harness.handler);

    FakeCurrentPlaylistController observer;
    observer.setPlaylistHandler(&harness.handler);
    handler.setCurrentPlaylistController(&observer);

    Fooyin::PlaylistView view;
    QStandardItemModel playlistModel;
    view.setModel(&playlistModel);

    syncPlaylistModel(playlistModel, playlistA);
    view.setCurrentIndex(playlistModel.index(0, 0));
    focusTree(&view);
    observer.changeCurrentPlaylist(playlistA);

    SearchDialog dialog;
    QStandardItemModel dialogModel;
    dialogModel.appendRow(new QStandardItem(QStringLiteral("Result 1")));
    dialogModel.appendRow(new QStandardItem(QStringLiteral("Result 2")));
    dialogModel.appendRow(new QStandardItem(QStringLiteral("Result 3")));
    dialog.view()->setModel(&dialogModel);
    dialog.show();

    dialog.searchBar()->setFocus();
    pumpEvents();
    focusTree(dialog.view());
    pumpEvents();

    handler.enterVisual();
    handler.extendVisualCursor(+1);
    QCOMPARE(handler.mode(), VimHandler::Mode::Visual);
    QCOMPARE(dialog.view()->selectionModel()->selectedRows().size(), 2);

    dialog.close();
    pumpEvents();
    focusTree(&view);

    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);
    QCOMPARE(view.selectionModel()->selectedRows().size(), 1);
    QCOMPARE(view.currentIndex().row(), 0);
}

void TestVimHandlerViewContext::searchLibraryCloseRestoresPlaylistVisualModeFromSearchBox()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("search_library_close_restore_visual_box.ini"))};
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    const Fooyin::TrackList aTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a1.flac"), 0};
            track.setId(131);
            track.setTitle(QStringLiteral("A1"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a2.flac"), 0};
            track.setId(132);
            track.setTitle(QStringLiteral("A2"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a3.flac"), 0};
            track.setId(133);
            track.setTitle(QStringLiteral("A3"));
            track.generateHash();
            return track;
        }(),
    };

    auto* playlistA = harness.handler.createNewPlaylist(QStringLiteral("Playlist A"), aTracks);
    QVERIFY(playlistA);

    VimHandler handler;
    handler.setPlaylistHandler(&harness.handler);

    FakeCurrentPlaylistController observer;
    observer.setPlaylistHandler(&harness.handler);
    handler.setCurrentPlaylistController(&observer);

    Fooyin::PlaylistView view;
    QStandardItemModel playlistModel;
    view.setModel(&playlistModel);

    syncPlaylistModel(playlistModel, playlistA);
    view.setCurrentIndex(playlistModel.index(0, 0));
    focusTree(&view);
    observer.changeCurrentPlaylist(playlistA);

    handler.enterVisual();
    handler.extendVisualCursor(+1);

    SearchDialog dialog;
    QStandardItemModel dialogModel;
    dialogModel.appendRow(new QStandardItem(QStringLiteral("Result 1")));
    dialog.view()->setModel(&dialogModel);
    dialog.show();

    dialog.searchBar()->setFocus();
    pumpEvents();
    QCOMPARE(handler.mode(), VimHandler::Mode::Insert);

    dialog.close();
    pumpEvents();
    focusTree(&view);
    dispatchFocusIn(handler, &view);

    QCOMPARE(handler.mode(), VimHandler::Mode::Visual);
    QCOMPARE(view.currentIndex().row(), 1);
    QCOMPARE(view.selectionModel()->selectedRows().size(), 2);
}

void TestVimHandlerViewContext::searchLibraryCloseRestoresPlaylistVisualModeAfterResults()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{
        tempDir.filePath(QStringLiteral("search_library_close_restore_visual_results.ini"))};
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    const Fooyin::TrackList aTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a1.flac"), 0};
            track.setId(141);
            track.setTitle(QStringLiteral("A1"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a2.flac"), 0};
            track.setId(142);
            track.setTitle(QStringLiteral("A2"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a3.flac"), 0};
            track.setId(143);
            track.setTitle(QStringLiteral("A3"));
            track.generateHash();
            return track;
        }(),
    };

    auto* playlistA = harness.handler.createNewPlaylist(QStringLiteral("Playlist A"), aTracks);
    QVERIFY(playlistA);

    VimHandler handler;
    handler.setPlaylistHandler(&harness.handler);

    FakeCurrentPlaylistController observer;
    observer.setPlaylistHandler(&harness.handler);
    handler.setCurrentPlaylistController(&observer);

    Fooyin::PlaylistView view;
    QStandardItemModel playlistModel;
    view.setModel(&playlistModel);

    syncPlaylistModel(playlistModel, playlistA);
    view.setCurrentIndex(playlistModel.index(0, 0));
    focusTree(&view);
    observer.changeCurrentPlaylist(playlistA);

    handler.enterVisual();
    handler.extendVisualCursor(+1);

    SearchDialog dialog;
    QStandardItemModel dialogModel;
    dialogModel.appendRow(new QStandardItem(QStringLiteral("Result 1")));
    dialogModel.appendRow(new QStandardItem(QStringLiteral("Result 2")));
    dialog.view()->setModel(&dialogModel);
    dialog.show();

    dialog.searchBar()->setFocus();
    pumpEvents();
    focusTree(dialog.view());
    pumpEvents();
    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);

    dialog.close();
    pumpEvents();
    focusTree(&view);
    dispatchFocusIn(handler, &view);

    QCOMPARE(handler.mode(), VimHandler::Mode::Visual);
    QCOMPARE(view.currentIndex().row(), 1);
    QCOMPARE(view.selectionModel()->selectedRows().size(), 2);
}

void TestVimHandlerViewContext::searchLibraryDialogDoesNotRestoreOrOverwritePlaylistState()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("search_library_cursor_state.ini"))};
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    const Fooyin::TrackList aTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a1.flac"), 0};
            track.setId(91);
            track.setTitle(QStringLiteral("A1"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a2.flac"), 0};
            track.setId(92);
            track.setTitle(QStringLiteral("A2"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a3.flac"), 0};
            track.setId(93);
            track.setTitle(QStringLiteral("A3"));
            track.generateHash();
            return track;
        }(),
    };
    const Fooyin::TrackList bTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/b1.flac"), 0};
            track.setId(94);
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

    FakeCurrentPlaylistController observer;
    observer.setPlaylistHandler(&harness.handler);
    handler.setCurrentPlaylistController(&observer);

    Fooyin::PlaylistView view;
    QStandardItemModel playlistModel;
    view.setModel(&playlistModel);

    syncPlaylistModel(playlistModel, playlistA);
    view.setCurrentIndex(playlistModel.index(1, 0));
    focusTree(&view);
    observer.changeCurrentPlaylist(playlistA);

    observer.changeCurrentPlaylist(playlistB);
    syncPlaylistModel(playlistModel, playlistB);
    pumpEvents();

    observer.changeCurrentPlaylist(playlistA);
    syncPlaylistModel(playlistModel, playlistA);
    pumpEvents();
    focusTree(&view);
    QCOMPARE(view.currentIndex().row(), 1);

    view.setCurrentIndex(playlistModel.index(2, 0));
    QCOMPARE(view.currentIndex().row(), 2);

    SearchDialog dialog;
    QStandardItemModel dialogModel;
    dialogModel.appendRow(new QStandardItem(QStringLiteral("Result 1")));
    dialogModel.appendRow(new QStandardItem(QStringLiteral("Result 2")));
    dialog.view()->setModel(&dialogModel);
    dialog.show();

    dialog.searchBar()->setFocus();
    pumpEvents();
    QCOMPARE(view.currentIndex().row(), 2);

    focusTree(dialog.view());
    dialog.view()->setCurrentIndex(dialogModel.index(0, 0));
    pumpEvents();
    QCOMPARE(view.currentIndex().row(), 2);

    dialog.close();
    pumpEvents();

    focusTree(&view);
    observer.changeCurrentPlaylist(playlistB);
    syncPlaylistModel(playlistModel, playlistB);
    pumpEvents();

    observer.changeCurrentPlaylist(playlistA);
    syncPlaylistModel(playlistModel, playlistA);
    pumpEvents();
    focusTree(&view);
    dispatchFocusIn(handler, &view);
    QCOMPARE(view.currentIndex().row(), 2);
    QCOMPARE(view.currentIndex().data().toString(), QStringLiteral("A3"));
}

void TestVimHandlerViewContext::copyAfterCurrentPlayingUsesSearchLibrarySelectionTracks()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("search_library_copy_after_playing.ini"))};
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    Fooyin::Track sourceTrack{QStringLiteral("/tmp/source.flac"), 0};
    sourceTrack.setId(101);
    sourceTrack.setTitle(QStringLiteral("Background Source"));
    sourceTrack.generateHash();

    Fooyin::Track playingTrack{QStringLiteral("/tmp/playing.flac"), 0};
    playingTrack.setId(102);
    playingTrack.setTitle(QStringLiteral("Playing"));
    playingTrack.generateHash();

    Fooyin::Track tailTrack{QStringLiteral("/tmp/tail.flac"), 0};
    tailTrack.setId(103);
    tailTrack.setTitle(QStringLiteral("Tail"));
    tailTrack.generateHash();

    Fooyin::Track detachedTrack{QStringLiteral("/tmp/detached.flac"), 0};
    detachedTrack.setId(104);
    detachedTrack.setTitle(QStringLiteral("Detached Result"));
    detachedTrack.generateHash();

    auto* sourcePlaylist = harness.handler.createNewPlaylist(QStringLiteral("Source Playlist"), {sourceTrack});
    auto* playingPlaylist
        = harness.handler.createNewPlaylist(QStringLiteral("Playing Playlist"), {playingTrack, tailTrack});
    QVERIFY(sourcePlaylist);
    QVERIFY(playingPlaylist);

    playingPlaylist->changeCurrentIndex(0);
    harness.handler.changeActivePlaylist(playingPlaylist);

    FakeCurrentPlaylistController observer;
    observer.setPlaylistHandler(&harness.handler);
    observer.changeCurrentPlaylist(sourcePlaylist);

    VimHandler handler;
    handler.setPlaylistHandler(&harness.handler);
    handler.setCurrentPlaylistController(&observer);

    SearchDialog dialog;
    QStandardItemModel model;
    auto* item = new QStandardItem(detachedTrack.title());
    item->setData(QVariant::fromValue(Fooyin::PlaylistTrack{.track = detachedTrack}), PlaylistItemDataRole);
    model.appendRow(item);
    dialog.view()->setModel(&model);
    dialog.view()->setCurrentIndex(model.index(0, 0));
    dialog.show();
    focusTree(dialog.view());

    handler.copyAfterCurrentPlaying();

    const auto resultTracks = playingPlaylist->tracks();
    QCOMPARE(resultTracks.size(), 3);
    QCOMPARE(resultTracks.at(0).title(), QStringLiteral("Playing"));
    QCOMPARE(resultTracks.at(1).title(), QStringLiteral("Detached Result"));
    QCOMPARE(resultTracks.at(2).title(), QStringLiteral("Tail"));
    QCOMPARE(sourcePlaylist->tracks().size(), 1);
    QCOMPARE(sourcePlaylist->tracks().front().title(), QStringLiteral("Background Source"));
}

void TestVimHandlerViewContext::yankRowsUsesSearchLibrarySelectionTracks()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("search_library_yank_rows.ini"))};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    Fooyin::Track sourceTrack{QStringLiteral("/tmp/source_yank_rows.flac"), 0};
    sourceTrack.setId(121);
    sourceTrack.setTitle(QStringLiteral("Background Source"));
    sourceTrack.generateHash();

    Fooyin::Track destinationTrack{QStringLiteral("/tmp/destination_yank_rows.flac"), 0};
    destinationTrack.setId(122);
    destinationTrack.setTitle(QStringLiteral("Destination"));
    destinationTrack.generateHash();

    Fooyin::Track detachedTrack{QStringLiteral("/tmp/detached_yank_rows.flac"), 0};
    detachedTrack.setId(123);
    detachedTrack.setTitle(QStringLiteral("Detached Result"));
    detachedTrack.generateHash();

    auto* sourcePlaylist = harness.handler.createNewPlaylist(QStringLiteral("Source Playlist"), {sourceTrack});
    auto* destinationPlaylist
        = harness.handler.createNewPlaylist(QStringLiteral("Destination Playlist"), {destinationTrack});
    QVERIFY(sourcePlaylist);
    QVERIFY(destinationPlaylist);

    FakeCurrentPlaylistController observer;
    observer.setPlaylistHandler(&harness.handler);
    observer.changeCurrentPlaylist(sourcePlaylist);

    VimHandler handler;
    handler.setPlaylistHandler(&harness.handler);
    handler.setCurrentPlaylistController(&observer);
    handler.setSettingsManager(&settings);

    SearchDialog dialog;
    QStandardItemModel searchModel;
    auto* searchItem = new QStandardItem(detachedTrack.title());
    searchItem->setData(QVariant::fromValue(Fooyin::PlaylistTrack{.track = detachedTrack}), PlaylistItemDataRole);
    searchModel.appendRow(searchItem);
    dialog.view()->setModel(&searchModel);
    dialog.view()->setCurrentIndex(searchModel.index(0, 0));
    dialog.show();
    focusTree(dialog.view());

    QVERIFY(dispatchKey(handler, dialog.view(), u'y'));
    QVERIFY(dispatchKey(handler, dialog.view(), u'y'));

    Fooyin::PlaylistView destinationView;
    QStandardItemModel destinationModel;
    destinationModel.appendRow(new QStandardItem(destinationTrack.title()));
    destinationView.setModel(&destinationModel);
    destinationView.setCurrentIndex(destinationModel.index(0, 0));

    focusTree(&destinationView);
    observer.changeCurrentPlaylist(destinationPlaylist);
    handler.pasteAfter();

    const auto resultTracks = destinationPlaylist->tracks();
    QCOMPARE(resultTracks.size(), 2);
    QCOMPARE(resultTracks.at(0).title(), QStringLiteral("Destination"));
    QCOMPARE(resultTracks.at(1).title(), QStringLiteral("Detached Result"));
    QCOMPARE(sourcePlaylist->trackCount(), 1);
    QCOMPARE(sourcePlaylist->tracks().front().title(), QStringLiteral("Background Source"));
}

void TestVimHandlerViewContext::yankVisualSelectionUsesSearchLibrarySelectionTracks()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("search_library_yank_visual.ini"))};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    Fooyin::Track sourceTrack{QStringLiteral("/tmp/source_yank_visual.flac"), 0};
    sourceTrack.setId(131);
    sourceTrack.setTitle(QStringLiteral("Background Source"));
    sourceTrack.generateHash();

    Fooyin::Track destinationTrack{QStringLiteral("/tmp/destination_yank_visual.flac"), 0};
    destinationTrack.setId(132);
    destinationTrack.setTitle(QStringLiteral("Destination"));
    destinationTrack.generateHash();

    Fooyin::Track detachedTrackA{QStringLiteral("/tmp/detached_yank_visual_a.flac"), 0};
    detachedTrackA.setId(133);
    detachedTrackA.setTitle(QStringLiteral("Detached Result A"));
    detachedTrackA.generateHash();

    Fooyin::Track detachedTrackB{QStringLiteral("/tmp/detached_yank_visual_b.flac"), 0};
    detachedTrackB.setId(134);
    detachedTrackB.setTitle(QStringLiteral("Detached Result B"));
    detachedTrackB.generateHash();

    auto* sourcePlaylist = harness.handler.createNewPlaylist(QStringLiteral("Source Playlist"), {sourceTrack});
    auto* destinationPlaylist
        = harness.handler.createNewPlaylist(QStringLiteral("Destination Playlist"), {destinationTrack});
    QVERIFY(sourcePlaylist);
    QVERIFY(destinationPlaylist);

    FakeCurrentPlaylistController observer;
    observer.setPlaylistHandler(&harness.handler);
    observer.changeCurrentPlaylist(sourcePlaylist);

    VimHandler handler;
    handler.setPlaylistHandler(&harness.handler);
    handler.setCurrentPlaylistController(&observer);
    handler.setSettingsManager(&settings);

    SearchDialog dialog;
    QStandardItemModel searchModel;
    auto* itemA = new QStandardItem(detachedTrackA.title());
    itemA->setData(QVariant::fromValue(Fooyin::PlaylistTrack{.track = detachedTrackA}), PlaylistItemDataRole);
    auto* itemB = new QStandardItem(detachedTrackB.title());
    itemB->setData(QVariant::fromValue(Fooyin::PlaylistTrack{.track = detachedTrackB}), PlaylistItemDataRole);
    searchModel.appendRow(itemA);
    searchModel.appendRow(itemB);
    dialog.view()->setModel(&searchModel);
    dialog.view()->setCurrentIndex(searchModel.index(0, 0));
    dialog.show();
    focusTree(dialog.view());

    QVERIFY(dispatchKey(handler, dialog.view(), u'v'));
    QVERIFY(dispatchKey(handler, dialog.view(), u'j'));
    QVERIFY(dispatchKey(handler, dialog.view(), u'y'));
    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);

    Fooyin::PlaylistView destinationView;
    QStandardItemModel destinationModel;
    destinationModel.appendRow(new QStandardItem(destinationTrack.title()));
    destinationView.setModel(&destinationModel);
    destinationView.setCurrentIndex(destinationModel.index(0, 0));

    focusTree(&destinationView);
    observer.changeCurrentPlaylist(destinationPlaylist);
    handler.pasteAfter();

    const auto resultTracks = destinationPlaylist->tracks();
    QCOMPARE(resultTracks.size(), 3);
    QCOMPARE(resultTracks.at(0).title(), QStringLiteral("Destination"));
    QCOMPARE(resultTracks.at(1).title(), QStringLiteral("Detached Result A"));
    QCOMPARE(resultTracks.at(2).title(), QStringLiteral("Detached Result B"));
    QCOMPARE(sourcePlaylist->trackCount(), 1);
    QCOMPARE(sourcePlaylist->tracks().front().title(), QStringLiteral("Background Source"));
}

void TestVimHandlerViewContext::moveAfterCurrentPlayingIsNoOpForSearchLibrarySelection()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("search_library_move_after_playing.ini"))};
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    Fooyin::Track sourceTrack{QStringLiteral("/tmp/source_move.flac"), 0};
    sourceTrack.setId(111);
    sourceTrack.setTitle(QStringLiteral("Background Source"));
    sourceTrack.generateHash();

    Fooyin::Track playingTrack{QStringLiteral("/tmp/playing_move.flac"), 0};
    playingTrack.setId(112);
    playingTrack.setTitle(QStringLiteral("Playing"));
    playingTrack.generateHash();

    Fooyin::Track tailTrack{QStringLiteral("/tmp/tail_move.flac"), 0};
    tailTrack.setId(113);
    tailTrack.setTitle(QStringLiteral("Tail"));
    tailTrack.generateHash();

    Fooyin::Track detachedTrack{QStringLiteral("/tmp/detached_move.flac"), 0};
    detachedTrack.setId(114);
    detachedTrack.setTitle(QStringLiteral("Detached Result"));
    detachedTrack.generateHash();

    auto* sourcePlaylist = harness.handler.createNewPlaylist(QStringLiteral("Source Playlist"), {sourceTrack});
    auto* playingPlaylist
        = harness.handler.createNewPlaylist(QStringLiteral("Playing Playlist"), {playingTrack, tailTrack});
    QVERIFY(sourcePlaylist);
    QVERIFY(playingPlaylist);

    playingPlaylist->changeCurrentIndex(0);
    harness.handler.changeActivePlaylist(playingPlaylist);

    FakeCurrentPlaylistController observer;
    observer.setPlaylistHandler(&harness.handler);
    observer.changeCurrentPlaylist(sourcePlaylist);

    VimHandler handler;
    handler.setPlaylistHandler(&harness.handler);
    handler.setCurrentPlaylistController(&observer);

    SearchDialog dialog;
    QStandardItemModel model;
    auto* item = new QStandardItem(detachedTrack.title());
    item->setData(QVariant::fromValue(Fooyin::PlaylistTrack{.track = detachedTrack}), PlaylistItemDataRole);
    model.appendRow(item);
    dialog.view()->setModel(&model);
    dialog.view()->setCurrentIndex(model.index(0, 0));
    dialog.show();
    focusTree(dialog.view());

    handler.moveAfterCurrentPlaying();

    const auto resultTracks = playingPlaylist->tracks();
    QCOMPARE(resultTracks.size(), 2);
    QCOMPARE(resultTracks.at(0).title(), QStringLiteral("Playing"));
    QCOMPARE(resultTracks.at(1).title(), QStringLiteral("Tail"));
    QCOMPARE(sourcePlaylist->tracks().size(), 1);
    QCOMPARE(sourcePlaylist->tracks().front().title(), QStringLiteral("Background Source"));
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

    FakeCurrentPlaylistController observer;
    handler.setCurrentPlaylistController(&observer);

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

    FakeCurrentPlaylistController observer;
    observer.setPlaylistHandler(&harness.handler);
    handler.setCurrentPlaylistController(&observer);
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

    FakeCurrentPlaylistController observer;
    observer.setPlaylistHandler(&harness.handler);
    handler.setCurrentPlaylistController(&observer);
    observer.changeCurrentPlaylist(currentPlaylist);

    handler.previousPlaylist();

    QCOMPARE(observer.currentPlaylist(), expectedPlaylist);
    QCOMPARE(observer.currentPlaylistId(), expectedPlaylist->id());
}

void TestVimHandlerViewContext::unseenPlaylistDefaultsToNormalWithoutSelection()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("unseen_playlist_defaults.ini"))};
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    const Fooyin::TrackList aTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a1.flac"), 0};
            track.setId(151);
            track.setTitle(QStringLiteral("A1"));
            track.generateHash();
            return track;
        }(),
    };
    const Fooyin::TrackList bTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/b1.flac"), 0};
            track.setId(152);
            track.setTitle(QStringLiteral("B1"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/b2.flac"), 0};
            track.setId(153);
            track.setTitle(QStringLiteral("B2"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/b3.flac"), 0};
            track.setId(154);
            track.setTitle(QStringLiteral("B3"));
            track.generateHash();
            return track;
        }(),
    };
    const Fooyin::TrackList cTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/c1.flac"), 0};
            track.setId(155);
            track.setTitle(QStringLiteral("C1"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/c2.flac"), 0};
            track.setId(156);
            track.setTitle(QStringLiteral("C2"));
            track.generateHash();
            return track;
        }(),
    };

    auto* playlistA = harness.handler.createNewPlaylist(QStringLiteral("Playlist A"), aTracks);
    auto* playlistB = harness.handler.createNewPlaylist(QStringLiteral("Playlist B"), bTracks);
    auto* playlistC = harness.handler.createNewPlaylist(QStringLiteral("Playlist C"), cTracks);
    QVERIFY(playlistA);
    QVERIFY(playlistB);
    QVERIFY(playlistC);

    VimHandler handler;
    handler.setPlaylistHandler(&harness.handler);

    FakeCurrentPlaylistController observer;
    observer.setPlaylistHandler(&harness.handler);
    handler.setCurrentPlaylistController(&observer);

    Fooyin::PlaylistView view;
    QStandardItemModel model;
    view.setModel(&model);

    syncPlaylistModel(model, playlistA);
    view.setCurrentIndex(model.index(0, 0));
    focusTree(&view);
    observer.changeCurrentPlaylist(playlistA);

    observer.changeCurrentPlaylist(playlistB);
    syncPlaylistModel(model, playlistB);
    pumpEvents();

    view.setCurrentIndex(model.index(0, 0));
    handler.enterVisual();
    handler.extendVisualCursor(+2);
    QCOMPARE(handler.mode(), VimHandler::Mode::Visual);

    observer.changeCurrentPlaylist(playlistC);
    syncPlaylistModel(model, playlistC);
    pumpEvents();

    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);
    QCOMPARE(view.selectionModel()->selectedRows().size(), 0);
    QVERIFY(!view.currentIndex().isValid());
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

    FakeCurrentPlaylistController observer;
    observer.setPlaylistHandler(&harness.handler);
    handler.setCurrentPlaylistController(&observer);

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

    FakeCurrentPlaylistController observer;
    observer.setPlaylistHandler(&harness.handler);
    handler.setCurrentPlaylistController(&observer);

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

    FakeCurrentPlaylistController observer;
    observer.setPlaylistHandler(&harness.handler);
    handler.setCurrentPlaylistController(&observer);

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

void TestVimHandlerViewContext::jumpsToGlobalMarkInAnotherPlaylist()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("global_mark_cross_playlist.ini"))};
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    const Fooyin::TrackList aTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a1.flac"), 0};
            track.setId(41);
            track.setTitle(QStringLiteral("A1"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a2.flac"), 0};
            track.setId(42);
            track.setTitle(QStringLiteral("A2"));
            track.generateHash();
            return track;
        }(),
    };
    const Fooyin::TrackList bTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/b1.flac"), 0};
            track.setId(43);
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

    FakeCurrentPlaylistController observer;
    observer.setPlaylistHandler(&harness.handler);
    handler.setCurrentPlaylistController(&observer);

    Fooyin::PlaylistView view;
    QStandardItemModel model;
    view.setModel(&model);

    syncPlaylistModel(model, playlistA);
    view.setCurrentIndex(model.index(1, 0));
    focusTree(&view);
    observer.changeCurrentPlaylist(playlistA);

    handler.beginSetMark();
    QVERIFY(dispatchKey(handler, &view, u'A'));

    observer.changeCurrentPlaylist(playlistB);
    syncPlaylistModel(model, playlistB);
    pumpEvents();
    view.setCurrentIndex(model.index(0, 0));

    handler.beginJumpToMark();
    QVERIFY(dispatchKey(handler, &view, u'A'));

    QCOMPARE(observer.currentPlaylist(), playlistA);
    syncPlaylistModel(model, playlistA);
    pumpEvents();
    dispatchFocusIn(handler, &view);

    QCOMPARE(view.currentIndex().row(), 1);
    QCOMPARE(view.currentIndex().data().toString(), QStringLiteral("A2"));
}

void TestVimHandlerViewContext::clearsStaleGlobalMarkWhenMarkedEntryIsDeleted()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("global_mark_stale_entry.ini"))};
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    const Fooyin::TrackList aTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a1.flac"), 0};
            track.setId(51);
            track.setTitle(QStringLiteral("A1"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a2.flac"), 0};
            track.setId(52);
            track.setTitle(QStringLiteral("A2"));
            track.generateHash();
            return track;
        }(),
    };
    const Fooyin::TrackList bTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/b1.flac"), 0};
            track.setId(53);
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

    FakeCurrentPlaylistController observer;
    observer.setPlaylistHandler(&harness.handler);
    handler.setCurrentPlaylistController(&observer);

    Fooyin::PlaylistView view;
    QStandardItemModel model;
    view.setModel(&model);

    syncPlaylistModel(model, playlistA);
    view.setCurrentIndex(model.index(1, 0));
    focusTree(&view);
    observer.changeCurrentPlaylist(playlistA);

    handler.beginSetMark();
    QVERIFY(dispatchKey(handler, &view, u'B'));

    harness.handler.replacePlaylistTracks(playlistA->id(), Fooyin::TrackList{aTracks.front()});

    observer.changeCurrentPlaylist(playlistB);
    syncPlaylistModel(model, playlistB);
    pumpEvents();

    handler.beginJumpToMark();
    QVERIFY(dispatchKey(handler, &view, u'B'));
    QCOMPARE(observer.currentPlaylist(), playlistB);

    handler.beginJumpToMark();
    QVERIFY(dispatchKey(handler, &view, u'B'));
    QCOMPARE(observer.currentPlaylist(), playlistB);
}

void TestVimHandlerViewContext::exposesMarksForScriptContext()
{
    MarkScriptHarness harness;
    QVERIFY(harness.playlists.dbInitialised);
    QVERIFY(harness.registered);
    const auto track = markTestTrack();
    auto* playlist   = harness.playlists.handler.createNewPlaylist(u"Playlist A"_s, {track, track});
    QVERIFY(playlist);
    harness.select(playlist, 1);
    QVERIFY(harness.mark(u'c'));
    QVERIFY(harness.mark(u'a'));
    QVERIFY(harness.mark(u'D'));
    QVERIFY(harness.mark(u'B'));

    Fooyin::ScriptParser parser{&harness.registry};
    Fooyin::ScriptPlaylistData row{.playlistId = playlist->id(), .entryId = playlist->playlistTrack(1)->entryId};
    const Fooyin::ScriptContext context{.playlistData = &row};
    // No live playlist or row index is needed, even for duplicate tracks.
    const auto script = parser.parse(u"[%mark%]-[%global_mark%]"_s);
    QCOMPARE(parser.evaluate(script, track, context), u"a-B"_s);
    QCOMPARE(parser.evaluate(u"$if(%mark%,marked,unmarked)"_s, track, context), u"marked"_s);
    for(const auto& variable : harness.provider.variables()) {
        QCOMPARE(variable.category, u"Vim Motions"_s);
        QVERIFY(!variable.description.isEmpty());
    }

    row.entryId = playlist->playlistTrack(0)->entryId;
    QCOMPARE(parser.evaluate(script, track, context), u"-"_s);
    QCOMPARE(parser.evaluate(u"$if(%mark%,marked,unmarked)"_s, track, context), u"unmarked"_s);
    row.entryId    = playlist->playlistTrack(1)->entryId;
    row.playlistId = Fooyin::UId::create();
    QCOMPARE(parser.evaluate(script, track, context), u"-"_s);
    row.playlistId = {};
    QCOMPARE(parser.evaluate(script, track, context), u"-"_s);
    row.playlistId = playlist->id();
    row.entryId    = {};
    QCOMPARE(parser.evaluate(script, track, context), u"-"_s);
    QCOMPARE(parser.evaluate(script, track), u"-"_s);

    auto entries = playlist->playlistTracks();
    std::reverse(entries.begin(), entries.end());
    harness.playlists.handler.replacePlaylistTracks(playlist->id(), entries);
    row.entryId = entries.front().entryId;
    QCOMPARE(parser.evaluate(script, track, context), u"a-B"_s);

    // A second provider must not redirect callbacks registered by the first.
    VimHandler otherHandler;
    VimMarkVariableProvider otherProvider{&otherHandler};
    Fooyin::ScriptRegistry otherRegistry;
    for(const auto& variable : otherProvider.variables())
        QVERIFY(otherRegistry.registerVariable(variable));
    otherRegistry.finalise();
    Fooyin::ScriptParser otherParser{&otherRegistry};
    QCOMPARE(otherParser.evaluate(script, track, context), u"-"_s);
    QCOMPARE(parser.evaluate(script, track, context), u"a-B"_s);
}

void TestVimHandlerViewContext::refreshesMarksAfterReassignment()
{
    MarkScriptHarness harness;
    QVERIFY(harness.playlists.dbInitialised);
    const auto track = markTestTrack();
    auto* playlistA  = harness.playlists.handler.createNewPlaylist(u"Playlist A"_s, {track, track});
    auto* playlistB  = harness.playlists.handler.createNewPlaylist(u"Playlist B"_s, {track});
    QVERIFY(playlistA);
    QVERIFY(playlistB);
    harness.select(playlistA, 0);
    const auto first  = playlistA->playlistTrack(0)->entryId;
    const auto second = playlistA->playlistTrack(1)->entryId;
    const auto other  = playlistB->playlistTrack(0)->entryId;

    struct Refresh
    {
        Fooyin::UId playlistId;
        std::vector<Fooyin::UId> entries;
        QStringList values;
    };
    std::vector<Refresh> refreshes;
    Fooyin::ScriptParser parser{&harness.registry};
    connect(&harness.interactor, &Fooyin::PlaylistInteractor::playlistRefreshRequested, this,
            [&](const Fooyin::UId& id, const std::vector<Fooyin::UId>& entries) {
                Refresh refresh{id, entries, {}};
                for(const auto& entry : entries) {
                    Fooyin::ScriptPlaylistData row{.playlistId = id, .entryId = entry};
                    refresh.values.push_back(
                        parser.evaluate(u"%mark%/%global_mark%"_s, Fooyin::ScriptContext{.playlistData = &row}));
                }
                refreshes.push_back(std::move(refresh));
            });
    int secondViewRefreshes{0};
    connect(&harness.interactor, &Fooyin::PlaylistInteractor::playlistRefreshRequested, &harness.view,
            [&]() { ++secondViewRefreshes; });

    QVERIFY(harness.mark(u'a'));
    QCOMPARE(refreshes.size(), 1U);
    QCOMPARE(refreshes.back().entries, std::vector{first});
    QCOMPARE(refreshes.back().values, QStringList{u"a/"_s});
    refreshes.clear();
    harness.view.setCurrentIndex(harness.model.index(1, 0));
    QVERIFY(harness.mark(u'a'));
    QCOMPARE(refreshes.size(), 1U);
    QCOMPARE(refreshes.back().entries, (std::vector{first, second}));
    QCOMPARE(refreshes.back().values, (QStringList{u"/"_s, u"a/"_s}));

    QVERIFY(harness.mark(u'B'));
    refreshes.clear();
    harness.select(playlistB, 0);
    QVERIFY(harness.mark(u'B'));
    QCOMPARE(refreshes.size(), 2U);
    QCOMPARE(refreshes[0].playlistId, playlistA->id());
    QCOMPARE(refreshes[0].entries, std::vector{second});
    QCOMPARE(refreshes[0].values, QStringList{u"a/"_s});
    QCOMPARE(refreshes[1].playlistId, playlistB->id());
    QCOMPARE(refreshes[1].entries, std::vector{other});
    QCOMPARE(refreshes[1].values, QStringList{u"/B"_s});
    QCOMPARE(secondViewRefreshes, 5);

    // Stale cleanup must also publish a snapshot, not only update navigation state.
    harness.playlists.handler.removePlaylistTracks(playlistB->id(), {0});
    harness.handler.beginJumpToMark();
    QVERIFY(dispatchKey(harness.handler, &harness.view, u'B'));
    Fooyin::ScriptPlaylistData stale{.playlistId = playlistB->id(), .entryId = other};
    QCOMPARE(parser.evaluate(u"%global_mark%"_s, Fooyin::ScriptContext{.playlistData = &stale}), QString{});

    harness.playlists.handler.removePlaylistTracks(playlistA->id(), {1});
    harness.select(playlistA, 0);
    harness.handler.beginJumpToMark();
    QVERIFY(dispatchKey(harness.handler, &harness.view, u'a'));
    stale.playlistId = playlistA->id();
    stale.entryId    = second;
    QCOMPARE(parser.evaluate(u"%mark%"_s, Fooyin::ScriptContext{.playlistData = &stale}), QString{});
}

void TestVimHandlerViewContext::refreshesTransferredMarks_data()
{
    QTest::addColumn<QString>("action");
    QTest::newRow("paste-after") << u"pasteAfter"_s;
    QTest::newRow("paste-before") << u"pasteBefore"_s;
    QTest::newRow("move-after-playing") << u"moveAfterCurrentPlaying"_s;
}

void TestVimHandlerViewContext::refreshesTransferredMarks()
{
    QFETCH(QString, action);
    MarkScriptHarness harness;
    QVERIFY(harness.playlists.dbInitialised);
    const auto track  = markTestTrack();
    auto* source      = harness.playlists.handler.createNewPlaylist(u"Source"_s, {track});
    auto* destination = harness.playlists.handler.createNewPlaylist(u"Destination"_s, {track});
    QVERIFY(source);
    QVERIFY(destination);
    harness.select(destination, 0);
    QVERIFY(harness.mark(u'a'));
    const auto previous = destination->playlistTrack(0)->entryId;
    harness.select(source, 0);
    QVERIFY(harness.mark(u'a'));
    const auto cutEntry = source->playlistTrack(0)->entryId;
    Fooyin::ScriptParser parser{&harness.registry};
    Fooyin::ScriptPlaylistData row{.playlistId = source->id(), .entryId = cutEntry};
    if(action == u"moveAfterCurrentPlaying") {
        destination->changeCurrentIndex(0);
        harness.playlists.handler.changeActivePlaylist(destination);
    }
    else {
        harness.handler.deleteRows(1);
        QCOMPARE(parser.evaluate(u"%mark%"_s, Fooyin::ScriptContext{.playlistData = &row}), QString{});
        harness.select(destination, 0);
    }

    std::vector<Fooyin::UId> refreshed;
    connect(&harness.interactor, &Fooyin::PlaylistInteractor::playlistRefreshRequested, this,
            [&](const Fooyin::UId& id, const std::vector<Fooyin::UId>& entries) {
                if(id == source->id())
                    return;
                QCOMPARE(id, destination->id());
                QCOMPARE(destination->trackCount(), 2);
                for(const auto& entry : entries)
                    QVERIFY(destination->indexOfTrackEntry(entry) >= 0);
                refreshed = entries;
            });
    if(action == u"pasteBefore")
        harness.handler.pasteBefore();
    else if(action == u"pasteAfter")
        harness.handler.pasteAfter();
    else
        harness.handler.moveAfterCurrentPlaying();
    QCOMPARE(parser.evaluate(u"%mark%"_s, Fooyin::ScriptContext{.playlistData = &row}), QString{});
    const auto inserted = destination->playlistTrack(action == u"pasteBefore" ? 0 : 1)->entryId;
    QCOMPARE(refreshed, (std::vector{previous, inserted}));
    row.playlistId = destination->id();
    row.entryId    = previous;
    QCOMPARE(parser.evaluate(u"%mark%"_s, Fooyin::ScriptContext{.playlistData = &row}), QString{});
    row.entryId = inserted;
    QCOMPARE(parser.evaluate(u"%mark%"_s, Fooyin::ScriptContext{.playlistData = &row}), u"a"_s);
}

void TestVimHandlerViewContext::evaluatesMarksWhileTheyChange()
{
    MarkScriptHarness harness;
    QVERIFY(harness.playlists.dbInitialised);
    const auto track = markTestTrack();
    auto* playlist   = harness.playlists.handler.createNewPlaylist(u"Playlist"_s, {track, track});
    QVERIFY(playlist);
    harness.select(playlist, 0);
    QVERIFY(harness.mark(u'a'));
    QVERIFY(harness.mark(u'B'));
    const Fooyin::ScriptPlaylistData row{.playlistId = playlist->id(), .entryId = playlist->playlistTrack(0)->entryId};
    std::atomic<bool> valid{true};
    std::latch ready{2};
    std::vector<std::jthread> workers;
    for(int i = 0; i < 2; ++i) {
        workers.emplace_back([&](std::stop_token stop) {
            Fooyin::ScriptParser parser{&harness.registry};
            const auto local  = parser.parse(u"%mark%"_s);
            const auto global = parser.parse(u"%global_mark%"_s);
            const Fooyin::ScriptContext context{.playlistData = &row};
            bool first{true};
            do {
                const auto localValue  = parser.evaluate(local, track, context);
                const auto globalValue = parser.evaluate(global, track, context);
                if((!localValue.isEmpty() && localValue != u"a") || (!globalValue.isEmpty() && globalValue != u"B"))
                    valid.store(false);
                if(first) {
                    if(localValue != u"a" || globalValue != u"B")
                        valid.store(false);
                    ready.count_down();
                    first = false;
                }
            } while(!stop.stop_requested());
        });
    }
    ready.wait();
    bool dispatched{true};
    for(int i = 0; i < 64; ++i) {
        harness.view.setCurrentIndex(harness.model.index(i % 2, 0));
        dispatched = harness.mark(u'a') && harness.mark(u'B') && dispatched;
    }
    for(auto& worker : workers)
        worker.request_stop();
    workers.clear();
    QVERIFY(dispatched);
    QVERIFY(valid.load());
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
    FakeCurrentPlaylistController observer;
    handler.setCurrentPlaylistController(&observer);
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
    FakeCurrentPlaylistController observer;
    handler.setCurrentPlaylistController(&observer);

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
    FakeCurrentPlaylistController observer;
    handler.setCurrentPlaylistController(&observer);

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

void TestVimHandlerViewContext::restoresVisualSelectionWhenRefocusingSamePlaylistViaOrganiser()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("cursor_visual_refocus_organiser.ini"))};
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    const Fooyin::TrackList aTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a1.flac"), 0};
            track.setId(211);
            track.setTitle(QStringLiteral("A1"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a2.flac"), 0};
            track.setId(212);
            track.setTitle(QStringLiteral("A2"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a3.flac"), 0};
            track.setId(213);
            track.setTitle(QStringLiteral("A3"));
            track.generateHash();
            return track;
        }(),
    };

    auto* playlistA = harness.handler.createNewPlaylist(QStringLiteral("Playlist A"), aTracks);
    QVERIFY(playlistA);

    VimHandler handler;
    handler.setPlaylistHandler(&harness.handler);
    FakeCurrentPlaylistController observer;
    observer.setPlaylistHandler(&harness.handler);
    handler.setCurrentPlaylistController(&observer);

    Fooyin::PlaylistView view;
    QStandardItemModel model;
    view.setModel(&model);

    FakeOrganiserWidget organiser;
    QStandardItemModel organiserModel;
    organiserModel.appendRow(makeOrganiserPlaylistItem(QStringLiteral("Playlist A"), playlistA));
    organiser.view()->setModel(&organiserModel);

    syncPlaylistModel(model, playlistA);
    view.setCurrentIndex(model.index(1, 0));
    focusTree(&view);
    observer.changeCurrentPlaylist(playlistA);

    handler.enterVisual();
    handler.extendVisualCursor(+1);

    focusTree(organiser.view());
    focusTree(&view);
    dispatchFocusIn(handler, &view);

    QCOMPARE(handler.mode(), VimHandler::Mode::Visual);
    QCOMPARE(view.currentIndex().row(), 2);
    QCOMPARE(view.selectionModel()->selectedRows().size(), 2);
    QVERIFY(view.selectionModel()->isRowSelected(1, QModelIndex{}));
    QVERIFY(view.selectionModel()->isRowSelected(2, QModelIndex{}));
}

void TestVimHandlerViewContext::restoresNormalCursorWhenRefocusingAfterLeavingVisualViaOrganiser()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("cursor_normal_refocus_organiser.ini"))};
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    const Fooyin::TrackList aTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a1.flac"), 0};
            track.setId(221);
            track.setTitle(QStringLiteral("A1"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a2.flac"), 0};
            track.setId(222);
            track.setTitle(QStringLiteral("A2"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a3.flac"), 0};
            track.setId(223);
            track.setTitle(QStringLiteral("A3"));
            track.generateHash();
            return track;
        }(),
    };

    auto* playlistA = harness.handler.createNewPlaylist(QStringLiteral("Playlist A"), aTracks);
    QVERIFY(playlistA);

    VimHandler handler;
    handler.setPlaylistHandler(&harness.handler);
    FakeCurrentPlaylistController observer;
    observer.setPlaylistHandler(&harness.handler);
    handler.setCurrentPlaylistController(&observer);

    Fooyin::PlaylistView view;
    QStandardItemModel model;
    view.setModel(&model);

    FakeOrganiserWidget organiser;
    QStandardItemModel organiserModel;
    organiserModel.appendRow(makeOrganiserPlaylistItem(QStringLiteral("Playlist A"), playlistA));
    organiser.view()->setModel(&organiserModel);

    syncPlaylistModel(model, playlistA);
    view.setCurrentIndex(model.index(0, 0));
    focusTree(&view);
    observer.changeCurrentPlaylist(playlistA);

    handler.enterVisual();
    handler.extendVisualCursor(+2);
    handler.enterNormal();

    focusTree(organiser.view());
    focusTree(&view);
    dispatchFocusIn(handler, &view);

    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);
    QCOMPARE(view.currentIndex().row(), 2);
    QCOMPARE(view.selectionModel()->selectedRows().size(), 1);
    QVERIFY(view.selectionModel()->isRowSelected(2, QModelIndex{}));
}

void TestVimHandlerViewContext::restoresVisualSelectionWhenRefocusingAfterSpatialFocusMoveViaOrganiser()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("cursor_visual_spatial_refocus_organiser.ini"))};
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    const Fooyin::TrackList aTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a1.flac"), 0};
            track.setId(231);
            track.setTitle(QStringLiteral("A1"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a2.flac"), 0};
            track.setId(232);
            track.setTitle(QStringLiteral("A2"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a3.flac"), 0};
            track.setId(233);
            track.setTitle(QStringLiteral("A3"));
            track.generateHash();
            return track;
        }(),
    };

    auto* playlistA = harness.handler.createNewPlaylist(QStringLiteral("Playlist A"), aTracks);
    QVERIFY(playlistA);

    VimHandler handler;
    handler.setPlaylistHandler(&harness.handler);
    FakeCurrentPlaylistController observer;
    observer.setPlaylistHandler(&harness.handler);
    handler.setCurrentPlaylistController(&observer);

    FakeSplitterWidget splitter;

    auto* organiser = new FakeOrganiserWidget();
    QStandardItemModel organiserModel;
    organiserModel.appendRow(makeOrganiserPlaylistItem(QStringLiteral("Playlist A"), playlistA));
    organiser->view()->setModel(&organiserModel);
    organiser->view()->setCurrentIndex(organiserModel.index(0, 0));

    auto* playlistWidget = new FakePlaylistWidget();
    auto* view           = playlistWidget->view();
    QStandardItemModel model;
    view->setModel(&model);

    splitter.addWidget(organiser);
    splitter.addWidget(playlistWidget);

    syncPlaylistModel(model, playlistA);
    view->setCurrentIndex(model.index(1, 0));
    focusTree(view);
    observer.changeCurrentPlaylist(playlistA);

    handler.enterVisual();
    handler.extendVisualCursor(+1);

    handler.moveSpatialFocus(Direction::Left);
    pumpEvents();
    QVERIFY(organiser->view()->hasFocus());
    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);

    handler.moveSpatialFocus(Direction::Right);
    pumpEvents();
    dispatchFocusIn(handler, view);

    QCOMPARE(handler.mode(), VimHandler::Mode::Visual);
    QCOMPARE(view->currentIndex().row(), 2);
    QCOMPARE(view->selectionModel()->selectedRows().size(), 2);
    QVERIFY(view->selectionModel()->isRowSelected(1, QModelIndex{}));
    QVERIFY(view->selectionModel()->isRowSelected(2, QModelIndex{}));
}

void TestVimHandlerViewContext::preservesVisualSelectionWhenSwitchingViaOrganiser()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("cursor_visual_organiser_restore.ini"))};
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    const Fooyin::TrackList aTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a1.flac"), 0};
            track.setId(25);
            track.setTitle(QStringLiteral("A1"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a2.flac"), 0};
            track.setId(26);
            track.setTitle(QStringLiteral("A2"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a3.flac"), 0};
            track.setId(27);
            track.setTitle(QStringLiteral("A3"));
            track.generateHash();
            return track;
        }(),
    };
    const Fooyin::TrackList bTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/b1.flac"), 0};
            track.setId(28);
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
    FakeCurrentPlaylistController observer;
    observer.setPlaylistHandler(&harness.handler);
    handler.setCurrentPlaylistController(&observer);

    Fooyin::PlaylistView view;
    QStandardItemModel model;
    view.setModel(&model);

    FakeOrganiserWidget organiser;
    QStandardItemModel organiserModel;
    organiserModel.appendRow(makeOrganiserPlaylistItem(QStringLiteral("Playlist A"), playlistA));
    organiserModel.appendRow(makeOrganiserPlaylistItem(QStringLiteral("Playlist B"), playlistB));
    organiser.view()->setModel(&organiserModel);

    syncPlaylistModel(model, playlistA);
    view.setCurrentIndex(model.index(1, 0));
    focusTree(&view);
    observer.changeCurrentPlaylist(playlistA);

    handler.enterVisual();
    handler.extendVisualCursor(+1);

    focusTree(organiser.view());
    observer.changeCurrentPlaylist(playlistB);
    syncPlaylistModel(model, playlistB);
    pumpEvents();

    observer.changeCurrentPlaylist(playlistA);
    syncPlaylistModel(model, playlistA);
    pumpEvents();

    focusTree(&view);
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
    FakeCurrentPlaylistController observer;
    handler.setCurrentPlaylistController(&observer);

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

void TestVimHandlerViewContext::restoresLatestCursorPositionAfterPlaylistRefocus()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("cursor_refocus_restore.ini"))};
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    const Fooyin::TrackList aTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a1.flac"), 0};
            track.setId(81);
            track.setTitle(QStringLiteral("A1"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a2.flac"), 0};
            track.setId(82);
            track.setTitle(QStringLiteral("A2"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a3.flac"), 0};
            track.setId(83);
            track.setTitle(QStringLiteral("A3"));
            track.generateHash();
            return track;
        }(),
    };
    const Fooyin::TrackList bTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/b1.flac"), 0};
            track.setId(84);
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

    FakeCurrentPlaylistController observer;
    observer.setPlaylistHandler(&harness.handler);
    handler.setCurrentPlaylistController(&observer);

    Fooyin::PlaylistView view;
    QStandardItemModel playlistModel;
    view.setModel(&playlistModel);

    FakeOrganiserWidget organiser;
    QStandardItemModel organiserModel;
    organiserModel.appendRow(makeOrganiserPlaylistItem(QStringLiteral("Playlist A"), playlistA));
    organiserModel.appendRow(makeOrganiserPlaylistItem(QStringLiteral("Playlist B"), playlistB));
    organiser.view()->setModel(&organiserModel);

    syncPlaylistModel(playlistModel, playlistA);
    view.setCurrentIndex(playlistModel.index(1, 0));
    focusTree(&view);
    observer.changeCurrentPlaylist(playlistA);

    observer.changeCurrentPlaylist(playlistB);
    syncPlaylistModel(playlistModel, playlistB);
    pumpEvents();

    observer.changeCurrentPlaylist(playlistA);
    syncPlaylistModel(playlistModel, playlistA);
    pumpEvents();
    focusTree(&view);
    dispatchFocusIn(handler, &view);
    QCOMPARE(view.currentIndex().row(), 1);

    view.setCurrentIndex(playlistModel.index(2, 0));
    focusTree(organiser.view());
    focusTree(&view);
    dispatchFocusIn(handler, &view);

    QCOMPARE(view.currentIndex().row(), 2);
    QCOMPARE(view.currentIndex().data().toString(), QStringLiteral("A3"));
}

void TestVimHandlerViewContext::focusNowPlayingRestoresPlayingRowInsteadOfSavedRow()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("focus_now_playing.ini"))};
    PlaylistHandlerHarness harness{settings};
    QVERIFY(harness.dbInitialised);

    const Fooyin::TrackList aTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a1.flac"), 0};
            track.setId(71);
            track.setTitle(QStringLiteral("A1"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/a2.flac"), 0};
            track.setId(72);
            track.setTitle(QStringLiteral("A2"));
            track.generateHash();
            return track;
        }(),
    };
    const Fooyin::TrackList bTracks{
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/b1.flac"), 0};
            track.setId(73);
            track.setTitle(QStringLiteral("B1"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/b2.flac"), 0};
            track.setId(74);
            track.setTitle(QStringLiteral("B2"));
            track.generateHash();
            return track;
        }(),
        [] {
            Fooyin::Track track{QStringLiteral("/tmp/b3.flac"), 0};
            track.setId(75);
            track.setTitle(QStringLiteral("B3"));
            track.generateHash();
            return track;
        }(),
    };

    auto* playlistA = harness.handler.createNewPlaylist(QStringLiteral("Playlist A"), aTracks);
    auto* playlistB = harness.handler.createNewPlaylist(QStringLiteral("Playlist B"), bTracks);
    QVERIFY(playlistA);
    QVERIFY(playlistB);

    playlistB->changeCurrentIndex(2);
    harness.handler.changeActivePlaylist(playlistB);

    FakeCurrentPlaylistController observer;
    observer.setPlaylistHandler(&harness.handler);

    QMainWindow window;
    Fooyin::ActionManager actionManager(&settings);
    actionManager.setMainWindow(&window);

    Fooyin::PlaylistView view;
    QStandardItemModel model;
    view.setModel(&model);

    QAction showNowPlayingAction(QStringLiteral("Show Now Playing"), &window);
    QObject::connect(&showNowPlayingAction, &QAction::triggered, &view, [&]() {
        observer.changeCurrentPlaylist(playlistB);
        syncPlaylistModel(model, playlistB);
    });
    actionManager.registerAction(&showNowPlayingAction, Fooyin::Id(Fooyin::Constants::Actions::ShowNowPlaying));

    VimHandler handler;
    handler.setPlaylistHandler(&harness.handler);
    handler.setActionManager(&actionManager);
    handler.setCurrentPlaylistController(&observer);

    syncPlaylistModel(model, playlistA);
    view.setCurrentIndex(model.index(0, 0));
    focusTree(&view);
    observer.changeCurrentPlaylist(playlistA);

    syncPlaylistModel(model, playlistB);
    view.setCurrentIndex(model.index(0, 0));
    observer.changeCurrentPlaylist(playlistB);
    pumpEvents();

    syncPlaylistModel(model, playlistA);
    view.setCurrentIndex(model.index(1, 0));
    observer.changeCurrentPlaylist(playlistA);
    pumpEvents();

    const int browsingRow = view.currentIndex().row();
    handler.focusNowPlaying();
    pumpEvents();

    QCOMPARE(observer.currentPlaylist(), playlistB);
    QCOMPARE(view.currentIndex().row(), 2);
    QCOMPARE(view.currentIndex().data().toString(), QStringLiteral("B3"));
    handler.jumpOlder();
    syncPlaylistModel(model, playlistA);
    pumpEvents();
    QCOMPARE(observer.currentPlaylist(), playlistA);
    QCOMPARE(view.currentIndex().row(), browsingRow);
    handler.jumpNewer();
    syncPlaylistModel(model, playlistB);
    pumpEvents();
    QCOMPARE(observer.currentPlaylist(), playlistB);
    QCOMPARE(view.currentIndex().row(), 2);
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

void TestVimHandlerViewContext::playlistSearchAdvancesFromMatchInNonzeroColumn()
{
    VimHandler handler;
    Fooyin::PlaylistView view;
    QStandardItemModel model;

    for(const QString& title : {QStringLiteral("Foo"), QStringLiteral("Foo"), QStringLiteral("Bar"),
                                QStringLiteral("Foo"), QStringLiteral("Bar")}) {
        model.appendRow({new QStandardItem(QStringLiteral("Track")), new QStandardItem(title)});
    }
    view.setModel(&model);
    view.setCurrentIndex(model.index(0, 1));

    qApp->installEventFilter(&handler);
    focusTree(&view);
    handler.enterSearch();
    qApp->processEvents();

    auto* editor = view.window()->findChild<QLineEdit*>();
    QVERIFY(editor);
    QTest::keyClicks(editor, QStringLiteral("Bar"));
    qApp->processEvents();
    QCOMPARE(view.currentIndex(), model.index(2, 1));

    QTest::keyClick(editor, Qt::Key_Return);
    qApp->processEvents();
    QCOMPARE(handler.mode(), VimHandler::Mode::Normal);

    handler.nextMatch();
    QCOMPARE(view.currentIndex(), model.index(4, 1));
    handler.nextMatch();
    QCOMPARE(view.currentIndex(), model.index(2, 1));
    handler.prevMatch();
    QCOMPARE(view.currentIndex(), model.index(4, 1));
    handler.prevMatch();
    QCOMPARE(view.currentIndex(), model.index(2, 1));

    view.setCurrentIndex(model.index(3, 1));
    handler.nextMatch();
    QCOMPARE(view.currentIndex(), model.index(4, 1));
    view.setCurrentIndex(model.index(3, 1));
    handler.prevMatch();
    QCOMPARE(view.currentIndex(), model.index(2, 1));

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
