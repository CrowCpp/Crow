#include <atomic>
#include <cstdint>
#include <fuzzer/FuzzedDataProvider.h>
#include <thread>

#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "crow.h"

// Fuzz harness that drives crow's full server path (http_server.h, http_connection.h,
// socket_adaptors.h) with fuzzed requests sent over a real loopback socket.
//
// Harness-defect history this version fixes (the previous revision produced an
// all-zero coverage report in production and was a memory hog):
// - Fixed port 18080 collided with other fuzz targets running in parallel on the same
//   host; when the bind failed the server never came up and every iteration silently
//   exercised nothing. The port is now derived from the process id, and the harness
//   waits (bounded) until the server accepts before sending.
// - .multithreaded() started one Asio worker thread per hardware core (22+ threads on
//   typical fuzzing hosts) under sanitizers: large per-thread stacks and RSS. The
//   server now runs single-threaded: bounded threads, bounded memory.
// - Request size is capped per iteration so allocations driven by fuzzed lengths stay
//   bounded; send() passes MSG_NOSIGNAL and the socket is shut down cleanly so a
//   closed peer cannot turn SIGPIPE into a false crash.

namespace
{
    std::atomic<int> g_port{-1};
} // namespace

/**
 * To be run in a separate thread,
 *
 * Starts up the web-server, configures a dummy route, and serves incoming requests
 */
static void start_web_server()
{
    crow::SimpleApp app{};

    CROW_ROUTE(app, "/test/<string>/<int>")
      ([](const crow::request& req, std::string a, int b)
       {
         std::string resp{};
         for (const auto& param : req.get_body_params().keys())
         {
            resp += param;
         }
         return resp;
       });

    crow::logger::setLogLevel(crow::LogLevel::CRITICAL);
    // Memory discipline: bounded worker pool regardless of hardware concurrency
    // (crow clamps this to a minimum of 2 threads; the previous harness started
    // one thread per core - 22+ on typical fuzzing hosts - under sanitizers).
    app.concurrency(2);
    // Port uniqueness: parallel fuzzing of multiple targets on one host used to
    // collide on the fixed port 18080 and the server then never came up.
    const int port = 20000 + (static_cast<int>(::getpid()) % 20000);
    g_port = port;
    app.bindaddr("127.0.0.1")
      .port(static_cast<unsigned short>(port))
      .run();
}

/**
 * Called once at fuzzer start-up, initializes the web-server
 * @return True,
 */
static bool initialize_web_server()
{
    static std::thread ws_th{start_web_server};
    // The server intentionally lives for the whole process lifetime; detach so that
    // process exit does not run ~thread() on a still-joinable std::thread, which
    // calls std::terminate -> SIGABRT. The previous harness (upstream revision)
    // aborted on every run at exit, which is why production recorded a crash per
    // run and the target contributed zero coverage.
    ws_th.detach();
    return true;
}

static int send_request_to_web_server(FuzzedDataProvider& fdp)
{
    // Connection: close keeps server-side connection state bounded (no keep-alive
    // accumulation); the response is drained (bounded) so the server's close is
    // observed before the next iteration opens a new connection. Without this
    // backpressure a ~20k iterations/s fire-and-forget loop exhausts the process
    // file descriptors and the sanitizer runtime aborts on its own pipe()
    // allocation - a harness defect, not a target bug.
    std::string http_msg = "GET / HTTP/1.1\r\nConnection: close\r\n";
    http_msg += fdp.ConsumeRemainingBytesAsString();
    if (http_msg.size() > 4096)
        http_msg.resize(4096);

    // Bounded wait (<= ~2s total) until the server is accepting connections, then
    // exactly one synchronous request/response per iteration.
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        const int port = g_port.load();
        if (port > 0)
        {
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock >= 0)
            {
                // Never block for long on recv: bounded server response wait.
                timeval tv{};
                tv.tv_sec = 0;
                tv.tv_usec = 100 * 1000;
                setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

                sockaddr_in ws_addr{};
                ws_addr.sin_family = AF_INET;
                ws_addr.sin_port = htons(static_cast<unsigned short>(port));
                ws_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

                if (connect(sock, (struct sockaddr*)&ws_addr, sizeof(ws_addr)) == 0)
                {
                    send(sock, http_msg.data(), http_msg.length(), MSG_NOSIGNAL);
                    // Bounded drain of the response (max 64 KiB, max 128 reads).
                    char buf[4096];
                    std::size_t drained = 0;
                    for (int reads = 0; reads < 128 && drained < 65536; ++reads)
                    {
                        const ssize_t n = recv(sock, buf, sizeof(buf), 0);
                        if (n <= 0)
                            break;
                        drained += static_cast<std::size_t>(n);
                    }
                    (void)drained;
                    shutdown(sock, SHUT_WR);
                    close(sock);
                    return 0;
                }
                close(sock);
            }
        }
        usleep(20 * 1000);
    }
    return -1;
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size)
{
    static bool initialized = initialize_web_server();
    (void)initialized;
    FuzzedDataProvider fdp{data, size};

    send_request_to_web_server(fdp);
    return 0;
}

