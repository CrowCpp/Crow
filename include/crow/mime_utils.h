#pragma once

#include <string>
#include "crow/logging.h"
#include "crow/mime_types.h"

namespace crow
{
    // *naive* validation of a mime type string
    inline bool validate_mime_type(const std::string& candidate)
    {
        // Here we simply check that the candidate type starts with
        // a valid parent type, and has at least one character afterwards.
        std::array<std::string, 10> valid_parent_types = {
          "application/", "audio/", "font/", "example/",
          "image/", "message/", "model/", "multipart/",
          "text/", "video/"};
        for (const std::string& parent : valid_parent_types)
        {
            // ensure the candidate is *longer* than the parent,
            // to avoid unnecessary string comparison and to
            // reject zero-length subtypes.
            if (candidate.size() <= parent.size())
            {
                continue;
            }
            // strncmp is used rather than substr to avoid allocation,
            // but a string_view approach would be better if Crow
            // migrates to C++17.
            if (strncmp(parent.c_str(), candidate.c_str(), parent.size()) == 0)
            {
                return true;
            }
        }
        return false;
    }

    // find the mime type from the content type either by lookup,
    // or by the content type itself if it is a valid a mime type.
    // Defaults to text/plain.
    inline std::string get_mime_type(const std::string& contentType)
    {
        const auto mimeTypeIterator = mime_types.find(contentType);
        if (mimeTypeIterator != mime_types.end())
        {
            return mimeTypeIterator->second;
        }
        else if (validate_mime_type(contentType))
        {
            return contentType;
        }
        else
        {
            CROW_LOG_WARNING << "Unable to interpret mime type for content type '" << contentType << "'. Defaulting to text/plain.";
            return "text/plain";
        }
    }
} // namespace crow
