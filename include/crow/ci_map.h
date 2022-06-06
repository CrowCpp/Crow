#pragma once

#include <locale>
#include <unordered_map>
#include "crow/utility.h"

namespace crow
{
    /// Hashing function for ci_map (unordered_multimap).
    struct ci_hash
    {
        size_t operator()(const std::string& key) const
        {
            std::size_t seed = 0;
            std::locale locale;

            for (auto c : key)
                hash_combine(seed, std::toupper(c, locale));

            return seed;
        }

    private:
        static inline void hash_combine(std::size_t& seed, char v)
        {
            std::hash<char> hasher;
            seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
    };

    /// Equals function for ci_map (unordered_multimap).
    struct ci_key_eq
    {
        bool operator()(const std::string& l, const std::string& r) const
        {
            return utility::string_equals(l, r);
        }
    };

    using ci_map = std::unordered_multimap<std::string, std::string, ci_hash, ci_key_eq>;
} // namespace crow
