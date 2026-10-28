#include "core/Json.h"

namespace bb {

std::vector<std::string> JsonStrings(std::string_view json, std::string_view key)
{
    std::vector<std::string> found;
    std::string quoted = "\"" + std::string(key) + "\"";
    for (size_t at = json.find(quoted); at != std::string_view::npos; at = json.find(quoted, at + 1)) {
        size_t i = at + quoted.size();
        auto skipSpace = [&] { while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) i++; };
        skipSpace();
        if (i >= json.size() || json[i] != ':') continue;   // (the key's name as a value, not a key)
        i++;
        skipSpace();
        if (i >= json.size() || json[i] != '"') continue;   // (a number, a list, ...)
        std::string value;
        bool closed = false;
        for (i++; i < json.size(); i++) {
            char c = json[i];
            if (c == '"') { closed = true; break; }
            if (c == '\\' && i + 1 < json.size()) {
                c = json[++i];
                if (c == 'n') c = '\n';
                else if (c == 't') c = '\t';
                else if (c == 'r') c = '\r';
            }
            value += c;
        }
        if (closed) found.push_back(std::move(value));
    }
    return found;
}

} // namespace bb
