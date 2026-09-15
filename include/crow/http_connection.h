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
#include <cstdint>
#include <exception>
#include <fstream>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
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

    namespace detail {
#ifdef CROW_ENABLE_ASYNC_CHUNK_PUBLICATION_TEST_HOOK
    void invoke_async_chunk_publication_test_hook();
#endif

    class connection_lifecycle_registry {
    public:
        template<typename ConnectionType>
        bool track(const std::shared_ptr<ConnectionType>& connection)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shutting_down_)
            {
                return false;
            }
            connections_[connection.get()] = [connection] {
                connection->shutdown_on_worker_exit();
            };
            return true;
        }

        void untrack(const void* connection) noexcept
        {
            std::lock_guard<std::mutex> lock(mutex_);
            connections_.erase(connection);
        }

        // Not noexcept: copying the tracked callbacks can allocate, and a rare
        // failure must reach the worker future instead of std::terminate.
        void shutdown_all()
        {
            std::vector<std::function<void()>> shutdown_callbacks;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (shutting_down_)
                {
                    return;
                }
                shutting_down_ = true;
                shutdown_callbacks.reserve(connections_.size());
                for (const auto& item : connections_)
                {
                    shutdown_callbacks.push_back(item.second);
                }
            }
            for (auto& shutdown : shutdown_callbacks)
            {
                shutdown();
            }
        }

    private:
        std::mutex mutex_;
        bool shutting_down_{false};
        std::unordered_map<const void*, std::function<void()>> connections_;
    };
    } // namespace detail

    /// An HTTP connection.
    template<typename Adaptor, typename Handler, typename... Middlewares>
    class Connection : public std::enable_shared_from_this<Connection<Adaptor, Handler, Middlewares...>>
    {
        friend struct crow::response;
        friend class detail::connection_lifecycle_registry;
#ifdef CROW_ENABLE_ASYNC_CHUNK_PUBLICATION_TEST_HOOK
        friend struct connection_test_access;
#endif

    public:
        Connection(asio::io_context& io_context,
                   Handler* handler,
                   const std::string& server_name,
                   std::tuple<Middlewares...>* middlewares,
                   std::function<std::string()>& get_cached_date_str_f,
                   detail::task_timer& task_timer,
                   typename Adaptor::context* adaptor_ctx_,
                   std::atomic<unsigned int>& queue_length,
                   std::shared_ptr<detail::connection_lifecycle_registry> lifecycle_registry = nullptr):
          adaptor_(io_context, adaptor_ctx_), handler_(handler), parser_(this), req_(parser_.req), server_name_(server_name), middlewares_(middlewares), get_cached_date_str(get_cached_date_str_f), task_timer_(task_timer), res_stream_threshold_(handler->stream_threshold()), stream_idle_timeout_(handler->stream_idle_timeout()), stream_write_timeout_(handler->stream_write_timeout()), max_stream_chunk_size_(handler->max_stream_chunk_size()), queue_length_(queue_length), lifecycle_registry_(lifecycle_registry)
        {
            res.deferred_lifecycle_ = std::make_shared<response::deferred_response_lifecycle>();
            queue_length_++;
#ifdef CROW_ENABLE_DEBUG
            connectionCount++;
            CROW_LOG_DEBUG << "Connection (" << this << ") allocated, total: " << connectionCount;
#endif
        }

        ~Connection() {
            destroy_async_chunk_transfer();
            queue_length_--;
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
            if (!routing_handle_result_->rule_index && !routing_handle_result_->catch_all && (req_.method != HTTPMethod::Options || routing_handle_result_->method == HTTPMethod::InternalMethodCount))
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
                static const std::string expect_100_continue = "HTTP/1.1 100 Continue\r\n\r\n";
                buffers_.emplace_back(expect_100_continue.data(), expect_100_continue.size());
                error_code ec = do_write_sync(buffers_);
                if (ec)
                {
                    CROW_LOG_ERROR << ec << " buffer write error happened while handling sending continuation buffer header";
                }
            }
            if (!routing_handle_result_->rule_index && !routing_handle_result_->catch_all && req_.method == HTTPMethod::Options)
            {
                parser_.done();
                need_to_call_after_handlers_ = true;
                complete_request();
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
            req_.io_context = &adaptor_.get_io_context();
            req_.remote_ip_address = adaptor_.address();
            add_keep_alive_ = req_.keep_alive;
            close_connection_ = req_.close_connection;

            if (req_.check_version(1, 1)) // HTTP/1.1
            {
                if (!req_.headers.count("host"))
                {
                    is_invalid_request = true;
                    res = response(400);
                }
                else if (req_.upgrade && req_.method != HTTPMethod::Options)
                {
                    // h2 or h2c headers
                    if (req_.get_header_value("upgrade").find("h2")==0)
                    {
                        // TODO(ipkn): HTTP/2
                        // currently, ignore upgrade header
                    }
                    else
                    {

                        detail::middleware_call_helper<detail::middleware_call_criteria_only_global,
                                                       0, decltype(ctx_), decltype(*middlewares_)>({}, *middlewares_, req_, res, ctx_);
                        close_connection_ = true;
                        if (res.is_completed())
                        {
                            CROW_LOG_INFO << "Request completed before websocket upgrade: " << utility::lexical_cast<std::string>(adaptor_.remote_endpoint()) << " " << this << " HTTP/" << (char)(req_.http_ver_major + '0') << "." << (char)(req_.http_ver_minor + '0') << ' ' << method_name(req_.method) << " " << req_.url;
                            complete_request();

                        }
                        else
                        {
                            handler_->handle_upgrade(req_, res, std::move(adaptor_));
                        }
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
                // The helper must stay callable from foreign threads while the
                // executor closes the socket, so it reads shared atomic state
                // instead of the socket.
                res.is_alive_helper_ = [flag = peer_open_]() -> bool {
                    return flag->load();
                };

                detail::middleware_call_helper<detail::middleware_call_criteria_only_global,
                                               0, decltype(ctx_), decltype(*middlewares_)>({}, *middlewares_, req_, res, ctx_);

                if (!res.completed_)
                {
                    if (!track_connection_lifecycle())
                    {
                        shutdown_on_worker_exit();
                        return;
                    }
                    res.complete_request_handler_ = [self] {
                        asio::dispatch(self->adaptor_.get_io_context(), [self] { self->complete_request(); });
                    };
                    need_to_call_after_handlers_ = true;
                    handler_->handle(req_, res, routing_handle_result_);
                    if (need_to_call_after_handlers_)
                        parser_.stop_after_message();
                    // After the handler returns the response may be deferred and
                    // owned by another thread; the header writer emits the
                    // keep-alive field from add_keep_alive_ instead.
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
            // The IOCP scheduler can run handlers queued before io_context::stop();
            // a stopped worker must not finalize: the connection stays tracked and
            // worker shutdown reports the response instead.
            if (adaptor_.get_io_context().stopped())
            {
                return;
            }
            untrack_connection_lifecycle();
            CROW_LOG_INFO << "Response: " << this << ' ' << req_.raw_url << ' ' << res.code << ' ' << close_connection_;
            {
                // Serialized with foreign-thread is_alive() readers.
                std::lock_guard<std::mutex> lifecycle_lock(res.deferred_lifecycle_->mutex);
                res.is_alive_helper_ = nullptr;
            }

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
            // A chunked response discards res.body, so compressing it would
            // only stamp a stale Content-Encoding onto uncompressed chunks.
            // Interim and bodyless statuses discard res.body below for the
            // same reason.
            if (!res.is_chunked_type() && !res.body.empty() && handler_->compression_used() &&
                res.code >= 200 && response_status_allows_body())
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

            if (res.code < 200)
            {
                // An interim (1xx) status cannot be a final response: the
                // client would keep waiting for the real one. The dedicated
                // 100-continue and upgrade paths write interim responses
                // directly; here the status is a handler mistake.
                res.code = 500;
                res.body.clear();
                res.file_info = response::static_file_info{};
                res.async_chunk_provider_ = nullptr;
                res.body_source_ = response::body_source_kind::none;
                res.headers.erase("Content-Length");
                res.headers.erase("Transfer-Encoding");
                res.headers.erase("Content-Encoding");
                res.headers.erase("Trailer");
                res.manual_length_header = false;
            }

            if (res.completed_ && res.skip_body)
            {
                // HEAD representation metadata is captured after middleware and
                // compression, so it matches what the equivalent GET would send.
                if (res.is_chunked_type())
                {
                    // HEAD keeps "Transfer-Encoding: chunked" and omits "Content-Length".
                    res.async_chunk_provider_ = nullptr;
                    res.body = "";
                    res.manual_length_header = true;
                }
                else
                {
                    if (!res.is_static_type() && !res.body.empty())
                    {
                        res.set_header("Content-Length", std::to_string(res.body.size()));
                        res.manual_length_header = true;
                    }
                    res.body = "";
                    if (res.is_static_type())
                    {
                        res.manual_length_header = true;
                    }
                }
            }

            // An application-supplied "Connection: close" commits the server
            // to closing after this response (RFC 9112 §9.6), and a closing
            // connection must not advertise Keep-Alive.
            if (!close_connection_ && response_connection_header_requests_close())
            {
                close_connection_ = true;
            }
            if (close_connection_)
            {
                add_keep_alive_ = false;
                if (!req_.close_connection && !res.headers.count("connection"))
                {
                    // A server-initiated close is made explicit when the
                    // application did not; a client that itself asked for
                    // close needs no echo.
                    res.set_header("Connection", "close");
                }
            }

            // Bodyless statuses neutralize the provider first: a bodyless
            // response needs no chunked coding, so HTTP/1.0 clients can
            // receive it instead of the chunked-coding rejection.
            if (!response_status_allows_body())
            {
                suppress_response_body_for_status();
            }
            else if (req_.http_ver_major == 1 && req_.http_ver_minor == 0 && res.is_chunked_type())
            {
                reject_http_1_0_chunked_response();
            }
            if (res.is_chunked_type())
            {
                // Framing headers may have been touched after the provider was
                // installed; a chunked body carries exactly one Transfer-Encoding
                // and no Content-Length. Crow never sends trailer fields, so an
                // application "Trailer" header must not survive either.
                res.headers.erase("Content-Length");
                res.headers.erase("Transfer-Encoding");
                res.headers.erase("Trailer");
                res.set_header("Transfer-Encoding", "chunked");
                res.manual_length_header = true;
            }
            prepare_buffers();
            if (res.skip_body)
            {
                // Header preparation may synthesize an error representation. HEAD reports
                // its length while omitting its bytes from the response message.
                res.body.clear();
            }

            const bool write_static = res.is_static_type() && !res.skip_body;
            const bool write_chunked = res.is_chunked_type() && !res.skip_body;

            // A synchronous write can immediately parse a pipelined request. Reset the
            // completed request's flags before entering any write path so reentrant
            // routing sees independent response state.
            res.manual_length_header = false;
            res.skip_body = false;

            if (write_static)
            {
                do_write_static();
            }
            else if (write_chunked)
            {
                do_write_async_chunked();
            }
            else
            {
                do_write_general();
            }

        }

    private:
        bool response_status_allows_body() const noexcept
        {
            // Callers running before the 1xx normalization in
            // complete_request() must exclude interim statuses themselves.
            return res.code != status::NO_CONTENT && res.code != status::RESET_CONTENT &&
                   res.code != status::NOT_MODIFIED;
        }

        bool response_connection_header_requests_close() const
        {
            const auto connection_headers = res.headers.equal_range("connection");
            for (auto field = connection_headers.first; field != connection_headers.second; ++field)
            {
                const std::string& value = field->second;
                std::size_t token_start = 0;
                while (token_start <= value.size())
                {
                    std::size_t token_end = value.find(',', token_start);
                    if (token_end == std::string::npos)
                        token_end = value.size();
                    const std::size_t begin = value.find_first_not_of(" \t", token_start);
                    if (begin != std::string::npos && begin < token_end)
                    {
                        const std::size_t end = value.find_last_not_of(" \t", token_end - 1) + 1;
                        if (utility::string_equals(std::string_view(value.data() + begin, end - begin), "close"))
                            return true;
                    }
                    token_start = token_end + 1;
                }
            }
            return false;
        }

        void suppress_response_body_for_status()
        {
            res.body.clear();
            res.file_info = response::static_file_info{};
            res.async_chunk_provider_ = nullptr;
            res.body_source_ = response::body_source_kind::none;
            res.manual_length_header = true;
            // Crow never sends a trailer section, so the header must not
            // survive on a bodyless response either. Content-Encoding stays:
            // 204 and 304 may carry representation metadata (RFC 9110
            // §15.3.5, §15.4.5).
            res.headers.erase("Trailer");

            if (res.code == status::NO_CONTENT)
            {
                res.headers.erase("Content-Length");
                res.headers.erase("Transfer-Encoding");
            }
            else if (res.code == status::RESET_CONTENT)
            {
                // 205 forbids content but is framed as an empty representation:
                // without an explicit zero length a keep-alive client would
                // wait for close-delimited content. The forced empty
                // representation carries no coding, so a leftover
                // Content-Encoding would misdescribe it.
                res.headers.erase("Transfer-Encoding");
                res.headers.erase("Content-Length");
                res.headers.erase("Content-Encoding");
                res.set_header("Content-Length", "0");
            }
            else
            {
                // 304 keeps representation metadata such as Content-Length;
                // hop-by-hop transfer framing must not survive on a bodyless
                // response.
                res.headers.erase("Transfer-Encoding");
            }
        }

        void prepare_buffers()
        {
            {
                // Serialized with foreign-thread is_alive() readers.
                std::lock_guard<std::mutex> lifecycle_lock(res.deferred_lifecycle_->mutex);
                res.complete_request_handler_ = nullptr;
                res.is_alive_helper_ = nullptr;
            }

            if (!adaptor_.is_open())
            {
                //CROW_LOG_DEBUG << this << " delete (socket is closed) " << is_reading << ' ' << is_writing;
                //delete this;
                return;
            }
            res.write_header_into_buffer(buffers_, content_length_, add_keep_alive_, server_name_);
        }

        void reject_http_1_0_chunked_response()
        {
            res.async_chunk_provider_ = nullptr;
            res.body_source_ = response::body_source_kind::none;
            res.body.clear();
            res.code = status::HTTP_VERSION_NOT_SUPPORTED;
            res.manual_length_header = false;
            res.headers.erase("Content-Length");
            res.headers.erase("Transfer-Encoding");
            res.headers.erase("Content-Encoding");
            res.headers.erase("Trailer");
            res.set_header("Connection", "close");
            add_keep_alive_ = false;
            close_connection_ = true;
            res.notify_chunked_completion(false);
        }

        void do_write_static()
        {
            auto completion_handler = std::move(res.chunk_complete_);
            res.chunk_complete_ = nullptr;
            const std::string file_path = res.file_info.path;
            const auto expected_file_size = static_cast<std::uintmax_t>(res.file_info.statbuf.st_size);
            error_code ec;
            asio::write(adaptor_.socket(), buffers_, ec);
            bool write_failed = static_cast<bool>(ec);
            if (ec)
            {
                CROW_LOG_ERROR << ec << " - buffer write error happened while sending static response headers.";
            }

            if (!write_failed && res.file_info.statResult == 0)
            {
                std::ifstream is(file_path.c_str(), std::ios::in | std::ios::binary);
                std::vector<asio::const_buffer> buffers{1};
                char buf[16384];
                std::uintmax_t bytes_read = 0;
                if (!is)
                {
                    write_failed = true;
                    CROW_LOG_ERROR << "Unable to open static response file " << file_path << ".";
                }
                while (!write_failed && bytes_read < expected_file_size)
                {
                    const auto remaining = expected_file_size - bytes_read;
                    const auto read_size = static_cast<std::streamsize>(std::min<std::uintmax_t>(sizeof(buf), remaining));
                    is.read(buf, read_size);
                    const auto count = is.gcount();
                    if (count <= 0)
                    {
                        write_failed = true;
                        CROW_LOG_ERROR << "Static response file " << file_path
                                       << " ended before its advertised Content-Length.";
                        break;
                    }
                    buffers[0] = asio::buffer(buf, is.gcount());
                    ec = do_write_sync(buffers);
                    if (ec)
                    {
                        write_failed = true;
                        CROW_LOG_ERROR << ec << " - buffer write error happened while sending content of file "
                                       << file_path << ". Writing stopped premature.";
                        break;
                    }
                    bytes_read += static_cast<std::uintmax_t>(count);
                }
                if (!write_failed && (is.bad() || bytes_read != expected_file_size))
                {
                    write_failed = true;
                    CROW_LOG_ERROR << "Unable to read the complete static response file " << file_path << ".";
                }
            }
            if (close_connection_ || write_failed)
            {
                adaptor_.shutdown_readwrite();
                close_adaptor();
                CROW_LOG_DEBUG << this << " from write (static)";
            }

            response::invoke_chunked_completion(std::move(completion_handler), !write_failed);

            res.end();
            res.clear();
            buffers_.clear();
            parser_.clear();

            if (!close_connection_ && !write_failed && need_to_start_read_after_complete_)
            {
                resume_input_after_response();
            }
            else if (write_failed)
            {
                need_to_start_read_after_complete_ = false;
            }
        }

        /// Format a chunk size the way chunked transfer encoding wants it: lowercase hex.
        static std::string chunk_size_to_hex(std::size_t value)
        {
            static const char digits[] = "0123456789abcdef";
            if (value == 0)
            {
                return "0";
            }
            std::string out;
            while (value != 0) {
                out.insert(out.begin(), digits[value & 0xF]);
                value >>= 4;
            }
            return out;
        }

        enum class AsyncChunkPhase {
            writing_headers,
            requesting_chunk,
            writing_chunk,
            writing_terminator,
            completed,
            aborted
        };

        struct AsyncChunkTransfer;

        struct AsyncChunkRequest {
            std::mutex mutex;
            bool completed{false};
            bool active{true};
            asio::io_context* io_context{nullptr};
            std::weak_ptr<Connection> connection;
            std::weak_ptr<AsyncChunkTransfer> transfer;
        };

        template<typename CompletionHandler>
        static void post_async_chunk_completion(asio::io_context& io_context, CompletionHandler&& handler) {
#ifdef CROW_ENABLE_ASYNC_CHUNK_PUBLICATION_TEST_HOOK
            detail::invoke_async_chunk_publication_test_hook();
#endif
            asio::post(io_context, std::forward<CompletionHandler>(handler));
        }

        static void log_async_chunk_publication_failure(const char* message,
                                                        const std::exception_ptr& failure) noexcept {
            if (!failure) {
                return;
            }

            try {
                try {
                    std::rethrow_exception(failure);
                } catch (const std::exception& e) {
                    CROW_LOG_ERROR << message << ": " << e.what();
                } catch (...) {
                    CROW_LOG_ERROR << message << ".";
                }
            } catch (...) {
                // Publication errors must not escape through logging on provider threads.
            }
        }

        static void log_duplicate_async_chunk_completion() noexcept {
            try {
                CROW_LOG_WARNING << "An asynchronous chunk completion callback was invoked more than once.";
            } catch (...) {
                // Logging must not throw through the provider thread.
            }
        }

        struct AsyncChunkTransfer {
            ~AsyncChunkTransfer() {
                notify_completion(false);
            }

            void notify_completion(bool clean) noexcept {
                response::chunk_complete_t handler;
                {
                    // Once-only: completion may run from the worker or from destruction.
                    std::lock_guard<std::mutex> lock(completion_mutex);
                    if (completion_reported) {
                        return;
                    }
                    completion_reported = true;
                    handler             = std::move(completion_handler);
                }

                if (!handler) {
                    return;
                }

                try {
                    handler(clean);
                } catch (const std::exception& e) {
                    CROW_LOG_ERROR << "An uncaught exception occurred in the chunked completion handler: " << e.what();
                } catch (...) {
                    CROW_LOG_ERROR << "An uncaught exception occurred in the chunked completion handler.";
                }
            }

            AsyncChunkPhase phase{AsyncChunkPhase::writing_headers};
            response::async_chunk_provider_t provider;
            response::chunk_complete_t completion_handler;
            std::string header_bytes;
            std::string chunk;
            std::string chunk_header;
            std::vector<asio::const_buffer> buffers;
            std::shared_ptr<AsyncChunkRequest> pending_request;
            detail::task_timer::identifier_type idle_task_id{};
            bool idle_task_armed{false};
            std::mutex completion_mutex;
            bool completion_reported{false};
        };

        /// Completion condition for the transfer writes: it forwards to
        /// `transfer_all()` and marks the moment the socket accepted more bytes.
        ///
        /// The write limit must measure a stalled transfer, not the wall-clock
        /// time one write takes. A consumer that reads slowly keeps a large chunk
        /// in flight for far longer than any sane limit while the connection is
        /// perfectly healthy, and closing the socket underneath such a write
        /// truncates the body without a terminating frame. Marking progress here
        /// and judging it in the timer task keeps a stopped peer bounded while
        /// letting a slow one take as long as it needs, and costs nothing per
        /// write beyond a timestamp.
        ///
        /// `bytes_transferred` is cumulative for the whole composed operation, so
        /// progress is a strict increase rather than a non-zero value.
        auto mark_write_progress()
        {
            auto self = this->shared_from_this();
            auto previous = std::make_shared<std::size_t>(0);
            return [self, previous](const error_code& ec, std::size_t bytes_transferred) -> std::size_t {
                if (!ec && bytes_transferred > *previous)
                {
                    *previous = bytes_transferred;
                    self->last_write_progress_ = std::chrono::steady_clock::now();
                }
                return asio::transfer_all()(ec, bytes_transferred);
            };
        }

        /// Arm the stream write limit. Zero disables it: the transfer then relies
        /// on the peer watch and on whatever limits the body producer keeps.
        void start_write_deadline()
        {
            cancel_deadline_timer();
            if (stream_write_timeout_ == 0)
            {
                return;
            }
            last_write_progress_ = std::chrono::steady_clock::now();
            schedule_write_deadline(stream_write_timeout_);
        }

        /// Schedules the write limit task without cancelling first, so the task
        /// itself may re-arm: cancelling from inside the task would erase the
        /// entry the timer is currently iterating over.
        void schedule_write_deadline(std::uint8_t seconds)
        {
            auto self = this->shared_from_this();
            task_id_ = task_timer_.schedule([self] { self->on_write_deadline(); }, seconds);
            deadline_armed_ = true;
        }

        /// The limit expired on the calendar. If bytes moved in the meantime the
        /// transfer is healthy and the task waits out the remainder; otherwise the
        /// consumer stopped taking the body and the connection is released.
        void on_write_deadline()
        {
            // The task has fired and the timer drops it right after this returns.
            deadline_armed_ = false;
            if (!adaptor_.is_open() || !async_chunk_transfer_ || stream_write_timeout_ == 0)
            {
                return;
            }

            const auto idle = std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::steady_clock::now() - last_write_progress_)
                                .count();
            if (idle < static_cast<std::int64_t>(stream_write_timeout_))
            {
                // Re-arming is posted rather than scheduled here: the timer is
                // iterating its task list around this call, and inserting into it
                // from inside that walk is asking for trouble.
                const auto remaining = static_cast<std::uint8_t>(stream_write_timeout_ - idle);
                auto self = this->shared_from_this();
                asio::post(adaptor_.get_io_context(), [self, remaining] {
                    if (self->adaptor_.is_open() && self->async_chunk_transfer_ && !self->deadline_armed_)
                    {
                        self->schedule_write_deadline(remaining);
                    }
                });
                return;
            }

            CROW_LOG_ERROR << "The stream write made no progress within the limit; closing the connection.";
            adaptor_.shutdown_readwrite();
            close_adaptor();
        }

        void do_write_async_chunked() {
            // Every socket write of the transfer runs under the stream write
            // limit, which measures stalled progress (see mark_write_progress);
            // only the provider wait is unbounded by default.
            start_write_deadline();

            auto state                = std::make_shared<AsyncChunkTransfer>();
            state->provider           = std::move(res.async_chunk_provider_);
            res.async_chunk_provider_ = nullptr;
            state->completion_handler = std::move(res.chunk_complete_);
            res.chunk_complete_       = nullptr;
            async_chunk_transfer_     = state;
            parser_.stop_after_message();

            auto self = this->shared_from_this();
            if (!track_connection_lifecycle())
            {
                shutdown_on_worker_exit();
                return;
            }
            // The header buffers point into res.headers; the transfer owns a copy
            // so nothing aliases user-reachable state while the write is in flight.
            try
            {
                std::size_t header_size = 0;
                for (const auto& buffer : buffers_)
                {
                    header_size += buffer.size();
                }
                state->header_bytes.reserve(header_size);
                for (const auto& buffer : buffers_)
                {
                    state->header_bytes.append(static_cast<const char*>(buffer.data()), buffer.size());
                }
                buffers_.clear();
                asio::async_write(
                  adaptor_.socket(), asio::buffer(state->header_bytes), mark_write_progress(), [self, state](const error_code& ec, std::size_t /*bytes_transferred*/) {
                      if (self->async_chunk_transfer_ != state)
                      {
                          return;
                      }

                      if (ec)
                      {
                          CROW_LOG_ERROR << ec
                                         << " - buffer write error happened while sending response start / headers. "
                                            "Writing stopped prematurely.";
                          self->finish_async_chunked(state, false, true);
                          return;
                      }

                      state->header_bytes.clear();
                      self->cancel_deadline_timer();
                      self->request_async_chunk(state);
                  });
            }
            catch (...)
            {
                // A failed header copy or write start must not strand the
                // tracked transfer.
                CROW_LOG_ERROR << "Failed to send the response header. Closing connection.";
                buffers_.clear();
                finish_async_chunked(state, false, true);
                return;
            }
        }

        void arm_provider_idle_timeout(const std::shared_ptr<AsyncChunkTransfer>& state)
        {
            if (stream_idle_timeout_ == 0 || async_chunk_transfer_ != state)
            {
                return;
            }

            auto self                                    = this->shared_from_this();
            std::weak_ptr<AsyncChunkTransfer> weak_state = state;
            state->idle_task_id = task_timer_.schedule(
              [self, weak_state] {
                  auto active_state = weak_state.lock();
                  if (!active_state)
                  {
                      return;
                  }

                  active_state->idle_task_armed = false;
                  if (self->async_chunk_transfer_ == active_state && active_state->phase == AsyncChunkPhase::requesting_chunk)
                  {
                      CROW_LOG_WARNING << "Chunk provider exceeded the stream idle limit; aborting the transfer.";
                      self->finish_async_chunked(active_state, false, true);
                  }
              },
              stream_idle_timeout_);
            state->idle_task_armed = true;
        }

        void cancel_provider_idle_timeout(const std::shared_ptr<AsyncChunkTransfer>& state)
        {
            if (!state->idle_task_armed)
            {
                return;
            }

            const auto task_id = state->idle_task_id;
            state->idle_task_armed = false;
            task_timer_.cancel(task_id);
        }

        static void invalidate_async_chunk_request(const std::shared_ptr<AsyncChunkTransfer>& state) {
            auto request = std::move(state->pending_request);
            if (!request) {
                return;
            }

            std::lock_guard<std::mutex> lock(request->mutex);
            request->active     = false;
            request->io_context = nullptr;
            request->connection.reset();
            request->transfer.reset();
        }

        void shutdown_on_worker_exit() noexcept {
            shutting_down_ = true;
            auto state = std::move(async_chunk_transfer_);
            if (state) {
                // The worker has stopped dispatching handlers. Keep buffers referenced by
                // outstanding writes intact until Asio releases their handlers.
                invalidate_async_chunk_request(state);
                cancel_provider_idle_timeout(state);
                state->phase    = AsyncChunkPhase::aborted;
                state->provider = nullptr;
            }

            adaptor_.shutdown_readwrite();
            close_adaptor();

            if (state) {
                untrack_connection_lifecycle();
                state->notify_completion(false);
            }
            else
            {
                response::chunk_complete_t completion_handler;
                bool release_registry_entry = false;
                {
                    std::lock_guard<std::mutex> lifecycle_lock(res.deferred_lifecycle_->mutex);
                    completion_handler = std::move(res.chunk_complete_);
                    res.chunk_complete_ = nullptr;
                    // Release the application's providers; the response is
                    // otherwise left alone for a late end() to observe.
                    res.async_chunk_provider_ = nullptr;
                    res.body_source_ = response::body_source_kind::none;
                    res.is_alive_helper_ = nullptr;
                    if (res.completed_)
                    {
                        // end() already latched, so no later call can release
                        // the registry entry; release it here instead.
                        res.complete_request_handler_ = nullptr;
                        release_registry_entry = true;
                    }
                    else
                    {
                        std::weak_ptr<Connection> weak_self = this->shared_from_this();
                        try
                        {
                            res.complete_request_handler_ = [weak_self] {
                                if (auto self = weak_self.lock())
                                {
                                    self->release_stopped_deferred_response();
                                }
                            };
                        }
                        catch (...)
                        {
                            // On allocation failure the latch still protects a
                            // late end(); the entry is released below.
                            res.complete_request_handler_ = nullptr;
                            release_registry_entry = true;
                        }
                    }
                }
                if (release_registry_entry)
                {
                    untrack_connection_lifecycle();
                }
                response::invoke_chunked_completion(std::move(completion_handler), false);
            }
        }

        void release_stopped_deferred_response() noexcept
        {
            {
                std::lock_guard<std::mutex> lifecycle_lock(res.deferred_lifecycle_->mutex);
                res.complete_request_handler_ = nullptr;
            }
            untrack_connection_lifecycle();
        }

        void destroy_async_chunk_transfer() noexcept {
            shutting_down_ = true;
            auto state = std::move(async_chunk_transfer_);
            if (!state) {
                // Destruction is the fallback report for a deferred response
                // that never started writing.
                res.notify_chunked_completion(false);
                return;
            }

            untrack_connection_lifecycle();

            // Invalidate before destruction so racing completion cannot post to a dead executor.
            invalidate_async_chunk_request(state);
            state->phase    = AsyncChunkPhase::aborted;
            state->provider = nullptr;
            state->buffers.clear();

            adaptor_.shutdown_readwrite();
            close_adaptor();
            state->notify_completion(false);
        }

        bool track_connection_lifecycle()
        {
            auto lifecycle_registry = lifecycle_registry_.lock();
            if (!lifecycle_registry || lifecycle_tracked_)
            {
                return true;
            }
            if (!lifecycle_registry->track(this->shared_from_this()))
            {
                return false;
            }
            lifecycle_tracked_ = true;
            return true;
        }

        void untrack_connection_lifecycle() noexcept
        {
            auto lifecycle_registry = lifecycle_registry_.lock();
            if (lifecycle_registry && lifecycle_tracked_)
            {
                lifecycle_registry->untrack(this);
                lifecycle_tracked_ = false;
            }
        }

        void request_async_chunk(const std::shared_ptr<AsyncChunkTransfer>& state) {
            if (async_chunk_transfer_ != state) {
                return;
            }
            if (adaptor_.get_io_context().stopped())
            {
                finish_async_chunked(state, false, true);
                return;
            }
            try
            {
                request_async_chunk_impl(state);
            }
            catch (...)
            {
                // A failed request setup must not strand the tracked transfer:
                // on the first chunk no peer watch is armed yet and the write
                // deadline was just cancelled, so a throw here would leave the
                // client waiting until worker shutdown.
                CROW_LOG_ERROR << "Failed to request the next chunk. Closing connection.";
                finish_async_chunked(state, false, true);
            }
        }

        void request_async_chunk_impl(const std::shared_ptr<AsyncChunkTransfer>& state)
        {
            state->phase           = AsyncChunkPhase::requesting_chunk;
            auto request           = std::make_shared<AsyncChunkRequest>();
            request->io_context    = &adaptor_.get_io_context();
            request->connection    = this->shared_from_this();
            request->transfer      = state;
            state->pending_request = request;
            arm_provider_idle_timeout(state);

            response::async_chunk_completion_t complete = [request](response::chunk_result result,
                                                                    std::string chunk) mutable -> bool {
                std::unique_lock<std::mutex> lock(request->mutex);
                if (request->completed)
                {
                    lock.unlock();
                    log_duplicate_async_chunk_completion();
                    return false;
                }

                if (!request->active)
                {
                    return false;
                }

                auto self = request->connection.lock();
                if (!self)
                {
                    return false;
                }

                const auto publish = [request](response::chunk_result published_result, std::string published_chunk) {
                    // Posting is unconditional, including when the provider completed inline.
                    post_async_chunk_completion(*request->io_context,
                                                [weak_self = request->connection,
                                                 weak_state = request->transfer,
                                                 published_result,
                                                 published_chunk = std::move(published_chunk)]() mutable {
                                                    auto active_connection = weak_self.lock();
                                                    auto active_state = weak_state.lock();
                                                    if (!active_connection || !active_state)
                                                    {
                                                        return;
                                                    }
                                                    active_connection->handle_async_chunk_result(
                                                      active_state, published_result, std::move(published_chunk));
                                                });
                };

                std::exception_ptr publication_failure;
                try
                {
                    publish(result, std::move(chunk));
                    request->completed = true;
                    return true;
                }
                catch (...)
                {
                    publication_failure = std::current_exception();
                }

                std::exception_ptr abort_publication_failure;
                try
                {
                    publish(response::chunk_result::abort, "");
                    request->completed = true;
                }
                catch (...)
                {
                    abort_publication_failure = std::current_exception();
                }

                lock.unlock();
                log_async_chunk_publication_failure(
                    "Failed to publish an asynchronous chunk completion; abort recovery was attempted",
                    publication_failure);
                log_async_chunk_publication_failure(
                    "Failed to publish asynchronous chunk abort recovery; the transfer remains active for "
                    "shutdown cleanup",
                    abort_publication_failure);
                return false;
            };

            try {
                state->provider(complete);
            } catch (const std::exception& e) {
                CROW_LOG_ERROR << "An uncaught exception occurred in the asynchronous chunk provider: " << e.what();
                complete(response::chunk_result::abort, "");
            } catch (...) {
                CROW_LOG_ERROR << "An uncaught exception occurred in the asynchronous chunk provider.";
                complete(response::chunk_result::abort, "");
            }

            if (async_chunk_transfer_ == state && state->phase == AsyncChunkPhase::requesting_chunk)
            {
                do_read();
            }
        }

        void handle_async_chunk_result(const std::shared_ptr<AsyncChunkTransfer>& state,
                                       response::chunk_result result,
                                       std::string chunk) {
            if (async_chunk_transfer_ != state || state->phase != AsyncChunkPhase::requesting_chunk) {
                return;
            }
            if (adaptor_.get_io_context().stopped())
            {
                finish_async_chunked(state, false, true);
                return;
            }

            invalidate_async_chunk_request(state);
            cancel_provider_idle_timeout(state);

            // The peer watch read stays outstanding: one read and one write may
            // be in flight concurrently on the connection executor.
            apply_async_chunk_result(state, result, std::move(chunk));
        }

        void apply_async_chunk_result(const std::shared_ptr<AsyncChunkTransfer>& state,
                                      response::chunk_result result,
                                      std::string chunk)
        {
            if (async_chunk_transfer_ != state || state->phase != AsyncChunkPhase::requesting_chunk)
            {
                return;
            }

            if (result == response::chunk_result::abort) {
                finish_async_chunked(state, false, true);
                return;
            }

            if (chunk.empty()) {
                if (result == response::chunk_result::done) {
                    write_async_chunk_terminator(state);
                } else {
                    request_async_chunk(state);
                }
                return;
            }

            if (max_stream_chunk_size_ > 0 && chunk.size() > max_stream_chunk_size_)
            {
                CROW_LOG_ERROR << "Chunk provider supplied " << chunk.size()
                               << " bytes, above the configured cap; aborting the transfer.";
                finish_async_chunked(state, false, true);
                return;
            }

            state->phase = AsyncChunkPhase::writing_chunk;
            auto self = this->shared_from_this();
            try
            {
                state->chunk = std::move(chunk);
                state->chunk_header = chunk_size_to_hex(state->chunk.size());
                state->chunk_header += crlf;
                state->buffers.clear();
                state->buffers.reserve(3);
                state->buffers.emplace_back(state->chunk_header.data(), state->chunk_header.size());
                state->buffers.emplace_back(state->chunk.data(), state->chunk.size());
                state->buffers.emplace_back(crlf.data(), crlf.size());

                start_write_deadline();
                asio::async_write(
                  adaptor_.socket(),
                  state->buffers,
                  mark_write_progress(),
                  [self, state, result](const error_code& ec, std::size_t /*bytes_transferred*/) {
                      if (self->async_chunk_transfer_ != state)
                      {
                          return;
                      }

                      if (ec)
                      {
                          CROW_LOG_ERROR
                            << ec << " - buffer write error happened while sending a chunk. Writing stopped prematurely.";
                          self->finish_async_chunked(state, false, true);
                          return;
                      }

                      self->cancel_deadline_timer();
                      state->buffers.clear();
                      state->chunk.clear();
                      state->chunk_header.clear();
                      if (result == response::chunk_result::done)
                      {
                          self->write_async_chunk_terminator(state);
                      }
                      else
                      {
                          self->request_async_chunk(state);
                      }
                  });
            }
            catch (...)
            {
                // A failed frame build or write start must not strand the
                // tracked transfer.
                CROW_LOG_ERROR << "Failed to send a chunk frame. Closing connection.";
                finish_async_chunked(state, false, true);
            }
        }

        void write_async_chunk_terminator(const std::shared_ptr<AsyncChunkTransfer>& state) {
            if (async_chunk_transfer_ != state) {
                return;
            }

            static constexpr char terminator[] = "0\r\n\r\n";
            state->phase = AsyncChunkPhase::writing_terminator;
            auto self = this->shared_from_this();
            try
            {
                state->buffers.clear();
                state->buffers.emplace_back(terminator, sizeof(terminator) - 1);

                start_write_deadline();
                asio::async_write(adaptor_.socket(),
                                  state->buffers,
                                  mark_write_progress(),
                                  [self, state](const error_code& ec, std::size_t /*bytes_transferred*/) {
                                      if (self->async_chunk_transfer_ != state)
                                      {
                                          return;
                                      }

                                      if (ec)
                                      {
                                          CROW_LOG_ERROR << ec
                                                         << " - buffer write error happened while sending the last chunk.";
                                          self->finish_async_chunked(state, false, true);
                                          return;
                                      }

                                      self->cancel_deadline_timer();
                                      self->finish_async_chunked(state, true, false);
                                  });
            }
            catch (...)
            {
                // A failed terminator write start must not strand the tracked
                // transfer.
                CROW_LOG_ERROR << "Failed to send the terminating chunk. Closing connection.";
                finish_async_chunked(state, false, true);
            }
        }

        void finish_async_chunked(const std::shared_ptr<AsyncChunkTransfer>& state, bool clean, bool force_close) {
            if (async_chunk_transfer_ != state) {
                return;
            }
            cancel_deadline_timer();

            const bool resume_input = clean && !force_close && !close_connection_ && need_to_start_read_after_complete_;
            state->phase = clean ? AsyncChunkPhase::completed : AsyncChunkPhase::aborted;
            invalidate_async_chunk_request(state);
            cancel_provider_idle_timeout(state);
            state->provider = nullptr;
            state->buffers.clear();
            async_chunk_transfer_.reset();
            untrack_connection_lifecycle();

            if (force_close) {
                adaptor_.shutdown_readwrite();
                close_adaptor();
                CROW_LOG_DEBUG << this << " from write (async chunked, aborted or write error)";
            }

            state->notify_completion(clean);

            if (close_connection_ && !force_close) {
                adaptor_.shutdown_readwrite();
                close_adaptor();
                CROW_LOG_DEBUG << this << " from write (async chunked)";
            }

            res.end();
            res.clear();
            buffers_.clear();
            parser_.clear();

            if (resume_input)
            {
                resume_input_after_response();
            }
            else
            {
                need_to_start_read_after_complete_ = false;
            }
        }

        void resume_input_after_response()
        {
            need_to_start_read_after_complete_ = false;
            start_deadline();
            do_read();
        }

        void do_write_general()
        {
            error_code ec;
            bool write_failed = false;
            auto completion_handler = std::move(res.chunk_complete_);
            res.chunk_complete_ = nullptr;
            if (res.body.length() < res_stream_threshold_)
            {
                res_body_copy_.swap(res.body);
                buffers_.emplace_back(res_body_copy_.data(), res_body_copy_.size());

                ec = do_write_sync(buffers_);
                write_failed = static_cast<bool>(ec);
                if (write_failed) {
                    CROW_LOG_ERROR << ec << " - buffer write error happened while sending response. Writing stopped premature.";
                }
            }
            else
            {
                asio::write(adaptor_.socket(), buffers_, ec); // Write the response start / headers
                write_failed = static_cast<bool>(ec);
                if (write_failed) {
                    CROW_LOG_ERROR << ec
                                   << " - buffer write error happened while sending response start / headers. Writing "
                                      "stopped premature.";
                }
                cancel_deadline_timer();
                if (!write_failed && !res.body.empty()) {
                    std::vector<asio::const_buffer> buffers{1};
                    const uint8_t* data = reinterpret_cast<const uint8_t*>(res.body.data());
                    size_t length = res.body.length();
                    for (size_t transferred = 0; transferred < length;)
                    {
                        size_t to_transfer = CROW_MIN(16384UL, length - transferred);
                        buffers[0] = asio::const_buffer(data + transferred, to_transfer);
                        asio::write(adaptor_.socket(), buffers, ec);
                        if (ec) {
                            write_failed = true;
                            CROW_LOG_ERROR << ec << " - " << transferred << " - buffer write error happened while sending response. Writing stopped premature.";
                            break;
                        }
                        transferred += to_transfer;
                    }
                }

                res.end();
                res.clear();
                buffers_.clear();
                parser_.clear();
            }

            if (completion_handler)
            {
                try
                {
                    completion_handler(!write_failed);
                }
                catch (const std::exception& e)
                {
                    CROW_LOG_ERROR << "An uncaught exception occurred in the chunked completion handler: " << e.what();
                }
                catch (...)
                {
                    CROW_LOG_ERROR << "An uncaught exception occurred in the chunked completion handler.";
                }
            }

            if (close_connection_ || write_failed) {
                adaptor_.shutdown_readwrite();
                close_adaptor();
                need_to_start_read_after_complete_ = false;
                CROW_LOG_DEBUG << this << " from write (res" << (write_failed ? ", write error" : "") << ")";
            } else if (need_to_start_read_after_complete_) {
                resume_input_after_response();
            }
        }

        void do_read()
        {
            if (read_in_progress_ || shutting_down_ || !adaptor_.is_open())
            {
                return;
            }
            read_in_progress_ = true;
            auto self = this->shared_from_this();
            adaptor_.socket().async_read_some(
              asio::buffer(buffer_),
              [self](const error_code& ec, std::size_t bytes_transferred) {
                  self->read_in_progress_ = false;
                  if (self->shutting_down_)
                  {
                      return;
                  }

                  if (auto state = self->async_chunk_transfer_)
                  {
                      // Peer watch while a transfer is active: an error aborts
                      // the stream, extra bytes only mark the connection to
                      // close after it finishes, and the watch stays armed.
                      if (ec)
                      {
                          self->finish_async_chunked(state, false, true);
                          return;
                      }
                      if (bytes_transferred > 0)
                      {
                          // Early pipelined bytes: the stream finishes
                          // correctly, then the connection closes. The watch
                          // stays armed so a peer that disconnects while the
                          // provider is idle is still detected.
                          self->close_connection_ = true;
                          self->do_read();
                      }
                      return;
                  }

                  if (!ec)
                  {
                      self->process_incoming_input(self->buffer_.data(), bytes_transferred);
                      return;
                  }

                  self->close_after_read_error();
              });
        }

        void process_incoming_input(const char* data, std::size_t length)
        {
            std::size_t consumed = 0;
            const bool parsed = parser_.feed(data, static_cast<int>(length), consumed);
            if (parsed && consumed < length)
            {
                // Pipelined bytes behind a deferred or streamed response: the
                // current response finishes, then the connection closes.
                close_connection_ = true;
            }

            if (!parsed || consumed > length || !adaptor_.is_open())
            {
                close_after_read_error();
            }
            else if (close_connection_)
            {
                // An in-flight transfer keeps its write deadline until it
                // finishes; close-after-response is decided by the flag alone.
                if (!async_chunk_transfer_)
                {
                    cancel_deadline_timer();
                }
                parser_.done();
                // adaptor will close after write
            }
            else if (async_chunk_transfer_)
            {
                // The transfer owns the connection until it finishes; its
                // completion path decides between keep-alive and close.
                need_to_start_read_after_complete_ = true;
            }
            else if (!need_to_call_after_handlers_)
            {
                start_deadline();
                do_read();
            }
            else
            {
                // res will be completed later by user
                need_to_start_read_after_complete_ = true;
            }
        }

        void close_adaptor()
        {
            // The flag feeds response::is_alive() from any thread; the socket
            // itself is only touched on the connection executor.
            peer_open_->store(false);
            adaptor_.close();
        }

        void close_after_read_error()
        {
            cancel_deadline_timer();
            parser_.done();
            adaptor_.shutdown_read();
            close_adaptor();
            CROW_LOG_DEBUG << this << " from read(1) with description: \""
                           << http_errno_description(static_cast<http_errno>(parser_.http_errno)) << '\"';
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
                          self->close_adaptor();
                          CROW_LOG_DEBUG << self << " from write(1)";
                      }
                  }
                  else
                  {
                      CROW_LOG_DEBUG << self << " from write(2)";
                  }
              });
        }

        inline error_code do_write_sync(std::vector<asio::const_buffer>& buffers)
        {
            error_code ec;
            asio::write(adaptor_.socket(), buffers, ec);
            if (ec)
            {
                // CROW_LOG_ERROR << ec << " - happened while sending buffers";
                CROW_LOG_DEBUG << this << " from write (sync)(2)";
            }

            this->res.clear();
            this->res_body_copy_.clear();
            if (this->continue_requested)
            {
                this->continue_requested = false;
            }
            else
            {
                this->parser_.clear();
            }

            return ec;
        }

        void cancel_deadline_timer()
        {
            // task_timer ids are reused once its task map drains; a stale id
            // must never cancel another connection's timer.
            if (!deadline_armed_)
            {
                return;
            }
            deadline_armed_ = false;
            CROW_LOG_DEBUG << this << " timer cancelled: " << &task_timer_ << ' ' << task_id_;
            task_timer_.cancel(task_id_);
        }

        void start_deadline(/*int timeout = 5*/)
        {
            cancel_deadline_timer();

            auto self = this->shared_from_this();
            task_id_ = task_timer_.schedule([self] {
                self->deadline_armed_ = false;
                if (!self->adaptor_.is_open())
                {
                    return;
                }
                self->adaptor_.shutdown_readwrite();
                self->close_adaptor();
            });
            deadline_armed_ = true;
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
        // Read by response::is_alive() from any thread; written on the
        // connection executor when the socket closes.
        std::shared_ptr<std::atomic<bool>> peer_open_{std::make_shared<std::atomic<bool>>(true)};

        const std::string& server_name_;
        std::vector<asio::const_buffer> buffers_;

        std::string content_length_;
        std::string date_str_;
        std::string res_body_copy_;
        std::shared_ptr<AsyncChunkTransfer> async_chunk_transfer_;

        detail::task_timer::identifier_type task_id_{};

        bool continue_requested{};
        bool need_to_call_after_handlers_{};
        bool need_to_start_read_after_complete_{};
        bool add_keep_alive_{};
        bool read_in_progress_{};
        bool deadline_armed_{};
        bool shutting_down_{};
        bool lifecycle_tracked_{};

        std::tuple<Middlewares...>* middlewares_;
        detail::context<Middlewares...> ctx_;

        std::function<std::string()>& get_cached_date_str;
        detail::task_timer& task_timer_;

        size_t res_stream_threshold_;
        uint8_t stream_idle_timeout_;
        uint8_t stream_write_timeout_;
        /// Last moment a transfer write was accepted by the socket; the write limit
        /// is measured from here rather than from the start of the write.
        std::chrono::steady_clock::time_point last_write_progress_{};
        size_t max_stream_chunk_size_;

        std::atomic<unsigned int>& queue_length_;
        std::weak_ptr<detail::connection_lifecycle_registry> lifecycle_registry_;
    };

} // namespace crow
