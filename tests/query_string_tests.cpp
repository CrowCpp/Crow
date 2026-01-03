
#include <iostream>
#include <vector>

#include "catch2/catch_all.hpp"
#include "crow/query_string.h"

namespace
{
    std::string buildQueryStr(const std::vector<std::pair<std::string, std::string>>& paramList)
    {
        std::string paramsStr{};
        for (const auto& param : paramList)
            paramsStr.append(param.first).append(1, '=').append(param.second).append(1, '&');
        if (!paramsStr.empty())
            paramsStr.resize(paramsStr.size() - 1);
        return paramsStr;
    }
}

TEST_CASE( "empty query params" )
{
    const crow::query_string query_params("");
    const std::vector<std::string> keys = query_params.keys();

    REQUIRE(keys.empty() == true);
}

TEST_CASE( "query string keys" )
{
    const std::vector<std::pair<std::string, std::string>> params {
      {"foo", "bar"}, {"mode", "night"}, {"page", "2"},
      {"tag", "js"}, {"name", "John Smith"}, {"age", "25"},
    };

    const crow::query_string query_params("params?" + buildQueryStr(params));
    const std::vector<std::string> keys = query_params.keys();

    for (const auto& entry: params)
    {
        const bool exist = std::any_of(keys.cbegin(), keys.cend(), [&](const std::string& key) {
            return key == entry.first;});
        REQUIRE(exist == true);
    }
}

TEST_CASE("query string decoding in get_dict")
{
    const std::vector<std::pair<std::string, std::string>> params{
      {"foo%5Bkey1%5D", "bar1"},
      {"foo%5Bkey2%5D", "bar2"},
      {"foo%5B%5D", "bar3"},
      {"foo%5B%5D", "bar4"},
      {"foo%5B%5Bkey%5D", "bar%5B%1%5D"}};
    const crow::query_string query_params("params?" + buildQueryStr(params));
    auto map = query_params.get_dict("foo");
    REQUIRE(map["key1"] == "bar1");
    REQUIRE(map["key2"] == "bar2");
    REQUIRE(map[""] == "bar3");
    REQUIRE(map["[key"] == "bar[");
}