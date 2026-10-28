#pragma once
// Just enough JSON to read GitHub's release info (no library, no regex): the string values given for a key.
#include <string>
#include <string_view>
#include <vector>

namespace bb {

// Every string value of "key" in the text, in order; escapes undone (all but \u, which no URL or tag has).
std::vector<std::string> JsonStrings(std::string_view json, std::string_view key);

// The first of them that wanted accepts, or "".
template <typename Wanted> std::string JsonFirst(std::string_view json, std::string_view key, Wanted wanted)
{
    for (std::string& v : JsonStrings(json, key))
        if (wanted(std::string_view(v))) return v;
    return {};
}

} // namespace bb
