#pragma once

#include <core/track.h>

namespace Fooyin::VimMotions {

class VimClipboard
{
public:
    void yank(Fooyin::TrackList tracks);

    [[nodiscard]] bool hasData() const;
    [[nodiscard]] const Fooyin::TrackList& tracks() const;

private:
    Fooyin::TrackList m_tracks;
};

} // namespace Fooyin::VimMotions
