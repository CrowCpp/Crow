#include <cstdint>
#include <fuzzer/FuzzedDataProvider.h>
#include <string>

#include "crow/query_string.h"

// Fuzz harness for crow's URL query-string parser (query_string.h: qs_parse,
// qs_kv_sort, qs_decode - the in-place percent-decoding of keys and values).
//
// Threat model: query strings arrive percent-encoded from the network on every
// request URL and body (crow::request::url_params / get_body_params); malformed
// escapes, duplicate keys and bracket keys reach the parser directly. query_string.h
// was largely dark (314 of 389 lines uncovered).
//
// Design: both decode modes are driven (url=true decodes values in place; url=false
// keeps raw values and exercises a different split path), then the parsed pairs are
// walked via keys()/get()/get_list()/get_list(...) including the bracket-handling
// variants. The parsed string is the bounded fuzz input itself, and query_string
// caps itself at MAX_KEY_VALUE_PAIRS_COUNT pairs, so per-iteration allocations stay
// bounded and the target cannot OOM on fuzzed lengths.

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    FuzzedDataProvider fdp{data, size};

    std::string input = fdp.ConsumeRandomLengthString();
    if (input.empty())
        return 0;
    const bool decode = fdp.ConsumeIntegralInRange<std::uint8_t>(0, 1) != 0;

    try
    {
        crow::query_string qs(input, decode);

        for (const auto& key : qs.keys())
        {
            if (const char* single = qs.get(key))
                (void)*single;
            for (char* value : qs.get_list(key))
                (void)*value;
            for (char* value : qs.get_list(key, false))
                (void)*value;
        }
    }
    catch (const std::exception&)
    {
        // Parsing failures must not escape the harness.
    }

    return 0;
}
