#pragma once

#include <QString>
#include <compare>
#include <optional>

namespace Fooyin::VimMotions {

struct PlaylistFuzzyScore
{
    int category{0};
    int alignment{0};

    auto operator<=>(const PlaylistFuzzyScore&) const = default;
};

// Inputs are case-folded; the query is also trimmed. Higher scores rank first.
// O(query length * name length) time and O(name length) memory.
[[nodiscard]] std::optional<PlaylistFuzzyScore> playlistFuzzyScore(const QString& foldedName,
                                                                   const QString& foldedQuery);

} // namespace Fooyin::VimMotions
