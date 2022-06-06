#pragma once

#include <boost/algorithm/string/predicate.hpp>
#include <boost/functional/hash.hpp>
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
            {
                boost::hash_combine(seed, std::toupper(c, locale));
            }

            return seed;
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
