#pragma once

#include <string_view>
#include <locale>
#include <unordered_map>

#include "crow/utility.h"

namespace crow
{
    /// Hashing function for ci_map (unordered_multimap).
    struct ci_hash
    {
        size_t operator()(const std::string_view key) const
        {
            std::size_t seed = 0;

            for (auto c : key) {
                std::hash<char> hasher;
                seed ^= hasher(std::toupper(c)) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            }
            return seed;
        }
    };

    /// Equals function for ci_map (unordered_multimap).
    struct ci_key_eq
    {
        bool operator()(const std::string_view l, const std::string_view r) const
        {
            return (l.length() == r.length())
#ifdef _MSC_VER
                   && (::_strnicmp(l.data(), r.data(), l.length())==0);
#else
                   && (::strncasecmp(l.data(), r.data(), l.length())==0);
#endif
        }
    };

    using ci_map = std::unordered_multimap<std::string, std::string, ci_hash, ci_key_eq>;
} // namespace crow
