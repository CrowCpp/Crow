#pragma once

#include <string>

namespace http
{
    /// An abstract class that allows any other class to be returned by a handler.
    struct returnable
    {
        std::string content_type;
        virtual std::string dump() const = 0;

        returnable(std::string ctype):
          content_type{ctype}
        {}

        virtual ~returnable(){};
    };
} // namespace http
