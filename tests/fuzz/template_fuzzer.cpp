#include <cstdint>
#include <fuzzer/FuzzedDataProvider.h>

#include "crow.h"

static crow::mustache::context build_context_object(FuzzedDataProvider &fdp)
{
    crow::mustache::context ctx{};

    for (auto i = 0; i < fdp.ConsumeIntegralInRange(0, 10); ++i)
    {
        ctx[fdp.ConsumeRandomLengthString()] = fdp.ConsumeRandomLengthString();
    }

    return ctx;
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size)
{
    FuzzedDataProvider fdp{data, size};
    try
    {
        auto page = crow::mustache::compile(fdp.ConsumeRandomLengthString());
        auto ctx = build_context_object(fdp);
        page.render_string(ctx);
    }
    catch (const std::exception& e)
    {
        // No special handling for invalid inputs or rendering errors
    }

    return 0;
}
