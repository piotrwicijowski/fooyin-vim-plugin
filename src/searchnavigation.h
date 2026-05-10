#pragma once

#include <cstddef>
#include <vector>

namespace Fooyin::VimMotions {

[[nodiscard]] inline int firstSearchMatchIndex(const std::vector<int>& matches, int startRow, bool wrapScan)
{
    for(int i = 0; i < static_cast<int>(matches.size()); ++i) {
        if(matches[static_cast<size_t>(i)] >= startRow)
            return i;
    }

    return wrapScan && !matches.empty() ? 0 : -1;
}

[[nodiscard]] inline int nextSearchMatchIndexForRow(const std::vector<int>& matches, int currentRow, bool wrapScan)
{
    for(int i = 0; i < static_cast<int>(matches.size()); ++i) {
        if(matches[static_cast<size_t>(i)] >= currentRow)
            return i;
    }

    return wrapScan && !matches.empty() ? 0 : -1;
}

[[nodiscard]] inline int prevSearchMatchIndexForRow(const std::vector<int>& matches, int currentRow, bool wrapScan)
{
    for(int i = static_cast<int>(matches.size()) - 1; i >= 0; --i) {
        if(matches[static_cast<size_t>(i)] <= currentRow)
            return i;
    }

    return wrapScan && !matches.empty() ? static_cast<int>(matches.size()) - 1 : -1;
}

[[nodiscard]] inline int nextSearchMatchIndex(int currentIdx, int matchCount, bool wrapScan)
{
    if(matchCount <= 0)
        return -1;
    if(currentIdx < 0)
        return 0;
    if(currentIdx + 1 < matchCount)
        return currentIdx + 1;

    return wrapScan ? 0 : -1;
}

[[nodiscard]] inline int prevSearchMatchIndex(int currentIdx, int matchCount, bool wrapScan)
{
    if(matchCount <= 0)
        return -1;
    if(currentIdx < 0)
        return matchCount - 1;
    if(currentIdx > 0)
        return currentIdx - 1;

    return wrapScan ? matchCount - 1 : -1;
}

} // namespace Fooyin::VimMotions
