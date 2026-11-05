#include <cstdint>
#include <string>
#include "crow/json.h"

// Fuzz harness for crow's JSON parser and serializer.
//
// Threat model: crow::json::load parses attacker-controlled strings (request bodies,
// query values) and crow::json::wvalue serializes them back out. json.h is the
// highest-dark crow file (1024 of 1899 lines uncovered): the original harness only
// exercised load() plus Object keys()/operator[] and List iteration.
//
// This extension additionally drives:
// - the primitive accessors (i/d/b/s) on their matching parsed types,
// - the serializer (dump(), dump(indent, separator); dump_string escape handling and
//   number formatting were dark) via the documented rvalue -> wvalue conversion,
// - a re-load of the serialized output to connect serializer and parser paths.
//
// Memory discipline: the input is the fuzzed buffer itself (bounded by -max_len), so
// allocations driven by fuzzed lengths stay bounded. Exceptions from invalid JSON are
// the parser's documented error channel and are caught.

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0) {
        return 0;
    }

    std::string input(reinterpret_cast<const char*>(data), size);
    try {
        crow::json::rvalue json_val = crow::json::load(input);
        if (json_val) {
            const crow::json::type t = json_val.t();
            if (t == crow::json::type::Object) {
                for (const auto& key : json_val.keys()) {
                    auto& val = json_val[key];
                    (void)val.t();
                }
            } else if (t == crow::json::type::List) {
                for (const auto& val : json_val) {
                    (void)val.t();
                }
            } else if (t == crow::json::type::String) {
                (void)json_val.s().size();
            }

            switch (t) {
                case crow::json::type::Number:
                    (void)json_val.i();
                    (void)json_val.d();
                    break;
                case crow::json::type::True:
                case crow::json::type::False: (void)json_val.b(); break;
                default: break;
            }

            // Serializer paths: escape handling, number formatting, object/list dump.
            crow::json::wvalue w(json_val);
            std::string compact = w.dump();
            std::string pretty = w.dump(2, ' ');
            (void)compact.size();
            (void)pretty.size();

            // Re-load the serialization to reach the parser from serializer output.
            if (!compact.empty()) {
                crow::json::rvalue reloaded = crow::json::load(compact);
                (void)reloaded.t();
            }
        }
    } catch (...) {
        // Ignore exceptions from invalid JSON: documented parser error channel.
    }

    return 0;
}

