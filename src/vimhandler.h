#pragma once

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
class FyWidget;
class Playlist;
class PlaylistHandler;
} // namespace Fooyin

namespace Fooyin::VimMotions {

class ViewLocator;
class SpatialNavigator;
class VimSearchBar;

class VimHandler : public QObject
{
    Q_OBJECT

public:
    enum class Mode { Normal, Visual, Insert, Filter };

    explicit VimHandler(QObject* parent = nullptr);
    ~VimHandler() override;

    [[nodiscard]] Mode mode() const;

    void setPlaylistHandler(Fooyin::PlaylistHandler* handler);

    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void modeChanged(Mode newMode);

private:
    bool handleKeyPress(QKeyEvent* ev);
    bool handleNormalKey(QKeyEvent* ev);
    bool handleVisualKey(QKeyEvent* ev);
    [[nodiscard]] bool wouldHandleNormal(QKeyEvent* ev) const;
    [[nodiscard]] bool wouldHandleVisual(QKeyEvent* ev) const;

    void enterNormal();
    void enterInsert();
    void enterVisual();
    void enterFilter();
    void commitFilter();
    void cancelFilter();
    void onFilterTextChanged(const QString& text);

    void moveCursor(int delta);
    void jumpToFirst();
    void jumpToLast();
    void jumpToRow(int row);
    void moveCursorHalfPage(int direction);
    void activateCurrentRow();
    void nextMatch();
    void prevMatch();

    void deleteRows(int count);
    void yankRows(int count);
    void deleteVisualSelection();
    void yankVisualSelection();
    void pasteAfter();
    void pasteBefore();
    void updateVisualSelection();
    void moveRows(int delta);
    void moveVisualSelection(int delta);

    void treeMoveCursor(QTreeView* tree, int delta);
    void treeMoveSibling(int delta);
    void treeOpenOrDescend();
    void treeCloseOrAscend();

    void pushUndoEntry(Fooyin::UId playlistId, Fooyin::PlaylistTrackList before,
                       Fooyin::PlaylistTrackList after, int cursorBefore, int cursorAfter, int col);
    void undo();
    void redo();

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

    VimClipboard             m_clipboard;
    Fooyin::PlaylistHandler* m_playlistHandler{nullptr};

    std::vector<UndoEntry> m_undoStack;
    int                    m_undoIndex{-1};

    QPointer<VimSearchBar>     m_filterBar;
    QPointer<Fooyin::FyWidget> m_filterTarget;
    QString                    m_lastFilter;
};

} // namespace Fooyin::VimMotions
