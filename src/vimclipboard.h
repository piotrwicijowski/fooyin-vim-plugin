#pragma once

#include <core/track.h>

#include <QChar>

#include <vector>

namespace Fooyin::VimMotions {

class VimClipboard
{
public:
    struct MarkTransfer
    {
        int offset{-1};
        QChar mark;
    };

    void yank(Fooyin::TrackList tracks);
    void cut(Fooyin::TrackList tracks, std::vector<MarkTransfer> markTransfers);

    [[nodiscard]] bool hasData() const;
    [[nodiscard]] const Fooyin::TrackList& tracks() const;
    [[nodiscard]] const std::vector<MarkTransfer>& markTransfers() const;
    void clearMarkTransfers();

private:
    Fooyin::TrackList m_tracks;
    std::vector<MarkTransfer> m_markTransfers;
};

} // namespace Fooyin::VimMotions
