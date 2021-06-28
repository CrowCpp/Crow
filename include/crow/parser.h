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

#include <string>
#include <unordered_map>
#include <boost/algorithm/string.hpp>
#include <algorithm>

#include "crow/http_parser_merged.h"
#include "crow/http_request.h"

namespace crow
{
    /// A wrapper for `nodejs/http-parser`.

    /// Used to generate a \ref crow.request from the TCP socket buffer.
    ///
    template <typename Handler>
    struct HTTPParser : public http_parser
    {
        static int on_message_begin(http_parser* self_)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);
            self->clear();
            return 0;
        }
        static int on_url(http_parser* self_, const char* at, size_t length)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);
            self->raw_url.insert(self->raw_url.end(), at, at+length);
            return 0;
        }
        static int on_header_field(http_parser* self_, const char* at, size_t length)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);
            switch (self->header_building_state)
            {
                case 0:
                    if (!self->header_value.empty())
                    {
                        self->headers.emplace(std::move(self->header_field), std::move(self->header_value));
                    }
                    self->header_field.assign(at, at+length);
                    self->header_building_state = 1;
                    break;
                case 1:
                    self->header_field.insert(self->header_field.end(), at, at+length);
                    break;
            }
            return 0;
        }
        static int on_header_value(http_parser* self_, const char* at, size_t length)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);
            switch (self->header_building_state)
            {
                case 0:
                    self->header_value.insert(self->header_value.end(), at, at+length);
                    break;
                case 1:
                    self->header_building_state = 0;
                    self->header_value.assign(at, at+length);
                    break;
            }
            return 0;
        }
        static int on_headers_complete(http_parser* self_)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);
            if (!self->header_field.empty())
            {
                self->headers.emplace(std::move(self->header_field), std::move(self->header_value));
            }
            self->process_header();
            return 0;
        }
        static int on_body(http_parser* self_, const char* at, size_t length)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);
            self->body.insert(self->body.end(), at, at+length);
            return 0;
        }
        static int on_message_complete(http_parser* self_)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);

            // url params
            self->url = self->raw_url.substr(0, self->raw_url.find("?"));
            self->url_params = query_string(self->raw_url);

            self->process_message();
            return 0;
        }
        HTTPParser(Handler* handler) :
            handler_(handler)
        {
            http_parser_init(this, HTTP_REQUEST);
        }

        // return false on error
        /// Parse a buffer into the different sections of an HTTP request.
        bool feed(const char* buffer, int length)
        {
            const static http_parser_settings settings_{
                on_message_begin,
                on_url,
                nullptr,
                on_header_field,
                on_header_value,
                on_headers_complete,
                on_body,
                on_message_complete,
            };

            int nparsed = http_parser_execute(this, &settings_, buffer, length);
            return nparsed == length;
        }

        bool done()
        {
            return feed(nullptr, 0);
        }

        void clear()
        {
            url.clear();
            raw_url.clear();
            header_building_state = 0;
            header_field.clear();
            header_value.clear();
            headers.clear();
            url_params.clear();
            body.clear();
        }

        void process_header()
        {
            handler_->handle_header();
        }

        void process_message()
        {
            handler_->handle();
        }

        /// Take the parsed HTTP request data and convert it to a \ref crow.request
        request to_request() const
        {
            return request{static_cast<HTTPMethod>(method), std::move(raw_url), std::move(url), std::move(url_params), std::move(headers), std::move(body)};
        }

		bool is_upgrade() const
		{
			return upgrade;
		}

        bool check_version(int major, int minor) const
        {
            return http_major == major && http_minor == minor;
        }

        std::string raw_url;
        std::string url;

        int header_building_state = 0;
        std::string header_field;
        std::string header_value;
        ci_map headers;
        query_string url_params; ///< What comes after the `?` in the URL.
        std::string body;

        Handler* handler_; ///< This is currently an HTTP connection object (\ref crow.Connection).
    };
}
