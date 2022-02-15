#pragma once

#include <boost/asio.hpp>

#include "crow/common.h"
#include "crow/ci_map.h"
#include "crow/query_string.h"

namespace crow
{
    /// Find and return the value associated with the key. (returns an empty string if nothing is found)
    template<typename T>
    inline const std::string& get_header_value(const T& headers, const std::string& key)
    {
        if (headers.count(key))
        {
            return headers.find(key)->second;
        }
        static std::string empty;
        return empty;
    }

    /// An HTTP request.
    struct request
    {
        HTTPMethod method;
        std::string raw_url;     ///< The full URL containing the `?` and URL parameters.
        std::string url;         ///< The endpoint without any parameters.
        query_string url_params; ///< The parameters associated with the request. (everything after the `?`)
        ci_map headers;
        std::string body;
        std::string remote_ip_address; ///< The IP address from which the request was sent.
        unsigned char http_ver_major, http_ver_minor;
        bool keep_alive, close_connection, upgrade;

        void* middleware_context{};
        void* middleware_container{};
        boost::asio::io_service* io_service{};

        /// Construct an empty request. (sets the method to `GET`)
        request():
          method(HTTPMethod::Get)
        {}

        /// Construct a request with all values assigned.
        request(HTTPMethod method, std::string raw_url, std::string url, query_string url_params, ci_map headers, std::string body, unsigned char http_major, unsigned char http_minor, bool has_keep_alive, bool has_close_connection, bool is_upgrade):
          method(method), raw_url(std::move(raw_url)), url(std::move(url)), url_params(std::move(url_params)), headers(std::move(headers)), body(std::move(body)), http_ver_major(http_major), http_ver_minor(http_minor), keep_alive(has_keep_alive), close_connection(has_close_connection), upgrade(is_upgrade)
        {}

        void add_header(std::string key, std::string value)
        {
            headers.emplace(std::move(key), std::move(value));
        }

        const std::string& get_header_value(const std::string& key) const
        {
            return crow::get_header_value(headers, key);
        }

        bool check_version(unsigned char major, unsigned char minor) const
        {
            return http_ver_major == major && http_ver_minor == minor;
        }

        /// Send data to whoever made this request with a completion handler and return immediately.
        template<typename CompletionHandler>
        void post(CompletionHandler handler)
        {
            io_service->post(handler);
        }

        /// Send data to whoever made this request with a completion handler.
        template<typename CompletionHandler>
        void dispatch(CompletionHandler handler)
        {
            io_service->dispatch(handler);
        }
    };
} // namespace crow
