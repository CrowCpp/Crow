#pragma once

#ifdef CROW_USE_BOOST
#include <boost/asio.hpp>
#else
#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE
#endif
#include <asio.hpp>
#endif

#include "crow/common.h"
#include "crow/ci_map.h"
#include "crow/query_string.h"

namespace crow // NOTE: Already documented in "crow/app.h"
{
#ifdef CROW_USE_BOOST
    namespace asio = boost::asio;
#endif

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
        query_string url_params; ///< The parameters associated with the request. (everything after the `?` in the URL)
        ci_map headers;
        std::string body;
        std::string remote_ip_address; ///< The IP address from which the request was sent.
        unsigned char http_ver_major, http_ver_minor;
        bool keep_alive,    ///< Whether or not the server should send a `connection: Keep-Alive` header to the client.
          close_connection, ///< Whether or not the server should shut down the TCP connection once a response is sent.
          upgrade;          ///< Whether or noth the server should change the HTTP connection to a different connection.

        void* middleware_context{};
        void* middleware_container{};
        asio::io_context* io_context{};

        /// Construct an empty request. (sets the method to `GET`)
        request():
          method(HTTPMethod::Get)
        {}

        /// Construct a request with all values assigned.
        request(HTTPMethod method_, std::string raw_url_, std::string url_, query_string url_params_, ci_map headers_, std::string body_, unsigned char http_major, unsigned char http_minor, bool has_keep_alive, bool has_close_connection, bool is_upgrade):
          method(method_), raw_url(std::move(raw_url_)), url(std::move(url_)), url_params(std::move(url_params_)), headers(std::move(headers_)), body(std::move(body_)), http_ver_major(http_major), http_ver_minor(http_minor), keep_alive(has_keep_alive), close_connection(has_close_connection), upgrade(is_upgrade)
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

        /// Get the body as parameters in QS format.

        ///
        /// This is meant to be used with requests of type "application/x-www-form-urlencoded"
        const query_string get_body_params() const
        {
            return query_string(body, false);
        }

        /// Send data to whoever made this request with a completion handler and return immediately.
        template<typename CompletionHandler>
        void post(CompletionHandler handler)
        {
            asio::post(io_context, handler);
        }

        /// Send data to whoever made this request with a completion handler.
        template<typename CompletionHandler>
        void dispatch(CompletionHandler handler)
        {
            asio::dispatch(io_context, handler);
        }
    };
} // namespace crow
