#pragma once
#include <stdexcept>

namespace crow
{
    struct bad_request : public std::runtime_error
    {
        explicit bad_request(const std::string& what_arg)
            : std::runtime_error(what_arg) {}

        explicit bad_request(const char* what_arg)
            : std::runtime_error(what_arg) {}
    };
}