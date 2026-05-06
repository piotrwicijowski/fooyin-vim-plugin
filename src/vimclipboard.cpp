#include "vimclipboard.h"

namespace Fooyin::VimMotions {

void VimClipboard::yank(Fooyin::TrackList tracks)
{
    m_tracks = std::move(tracks);
}

bool VimClipboard::hasData() const
{
    return !m_tracks.empty();
}

const Fooyin::TrackList& VimClipboard::tracks() const
{
    return m_tracks;
}

} // namespace Fooyin::VimMotions
