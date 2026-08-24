#define CROW_ENABLE_DEBUG
#define CROW_LOG_LEVEL 0
#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <exception>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <vector>
#include <thread>
#include <type_traits>
#include <regex>
#include <stdexcept>

#include "catch2/catch_all.hpp"
#include "crow.h"

#include "test_common.h"

using namespace std;
using namespace crow;


#ifdef CROW_ENABLE_ASYNC_CHUNK_PUBLICATION_TEST_HOOK
namespace crow { namespace detail {
static std::function<void()> async_chunk_publication_test_hook;

void invoke_async_chunk_publication_test_hook() {
    if (async_chunk_publication_test_hook)
        async_chunk_publication_test_hook();
}
}} // namespace crow::detail

class ScopedAsyncChunkPublicationTestHook {
public:
    explicit ScopedAsyncChunkPublicationTestHook(std::function<void()> hook) {
        crow::detail::async_chunk_publication_test_hook = std::move(hook);
    }

    ~ScopedAsyncChunkPublicationTestHook() {
        crow::detail::async_chunk_publication_test_hook = nullptr;
    }

    ScopedAsyncChunkPublicationTestHook(const ScopedAsyncChunkPublicationTestHook&)            = delete;
    ScopedAsyncChunkPublicationTestHook& operator=(const ScopedAsyncChunkPublicationTestHook&) = delete;
};
#endif

bool has_chunk_terminator(const std::string& response)
{
    return response.size() >= 5 && response.compare(response.size() - 5, 5, "0\r\n\r\n") == 0;
}

bool has_complete_http_response(const std::string& response) {
    const auto header_end = response.find("\r\n\r\n");
    if (header_end == std::string::npos)
        return false;

    const std::string content_length_name = "Content-Length:";
    const auto content_length_header      = response.find(content_length_name);
    if (content_length_header == std::string::npos || content_length_header > header_end)
        return false;

    auto value = content_length_header + content_length_name.size();
    while (value < header_end && response[value] == ' ')
        ++value;
    if (value == header_end || response[value] < '0' || response[value] > '9')
        return false;

    std::size_t content_length = 0;
    while (value < header_end && response[value] >= '0' && response[value] <= '9') {
        content_length = content_length * 10 + static_cast<std::size_t>(response[value] - '0');
        ++value;
    }

    return response.size() >= header_end + 4 + content_length;
}

class PausingSocketContext {
public:
    void observe_next_read()
    {
        observe_next_read_.store(true);
    }

    bool report_read_started()
    {
        if (!observe_next_read_.exchange(false))
            return false;

        observed_read_started_.set_value();
        return true;
    }

    void report_read_completed(std::size_t bytes_transferred)
    {
        observed_read_completed_.set_value(bytes_transferred);
    }

    std::future<void> observed_read_started_future()
    {
        return observed_read_started_.get_future();
    }

    std::future<std::size_t> observed_read_completed_future()
    {
        return observed_read_completed_.get_future();
    }

    void pause_read_completion_after(std::size_t byte_count)
    {
        observed_read_bytes_.store(0);
        pause_read_completion_after_.store(byte_count);
    }

    bool take_read_completion_pause(std::size_t bytes_transferred)
    {
        const auto total = observed_read_bytes_.fetch_add(bytes_transferred) + bytes_transferred;
        const auto limit = pause_read_completion_after_.load();
        if (limit == 0 || total <= limit)
            return false;

        pause_read_completion_after_.store(0);
        return true;
    }

    void set_pending_read(std::function<void()> resume)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            resume_read_ = std::move(resume);
        }
        read_pending_.set_value();
    }

    void resume_pending_read()
    {
        std::function<void()> resume;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            resume = std::move(resume_read_);
        }
        if (resume)
            resume();
    }

    std::future<void> pending_read_future()
    {
        return read_pending_.get_future();
    }

    void pause_next_write() {
        pause_next_write_.store(true);
    }

    bool take_pause_request() {
        return pause_next_write_.exchange(false);
    }

    void fail_next_write() {
        fail_next_write_.store(true);
    }

    bool take_failure_request() {
        return fail_next_write_.exchange(false);
    }

    void set_pending_write(std::function<void()> resume) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            resume_write_ = std::move(resume);
        }
        write_pending_.set_value();
    }

    void resume_pending_write() {
        std::function<void()> resume;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            resume = std::move(resume_write_);
        }
        if (resume)
            resume();
    }

    void discard_pending_write() {
        std::function<void()> pending_write;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_write = std::move(resume_write_);
        }
    }

    std::future<void> pending_write_future() {
        return write_pending_.get_future();
    }

    std::future<void> started_connection_destroyed_future() {
        return started_connection_destroyed_.get_future();
    }

    void report_started_connection_destroyed() {
        if (!started_connection_destruction_reported_.exchange(true))
            started_connection_destroyed_.set_value();
    }

private:
    std::atomic<bool> observe_next_read_{false};
    std::atomic<std::size_t> observed_read_bytes_{0};
    std::atomic<std::size_t> pause_read_completion_after_{0};
    std::atomic<bool> pause_next_write_{false};
    std::atomic<bool> fail_next_write_{false};
    std::atomic<bool> started_connection_destruction_reported_{false};
    std::promise<void> observed_read_started_;
    std::promise<std::size_t> observed_read_completed_;
    std::promise<void> read_pending_;
    std::promise<void> write_pending_;
    std::promise<void> started_connection_destroyed_;
    std::mutex mutex_;
    std::function<void()> resume_write_;
    std::function<void()> resume_read_;
};

class PausingSocketAdaptor : public crow::SocketAdaptor {
public:
    using context       = PausingSocketContext;
    using executor_type = asio::ip::tcp::socket::executor_type;

    PausingSocketAdaptor(asio::io_context& io_context, context* socket_context):
      crow::SocketAdaptor(io_context, nullptr), context_(socket_context)
    {
    }

    ~PausingSocketAdaptor() {
        if (started_ && context_)
            context_->report_started_connection_destroyed();
    }

    executor_type get_executor() noexcept {
        return socket_.get_executor();
    }

    asio::io_context& get_io_context() {
        return GET_IO_CONTEXT(socket_);
    }

    asio::ip::tcp::socket& raw_socket() {
        return socket_;
    }

    PausingSocketAdaptor& socket() {
        return *this;
    }

    asio::ip::tcp::endpoint remote_endpoint() const {
        return socket_.remote_endpoint();
    }

    std::string address() const {
        return socket_.remote_endpoint().address().to_string();
    }

    bool is_open() const {
        return socket_.is_open();
    }

    void close() {
        asio_error_code ec;
        socket_.close(ec);
    }

    void shutdown_readwrite() {
        asio_error_code ec;
        socket_.shutdown(asio::socket_base::shutdown_both, ec);
    }

    void shutdown_write() {
        asio_error_code ec;
        socket_.shutdown(asio::socket_base::shutdown_send, ec);
    }

    void shutdown_read() {
        asio_error_code ec;
        socket_.shutdown(asio::socket_base::shutdown_receive, ec);
    }

    template<typename F>
    void start(F complete) {
        started_ = true;
        complete(asio_error_code());
    }

    template<typename MutableBufferSequence, typename ReadHandler>
    void async_read_some(const MutableBufferSequence& buffers, ReadHandler&& handler) {
        const bool observed = context_ && context_->report_read_started();
        auto* io_context = &get_io_context();
        socket_.async_read_some(
          buffers,
          [context = context_, io_context, observed, handler = std::forward<ReadHandler>(handler)](
            const asio_error_code& ec, std::size_t bytes_transferred) mutable {
              if (observed)
                  context->report_read_completed(bytes_transferred);
              if (context && context->take_read_completion_pause(bytes_transferred))
              {
                  context->set_pending_read(
                    [io_context, handler = std::move(handler), ec, bytes_transferred]() mutable {
                        asio::post(*io_context,
                                   [handler = std::move(handler), ec, bytes_transferred]() mutable {
                                       handler(ec, bytes_transferred);
                                   });
                    });
                  return;
              }
              handler(ec, bytes_transferred);
          });
    }

    template<typename ConstBufferSequence>
    std::size_t write_some(const ConstBufferSequence& buffers) {
        return socket_.write_some(buffers);
    }

    template<typename ConstBufferSequence>
    std::size_t write_some(const ConstBufferSequence& buffers, asio_error_code& ec) {
        if (context_ && context_->take_failure_request()) {
            ec = asio::error::operation_aborted;
            return 0;
        }
        return socket_.write_some(buffers, ec);
    }

    template<typename ConstBufferSequence, typename WriteHandler>
    void async_write_some(const ConstBufferSequence& buffers, WriteHandler&& handler) {
        if (context_ && context_->take_failure_request()) {
            auto copied_handler
                = std::make_shared<typename std::decay<WriteHandler>::type>(std::forward<WriteHandler>(handler));
            asio::post(get_io_context(), [copied_handler]() mutable {
                const asio_error_code ec = asio::error::operation_aborted;
                (*copied_handler)(ec, 0);
            });
            return;
        }

        if (!context_ || !context_->take_pause_request()) {
            socket_.async_write_some(buffers, std::forward<WriteHandler>(handler));
            return;
        }

        auto copied_buffers = std::make_shared<std::vector<asio::const_buffer>>();
        for (auto iterator = asio::buffer_sequence_begin(buffers); iterator != asio::buffer_sequence_end(buffers);
             ++iterator)
            copied_buffers->emplace_back(*iterator);
        auto copied_handler
            = std::make_shared<typename std::decay<WriteHandler>::type>(std::forward<WriteHandler>(handler));

        context_->set_pending_write([this, copied_buffers, copied_handler]() mutable {
            asio::post(get_io_context(), [this, copied_buffers, copied_handler]() mutable {
                socket_.async_write_some(*copied_buffers, std::move(*copied_handler));
            });
        });
    }

private:
    context* context_;
    bool started_{false};
};

class ChunkCompletionObservation {
public:
    std::future<bool> first_result() {
        return first_result_.get_future();
    }

    std::future<std::thread::id> first_thread() {
        return first_thread_.get_future();
    }

    void record(bool clean) {
        if (calls_.fetch_add(1) == 0) {
            first_thread_.set_value(std::this_thread::get_id());
            first_result_.set_value(clean);
        }
    }

    std::size_t calls() const {
        return calls_.load();
    }

private:
    std::atomic<std::size_t> calls_{0};
    std::promise<std::thread::id> first_thread_;
    std::promise<bool> first_result_;
};

class DeferredChunkCompletion
{
public:
    std::future_status wait_for(std::chrono::milliseconds timeout)
    {
        return captured_.wait_for(timeout);
    }

    void capture(crow::response::async_chunk_completion_t complete)
    {
        if (capture_reported_.exchange(true))
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            complete_ = std::move(complete);
        }
        captured_promise_.set_value();
    }

    bool complete(crow::chunk_result result, std::string chunk)
    {
        crow::response::async_chunk_completion_t complete;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            complete = std::move(complete_);
        }
        if (!complete)
        {
            return false;
        }

        return complete(result, std::move(chunk));
    }

private:
    std::promise<void> captured_promise_;
    std::future<void> captured_{captured_promise_.get_future()};
    std::atomic<bool> capture_reported_{false};
    std::mutex mutex_;
    crow::response::async_chunk_completion_t complete_;
};

struct PipelinedAsyncObservation
{
    std::atomic<std::size_t> first_route_calls{0};
    std::atomic<std::size_t> provider_calls{0};
    std::atomic<std::size_t> second_route_calls{0};
    std::atomic<bool> first_completion_seen{false};
    std::atomic<bool> second_route_overlapped{false};
    std::atomic<bool> first_body_correct{true};
    DeferredChunkCompletion provider_completion;
    ChunkCompletionObservation completion;
};

class LifecycleRegistryProbe
{
public:
    void shutdown_on_worker_exit() noexcept
    {
        shutdown_calls_.fetch_add(1);
    }

    std::size_t shutdown_calls() const
    {
        return shutdown_calls_.load();
    }

private:
    std::atomic<std::size_t> shutdown_calls_{0};
};

namespace crow
{
    struct connection_test_access
    {
        template<typename Connection>
        static response& res(Connection& connection)
        {
            return connection.res;
        }
    };
} // namespace crow


TEST_CASE("stream_response")
{
    SimpleApp app;


    const std::string keyword_ = "hello";
    const size_t repetitions = 250000;
    const size_t key_response_size = keyword_.length() * repetitions;

    std::string key_response;

    for (size_t i = 0; i < repetitions; i++)
        key_response += keyword_;

    CROW_ROUTE(app, "/test")
    ([&key_response](const crow::request&, crow::response& res) {
        res.body = key_response;
        res.end();
    });

    app.validate();

    // running the test on a separate thread to allow the client to sleep
    std::thread runTest([&app, &key_response, key_response_size, keyword_]() {
        auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
        app.wait_for_server_start();
        asio::io_context io_context;
        std::string sendmsg;

        //Total bytes received
        unsigned int received = 0;
        sendmsg = "GET /test HTTP/1.0\r\n\r\n";
        {
            asio::streambuf b;

            asio::ip::tcp::socket c(io_context);
            c.connect(asio::ip::tcp::endpoint(
              asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
            c.send(asio::buffer(sendmsg));

            // consuming the headers, since we don't need those for the test
            static char buf[2048];
            size_t received_headers_bytes = 0;

            // Magic number is 102. It's the size of the headers, which is at
            // least how much we need to read. Since the header size may change
            // and break the test, we read twice as much as the header and
            // search in the received data for the first occurrence of keyword_.
            const size_t headers_bytes_and_some = 102 * 2;
            while (received_headers_bytes < headers_bytes_and_some)
                received_headers_bytes += c.receive(asio::buffer(buf + received_headers_bytes,
                                                                 sizeof(buf) / sizeof(buf[0]) - received_headers_bytes));

            const std::string::size_type header_end_pos = std::string(buf, received_headers_bytes).find(keyword_);
            received += received_headers_bytes - header_end_pos; // add any extra that might have been received to the proper received count

            while (received < key_response_size)
            {
                asio::streambuf::mutable_buffers_type bufs = b.prepare(16384);

                size_t n(0);
                n = c.receive(bufs);
                b.commit(n);
                received += n;

                std::istream istream(&b);
                std::string s;
                istream >> s;

                CHECK(key_response.substr(received - n, n) == s);
            }
        }
        app.stop();
    });
    runTest.join();
} // stream_response


TEST_CASE("chunked_response")
{
    SimpleApp app;

    CROW_ROUTE(app, "/chunks")
    ([](const crow::request&, crow::response& res) {
        int remaining = 3;
        res.set_chunked_content_provider(
          [remaining](std::string& chunk) mutable -> bool {
              if (remaining == 0)
                  return false;
              chunk = "part" + std::to_string(4 - remaining);
              --remaining;
              return true;
          },
          "text/plain");
        res.end();
    });

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    app.wait_for_server_start();

    HttpClient client(LOCALHOST_ADDRESS, 45451);
    client.send("GET /chunks HTTP/1.1\r\nHost: localhost\r\n\r\n");

    std::string response;
    while (response.size() < 5 || response.compare(response.size() - 5, 5, "0\r\n\r\n") != 0)
        response += client.receive();

    CHECK(response.find("Transfer-Encoding: chunked") != std::string::npos);
    CHECK(response.find("Content-Length") == std::string::npos);
    CHECK(response.find("Content-Type: text/plain") != std::string::npos);
    CHECK(response.find("5\r\npart1\r\n") != std::string::npos);
    CHECK(response.find("5\r\npart2\r\n") != std::string::npos);
    CHECK(response.find("5\r\npart3\r\n") != std::string::npos);

    // The connection is kept alive after a chunked response: a second request on
    // the same connection is served, so the connection went back to reading state.
    client.send("GET /chunks HTTP/1.1\r\nHost: localhost\r\n\r\n");
    std::string second;
    while (second.size() < 5 || second.compare(second.size() - 5, 5, "0\r\n\r\n") != 0)
        second += client.receive();
    CHECK(second.find("Transfer-Encoding: chunked") != std::string::npos);
    CHECK(second.find("5\r\npart1\r\n") != std::string::npos);
    CHECK(second.find("5\r\npart3\r\n") != std::string::npos);

    app.stop();
} // chunked_response


TEST_CASE("chunked_response_canonicalizes_framing_headers")
{
    SimpleApp app;

    CROW_ROUTE(app, "/late-framing-headers")
    ([](const crow::request&, crow::response& res) {
        res.set_chunked_content_provider(
          [sent = false](std::string& chunk) mutable -> bool {
              if (sent)
                  return false;
              chunk = "payload";
              sent = true;
              return true;
          },
          "text/plain");
        // Application code may still touch the framing headers after the
        // provider is installed; the wire must carry exactly one
        // Transfer-Encoding and no Content-Length regardless.
        res.set_header("Content-Length", "999");
        res.add_header("Transfer-Encoding", "chunked");
        res.end();
    });

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    app.wait_for_server_start();

    HttpClient client(LOCALHOST_ADDRESS, 45451);
    client.send("GET /late-framing-headers HTTP/1.1\r\nHost: localhost\r\n\r\n");

    std::string response;
    while (response.size() < 5 || response.compare(response.size() - 5, 5, "0\r\n\r\n") != 0)
        response += client.receive();

    CHECK(response.find("Content-Length") == std::string::npos);
    std::size_t transfer_encoding_count = 0;
    for (std::size_t at = response.find("Transfer-Encoding");
         at != std::string::npos;
         at = response.find("Transfer-Encoding", at + 1))
        ++transfer_encoding_count;
    CHECK(transfer_encoding_count == 1);
    CHECK(response.find("7\r\npayload\r\n") != std::string::npos);

    app.stop();
} // chunked_response_canonicalizes_framing_headers


TEST_CASE("sync_chunked_response_does_not_block_the_worker_for_a_stalled_client")
{
    SimpleApp app;

    auto completion_observation = std::make_shared<ChunkCompletionObservation>();
    auto completion_result = completion_observation->first_result();
    CROW_ROUTE(app, "/stalled-stream")
    ([completion_observation](const crow::request&, crow::response& res) {
        res.set_chunked_content_provider(
          [](std::string& chunk) -> bool {
              // Each chunk stays under the size cap; together they exceed any
              // default socket buffer pair, so writes stall while the client
              // refuses to read.
              chunk.assign(8u * 1024u * 1024u, 'x');
              return true;
          },
          "application/octet-stream");
        res.set_chunked_completion_handler([completion_observation](bool clean) {
            completion_observation->record(clean);
        });
        res.end();
    });
    CROW_ROUTE(app, "/ping")
    ([] {
        return "pong";
    });

    // A generous write deadline: the default 5 s is within reach of this
    // test's own waits on a slow runner, and a deadline abort would fire the
    // completion early.
    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).concurrency(1).timeout(30).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    app.wait_for_server_start();

    // The stalled client requests the stream and never reads the body.
    asio::io_context io_context;
    asio::ip::tcp::socket stalled_client(io_context);
    stalled_client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string stream_request = "GET /stalled-stream HTTP/1.1\r\nHost: localhost\r\n\r\n";
    asio::write(stalled_client, asio::buffer(stream_request));

    // Give the worker time to enter the stalled transfer.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // The single worker must still serve another connection.
    HttpClient ping_client(LOCALHOST_ADDRESS, 45451);
    ping_client.send("GET /ping HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    std::string ping_response;
    const bool ping_answered = receive_until_closed_with_deadline(ping_client.socket(), ping_response, std::chrono::seconds(3));

    // Sampled before the sockets close: peer closure aborts the stalled
    // transfer, and that abort can win the race to the assertion below.
    const auto completion_status_before_close =
      completion_result.wait_for(std::chrono::milliseconds(0));

    asio_error_code close_error;
    stalled_client.close(close_error);
    ping_client.socket().close(close_error);

    REQUIRE(ping_answered);
    CHECK(ping_response.find("pong") != std::string::npos);
    // The stalled transfer was still in flight while the ping was served: the
    // worker was free during a genuinely stalled socket write, and the stream
    // was not aborted by the chunk-size cap.
    CHECK(completion_status_before_close == std::future_status::timeout);
} // sync_chunked_response_does_not_block_the_worker_for_a_stalled_client


TEST_CASE("chunked_response_times_out_writing_to_a_stalled_client")
{
    SimpleApp app;
    auto completion_observation = std::make_shared<ChunkCompletionObservation>();
    auto completion_result = completion_observation->first_result();

    CROW_ROUTE(app, "/stalled-write-deadline")
    ([completion_observation](const crow::request&, crow::response& res) {
        res.set_chunked_content_provider(
          [sent = false](std::string& chunk) mutable -> bool {
              if (sent)
                  return false;
              chunk.assign(8u * 1024u * 1024u, 'x');
              sent = true;
              return true;
          },
          "application/octet-stream");
        res.set_chunked_completion_handler([completion_observation](bool clean) {
            completion_observation->record(clean);
        });
        res.end();
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).timeout(1).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    app.wait_for_server_start();

    asio::io_context io_context;
    asio::ip::tcp::socket stalled_client(io_context);
    stalled_client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string request = "GET /stalled-write-deadline HTTP/1.1\r\nHost: localhost\r\n\r\n";
    asio::write(stalled_client, asio::buffer(request));

    // A client that stops reading trips the write deadline: the transfer is
    // aborted and reported unclean instead of pinning the buffers forever.
    const auto completion_status = completion_result.wait_for(std::chrono::seconds(5));
    REQUIRE(completion_status == std::future_status::ready);
    CHECK(completion_result.get() == false);
    CHECK(completion_observation->calls() == 1);

    asio_error_code close_error;
    stalled_client.close(close_error);
} // chunked_response_times_out_writing_to_a_stalled_client


TEST_CASE("close_marked_transfers_keep_the_write_deadline")
{
    SimpleApp app;
    auto request_close_completion = std::make_shared<ChunkCompletionObservation>();
    auto request_close_result = request_close_completion->first_result();
    auto response_close_completion = std::make_shared<ChunkCompletionObservation>();
    auto response_close_result = response_close_completion->first_result();

    const auto endless_stream = [](const std::shared_ptr<ChunkCompletionObservation>& completion, crow::response& res) {
        res.set_chunked_content_provider(
          [](std::string& chunk) -> bool {
              chunk.assign(8u * 1024u * 1024u, 'x');
              return true;
          },
          "application/octet-stream");
        res.set_chunked_completion_handler([completion](bool clean) {
            completion->record(clean);
        });
        res.end();
    };
    CROW_ROUTE(app, "/stalled-close-request")
    ([endless_stream, request_close_completion](const crow::request&, crow::response& res) {
        endless_stream(request_close_completion, res);
    });
    CROW_ROUTE(app, "/stalled-close-response")
    ([endless_stream, response_close_completion](const crow::request&, crow::response& res) {
        res.set_header("Connection", "close");
        endless_stream(response_close_completion, res);
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).timeout(1).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    app.wait_for_server_start();

    const auto stall = [](const std::string& request_text, std::future<bool>& result) {
        asio::io_context io_context;
        asio::ip::tcp::socket stalled_client(io_context);
        stalled_client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
        asio::write(stalled_client, asio::buffer(request_text));
        // The client never reads: the write deadline must abort the transfer
        // even though the connection is already marked to close.
        const auto completion_status = result.wait_for(std::chrono::seconds(5));
        asio_error_code close_error;
        stalled_client.close(close_error);
        REQUIRE(completion_status == std::future_status::ready);
        return result.get();
    };

    // A request-side "Connection: close" marks the connection before the
    // in-flight transfer submits its first write.
    CHECK(stall("GET /stalled-close-request HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n", request_close_result) == false);
    // An application-supplied "Connection: close" marks it during finalization.
    CHECK(stall("GET /stalled-close-response HTTP/1.1\r\nHost: localhost\r\n\r\n", response_close_result) == false);
    server_shutdown.shutdown();

    CHECK(request_close_completion->calls() == 1);
    CHECK(response_close_completion->calls() == 1);
} // close_marked_transfers_keep_the_write_deadline


TEST_CASE("async_chunked_response_aborts_an_idle_provider_when_configured")
{
    SimpleApp app;
    auto completion_observation = std::make_shared<ChunkCompletionObservation>();
    auto completion_result = completion_observation->first_result();
    auto provider_calls = std::make_shared<std::atomic<std::size_t>>(0);

    CROW_ROUTE(app, "/idle-provider")
    ([completion_observation, provider_calls](const crow::request&, crow::response& res) {
        res.set_async_chunked_content_provider(
          [provider_calls](crow::response::async_chunk_completion_t) {
              provider_calls->fetch_add(1);
          });
        res.set_chunked_completion_handler([completion_observation](bool clean) {
            completion_observation->record(clean);
        });
        res.end();
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).stream_idle_timeout(1).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    app.wait_for_server_start();

    HttpClient client(LOCALHOST_ADDRESS, 45451);
    client.send("GET /idle-provider HTTP/1.1\r\nHost: localhost\r\n\r\n");

    std::string response;
    const bool connection_closed = receive_until_closed_with_deadline(client.socket(), response, std::chrono::seconds(5));

    const auto completion_status = completion_result.wait_for(std::chrono::seconds(1));
    REQUIRE(completion_status == std::future_status::ready);
    CHECK(completion_result.get() == false);
    CHECK(completion_observation->calls() == 1);
    CHECK(provider_calls->load() == 1);
    REQUIRE(connection_closed);
    CHECK(response.find("0\r\n\r\n") == std::string::npos);

    asio_error_code close_error;
    client.socket().close(close_error);
} // async_chunked_response_aborts_an_idle_provider_when_configured


TEST_CASE("async_chunked_response_aborts_oversized_chunks")
{
    SimpleApp app;
    auto completion_observation = std::make_shared<ChunkCompletionObservation>();
    auto completion_result = completion_observation->first_result();

    CROW_ROUTE(app, "/oversized-chunk")
    ([completion_observation](const crow::request&, crow::response& res) {
        res.set_async_chunked_content_provider(
          [](crow::response::async_chunk_completion_t complete) {
              complete(crow::chunk_result::done, std::string(2048, 'x'));
          });
        res.set_chunked_completion_handler([completion_observation](bool clean) {
            completion_observation->record(clean);
        });
        res.end();
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).max_stream_chunk_size(1024).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    app.wait_for_server_start();

    HttpClient client(LOCALHOST_ADDRESS, 45451);
    client.send("GET /oversized-chunk HTTP/1.1\r\nHost: localhost\r\n\r\n");

    std::string response;
    const bool connection_closed = receive_until_closed_with_deadline(client.socket(), response, std::chrono::seconds(5));

    const auto completion_status = completion_result.wait_for(std::chrono::seconds(1));
    REQUIRE(completion_status == std::future_status::ready);
    CHECK(completion_result.get() == false);
    REQUIRE(connection_closed);
    CHECK(response.find("0\r\n\r\n") == std::string::npos);

    asio_error_code close_error;
    client.socket().close(close_error);
} // async_chunked_response_aborts_oversized_chunks


TEST_CASE("async_chunked_response_move_assignment_releases_destination_provider") {
    response destination;
    auto destination_marker                        = std::make_shared<int>(1);
    std::weak_ptr<int> destination_marker_observer = destination_marker;
    destination.set_async_chunked_content_provider([destination_marker](response::async_chunk_completion_t complete) {
        complete(response::chunk_result::done, "old");
    });
    destination_marker.reset();

    response source;
    auto source_marker                        = std::make_shared<int>(1);
    std::weak_ptr<int> source_marker_observer = source_marker;
    source.set_header("X-Source", "moved");
    source.set_async_chunked_content_provider([source_marker](response::async_chunk_completion_t complete) {
        complete(response::chunk_result::done, "new");
    });
    source_marker.reset();

    destination = std::move(source);

    CHECK(destination_marker_observer.expired());
    CHECK(!source_marker_observer.expired());
    CHECK(destination.is_chunked_type());
    CHECK(destination.get_header_value("Transfer-Encoding") == "chunked");
    CHECK(destination.get_header_value("X-Source") == "moved");

    destination.clear();

    CHECK(source_marker_observer.expired());
    CHECK(!destination.is_chunked_type());
} // async_chunked_response_move_assignment_releases_destination_provider


TEST_CASE("async_chunk_transfer_registry_ignores_ordinary_connections_and_unregisters_completed_transfers")
{
    crow::detail::connection_lifecycle_registry registry;
    auto ordinary_connection = std::make_shared<LifecycleRegistryProbe>();
    auto active_transfer = std::make_shared<LifecycleRegistryProbe>();

    CHECK(registry.track(active_transfer));
    registry.untrack(active_transfer.get());
    registry.shutdown_all();

    CHECK(active_transfer->shutdown_calls() == 0);

    CHECK_FALSE(registry.track(active_transfer));

    CHECK(active_transfer->shutdown_calls() == 0);
    CHECK(ordinary_connection->shutdown_calls() == 0);
} // async_chunk_transfer_registry_ignores_ordinary_connections_and_unregisters_completed_transfers


TEST_CASE("async_chunk_transfer_registry_shuts_down_tracked_connections_once")
{
    crow::detail::connection_lifecycle_registry registry;
    auto active_transfer = std::make_shared<LifecycleRegistryProbe>();

    CHECK(registry.track(active_transfer));
    registry.shutdown_all();
    registry.shutdown_all();

    CHECK(active_transfer->shutdown_calls() == 1);
    CHECK_FALSE(registry.track(active_transfer));
} // async_chunk_transfer_registry_shuts_down_tracked_connections_once


TEST_CASE("async_chunk_transfer_registry_retains_connections_until_untracked")
{
    crow::detail::connection_lifecycle_registry registry;
    auto connection = std::make_shared<LifecycleRegistryProbe>();
    auto* connection_key = connection.get();
    std::weak_ptr<LifecycleRegistryProbe> connection_observer = connection;

    REQUIRE(registry.track(connection));
    connection.reset();
    registry.shutdown_all();

    REQUIRE_FALSE(connection_observer.expired());
    CHECK(connection_observer.lock()->shutdown_calls() == 1);

    registry.untrack(connection_key);
    CHECK(connection_observer.expired());
} // async_chunk_transfer_registry_retains_connections_until_untracked


TEST_CASE("async_chunk_transfer_registry_serializes_untrack_with_shutdown")
{
    crow::detail::connection_lifecycle_registry registry;
    auto active_transfer = std::make_shared<LifecycleRegistryProbe>();
    REQUIRE(registry.track(active_transfer));

    auto shutdown = std::async(std::launch::async, [&registry] {
        registry.shutdown_all();
    });
    registry.untrack(active_transfer.get());
    shutdown.get();

    CHECK(active_transfer->shutdown_calls() <= 1);
    CHECK_FALSE(registry.track(active_transfer));
} // async_chunk_transfer_registry_serializes_untrack_with_shutdown


TEST_CASE("empty_async_chunk_provider_closes_without_terminator_and_completes_once_unclean")
{
    SimpleApp app;

    auto completion_observation = std::make_shared<ChunkCompletionObservation>();
    auto completion_result = completion_observation->first_result();

    CROW_ROUTE(app, "/empty-async-chunk-provider")
    ([completion_observation](const crow::request&, crow::response& res) {
        crow::response::async_chunk_provider_t empty_provider;
        res.set_async_chunked_content_provider(std::move(empty_provider), "text/plain");
        res.set_chunked_completion_handler(
          [completion_observation](bool clean) {
              completion_observation->record(clean);
          });
        res.end();
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    REQUIRE(app.wait_for_server_start() == std::cv_status::no_timeout);

    HttpClient client(LOCALHOST_ADDRESS, 45451);
    client.send("GET /empty-async-chunk-provider HTTP/1.1\r\nHost: localhost\r\n\r\n");

    std::string response;
    const bool connection_closed = receive_until_closed_with_deadline(client.socket(), response, std::chrono::seconds(5));
    const auto completion_status = completion_result.wait_for(std::chrono::seconds(1));
    const bool clean = completion_status == std::future_status::ready ? completion_result.get() : true;

    asio_error_code close_error;
    client.socket().close(close_error);
    server_shutdown.shutdown();

    REQUIRE(connection_closed);
    const auto header_end = response.find("\r\n\r\n");
    REQUIRE(header_end != std::string::npos);
    CHECK(response.find("Transfer-Encoding: chunked") != std::string::npos);
    CHECK(response.find("Content-Length") == std::string::npos);
    CHECK(response.substr(header_end + 4).find("0\r\n\r\n") == std::string::npos);
    REQUIRE(completion_status == std::future_status::ready);
    CHECK(clean == false);
    CHECK(completion_observation->calls() == 1);
} // empty_async_chunk_provider_closes_without_terminator_and_completes_once_unclean


TEST_CASE("http_1_0_rejects_synchronous_and_asynchronous_chunk_providers")
{
    SimpleApp app;

    auto synchronous_provider_calls = std::make_shared<std::atomic<std::size_t>>(0);
    auto asynchronous_provider_calls = std::make_shared<std::atomic<std::size_t>>(0);
    auto synchronous_completion = std::make_shared<ChunkCompletionObservation>();
    auto asynchronous_completion = std::make_shared<ChunkCompletionObservation>();
    auto synchronous_result = synchronous_completion->first_result();
    auto asynchronous_result = asynchronous_completion->first_result();

    CROW_ROUTE(app, "/http-1-0-sync-chunks")
    ([synchronous_provider_calls, synchronous_completion](const crow::request&, crow::response& res) {
        res.set_chunked_content_provider(
          [synchronous_provider_calls](std::string& chunk) {
              synchronous_provider_calls->fetch_add(1);
              chunk = "sync";
              return crow::chunk_result::done;
          },
          "text/plain");
        res.set_header("Content-Encoding", "gzip");
        res.set_header("Trailer", "Digest");
        res.set_chunked_completion_handler(
          [synchronous_completion](bool clean) {
              synchronous_completion->record(clean);
          });
        res.end();
    });

    CROW_ROUTE(app, "/http-1-0-async-chunks")
    ([asynchronous_provider_calls, asynchronous_completion](const crow::request&, crow::response& res) {
        res.set_async_chunked_content_provider(
          [asynchronous_provider_calls](crow::response::async_chunk_completion_t complete) {
              asynchronous_provider_calls->fetch_add(1);
              complete(crow::chunk_result::done, "async");
          },
          "text/plain");
        res.set_header("Content-Encoding", "gzip");
        res.set_header("Trailer", "Digest");
        res.set_chunked_completion_handler(
          [asynchronous_completion](bool clean) {
              asynchronous_completion->record(clean);
          });
        res.end();
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    REQUIRE(app.wait_for_server_start() == std::cv_status::no_timeout);

    HttpClient synchronous_client(LOCALHOST_ADDRESS, 45451);
    synchronous_client.send("GET /http-1-0-sync-chunks HTTP/1.0\r\n\r\n");
    std::string synchronous_response;
    const bool synchronous_connection_closed = receive_until_closed_with_deadline(
      synchronous_client.socket(), synchronous_response, std::chrono::seconds(5));

    HttpClient asynchronous_client(LOCALHOST_ADDRESS, 45451);
    asynchronous_client.send("GET /http-1-0-async-chunks HTTP/1.0\r\n\r\n");
    std::string asynchronous_response;
    const bool asynchronous_connection_closed = receive_until_closed_with_deadline(
      asynchronous_client.socket(), asynchronous_response, std::chrono::seconds(5));

    const auto synchronous_completion_status = synchronous_result.wait_for(std::chrono::seconds(1));
    const bool synchronous_clean = synchronous_completion_status == std::future_status::ready ? synchronous_result.get() : true;
    const auto asynchronous_completion_status = asynchronous_result.wait_for(std::chrono::seconds(1));
    const bool asynchronous_clean = asynchronous_completion_status == std::future_status::ready ? asynchronous_result.get() : true;

    asio_error_code close_error;
    synchronous_client.socket().close(close_error);
    asynchronous_client.socket().close(close_error);
    server_shutdown.shutdown();

    REQUIRE(synchronous_connection_closed);
    REQUIRE(asynchronous_connection_closed);
    CHECK(has_complete_http_response(synchronous_response));
    CHECK(has_complete_http_response(asynchronous_response));
    CHECK(synchronous_response.find("HTTP/1.1 505 HTTP Version Not Supported") != std::string::npos);
    CHECK(asynchronous_response.find("HTTP/1.1 505 HTTP Version Not Supported") != std::string::npos);
    CHECK(synchronous_response.find("Transfer-Encoding") == std::string::npos);
    CHECK(asynchronous_response.find("Transfer-Encoding") == std::string::npos);
    CHECK(synchronous_response.find("Content-Encoding") == std::string::npos);
    CHECK(asynchronous_response.find("Content-Encoding") == std::string::npos);
    CHECK(synchronous_response.find("Trailer") == std::string::npos);
    CHECK(asynchronous_response.find("Trailer") == std::string::npos);
    CHECK(synchronous_response.find("Content-Length") != std::string::npos);
    CHECK(asynchronous_response.find("Content-Length") != std::string::npos);
    CHECK(synchronous_provider_calls->load() == 0);
    CHECK(asynchronous_provider_calls->load() == 0);
    REQUIRE(synchronous_completion_status == std::future_status::ready);
    REQUIRE(asynchronous_completion_status == std::future_status::ready);
    CHECK(synchronous_clean == false);
    CHECK(asynchronous_clean == false);
    CHECK(synchronous_completion->calls() == 1);
    CHECK(asynchronous_completion->calls() == 1);
} // http_1_0_rejects_synchronous_and_asynchronous_chunk_providers


TEST_CASE("http_1_0_rejects_head_chunk_providers_without_sending_the_error_body")
{
    SimpleApp app;

    auto synchronous_provider_calls = std::make_shared<std::atomic<std::size_t>>(0);
    auto asynchronous_provider_calls = std::make_shared<std::atomic<std::size_t>>(0);
    auto synchronous_completion = std::make_shared<ChunkCompletionObservation>();
    auto asynchronous_completion = std::make_shared<ChunkCompletionObservation>();
    auto synchronous_result = synchronous_completion->first_result();
    auto asynchronous_result = asynchronous_completion->first_result();

    CROW_ROUTE(app, "/http-1-0-head-sync-chunks")
      .methods("GET"_method,
               "HEAD"_method)([synchronous_provider_calls, synchronous_completion](const crow::request&,
                                                                                   crow::response& res) {
          res.set_chunked_content_provider([synchronous_provider_calls](std::string& chunk) {
              synchronous_provider_calls->fetch_add(1);
              chunk = "sync";
              return crow::chunk_result::done;
          });
          res.set_header("Content-Encoding", "gzip");
          res.set_header("Trailer", "Digest");
          res.set_chunked_completion_handler(
            [synchronous_completion](bool clean) {
                synchronous_completion->record(clean);
            });
          res.end();
      });

    CROW_ROUTE(app, "/http-1-0-head-async-chunks")
      .methods("GET"_method,
               "HEAD"_method)([asynchronous_provider_calls, asynchronous_completion](const crow::request&,
                                                                                     crow::response& res) {
          res.set_async_chunked_content_provider(
            [asynchronous_provider_calls](crow::response::async_chunk_completion_t complete) {
                asynchronous_provider_calls->fetch_add(1);
                complete(crow::chunk_result::done, "async");
            });
          res.set_header("Content-Encoding", "gzip");
          res.set_header("Trailer", "Digest");
          res.set_chunked_completion_handler(
            [asynchronous_completion](bool clean) {
                asynchronous_completion->record(clean);
            });
          res.end();
      });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    REQUIRE(app.wait_for_server_start() == std::cv_status::no_timeout);

    HttpClient synchronous_client(LOCALHOST_ADDRESS, 45451);
    synchronous_client.send("HEAD /http-1-0-head-sync-chunks HTTP/1.0\r\n\r\n");
    std::string synchronous_response;
    const bool synchronous_connection_closed = receive_until_closed_with_deadline(
      synchronous_client.socket(), synchronous_response, std::chrono::seconds(5));

    HttpClient asynchronous_client(LOCALHOST_ADDRESS, 45451);
    asynchronous_client.send("HEAD /http-1-0-head-async-chunks HTTP/1.0\r\n\r\n");
    std::string asynchronous_response;
    const bool asynchronous_connection_closed = receive_until_closed_with_deadline(
      asynchronous_client.socket(), asynchronous_response, std::chrono::seconds(5));

    const auto synchronous_completion_status = synchronous_result.wait_for(std::chrono::seconds(1));
    const bool synchronous_clean = synchronous_completion_status == std::future_status::ready ? synchronous_result.get() : true;
    const auto asynchronous_completion_status = asynchronous_result.wait_for(std::chrono::seconds(1));
    const bool asynchronous_clean = asynchronous_completion_status == std::future_status::ready ? asynchronous_result.get() : true;

    asio_error_code close_error;
    synchronous_client.socket().close(close_error);
    asynchronous_client.socket().close(close_error);
    server_shutdown.shutdown();

    REQUIRE(synchronous_connection_closed);
    REQUIRE(asynchronous_connection_closed);
    const auto synchronous_header_end = synchronous_response.find("\r\n\r\n");
    const auto asynchronous_header_end = asynchronous_response.find("\r\n\r\n");
    REQUIRE(synchronous_header_end != std::string::npos);
    REQUIRE(asynchronous_header_end != std::string::npos);
    CHECK(synchronous_response.find("HTTP/1.1 505 HTTP Version Not Supported") != std::string::npos);
    CHECK(asynchronous_response.find("HTTP/1.1 505 HTTP Version Not Supported") != std::string::npos);
    CHECK(synchronous_response.find("Content-Length: 32") != std::string::npos);
    CHECK(asynchronous_response.find("Content-Length: 32") != std::string::npos);
    CHECK(synchronous_response.find("Connection: close") != std::string::npos);
    CHECK(asynchronous_response.find("Connection: close") != std::string::npos);
    CHECK(synchronous_response.find("Transfer-Encoding") == std::string::npos);
    CHECK(asynchronous_response.find("Transfer-Encoding") == std::string::npos);
    CHECK(synchronous_response.find("Content-Encoding") == std::string::npos);
    CHECK(asynchronous_response.find("Content-Encoding") == std::string::npos);
    CHECK(synchronous_response.find("Trailer") == std::string::npos);
    CHECK(asynchronous_response.find("Trailer") == std::string::npos);
    CHECK(synchronous_response.substr(synchronous_header_end + 4).empty());
    CHECK(asynchronous_response.substr(asynchronous_header_end + 4).empty());
    CHECK(synchronous_provider_calls->load() == 0);
    CHECK(asynchronous_provider_calls->load() == 0);
    REQUIRE(synchronous_completion_status == std::future_status::ready);
    REQUIRE(asynchronous_completion_status == std::future_status::ready);
    CHECK(synchronous_clean == false);
    CHECK(asynchronous_clean == false);
    CHECK(synchronous_completion->calls() == 1);
    CHECK(asynchronous_completion->calls() == 1);
} // http_1_0_rejects_head_chunk_providers_without_sending_the_error_body


TEST_CASE("clear_after_chunk_provider_restores_content_length_and_keep_alive_boundaries")
{
    SimpleApp app;

    auto synchronous_provider_calls = std::make_shared<std::atomic<std::size_t>>(0);
    auto asynchronous_provider_calls = std::make_shared<std::atomic<std::size_t>>(0);

    CROW_ROUTE(app, "/clear-sync-chunks")
    ([synchronous_provider_calls](const crow::request&, crow::response& res) {
        res.set_chunked_content_provider([synchronous_provider_calls](std::string& chunk) {
            synchronous_provider_calls->fetch_add(1);
            chunk = "unused";
            return crow::chunk_result::done;
        });
        res.clear();
        res.end("sync-body");
    });

    CROW_ROUTE(app, "/clear-async-chunks")
    ([asynchronous_provider_calls](const crow::request&, crow::response& res) {
        res.set_async_chunked_content_provider(
          [asynchronous_provider_calls](crow::response::async_chunk_completion_t complete) {
              asynchronous_provider_calls->fetch_add(1);
              complete(crow::chunk_result::done, "unused");
          });
        res.clear();
        res.end("async-body");
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    REQUIRE(app.wait_for_server_start() == std::cv_status::no_timeout);

    HttpClient client(LOCALHOST_ADDRESS, 45451);
    client.send("GET /clear-sync-chunks HTTP/1.1\r\nHost: localhost\r\n\r\n");
    std::string synchronous_response;
    const bool synchronous_complete = receive_with_deadline(client.socket(),
                                                            synchronous_response,
                                                            std::chrono::seconds(5),
                                                            has_complete_http_response);

    std::string asynchronous_response;
    bool asynchronous_connection_closed = false;
    if (synchronous_complete)
    {
        client.send(
          "GET /clear-async-chunks HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
        asynchronous_connection_closed = receive_until_closed_with_deadline(
          client.socket(), asynchronous_response, std::chrono::seconds(5));
    }

    asio_error_code close_error;
    client.socket().close(close_error);
    server_shutdown.shutdown();

    REQUIRE(synchronous_complete);
    REQUIRE(asynchronous_connection_closed);
    const auto synchronous_header_end = synchronous_response.find("\r\n\r\n");
    const auto asynchronous_header_end = asynchronous_response.find("\r\n\r\n");
    REQUIRE(synchronous_header_end != std::string::npos);
    REQUIRE(asynchronous_header_end != std::string::npos);
    CHECK(synchronous_response.find("Content-Length: 9") != std::string::npos);
    CHECK(asynchronous_response.find("Content-Length: 10") != std::string::npos);
    CHECK(synchronous_response.find("Transfer-Encoding") == std::string::npos);
    CHECK(asynchronous_response.find("Transfer-Encoding") == std::string::npos);
    CHECK(synchronous_response.substr(synchronous_header_end + 4) == "sync-body");
    CHECK(asynchronous_response.substr(asynchronous_header_end + 4) == "async-body");
    CHECK(synchronous_provider_calls->load() == 0);
    CHECK(asynchronous_provider_calls->load() == 0);
} // clear_after_chunk_provider_restores_content_length_and_keep_alive_boundaries


TEST_CASE("throwing_async_chunk_provider_closes_without_terminator_and_completes_once_unclean")
{
    SimpleApp app;

    auto provider_calls = std::make_shared<std::atomic<std::size_t>>(0);
    auto completion_observation = std::make_shared<ChunkCompletionObservation>();
    auto completion_result = completion_observation->first_result();

    CROW_ROUTE(app, "/throwing-async-chunk-provider")
    ([provider_calls, completion_observation](const crow::request&, crow::response& res) {
        res.set_async_chunked_content_provider(
          [provider_calls](crow::response::async_chunk_completion_t) {
              provider_calls->fetch_add(1);
              throw std::runtime_error("asynchronous provider failure");
          });
        res.set_chunked_completion_handler(
          [completion_observation](bool clean) {
              completion_observation->record(clean);
          });
        res.end();
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    REQUIRE(app.wait_for_server_start() == std::cv_status::no_timeout);

    HttpClient client(LOCALHOST_ADDRESS, 45451);
    client.send("GET /throwing-async-chunk-provider HTTP/1.1\r\nHost: localhost\r\n\r\n");

    std::string response;
    const bool connection_closed = receive_until_closed_with_deadline(client.socket(), response, std::chrono::seconds(5));
    const auto completion_status = completion_result.wait_for(std::chrono::seconds(1));
    const bool clean = completion_status == std::future_status::ready ? completion_result.get() : true;

    asio_error_code close_error;
    client.socket().close(close_error);
    server_shutdown.shutdown();

    REQUIRE(connection_closed);
    const auto header_end = response.find("\r\n\r\n");
    REQUIRE(header_end != std::string::npos);
    CHECK(response.substr(header_end + 4).find("0\r\n\r\n") == std::string::npos);
    CHECK(provider_calls->load() == 1);
    REQUIRE(completion_status == std::future_status::ready);
    CHECK(clean == false);
    CHECK(completion_observation->calls() == 1);
} // throwing_async_chunk_provider_closes_without_terminator_and_completes_once_unclean


TEST_CASE("async_chunked_response_head_suppresses_provider_and_completes_once_clean") {
    SimpleApp app;

    auto provider_calls         = std::make_shared<std::atomic<std::size_t>>(0);
    auto completion_observation = std::make_shared<ChunkCompletionObservation>();
    auto completion_result      = completion_observation->first_result();

    CROW_ROUTE(app, "/head-async-chunks")
        .methods("GET"_method,
                 "HEAD"_method)([provider_calls, completion_observation](const crow::request&, crow::response& res) {
            res.set_async_chunked_content_provider(
                [provider_calls](crow::response::async_chunk_completion_t complete) {
                    provider_calls->fetch_add(1);
                    complete(crow::chunk_result::done, "body");
                },
                "text/plain");
            res.set_header("X-Streaming-Mode", "async");
            res.set_chunked_completion_handler(
                [completion_observation](bool clean) { completion_observation->record(clean); });
            res.end();
        });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] { app.stop(); });
    REQUIRE(app.wait_for_server_start() == std::cv_status::no_timeout);

    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string request = "HEAD /head-async-chunks HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    asio::write(client, asio::buffer(request));

    std::string received;
    const bool peer_closed       = receive_until_closed_with_deadline(client, received, std::chrono::seconds(5));
    const auto completion_status = completion_result.wait_for(std::chrono::seconds(5));
    const bool clean             = completion_status == std::future_status::ready ? completion_result.get() : false;

    asio_error_code close_error;
    client.close(close_error);
    server_shutdown.shutdown();

    REQUIRE(peer_closed);
    const auto header_end = received.find("\r\n\r\n");
    REQUIRE(header_end != std::string::npos);
    CHECK(received.find("Transfer-Encoding: chunked") != std::string::npos);
    CHECK(received.find("Content-Length") == std::string::npos);
    CHECK(received.find("Content-Type: text/plain") != std::string::npos);
    CHECK(received.find("X-Streaming-Mode: async") != std::string::npos);
    CHECK(received.substr(header_end + 4).empty());
    CHECK(provider_calls->load() == 0);
    REQUIRE(completion_status == std::future_status::ready);
    CHECK(clean == true);
    CHECK(completion_observation->calls() == 1);
} // async_chunked_response_head_suppresses_provider_and_completes_once_clean


TEST_CASE("async_chunked_response_clean_completion_restores_keep_alive_reading") {
    SimpleApp app;

    auto provider_calls         = std::make_shared<std::atomic<std::size_t>>(0);
    auto first_route_calls      = std::make_shared<std::atomic<std::size_t>>(0);
    auto second_route_calls     = std::make_shared<std::atomic<std::size_t>>(0);
    auto completion_observation = std::make_shared<ChunkCompletionObservation>();
    auto completion_result      = completion_observation->first_result();
    auto worker_tasks           = std::make_shared<std::vector<std::future<void>>>();
    auto worker_tasks_mutex     = std::make_shared<std::mutex>();

    CROW_ROUTE(app, "/first-async-response")
    ([provider_calls, first_route_calls, completion_observation, worker_tasks, worker_tasks_mutex](
         const crow::request&, crow::response& res) {
        first_route_calls->fetch_add(1);
        res.set_async_chunked_content_provider(
            [provider_calls, worker_tasks, worker_tasks_mutex](crow::response::async_chunk_completion_t complete) {
                provider_calls->fetch_add(1);
                std::lock_guard<std::mutex> lock(*worker_tasks_mutex);
                worker_tasks->emplace_back(std::async(std::launch::async, [complete = std::move(complete)]() mutable {
                    complete(crow::chunk_result::done, "first");
                }));
            },
            "text/plain");
        res.set_chunked_completion_handler(
            [completion_observation](bool clean) { completion_observation->record(clean); });
        res.end();
    });

    CROW_ROUTE(app, "/second-regular-response")
    ([second_route_calls] {
        second_route_calls->fetch_add(1);
        return "second";
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] { app.stop(); });
    REQUIRE(app.wait_for_server_start() == std::cv_status::no_timeout);

    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string first_request = "GET /first-async-response HTTP/1.1\r\nHost: localhost\r\n\r\n";
    asio::write(client, asio::buffer(first_request));

    const auto has_chunk_terminator = [](const std::string& response) {
        return response.size() >= 5 && response.compare(response.size() - 5, 5, "0\r\n\r\n") == 0;
    };
    std::string first_response;
    const bool first_response_complete
        = receive_with_deadline(client, first_response, std::chrono::seconds(5), has_chunk_terminator);
    const auto completion_status = completion_result.wait_for(std::chrono::seconds(5));
    const bool clean             = completion_status == std::future_status::ready ? completion_result.get() : false;

    const std::string second_request
        = "GET /second-regular-response HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    asio::write(client, asio::buffer(second_request));
    std::string second_response;
    const bool second_response_complete
        = receive_with_deadline(client, second_response, std::chrono::seconds(5), has_complete_http_response);

    std::vector<std::future<void>> tasks;
    {
        std::lock_guard<std::mutex> lock(*worker_tasks_mutex);
        tasks.swap(*worker_tasks);
    }
    for (auto& task : tasks)
        task.get();

    asio_error_code close_error;
    client.close(close_error);
    server_shutdown.shutdown();

    REQUIRE(first_response_complete);
    const auto first_header_end = first_response.find("\r\n\r\n");
    REQUIRE(first_header_end != std::string::npos);
    CHECK(first_response.find("200 OK") != std::string::npos);
    CHECK(first_response.find("Transfer-Encoding: chunked") != std::string::npos);
    CHECK(first_response.find("Content-Length") == std::string::npos);
    CHECK(first_response.substr(first_header_end + 4) == "5\r\nfirst\r\n0\r\n\r\n");

    REQUIRE(completion_status == std::future_status::ready);
    CHECK(clean == true);
    CHECK(completion_observation->calls() == 1);
    CHECK(provider_calls->load() == 1);
    CHECK(first_route_calls->load() == 1);

    REQUIRE(second_response_complete);
    const auto second_header_end = second_response.find("\r\n\r\n");
    REQUIRE(second_header_end != std::string::npos);
    CHECK(second_response.find("200 OK") != std::string::npos);
    CHECK(second_response.find("Transfer-Encoding: chunked") == std::string::npos);
    CHECK(second_response.substr(second_header_end + 4) == "second");
    CHECK(second_route_calls->load() == 1);
} // async_chunked_response_clean_completion_restores_keep_alive_reading


TEST_CASE("deferred_chunked_response_stops_parsing_at_its_request_boundary")
{
    SimpleApp app;

    auto deferred_end_promise = std::make_shared<std::promise<std::function<void()>>>();
    auto deferred_end = deferred_end_promise->get_future();
    auto second_route_calls = std::make_shared<std::atomic<std::size_t>>(0);

    CROW_ROUTE(app, "/deferred-chunked-boundary")
    ([deferred_end_promise](const crow::request&, crow::response& res) {
        res.set_async_chunked_content_provider(
          [](crow::response::async_chunk_completion_t complete) {
              complete(crow::chunk_result::done, "first");
          });
        deferred_end_promise->set_value([&res] {
            res.end();
        });
    });

    CROW_ROUTE(app, "/after-deferred-chunked-boundary")
    ([second_route_calls](const crow::request&, crow::response& res) {
        second_route_calls->fetch_add(1);
        res.end("second");
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    app.wait_for_server_start();

    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string requests = "GET /deferred-chunked-boundary HTTP/1.1\r\nHost: localhost\r\n\r\n"
                                 "GET /after-deferred-chunked-boundary HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    asio::write(client, asio::buffer(requests));

    REQUIRE(deferred_end.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    CHECK(second_route_calls->load() == 0);
    deferred_end.get()();

    std::string response;
    const bool connection_closed = receive_until_closed_with_deadline(client, response, std::chrono::seconds(5));

    asio_error_code close_error;
    client.close(close_error);
    server_shutdown.shutdown();

    REQUIRE(connection_closed);
    CHECK(second_route_calls->load() == 0);
    CHECK(response.find("5\r\nfirst\r\n0\r\n\r\n") != std::string::npos);
    CHECK(response.find("second") == std::string::npos);
} // deferred_chunked_response_stops_parsing_at_its_request_boundary


TEST_CASE("deferred_head_chunked_response_closes_after_pipelined_input")
{
    SimpleApp app;
    auto deferred_end_promise = std::make_shared<std::promise<std::function<void()>>>();
    auto deferred_end = deferred_end_promise->get_future();
    auto provider_calls = std::make_shared<std::atomic<std::size_t>>(0);
    auto second_route_calls = std::make_shared<std::atomic<std::size_t>>(0);

    CROW_ROUTE(app, "/deferred-head-boundary")
    ([deferred_end_promise, provider_calls](const crow::request&, crow::response& res) {
        res.set_async_chunked_content_provider(
          [provider_calls](crow::response::async_chunk_completion_t) {
              provider_calls->fetch_add(1);
          });
        deferred_end_promise->set_value([&res] {
            res.end();
        });
    });
    CROW_ROUTE(app, "/after-deferred-head-boundary")
    ([second_route_calls](const crow::request&, crow::response& res) {
        second_route_calls->fetch_add(1);
        res.end("second");
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    app.wait_for_server_start();
    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string requests = "HEAD /deferred-head-boundary HTTP/1.1\r\nHost: localhost\r\n\r\n"
                                 "GET /after-deferred-head-boundary HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    asio::write(client, asio::buffer(requests));

    REQUIRE(deferred_end.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    CHECK(second_route_calls->load() == 0);
    deferred_end.get()();
    std::string response;
    const bool connection_closed = receive_until_closed_with_deadline(client, response, std::chrono::seconds(5));
    asio_error_code close_error;
    client.close(close_error);
    server_shutdown.shutdown();

    REQUIRE(connection_closed);
    CHECK(provider_calls->load() == 0);
    CHECK(second_route_calls->load() == 0);
    const auto first_header_end = response.find("\r\n\r\n");
    REQUIRE(first_header_end != std::string::npos);
    const auto first_headers = response.substr(0, first_header_end + 4);
    CHECK(first_headers.find("Transfer-Encoding: chunked") != std::string::npos);
    CHECK(first_headers.find("Content-Length:") == std::string::npos);
    CHECK(response.size() == first_header_end + 4);
} // deferred_head_chunked_response_closes_after_pipelined_input


TEST_CASE("unmatched_head_does_not_emit_a_body")
{
    SimpleApp app;

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    app.wait_for_server_start();
    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string request = "HEAD /missing HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    asio::write(client, asio::buffer(request));

    std::string response;
    const bool connection_closed = receive_until_closed_with_deadline(client, response, std::chrono::seconds(5));
    asio_error_code close_error;
    client.close(close_error);
    server_shutdown.shutdown();

    REQUIRE(connection_closed);
    const auto first_header_end = response.find("\r\n\r\n");
    REQUIRE(first_header_end != std::string::npos);
    CHECK(response.find("Content-Length: 15\r\n") != std::string::npos);
    CHECK(response.size() == first_header_end + 4);
} // unmatched_head_does_not_emit_a_body


TEST_CASE("static_head_preserves_representation_length")
{
    SimpleApp app;
    struct stat file_status
    {};
    REQUIRE(stat("tests/img/cat.jpg", &file_status) == 0);

    CROW_ROUTE(app, "/static-head")
    ([](const crow::request&, crow::response& res) {
        res.set_static_file_info("tests/img/cat.jpg");
        res.end();
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    app.wait_for_server_start();
    HttpClient client(LOCALHOST_ADDRESS, 45451);
    client.send("HEAD /static-head HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    std::string response;
    const bool closed = receive_until_closed_with_deadline(client.socket(), response, std::chrono::seconds(5));
    asio_error_code close_error;
    client.socket().close(close_error);
    server_shutdown.shutdown();

    REQUIRE(closed);
    const auto header_end = response.find("\r\n\r\n");
    REQUIRE(header_end != std::string::npos);
    CHECK(response.find("Content-Length: " + std::to_string(file_status.st_size)) != std::string::npos);
    CHECK(response.size() == header_end + 4);
} // static_head_preserves_representation_length


TEST_CASE("unsupported_informational_status_uses_normalized_framing")
{
    SimpleApp app;
    auto interim_provider_calls = std::make_shared<std::atomic<std::size_t>>(0);
    auto interim_completion = std::make_shared<ChunkCompletionObservation>();
    auto interim_result = interim_completion->first_result();
    CROW_ROUTE(app, "/unsupported-status")
    ([](const crow::request&, crow::response& res) {
        res.code = 199;
        // Discarded together with the body: the synthesized 500 carries no
        // coding and Crow sends no trailer section.
        res.body = "interim details";
        res.set_header("Content-Encoding", "gzip");
        res.set_header("Trailer", "Digest");
        res.end();
    });
    CROW_ROUTE(app, "/interim-status")
    ([interim_provider_calls, interim_completion](const crow::request&, crow::response& res) {
        res.code = 101;
        res.set_chunked_content_provider([interim_provider_calls](std::string& chunk) {
            interim_provider_calls->fetch_add(1);
            chunk = "forbidden";
            return false;
        });
        res.set_chunked_completion_handler(
          [interim_completion](bool clean) {
              interim_completion->record(clean);
          });
        res.end();
    });
    CROW_ROUTE(app, "/after-unsupported-status")
    ([] {
        return "second";
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    app.wait_for_server_start();
    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string requests = "GET /unsupported-status HTTP/1.1\r\nHost: localhost\r\n\r\n"
                                 "GET /interim-status HTTP/1.1\r\nHost: localhost\r\n\r\n"
                                 "GET /after-unsupported-status HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    asio::write(client, asio::buffer(requests));
    std::string response;
    REQUIRE(receive_until_closed_with_deadline(client, response, std::chrono::seconds(5)));
    server_shutdown.shutdown();

    CHECK(response.find("HTTP/1.1 500 Internal Server Error\r\n") == 0);
    CHECK(response.find("Content-Length: 27\r\n") != std::string::npos);
    // A handler-returned interim status is normalized to a final 500 as well,
    // its provider is suppressed, and the completion handler still reports.
    const auto interim_response = response.find("HTTP/1.1 500 Internal Server Error\r\n", 1);
    REQUIRE(interim_response != std::string::npos);
    CHECK(response.find("Transfer-Encoding:") == std::string::npos);
    CHECK(response.find("Content-Encoding:") == std::string::npos);
    CHECK(response.find("Trailer:") == std::string::npos);
    CHECK(interim_provider_calls->load() == 0);
    REQUIRE(interim_result.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    CHECK(interim_result.get() == true);
    CHECK(interim_completion->calls() == 1);
    const auto second_response = response.find("HTTP/1.1 200 OK\r\n");
    REQUIRE(second_response != std::string::npos);
    CHECK(second_response > interim_response);
} // unsupported_informational_status_uses_normalized_framing


TEST_CASE("ordinary_deferred_response_closes_after_pipelined_input")
{
    SimpleApp app;
    auto deferred_end_promise = std::make_shared<std::promise<std::function<void()>>>();
    auto deferred_end = deferred_end_promise->get_future();
    auto second_route_calls = std::make_shared<std::atomic<std::size_t>>(0);

    CROW_ROUTE(app, "/ordinary-deferred-first")
    ([deferred_end_promise](const crow::request&, crow::response& res) {
        deferred_end_promise->set_value([&res] {
            res.end("first");
        });
    });
    CROW_ROUTE(app, "/ordinary-deferred-second")
    ([second_route_calls] {
        second_route_calls->fetch_add(1);
        return "second";
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    app.wait_for_server_start();
    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string requests = "GET /ordinary-deferred-first HTTP/1.1\r\nHost: localhost\r\n\r\n"
                                 "GET /ordinary-deferred-second HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    asio::write(client, asio::buffer(requests));

    REQUIRE(deferred_end.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    CHECK(second_route_calls->load() == 0);
    deferred_end.get()();
    std::string response;
    const bool connection_closed = receive_until_closed_with_deadline(client, response, std::chrono::seconds(5));
    asio_error_code close_error;
    client.close(close_error);
    server_shutdown.shutdown();

    REQUIRE(connection_closed);
    CHECK(second_route_calls->load() == 0);
    CHECK(response.find("first") != std::string::npos);
    CHECK(response.find("second") == std::string::npos);
} // ordinary_deferred_response_closes_after_pipelined_input


TEST_CASE("server_stop_cleans_up_queued_deferred_finalization")
{
    SimpleApp app;
    auto deferred_end_promise = std::make_shared<std::promise<std::function<void()>>>();
    auto deferred_end = deferred_end_promise->get_future();
    auto blocker_started_promise = std::make_shared<std::promise<void>>();
    auto blocker_started = blocker_started_promise->get_future();
    auto blocker_release_promise = std::make_shared<std::promise<void>>();
    auto blocker_release = blocker_release_promise->get_future().share();
    auto provider_calls = std::make_shared<std::atomic<std::size_t>>(0);
    auto completion_observation = std::make_shared<ChunkCompletionObservation>();
    auto completion_result = completion_observation->first_result();
    auto provider_marker_holder = std::make_shared<std::shared_ptr<int>>(std::make_shared<int>(1));
    std::weak_ptr<int> provider_marker_observer = *provider_marker_holder;

    CROW_ROUTE(app, "/queued-deferred-finalization")
    ([deferred_end_promise,
      blocker_started_promise,
      blocker_release,
      provider_calls,
      completion_observation,
      provider_marker_holder](const crow::request& req, crow::response& res) {
        auto provider_marker = std::move(*provider_marker_holder);
        res.set_async_chunked_content_provider(
          [provider_calls, provider_marker](crow::response::async_chunk_completion_t) {
              provider_calls->fetch_add(1);
          });
        res.set_chunked_completion_handler(
          [completion_observation](bool clean) {
              completion_observation->record(clean);
          });
        asio::post(*req.io_context, [blocker_started_promise, blocker_release] {
            blocker_started_promise->set_value();
            blocker_release.wait();
        });
        deferred_end_promise->set_value([&res] {
            res.end();
        });
    });
    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    app.wait_for_server_start();
    HttpClient client(LOCALHOST_ADDRESS, 45451);
    client.send("GET /queued-deferred-finalization HTTP/1.1\r\nHost: localhost\r\n\r\n");

    REQUIRE(deferred_end.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    REQUIRE(blocker_started.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    deferred_end.get()();
    app.stop();
    blocker_release_promise->set_value();

    REQUIRE(server_task.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    const auto completion_status = completion_result.wait_for(std::chrono::seconds(1));
    CHECK(completion_status == std::future_status::ready);
    if (completion_status == std::future_status::ready)
    {
        CHECK(completion_result.get() == false);
    }
    CHECK(completion_observation->calls() == 1);
    CHECK(provider_calls->load() == 0);
    CHECK(provider_marker_observer.expired());
    asio_error_code close_error;
    client.socket().close(close_error);
} // server_stop_cleans_up_queued_deferred_finalization


TEST_CASE("deferred_end_after_worker_shutdown_is_safe")
{
    SimpleApp app;
    auto deferred_end_promise = std::make_shared<std::promise<std::function<void()>>>();
    auto deferred_end = deferred_end_promise->get_future();
    auto completion_observation = std::make_shared<ChunkCompletionObservation>();
    auto completion_result = completion_observation->first_result();
    auto provider_marker_holder = std::make_shared<std::shared_ptr<int>>(std::make_shared<int>(1));
    std::weak_ptr<int> provider_marker_observer = *provider_marker_holder;
    PausingSocketContext socket_context;
    auto connection_destroyed = socket_context.started_connection_destroyed_future();

    CROW_ROUTE(app, "/late-deferred-end")
    ([deferred_end_promise, completion_observation, provider_marker_holder](const crow::request&, crow::response& res) {
        auto provider_marker = std::move(*provider_marker_holder);
        res.set_async_chunked_content_provider(
          [provider_marker](crow::response::async_chunk_completion_t) {
              static_cast<void>(provider_marker);
          });
        res.set_chunked_completion_handler(
          [completion_observation](bool clean) {
              completion_observation->record(clean);
          });
        deferred_end_promise->set_value([&res] {
            res.end();
        });
    });

    app.validate();
    std::tuple<> middlewares;
    using DeferredServer = crow::Server<crow::SimpleApp, crow::TCPAcceptor, PausingSocketAdaptor>;
    DeferredServer server(&app,
                          asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451),
                          "Crow/Test",
                          &middlewares,
                          2,
                          5,
                          &socket_context);
    auto server_task = std::async(std::launch::async, [&server] {
        server.run();
    });
    BoundedServerShutdown server_shutdown(server_task, [&server] {
        server.stop();
    });
    REQUIRE(server.wait_for_start(std::chrono::steady_clock::now() + std::chrono::seconds(3)) == std::cv_status::no_timeout);
    HttpClient client(LOCALHOST_ADDRESS, 45451);
    client.send("GET /late-deferred-end HTTP/1.1\r\nHost: localhost\r\n\r\n");

    REQUIRE(deferred_end.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    server.stop();
    REQUIRE(server_task.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    REQUIRE(completion_result.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    CHECK(completion_result.get() == false);
    CHECK(provider_marker_observer.expired());
    CHECK(connection_destroyed.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout);

    deferred_end.get()();
    CHECK(completion_observation->calls() == 1);
    CHECK(connection_destroyed.wait_for(std::chrono::seconds(1)) == std::future_status::ready);

    asio_error_code close_error;
    client.socket().close(close_error);
} // deferred_end_after_worker_shutdown_is_safe


TEST_CASE("deferred_end_with_body_after_worker_shutdown_is_safe")
{
    SimpleApp app;
    auto deferred_end_promise = std::make_shared<std::promise<std::function<void()>>>();
    auto deferred_end = deferred_end_promise->get_future();
    auto completion_observation = std::make_shared<ChunkCompletionObservation>();
    auto completion_result = completion_observation->first_result();
    auto provider_marker_holder = std::make_shared<std::shared_ptr<int>>(std::make_shared<int>(1));
    std::weak_ptr<int> provider_marker_observer = *provider_marker_holder;
    PausingSocketContext socket_context;
    auto connection_destroyed = socket_context.started_connection_destroyed_future();

    CROW_ROUTE(app, "/late-deferred-end-with-body")
    ([deferred_end_promise, completion_observation, provider_marker_holder](const crow::request&, crow::response& res) {
        auto provider_marker = std::move(*provider_marker_holder);
        res.set_async_chunked_content_provider(
          [provider_marker](crow::response::async_chunk_completion_t) {
              static_cast<void>(provider_marker);
          });
        res.set_chunked_completion_handler(
          [completion_observation](bool clean) {
              completion_observation->record(clean);
          });
        deferred_end_promise->set_value([&res] {
            res.end("late");
        });
    });

    app.validate();
    std::tuple<> middlewares;
    using DeferredServer = crow::Server<crow::SimpleApp, crow::TCPAcceptor, PausingSocketAdaptor>;
    DeferredServer server(&app,
                          asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451),
                          "Crow/Test",
                          &middlewares,
                          2,
                          5,
                          &socket_context);
    auto server_task = std::async(std::launch::async, [&server] {
        server.run();
    });
    BoundedServerShutdown server_shutdown(server_task, [&server] {
        server.stop();
    });
    REQUIRE(server.wait_for_start(std::chrono::steady_clock::now() + std::chrono::seconds(3)) == std::cv_status::no_timeout);
    HttpClient client(LOCALHOST_ADDRESS, 45451);
    client.send("GET /late-deferred-end-with-body HTTP/1.1\r\nHost: localhost\r\n\r\n");

    REQUIRE(deferred_end.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    server.stop();
    REQUIRE(server_task.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    REQUIRE(completion_result.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    CHECK(completion_result.get() == false);
    CHECK(provider_marker_observer.expired());
    CHECK(connection_destroyed.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout);

    deferred_end.get()();
    CHECK(completion_observation->calls() == 1);
    CHECK(connection_destroyed.wait_for(std::chrono::seconds(1)) == std::future_status::ready);

    asio_error_code close_error;
    client.socket().close(close_error);
} // deferred_end_with_body_after_worker_shutdown_is_safe


TEST_CASE("connection_destruction_reports_unstarted_deferred_completion")
{
    SimpleApp app;
    asio::io_context io_context;
    crow::detail::task_timer task_timer(io_context);
    std::function<std::string()> date_str_getter = [] {
        return std::string("Tue, 01 Jan 2030 00:00:00 GMT");
    };
    std::tuple<> middlewares;
    std::atomic<unsigned int> queue_length{0};
    auto completion_observation = std::make_shared<ChunkCompletionObservation>();
    auto completion_result = completion_observation->first_result();

    using DirectConnection = crow::Connection<crow::SocketAdaptor, crow::SimpleApp>;
    {
        auto connection = std::make_shared<DirectConnection>(io_context,
                                                             &app,
                                                             "Crow/Test",
                                                             &middlewares,
                                                             date_str_getter,
                                                             task_timer,
                                                             nullptr,
                                                             queue_length);
        auto& res = crow::connection_test_access::res(*connection);
        res.set_async_chunked_content_provider(
          [](crow::response::async_chunk_completion_t) {});
        res.set_chunked_completion_handler(
          [completion_observation](bool clean) {
              completion_observation->record(clean);
          });
    }

    const auto completion_status = completion_result.wait_for(std::chrono::milliseconds(100));
    CHECK(completion_status == std::future_status::ready);
    if (completion_status == std::future_status::ready)
    {
        CHECK(completion_result.get() == false);
    }
    CHECK(completion_observation->calls() == 1);
    CHECK(queue_length.load() == 0);
} // connection_destruction_reports_unstarted_deferred_completion


TEST_CASE("bodyless_statuses_do_not_invoke_chunk_providers")
{
    SimpleApp app;
    auto sync_204_calls = std::make_shared<std::atomic<std::size_t>>(0);
    auto async_204_calls = std::make_shared<std::atomic<std::size_t>>(0);
    auto sync_304_calls = std::make_shared<std::atomic<std::size_t>>(0);
    auto async_304_calls = std::make_shared<std::atomic<std::size_t>>(0);
    auto sync_205_calls = std::make_shared<std::atomic<std::size_t>>(0);
    auto async_205_calls = std::make_shared<std::atomic<std::size_t>>(0);
    auto sync_204_completion = std::make_shared<ChunkCompletionObservation>();
    auto async_204_completion = std::make_shared<ChunkCompletionObservation>();
    auto sync_304_completion = std::make_shared<ChunkCompletionObservation>();
    auto async_304_completion = std::make_shared<ChunkCompletionObservation>();
    auto sync_205_completion = std::make_shared<ChunkCompletionObservation>();
    auto async_205_completion = std::make_shared<ChunkCompletionObservation>();
    auto sync_204_result = sync_204_completion->first_result();
    auto async_204_result = async_204_completion->first_result();
    auto sync_304_result = sync_304_completion->first_result();
    auto async_304_result = async_304_completion->first_result();
    auto sync_205_result = sync_205_completion->first_result();
    auto async_205_result = async_205_completion->first_result();

    CROW_ROUTE(app, "/sync-204")
    ([sync_204_calls, sync_204_completion](const crow::request&, crow::response& res) {
        res.code = 204;
        res.set_header("Content-Encoding", "gzip");
        res.set_header("Trailer", "Digest");
        res.set_chunked_content_provider([sync_204_calls](std::string& chunk) {
            sync_204_calls->fetch_add(1);
            chunk = "forbidden";
            return false;
        });
        res.set_chunked_completion_handler(
          [sync_204_completion](bool clean) {
              sync_204_completion->record(clean);
          });
        res.end();
    });
    CROW_ROUTE(app, "/async-204")
    ([async_204_calls, async_204_completion](const crow::request&, crow::response& res) {
        res.code = 204;
        res.set_async_chunked_content_provider(
          [async_204_calls](crow::response::async_chunk_completion_t complete) {
              async_204_calls->fetch_add(1);
              complete(crow::chunk_result::done, "forbidden");
          });
        res.set_chunked_completion_handler(
          [async_204_completion](bool clean) {
              async_204_completion->record(clean);
          });
        res.end();
    });
    CROW_ROUTE(app, "/sync-304")
    ([sync_304_calls, sync_304_completion](const crow::request&, crow::response& res) {
        res.code = 304;
        res.set_header("Content-Encoding", "gzip");
        res.set_header("Trailer", "Digest");
        res.set_chunked_content_provider([sync_304_calls](std::string& chunk) {
            sync_304_calls->fetch_add(1);
            chunk = "forbidden";
            return false;
        });
        res.set_chunked_completion_handler(
          [sync_304_completion](bool clean) {
              sync_304_completion->record(clean);
          });
        res.end();
    });
    CROW_ROUTE(app, "/async-304")
    ([async_304_calls, async_304_completion](const crow::request&, crow::response& res) {
        res.code = 304;
        res.set_async_chunked_content_provider(
          [async_304_calls](crow::response::async_chunk_completion_t complete) {
              async_304_calls->fetch_add(1);
              complete(crow::chunk_result::done, "forbidden");
          });
        res.set_chunked_completion_handler(
          [async_304_completion](bool clean) {
              async_304_completion->record(clean);
          });
        res.end();
    });
    CROW_ROUTE(app, "/sync-205")
    ([sync_205_calls, sync_205_completion](const crow::request&, crow::response& res) {
        res.code = 205;
        res.set_header("Content-Encoding", "gzip");
        res.set_header("Trailer", "Digest");
        res.set_chunked_content_provider([sync_205_calls](std::string& chunk) {
            sync_205_calls->fetch_add(1);
            chunk = "forbidden";
            return false;
        });
        res.set_chunked_completion_handler(
          [sync_205_completion](bool clean) {
              sync_205_completion->record(clean);
          });
        res.end();
    });
    CROW_ROUTE(app, "/async-205")
    ([async_205_calls, async_205_completion](const crow::request&, crow::response& res) {
        res.code = 205;
        res.set_async_chunked_content_provider(
          [async_205_calls](crow::response::async_chunk_completion_t complete) {
              async_205_calls->fetch_add(1);
              complete(crow::chunk_result::done, "forbidden");
          });
        res.set_chunked_completion_handler(
          [async_205_completion](bool clean) {
              async_205_completion->record(clean);
          });
        res.end();
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    app.wait_for_server_start();

    const auto request = [](const std::string& path) {
        HttpClient client(LOCALHOST_ADDRESS, 45451);
        client.send("GET " + path + " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
        std::string response;
        const bool closed = receive_until_closed_with_deadline(client.socket(), response, std::chrono::seconds(5));
        asio_error_code close_error;
        client.socket().close(close_error);
        REQUIRE(closed);
        return response;
    };

    const auto sync_204_response = request("/sync-204");
    const auto async_204_response = request("/async-204");
    const auto sync_304_response = request("/sync-304");
    const auto async_304_response = request("/async-304");
    const auto sync_205_response = request("/sync-205");
    const auto async_205_response = request("/async-205");

    // A 205 with a provider on HTTP/1.0 is normalized to a bodyless 205, so the
    // chunked-coding rejection must not fire.
    HttpClient http_1_0_client(LOCALHOST_ADDRESS, 45451);
    http_1_0_client.send("GET /sync-205 HTTP/1.0\r\nHost: localhost\r\n\r\n");
    std::string http_1_0_response;
    const bool http_1_0_closed = receive_until_closed_with_deadline(http_1_0_client.socket(), http_1_0_response, std::chrono::seconds(5));
    asio_error_code http_1_0_close_error;
    http_1_0_client.socket().close(http_1_0_close_error);
    REQUIRE(http_1_0_closed);
    server_shutdown.shutdown();

    const auto check_bodyless = [](const std::string& response) {
        const auto header_end = response.find("\r\n\r\n");
        REQUIRE(header_end != std::string::npos);
        CHECK(response.size() == header_end + 4);
    };
    check_bodyless(sync_204_response);
    check_bodyless(async_204_response);
    check_bodyless(sync_304_response);
    check_bodyless(async_304_response);
    check_bodyless(sync_205_response);
    check_bodyless(async_205_response);
    check_bodyless(http_1_0_response);
    CHECK(sync_204_response.find("Transfer-Encoding:") == std::string::npos);
    CHECK(async_204_response.find("Transfer-Encoding:") == std::string::npos);
    CHECK(sync_304_response.find("Transfer-Encoding:") == std::string::npos);
    CHECK(async_304_response.find("Transfer-Encoding:") == std::string::npos);
    CHECK(sync_205_response.find("Transfer-Encoding:") == std::string::npos);
    CHECK(async_205_response.find("Transfer-Encoding:") == std::string::npos);
    CHECK(http_1_0_response.find("Transfer-Encoding:") == std::string::npos);
    // 205 is empty rather than self-delimiting: without an explicit zero
    // length a keep-alive client would wait for close-delimited content.
    CHECK(sync_205_response.find("Content-Length: 0") != std::string::npos);
    CHECK(async_205_response.find("Content-Length: 0") != std::string::npos);
    CHECK(http_1_0_response.find(" 205 ") != std::string::npos);
    // Crow sends no trailer section on any of these. 204 and 304 keep
    // handler-set representation metadata; the forced-empty 205 drops a
    // coding header that would misdescribe it.
    CHECK(sync_204_response.find("Trailer:") == std::string::npos);
    CHECK(sync_304_response.find("Trailer:") == std::string::npos);
    CHECK(sync_205_response.find("Trailer:") == std::string::npos);
    CHECK(sync_204_response.find("Content-Encoding: gzip") != std::string::npos);
    CHECK(sync_304_response.find("Content-Encoding: gzip") != std::string::npos);
    CHECK(sync_205_response.find("Content-Encoding:") == std::string::npos);
    CHECK(sync_204_calls->load() == 0);
    CHECK(async_204_calls->load() == 0);
    CHECK(sync_304_calls->load() == 0);
    CHECK(async_304_calls->load() == 0);
    CHECK(sync_205_calls->load() == 0);
    CHECK(async_205_calls->load() == 0);
    CHECK(sync_204_completion->calls() == 1);
    CHECK(async_204_completion->calls() == 1);
    CHECK(sync_304_completion->calls() == 1);
    CHECK(async_304_completion->calls() == 1);
    // The sync-205 route was requested twice (HTTP/1.1 and HTTP/1.0).
    CHECK(sync_205_completion->calls() == 2);
    CHECK(async_205_completion->calls() == 1);
    REQUIRE(sync_204_result.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    REQUIRE(async_204_result.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    REQUIRE(sync_304_result.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    REQUIRE(async_304_result.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    REQUIRE(sync_205_result.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    REQUIRE(async_205_result.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    CHECK(sync_204_result.get() == true);
    CHECK(async_204_result.get() == true);
    CHECK(sync_304_result.get() == true);
    CHECK(async_304_result.get() == true);
    CHECK(sync_205_result.get() == true);
    CHECK(async_205_result.get() == true);
} // bodyless_statuses_do_not_invoke_chunk_providers


#ifdef CROW_ENABLE_COMPRESSION
TEST_CASE("discarded_bodies_are_not_compressed")
{
    SimpleApp app;
    CROW_ROUTE(app, "/interim-compressed")
    ([](const crow::request&, crow::response& res) {
        res.code = 199;
        res.body = "interim details";
        res.end();
    });
    CROW_ROUTE(app, "/no-content-compressed")
    ([](const crow::request&, crow::response& res) {
        res.code = 204;
        res.body = "discarded";
        res.end();
    });
    CROW_ROUTE(app, "/normal-compressed")
    ([] {
        return "full-length body that goes out compressed";
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).use_compression(compression::algorithm::GZIP).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    app.wait_for_server_start();

    const auto request = [](const std::string& path) {
        HttpClient client(LOCALHOST_ADDRESS, 45451);
        client.send("GET " + path + " HTTP/1.1\r\nHost: localhost\r\nAccept-Encoding: gzip\r\nConnection: close\r\n\r\n");
        std::string response;
        const bool closed = receive_until_closed_with_deadline(client.socket(), response, std::chrono::seconds(5));
        asio_error_code close_error;
        client.socket().close(close_error);
        REQUIRE(closed);
        return response;
    };

    const auto interim_response = request("/interim-compressed");
    const auto no_content_response = request("/no-content-compressed");
    const auto normal_response = request("/normal-compressed");
    server_shutdown.shutdown();

    // A body that is about to be discarded is never compressed: the
    // synthesized 500 and the bodyless 204 carry no Content-Encoding.
    CHECK(interim_response.find(" 500 ") != std::string::npos);
    CHECK(interim_response.find("Content-Encoding:") == std::string::npos);
    CHECK(interim_response.find("Content-Length: 27\r\n") != std::string::npos);
    CHECK(no_content_response.find(" 204 ") != std::string::npos);
    CHECK(no_content_response.find("Content-Encoding:") == std::string::npos);
    // The gate leaves ordinary responses untouched.
    CHECK(normal_response.find("Content-Encoding: gzip") != std::string::npos);
} // discarded_bodies_are_not_compressed
#endif


TEST_CASE("deferred_chunked_response_replaced_by_static_file_closes_after_pipelined_input")
{
    SimpleApp app;
    auto deferred_end_promise = std::make_shared<std::promise<std::function<void()>>>();
    auto deferred_end = deferred_end_promise->get_future();
    auto provider_calls = std::make_shared<std::atomic<std::size_t>>(0);
    auto second_route_calls = std::make_shared<std::atomic<std::size_t>>(0);
    auto completion_observation = std::make_shared<ChunkCompletionObservation>();
    auto completion_result = completion_observation->first_result();

    CROW_ROUTE(app, "/deferred-static-boundary")
    ([deferred_end_promise, provider_calls, completion_observation](const crow::request&, crow::response& res) {
        res.set_async_chunked_content_provider(
          [provider_calls](crow::response::async_chunk_completion_t) {
              provider_calls->fetch_add(1);
          });
        res.set_chunked_completion_handler(
          [completion_observation](bool clean) {
              completion_observation->record(clean);
          });
        deferred_end_promise->set_value([&res] {
            res.set_static_file_info("tests/img/cat.jpg");
            res.end();
        });
    });
    CROW_ROUTE(app, "/after-deferred-static-boundary")
    ([second_route_calls](const crow::request&, crow::response& res) {
        second_route_calls->fetch_add(1);
        res.end("second-static");
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    app.wait_for_server_start();
    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string requests = "GET /deferred-static-boundary HTTP/1.1\r\nHost: localhost\r\n\r\n"
                                 "GET /after-deferred-static-boundary HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    asio::write(client, asio::buffer(requests));

    REQUIRE(deferred_end.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    CHECK(second_route_calls->load() == 0);
    deferred_end.get()();
    std::string response;
    const bool connection_closed = receive_until_closed_with_deadline(client, response, std::chrono::seconds(5));
    asio_error_code close_error;
    client.close(close_error);
    server_shutdown.shutdown();

    REQUIRE(connection_closed);
    CHECK(provider_calls->load() == 0);
    CHECK(second_route_calls->load() == 0);
    CHECK(response.find("second-static") == std::string::npos);
    REQUIRE(completion_result.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    CHECK(completion_result.get() == true);
    CHECK(completion_observation->calls() == 1);
} // deferred_chunked_response_replaced_by_static_file_closes_after_pipelined_input


TEST_CASE("async_chunked_response_closes_after_pipelined_input")
{
    SimpleApp app;
    auto observation = std::make_shared<PipelinedAsyncObservation>();

    CROW_ROUTE(app, "/async-close-on-pipeline")
    ([observation](const crow::request&, crow::response& res) {
        observation->first_route_calls.fetch_add(1);
        res.set_async_chunked_content_provider(
          [observation](crow::response::async_chunk_completion_t complete) {
              observation->provider_calls.fetch_add(1);
              observation->provider_completion.capture(std::move(complete));
          });
        res.set_chunked_completion_handler([observation](bool clean) {
            observation->completion.record(clean);
        });
        res.end();
    });
    CROW_ROUTE(app, "/after-async-close-on-pipeline")
    ([observation](const crow::request&, crow::response& res) {
        observation->second_route_calls.fetch_add(1);
        res.end("second");
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    REQUIRE(app.wait_for_server_start() == std::cv_status::no_timeout);

    auto completion_result = observation->completion.first_result();
    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string requests = "GET /async-close-on-pipeline HTTP/1.1\r\nHost: localhost\r\n\r\n"
                                 "GET /after-async-close-on-pipeline HTTP/1.1\r\nHost: localhost\r\n\r\n";
    asio::write(client, asio::buffer(requests));

    REQUIRE(observation->provider_completion.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    CHECK(observation->provider_completion.complete(crow::chunk_result::done, "payload"));

    std::string response;
    const bool connection_closed = receive_until_closed_with_deadline(client, response, std::chrono::seconds(5));
    asio_error_code close_error;
    client.close(close_error);

    // Pipelined bytes behind a streamed response: the stream finishes
    // correctly, then the connection closes; the second request is not served.
    REQUIRE(connection_closed);
    CHECK(response.find("Transfer-Encoding: chunked") != std::string::npos);
    CHECK(response.find("7\r\npayload\r\n") != std::string::npos);
    CHECK(response.find("0\r\n\r\n") != std::string::npos);
    CHECK(observation->second_route_calls.load() == 0);
    REQUIRE(completion_result.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    CHECK(completion_result.get() == true);
} // async_chunked_response_closes_after_pipelined_input

TEST_CASE("async_chunked_response_aborts_when_peer_closes_after_early_input")
{
    SimpleApp app;
    auto observation = std::make_shared<PipelinedAsyncObservation>();

    CROW_ROUTE(app, "/early-input-then-close")
    ([observation](const crow::request&, crow::response& res) {
        res.set_async_chunked_content_provider(
          [observation](crow::response::async_chunk_completion_t complete) {
              observation->provider_calls.fetch_add(1);
              observation->provider_completion.capture(std::move(complete));
          });
        res.set_chunked_completion_handler([observation](bool clean) {
            observation->completion.record(clean);
        });
        res.end();
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    REQUIRE(app.wait_for_server_start() == std::cv_status::no_timeout);

    auto completion_result = observation->completion.first_result();
    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string request = "GET /early-input-then-close HTTP/1.1\r\nHost: localhost\r\n\r\n";
    asio::write(client, asio::buffer(request));
    REQUIRE(observation->provider_completion.wait_for(std::chrono::seconds(5)) == std::future_status::ready);

    // A stray early byte followed by a full close: the idle stream must not
    // become an undetectable zombie holding the provider and the socket.
    asio::write(client, asio::buffer(std::string("X")));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    asio_error_code close_error;
    client.close(close_error);

    const auto completion_status = completion_result.wait_for(std::chrono::seconds(3));
    REQUIRE(completion_status == std::future_status::ready);
    CHECK(completion_result.get() == false);
    CHECK(observation->completion.calls() == 1);
} // async_chunked_response_aborts_when_peer_closes_after_early_input

TEST_CASE("keep_alive_emission_respects_an_application_connection_header")
{
    SimpleApp app;

    CROW_ROUTE(app, "/app-connection-header")
    ([](const crow::request&, crow::response& res) {
        res.set_header("Connection", "close");
        res.end("owned");
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    REQUIRE(app.wait_for_server_start() == std::cv_status::no_timeout);

    HttpClient client(LOCALHOST_ADDRESS, 45451);
    client.send("GET /app-connection-header HTTP/1.1\r\nHost: localhost\r\n\r\n");
    std::string response;
    receive_with_deadline(client.socket(), response, std::chrono::seconds(3), has_complete_http_response);

    std::size_t connection_headers = 0;
    std::string lowered;
    lowered.resize(response.size());
    std::transform(response.begin(), response.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    for (auto at = lowered.find("\nconnection:"); at != std::string::npos; at = lowered.find("\nconnection:", at + 1))
        ++connection_headers;
    CHECK(connection_headers == 1);
    CHECK(lowered.find("connection: close") != std::string::npos);

    asio_error_code close_error;
    client.socket().close(close_error);
} // keep_alive_emission_respects_an_application_connection_header

TEST_CASE("application_connection_close_closes_the_connection")
{
    SimpleApp app;
    auto stream_completion = std::make_shared<ChunkCompletionObservation>();
    auto stream_result = stream_completion->first_result();

    CROW_ROUTE(app, "/plain-close")
    ([](const crow::request&, crow::response& res) {
        res.set_header("Connection", "Close");
        res.end("owned");
    });
    CROW_ROUTE(app, "/list-close")
    ([](const crow::request&, crow::response& res) {
        res.set_header("Connection", "keep-alive, Close");
        res.end("listed");
    });
    CROW_ROUTE(app, "/stream-close")
    ([stream_completion](const crow::request&, crow::response& res) {
        res.set_header("Connection", "close");
        int remaining = 1;
        res.set_chunked_content_provider(
          [remaining](std::string& chunk) mutable -> bool {
              if (remaining == 0)
                  return false;
              chunk = "payload";
              --remaining;
              return true;
          });
        res.set_chunked_completion_handler(
          [stream_completion](bool clean) {
              stream_completion->record(clean);
          });
        res.end();
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    REQUIRE(app.wait_for_server_start() == std::cv_status::no_timeout);

    const auto closed_response = [](const std::string& request_text) {
        HttpClient client(LOCALHOST_ADDRESS, 45451);
        client.send(request_text);
        std::string response;
        const bool closed = receive_until_closed_with_deadline(client.socket(), response, std::chrono::seconds(5));
        asio_error_code close_error;
        client.socket().close(close_error);
        REQUIRE(closed);
        return response;
    };

    // A keep-alive HTTP/1.1 request: the application-supplied header alone
    // must close the connection after the response.
    const auto plain_response = closed_response("GET /plain-close HTTP/1.1\r\nHost: localhost\r\n\r\n");
    CHECK(plain_response.find("owned") != std::string::npos);

    // HTTP/1.0 with request-side keep-alive: the response header still wins.
    const auto http_1_0_response = closed_response("GET /plain-close HTTP/1.0\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n");
    CHECK(http_1_0_response.find("owned") != std::string::npos);

    // The close token is honored inside a comma-separated list too.
    const auto list_response = closed_response("GET /list-close HTTP/1.1\r\nHost: localhost\r\n\r\n");
    CHECK(list_response.find("listed") != std::string::npos);

    // The chunked path delivers the whole body and terminator, then closes.
    const auto stream_response = closed_response("GET /stream-close HTTP/1.1\r\nHost: localhost\r\n\r\n");
    CHECK(stream_response.find("7\r\npayload\r\n") != std::string::npos);
    CHECK(stream_response.find("0\r\n\r\n") != std::string::npos);
    server_shutdown.shutdown();

    REQUIRE(stream_result.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    CHECK(stream_result.get() == true);
    CHECK(stream_completion->calls() == 1);
} // application_connection_close_closes_the_connection


TEST_CASE("later_packet_request_is_served_after_an_ordinary_deferred_response")
{
    SimpleApp app;
    auto deferred_end_promise = std::make_shared<std::promise<std::function<void()>>>();
    auto deferred_end = deferred_end_promise->get_future();

    CROW_ROUTE(app, "/later-packet-first")
    ([deferred_end_promise](const crow::request&, crow::response& res) {
        deferred_end_promise->set_value([&res] {
            res.end("first");
        });
    });
    CROW_ROUTE(app, "/later-packet-second")
    ([] {
        return "second";
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    app.wait_for_server_start();

    HttpClient client(LOCALHOST_ADDRESS, 45451);
    client.send("GET /later-packet-first HTTP/1.1\r\nHost: localhost\r\n\r\n");
    REQUIRE(deferred_end.wait_for(std::chrono::seconds(3)) == std::future_status::ready);

    // The second request arrives in a separate packet while the first
    // response is deferred: it is served afterwards as a sequential request.
    client.send("GET /later-packet-second HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    deferred_end.get()();

    std::string response;
    REQUIRE(receive_until_closed_with_deadline(client.socket(), response, std::chrono::seconds(5)));
    asio_error_code close_error;
    client.socket().close(close_error);
    server_shutdown.shutdown();

    const auto first_at = response.find("first");
    const auto second_at = response.find("second");
    REQUIRE(first_at != std::string::npos);
    REQUIRE(second_at != std::string::npos);
    CHECK(first_at < second_at);
} // later_packet_request_is_served_after_an_ordinary_deferred_response


struct ExecutorStampLocalMiddleware : crow::ILocalMiddleware
{
    struct context
    {};
    static inline std::shared_ptr<std::promise<std::thread::id>> after_handle_thread;
    void before_handle(crow::request&, crow::response&, context&) {}
    void after_handle(crow::request&, crow::response&, context&)
    {
        if (auto stamp = after_handle_thread)
        {
            after_handle_thread = nullptr;
            stamp->set_value(std::this_thread::get_id());
        }
    }
};

struct ThrowingLocalMiddleware : crow::ILocalMiddleware
{
    struct context
    {};
    void before_handle(crow::request&, crow::response&, context&) {}
    void after_handle(crow::request&, crow::response&, context&)
    {
        throw std::runtime_error("after_handle failure");
    }
};

TEST_CASE("route_local_after_handlers_run_on_the_connection_executor")
{
    crow::App<ExecutorStampLocalMiddleware, ThrowingLocalMiddleware> app;
    ExecutorStampLocalMiddleware::after_handle_thread = std::make_shared<std::promise<std::thread::id>>();
    auto after_handle_thread = ExecutorStampLocalMiddleware::after_handle_thread->get_future();
    auto handler_thread_promise = std::make_shared<std::promise<std::thread::id>>();
    auto handler_thread = handler_thread_promise->get_future();
    auto deferred_end_promise = std::make_shared<std::promise<std::function<void()>>>();
    auto deferred_end = deferred_end_promise->get_future();

    CROW_ROUTE(app, "/local-mw-deferred")
      .middlewares<decltype(app), ExecutorStampLocalMiddleware>()(
        [handler_thread_promise, deferred_end_promise](const crow::request&, crow::response& res) {
            handler_thread_promise->set_value(std::this_thread::get_id());
            deferred_end_promise->set_value([&res] {
                res.end("deferred");
            });
        });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).concurrency(1).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    app.wait_for_server_start();

    HttpClient client(LOCALHOST_ADDRESS, 45451);
    client.send("GET /local-mw-deferred HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    REQUIRE(deferred_end.wait_for(std::chrono::seconds(3)) == std::future_status::ready);

    // end() runs on a foreign thread; the route-local after handler must not.
    std::thread foreign_end_thread([&deferred_end] {
        deferred_end.get()();
    });
    const auto foreign_thread_id = foreign_end_thread.get_id();

    std::string response;
    REQUIRE(receive_until_closed_with_deadline(client.socket(), response, std::chrono::seconds(5)));
    asio_error_code close_error;
    client.socket().close(close_error);
    foreign_end_thread.join();
    server_shutdown.shutdown();

    CHECK(response.find("deferred") != std::string::npos);
    REQUIRE(after_handle_thread.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    const auto after_thread_id = after_handle_thread.get();
    CHECK(after_thread_id != foreign_thread_id);
    REQUIRE(handler_thread.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    CHECK(after_thread_id == handler_thread.get());
} // route_local_after_handlers_run_on_the_connection_executor


TEST_CASE("throwing_route_local_after_handler_still_completes_the_response")
{
    crow::App<ExecutorStampLocalMiddleware, ThrowingLocalMiddleware> app;

    CROW_ROUTE(app, "/local-mw-throwing")
      .middlewares<decltype(app), ThrowingLocalMiddleware>()(
        [](const crow::request&, crow::response& res) {
            res.end("survived");
        });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    app.wait_for_server_start();

    HttpClient client(LOCALHOST_ADDRESS, 45451);
    client.send("GET /local-mw-throwing HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    std::string response;
    REQUIRE(receive_until_closed_with_deadline(client.socket(), response, std::chrono::seconds(5)));
    asio_error_code close_error;
    client.socket().close(close_error);
    server_shutdown.shutdown();

    CHECK(response.find("survived") != std::string::npos);
} // throwing_route_local_after_handler_still_completes_the_response


TEST_CASE("is_alive_is_readable_from_a_foreign_thread_while_deferred")
{
    SimpleApp app;
    auto deferred_response_promise = std::make_shared<std::promise<crow::response*>>();
    auto deferred_response = deferred_response_promise->get_future();

    CROW_ROUTE(app, "/deferred-is-alive")
    ([deferred_response_promise](const crow::request&, crow::response& res) {
        deferred_response_promise->set_value(&res);
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    app.wait_for_server_start();

    HttpClient client(LOCALHOST_ADDRESS, 45451);
    client.send("GET /deferred-is-alive HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    REQUIRE(deferred_response.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    crow::response* res = deferred_response.get();

    // The documented any-thread window: after the handler returned with the
    // response deferred and before end().
    std::atomic<std::size_t> alive_observations{0};
    std::thread poller([res, &alive_observations] {
        for (int i = 0; i < 100; ++i)
        {
            if (res->is_alive())
                alive_observations.fetch_add(1);
        }
    });
    poller.join();
    res->end("polled");

    std::string response;
    REQUIRE(receive_until_closed_with_deadline(client.socket(), response, std::chrono::seconds(5)));
    asio_error_code close_error;
    client.socket().close(close_error);
    server_shutdown.shutdown();

    CHECK(response.find("polled") != std::string::npos);
    CHECK(alive_observations.load() == 100);
} // is_alive_is_readable_from_a_foreign_thread_while_deferred


struct BodyStampMiddleware
{
    struct context
    {};
    void before_handle(crow::request&, crow::response&, context&) {}
    void after_handle(crow::request&, crow::response& res, context&)
    {
        res.body += "-stamped";
    }
};

TEST_CASE("head_content_length_matches_the_post_middleware_representation")
{
    crow::App<BodyStampMiddleware> app;

    CROW_ROUTE(app, "/stamped")
      .methods("GET"_method, "HEAD"_method)([](const crow::request&, crow::response& res) {
          res.end("base");
      });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    app.wait_for_server_start();

    const auto request = [](const std::string& method) {
        HttpClient client(LOCALHOST_ADDRESS, 45451);
        client.send(method + " /stamped HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
        std::string response;
        const bool closed = receive_until_closed_with_deadline(client.socket(), response, std::chrono::seconds(5));
        asio_error_code close_error;
        client.socket().close(close_error);
        REQUIRE(closed);
        return response;
    };

    const auto get_response = request("GET");
    const auto head_response = request("HEAD");
    server_shutdown.shutdown();

    // Middleware appended to the body, so HEAD must report the length of the
    // representation the equivalent GET actually sends.
    CHECK(get_response.find("base-stamped") != std::string::npos);
    CHECK(get_response.find("Content-Length: 12\r\n") != std::string::npos);
    CHECK(head_response.find("Content-Length: 12\r\n") != std::string::npos);
    const auto head_header_end = head_response.find("\r\n\r\n");
    REQUIRE(head_header_end != std::string::npos);
    CHECK(head_response.size() == head_header_end + 4);
} // head_content_length_matches_the_post_middleware_representation


TEST_CASE("chunked_header_write_runs_under_the_write_deadline")
{
    SimpleApp app;
    PausingSocketContext socket_context;
    socket_context.pause_next_write();
    auto header_write_pending = socket_context.pending_write_future();
    auto completion_observation = std::make_shared<ChunkCompletionObservation>();
    auto completion_result = completion_observation->first_result();

    CROW_ROUTE(app, "/paused-header-stream")
    ([completion_observation](const crow::request&, crow::response& res) {
        res.set_async_chunked_content_provider(
          [](crow::response::async_chunk_completion_t) {
              // The provider stays idle: the header write is the phase under test.
          });
        res.set_chunked_completion_handler([completion_observation](bool clean) {
            completion_observation->record(clean);
        });
        res.end();
    });

    app.validate();
    std::tuple<> middlewares;
    using PausedWriteServer = crow::Server<crow::SimpleApp, crow::TCPAcceptor, PausingSocketAdaptor>;
    PausedWriteServer server(&app,
                             asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451),
                             "Crow/Test",
                             &middlewares,
                             2,
                             1,
                             &socket_context);
    auto server_task = std::async(std::launch::async, [&server] {
        server.run();
    });
    BoundedServerShutdown server_shutdown(server_task, [&server] {
        server.stop();
    });
    REQUIRE(server.wait_for_start(std::chrono::steady_clock::now() + std::chrono::seconds(3)) == std::cv_status::no_timeout);

    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    asio::write(client, asio::buffer(std::string("GET /paused-header-stream HTTP/1.1\r\nHost: localhost\r\n\r\n")));
    REQUIRE(header_write_pending.wait_for(std::chrono::seconds(3)) == std::future_status::ready);

    // The parked header write runs under the connection deadline: the timer
    // closes the socket, so the client observes EOF instead of a silent hang.
    std::string response;
    REQUIRE(receive_until_closed_with_deadline(client, response, std::chrono::seconds(5)));
    asio_error_code close_error;
    client.close(close_error);

    socket_context.resume_pending_write();
    REQUIRE(completion_result.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    CHECK(completion_result.get() == false);
    CHECK(completion_observation->calls() == 1);
    server_shutdown.shutdown();
} // chunked_header_write_runs_under_the_write_deadline

TEST_CASE("concurrent_end_calls_deliver_exactly_one_body")
{
    SimpleApp app;
    auto deferred_end_promise = std::make_shared<std::promise<std::reference_wrapper<crow::response>>>();

    CROW_ROUTE(app, "/racing-end")
    ([deferred_end_promise](const crow::request&, crow::response& res) {
        deferred_end_promise->set_value(std::ref(res));
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    REQUIRE(app.wait_for_server_start() == std::cv_status::no_timeout);

    for (int round = 0; round < 60; ++round)
    {
        auto fresh_promise = std::make_shared<std::promise<std::reference_wrapper<crow::response>>>();
        *deferred_end_promise = std::move(*fresh_promise);
        auto deferred = deferred_end_promise->get_future();

        HttpClient client(LOCALHOST_ADDRESS, 45451);
        client.send("GET /racing-end HTTP/1.1\r\nHost: localhost\r\n\r\n");
        REQUIRE(deferred.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
        crow::response& res = deferred.get();

        std::atomic<int> go{0};
        std::thread first([&] {
            go.fetch_add(1);
            while (go.load() < 2) {}
            res.end("alpha");
        });
        std::thread second([&] {
            go.fetch_add(1);
            while (go.load() < 2) {}
            res.end("bravo");
        });
        first.join();
        second.join();

        std::string response;
        REQUIRE(receive_with_deadline(client.socket(), response, std::chrono::seconds(3), has_complete_http_response));
        const auto header_end = response.find("\r\n\r\n");
        REQUIRE(header_end != std::string::npos);
        const std::string body = response.substr(header_end + 4);
        // Only the first end() takes effect: the body is one of the two
        // candidates, never a concatenation.
        const bool single_body = body == "alpha" || body == "bravo";
        REQUIRE(single_body);
        asio_error_code close_error;
        client.socket().close(close_error);
    }
} // concurrent_end_calls_deliver_exactly_one_body

TEST_CASE("max_stream_chunk_size_zero_means_unlimited")
{
    SimpleApp app;
    auto completion_observation = std::make_shared<ChunkCompletionObservation>();
    auto completion_result = completion_observation->first_result();

    CROW_ROUTE(app, "/uncapped-chunk")
    ([completion_observation](const crow::request&, crow::response& res) {
        res.set_async_chunked_content_provider(
          [](crow::response::async_chunk_completion_t complete) {
              complete(crow::chunk_result::done, std::string(64u * 1024u, 'y'));
          });
        res.set_chunked_completion_handler([completion_observation](bool clean) {
            completion_observation->record(clean);
        });
        res.end();
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).max_stream_chunk_size(0).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    REQUIRE(app.wait_for_server_start() == std::cv_status::no_timeout);

    HttpClient client(LOCALHOST_ADDRESS, 45451);
    client.send("GET /uncapped-chunk HTTP/1.1\r\nHost: localhost\r\n\r\n");
    std::string response;
    while (response.size() < 5 || response.compare(response.size() - 5, 5, "0\r\n\r\n") != 0)
        response += client.receive();

    REQUIRE(completion_result.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    CHECK(completion_result.get() == true);

    asio_error_code close_error;
    client.socket().close(close_error);
} // max_stream_chunk_size_zero_means_unlimited


TEST_CASE("deferred_chunked_response_closes_after_pipelined_input")
{
    SimpleApp app;
    auto deferred_end_promise = std::make_shared<std::promise<std::function<void()>>>();
    auto deferred_end = deferred_end_promise->get_future();
    auto second_route_calls = std::make_shared<std::atomic<std::size_t>>(0);
    auto completion_observation = std::make_shared<ChunkCompletionObservation>();
    auto completion_result = completion_observation->first_result();

    CROW_ROUTE(app, "/deferred-close-on-pipeline")
    ([deferred_end_promise, completion_observation](const crow::request&, crow::response& res) {
        res.set_chunked_content_provider(
          [sent = false](std::string& chunk) mutable -> bool {
              if (sent)
                  return false;
              chunk = "payload";
              sent = true;
              return true;
          },
          "text/plain");
        res.set_chunked_completion_handler([completion_observation](bool clean) {
            completion_observation->record(clean);
        });
        deferred_end_promise->set_value([&res] {
            res.end();
        });
    });
    CROW_ROUTE(app, "/after-deferred-close-on-pipeline")
    ([second_route_calls](const crow::request&, crow::response& res) {
        second_route_calls->fetch_add(1);
        res.end("second");
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    REQUIRE(app.wait_for_server_start() == std::cv_status::no_timeout);

    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string requests = "GET /deferred-close-on-pipeline HTTP/1.1\r\nHost: localhost\r\n\r\n"
                                 "GET /after-deferred-close-on-pipeline HTTP/1.1\r\nHost: localhost\r\n\r\n";
    asio::write(client, asio::buffer(requests));

    REQUIRE(deferred_end.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    deferred_end.get()();

    std::string response;
    const bool connection_closed = receive_until_closed_with_deadline(client, response, std::chrono::seconds(5));
    asio_error_code close_error;
    client.close(close_error);

    REQUIRE(connection_closed);
    CHECK(response.find("Transfer-Encoding: chunked") != std::string::npos);
    CHECK(response.find("7\r\npayload\r\n") != std::string::npos);
    CHECK(response.find("0\r\n\r\n") != std::string::npos);
    CHECK(second_route_calls->load() == 0);
    REQUIRE(completion_result.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    CHECK(completion_result.get() == true);
} // deferred_chunked_response_closes_after_pipelined_input


TEST_CASE("static_file_failure_reports_unclean_chunk_completion")
{
    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string file_path = "crow-static-completion-" + std::to_string(unique_suffix) + ".tmp";
    {
        std::ofstream file(file_path, std::ios::binary);
        REQUIRE(file.good());
        file << "static-body";
    }

    SimpleApp app;
    auto completion_observation = std::make_shared<ChunkCompletionObservation>();
    auto completion_result = completion_observation->first_result();
    auto removal_result = std::make_shared<std::atomic<int>>(-1);

    CROW_ROUTE(app, "/missing-static")
    ([file_path, completion_observation, removal_result](crow::response& res) {
        res.set_async_chunked_content_provider([](crow::response::async_chunk_completion_t) {});
        res.set_chunked_completion_handler(
          [completion_observation](bool clean) {
              completion_observation->record(clean);
          });
        res.set_static_file_info_unsafe(file_path);
        removal_result->store(std::remove(file_path.c_str()));
        res.end();
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45463).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    app.wait_for_server_start();
    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45463));
    const std::string request = "GET /missing-static HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    asio::write(client, asio::buffer(request));

    std::string response;
    const bool connection_closed = receive_until_closed_with_deadline(client, response, std::chrono::seconds(5));
    asio_error_code close_error;
    client.close(close_error);
    server_shutdown.shutdown();

    REQUIRE(connection_closed);
    CHECK(removal_result->load() == 0);
    REQUIRE(completion_result.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    CHECK(completion_result.get() == false);
    CHECK(completion_observation->calls() == 1);
} // static_file_failure_reports_unclean_chunk_completion


TEST_CASE("deferred_chunked_response_replaced_by_large_body_closes_after_pipelined_input")
{
    SimpleApp app;
    app.stream_threshold(8);
    auto deferred_end_promise = std::make_shared<std::promise<std::function<void()>>>();
    auto deferred_end         = deferred_end_promise->get_future();
    auto provider_calls       = std::make_shared<std::atomic<std::size_t>>(0);
    auto second_route_calls   = std::make_shared<std::atomic<std::size_t>>(0);
    auto first_body           = std::make_shared<std::string>(20000, 'x');

    CROW_ROUTE(app, "/deferred-large-body-boundary")
    ([deferred_end_promise, provider_calls, first_body](const crow::request& req, crow::response& res) {
        res.set_async_chunked_content_provider(
            [provider_calls](crow::response::async_chunk_completion_t) { provider_calls->fetch_add(1); });
        asio::post(*req.io_context, [deferred_end_promise, &res, first_body] {
            deferred_end_promise->set_value([&res, first_body] {
                res.clear();
                res.end(*first_body);
            });
        });
    });
    CROW_ROUTE(app, "/after-deferred-large-body-boundary")
    ([second_route_calls](const crow::request&, crow::response& res) {
        second_route_calls->fetch_add(1);
        res.end("second-large-body");
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] { app.stop(); });
    app.wait_for_server_start();
    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string requests
        = "GET /deferred-large-body-boundary HTTP/1.1\r\nHost: localhost\r\n\r\n"
          "GET /after-deferred-large-body-boundary HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    asio::write(client, asio::buffer(requests));

    REQUIRE(deferred_end.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    CHECK(second_route_calls->load() == 0);
    deferred_end.get()();
    std::string response;
    const bool connection_closed = receive_until_closed_with_deadline(client, response, std::chrono::seconds(5));
    asio_error_code close_error;
    client.close(close_error);
    server_shutdown.shutdown();

    REQUIRE(connection_closed);
    CHECK(provider_calls->load() == 0);
    CHECK(second_route_calls->load() == 0);
    CHECK(response.find(*first_body) != std::string::npos);
    CHECK(response.find("second-large-body") == std::string::npos);
} // deferred_chunked_response_replaced_by_large_body_closes_after_pipelined_input


TEST_CASE("failed_regular_response_discards_retained_pipelined_input") {
    SimpleApp app;
    auto deferred_end_promise = std::make_shared<std::promise<std::function<void()>>>();
    auto deferred_end         = deferred_end_promise->get_future();
    auto second_route_calls   = std::make_shared<std::atomic<std::size_t>>(0);
    PausingSocketContext socket_context;

    CROW_ROUTE(app, "/failing-regular-response")
    ([deferred_end_promise, &socket_context](const crow::request& req, crow::response& res) {
        res.set_async_chunked_content_provider([](crow::response::async_chunk_completion_t) {});
        asio::post(*req.io_context, [deferred_end_promise, &res, &socket_context] {
            deferred_end_promise->set_value([&res, &socket_context] {
                res.clear();
                socket_context.fail_next_write();
                res.end("first");
            });
        });
    });
    CROW_ROUTE(app, "/after-failing-regular-response")
    ([second_route_calls](const crow::request&, crow::response& res) {
        second_route_calls->fetch_add(1);
        res.end("unexpected");
    });

    app.validate();
    std::tuple<> middlewares;
    using RegularWriteErrorServer = crow::Server<crow::SimpleApp, crow::TCPAcceptor, PausingSocketAdaptor>;
    RegularWriteErrorServer server(&app,
                                   asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451),
                                   "Crow/Test",
                                   &middlewares,
                                   2,
                                   5,
                                   &socket_context);
    auto server_task = std::async(std::launch::async, [&server] { server.run(); });
    BoundedServerShutdown server_shutdown(server_task, [&server] { server.stop(); });
    REQUIRE(server.wait_for_start(std::chrono::steady_clock::now() + std::chrono::seconds(3))
            == std::cv_status::no_timeout);

    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string requests
        = "GET /failing-regular-response HTTP/1.1\r\nHost: localhost\r\n\r\n"
          "GET /after-failing-regular-response HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    asio::write(client, asio::buffer(requests));

    REQUIRE(deferred_end.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    CHECK(second_route_calls->load() == 0);
    deferred_end.get()();
    std::string response;
    const bool connection_closed = receive_until_closed_with_deadline(client, response, std::chrono::seconds(5));
    asio_error_code close_error;
    client.close(close_error);
    server_shutdown.shutdown();

    REQUIRE(connection_closed);
    CHECK(second_route_calls->load() == 0);
    CHECK(response.find("unexpected") == std::string::npos);
} // failed_regular_response_discards_retained_pipelined_input


TEST_CASE("async_chunked_response_closes_after_input_arrives_while_provider_waits")
{
    SimpleApp app;

    auto observation = std::make_shared<PipelinedAsyncObservation>();
    PausingSocketContext socket_context;
    auto waiting_read_started = socket_context.observed_read_started_future();
    auto waiting_read_completed = socket_context.observed_read_completed_future();

    CROW_ROUTE(app, "/waiting-pipeline")
    ([observation, &socket_context](const crow::request&, crow::response& res) {
        observation->first_route_calls.fetch_add(1);
        res.set_header("X-Pipeline-Response", "waiting");
        res.set_async_chunked_content_provider(
          [observation, &socket_context](crow::response::async_chunk_completion_t complete) {
              observation->provider_calls.fetch_add(1);
              socket_context.observe_next_read();
              observation->provider_completion.capture(std::move(complete));
          });
        res.set_chunked_completion_handler([observation](bool clean) {
            observation->first_completion_seen.store(true);
            observation->completion.record(clean);
        });
        res.end();
    });

    CROW_ROUTE(app, "/after-waiting-pipeline")
    ([observation](const crow::request&, crow::response& res) {
        observation->second_route_calls.fetch_add(1);
        if (!observation->first_completion_seen.load())
            observation->second_route_overlapped.store(true);
        res.end("after-wait");
    });

    app.validate();
    std::tuple<> middlewares;
    using ObservedReadServer = crow::Server<crow::SimpleApp, crow::TCPAcceptor, PausingSocketAdaptor>;
    ObservedReadServer server(&app,
                              asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451),
                              "Crow/Test",
                              &middlewares,
                              2,
                              5,
                              &socket_context);
    auto server_task = std::async(std::launch::async, [&server] {
        server.run();
    });
    BoundedServerShutdown server_shutdown(server_task, [&server] {
        server.stop();
    });
    REQUIRE(server.wait_for_start(std::chrono::steady_clock::now() + std::chrono::seconds(3)) == std::cv_status::no_timeout);

    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string first_request = "GET /waiting-pipeline HTTP/1.1\r\nHost: localhost\r\n\r\n";
    asio::write(client, asio::buffer(first_request));

    auto completion_result = observation->completion.first_result();
    const auto provider_status = observation->provider_completion.wait_for(std::chrono::seconds(5));
    const auto read_start_status = waiting_read_started.wait_for(std::chrono::seconds(1));
    const std::string second_request = "GET /after-waiting-pipeline HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    asio_error_code pipeline_write_error;
    asio::write(client, asio::buffer(second_request), pipeline_write_error);
    const auto read_completion_status = waiting_read_completed.wait_for(std::chrono::seconds(1));
    const std::size_t retained_bytes = read_completion_status == std::future_status::ready ? waiting_read_completed.get() : 0;
    const bool second_route_called_while_waiting = observation->second_route_calls.load() != 0;
    const bool result_accepted = provider_status == std::future_status::ready ? observation->provider_completion.complete(crow::chunk_result::done, "waiting") : false;

    std::string response;
    const bool connection_closed = receive_until_closed_with_deadline(client, response, std::chrono::seconds(5));
    const auto completion_status = completion_result.wait_for(std::chrono::seconds(1));
    const bool clean = completion_status == std::future_status::ready ? completion_result.get() : false;

    asio_error_code close_error;
    client.close(close_error);
    server_shutdown.shutdown();

    REQUIRE(provider_status == std::future_status::ready);
    REQUIRE(read_start_status == std::future_status::ready);
    CHECK_FALSE(pipeline_write_error);
    REQUIRE(read_completion_status == std::future_status::ready);
    CHECK(retained_bytes > 0);
    CHECK(retained_bytes <= second_request.size());
    CHECK_FALSE(second_route_called_while_waiting);
    CHECK(result_accepted);
    REQUIRE(connection_closed);
    REQUIRE(completion_status == std::future_status::ready);
    CHECK(clean);
    CHECK(observation->first_route_calls.load() == 1);
    CHECK(observation->provider_calls.load() == 1);
    CHECK(observation->completion.calls() == 1);
    CHECK(observation->second_route_calls.load() == 0);

    const auto first_header_end = response.find("\r\n\r\n");
    REQUIRE(first_header_end != std::string::npos);
    const auto first_body_begin = first_header_end + 4;
    const std::string first_body = "7\r\nwaiting\r\n0\r\n\r\n";
    REQUIRE(response.compare(first_body_begin, first_body.size(), first_body) == 0);
    CHECK(response.size() == first_body_begin + first_body.size());
} // async_chunked_response_closes_after_input_arrives_while_provider_waits


TEST_CASE("async_chunked_response_keeps_header_storage_stable_after_end")
{
    SimpleApp app;

    PausingSocketContext socket_context;
    socket_context.pause_next_write();
    auto header_write_pending = socket_context.pending_write_future();

    CROW_ROUTE(app, "/stable-async-headers")
    ([](const crow::request&, crow::response& res) {
        res.set_header("Connection", "custom-keep-alive-value-with-owned-storage");
        res.set_async_chunked_content_provider(
          [](crow::response::async_chunk_completion_t complete) {
              complete(crow::chunk_result::done, "body");
          });
        res.end();
    });

    app.validate();
    std::tuple<> middlewares;
    using PausedWriteServer = crow::Server<crow::SimpleApp, crow::TCPAcceptor, PausingSocketAdaptor>;
    PausedWriteServer server(&app,
                             asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451),
                             "Crow/Test",
                             &middlewares,
                             2,
                             5,
                             &socket_context);
    auto server_task = std::async(std::launch::async, [&server] {
        server.run();
    });
    BoundedServerShutdown server_shutdown(server_task, [&server] {
        server.stop();
    });
    REQUIRE(server.wait_for_start(std::chrono::steady_clock::now() + std::chrono::seconds(3)) == std::cv_status::no_timeout);

    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string request = "GET /stable-async-headers HTTP/1.1\r\nHost: localhost\r\n\r\n";
    asio::write(client, asio::buffer(request));

    REQUIRE(header_write_pending.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    socket_context.resume_pending_write();
    std::string response;
    const bool response_complete = receive_with_deadline(client, response, std::chrono::seconds(5), has_chunk_terminator);

    asio_error_code close_error;
    client.close(close_error);
    server_shutdown.shutdown();

    REQUIRE(response_complete);
    CHECK(response.find("Connection: custom-keep-alive-value-with-owned-storage\r\n") != std::string::npos);
    CHECK(response.find("4\r\nbody\r\n0\r\n\r\n") != std::string::npos);
} // async_chunked_response_keeps_header_storage_stable_after_end


TEST_CASE("async_chunked_response_completed_from_other_threads") {
    SimpleApp app;

    auto next_chunk              = std::make_shared<std::atomic<std::size_t>>(0);
    auto active_requests         = std::make_shared<std::atomic<std::size_t>>(0);
    auto maximum_active_requests = std::make_shared<std::atomic<std::size_t>>(0);
    auto worker_tasks            = std::make_shared<std::vector<std::future<void>>>();
    auto worker_tasks_mutex      = std::make_shared<std::mutex>();
    auto completion_clean        = std::make_shared<std::promise<bool>>();

    CROW_ROUTE(app, "/async-chunks")
    ([next_chunk, active_requests, maximum_active_requests, worker_tasks, worker_tasks_mutex, completion_clean](
         const crow::request&, crow::response& res) {
        res.set_async_chunked_content_provider(
            [next_chunk, active_requests, maximum_active_requests, worker_tasks, worker_tasks_mutex](
                crow::response::async_chunk_completion_t complete) {
                const std::size_t index      = next_chunk->fetch_add(1);
                const std::size_t pending    = active_requests->fetch_add(1) + 1;
                std::size_t observed_maximum = maximum_active_requests->load();
                while (observed_maximum < pending
                       && !maximum_active_requests->compare_exchange_weak(observed_maximum, pending)) {
                }

                std::lock_guard<std::mutex> lock(*worker_tasks_mutex);
                worker_tasks->emplace_back(
                    std::async(std::launch::async, [index, active_requests, complete = std::move(complete)]() mutable {
                        active_requests->fetch_sub(1);
                        if (index < 2) {
                            complete(crow::chunk_result::more, "part" + std::to_string(index + 1));
                        } else {
                            complete(crow::chunk_result::done, "part3");
                        }
                    }));
            },
            "text/plain");
        res.set_chunked_completion_handler([completion_clean](bool clean) { completion_clean->set_value(clean); });
        res.end();
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    REQUIRE(app.wait_for_server_start() == std::cv_status::no_timeout);

    HttpClient client(LOCALHOST_ADDRESS, 45451);
    client.send("GET /async-chunks HTTP/1.1\r\nHost: localhost\r\n\r\n");

    std::string response;
    const bool response_complete = receive_with_deadline(client.socket(), response, std::chrono::seconds(5), has_chunk_terminator);

    std::vector<std::future<void>> tasks;
    {
        std::lock_guard<std::mutex> lock(*worker_tasks_mutex);
        tasks.swap(*worker_tasks);
    }
    for (auto& task : tasks)
        task.get();

    auto completion = completion_clean->get_future();
    const auto completion_status = completion.wait_for(std::chrono::seconds(5));
    const bool clean = completion_status == std::future_status::ready ? completion.get() : false;

    asio_error_code close_error;
    client.socket().close(close_error);
    server_shutdown.shutdown();

    REQUIRE(response_complete);
    CHECK(next_chunk->load() == 3);
    CHECK(active_requests->load() == 0);
    CHECK(maximum_active_requests->load() == 1);
    CHECK(response.find("Transfer-Encoding: chunked") != std::string::npos);
    CHECK(response.find("Content-Length") == std::string::npos);
    CHECK(response.find("Content-Type: text/plain") != std::string::npos);
    const auto header_end = response.find("\r\n\r\n");
    REQUIRE(header_end != std::string::npos);
    CHECK(response.substr(header_end + 4) == "5\r\npart1\r\n5\r\npart2\r\n5\r\npart3\r\n0\r\n\r\n");

    REQUIRE(completion_status == std::future_status::ready);
    CHECK(clean == true);
} // async_chunked_response_completed_from_other_threads


TEST_CASE("async_chunked_response_synchronous_completion_does_not_recurse") {
    SimpleApp app;

    const std::size_t chunk_count = 256;
    auto calls                    = std::make_shared<std::atomic<std::size_t>>(0);
    auto current_depth            = std::make_shared<std::atomic<std::size_t>>(0);
    auto maximum_depth            = std::make_shared<std::atomic<std::size_t>>(0);

    CROW_ROUTE(app, "/synchronous-async-chunks")
    ([calls, current_depth, maximum_depth, chunk_count](const crow::request&, crow::response& res) {
        res.set_async_chunked_content_provider(
            [calls, current_depth, maximum_depth, chunk_count](crow::response::async_chunk_completion_t complete) {
                const std::size_t depth      = current_depth->fetch_add(1) + 1;
                std::size_t observed_maximum = maximum_depth->load();
                while (observed_maximum < depth && !maximum_depth->compare_exchange_weak(observed_maximum, depth)) {
                }

                const std::size_t index = calls->fetch_add(1);
                if (index < chunk_count) {
                    complete(crow::chunk_result::more, "");
                } else {
                    complete(crow::chunk_result::done, "");
                }
                current_depth->fetch_sub(1);
            });
        res.end();
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    REQUIRE(app.wait_for_server_start() == std::cv_status::no_timeout);

    HttpClient client(LOCALHOST_ADDRESS, 45451);
    client.send("GET /synchronous-async-chunks HTTP/1.1\r\nHost: localhost\r\n\r\n");

    std::string response;
    const bool response_complete = receive_with_deadline(client.socket(), response, std::chrono::seconds(5), has_chunk_terminator);

    asio_error_code close_error;
    client.socket().close(close_error);
    server_shutdown.shutdown();

    REQUIRE(response_complete);
    CHECK(calls->load() == chunk_count + 1);
    CHECK(maximum_depth->load() == 1);
} // async_chunked_response_synchronous_completion_does_not_recurse


TEST_CASE("async_chunked_response_waits_beyond_connection_timeout") {
    SimpleApp app;

    auto worker_tasks       = std::make_shared<std::vector<std::future<void>>>();
    auto worker_tasks_mutex = std::make_shared<std::mutex>();
    auto completion_clean   = std::make_shared<std::promise<bool>>();
    auto completion         = completion_clean->get_future();

    CROW_ROUTE(app, "/delayed-async-chunk")
    ([worker_tasks, worker_tasks_mutex, completion_clean](const crow::request&, crow::response& res) {
        res.set_async_chunked_content_provider(
            [worker_tasks, worker_tasks_mutex](crow::response::async_chunk_completion_t complete) {
                std::lock_guard<std::mutex> lock(*worker_tasks_mutex);
                worker_tasks->emplace_back(std::async(std::launch::async, [complete = std::move(complete)]() mutable {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
                    complete(crow::chunk_result::done, "delayed");
                }));
            },
            "text/plain");
        res.set_chunked_completion_handler([completion_clean](bool clean) { completion_clean->set_value(clean); });
        res.end();
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).timeout(1).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] {
        app.stop();
    });
    REQUIRE(app.wait_for_server_start() == std::cv_status::no_timeout);

    HttpClient client(LOCALHOST_ADDRESS, 45451);
    client.send("GET /delayed-async-chunk HTTP/1.1\r\nHost: localhost\r\n\r\n");

    std::string response;
    const bool response_complete = receive_with_deadline(client.socket(), response, std::chrono::seconds(5), has_chunk_terminator);

    std::vector<std::future<void>> tasks;
    {
        std::lock_guard<std::mutex> lock(*worker_tasks_mutex);
        tasks.swap(*worker_tasks);
    }
    for (auto& task : tasks)
        task.get();

    const auto completion_status = completion.wait_for(std::chrono::seconds(5));
    const bool clean             = completion_status == std::future_status::ready ? completion.get() : false;

    asio_error_code close_error;
    client.socket().close(close_error);
    server_shutdown.shutdown();

    REQUIRE(response_complete);
    CHECK(response.find("Transfer-Encoding: chunked") != std::string::npos);
    CHECK(response.find("Content-Length") == std::string::npos);
    CHECK(response.find("Content-Type: text/plain") != std::string::npos);
    const auto header_end = response.find("\r\n\r\n");
    REQUIRE(header_end != std::string::npos);
    CHECK(response.substr(header_end + 4) == "7\r\ndelayed\r\n0\r\n\r\n");
    REQUIRE(completion_status == std::future_status::ready);
    CHECK(clean == true);
} // async_chunked_response_waits_beyond_connection_timeout


TEST_CASE("async_chunked_response_wait_does_not_block_other_routes") {
    SimpleApp app;

    auto provider_started_promise = std::make_shared<std::promise<void>>();
    auto provider_started         = provider_started_promise->get_future();
    auto completion_mutex         = std::make_shared<std::mutex>();
    auto delayed_completion       = std::make_shared<crow::response::async_chunk_completion_t>();

    CROW_ROUTE(app, "/waiting-async-chunk")
    ([provider_started_promise, completion_mutex, delayed_completion](const crow::request&, crow::response& res) {
        res.set_async_chunked_content_provider(
            [provider_started_promise, completion_mutex, delayed_completion](
                crow::response::async_chunk_completion_t complete) {
                {
                    std::lock_guard<std::mutex> lock(*completion_mutex);
                    *delayed_completion = std::move(complete);
                }
                provider_started_promise->set_value();
            },
            "text/plain");
        res.end();
    });

    CROW_ROUTE(app, "/ready-while-stream-waits")
    ([] { return "ready"; });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).concurrency(2).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app, completion_mutex, delayed_completion] {
        crow::response::async_chunk_completion_t complete;
        {
            std::lock_guard<std::mutex> lock(*completion_mutex);
            complete = std::move(*delayed_completion);
        }
        if (complete)
            complete(crow::chunk_result::abort, "");
        app.stop();
    });
    REQUIRE(app.wait_for_server_start() == std::cv_status::no_timeout);

    asio::io_context io_context;
    asio::ip::tcp::socket streaming_client(io_context);
    streaming_client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string streaming_request = "GET /waiting-async-chunk HTTP/1.1\r\nHost: localhost\r\n\r\n";
    asio::write(streaming_client, asio::buffer(streaming_request));

    const auto provider_status = provider_started.wait_for(std::chrono::seconds(5));

    asio::ip::tcp::socket regular_client(io_context);
    regular_client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string regular_request
        = "GET /ready-while-stream-waits HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    asio::write(regular_client, asio::buffer(regular_request));
    std::string regular_response;
    const bool regular_response_complete
        = receive_with_deadline(regular_client, regular_response, std::chrono::seconds(1), has_complete_http_response);

    crow::response::async_chunk_completion_t complete;
    if (provider_status == std::future_status::ready) {
        std::lock_guard<std::mutex> lock(*completion_mutex);
        complete = std::move(*delayed_completion);
    }
    if (complete)
        complete(crow::chunk_result::done, "delayed");

    const auto has_chunk_terminator = [](const std::string& response) {
        return response.size() >= 5 && response.compare(response.size() - 5, 5, "0\r\n\r\n") == 0;
    };
    std::string streaming_response;
    const bool streaming_response_complete
        = receive_with_deadline(streaming_client, streaming_response, std::chrono::seconds(5), has_chunk_terminator);

    asio_error_code close_error;
    regular_client.close(close_error);
    streaming_client.close(close_error);
    server_shutdown.shutdown();

    REQUIRE(provider_status == std::future_status::ready);
    REQUIRE(regular_response_complete);
    const auto regular_header_end = regular_response.find("\r\n\r\n");
    REQUIRE(regular_header_end != std::string::npos);
    CHECK(regular_response.substr(regular_header_end + 4) == "ready");

    REQUIRE(streaming_response_complete);
    const auto header_end = streaming_response.find("\r\n\r\n");
    REQUIRE(header_end != std::string::npos);
    CHECK(streaming_response.substr(header_end + 4) == "7\r\ndelayed\r\n0\r\n\r\n");
} // async_chunked_response_wait_does_not_block_other_routes


TEST_CASE("async_chunked_response_backpressures_provider_until_prior_write_completes") {
    SimpleApp app;

    auto provider_calls                = std::make_shared<std::atomic<std::size_t>>(0);
    auto active_provider_calls         = std::make_shared<std::atomic<std::size_t>>(0);
    auto maximum_active_provider_calls = std::make_shared<std::atomic<std::size_t>>(0);
    auto second_provider_call_promise  = std::make_shared<std::promise<void>>();
    auto second_provider_call          = second_provider_call_promise->get_future();
    auto connection_io_context_promise = std::make_shared<std::promise<asio::io_context*>>();
    auto connection_io_context         = connection_io_context_promise->get_future();
    PausingSocketContext socket_context;
    auto pending_write = socket_context.pending_write_future();

    CROW_ROUTE(app, "/backpressured-async-chunks")
    ([provider_calls,
      active_provider_calls,
      maximum_active_provider_calls,
      second_provider_call_promise,
      connection_io_context_promise,
      &socket_context](const crow::request& req, crow::response& res) {
        connection_io_context_promise->set_value(req.io_context);
        res.set_async_chunked_content_provider(
            [provider_calls,
             active_provider_calls,
             maximum_active_provider_calls,
             second_provider_call_promise,
             &socket_context](crow::response::async_chunk_completion_t complete) {
                const std::size_t active     = active_provider_calls->fetch_add(1) + 1;
                std::size_t observed_maximum = maximum_active_provider_calls->load();
                while (observed_maximum < active
                       && !maximum_active_provider_calls->compare_exchange_weak(observed_maximum, active)) {
                }

                const std::size_t call = provider_calls->fetch_add(1);
                if (call == 0) {
                    socket_context.pause_next_write();
                    complete(crow::chunk_result::more, "first");
                } else if (call == 1) {
                    second_provider_call_promise->set_value();
                    complete(crow::chunk_result::done, "");
                } else {
                    complete(crow::chunk_result::abort, "");
                }
                active_provider_calls->fetch_sub(1);
            },
            "application/octet-stream");
        res.end();
    });

    app.validate();
    std::tuple<> middlewares;
    using BackpressureServer = crow::Server<crow::SimpleApp, crow::TCPAcceptor, PausingSocketAdaptor>;
    BackpressureServer server(&app,
                              asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451),
                              "Crow/Test",
                              &middlewares,
                              2,
                              5,
                              &socket_context);
    auto server_task = std::async(std::launch::async, [&server] { server.run(); });
    BoundedServerShutdown server_shutdown(server_task, [&server, &socket_context] {
        socket_context.resume_pending_write();
        server.stop();
    });
    REQUIRE(server.wait_for_start(std::chrono::steady_clock::now() + std::chrono::seconds(3))
            == std::cv_status::no_timeout);

    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string request = "GET /backpressured-async-chunks HTTP/1.1\r\nHost: localhost\r\n\r\n";
    asio::write(client, asio::buffer(request));

    const auto pending_write_status         = pending_write.wait_for(std::chrono::seconds(5));
    const auto connection_io_context_status = connection_io_context.wait_for(std::chrono::seconds(5));
    auto executor_barrier_promise           = std::make_shared<std::promise<void>>();
    auto executor_barrier                   = executor_barrier_promise->get_future();
    if (connection_io_context_status == std::future_status::ready) {
        auto* executor = connection_io_context.get();
        asio::post(*executor, [executor, executor_barrier_promise] {
            asio::post(*executor, [executor_barrier_promise] { executor_barrier_promise->set_value(); });
        });
    }
    const auto executor_barrier_status                       = executor_barrier.wait_for(std::chrono::seconds(5));
    const std::size_t provider_calls_before_write_completion = provider_calls->load();

    socket_context.resume_pending_write();

    const auto has_chunk_terminator = [](const std::string& response) {
        return response.size() >= 5 && response.compare(response.size() - 5, 5, "0\r\n\r\n") == 0;
    };
    std::string response;
    const bool response_complete
        = receive_with_deadline(client, response, std::chrono::seconds(5), has_chunk_terminator);

    const auto second_call_after_prior_write = second_provider_call.wait_for(std::chrono::seconds(5));
    asio_error_code close_error;
    client.close(close_error);
    server_shutdown.shutdown();

    REQUIRE(pending_write_status == std::future_status::ready);
    REQUIRE(connection_io_context_status == std::future_status::ready);
    REQUIRE(executor_barrier_status == std::future_status::ready);
    CHECK(provider_calls_before_write_completion == 1);
    REQUIRE(response_complete);
    REQUIRE(second_call_after_prior_write == std::future_status::ready);
    CHECK(provider_calls->load() == 2);
    CHECK(active_provider_calls->load() == 0);
    CHECK(maximum_active_provider_calls->load() == 1);

    const auto header_end = response.find("\r\n\r\n");
    REQUIRE(header_end != std::string::npos);
    const std::string encoded_body = response.substr(header_end + 4);
    REQUIRE(encoded_body.size() == std::string("5\r\nfirst\r\n0\r\n\r\n").size());
    CHECK(encoded_body == "5\r\nfirst\r\n0\r\n\r\n");
} // async_chunked_response_backpressures_provider_until_prior_write_completes


TEST_CASE("async_chunked_response_abort_closes_without_terminator_and_completes_once_unclean") {
    SimpleApp app;

    auto completion_observation = std::make_shared<ChunkCompletionObservation>();
    auto completion_result      = completion_observation->first_result();

    CROW_ROUTE(app, "/aborted-async-chunks")
    ([completion_observation](const crow::request&, crow::response& res) {
        res.set_async_chunked_content_provider([](crow::response::async_chunk_completion_t complete) {
            complete(crow::chunk_result::abort, "discarded");
        });
        res.set_chunked_completion_handler(
            [completion_observation](bool clean) { completion_observation->record(clean); });
        res.end();
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] { app.stop(); });
    REQUIRE(app.wait_for_server_start() == std::cv_status::no_timeout);

    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string request = "GET /aborted-async-chunks HTTP/1.1\r\nHost: localhost\r\n\r\n";
    asio::write(client, asio::buffer(request));

    std::string response;
    const bool connection_closed = receive_until_closed_with_deadline(client, response, std::chrono::seconds(5));
    const auto completion_status = completion_result.wait_for(std::chrono::seconds(1));
    const bool clean             = completion_status == std::future_status::ready ? completion_result.get() : true;

    asio_error_code close_error;
    client.close(close_error);
    server_shutdown.shutdown();

    REQUIRE(connection_closed);
    const auto header_end = response.find("\r\n\r\n");
    REQUIRE(header_end != std::string::npos);
    CHECK(response.substr(header_end + 4).find("0\r\n\r\n") == std::string::npos);
    CHECK(response.find("discarded") == std::string::npos);
    REQUIRE(completion_status == std::future_status::ready);
    CHECK(clean == false);
    CHECK(completion_observation->calls() == 1);
} // async_chunked_response_abort_closes_without_terminator_and_completes_once_unclean


TEST_CASE("async_chunked_response_recovers_abort_after_foreign_thread_publication_failure") {
    SimpleApp app;

    auto provider_calls            = std::make_shared<std::atomic<std::size_t>>(0);
    auto publication_attempts      = std::make_shared<std::atomic<std::size_t>>(0);
    auto transfer_lifetime_promise = std::make_shared<std::promise<std::weak_ptr<int>>>();
    auto transfer_lifetime_result  = transfer_lifetime_promise->get_future();
    auto worker_task_promise = std::make_shared<std::promise<std::future<bool>>>();
    auto worker_task_result        = worker_task_promise->get_future();
    auto connection_thread_promise = std::make_shared<std::promise<std::thread::id>>();
    auto connection_thread_result  = connection_thread_promise->get_future();
    auto provider_thread_promise   = std::make_shared<std::promise<std::thread::id>>();
    auto provider_thread_result    = provider_thread_promise->get_future();
    auto completion_observation    = std::make_shared<ChunkCompletionObservation>();
    auto completion_result         = completion_observation->first_result();
    auto completion_thread         = completion_observation->first_thread();
    PausingSocketContext socket_context;
    auto connection_destroyed = socket_context.started_connection_destroyed_future();
    ScopedAsyncChunkPublicationTestHook publication_hook([publication_attempts] {
        if (publication_attempts->fetch_add(1) == 0)
            throw std::runtime_error("forced asynchronous chunk publication failure");
    });

    CROW_ROUTE(app, "/async-completion-publication-failure")
    ([provider_calls,
      transfer_lifetime_promise,
      worker_task_promise,
      connection_thread_promise,
      provider_thread_promise,
      completion_observation](const crow::request&, crow::response& res) {
        connection_thread_promise->set_value(std::this_thread::get_id());
        auto transfer_lifetime = std::make_shared<int>(0);
        transfer_lifetime_promise->set_value(std::weak_ptr<int>(transfer_lifetime));
        res.set_async_chunked_content_provider(
            [provider_calls, transfer_lifetime, worker_task_promise, provider_thread_promise](
                crow::response::async_chunk_completion_t complete) {
                static_cast<void>(transfer_lifetime);
                provider_calls->fetch_add(1);
                worker_task_promise->set_value(
                    std::async(std::launch::async, [provider_thread_promise, complete = std::move(complete)]() mutable {
                        provider_thread_promise->set_value(std::this_thread::get_id());
                        return complete(crow::chunk_result::done, "discarded");
                    }));
            },
            "text/plain");
        res.set_chunked_completion_handler([completion_observation, transfer_lifetime](bool clean) {
            static_cast<void>(transfer_lifetime);
            completion_observation->record(clean);
        });
        res.end();
    });

    app.validate();
    std::tuple<> middlewares;
    using PublicationFailureServer = crow::Server<crow::SimpleApp, crow::TCPAcceptor, PausingSocketAdaptor>;
    PublicationFailureServer server(&app,
                                    asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451),
                                    "Crow/Test",
                                    &middlewares,
                                    2,
                                    5,
                                    &socket_context);
    auto server_task = std::async(std::launch::async, [&server] { server.run(); });
    BoundedServerShutdown server_shutdown(server_task, [&server] { server.stop(); });
    REQUIRE(server.wait_for_start(std::chrono::steady_clock::now() + std::chrono::seconds(3))
            == std::cv_status::no_timeout);

    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string request = "GET /async-completion-publication-failure HTTP/1.1\r\nHost: localhost\r\n\r\n";
    asio::write(client, asio::buffer(request));

    std::string response;
    const bool connection_closed  = receive_until_closed_with_deadline(client, response, std::chrono::seconds(5));
    const auto worker_task_status = worker_task_result.wait_for(std::chrono::seconds(1));
    std::future<bool> worker_task;
    if (worker_task_status == std::future_status::ready)
        worker_task = worker_task_result.get();
    const auto worker_completion_status
        = worker_task.valid() ? worker_task.wait_for(std::chrono::seconds(1)) : std::future_status::deferred;
    std::exception_ptr worker_exception;
    bool publication_accepted = true;
    if (worker_completion_status == std::future_status::ready) {
        try {
            publication_accepted = worker_task.get();
        } catch (...) {
            worker_exception = std::current_exception();
        }
    }
    const auto completion_status = completion_result.wait_for(std::chrono::seconds(1));
    const bool clean             = completion_status == std::future_status::ready ? completion_result.get() : true;
    const auto connection_thread_status = connection_thread_result.wait_for(std::chrono::seconds(1));
    const auto connection_thread
        = connection_thread_status == std::future_status::ready ? connection_thread_result.get() : std::thread::id{};
    const auto provider_thread_status = provider_thread_result.wait_for(std::chrono::seconds(1));
    const auto provider_thread
        = provider_thread_status == std::future_status::ready ? provider_thread_result.get() : std::thread::id{};
    const auto completion_thread_status = completion_thread.wait_for(std::chrono::seconds(1));
    const auto completion_thread_id
        = completion_thread_status == std::future_status::ready ? completion_thread.get() : std::thread::id{};
    const auto transfer_lifetime_status = transfer_lifetime_result.wait_for(std::chrono::seconds(1));
    std::weak_ptr<int> transfer_lifetime;
    if (transfer_lifetime_status == std::future_status::ready)
        transfer_lifetime = transfer_lifetime_result.get();
    const auto connection_destroyed_status = connection_destroyed.wait_for(std::chrono::seconds(1));

    asio_error_code close_error;
    client.close(close_error);
    server_shutdown.shutdown();

    REQUIRE(connection_closed);
    const auto header_end = response.find("\r\n\r\n");
    REQUIRE(header_end != std::string::npos);
    CHECK(response.substr(header_end + 4).find("discarded") == std::string::npos);
    CHECK(response.substr(header_end + 4).find("0\r\n\r\n") == std::string::npos);
    REQUIRE(completion_status == std::future_status::ready);
    CHECK(clean == false);
    CHECK(completion_observation->calls() == 1);
    CHECK(provider_calls->load() == 1);
    CHECK(publication_attempts->load() == 2);
    REQUIRE(worker_task_status == std::future_status::ready);
    REQUIRE(worker_completion_status == std::future_status::ready);
    CHECK(!worker_exception);
    CHECK_FALSE(publication_accepted);
    REQUIRE(connection_thread_status == std::future_status::ready);
    REQUIRE(provider_thread_status == std::future_status::ready);
    REQUIRE(completion_thread_status == std::future_status::ready);
    CHECK(provider_thread != connection_thread);
    CHECK(completion_thread_id == connection_thread);
    REQUIRE(transfer_lifetime_status == std::future_status::ready);
    CHECK(transfer_lifetime.expired());
    CHECK(connection_destroyed_status == std::future_status::ready);
} // async_chunked_response_recovers_abort_after_foreign_thread_publication_failure


TEST_CASE("async_chunked_response_contains_abort_publication_failure_until_shutdown") {
    SimpleApp app;

    auto provider_calls            = std::make_shared<std::atomic<std::size_t>>(0);
    auto publication_attempts      = std::make_shared<std::atomic<std::size_t>>(0);
    auto transfer_lifetime_promise = std::make_shared<std::promise<std::weak_ptr<int>>>();
    auto transfer_lifetime_result  = transfer_lifetime_promise->get_future();
    auto worker_task_promise = std::make_shared<std::promise<std::future<bool>>>();
    auto worker_task_result        = worker_task_promise->get_future();
    auto completion_observation    = std::make_shared<ChunkCompletionObservation>();
    auto completion_result         = completion_observation->first_result();
    PausingSocketContext socket_context;
    ScopedAsyncChunkPublicationTestHook publication_hook([publication_attempts] {
        publication_attempts->fetch_add(1);
        throw std::runtime_error("forced asynchronous chunk publication failure");
    });

    CROW_ROUTE(app, "/async-abort-publication-failure")
    ([provider_calls, transfer_lifetime_promise, worker_task_promise, completion_observation](const crow::request&,
                                                                                              crow::response& res) {
        auto transfer_lifetime = std::make_shared<int>(0);
        transfer_lifetime_promise->set_value(std::weak_ptr<int>(transfer_lifetime));
        res.set_async_chunked_content_provider(
            [provider_calls, transfer_lifetime, worker_task_promise](
                crow::response::async_chunk_completion_t complete) {
                static_cast<void>(transfer_lifetime);
                provider_calls->fetch_add(1);
                worker_task_promise->set_value(
                  std::async(std::launch::async, [complete = std::move(complete)]() mutable {
                      return complete(crow::chunk_result::done, "discarded");
                  }));
            },
            "text/plain");
        res.set_chunked_completion_handler([completion_observation, transfer_lifetime](bool clean) {
            static_cast<void>(transfer_lifetime);
            completion_observation->record(clean);
        });
        res.end();
    });

    app.validate();
    std::tuple<> middlewares;
    using PublicationFailureServer = crow::Server<crow::SimpleApp, crow::TCPAcceptor, PausingSocketAdaptor>;
    PublicationFailureServer server(&app,
                                    asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451),
                                    "Crow/Test",
                                    &middlewares,
                                    2,
                                    5,
                                    &socket_context);
    auto server_task = std::async(std::launch::async, [&server] { server.run(); });
    BoundedServerShutdown server_shutdown(server_task, [&server] { server.stop(); });
    REQUIRE(server.wait_for_start(std::chrono::steady_clock::now() + std::chrono::seconds(3))
            == std::cv_status::no_timeout);

    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string request = "GET /async-abort-publication-failure HTTP/1.1\r\nHost: localhost\r\n\r\n";
    asio::write(client, asio::buffer(request));

    const auto worker_task_status = worker_task_result.wait_for(std::chrono::seconds(5));
    std::future<bool> worker_task;
    if (worker_task_status == std::future_status::ready)
        worker_task = worker_task_result.get();
    const auto worker_completion_status
        = worker_task.valid() ? worker_task.wait_for(std::chrono::seconds(1)) : std::future_status::deferred;
    std::exception_ptr worker_exception;
    bool publication_accepted = true;
    if (worker_completion_status == std::future_status::ready) {
        try {
            publication_accepted = worker_task.get();
        } catch (...) {
            worker_exception = std::current_exception();
        }
    }
    const auto completion_status_before_shutdown = completion_result.wait_for(std::chrono::milliseconds(100));
    const auto transfer_lifetime_status          = transfer_lifetime_result.wait_for(std::chrono::seconds(1));
    std::weak_ptr<int> transfer_lifetime;
    if (transfer_lifetime_status == std::future_status::ready)
        transfer_lifetime = transfer_lifetime_result.get();
    const bool retained_before_shutdown = !transfer_lifetime.expired();

    server_shutdown.shutdown();

    std::string response;
    const bool connection_closed = receive_until_closed_with_deadline(client, response, std::chrono::seconds(1));
    const auto completion_status = completion_result.wait_for(std::chrono::seconds(1));
    const bool clean             = completion_status == std::future_status::ready ? completion_result.get() : true;

    asio_error_code close_error;
    client.close(close_error);

    REQUIRE(worker_task_status == std::future_status::ready);
    REQUIRE(worker_completion_status == std::future_status::ready);
    CHECK(!worker_exception);
    CHECK_FALSE(publication_accepted);
    CHECK(publication_attempts->load() == 2);
    CHECK(completion_status_before_shutdown == std::future_status::timeout);
    REQUIRE(transfer_lifetime_status == std::future_status::ready);
    CHECK(retained_before_shutdown);
    REQUIRE(connection_closed);
    const auto header_end = response.find("\r\n\r\n");
    REQUIRE(header_end != std::string::npos);
    CHECK(response.substr(header_end + 4).find("discarded") == std::string::npos);
    CHECK(response.substr(header_end + 4).find("0\r\n\r\n") == std::string::npos);
    REQUIRE(completion_status == std::future_status::ready);
    CHECK(clean == false);
    CHECK(completion_observation->calls() == 1);
    CHECK(provider_calls->load() == 1);
    CHECK(transfer_lifetime.expired());
} // async_chunked_response_contains_abort_publication_failure_until_shutdown


TEST_CASE("async_chunked_response_ignores_duplicate_request_completion") {
    SimpleApp app;

    auto provider_calls         = std::make_shared<std::atomic<std::size_t>>(0);
    auto first_result_accepted = std::make_shared<std::atomic<int>>(-1);
    auto duplicate_accepted = std::make_shared<std::atomic<int>>(-1);
    auto final_result_accepted = std::make_shared<std::atomic<int>>(-1);
    auto completion_observation = std::make_shared<ChunkCompletionObservation>();
    auto completion_result      = completion_observation->first_result();

    CROW_ROUTE(app, "/duplicate-async-completion")
    ([provider_calls,
      first_result_accepted,
      duplicate_accepted,
      final_result_accepted,
      completion_observation](const crow::request&, crow::response& res) {
        res.set_async_chunked_content_provider(
          [provider_calls, first_result_accepted, duplicate_accepted, final_result_accepted](
            crow::response::async_chunk_completion_t complete) {
              const std::size_t call = provider_calls->fetch_add(1);
              if (call == 0)
              {
                  first_result_accepted->store(complete(crow::chunk_result::more, ""));
                  duplicate_accepted->store(complete(crow::chunk_result::done, "duplicate"));
              }
              else if (call == 1)
              {
                  final_result_accepted->store(complete(crow::chunk_result::done, "final"));
              }
              else
              {
                  complete(crow::chunk_result::abort, "");
              }
          },
          "text/plain");
        res.set_chunked_completion_handler(
            [completion_observation](bool clean) { completion_observation->record(clean); });
        res.end();
    });

    auto server_task = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    BoundedServerShutdown server_shutdown(server_task, [&app] { app.stop(); });
    REQUIRE(app.wait_for_server_start() == std::cv_status::no_timeout);

    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string request = "GET /duplicate-async-completion HTTP/1.1\r\nHost: localhost\r\n\r\n";
    asio::write(client, asio::buffer(request));

    const auto has_chunk_terminator = [](const std::string& response) {
        return response.size() >= 5 && response.compare(response.size() - 5, 5, "0\r\n\r\n") == 0;
    };
    std::string response;
    const bool response_complete
        = receive_with_deadline(client, response, std::chrono::seconds(5), has_chunk_terminator);
    const auto completion_status = completion_result.wait_for(std::chrono::seconds(1));
    const bool clean             = completion_status == std::future_status::ready ? completion_result.get() : false;

    asio_error_code close_error;
    client.close(close_error);
    server_shutdown.shutdown();

    REQUIRE(response_complete);
    const auto header_end = response.find("\r\n\r\n");
    REQUIRE(header_end != std::string::npos);
    CHECK(response.substr(header_end + 4) == "5\r\nfinal\r\n0\r\n\r\n");
    CHECK(provider_calls->load() == 2);
    CHECK(first_result_accepted->load() == 1);
    CHECK(duplicate_accepted->load() == 0);
    CHECK(final_result_accepted->load() == 1);
    REQUIRE(completion_status == std::future_status::ready);
    CHECK(clean == true);
    CHECK(completion_observation->calls() == 1);
} // async_chunked_response_ignores_duplicate_request_completion


TEST_CASE("async_chunked_response_discards_late_completion_after_peer_close") {
    SimpleApp app;

    auto provider_started_promise  = std::make_shared<std::promise<void>>();
    auto provider_started          = provider_started_promise->get_future();
    auto transfer_lifetime_promise = std::make_shared<std::promise<std::weak_ptr<int>>>();
    auto transfer_lifetime_result  = transfer_lifetime_promise->get_future();
    auto delayed_completion        = std::make_shared<crow::response::async_chunk_completion_t>();
    auto delayed_completion_mutex  = std::make_shared<std::mutex>();
    auto completion_observation    = std::make_shared<ChunkCompletionObservation>();
    auto completion_result         = completion_observation->first_result();
    PausingSocketContext socket_context;
    auto connection_destroyed = socket_context.started_connection_destroyed_future();
    auto waiting_read_started = socket_context.observed_read_started_future();

    CROW_ROUTE(app, "/late-async-completion")
    ([provider_started_promise,
      transfer_lifetime_promise,
      delayed_completion,
      delayed_completion_mutex,
      completion_observation,
      &socket_context](const crow::request&, crow::response& res) {
        auto transfer_lifetime = std::make_shared<int>(0);
        transfer_lifetime_promise->set_value(std::weak_ptr<int>(transfer_lifetime));
        res.set_async_chunked_content_provider(
          [provider_started_promise,
           delayed_completion,
           delayed_completion_mutex,
           transfer_lifetime,
           &socket_context](crow::response::async_chunk_completion_t complete) {
              static_cast<void>(transfer_lifetime);
              socket_context.observe_next_read();
              {
                  std::lock_guard<std::mutex> lock(*delayed_completion_mutex);
                  *delayed_completion = std::move(complete);
              }
              provider_started_promise->set_value();
          });
        res.set_chunked_completion_handler([completion_observation](bool clean) {
            completion_observation->record(clean);
        });
        res.end();
    });

    app.validate();
    std::tuple<> middlewares;
    using LifetimeServer = crow::Server<crow::SimpleApp, crow::TCPAcceptor, PausingSocketAdaptor>;
    LifetimeServer server(&app,
                          asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451),
                          "Crow/Test",
                          &middlewares,
                          2,
                          5,
                          &socket_context);
    auto server_task = std::async(std::launch::async, [&server] { server.run(); });
    BoundedServerShutdown server_shutdown(server_task, [&server] { server.stop(); });
    REQUIRE(server.wait_for_start(std::chrono::steady_clock::now() + std::chrono::seconds(3))
            == std::cv_status::no_timeout);

    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string request = "GET /late-async-completion HTTP/1.1\r\nHost: localhost\r\n\r\n";
    asio::write(client, asio::buffer(request));

    const auto provider_status = provider_started.wait_for(std::chrono::seconds(5));
    std::weak_ptr<int> transfer_lifetime;
    if (provider_status == std::future_status::ready)
        transfer_lifetime = transfer_lifetime_result.get();
    const auto read_start_status = waiting_read_started.wait_for(std::chrono::seconds(1));
    const bool retained_before_peer_close                = !transfer_lifetime.expired();
    const std::size_t completion_calls_before_peer_close = completion_observation->calls();
    const auto connection_status_before_peer_close       = connection_destroyed.wait_for(std::chrono::seconds(0));

    asio_error_code close_error;
    client.shutdown(asio::socket_base::shutdown_both, close_error);
    client.close(close_error);

    const auto connection_destroyed_status = connection_destroyed.wait_for(std::chrono::seconds(1));
    const auto completion_status           = completion_result.wait_for(std::chrono::seconds(1));
    const bool clean = completion_status == std::future_status::ready ? completion_result.get() : true;
    const bool released_before_late_completion = transfer_lifetime.expired();

    crow::response::async_chunk_completion_t complete;
    {
        std::lock_guard<std::mutex> lock(*delayed_completion_mutex);
        complete = std::move(*delayed_completion);
    }
    bool late_result_accepted = true;
    bool source_continued = false;
    if (complete)
    {
        late_result_accepted = complete(crow::chunk_result::more, "late");
        if (late_result_accepted)
            source_continued = true;
    }

    server_shutdown.shutdown();

    REQUIRE(provider_status == std::future_status::ready);
    REQUIRE(read_start_status == std::future_status::ready);
    CHECK(retained_before_peer_close);
    CHECK(completion_calls_before_peer_close == 0);
    CHECK(connection_status_before_peer_close == std::future_status::timeout);
    REQUIRE(connection_destroyed_status == std::future_status::ready);
    REQUIRE(completion_status == std::future_status::ready);
    CHECK(clean == false);
    CHECK(released_before_late_completion);
    CHECK(transfer_lifetime.expired());
    CHECK(completion_observation->calls() == 1);
    CHECK_FALSE(late_result_accepted);
    CHECK_FALSE(source_continued);
} // async_chunked_response_discards_late_completion_after_peer_close


TEST_CASE("async_chunked_response_shutdown_releases_never_completing_provider") {
    SimpleApp app;

    auto provider_started_promise  = std::make_shared<std::promise<void>>();
    auto provider_started          = provider_started_promise->get_future();
    auto transfer_lifetime_promise = std::make_shared<std::promise<std::weak_ptr<int>>>();
    auto transfer_lifetime_result  = transfer_lifetime_promise->get_future();
    auto completion_observation    = std::make_shared<ChunkCompletionObservation>();
    auto completion_result         = completion_observation->first_result();
    PausingSocketContext socket_context;

    CROW_ROUTE(app, "/never-completing-async-chunk")
    ([provider_started_promise, transfer_lifetime_promise, completion_observation](const crow::request&,
                                                                                   crow::response& res) {
        auto transfer_lifetime = std::make_shared<int>(0);
        transfer_lifetime_promise->set_value(std::weak_ptr<int>(transfer_lifetime));
        res.set_async_chunked_content_provider(
            [provider_started_promise, transfer_lifetime](crow::response::async_chunk_completion_t complete) {
                static_cast<void>(complete);
                static_cast<void>(transfer_lifetime);
                provider_started_promise->set_value();
            });
        res.set_chunked_completion_handler([completion_observation, transfer_lifetime](bool clean) {
            static_cast<void>(transfer_lifetime);
            completion_observation->record(clean);
        });
        res.end();
    });

    app.validate();
    std::tuple<> middlewares;
    using LifetimeServer = crow::Server<crow::SimpleApp, crow::TCPAcceptor, PausingSocketAdaptor>;
    LifetimeServer server(&app,
                          asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451),
                          "Crow/Test",
                          &middlewares,
                          2,
                          5,
                          &socket_context);
    auto server_task = std::async(std::launch::async, [&server] { server.run(); });
    BoundedServerShutdown server_shutdown(server_task, [&server] { server.stop(); });
    REQUIRE(server.wait_for_start(std::chrono::steady_clock::now() + std::chrono::seconds(3))
            == std::cv_status::no_timeout);

    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string request = "GET /never-completing-async-chunk HTTP/1.1\r\nHost: localhost\r\n\r\n";
    asio::write(client, asio::buffer(request));

    const auto provider_status = provider_started.wait_for(std::chrono::seconds(5));
    std::weak_ptr<int> transfer_lifetime;
    if (provider_status == std::future_status::ready)
        transfer_lifetime = transfer_lifetime_result.get();
    const bool retained_before_shutdown                = !transfer_lifetime.expired();
    const std::size_t completion_calls_before_shutdown = completion_observation->calls();

    server_shutdown.shutdown();
    asio_error_code close_error;
    client.close(close_error);

    const auto completion_status = completion_result.wait_for(std::chrono::seconds(1));
    const bool clean             = completion_status == std::future_status::ready ? completion_result.get() : true;

    REQUIRE(provider_status == std::future_status::ready);
    CHECK(retained_before_shutdown);
    CHECK(completion_calls_before_shutdown == 0);
    REQUIRE(completion_status == std::future_status::ready);
    CHECK(clean == false);
    CHECK(transfer_lifetime.expired());
    CHECK(completion_observation->calls() == 1);
} // async_chunked_response_shutdown_releases_never_completing_provider


TEST_CASE("async_chunked_response_shutdown_discards_queued_completion") {
    SimpleApp app;

    auto provider_stopped_context_promise = std::make_shared<std::promise<void>>();
    auto provider_stopped_context         = provider_stopped_context_promise->get_future();
    auto transfer_lifetime_promise        = std::make_shared<std::promise<std::weak_ptr<int>>>();
    auto transfer_lifetime_result         = transfer_lifetime_promise->get_future();
    auto completion_observation           = std::make_shared<ChunkCompletionObservation>();
    auto completion_result                = completion_observation->first_result();
    PausingSocketContext socket_context;

    CROW_ROUTE(app, "/queued-async-completion")
    ([provider_stopped_context_promise, transfer_lifetime_promise, completion_observation](const crow::request& req,
                                                                                           crow::response& res) {
        auto transfer_lifetime = std::make_shared<int>(0);
        transfer_lifetime_promise->set_value(std::weak_ptr<int>(transfer_lifetime));
        auto* connection_io_context = req.io_context;
        res.set_async_chunked_content_provider(
            [provider_stopped_context_promise, transfer_lifetime, connection_io_context](
                crow::response::async_chunk_completion_t complete) {
                static_cast<void>(transfer_lifetime);
                complete(crow::chunk_result::done, "queued");
                connection_io_context->stop();
                provider_stopped_context_promise->set_value();
            });
        res.set_chunked_completion_handler([completion_observation, transfer_lifetime](bool clean) {
            static_cast<void>(transfer_lifetime);
            completion_observation->record(clean);
        });
        res.end();
    });

    app.validate();
    std::tuple<> middlewares;
    using LifetimeServer = crow::Server<crow::SimpleApp, crow::TCPAcceptor, PausingSocketAdaptor>;
    LifetimeServer server(&app,
                          asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451),
                          "Crow/Test",
                          &middlewares,
                          2,
                          5,
                          &socket_context);
    auto server_task = std::async(std::launch::async, [&server] { server.run(); });
    BoundedServerShutdown server_shutdown(server_task, [&server] { server.stop(); });
    REQUIRE(server.wait_for_start(std::chrono::steady_clock::now() + std::chrono::seconds(3))
            == std::cv_status::no_timeout);

    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string request = "GET /queued-async-completion HTTP/1.1\r\nHost: localhost\r\n\r\n";
    asio::write(client, asio::buffer(request));

    const auto provider_status = provider_stopped_context.wait_for(std::chrono::seconds(5));
    std::weak_ptr<int> transfer_lifetime;
    if (provider_status == std::future_status::ready)
        transfer_lifetime = transfer_lifetime_result.get();

    server_shutdown.shutdown();

    std::string response;
    const bool connection_closed = receive_until_closed_with_deadline(client, response, std::chrono::seconds(1));
    const auto completion_status = completion_result.wait_for(std::chrono::seconds(1));
    const bool clean             = completion_status == std::future_status::ready ? completion_result.get() : true;

    asio_error_code close_error;
    client.close(close_error);

    REQUIRE(provider_status == std::future_status::ready);
    REQUIRE(connection_closed);
    CHECK(response.find("queued") == std::string::npos);
    CHECK(response.find("0\r\n\r\n") == std::string::npos);
    REQUIRE(completion_status == std::future_status::ready);
    CHECK(clean == false);
    CHECK(transfer_lifetime.expired());
    CHECK(completion_observation->calls() == 1);
} // async_chunked_response_shutdown_discards_queued_completion


TEST_CASE("async_chunked_response_server_stop_finishes_paused_write_on_worker") {
    SimpleApp app;

    auto worker_thread_promise  = std::make_shared<std::promise<std::thread::id>>();
    auto worker_thread_result   = worker_thread_promise->get_future();
    auto completion_observation = std::make_shared<ChunkCompletionObservation>();
    auto completion_result      = completion_observation->first_result();
    auto completion_thread      = completion_observation->first_thread();
    PausingSocketContext socket_context;
    auto pending_write = socket_context.pending_write_future();

    CROW_ROUTE(app, "/paused-async-chunk-during-stop")
    ([worker_thread_promise, completion_observation, &socket_context](const crow::request&, crow::response& res) {
        res.set_async_chunked_content_provider(
            [worker_thread_promise, &socket_context](crow::response::async_chunk_completion_t complete) {
                worker_thread_promise->set_value(std::this_thread::get_id());
                socket_context.pause_next_write();
                complete(crow::chunk_result::done, "paused");
            });
        res.set_chunked_completion_handler(
            [completion_observation](bool clean) { completion_observation->record(clean); });
        res.end();
    });

    app.validate();
    std::tuple<> middlewares;
    using PausedWriteServer = crow::Server<crow::SimpleApp, crow::TCPAcceptor, PausingSocketAdaptor>;
    PausedWriteServer server(&app,
                             asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451),
                             "Crow/Test",
                             &middlewares,
                             2,
                             5,
                             &socket_context);
    auto server_task = std::async(std::launch::async, [&server] { server.run(); });
    BoundedServerShutdown server_shutdown(server_task, [&server] { server.stop(); });
    REQUIRE(server.wait_for_start(std::chrono::steady_clock::now() + std::chrono::seconds(3))
            == std::cv_status::no_timeout);

    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string request = "GET /paused-async-chunk-during-stop HTTP/1.1\r\nHost: localhost\r\n\r\n";
    asio::write(client, asio::buffer(request));

    const auto pending_write_status = pending_write.wait_for(std::chrono::seconds(5));
    const auto worker_thread_status = worker_thread_result.wait_for(std::chrono::seconds(1));
    const auto worker_thread
        = worker_thread_status == std::future_status::ready ? worker_thread_result.get() : std::thread::id{};

    server_shutdown.shutdown();

    std::string response;
    const bool connection_closed = receive_until_closed_with_deadline(client, response, std::chrono::seconds(1));
    const auto completion_status = completion_result.wait_for(std::chrono::seconds(1));
    const bool clean             = completion_status == std::future_status::ready ? completion_result.get() : true;
    const auto completion_thread_status = completion_thread.wait_for(std::chrono::seconds(1));
    const auto observed_completion_thread
        = completion_thread_status == std::future_status::ready ? completion_thread.get() : std::thread::id{};

    socket_context.discard_pending_write();
    asio_error_code close_error;
    client.close(close_error);

    REQUIRE(pending_write_status == std::future_status::ready);
    REQUIRE(worker_thread_status == std::future_status::ready);
    REQUIRE(connection_closed);
    REQUIRE(completion_status == std::future_status::ready);
    REQUIRE(completion_thread_status == std::future_status::ready);
    CHECK(clean == false);
    CHECK(observed_completion_thread == worker_thread);
    CHECK(completion_observation->calls() == 1);
    const auto header_end = response.find("\r\n\r\n");
    REQUIRE(header_end != std::string::npos);
    CHECK(response.substr(header_end + 4).find("0\r\n\r\n") == std::string::npos);
} // async_chunked_response_server_stop_finishes_paused_write_on_worker


TEST_CASE("async_chunked_response_header_write_error_completes_once_unclean")
{
    SimpleApp app;

    auto provider_calls = std::make_shared<std::atomic<std::size_t>>(0);
    auto completion_observation = std::make_shared<ChunkCompletionObservation>();
    auto completion_result = completion_observation->first_result();
    PausingSocketContext socket_context;
    auto connection_destroyed = socket_context.started_connection_destroyed_future();

    CROW_ROUTE(app, "/failing-async-chunk-header")
    ([provider_calls, completion_observation, &socket_context](const crow::request&, crow::response& res) {
        socket_context.fail_next_write();
        res.set_async_chunked_content_provider(
          [provider_calls](crow::response::async_chunk_completion_t complete) {
              provider_calls->fetch_add(1);
              complete(crow::chunk_result::done, "unwritten");
          });
        res.set_chunked_completion_handler(
          [completion_observation](bool clean) {
              completion_observation->record(clean);
          });
        res.end();
    });

    app.validate();
    std::tuple<> middlewares;
    using HeaderWriteErrorServer = crow::Server<crow::SimpleApp, crow::TCPAcceptor, PausingSocketAdaptor>;
    HeaderWriteErrorServer server(&app,
                                  asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451),
                                  "Crow/Test",
                                  &middlewares,
                                  2,
                                  5,
                                  &socket_context);
    auto server_task = std::async(std::launch::async, [&server] {
        server.run();
    });
    BoundedServerShutdown server_shutdown(server_task, [&server] {
        server.stop();
    });
    REQUIRE(server.wait_for_start(std::chrono::steady_clock::now() + std::chrono::seconds(3)) == std::cv_status::no_timeout);

    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string request = "GET /failing-async-chunk-header HTTP/1.1\r\nHost: localhost\r\n\r\n";
    asio::write(client, asio::buffer(request));

    std::string response;
    const bool connection_closed = receive_until_closed_with_deadline(client, response, std::chrono::seconds(5));
    const auto completion_status = completion_result.wait_for(std::chrono::seconds(1));
    const bool clean = completion_status == std::future_status::ready ? completion_result.get() : true;

    asio_error_code close_error;
    client.close(close_error);

    // A failed header write must not stay alive on an armed write deadline.
    CHECK(connection_destroyed.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    server_shutdown.shutdown();

    REQUIRE(connection_closed);
    CHECK(response.find("0\r\n\r\n") == std::string::npos);
    CHECK(provider_calls->load() == 0);
    REQUIRE(completion_status == std::future_status::ready);
    CHECK(clean == false);
    CHECK(completion_observation->calls() == 1);
} // async_chunked_response_header_write_error_completes_once_unclean


TEST_CASE("skipped_chunk_provider_header_write_error_completes_once_unclean")
{
    SimpleApp app;
    auto provider_calls = std::make_shared<std::atomic<std::size_t>>(0);
    auto completion_observation = std::make_shared<ChunkCompletionObservation>();
    auto completion_result = completion_observation->first_result();
    PausingSocketContext socket_context;

    CROW_ROUTE(app, "/failing-bodyless-header")
    ([provider_calls, completion_observation, &socket_context](const crow::request&, crow::response& res) {
        socket_context.fail_next_write();
        res.code = 204;
        res.set_async_chunked_content_provider(
          [provider_calls](crow::response::async_chunk_completion_t) {
              provider_calls->fetch_add(1);
          });
        res.set_chunked_completion_handler(
          [completion_observation](bool clean) {
              completion_observation->record(clean);
          });
        res.end();
    });

    app.validate();
    std::tuple<> middlewares;
    using HeaderWriteErrorServer = crow::Server<crow::SimpleApp, crow::TCPAcceptor, PausingSocketAdaptor>;
    HeaderWriteErrorServer server(&app,
                                  asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451),
                                  "Crow/Test",
                                  &middlewares,
                                  2,
                                  5,
                                  &socket_context);
    auto server_task = std::async(std::launch::async, [&server] {
        server.run();
    });
    BoundedServerShutdown server_shutdown(server_task, [&server] {
        server.stop();
    });
    REQUIRE(server.wait_for_start(std::chrono::steady_clock::now() + std::chrono::seconds(3)) == std::cv_status::no_timeout);

    HttpClient client(LOCALHOST_ADDRESS, 45451);
    client.send("GET /failing-bodyless-header HTTP/1.1\r\nHost: localhost\r\n\r\n");
    std::string response;
    REQUIRE(receive_until_closed_with_deadline(client.socket(), response, std::chrono::seconds(5)));
    REQUIRE(completion_result.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    CHECK(completion_result.get() == false);
    CHECK(completion_observation->calls() == 1);
    CHECK(provider_calls->load() == 0);
    server_shutdown.shutdown();
} // skipped_chunk_provider_header_write_error_completes_once_unclean


TEST_CASE("async_chunked_response_terminator_write_error_completes_once_unclean")
{
    SimpleApp app;

    auto provider_calls = std::make_shared<std::atomic<std::size_t>>(0);
    auto completion_observation = std::make_shared<ChunkCompletionObservation>();
    auto completion_result = completion_observation->first_result();
    PausingSocketContext socket_context;

    CROW_ROUTE(app, "/failing-async-chunk-terminator")
    ([provider_calls, completion_observation, &socket_context](const crow::request&, crow::response& res) {
        res.set_async_chunked_content_provider(
          [provider_calls, &socket_context](crow::response::async_chunk_completion_t complete) {
              const auto call = provider_calls->fetch_add(1);
              if (call == 0)
              {
                  complete(crow::chunk_result::more, "written");
                  return;
              }

              socket_context.fail_next_write();
              complete(crow::chunk_result::done, "");
          });
        res.set_chunked_completion_handler(
          [completion_observation](bool clean) {
              completion_observation->record(clean);
          });
        res.end();
    });

    app.validate();
    std::tuple<> middlewares;
    using TerminatorWriteErrorServer = crow::Server<crow::SimpleApp, crow::TCPAcceptor, PausingSocketAdaptor>;
    TerminatorWriteErrorServer server(&app,
                                      asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451),
                                      "Crow/Test",
                                      &middlewares,
                                      2,
                                      5,
                                      &socket_context);
    auto server_task = std::async(std::launch::async, [&server] {
        server.run();
    });
    BoundedServerShutdown server_shutdown(server_task, [&server] {
        server.stop();
    });
    REQUIRE(server.wait_for_start(std::chrono::steady_clock::now() + std::chrono::seconds(3)) == std::cv_status::no_timeout);

    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string request = "GET /failing-async-chunk-terminator HTTP/1.1\r\nHost: localhost\r\n\r\n";
    asio::write(client, asio::buffer(request));

    std::string response;
    const bool connection_closed = receive_until_closed_with_deadline(client, response, std::chrono::seconds(5));
    const auto completion_status = completion_result.wait_for(std::chrono::seconds(1));
    const bool clean = completion_status == std::future_status::ready ? completion_result.get() : true;

    asio_error_code close_error;
    client.close(close_error);
    server_shutdown.shutdown();

    REQUIRE(connection_closed);
    const auto header_end = response.find("\r\n\r\n");
    REQUIRE(header_end != std::string::npos);
    CHECK(response.substr(header_end + 4) == "7\r\nwritten\r\n");
    CHECK(response.find("0\r\n\r\n") == std::string::npos);
    CHECK(provider_calls->load() == 2);
    REQUIRE(completion_status == std::future_status::ready);
    CHECK(clean == false);
    CHECK(completion_observation->calls() == 1);
} // async_chunked_response_terminator_write_error_completes_once_unclean


TEST_CASE("async_chunked_response_write_error_completes_once_unclean") {
    SimpleApp app;

    auto provider_calls         = std::make_shared<std::atomic<std::size_t>>(0);
    auto completion_observation = std::make_shared<ChunkCompletionObservation>();
    auto completion_result      = completion_observation->first_result();
    PausingSocketContext socket_context;
    auto connection_destroyed = socket_context.started_connection_destroyed_future();

    CROW_ROUTE(app, "/failing-async-chunk-write")
    ([provider_calls, completion_observation, &socket_context](const crow::request&, crow::response& res) {
        res.set_async_chunked_content_provider(
            [provider_calls, &socket_context](crow::response::async_chunk_completion_t complete) {
                provider_calls->fetch_add(1);
                socket_context.fail_next_write();
                complete(crow::chunk_result::done, "unwritten");
            });
        res.set_chunked_completion_handler(
            [completion_observation](bool clean) { completion_observation->record(clean); });
        res.end();
    });

    app.validate();
    std::tuple<> middlewares;
    using WriteErrorServer = crow::Server<crow::SimpleApp, crow::TCPAcceptor, PausingSocketAdaptor>;
    WriteErrorServer server(&app,
                            asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451),
                            "Crow/Test",
                            &middlewares,
                            2,
                            5,
                            &socket_context);
    auto server_task = std::async(std::launch::async, [&server] { server.run(); });
    BoundedServerShutdown server_shutdown(server_task, [&server] { server.stop(); });
    REQUIRE(server.wait_for_start(std::chrono::steady_clock::now() + std::chrono::seconds(3))
            == std::cv_status::no_timeout);

    asio::io_context io_context;
    asio::ip::tcp::socket client(io_context);
    client.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
    const std::string request = "GET /failing-async-chunk-write HTTP/1.1\r\nHost: localhost\r\n\r\n";
    asio::write(client, asio::buffer(request));

    std::string response;
    const bool connection_closed = receive_until_closed_with_deadline(client, response, std::chrono::seconds(5));
    const auto completion_status = completion_result.wait_for(std::chrono::seconds(1));
    const bool clean             = completion_status == std::future_status::ready ? completion_result.get() : true;

    asio_error_code close_error;
    client.close(close_error);

    // A failed chunk write must not stay alive on an armed write deadline.
    CHECK(connection_destroyed.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    server_shutdown.shutdown();

    REQUIRE(connection_closed);
    const auto header_end = response.find("\r\n\r\n");
    REQUIRE(header_end != std::string::npos);
    CHECK(response.substr(header_end + 4).empty());
    CHECK(provider_calls->load() == 1);
    REQUIRE(completion_status == std::future_status::ready);
    CHECK(clean == false);
    CHECK(completion_observation->calls() == 1);
} // async_chunked_response_write_error_completes_once_unclean


TEST_CASE("chunked_response_no_data") {
    SimpleApp app;

    CROW_ROUTE(app, "/empty")
    ([](const crow::request&, crow::response& res) {
        int calls = 0;
        res.set_chunked_content_provider([calls](std::string& chunk) mutable -> bool {
            chunk.clear();
            return ++calls < 3; // three calls producing nothing at all
        });
        res.end();
    });

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    app.wait_for_server_start();

    HttpClient client(LOCALHOST_ADDRESS, 45451);
    client.send("GET /empty HTTP/1.1\r\nHost: localhost\r\n\r\n");

    std::string response;
    while (response.size() < 5 || response.compare(response.size() - 5, 5, "0\r\n\r\n") != 0)
        response += client.receive();

    CHECK(response.find("Transfer-Encoding: chunked") != std::string::npos);
    CHECK(response.find("Content-Length") == std::string::npos);

    app.stop();
} // chunked_response_no_data


TEST_CASE("chunked_response_large_body")
{
    SimpleApp app;

    const size_t chunk_count = 64;
    const size_t chunk_size = 1024;

    CROW_ROUTE(app, "/large")
    ([chunk_count, chunk_size](const crow::request&, crow::response& res) {
        size_t remaining = chunk_count;
        res.set_chunked_content_provider([remaining, chunk_size](std::string& chunk) mutable -> bool {
            if (remaining == 0)
                return false;
            chunk.assign(chunk_size, 'x');
            --remaining;
            return true;
        });
        res.end();
    });

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    app.wait_for_server_start();

    HttpClient client(LOCALHOST_ADDRESS, 45451);
    client.send("GET /large HTTP/1.1\r\nHost: localhost\r\n\r\n");

    std::string response;
    while (response.size() < 5 || response.compare(response.size() - 5, 5, "0\r\n\r\n") != 0)
        response += client.receive();

    CHECK(response.find("Transfer-Encoding: chunked") != std::string::npos);

    // decode the chunked body: every frame is "<size in hex>\r\n<data>\r\n",
    // the terminating frame has size zero
    auto header_end = response.find("\r\n\r\n");
    REQUIRE(header_end != std::string::npos);
    std::string chunked_body = response.substr(header_end + 4);
    size_t seen = 0;
    size_t total = 0;
    std::string::size_type pos = 0;
    while (true)
    {
        auto size_end = chunked_body.find("\r\n", pos);
        REQUIRE(size_end != std::string::npos);
        size_t size = std::stoul(chunked_body.substr(pos, size_end - pos), nullptr, 16);
        if (size == 0)
            break;
        ++seen;
        total += size;
        pos = size_end + 2 + size + 2; // past the size line, the data and its trailing CRLF
        REQUIRE(pos <= chunked_body.size());
    }
    CHECK(seen == chunk_count);
    CHECK(total == chunk_count * chunk_size);

    app.stop();
} // chunked_response_large_body


TEST_CASE("chunked_response_head_request")
{
    SimpleApp app;

    auto completion_clean = std::make_shared<std::promise<bool>>();

    CROW_ROUTE(app, "/chunks").methods("GET"_method, "HEAD"_method)([completion_clean](const crow::request&, crow::response& res) {
        res.set_chunked_content_provider([](std::string& chunk) -> bool {
            chunk = "body";
            return false;
        });
        res.set_chunked_completion_handler([completion_clean](bool clean) {
            completion_clean->set_value(clean);
        });
        res.end();
    });

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    app.wait_for_server_start();

    std::string response = HttpClient::request(LOCALHOST_ADDRESS, 45451, "HEAD /chunks HTTP/1.1\r\nHost: localhost\r\n\r\n");

    // Same header fields as a GET would produce: the body length is unknown, so
    // "Transfer-Encoding: chunked" is announced and "Content-Length" is absent.
    CHECK(response.find("Transfer-Encoding: chunked") != std::string::npos);
    CHECK(response.find("Content-Length") == std::string::npos);

    // The body itself is skipped entirely.
    auto header_end = response.find("\r\n\r\n");
    REQUIRE(header_end != std::string::npos);
    CHECK(response.substr(header_end + 4).empty());
    CHECK(response.find("body") == std::string::npos);

    // The provider is never called, but the completion handler still runs (with
    // clean == true): it stays the single release point for the source of the data.
    auto completion = completion_clean->get_future();
    REQUIRE(completion.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    CHECK(completion.get() == true);

    app.stop();
} // chunked_response_head_request


TEST_CASE("chunked_response_abort")
{
    SimpleApp app;

    CROW_ROUTE(app, "/abort")
    ([](const crow::request&, crow::response& res) {
        int calls = 0;
        res.set_chunked_content_provider(
          [calls](std::string& chunk) mutable -> crow::chunk_result {
              if (++calls < 3)
              {
                  chunk = "part" + std::to_string(calls);
                  return crow::chunk_result::more;
              }
              return crow::chunk_result::abort;
          },
          "text/plain");
        res.end();
    });

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    app.wait_for_server_start();

    HttpClient client(LOCALHOST_ADDRESS, 45451);
    client.send("GET /abort HTTP/1.1\r\nHost: localhost\r\n\r\n");

    // The server closes the connection without the terminating frame, so reading
    // past the truncated body eventually throws (end of file).
    std::string response;
    try
    {
        while (true)
            response += client.receive();
    }
    catch (const std::exception&)
    {
    }

    CHECK(response.find("Transfer-Encoding: chunked") != std::string::npos);

    // the body is truncated: the produced chunks are there, the terminating frame is not
    auto body_start = response.find("\r\n\r\n");
    REQUIRE(body_start != std::string::npos);
    std::string chunked_body = response.substr(body_start + 4);
    CHECK(chunked_body.find("5\r\npart1\r\n") != std::string::npos);
    CHECK(chunked_body.find("5\r\npart2\r\n") != std::string::npos);
    CHECK(chunked_body.find("0\r\n\r\n") == std::string::npos);

    app.stop();
} // chunked_response_abort


TEST_CASE("chunked_response_completion_handler")
{
    SimpleApp app;

    auto done_clean = std::make_shared<std::promise<bool>>();
    auto abort_clean = std::make_shared<std::promise<bool>>();

    CROW_ROUTE(app, "/done")
    ([done_clean](const crow::request&, crow::response& res) {
        res.set_chunked_content_provider([](std::string& chunk) {
            chunk = "body";
            return crow::chunk_result::done;
        });
        res.set_chunked_completion_handler([done_clean](bool clean) {
            done_clean->set_value(clean);
        });
        res.end();
    });

    CROW_ROUTE(app, "/abort")
    ([abort_clean](const crow::request&, crow::response& res) {
        res.set_chunked_content_provider([](std::string&) {
            return crow::chunk_result::abort;
        });
        res.set_chunked_completion_handler([abort_clean](bool clean) {
            abort_clean->set_value(clean);
        });
        res.end();
    });

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    app.wait_for_server_start();

    {
        HttpClient client(LOCALHOST_ADDRESS, 45451);
        client.send("GET /done HTTP/1.1\r\nHost: localhost\r\n\r\n");
        std::string response;
        while (response.size() < 5 || response.compare(response.size() - 5, 5, "0\r\n\r\n") != 0)
            response += client.receive();
    }
    CHECK(done_clean->get_future().get() == true);

    {
        HttpClient client(LOCALHOST_ADDRESS, 45451);
        client.send("GET /abort HTTP/1.1\r\nHost: localhost\r\n\r\n");
        try
        {
            while (true)
                client.receive();
        }
        catch (const std::exception&)
        {
        }
    }
    CHECK(abort_clean->get_future().get() == false);

    app.stop();
} // chunked_response_completion_handler


TEST_CASE("chunked_response_throwing_completion_handler")
{
    SimpleApp app;

    CROW_ROUTE(app, "/throwing")
    ([](const crow::request&, crow::response& res) {
        res.set_chunked_content_provider([](std::string& chunk) {
            chunk = "body";
            return crow::chunk_result::done;
        });
        res.set_chunked_completion_handler([](bool) {
            throw std::runtime_error("completion failed");
        });
        res.end();
    });

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    app.wait_for_server_start();

    // An exception from the completion handler must not skip the connection
    // cleanup: the response is still delivered in full and the server survives.
    HttpClient client(LOCALHOST_ADDRESS, 45451);
    client.send("GET /throwing HTTP/1.1\r\nHost: localhost\r\n\r\n");
    std::string response;
    while (response.size() < 5 || response.compare(response.size() - 5, 5, "0\r\n\r\n") != 0)
        response += client.receive();
    CHECK(response.find("Transfer-Encoding: chunked") != std::string::npos);

    // The connection stays usable for the next request.
    client.send("GET /throwing HTTP/1.1\r\nHost: localhost\r\n\r\n");
    std::string second;
    while (second.size() < 5 || second.compare(second.size() - 5, 5, "0\r\n\r\n") != 0)
        second += client.receive();
    CHECK(second.find("Transfer-Encoding: chunked") != std::string::npos);

    app.stop();
} // chunked_response_throwing_completion_handler


TEST_CASE("chunked_provider_excludes_other_body_sources")
{
    // A response has exactly one body source; whichever is configured last wins.

    // A chunk provider discards a previously configured static file and string body.
    {
        response res;
        res.set_static_file_info("tests/img/cat.jpg");
        res.body = "leftover";
        res.set_chunked_content_provider([](std::string&) { return crow::chunk_result::done; });

        CHECK(res.is_chunked_type());
        CHECK(!res.is_static_type());
        CHECK(res.body.empty());
        CHECK(res.get_header_value("Content-Length").empty());
        CHECK(res.get_header_value("Transfer-Encoding") == "chunked");
    }

    // A static file discards a previously configured chunk provider and its framing header.
    {
        response res;
        res.set_chunked_content_provider([](std::string&) { return crow::chunk_result::done; });
        res.set_static_file_info("tests/img/cat.jpg");

        CHECK(!res.is_chunked_type());
        CHECK(res.is_static_type());
        CHECK(res.get_header_value("Transfer-Encoding").empty());
        CHECK(!res.get_header_value("Content-Length").empty());
    }
} // chunked_provider_excludes_other_body_sources


TEST_CASE("chunked_response_throwing_provider")
{
    SimpleApp app;

    auto throw_clean = std::make_shared<std::promise<bool>>();

    CROW_ROUTE(app, "/throw")
    ([throw_clean](const crow::request&, crow::response& res) {
        int calls = 0;
        res.set_chunked_content_provider(
          [calls](std::string& chunk) mutable -> bool {
              if (++calls < 3)
              {
                  chunk = "part" + std::to_string(calls);
                  return true;
              }
              throw std::runtime_error("provider failed");
          },
          "text/plain");
        res.set_chunked_completion_handler([throw_clean](bool clean) {
            throw_clean->set_value(clean);
        });
        res.end();
    });

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    app.wait_for_server_start();

    HttpClient client(LOCALHOST_ADDRESS, 45451);
    client.send("GET /throw HTTP/1.1\r\nHost: localhost\r\n\r\n");

    // The exception is treated as an abort: the connection is closed without the
    // terminating frame, so reading past the truncated body eventually throws.
    std::string response;
    try
    {
        while (true)
            response += client.receive();
    }
    catch (const std::exception&)
    {
    }

    CHECK(response.find("Transfer-Encoding: chunked") != std::string::npos);

    auto body_start = response.find("\r\n\r\n");
    REQUIRE(body_start != std::string::npos);
    std::string chunked_body = response.substr(body_start + 4);
    CHECK(chunked_body.find("5\r\npart1\r\n") != std::string::npos);
    CHECK(chunked_body.find("5\r\npart2\r\n") != std::string::npos);
    CHECK(chunked_body.find("0\r\n\r\n") == std::string::npos);

    CHECK(throw_clean->get_future().get() == false);

    app.stop();
} // chunked_response_throwing_provider
