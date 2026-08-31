#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "crow/body_sink.h"
#include "crow/common.h"
#include "crow/http_request.h"
#include "crow/http_parser_merged.h"

namespace crow
{
    /// A wrapper for `nodejs/http-parser`.

    ///
    /// Used to generate a \ref crow.request from the TCP socket buffer.
    template<typename Handler>
    struct HTTPParser : public http_parser
    {
        static int on_message_begin(http_parser*)
        {
            return 0;
        }
        static int on_method(http_parser* self_)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);
            self->req.method = static_cast<HTTPMethod>(self->method);

            return 0;
        }
        static int on_url(http_parser* self_, const char* at, size_t length)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);
            self->req.raw_url.insert(self->req.raw_url.end(), at, at + length);
            self->req.url_params = query_string(self->req.raw_url);
            self->req.url = self->req.raw_url.substr(0, self->qs_point != 0 ? self->qs_point : std::string::npos);

            self->process_url();

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
                        self->req.headers.emplace(std::move(self->header_field), std::move(self->header_value));
                    }
                    self->header_field.assign(at, at + length);
                    self->header_building_state = 1;
                    break;
                case 1:
                    self->header_field.insert(self->header_field.end(), at, at + length);
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
                    self->header_value.insert(self->header_value.end(), at, at + length);
                    break;
                case 1:
                    self->header_building_state = 0;
                    self->header_value.assign(at, at + length);
                    break;
            }
            return 0;
        }
        static int on_headers_complete(http_parser* self_)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);
            if (!self->header_field.empty())
            {
                self->req.headers.emplace(std::move(self->header_field), std::move(self->header_value));
            }

            self->set_connection_parameters();

            return self->process_header();
        }
        static int on_body(http_parser* self_, const char* at, size_t length)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);
            if (self->max_body_size_ != UINT64_MAX &&
                (length > self->max_body_size_ ||
                 self->body_bytes_ > self->max_body_size_ - length))
            {
                self->handler_->reject_body(status::PAYLOAD_TOO_LARGE);
                return 1;
            }
            if (self->body_sink_)
            {
                if (!self->body_sink_->write(at, length))
                {
                    self->handler_->reject_body(status::INTERNAL_SERVER_ERROR);
                    return 1;
                }
            }
            else
            {
                self->req.body.insert(self->req.body.end(), at, at + length);
            }
            self->body_bytes_ += length;
            return 0;
        }
        static int on_message_complete(http_parser* self_)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);

            self->message_complete = true;
            if (self->body_sink_)
            {
                const bool ok = self->body_sink_->finish();
                if (auto* file = dynamic_cast<FileBodySink*>(self->body_sink_.get()))
                    self->req.body_file_path = file->path();
                if (!ok)
                {
                    self->handler_->reject_body(status::INTERNAL_SERVER_ERROR);
                    return 1;
                }
            }
            self->process_message();
            // Stop so leftover skipped-body bytes are not parsed as the next request.
            return self->handler_->parser_should_abort() ? 1 : 0;
        }
        HTTPParser(Handler* handler):
          http_parser(),
          handler_(handler)
        {
            http_parser_init(this);
        }

        ~HTTPParser()
        {
            cleanup_body_sink();
        }

        // return false on error
        /// Parse a buffer into the different sections of an HTTP request.
        bool feed(const char* buffer, int length)
        {
            if (message_complete)
                return true;

            const static http_parser_settings settings_{
              on_message_begin,
              on_method,
              on_url,
              on_header_field,
              on_header_value,
              on_headers_complete,
              on_body,
              on_message_complete,
            };

            int nparsed = http_parser_execute(this, &settings_, buffer, length);
            if (http_errno != CHPE_OK)
            {
                return false;
            }
            return nparsed == length;
        }

        bool done()
        {
            return feed(nullptr, 0);
        }

        void clear()
        {
            cleanup_body_sink();
            req = crow::request();
            header_field.clear();
            header_value.clear();
            header_building_state = 0;
            qs_point = 0;
            message_complete = false;
            body_bytes_ = 0;
            max_body_size_ = UINT64_MAX;
            state = CROW_NEW_MESSAGE();
        }

        void set_max_body_size(uint64_t bytes)
        {
            max_body_size_ = bytes;
        }

        bool open_body_sink(std::unique_ptr<BodySink> sink)
        {
            cleanup_body_sink();
            if (!sink)
                return false;
            body_sink_ = std::move(sink);
            return true;
        }

        bool has_incoming_body() const
        {
            if (flags & F_CHUNKED)
                return true;
            return content_length != CROW_ULLONG_MAX && content_length > 0;
        }

        inline void process_url()
        {
            handler_->handle_url();
        }

        inline int process_header()
        {
            return handler_->handle_header();
        }

        inline void process_message()
        {
            handler_->handle();
        }

        inline void set_connection_parameters()
        {
            req.http_ver_major = http_major;
            req.http_ver_minor = http_minor;

            //NOTE(EDev): it seems that the problem is with crow's policy on closing the connection for HTTP_VERSION < 1.0, the behaviour for that in crow is "don't close the connection, but don't send a keep-alive either"

            // HTTP1.1 = always send keep_alive, HTTP1.0 = only send if header exists, HTTP?.? = never send
            req.keep_alive = (http_major == 1 && http_minor == 0) ?
                               ((flags & F_CONNECTION_KEEP_ALIVE) ? true : false) :
                               ((http_major == 1 && http_minor == 1) ? true : false);

            // HTTP1.1 = only close if close header exists, HTTP1.0 = always close unless keep_alive header exists, HTTP?.?= never close
            req.close_connection = (http_major == 1 && http_minor == 0) ?
                                     ((flags & F_CONNECTION_KEEP_ALIVE) ? false : true) :
                                     ((http_major == 1 && http_minor == 1) ? ((flags & F_CONNECTION_CLOSE) ? true : false) : false);
            req.upgrade = static_cast<bool>(upgrade);
        }

        /// The final request that this parser outputs.
        ///
        /// Data parsed is put directly into this object as soon as the related callback returns. (e.g. the request will have the cooorect method as soon as on_method() returns)
        request req;

    private:
        void cleanup_body_sink()
        {
            if (auto* file = dynamic_cast<FileBodySink*>(body_sink_.get()))
            {
                if (req.persist_body_file_)
                    file->persist();
            }
            body_sink_.reset();
        }

        int header_building_state = 0;
        bool message_complete = false;
        uint64_t body_bytes_{0};
        uint64_t max_body_size_{UINT64_MAX};
        std::unique_ptr<BodySink> body_sink_;
        std::string header_field;
        std::string header_value;

        Handler* handler_; ///< This is currently an HTTP connection object (\ref crow.Connection).
    };
} // namespace crow

#undef CROW_NEW_MESSAGE
#undef CROW_start_state
