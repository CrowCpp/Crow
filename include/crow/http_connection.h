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
#include <limits>
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

        void shutdown_all() noexcept {
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
          adaptor_(io_context, adaptor_ctx_), handler_(handler), parser_(this), req_(parser_.req), server_name_(server_name), middlewares_(middlewares), get_cached_date_str(get_cached_date_str_f), task_timer_(task_timer), res_stream_threshold_(handler->stream_threshold()), queue_length_(queue_length), lifecycle_registry_(lifecycle_registry)
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
                    if (add_keep_alive_ && !res.completed_)
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
            untrack_connection_lifecycle();
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
            if (!res.body.empty() && handler_->compression_used())
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

            if (req_.http_ver_major == 1 && req_.http_ver_minor == 0 && res.is_chunked_type())
            {
                reject_http_1_0_chunked_response();
            }
            else if (!response_status_allows_body())
            {
                suppress_response_body_for_status();
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
                do_write_chunked();
            }
            else
            {
                do_write_general();
            }

        }

    private:
        bool response_status_allows_body() const noexcept
        {
            return res.code != status::CONTINUE && res.code != status::SWITCHING_PROTOCOLS &&
                   res.code != status::NO_CONTENT && res.code != status::NOT_MODIFIED;
        }

        void suppress_response_body_for_status()
        {
            res.body.clear();
            res.file_info = response::static_file_info{};
            res.chunk_provider_ex_ = nullptr;
            res.async_chunk_provider_ = nullptr;
            res.body_source_ = response::body_source_kind::none;
            res.manual_length_header = true;

            if (res.code < 200 || res.code == status::NO_CONTENT)
            {
                res.headers.erase("Content-Length");
                res.headers.erase("Transfer-Encoding");
            }
        }

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
            res.write_header_into_buffer(buffers_, content_length_, add_keep_alive_, server_name_);
        }

        void reject_http_1_0_chunked_response()
        {
            res.chunk_provider_ex_ = nullptr;
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
                adaptor_.close();
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
                buffered_input_.clear();
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
            std::string chunk;
            std::string chunk_header;
            std::vector<asio::const_buffer> buffers;
            std::shared_ptr<AsyncChunkRequest> pending_request;
            detail::task_timer::identifier_type lifetime_task_id{};
            bool lifetime_task_armed{false};
            bool read_watch_active{false};
            bool read_watch_cancel_requested{false};
            bool retained_input_overflow{false};
            bool pending_result_ready{false};
            response::chunk_result pending_result{response::chunk_result::abort};
            std::string pending_result_chunk;
            std::mutex completion_mutex;
            bool completion_reported{false};
        };

        void do_write_chunked() {
            if (res.body_source_ == response::body_source_kind::asynchronous_chunked)
            {
                do_write_async_chunked();
                return;
            }

            do_write_sync_chunked();
        }

        void do_write_async_chunked() {
            cancel_deadline_timer();

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
            asio::async_write(
                adaptor_.socket(), buffers_, [self, state](const error_code& ec, std::size_t /*bytes_transferred*/) {
                    if (self->async_chunk_transfer_ != state) {
                        return;
                    }

                    if (ec) {
                        CROW_LOG_ERROR << ec
                                       << " - buffer write error happened while sending response start / headers. "
                                          "Writing stopped prematurely.";
                        self->finish_async_chunked(state, false, true);
                        return;
                    }

                    self->buffers_.clear();
                    self->request_async_chunk(state);
                });
        }

        void arm_async_chunk_lifetime(const std::shared_ptr<AsyncChunkTransfer>& state) {
            if (async_chunk_transfer_ != state) {
                return;
            }

            auto self                                    = this->shared_from_this();
            std::weak_ptr<AsyncChunkTransfer> weak_state = state;
            // task_timer owns scheduled callbacks outside Connection and destroys them
            // when the worker exits. Renewing the longest timer interval keeps an
            // arbitrarily slow provider alive without imposing a provider deadline.
            state->lifetime_task_id = task_timer_.schedule(
              [self, weak_state] {
                  auto active_state = weak_state.lock();
                  if (!active_state)
                  {
                      return;
                  }

                  active_state->lifetime_task_armed = false;
                  active_state->lifetime_task_id = 0;
                  if (self->async_chunk_transfer_ == active_state && active_state->phase == AsyncChunkPhase::requesting_chunk)
                  {
                      self->arm_async_chunk_lifetime(active_state);
                  }
              },
              std::numeric_limits<uint8_t>::max());
            state->lifetime_task_armed = true;
        }

        void release_async_chunk_lifetime(const std::shared_ptr<AsyncChunkTransfer>& state) {
            if (!state->lifetime_task_armed) {
                return;
            }

            const auto task_id         = state->lifetime_task_id;
            state->lifetime_task_armed = false;
            state->lifetime_task_id    = 0;
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
            buffered_input_.clear();
            if (state) {
                // The worker has stopped dispatching handlers. Keep buffers referenced by
                // outstanding writes intact until Asio releases their handlers.
                invalidate_async_chunk_request(state);
                release_async_chunk_lifetime(state);
                state->phase    = AsyncChunkPhase::aborted;
                state->provider = nullptr;
            }

            adaptor_.shutdown_readwrite();
            adaptor_.close();

            if (state) {
                untrack_connection_lifecycle();
                state->notify_completion(false);
            }
            else
            {
                std::unique_lock<std::recursive_mutex> lifecycle_lock(res.deferred_lifecycle_->mutex);
                res.deferred_lifecycle_->stopped = true;
                auto completion_handler = std::move(res.chunk_complete_);
                res.chunk_complete_ = nullptr;
                res.clear();
                res.is_alive_helper_ = nullptr;
                std::weak_ptr<Connection> weak_self = this->shared_from_this();
                res.complete_request_handler_ = [weak_self] {
                    if (auto self = weak_self.lock())
                    {
                        self->release_stopped_deferred_response();
                    }
                };
                lifecycle_lock.unlock();
                response::invoke_chunked_completion(std::move(completion_handler), false);
            }
        }

        void release_stopped_deferred_response() noexcept
        {
            {
                std::lock_guard<std::recursive_mutex> lifecycle_lock(res.deferred_lifecycle_->mutex);
                res.complete_request_handler_ = nullptr;
            }
            untrack_connection_lifecycle();
        }

        void destroy_async_chunk_transfer() noexcept {
            shutting_down_ = true;
            auto state = std::move(async_chunk_transfer_);
            buffered_input_.clear();
            if (!state) {
                return;
            }

            untrack_connection_lifecycle();

            // Invalidate before destruction so racing completion cannot post to a dead executor.
            invalidate_async_chunk_request(state);
            state->phase    = AsyncChunkPhase::aborted;
            state->provider = nullptr;
            state->buffers.clear();

            adaptor_.shutdown_readwrite();
            adaptor_.close();
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
            if (state->retained_input_overflow)
            {
                finish_async_chunked(state, false, true);
                return;
            }
            state->phase           = AsyncChunkPhase::requesting_chunk;
            auto request           = std::make_shared<AsyncChunkRequest>();
            request->io_context    = &adaptor_.get_io_context();
            request->connection    = this->shared_from_this();
            request->transfer      = state;
            state->pending_request = request;
            arm_async_chunk_lifetime(state);

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
                do_read(state);
            }
        }

        void handle_async_chunk_result(const std::shared_ptr<AsyncChunkTransfer>& state,
                                       response::chunk_result result,
                                       std::string chunk) {
            if (async_chunk_transfer_ != state || state->phase != AsyncChunkPhase::requesting_chunk) {
                return;
            }

            invalidate_async_chunk_request(state);
            release_async_chunk_lifetime(state);

            if (state->read_watch_active)
            {
                state->pending_result_ready = true;
                state->pending_result = result;
                state->pending_result_chunk = std::move(chunk);
                state->read_watch_cancel_requested = true;

                error_code cancel_error;
                // No response write is active during requesting_chunk, so the
                // socket-wide cancellation targets only the closure watch.
                adaptor_.raw_socket().cancel(cancel_error);
                if (cancel_error)
                {
                    state->pending_result_ready = false;
                    state->pending_result_chunk.clear();
                    state->read_watch_cancel_requested = false;
                    finish_async_chunked(state, false, true);
                }
                return;
            }

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

            state->phase        = AsyncChunkPhase::writing_chunk;
            state->chunk        = std::move(chunk);
            state->chunk_header = chunk_size_to_hex(state->chunk.size());
            state->chunk_header += crlf;
            state->buffers.clear();
            state->buffers.reserve(3);
            state->buffers.emplace_back(state->chunk_header.data(), state->chunk_header.size());
            state->buffers.emplace_back(state->chunk.data(), state->chunk.size());
            state->buffers.emplace_back(crlf.data(), crlf.size());

            auto self = this->shared_from_this();
            asio::async_write(
                adaptor_.socket(),
                state->buffers,
                [self, state, result](const error_code& ec, std::size_t /*bytes_transferred*/) {
                    if (self->async_chunk_transfer_ != state) {
                        return;
                    }

                    if (ec) {
                        CROW_LOG_ERROR
                          << ec << " - buffer write error happened while sending a chunk. Writing stopped prematurely.";
                        self->finish_async_chunked(state, false, true);
                        return;
                    }

                    state->buffers.clear();
                    state->chunk.clear();
                    state->chunk_header.clear();
                    if (result == response::chunk_result::done) {
                        self->write_async_chunk_terminator(state);
                    } else {
                        self->request_async_chunk(state);
                    }
                });
        }

        void write_async_chunk_terminator(const std::shared_ptr<AsyncChunkTransfer>& state) {
            if (async_chunk_transfer_ != state) {
                return;
            }

            static constexpr char terminator[] = "0\r\n\r\n";
            state->phase = AsyncChunkPhase::writing_terminator;
            state->buffers.clear();
            state->buffers.emplace_back(terminator, sizeof(terminator) - 1);

            auto self = this->shared_from_this();
            asio::async_write(adaptor_.socket(),
                              state->buffers,
                              [self, state](const error_code& ec, std::size_t /*bytes_transferred*/) {
                                  if (self->async_chunk_transfer_ != state) {
                                      return;
                                  }

                                  if (ec) {
                                      CROW_LOG_ERROR << ec
                                                     << " - buffer write error happened while sending the last chunk.";
                                      self->finish_async_chunked(state, false, true);
                                      return;
                                  }

                                  self->finish_async_chunked(state, true, state->retained_input_overflow);
                              });
        }

        void finish_async_chunked(const std::shared_ptr<AsyncChunkTransfer>& state, bool clean, bool force_close) {
            if (async_chunk_transfer_ != state) {
                return;
            }

            const bool resume_input = clean && !force_close && !close_connection_ && need_to_start_read_after_complete_;
            state->phase = clean ? AsyncChunkPhase::completed : AsyncChunkPhase::aborted;
            invalidate_async_chunk_request(state);
            release_async_chunk_lifetime(state);
            state->provider = nullptr;
            state->buffers.clear();
            async_chunk_transfer_.reset();
            untrack_connection_lifecycle();

            if (force_close) {
                adaptor_.shutdown_readwrite();
                adaptor_.close();
                CROW_LOG_DEBUG << this << " from write (async chunked, aborted or write error)";
            }

            state->notify_completion(clean);

            if (close_connection_ && !force_close) {
                adaptor_.shutdown_readwrite();
                adaptor_.close();
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
                buffered_input_.clear();
            }
        }

        void resume_input_after_response()
        {
            need_to_start_read_after_complete_ = false;
            start_deadline();
            if (buffered_input_.empty())
            {
                do_read();
            }
            else
            {
                process_buffered_input();
            }
        }

        void do_write_sync_chunked() {
            error_code ec;
            asio::write(adaptor_.socket(), buffers_, ec); // Write the response start / headers
            if (ec)
            {
                CROW_LOG_ERROR << ec << " - buffer write error happened while sending response start / headers. Writing stopped premature.";
            }

            // Producing the body may take arbitrarily long, so the connection must not be
            // closed by the deadline while chunks are still on their way.
            cancel_deadline_timer();

            // do_write_sync() clears the response on every write, so the provider and the
            // completion handler have to be taken out of it before the loop starts.
            auto provider           = std::move(res.chunk_provider_ex_);
            res.chunk_provider_ex_ = nullptr;
            auto completion_handler = std::move(res.chunk_complete_);
            res.chunk_complete_ = nullptr;

            std::string chunk;
            std::string chunk_header;
            std::vector<asio::const_buffer> buffers{3};
            auto result = provider ? response::chunk_result::more : response::chunk_result::done;
            while (result == response::chunk_result::more && !ec)
            {
                chunk.clear();
                // An exception from the provider must not escape into the Asio stack; it is
                // treated as an abort: no terminating frame, forced close, completion(false).
                try
                {
                    result = provider(chunk);
                }
                catch (const std::exception& e)
                {
                    CROW_LOG_ERROR << "An uncaught exception occurred in the chunk provider: " << e.what();
                    result = response::chunk_result::abort;
                }
                catch (...)
                {
                    CROW_LOG_ERROR << "An uncaught exception occurred in the chunk provider.";
                    result = response::chunk_result::abort;
                }
                if (result == response::chunk_result::abort || chunk.empty())
                {
                    continue;
                }

                chunk_header = chunk_size_to_hex(chunk.size());
                chunk_header += crlf;
                buffers[0] = asio::const_buffer(chunk_header.data(), chunk_header.size());
                buffers[1] = asio::const_buffer(chunk.data(), chunk.size());
                buffers[2] = asio::const_buffer(crlf.data(), crlf.size());
                ec = do_write_sync(buffers);
                if (ec)
                {
                    CROW_LOG_ERROR << ec << " - buffer write error happened while sending a chunk. Writing stopped premature.";
                }
            }

            // The terminating frame marks the body as complete, so it is only sent when the
            // provider finished cleanly and every previous write succeeded.
            if (result == response::chunk_result::done && !ec)
            {
                static constexpr char last_chunk[] = "0\r\n\r\n";
                std::vector<asio::const_buffer> tail{1};
                tail[0] = asio::const_buffer(last_chunk, sizeof(last_chunk) - 1);
                ec = do_write_sync(tail);
                if (ec)
                {
                    CROW_LOG_ERROR << ec << " - buffer write error happened while sending the last chunk.";
                }
            }

            // A write failure leaves the message framing just as incomplete as an explicit
            // abort (the terminating frame never made it out), so the connection policy is
            // the same for both: force the close and never reuse the socket for keep-alive.
            const bool aborted = (result == response::chunk_result::abort);
            const bool force_close = aborted || static_cast<bool>(ec);
            if (force_close)
            {
                // Close the connection forcefully, without the terminating frame, so that the
                // client sees a truncated body instead of a seemingly complete one.
                adaptor_.shutdown_readwrite();
                adaptor_.close();
                CROW_LOG_DEBUG << this << " from write (chunked, " << (aborted ? "aborted" : "write error") << ")";
            }

            if (completion_handler)
            {
                // An exception from the handler must not escape into the Asio stack or skip
                // the cleanup below; it is logged and swallowed.
                try
                {
                    completion_handler(result == response::chunk_result::done && !ec);
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

            if (close_connection_ && !force_close)
            {
                adaptor_.shutdown_readwrite();
                adaptor_.close();
                CROW_LOG_DEBUG << this << " from write (chunked)";
            }

            res.end();
            res.clear();
            buffers_.clear();
            parser_.clear();

            // The deadline was cancelled for the duration of the transfer, so a kept-alive
            // connection has to be put back into reading state explicitly.
            if (!force_close && !close_connection_ && need_to_start_read_after_complete_)
            {
                resume_input_after_response();
            }
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
                adaptor_.close();
                need_to_start_read_after_complete_ = false;
                buffered_input_.clear();
                CROW_LOG_DEBUG << this << " from write (res" << (write_failed ? ", write error" : "") << ")";
            } else if (need_to_start_read_after_complete_) {
                resume_input_after_response();
            }
        }

        void do_read(std::shared_ptr<AsyncChunkTransfer> watch_state = nullptr)
        {
            if (read_in_progress_ || shutting_down_ || !adaptor_.is_open())
            {
                return;
            }
            if (watch_state && buffered_input_.size() > max_header_size)
            {
                finish_async_chunked(watch_state, false, true);
                return;
            }

            if (watch_state)
            {
                watch_state->read_watch_active = true;
            }
            read_in_progress_ = true;
            auto self = this->shared_from_this();
            adaptor_.socket().async_read_some(
              asio::buffer(buffer_),
              [self, watch_state = std::move(watch_state)](const error_code& ec, std::size_t bytes_transferred) {
                  self->read_in_progress_ = false;
                  if (self->shutting_down_)
                  {
                      return;
                  }

                  if (watch_state)
                  {
                      watch_state->read_watch_active = false;
                      if (self->async_chunk_transfer_ == watch_state)
                      {
                          self->handle_async_chunk_read(watch_state, ec, bytes_transferred);
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

        void handle_async_chunk_read(const std::shared_ptr<AsyncChunkTransfer>& state,
                                     const error_code& ec,
                                     std::size_t bytes_transferred)
        {
            if (async_chunk_transfer_ != state)
            {
                return;
            }

            const bool cancel_requested = state->read_watch_cancel_requested;
            state->read_watch_cancel_requested = false;

            if (ec && !(cancel_requested && ec == asio::error::operation_aborted))
            {
                finish_async_chunked(state, false, true);
                return;
            }

            if (!ec)
            {
                const std::size_t retained_input_limit = max_header_size;
                const bool retained_input_overflow = buffered_input_.size() > retained_input_limit || bytes_transferred > retained_input_limit - buffered_input_.size();
                if (retained_input_overflow && !state->pending_result_ready)
                {
                    finish_async_chunked(state, false, true);
                    return;
                }

                if (retained_input_overflow)
                {
                    state->retained_input_overflow = true;
                }
                else
                {
                    buffered_input_.append(buffer_.data(), bytes_transferred);
                }
            }

            if (state->pending_result_ready)
            {
                const auto result = state->pending_result;
                auto chunk = std::move(state->pending_result_chunk);
                state->pending_result_ready = false;
                apply_async_chunk_result(state, result, std::move(chunk));
                return;
            }

            if (async_chunk_transfer_ == state && state->phase == AsyncChunkPhase::requesting_chunk)
            {
                do_read(state);
            }
        }

        void process_incoming_input(const char* data, std::size_t length)
        {
            std::size_t consumed = 0;
            const bool parsed = parser_.feed(data, static_cast<int>(length), consumed);
            if (parsed && consumed <= length && consumed < length)
            {
                buffered_input_.assign(data + consumed, length - consumed);
            }

            if (!parsed || consumed > length || !adaptor_.is_open())
            {
                close_after_read_error();
            }
            else if (close_connection_)
            {
                buffered_input_.clear();
                cancel_deadline_timer();
                parser_.done();
                // adaptor will close after write
            }
            else if (async_chunk_transfer_)
            {
                // The asynchronous transfer owns the connection until it finishes.
                // Its completion path parses buffered input before reading the socket again.
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

        void process_buffered_input()
        {
            std::string input = std::move(buffered_input_);
            buffered_input_.clear();
            process_incoming_input(input.data(), input.size());
        }

        void close_after_read_error()
        {
            buffered_input_.clear();
            cancel_deadline_timer();
            parser_.done();
            adaptor_.shutdown_read();
            adaptor_.close();
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
        std::string buffered_input_;

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
        std::shared_ptr<AsyncChunkTransfer> async_chunk_transfer_;

        detail::task_timer::identifier_type task_id_{};

        bool continue_requested{};
        bool need_to_call_after_handlers_{};
        bool need_to_start_read_after_complete_{};
        bool add_keep_alive_{};
        bool read_in_progress_{};
        bool shutting_down_{};
        bool lifecycle_tracked_{};

        std::tuple<Middlewares...>* middlewares_;
        detail::context<Middlewares...> ctx_;

        std::function<std::string()>& get_cached_date_str;
        detail::task_timer& task_timer_;

        size_t res_stream_threshold_;

        std::atomic<unsigned int>& queue_length_;
        std::weak_ptr<detail::connection_lifecycle_registry> lifecycle_registry_;
    };

} // namespace crow
