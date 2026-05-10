#pragma once

#include "searchnavigation.h"
#include "vimactions.h"
#include "vimbindingparser.h"
#include "vimclipboard.h"

#include <core/playlist/playlist.h>
#include <QObject>
#include <QPointer>
#include <utils/id.h>

#include <vector>

class QAbstractItemView;
class QKeyEvent;
class QTreeView;

namespace Fooyin {
class ActionManager;
class FyWidget;
class Playlist;
class PlaylistHandler;
class SettingsManager;
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
    enum class Mode { Normal, Visual, Insert, Filter, Search };

    explicit VimHandler(QObject* parent = nullptr);
    ~VimHandler() override;

    [[nodiscard]] Mode mode() const;

    void setPlaylistHandler(Fooyin::PlaylistHandler* handler);
    void setActionManager(Fooyin::ActionManager* manager);
    void setSettingsManager(Fooyin::SettingsManager* manager);

    [[nodiscard]] bool eventFilter(QObject* watched, QEvent* event) override;

    // Testing support
    void rebuildBindings();
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

    void enterInsert();
    void enterVisual();
    void enterNormal();
    void enterFilter();

    void deleteRows(int count);
    void yankRows(int count);
    void pasteAfter();
    void pasteBefore();
    void undo();
    void redo();

    void focusNowPlaying();

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

    void pushUndoEntry(Fooyin::UId playlistId, Fooyin::PlaylistTrackList before,
                       Fooyin::PlaylistTrackList after, int cursorBefore, int cursorAfter, int col);

    [[nodiscard]] Fooyin::Playlist* targetPlaylist() const;
    [[nodiscard]] Fooyin::FyWidget* findEnclosingFyWidget(QAbstractItemView* view) const;

    void scheduleIndexRestore(QAbstractItemView* view, int row, int col, int expectedRowCount);

    Mode  m_mode{Mode::Normal};
    int   m_count{0};
    QChar m_pendingKey;
    int   m_visualAnchor{-1};
    int   m_visualCursor{-1};
    bool  m_suppressFilter{false};

    ViewLocator*      m_viewLocator{nullptr};
    SpatialNavigator* m_spatialNavigator{nullptr};

    struct UndoEntry {
        Fooyin::UId               playlistId;
        Fooyin::PlaylistTrackList before;
        Fooyin::PlaylistTrackList after;
        int                       cursorBefore{-1};
        int                       cursorAfter{-1};
        int                       col{0};
    };

    VimClipboard              m_clipboard;
    Fooyin::ActionManager*    m_actionManager{nullptr};
    Fooyin::PlaylistHandler*  m_playlistHandler{nullptr};
    Fooyin::SettingsManager*  m_settingsManager{nullptr};

    std::vector<UndoEntry> m_undoStack;
    int                    m_undoIndex{-1};

    QPointer<VimSearchBar>     m_filterBar;
    QPointer<Fooyin::FyWidget> m_filterTarget;
    QString                    m_lastFilter;

    QPointer<VimSearchBar>      m_searchBar;
    QPointer<QAbstractItemView> m_searchView;
    std::vector<int>            m_searchMatches;
    int                         m_searchMatchIdx{-1};
    int                         m_preSearchRow{-1};
    QString                     m_lastSearchPattern;

    VimActions m_actions;
    bool m_useConfigBindings{false};
    bool m_useDefaultBindings{true};
    bool m_wrapScan{true};
    int m_dispatchCount{0};
    bool m_hadExplicitCount{false};
    QHash<Mode, QList<BindingEntry>> m_configBindings;
};

} // namespace Fooyin::VimMotions
