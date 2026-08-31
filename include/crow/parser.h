#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <unordered_map>

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

            self->process_header();
            return 0;
        }
        static int on_body(http_parser* self_, const char* at, size_t length)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);
            if (self->body_discard_)
                return 0;
            if (self->body_file_)
            {
                if (self->max_body_file_size_ != 0 &&
                    (length > self->max_body_file_size_ ||
                     self->body_bytes_ > self->max_body_file_size_ - length))
                {
                    self->fail_body_file(status::PAYLOAD_TOO_LARGE);
                    return 0;
                }
                self->body_file_->write(at, static_cast<std::streamsize>(length));
                if (!*self->body_file_)
                {
                    self->fail_body_file(status::INTERNAL_SERVER_ERROR);
                    return 0;
                }
                self->body_bytes_ += length;
                return 0;
            }
            self->req.body.insert(self->req.body.end(), at, at + length);
            return 0;
        }
        static int on_message_complete(http_parser* self_)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);

            self->close_body_file_stream();
            if (!self->req.body_file_path.empty())
                self->req.body_file_size = self->body_bytes_;
            self->message_complete = true;
            self->process_message();
            return 0;
        }
        HTTPParser(Handler* handler):
          http_parser(),
          handler_(handler)
        {
            http_parser_init(this);
        }

        ~HTTPParser()
        {
            cleanup_body_file(!req.persist_body_file);
        }

        /// Open `path` and write subsequent body bytes there instead of `req.body`.
        bool open_body_file(const std::string& path, uint64_t max_size)
        {
            if (path.empty())
                return false;

            cleanup_body_file(true);
            body_file_ = std::make_unique<std::ofstream>(path, std::ios::binary | std::ios::out | std::ios::trunc);
            if (!body_file_ || !body_file_->is_open())
            {
                body_file_.reset();
                std::error_code ec;
                std::filesystem::remove(path, ec);
                return false;
            }

            req.body_file_path = path;
            max_body_file_size_ = max_size;
            body_bytes_ = 0;
            body_discard_ = false;
            return true;
        }

        /// Stop storing the remainder of the body (do not append it to `req.body`).
        void discard_remaining_body()
        {
            body_discard_ = true;
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
            cleanup_body_file(!req.persist_body_file);
            req = crow::request();
            header_field.clear();
            header_value.clear();
            header_building_state = 0;
            qs_point = 0;
            message_complete = false;
            body_discard_ = false;
            body_bytes_ = 0;
            max_body_file_size_ = 0;
            state = CROW_NEW_MESSAGE();
        }

        inline void process_url()
        {
            handler_->handle_url();
        }

        inline void process_header()
        {
            handler_->handle_header();
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
        void close_body_file_stream()
        {
            if (!body_file_)
                return;
            body_file_->flush();
            body_file_->close();
            body_file_.reset();
        }

        void cleanup_body_file(bool remove_file)
        {
            close_body_file_stream();
            if (remove_file && !req.body_file_path.empty())
            {
                std::error_code ec;
                std::filesystem::remove(req.body_file_path, ec);
                req.body_file_path.clear();
                req.body_file_size = 0;
            }
        }

        void fail_body_file(int code)
        {
            body_discard_ = true;
            cleanup_body_file(true);
            req.body_error_code = code;
        }

        int header_building_state = 0;
        bool message_complete = false;
        bool body_discard_{false};
        uint64_t body_bytes_{0};
        uint64_t max_body_file_size_{0};
        std::unique_ptr<std::ofstream> body_file_;
        std::string header_field;
        std::string header_value;

        Handler* handler_; ///< This is currently an HTTP connection object (\ref crow.Connection).
    };
} // namespace crow

#undef CROW_NEW_MESSAGE
#undef CROW_start_state
