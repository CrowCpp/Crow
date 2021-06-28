/*
Copyright (c) 2014-2017, ipkn
              2020-2021, CrowCpp
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the author nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

#include <boost/asio.hpp>

#include "crow/common.h"
#include "crow/ci_map.h"
#include "crow/query_string.h"

namespace crow
{
    /// Find and return the value associated with the key. (returns an empty string if nothing is found)
    template <typename T>
    inline const std::string& get_header_value(const T& headers, const std::string& key)
    {
        if (headers.count(key))
        {
            return headers.find(key)->second;
        }
        static std::string empty;
        return empty;
    }

	struct DetachHelper;

    /// An HTTP request.
    struct request
    {
        HTTPMethod method;
        std::string raw_url; ///< The full URL containing the `?` and URL parameters.
        std::string url; ///< The endpoint without any parameters.
        query_string url_params; ///< The parameters associated with the request. (everything after the `?`)
        ci_map headers;
        std::string body;
        std::string remoteIpAddress; ///< The IP address from which the request was sent.

        void* middleware_context{};
        boost::asio::io_service* io_service{};

        /// Construct an empty request. (sets the method to `GET`)
        request()
            : method(HTTPMethod::Get)
        {
        }

        /// Construct a request with all values assigned.
        request(HTTPMethod method, std::string raw_url, std::string url, query_string url_params, ci_map headers, std::string body)
            : method(method), raw_url(std::move(raw_url)), url(std::move(url)), url_params(std::move(url_params)), headers(std::move(headers)), body(std::move(body))
        {
        }

        void add_header(std::string key, std::string value)
        {
            headers.emplace(std::move(key), std::move(value));
        }

        const std::string& get_header_value(const std::string& key) const
        {
            return crow::get_header_value(headers, key);
        }

        /// Send the request with a completion handler and return immediately.
        template<typename CompletionHandler>
        void post(CompletionHandler handler)
        {
            io_service->post(handler);
        }

        /// Send the request with a completion handler.
        template<typename CompletionHandler>
        void dispatch(CompletionHandler handler)
        {
            io_service->dispatch(handler);
        }

    };
}
