#pragma once

#ifdef CROW_USE_BOOST
#include <boost/asio.hpp>
#else
#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE
#endif
#include <asio.hpp>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <vector>

#include "crow/http_parser_merged.h"
#include "crow/common.h"
#include "crow/compression.h"
#include "crow/http_response.h"
#include "crow/logging.h"
#include "crow/middleware.h"
#include "crow/middleware_context.h"
#include "crow/parser.h"
#include "crow/settings.h"
#include "crow/socket_adaptors.h"
#include "crow/task_timer.h"
#include "crow/utility.h"

namespace crow
{
#ifdef CROW_USE_BOOST
    namespace asio = boost::asio;
    using error_code = boost::system::error_code;
#else
    using error_code = asio::error_code;
#endif
    using tcp = asio::ip::tcp;

#ifdef CROW_ENABLE_DEBUG
    static std::atomic<int> connectionCount;
#endif

    /// An HTTP connection.
    template<typename Adaptor, typename Handler, typename... Middlewares>
    class Connection: public std::enable_shared_from_this<Connection<Adaptor, Handler, Middlewares...>>
    {
        friend struct crow::response;

    public:
        Connection(
          asio::io_service& io_service,
          Handler* handler,
          const std::string& server_name,
          std::tuple<Middlewares...>* middlewares,
          std::function<std::string()>& get_cached_date_str_f,
          detail::task_timer& task_timer,
          typename Adaptor::context* adaptor_ctx_,
          std::atomic<unsigned int>& queue_length):
          adaptor_(io_service, adaptor_ctx_),
          handler_(handler),
          parser_(this),
          req_(parser_.req),
          server_name_(server_name),
          middlewares_(middlewares),
          get_cached_date_str(get_cached_date_str_f),
          task_timer_(task_timer),
          res_stream_threshold_(handler->stream_threshold()),
          queue_length_(queue_length)
        {
#ifdef CROW_ENABLE_DEBUG
            connectionCount++;
            CROW_LOG_DEBUG << "Connection (" << this << ") allocated, total: " << connectionCount;
#endif
        }

        ~Connection()
        {
#ifdef CROW_ENABLE_DEBUG
            connectionCount--;
            CROW_LOG_DEBUG << "Connection (" << this << ") freed, total: " << connectionCount;
#endif
        }

        /// The TCP socket on top of which the connection is established.
        decltype(std::declval<Adaptor>().raw_socket())& socket()
        {
            return adaptor_.raw_socket();
        }

        void start()
        {
            auto self = this->shared_from_this();
            adaptor_.start([self](const error_code& ec) {
                if (!ec)
                {
                    self->start_deadline();
                    self->parser_.clear();

                    self->do_read();
                }
                else
                {
                    CROW_LOG_ERROR << "Could not start adaptor: " << ec.message();
                }
            });
        }

        void handle_url()
        {
            routing_handle_result_ = handler_->handle_initial(req_, res);
            // if no route is found for the request method, return the response without parsing or processing anything further.
            if (!routing_handle_result_->rule_index)
            {
                parser_.done();
                need_to_call_after_handlers_ = true;
                complete_request();
            }
        }

        void handle_header()
        {
            // HTTP 1.1 Expect: 100-continue
            if (req_.http_ver_major == 1 && req_.http_ver_minor == 1 && get_header_value(req_.headers, "expect") == "100-continue")
            {
                continue_requested = true;
                buffers_.clear();
                static std::string expect_100_continue = "HTTP/1.1 100 Continue\r\n\r\n";
                buffers_.emplace_back(expect_100_continue.data(), expect_100_continue.size());
                do_write();
            }
        }

        void handle()
        {
            // TODO(EDev): cancel_deadline_timer should be looked into, it might be a good idea to add it to handle_url() and then restart the timer once everything passes
            cancel_deadline_timer();
            bool is_invalid_request = false;
            add_keep_alive_ = false;

            // Create context
            ctx_ = detail::context<Middlewares...>();
            req_.middleware_context = static_cast<void*>(&ctx_);
            req_.middleware_container = static_cast<void*>(middlewares_);
            req_.io_service = &adaptor_.get_io_service();
            
            req_.remote_ip_address = adaptor_.remote_endpoint().address().to_string();

            add_keep_alive_ = req_.keep_alive;
            close_connection_ = req_.close_connection;

            if (req_.check_version(1, 1)) // HTTP/1.1
            {
                if (!req_.headers.count("host"))
                {
                    is_invalid_request = true;
                    res = response(400);
                }
                else if (req_.upgrade)
                {
                    // h2 or h2c headers
                    if (req_.get_header_value("upgrade").substr(0, 2) == "h2")
                    {
                        // TODO(ipkn): HTTP/2
                        // currently, ignore upgrade header
                    }
                    else
                    {
                
                        detail::middleware_call_helper<detail::middleware_call_criteria_only_global,
                                                       0, decltype(ctx_), decltype(*middlewares_)>({}, *middlewares_, req_, res, ctx_);
                        close_connection_ = true;
                        handler_->handle_upgrade(req_, res, std::move(adaptor_));
                        return;
                    }
                }
            }

            CROW_LOG_INFO << "Request: " << utility::lexical_cast<std::string>(adaptor_.remote_endpoint()) << " " << this << " HTTP/" << (char)(req_.http_ver_major + '0') << "." << (char)(req_.http_ver_minor + '0') << ' ' << method_name(req_.method) << " " << req_.url;


            need_to_call_after_handlers_ = false;
            if (!is_invalid_request)
            {
                res.complete_request_handler_ = nullptr;
                auto self = this->shared_from_this();
                res.is_alive_helper_ = [self]() -> bool {
                    return self->adaptor_.is_open();
                };
                
                detail::middleware_call_helper<detail::middleware_call_criteria_only_global,
                                               0, decltype(ctx_), decltype(*middlewares_)>({}, *middlewares_, req_, res, ctx_);

                if (!res.completed_)
                {
                    auto self = this->shared_from_this();
                    res.complete_request_handler_ = [self] {
                        self->complete_request();
                    };
                    need_to_call_after_handlers_ = true;
                    handler_->handle(req_, res, routing_handle_result_);
                    if (add_keep_alive_)
                        res.set_header("connection", "Keep-Alive");
                }
                else
                {
                    complete_request();
                }
            }
            else
            {
                complete_request();
            }
        }

        /// Call the after handle middleware and send the write the response to the connection.
        void complete_request()
        {
            CROW_LOG_INFO << "Response: " << this << ' ' << req_.raw_url << ' ' << res.code << ' ' << close_connection_;
            res.is_alive_helper_ = nullptr;

            if (need_to_call_after_handlers_)
            {
                need_to_call_after_handlers_ = false;

                // call all after_handler of middlewares
                detail::after_handlers_call_helper<
                  detail::middleware_call_criteria_only_global,
                  (static_cast<int>(sizeof...(Middlewares)) - 1),
                  decltype(ctx_),
                  decltype(*middlewares_)>({}, *middlewares_, ctx_, req_, res);
            }
#ifdef CROW_ENABLE_COMPRESSION
            if (handler_->compression_used())
            {
                std::string accept_encoding = req_.get_header_value("Accept-Encoding");
                if (!accept_encoding.empty() && res.compressed)
                {
                    switch (handler_->compression_algorithm())
                    {
                        case compression::DEFLATE:
                            if (accept_encoding.find("deflate") != std::string::npos)
                            {
                                res.body = compression::compress_string(res.body, compression::algorithm::DEFLATE);
                                res.set_header("Content-Encoding", "deflate");
                            }
                            break;
                        case compression::GZIP:
                            if (accept_encoding.find("gzip") != std::string::npos)
                            {
                                res.body = compression::compress_string(res.body, compression::algorithm::GZIP);
                                res.set_header("Content-Encoding", "gzip");
                            }
                            break;
                        default:
                            break;
                    }
                }
            }
#endif
            //if there is a redirection with a partial URL, treat the URL as a route.
            std::string location = res.get_header_value("Location");
            if (!location.empty() && location.find("://", 0) == std::string::npos)
            {
#ifdef CROW_ENABLE_SSL
                if (handler_->ssl_used())
                    location.insert(0, "https://" + req_.get_header_value("Host"));
                else
#endif
                    location.insert(0, "http://" + req_.get_header_value("Host"));
                res.set_header("location", location);
            }

            prepare_buffers();

            if (res.is_static_type())
            {
                do_write_static();
            }
            else
            {
                do_write_general();
            }
        }

    private:
        void prepare_buffers()
        {
            res.complete_request_handler_ = nullptr;
            res.is_alive_helper_ = nullptr;

            if (!adaptor_.is_open())
            {
                //CROW_LOG_DEBUG << this << " delete (socket is closed) " << is_reading << ' ' << is_writing;
                //delete this;
                return;
            }
            // TODO(EDev): HTTP version in status codes should be dynamic
            // Keep in sync with common.h/status
            static std::unordered_map<int, std::string> statusCodes = {
              {status::CONTINUE, "HTTP/1.1 100 Continue\r\n"},
              {status::SWITCHING_PROTOCOLS, "HTTP/1.1 101 Switching Protocols\r\n"},

              {status::OK, "HTTP/1.1 200 OK\r\n"},
              {status::CREATED, "HTTP/1.1 201 Created\r\n"},
              {status::ACCEPTED, "HTTP/1.1 202 Accepted\r\n"},
              {status::NON_AUTHORITATIVE_INFORMATION, "HTTP/1.1 203 Non-Authoritative Information\r\n"},
              {status::NO_CONTENT, "HTTP/1.1 204 No Content\r\n"},
              {status::RESET_CONTENT, "HTTP/1.1 205 Reset Content\r\n"},
              {status::PARTIAL_CONTENT, "HTTP/1.1 206 Partial Content\r\n"},

              {status::MULTIPLE_CHOICES, "HTTP/1.1 300 Multiple Choices\r\n"},
              {status::MOVED_PERMANENTLY, "HTTP/1.1 301 Moved Permanently\r\n"},
              {status::FOUND, "HTTP/1.1 302 Found\r\n"},
              {status::SEE_OTHER, "HTTP/1.1 303 See Other\r\n"},
              {status::NOT_MODIFIED, "HTTP/1.1 304 Not Modified\r\n"},
              {status::TEMPORARY_REDIRECT, "HTTP/1.1 307 Temporary Redirect\r\n"},
              {status::PERMANENT_REDIRECT, "HTTP/1.1 308 Permanent Redirect\r\n"},

              {status::BAD_REQUEST, "HTTP/1.1 400 Bad Request\r\n"},
              {status::UNAUTHORIZED, "HTTP/1.1 401 Unauthorized\r\n"},
              {status::FORBIDDEN, "HTTP/1.1 403 Forbidden\r\n"},
              {status::NOT_FOUND, "HTTP/1.1 404 Not Found\r\n"},
              {status::METHOD_NOT_ALLOWED, "HTTP/1.1 405 Method Not Allowed\r\n"},
              {status::NOT_ACCEPTABLE, "HTTP/1.1 406 Not Acceptable\r\n"},
              {status::PROXY_AUTHENTICATION_REQUIRED, "HTTP/1.1 407 Proxy Authentication Required\r\n"},
              {status::CONFLICT, "HTTP/1.1 409 Conflict\r\n"},
              {status::GONE, "HTTP/1.1 410 Gone\r\n"},
              {status::PAYLOAD_TOO_LARGE, "HTTP/1.1 413 Payload Too Large\r\n"},
              {status::UNSUPPORTED_MEDIA_TYPE, "HTTP/1.1 415 Unsupported Media Type\r\n"},
              {status::RANGE_NOT_SATISFIABLE, "HTTP/1.1 416 Range Not Satisfiable\r\n"},
              {status::EXPECTATION_FAILED, "HTTP/1.1 417 Expectation Failed\r\n"},
              {status::PRECONDITION_REQUIRED, "HTTP/1.1 428 Precondition Required\r\n"},
              {status::TOO_MANY_REQUESTS, "HTTP/1.1 429 Too Many Requests\r\n"},
              {status::UNAVAILABLE_FOR_LEGAL_REASONS, "HTTP/1.1 451 Unavailable For Legal Reasons\r\n"},

              {status::INTERNAL_SERVER_ERROR, "HTTP/1.1 500 Internal Server Error\r\n"},
              {status::NOT_IMPLEMENTED, "HTTP/1.1 501 Not Implemented\r\n"},
              {status::BAD_GATEWAY, "HTTP/1.1 502 Bad Gateway\r\n"},
              {status::SERVICE_UNAVAILABLE, "HTTP/1.1 503 Service Unavailable\r\n"},
              {status::GATEWAY_TIMEOUT, "HTTP/1.1 504 Gateway Timeout\r\n"},
              {status::VARIANT_ALSO_NEGOTIATES, "HTTP/1.1 506 Variant Also Negotiates\r\n"},
            };

            static const std::string seperator = ": ";

            buffers_.clear();
            buffers_.reserve(4 * (res.headers.size() + 5) + 3);

            if (!statusCodes.count(res.code))
            {
                CROW_LOG_WARNING << this << " status code "
                                 << "(" << res.code << ")"
                                 << " not defined, returning 500 instead";
                res.code = 500;
            }

            auto& status = statusCodes.find(res.code)->second;
            buffers_.emplace_back(status.data(), status.size());

            if (res.code >= 400 && res.body.empty())
                res.body = statusCodes[res.code].substr(9);

            for (auto& kv : res.headers)
            {
                buffers_.emplace_back(kv.first.data(), kv.first.size());
                buffers_.emplace_back(seperator.data(), seperator.size());
                buffers_.emplace_back(kv.second.data(), kv.second.size());
                buffers_.emplace_back(crlf.data(), crlf.size());
            }

            if (!res.manual_length_header && !res.headers.count("content-length"))
            {
                content_length_ = std::to_string(res.body.size());
                static std::string content_length_tag = "Content-Length: ";
                buffers_.emplace_back(content_length_tag.data(), content_length_tag.size());
                buffers_.emplace_back(content_length_.data(), content_length_.size());
                buffers_.emplace_back(crlf.data(), crlf.size());
            }
            if (!res.headers.count("server"))
            {
                static std::string server_tag = "Server: ";
                buffers_.emplace_back(server_tag.data(), server_tag.size());
                buffers_.emplace_back(server_name_.data(), server_name_.size());
                buffers_.emplace_back(crlf.data(), crlf.size());
            }
            if (!res.headers.count("date"))
            {
                static std::string date_tag = "Date: ";
                date_str_ = get_cached_date_str();
                buffers_.emplace_back(date_tag.data(), date_tag.size());
                buffers_.emplace_back(date_str_.data(), date_str_.size());
                buffers_.emplace_back(crlf.data(), crlf.size());
            }
            if (add_keep_alive_)
            {
                static std::string keep_alive_tag = "Connection: Keep-Alive";
                buffers_.emplace_back(keep_alive_tag.data(), keep_alive_tag.size());
                buffers_.emplace_back(crlf.data(), crlf.size());
            }

            buffers_.emplace_back(crlf.data(), crlf.size());
        }

        void do_write_static()
        {
            asio::write(adaptor_.socket(), buffers_);

            if (res.file_info.statResult == 0)
            {
                std::ifstream is(res.file_info.path.c_str(), std::ios::in | std::ios::binary);
                std::vector<asio::const_buffer> buffers{1};
                char buf[16384];
                is.read(buf, sizeof(buf));
                while (is.gcount() > 0)
                {
                    buffers[0] = asio::buffer(buf, is.gcount());
                    do_write_sync(buffers);
                    is.read(buf, sizeof(buf));
                }
            }
            if (close_connection_)
            {
                adaptor_.shutdown_readwrite();
                adaptor_.close();
                CROW_LOG_DEBUG << this << " from write (static)";
            }

            res.end();
            res.clear();
            buffers_.clear();
            parser_.clear();
        }

        void do_write_general()
        {
            if (res.body.length() < res_stream_threshold_)
            {
                res_body_copy_.swap(res.body);
                buffers_.emplace_back(res_body_copy_.data(), res_body_copy_.size());

                do_write();

                if (need_to_start_read_after_complete_)
                {
                    need_to_start_read_after_complete_ = false;
                    start_deadline();
                    do_read();
                }
            }
            else
            {
                asio::write(adaptor_.socket(), buffers_); // Write the response start / headers
                cancel_deadline_timer();
                if (res.body.length() > 0)
                {
                    std::vector<asio::const_buffer> buffers{1};
                    const uint8_t *data = reinterpret_cast<const uint8_t*>(res.body.data());
                    size_t length = res.body.length();
                    for(size_t transferred = 0; transferred < length;)
                    {
                        size_t to_transfer = CROW_MIN(16384UL, length-transferred);
                        buffers[0] = asio::const_buffer(data+transferred, to_transfer);
                        do_write_sync(buffers);
                        transferred += to_transfer;
                    }
                }
                if (close_connection_)
                {
                    adaptor_.shutdown_readwrite();
                    adaptor_.close();
                    CROW_LOG_DEBUG << this << " from write (res_stream)";
                }

                res.end();
                res.clear();
                buffers_.clear();
                parser_.clear();
            }
        }

        void do_read()
        {
            auto self = this->shared_from_this();
            adaptor_.socket().async_read_some(
              asio::buffer(buffer_),
              [self](const error_code& ec, std::size_t bytes_transferred) {
                  bool error_while_reading = true;
                  if (!ec)
                  {
                      bool ret = self->parser_.feed(self->buffer_.data(), bytes_transferred);
                      if (ret && self->adaptor_.is_open())
                      {
                          error_while_reading = false;
                      }
                  }

                  if (error_while_reading)
                  {
                      self->cancel_deadline_timer();
                      self->parser_.done();
                      self->adaptor_.shutdown_read();
                      self->adaptor_.close();
                      CROW_LOG_DEBUG << self << " from read(1) with description: \"" << http_errno_description(static_cast<http_errno>(self->parser_.http_errno)) << '\"';
                  }
                  else if (self->close_connection_)
                  {
                      self->cancel_deadline_timer();
                      self->parser_.done();
                      // adaptor will close after write
                  }
                  else if (!self->need_to_call_after_handlers_)
                  {
                      self->start_deadline();
                      self->do_read();
                  }
                  else
                  {
                      // res will be completed later by user
                      self->need_to_start_read_after_complete_ = true;
                  }
              });
        }

        void do_write()
        {
            auto self = this->shared_from_this();
            asio::async_write(
              adaptor_.socket(), buffers_,
              [self](const error_code& ec, std::size_t /*bytes_transferred*/) {
                  self->res.clear();
                  self->res_body_copy_.clear();                  
                  if (!self->continue_requested)
                  {
                      self->parser_.clear();
                  }
                  else
                  {
                      self->continue_requested = false;
                  }
                  
                  if (!ec)
                  {
                      if (self->close_connection_)
                      {
                          self->adaptor_.shutdown_write();
                          self->adaptor_.close();
                          CROW_LOG_DEBUG << self << " from write(1)";
                      }
                  }
                  else
                  {
                      CROW_LOG_DEBUG << self << " from write(2)";
                  }
              });
        }

        inline void do_write_sync(std::vector<asio::const_buffer>& buffers)
        {

            asio::write(adaptor_.socket(), buffers, [&](error_code ec, std::size_t) {
                if (!ec)
                {
                    return false;
                }
                else
                {
                    CROW_LOG_ERROR << ec << " - happened while sending buffers";
                    CROW_LOG_DEBUG << this << " from write (sync)(2)";
                    return true;
                }
            });
        }

        void cancel_deadline_timer()
        {
            CROW_LOG_DEBUG << this << " timer cancelled: " << &task_timer_ << ' ' << task_id_;
            task_timer_.cancel(task_id_);
        }

        void start_deadline(/*int timeout = 5*/)
        {
            cancel_deadline_timer();

            auto self = this->shared_from_this();
            task_id_ = task_timer_.schedule([self] {
                if (!self->adaptor_.is_open())
                {
                    return;
                }
                self->adaptor_.shutdown_readwrite();
                self->adaptor_.close();
            });
            CROW_LOG_DEBUG << this << " timer added: " << &task_timer_ << ' ' << task_id_;
        }

    private:
        Adaptor adaptor_;
        Handler* handler_;

        std::array<char, 4096> buffer_;

        HTTPParser<Connection> parser_;
        std::unique_ptr<routing_handle_result> routing_handle_result_;
        request& req_;
        response res;

        bool close_connection_ = false;

        const std::string& server_name_;
        std::vector<asio::const_buffer> buffers_;

        std::string content_length_;
        std::string date_str_;
        std::string res_body_copy_;

        detail::task_timer::identifier_type task_id_{};

        bool continue_requested{};
        bool need_to_call_after_handlers_{};
        bool need_to_start_read_after_complete_{};
        bool add_keep_alive_{};

        std::tuple<Middlewares...>* middlewares_;
        detail::context<Middlewares...> ctx_;

        std::function<std::string()>& get_cached_date_str;
        detail::task_timer& task_timer_;

        size_t res_stream_threshold_;

        std::atomic<unsigned int>& queue_length_;
    };

} // namespace crow
