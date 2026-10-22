#include "playlistfuzzymatcher.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace Fooyin::VimMotions {

std::optional<PlaylistFuzzyScore> playlistFuzzyScore(const QString& name, const QString& query)
{
    if(query.isEmpty())
        return PlaylistFuzzyScore{};
    if(query.size() > name.size())
        return std::nullopt;

    constexpr int Unmatched = std::numeric_limits<int>::min() / 4;
    std::vector<int> previous(name.size(), Unmatched);
    std::vector<int> current(name.size(), Unmatched);
    for(qsizetype q = 0; q < query.size(); ++q) {
        std::fill(current.begin(), current.end(), Unmatched);
        int bestPrefix = Unmatched;
        for(qsizetype n = 0; n < name.size(); ++n) {
            // previous[k] - (n-k-1) penalises gaps. Keep the best previous[k]+k
            // so every possible alignment is considered without an inner scan.
            if(n > 0 && previous[n - 1] != Unmatched)
                bestPrefix = std::max(bestPrefix, previous[n - 1] + static_cast<int>(n - 1));
            if(name[n] != query[q])
                continue;
            const int boundary = (n == 0 || !name[n - 1].isLetterOrNumber()) ? 12 : 0;
            if(q == 0)
                current[n] = boundary - static_cast<int>(n);
            else if(bestPrefix != Unmatched) {
                current[n] = bestPrefix - static_cast<int>(n) + 1 + boundary;
                if(n > 0 && previous[n - 1] != Unmatched)
                    current[n] = std::max(current[n], previous[n - 1] + 16 + boundary);
            }
        }
        previous.swap(current);
    }
    const int best = *std::max_element(previous.begin(), previous.end());
    if(best == Unmatched)
        return std::nullopt;
    const int category = name == query ? 4 : name.startsWith(query) ? 3 : name.contains(query) ? 2 : 1;
    return PlaylistFuzzyScore{category, best};
}

} // namespace Fooyin::VimMotions
