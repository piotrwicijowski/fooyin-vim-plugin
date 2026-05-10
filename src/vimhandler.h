#pragma once

#include "searchnavigation.h"
#include "vimactions.h"
#include "vimbindingparser.h"
#include "vimclipboard.h"

#include <QObject>
#include <QPointer>
#include <core/playlist/playlist.h>
#include <utils/id.h>

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
    void setTrackSelectionController(Fooyin::TrackSelectionController* controller);

    [[nodiscard]] bool eventFilter(QObject* watched, QEvent* event) override;

    // Testing support
    void rebuildBindings();
    [[nodiscard]] ViewContext viewContext(QAbstractItemView* view) const;
    [[nodiscard]] const QHash<Mode, QList<BindingEntry>>& configBindings() const
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
    void undo();
    void redo();

    void focusNowPlaying();
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
    bool handleKeyPress(QKeyEvent* ev);
    bool handleNormalKey(QKeyEvent* ev);
    bool handleVisualKey(QKeyEvent* ev);
    [[nodiscard]] bool wouldHandleNormal(QKeyEvent* ev) const;
    [[nodiscard]] bool wouldHandleVisual(QKeyEvent* ev) const;
    [[nodiscard]] bool wouldHandleFromConfig(QKeyEvent* ev, Mode mode) const;

    bool dispatchFromConfig(QKeyEvent* ev, Mode mode);
    void executeAction(const BindingEntry& entry);
    bool handlePendingMarkOp(QKeyEvent* ev);

    void commitFilter();
    void cancelFilter();
    void onFilterTextChanged(const QString& text);

    void commitSearch();
    void cancelSearch();
    void onSearchTextChanged(const QString& text);
    void buildMatchList(const QString& pattern);
    void jumpToMatch(int idx);

    [[nodiscard]] int halfPageDelta() const;
    void treeMoveCursor(QTreeView* tree, int delta);

    void pushUndoEntry(Fooyin::UId playlistId, Fooyin::PlaylistTrackList before, Fooyin::PlaylistTrackList after,
                       int cursorBefore, int cursorAfter, int col);

    void setLocalMark(QChar mark);
    void jumpToLocalMark(QChar mark);
    [[nodiscard]] Fooyin::Playlist* selectedPlaylist() const;
    [[nodiscard]] Fooyin::UId currentTrackEntryId() const;
    [[nodiscard]] std::vector<VimClipboard::MarkTransfer> takeCutMarks(Fooyin::Playlist* playlist, int startRow,
                                                                       int endRow);
    [[nodiscard]] ViewContext activeViewContext() const;
    [[nodiscard]] Fooyin::Playlist* targetPlaylist() const;
    [[nodiscard]] Fooyin::FyWidget* findEnclosingFyWidget(QAbstractItemView* view) const;
    [[nodiscard]] bool organiserEditorActive(QObject* watched = nullptr) const;
    bool triggerCurrentContextAction(const Fooyin::Id& id) const;

    void scheduleIndexRestore(QAbstractItemView* view, int row, int col, int expectedRowCount);

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
        Fooyin::UId playlistId;
        Fooyin::PlaylistTrackList before;
        Fooyin::PlaylistTrackList after;
        int cursorBefore{-1};
        int cursorAfter{-1};
        int col{0};
    };

    VimClipboard m_clipboard;
    Fooyin::ActionManager* m_actionManager{nullptr};
    Fooyin::PlaylistHandler* m_playlistHandler{nullptr};
    Fooyin::SettingsManager* m_settingsManager{nullptr};
    Fooyin::TrackSelectionController* m_trackSelectionController{nullptr};

    enum class PendingMarkOp
    {
        None,
        Set,
        Jump,
    };

    PendingMarkOp m_pendingMarkOp{PendingMarkOp::None};
    QHash<Fooyin::UId, QHash<QChar, Fooyin::UId>> m_localMarks;

    std::vector<UndoEntry> m_undoStack;
    int m_undoIndex{-1};

    QPointer<VimSearchBar> m_filterBar;
    QPointer<Fooyin::FyWidget> m_filterTarget;
    QString m_lastFilter;

    QPointer<VimSearchBar> m_searchBar;
    QPointer<QAbstractItemView> m_searchView;
    std::vector<int> m_searchMatches;
    int m_searchMatchIdx{-1};
    int m_preSearchRow{-1};
    QString m_lastSearchPattern;

    VimActions m_actions;
    bool m_useConfigBindings{false};
    bool m_useDefaultBindings{true};
    bool m_wrapScan{true};
    int m_dispatchCount{0};
    bool m_hadExplicitCount{false};
    QHash<Mode, QList<BindingEntry>> m_configBindings;
};

} // namespace Fooyin::VimMotions
