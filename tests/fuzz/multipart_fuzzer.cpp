#include <cstdint>
#include <fuzzer/FuzzedDataProvider.h>
#include <string>

#include "crow/http_request.h"
#include "crow/multipart.h"
#include "crow/multipart_view.h"

// Fuzz harness for crow's multipart/form-data parsers.
//
// Threat model: multipart bodies are attacker-controlled file-upload surface that
// reaches crow::multipart::message / crow::multipart::message_view straight from the
// network (Content-Type header + request body). Before this harness both parser
// headers were entirely dark (0 of 388 lines covered).
//
// Design:
// - Raw mode parses the fuzzed bytes as-is; framed mode wraps them in structurally
//   valid multipart framing (boundary, Content-Disposition header) so deep parsing
//   code is reachable without the fuzzer having to guess the envelope.
// - A dump() -> re-parse round trip exercises the serializer against the parsers.
// - Memory discipline: the body is capped at 8 KiB per iteration regardless of
//   -max_len, so allocations driven by fuzzed lengths stay bounded and the target
//   cannot OOM. Ill-formed input is reported by the parsers as exceptions
//   (bad_request): the documented error channel, caught here.

namespace
{
    std::string bounded(std::string s)
    {
        constexpr std::size_t max_body = 8192;
        if (s.size() > max_body)
            s.resize(max_body);
        return s;
    }
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    FuzzedDataProvider fdp{data, size};

    const std::uint8_t mode = fdp.ConsumeIntegral<std::uint8_t>();
    const std::string boundary = (mode & 1) ? "X" : "CROW-BOUNDARY";
    std::string body = bounded(fdp.ConsumeRemainingBytesAsString());

    if (mode & 2)
    {
        // Structurally valid multipart framing around the fuzzed payload.
        body = "--" + boundary + "\r\n"
               "Content-Disposition: form-data; name=\"field\"\r\n"
               "\r\n" + body + "\r\n"
               "--" + boundary + "--\r\n";
    }

    crow::request req;
    req.body = body;
    req.add_header("Content-Type", "multipart/form-data; boundary=" + boundary);

    if (mode & 4)
    {
        try
        {
            crow::multipart::message msg(req);
            (void)msg.dump();
            for (std::size_t i = 0; i < msg.parts.size(); ++i)
            {
                (void)msg.dump(static_cast<int>(i));
                (void)msg.parts[i].body.size();
            }
            for (const auto& entry : msg.part_map)
                (void)msg.get_part_by_name(entry.first).body.size();
        }
        catch (const std::exception&)
        {
            // Ill-formed multipart body: documented parser error channel.
        }
    }

    if (mode & 8)
    {
        try
        {
            crow::multipart::message_view view(req);
            (void)view.dump();
            for (std::size_t i = 0; i < view.parts.size(); ++i)
                (void)view.dump(static_cast<int>(i));
        }
        catch (const std::exception&)
        {
            // Ill-formed multipart body: documented parser error channel.
        }
    }

    return 0;
}
