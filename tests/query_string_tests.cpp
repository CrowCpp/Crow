
#include <cstring>
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

TEST_CASE( "malformed percent encoding - trailing percent" )
{
    // URL ending with '%' should not cause heap overflow (Issue #1126)
    crow::query_string qs1("https://example.com/asdf?%");
    auto keys1 = qs1.keys();
    // Should handle gracefully without crashing
    if (!keys1.empty()) {
        (void)qs1.get(keys1[0]);
    }

    // URL with single hex digit after %
    crow::query_string qs2("https://example.com/?key=%2");
    auto keys2 = qs2.keys();
    if (!keys2.empty()) {
        (void)qs2.get(keys2[0]);
    }

    // URL with % followed by non-hex characters
    crow::query_string qs3("https://example.com/?key=%GG");
    auto keys3 = qs3.keys();
    if (!keys3.empty()) {
        (void)qs3.get(keys3[0]);
    }

    // Valid percent encoding should still work
    crow::query_string qs4("https://example.com/?key=%20value");
    auto val = qs4.get("key");
    REQUIRE(val != nullptr);
    REQUIRE(std::string(val) == " value");

    REQUIRE(true); // Test passes if no crash/sanitizer error
}

TEST_CASE( "qs_scanvalue truncates and terminates a value longer than the buffer" )
{
    char val[8];

    // value longer than the buffer: truncated to sizeof(val) - 1 characters and terminated
    std::memset(val, 'x', sizeof(val));
    REQUIRE(crow::qs_scanvalue("key", "/path?key=0123456789abcdef&other=1", val, sizeof(val)) != nullptr);
    REQUIRE(std::string(val) == "0123456");

    // value exactly sizeof(val) - 1 characters long
    std::memset(val, 'x', sizeof(val));
    REQUIRE(crow::qs_scanvalue("key", "/path?key=abcdefg", val, sizeof(val)) != nullptr);
    REQUIRE(std::string(val) == "abcdefg");

    // value that fits, followed by another pair
    std::memset(val, 'x', sizeof(val));
    REQUIRE(crow::qs_scanvalue("key", "/path?key=abc&other=1", val, sizeof(val)) != nullptr);
    REQUIRE(std::string(val) == "abc");

    // value that fits is still decoded up to the fragment
    std::memset(val, 'x', sizeof(val));
    REQUIRE(crow::qs_scanvalue("key", "/path?key=a+b%20c#frag", val, sizeof(val)) != nullptr);
    REQUIRE(std::string(val) == "a b c");

    // truncation inside a percent escape keeps only the complete characters
    std::memset(val, 'x', sizeof(val));
    REQUIRE(crow::qs_scanvalue("key", "/path?key=abcde%41%42", val, sizeof(val)) != nullptr);
    REQUIRE(std::string(val) == "abcde");

    // a one byte buffer only holds the terminator
    std::memset(val, 'x', sizeof(val));
    REQUIRE(crow::qs_scanvalue("key", "/path?key=abc", val, 1) != nullptr);
    REQUIRE(val[0] == '\0');

    // a zero length buffer is left untouched
    char empty[2] = {'x', '\0'};
    REQUIRE(crow::qs_scanvalue("key", "/path?key=abc", empty, 0) != nullptr);
    REQUIRE(empty[0] == 'x');
}