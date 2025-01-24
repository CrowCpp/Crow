#pragma once
#include <string>
#include <unordered_map>
#include <ios>
#include <fstream>
#include <sstream>
// S_ISREG is not defined for windows
// This defines it like suggested in https://stackoverflow.com/a/62371749
#if defined(_MSC_VER)
#define _CRT_INTERNAL_NONSTDC_NAMES 1
#endif
#include <sys/stat.h>
#if !defined(S_ISREG) && defined(S_IFMT) && defined(S_IFREG)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

#include "crow/http_request.h"
#include "crow/ci_map.h"
#include "crow/socket_adaptors.h"
#include "crow/logging.h"
#include "crow/mime_types.h"
#include "crow/returnable.h"


namespace crow
{
    template<typename Adaptor, typename Handler, typename... Middlewares>
    class Connection;

    class Router;

    /// HTTP response
    struct response
    {
        template<typename Adaptor, typename Handler, typename... Middlewares>
        friend class crow::Connection;

        friend class Router;

        int code{200};    ///< The Status code for the response.
        std::string body; ///< The actual payload containing the response data.
        ci_map headers;   ///< HTTP headers.

#ifdef CROW_ENABLE_COMPRESSION
        bool compressed = true; ///< If compression is enabled and this is false, the individual response will not be compressed.
#endif
        bool skip_body = false;            ///< Whether this is a response to a HEAD request.
        bool manual_length_header = false; ///< Whether Crow should automatically add a "Content-Length" header.

        /// Set the value of an existing header in the response.
        void set_header(std::string key, std::string value)
        {
            headers.erase(key);
            headers.emplace(std::move(key), std::move(value));
        }

        /// Add a new header to the response.
        void add_header(std::string key, std::string value)
        {
            headers.emplace(std::move(key), std::move(value));
        }

        const std::string& get_header_value(const std::string& key)
        {
            return crow::get_header_value(headers, key);
        }

        // naive validation of a mime-type string
        static bool validate_mime_type(const std::string& candidate) noexcept
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

        // Find the mime type from the content type either by lookup,
        // or by the content type itself, if it is a valid a mime type.
        // Defaults to text/plain.
        static std::string get_mime_type(const std::string& contentType)
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


        // clang-format off
        response() {}
        explicit response(int code_) : code(code_) {}
        response(std::string body_) : body(std::move(body_)) {}
        response(int code_, std::string body_) : code(code_), body(std::move(body_)) {}
        // clang-format on
        response(returnable&& value)
        {
            body = value.dump();
            set_header("Content-Type", value.content_type);
        }
        response(returnable& value)
        {
            body = value.dump();
            set_header("Content-Type", value.content_type);
        }
        response(int code_, returnable& value):
          code(code_)
        {
            body = value.dump();
            set_header("Content-Type", value.content_type);
        }
        response(int code_, returnable&& value):
          code(code_), body(value.dump())
        {
            set_header("Content-Type", std::move(value.content_type));
        }

        response(response&& r)
        {
            *this = std::move(r);
        }

        response(std::string contentType, std::string body_):
          body(std::move(body_))
        {
            set_header("Content-Type", get_mime_type(contentType));
        }

        response(int code_, std::string contentType, std::string body_):
          code(code_), body(std::move(body_))
        {
            set_header("Content-Type", get_mime_type(contentType));
        }

        response& operator=(const response& r) = delete;

        response& operator=(response&& r) noexcept
        {
            body = std::move(r.body);
            code = r.code;
            headers = std::move(r.headers);
            completed_ = r.completed_;
            file_info = std::move(r.file_info);
            return *this;
        }

        /// Check if the response has completed (whether response.end() has been called)
        bool is_completed() const noexcept
        {
            return completed_;
        }

        void clear()
        {
            body.clear();
            code = 200;
            headers.clear();
            completed_ = false;
            file_info = static_file_info{};
        }

        /// Return a "Temporary Redirect" response.

        ///
        /// Location can either be a route or a full URL.
        void redirect(const std::string& location)
        {
            code = 307;
            set_header("Location", location);
        }

        /// Return a "Permanent Redirect" response.

        ///
        /// Location can either be a route or a full URL.
        void redirect_perm(const std::string& location)
        {
            code = 308;
            set_header("Location", location);
        }

        /// Return a "Found (Moved Temporarily)" response.

        ///
        /// Location can either be a route or a full URL.
        void moved(const std::string& location)
        {
            code = 302;
            set_header("Location", location);
        }

        /// Return a "Moved Permanently" response.

        ///
        /// Location can either be a route or a full URL.
        void moved_perm(const std::string& location)
        {
            code = 301;
            set_header("Location", location);
        }

        void write(const std::string& body_part)
        {
            body += body_part;
        }

        /// Set the response completion flag and call the handler (to send the response).
        void end()
        {
            if (!completed_)
            {
                completed_ = true;
                if (skip_body)
                {
                    set_header("Content-Length", std::to_string(body.size()));
                    body = "";
                    manual_length_header = true;
                }
                if (complete_request_handler_)
                {
                    complete_request_handler_();
                    manual_length_header = false;
                    skip_body = false;
                }
            }
        }

        /// Same as end() except it adds a body part right before ending.
        void end(const std::string& body_part)
        {
            body += body_part;
            end();
        }

        /// Check if the connection is still alive (usually by checking the socket status).
        bool is_alive()
        {
            return is_alive_helper_ && is_alive_helper_();
        }

        /// Check whether the response has a static file defined.
        bool is_static_type()
        {
            return file_info.path.size();
        }

        /// This constains metadata (coming from the `stat` command) related to any static files associated with this response.

        ///
        /// Either a static file or a string body can be returned as 1 response.
        struct static_file_info
        {
            std::string path = "";
            struct stat statbuf;
            int statResult;
        };

        /// Return a static file as the response body
        void set_static_file_info(std::string path)
        {
            utility::sanitize_filename(path);
            set_static_file_info_unsafe(path);
        }

        /// Return a static file as the response body without sanitizing the path (use set_static_file_info instead)
        void set_static_file_info_unsafe(std::string path)
        {
            file_info.path = path;
            file_info.statResult = stat(file_info.path.c_str(), &file_info.statbuf);
#ifdef CROW_ENABLE_COMPRESSION
            compressed = false;
#endif
            if (file_info.statResult == 0 && S_ISREG(file_info.statbuf.st_mode))
            {
                std::size_t last_dot = path.find_last_of('.');
                std::string extension = path.substr(last_dot + 1);
                code = 200;
                this->add_header("Content-Length", std::to_string(file_info.statbuf.st_size));

                if (!extension.empty())
                {
                    this->add_header("Content-Type", get_mime_type(extension));
                }
            }
            else
            {
                code = 404;
                file_info.path.clear();
            }
        }

    private:
        bool completed_{};
        std::function<void()> complete_request_handler_;
        std::function<bool()> is_alive_helper_;
        static_file_info file_info;
    };
} // namespace crow
