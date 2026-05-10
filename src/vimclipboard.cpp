#include "vimclipboard.h"

namespace Fooyin::VimMotions {

void VimClipboard::yank(Fooyin::TrackList tracks)
{
    m_tracks = std::move(tracks);
    m_markTransfers.clear();
}

void VimClipboard::cut(Fooyin::TrackList tracks, std::vector<MarkTransfer> markTransfers)
{
    m_tracks        = std::move(tracks);
    m_markTransfers = std::move(markTransfers);
}

bool VimClipboard::hasData() const
{
    return !m_tracks.empty();
}

const Fooyin::TrackList& VimClipboard::tracks() const
{
    return m_tracks;
}

const std::vector<VimClipboard::MarkTransfer>& VimClipboard::markTransfers() const
{
    return m_markTransfers;
}

void VimClipboard::clearMarkTransfers()
{
    m_markTransfers.clear();
}

} // namespace Fooyin::VimMotions
