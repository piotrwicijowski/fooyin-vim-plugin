#pragma once

#include "searchnavigation.h"
#include "vimactions.h"
#include "vimbindingparser.h"
#include "vimclipboard.h"
#include "vimmotionsbindingbackend.h"

#include <QHash>
#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QTimer>
#include <core/playlist/playlist.h>
#include <utils/id.h>

#include <optional>
#include <vector>

class QAbstractItemView;
class QKeyEvent;
class QTreeView;
class QWidget;

namespace Fooyin {
class ActionManager;
class FyWidget;
class Playlist;
class PlaylistHandler;
class PlaylistSelectionObserver;
class SettingsManager;
class TrackSelectionController;
} // namespace Fooyin

namespace Fooyin::VimMotions {

enum class Direction : int;

class ViewLocator;
class SpatialNavigator;
class VimSearchBar;

class VimHandler : public QObject
{
    Q_OBJECT

public:
    enum class Mode
    {
        Normal,
        Visual,
        Insert,
        Filter,
        Search
    };

    using ScopedConfigBindings = QHash<BindingScope, QList<BindingEntry>>;
    using ModeConfigBindings   = QHash<Mode, ScopedConfigBindings>;

    enum class ViewContext
    {
        None,
        PlaylistView,
        PlaylistOrganiser,
        Other
    };

    explicit VimHandler(QObject* parent = nullptr);
    ~VimHandler() override;

    [[nodiscard]] Mode mode() const;

    void setPlaylistHandler(Fooyin::PlaylistHandler* handler);
    void setActionManager(Fooyin::ActionManager* manager);
    void setSettingsManager(Fooyin::SettingsManager* manager);
    void setSettingsBackend(VimMotionsBindingBackend* backend);
    void setTrackSelectionController(Fooyin::TrackSelectionController* controller);
    void setPlaylistSelectionObserver(Fooyin::PlaylistSelectionObserver* observer);

    [[nodiscard]] bool eventFilter(QObject* watched, QEvent* event) override;

    // Testing support
    void rebuildBindings();
    [[nodiscard]] ViewContext viewContext(QAbstractItemView* view) const;
    [[nodiscard]] const ModeConfigBindings& configBindings() const
    {
        return m_configBindings;
    }

    void moveCursor(int delta);
    void jumpToFirst();
    void jumpToLast();
    void jumpToRow(int row);
    void moveCursorHalfPage(int direction);
    void activateCurrentRow();

    void treeMoveSibling(int delta);
    void treeOpenOrDescend();
    void treeCloseOrAscend();
    void organiserCreatePlaylist();
    void organiserCreateGroup();

    void enterInsert();
    void enterVisual();
    void selectAll();
    void enterNormal();
    void enterFilter();

    void deleteRows(int count);
    void yankRows(int count);
    void pasteAfter();
    void pasteBefore();
    void copyAfterCurrentPlaying();
    void moveAfterCurrentPlaying();
    void undo();
    void redo();

    void focusNowPlaying();
    void triggerFooyinAction(const QStringView& actionId);
    void beginSetMark();
    void beginJumpToMark();

    void enterSearch();
    void nextMatch();
    void prevMatch();

    void moveRows(int delta);
    void moveVisualSelection(int delta);
    void deleteVisualSelection();
    void yankVisualSelection();
    void updateVisualSelection();

    [[nodiscard]] int currentCount();
    [[nodiscard]] bool hadExplicitCount() const;
    void clearPendingState();
    void moveSpatialFocus(Direction dir);
    void extendVisualCursor(int delta);
    void extendVisualToFirst();
    void extendVisualToEnd();
    void extendVisualToRow(int row);
    void extendVisualHalfPage(int direction);
    void swapVisualAnchor();

signals:
    void modeChanged(Mode newMode);

private:
    enum class PendingMarkOp
    {
        None,
        Set,
        Jump,
    };

    bool handleKeyPress(QKeyEvent* ev);
    [[nodiscard]] bool shouldSkipBindings(QObject* watched) const;
    [[nodiscard]] bool wouldHandleFromConfig(QKeyEvent* ev, Mode mode) const;
    [[nodiscard]] bool wouldHandleFromBindings(QKeyEvent* ev, const QList<BindingEntry>& bindings) const;
    [[nodiscard]] bool hasPendingInput() const;
    [[nodiscard]] bool pendingConfigPrefixMatches(const BindingEntry& entry) const;

    bool dispatchFromConfig(QKeyEvent* ev, Mode mode);
    bool dispatchFromBindings(QKeyEvent* ev, BindingScope scope, const QList<BindingEntry>& bindings);
    void executeAction(const BindingEntry& entry);
    bool handlePendingMarkOp(QKeyEvent* ev);
    void clearPendingInputState();
    void setPendingKey(QChar key);
    void setPendingMarkOp(PendingMarkOp op);
    void refreshPendingTimeout();
    void applyBackendBindings();

    void commitFilter();
    void cancelFilter();
    void onFilterTextChanged(const QString& text);

    void commitSearch();
    void cancelSearch();
    void onSearchTextChanged(const QString& text);
    void buildMatchList(const QString& pattern);
    void refreshSearchMatches();
    void jumpToMatch(int idx);

    [[nodiscard]] int halfPageDelta() const;
    void treeMoveCursor(QTreeView* tree, int delta);

    struct PlaylistSnapshot
    {
        Fooyin::UId playlistId;
        Fooyin::PlaylistTrackList tracks;
    };

    void pushUndoEntry(Fooyin::UId playlistId, Fooyin::PlaylistTrackList before, Fooyin::PlaylistTrackList after,
                       int cursorBefore, int cursorAfter, int col);
    void pushUndoEntry(std::vector<PlaylistSnapshot> before, std::vector<PlaylistSnapshot> after, int cursorBefore,
                       int cursorAfter, int col, int rowCountBefore, int rowCountAfter,
                       Fooyin::UId cursorBeforeEntryId = {}, Fooyin::UId cursorAfterEntryId = {});
    void applyPlaylistSnapshots(const std::vector<PlaylistSnapshot>& snapshots) const;

    void setLocalMark(QChar mark);
    void jumpToLocalMark(QChar mark);
    [[nodiscard]] Fooyin::Playlist* selectedPlaylist() const;
    [[nodiscard]] Fooyin::UId currentTrackEntryId() const;
    [[nodiscard]] std::vector<VimClipboard::MarkTransfer> takeCutMarks(Fooyin::Playlist* playlist, int startRow,
                                                                       int endRow);
    [[nodiscard]] ViewContext activeViewContext() const;
    [[nodiscard]] BindingScope bindingScopeForView(QAbstractItemView* view) const;
    [[nodiscard]] BindingScope activeBindingScope() const;
    [[nodiscard]] Fooyin::Playlist* targetPlaylist() const;
    [[nodiscard]] Fooyin::FyWidget* findEnclosingFyWidget(QAbstractItemView* view) const;
    [[nodiscard]] bool organiserEditorActive(QObject* watched = nullptr) const;
    [[nodiscard]] std::optional<std::pair<int, int>> selectedTrackRowRange(Fooyin::Playlist* playlist);
    void scheduleOrganiserInsertedSelection(QTreeView* tree, const QModelIndex& parent, int row);
    void insertSelectionAfterCurrentPlaying(bool move);
    bool triggerCurrentContextAction(const Fooyin::Id& id) const;

    void scheduleIndexRestore(QAbstractItemView* view, int row, int col, int expectedRowCount);
    void scheduleEntryRestore(QAbstractItemView* view, const Fooyin::UId& playlistId, const Fooyin::UId& entryId,
                              int fallbackRow, int col, int expectedRowCount);

    Mode m_mode{Mode::Normal};
    int m_count{0};
    QChar m_pendingKey;
    int m_visualAnchor{-1};
    int m_visualCursor{-1};
    bool m_suppressFilter{false};

    ViewLocator* m_viewLocator{nullptr};
    SpatialNavigator* m_spatialNavigator{nullptr};

    struct UndoEntry
    {
        std::vector<PlaylistSnapshot> before;
        std::vector<PlaylistSnapshot> after;
        int cursorBefore{-1};
        int cursorAfter{-1};
        int col{0};
        int rowCountBefore{0};
        int rowCountAfter{0};
        Fooyin::UId cursorBeforeEntryId;
        Fooyin::UId cursorAfterEntryId;
    };

    VimClipboard m_clipboard;
    Fooyin::ActionManager* m_actionManager{nullptr};
    Fooyin::PlaylistHandler* m_playlistHandler{nullptr};
    Fooyin::SettingsManager* m_settingsManager{nullptr};
    VimMotionsBindingBackend* m_settingsBackend{nullptr};
    std::unique_ptr<VimMotionsBindingBackend> m_ownedSettingsBackend;
    QMetaObject::Connection m_backendBindingsChangedConnection;
    Fooyin::TrackSelectionController* m_trackSelectionController{nullptr};
    QPointer<Fooyin::PlaylistSelectionObserver> m_playlistSelectionObserver;
    QMetaObject::Connection m_playlistSelectionChangedConnection;
    Fooyin::UId m_observedSelectedPlaylistId;

    PendingMarkOp m_pendingMarkOp{PendingMarkOp::None};
    QHash<Fooyin::UId, QHash<QChar, Fooyin::UId>> m_localMarks;

    std::vector<UndoEntry> m_undoStack;
    int m_undoIndex{-1};

    QPointer<VimSearchBar> m_filterBar;
    QPointer<Fooyin::FyWidget> m_filterTarget;
    QString m_lastFilter;

    QPointer<VimSearchBar> m_searchBar;
    QPointer<QAbstractItemView> m_searchView;
    std::vector<QPersistentModelIndex> m_searchMatches;
    int m_searchMatchIdx{-1};
    QPersistentModelIndex m_preSearchIndex;
    QString m_lastSearchPattern;

    VimActions m_actions;
    bool m_useDefaultBindings{true};
    bool m_useVimMotionsInSettings{false};
    bool m_wrapScan{true};
    int m_dispatchCount{0};
    bool m_hadExplicitCount{false};
    ModeConfigBindings m_configBindings;
    QList<KeyCombo> m_pendingConfigSequence;
    std::optional<BindingEntry> m_pendingConfigFallback;
    std::optional<BindingScope> m_pendingConfigScope;
    int m_pendingSequenceTimeoutMs{0};
    QTimer m_pendingTimeoutTimer;
};

} // namespace Fooyin::VimMotions
