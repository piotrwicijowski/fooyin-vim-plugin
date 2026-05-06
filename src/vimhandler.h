#pragma once

#include "vimclipboard.h"

#include <QObject>

class QAbstractItemView;
class QKeyEvent;

namespace Fooyin {
class Playlist;
class PlaylistHandler;
} // namespace Fooyin

namespace Fooyin::VimMotions {

class ViewLocator;
class SpatialNavigator;

class VimHandler : public QObject
{
    Q_OBJECT

public:
    enum class Mode { Normal, Visual, Insert };

    explicit VimHandler(QObject* parent = nullptr);

    [[nodiscard]] Mode mode() const;

    void setPlaylistHandler(Fooyin::PlaylistHandler* handler);

    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void modeChanged(Mode newMode);

private:
    bool handleKeyPress(QKeyEvent* ev);
    bool handleNormalKey(QKeyEvent* ev);
    bool handleVisualKey(QKeyEvent* ev);

    void enterNormal();
    void enterInsert();
    void enterVisual();

    void moveCursor(int delta);
    void jumpToFirst();
    void jumpToLast();
    void jumpToRow(int row);
    void moveCursorHalfPage(int direction);
    void activateCurrentRow();

    void deleteRows(int count);
    void yankRows(int count);
    void deleteVisualSelection();
    void yankVisualSelection();
    void pasteAfter();
    void pasteBefore();
    void updateVisualSelection();
    void moveRows(int delta);
    void moveVisualSelection(int delta);

    // Returns the playlist the visible PlaylistView is showing. Uses
    // activePlaylist() first (the playing one); falls back to matching by
    // row count, then to the first available playlist.
    [[nodiscard]] Fooyin::Playlist* targetPlaylist() const;

    // Defers a ClearAndSelect on (row, col) until the model has settled at
    // expectedRowCount rows. Handles both the sync in-memory reset and a
    // possible second async database-write reset from fooyin.
    void scheduleIndexRestore(QAbstractItemView* view, int row, int col, int expectedRowCount);

    Mode  m_mode{Mode::Normal};
    int   m_count{0};
    QChar m_pendingKey;
    int   m_visualAnchor{-1};
    int   m_visualCursor{-1};
    bool  m_suppressFilter{false};

    ViewLocator*      m_viewLocator{nullptr};
    SpatialNavigator* m_spatialNavigator{nullptr};

    VimClipboard             m_clipboard;
    Fooyin::PlaylistHandler* m_playlistHandler{nullptr};
};

} // namespace Fooyin::VimMotions
