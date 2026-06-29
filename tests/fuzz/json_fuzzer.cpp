#include <cstdint>
#include <string>
#include "crow/json.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0) {
        return 0;
    }

    std::string input(reinterpret_cast<const char*>(data), size);
    try {
        crow::json::rvalue json_val = crow::json::load(input);
        if (json_val) {
            // If parsing succeeded, try to access some properties to trigger more code
            auto t = json_val.t();
            if (t == crow::json::type::Object) {
                for (const auto& key : json_val.keys()) {
                    auto& val = json_val[key];
                    (void)val.t();
                }
            } else if (t == crow::json::type::List) {
                for (const auto& val : json_val) {
                    (void)val.t();
                }
            }
            
            // Also try to dump it back to string
            // (void)crow::json::dump(json_val); // Need to check if dump exists
        }
    } catch (...) {
        // Ignore exceptions from invalid JSON
    }

    return 0;
}
