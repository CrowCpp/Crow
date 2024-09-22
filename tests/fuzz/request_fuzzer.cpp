#include <cstdint>
#include <fuzzer/FuzzedDataProvider.h>

#include  <sys/socket.h>

#include "crow.h"

constexpr const int SERVER_PORT = 18080;

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
         for (const auto & param : req.get_body_params().keys())
         {
            resp += param;
         }
         return resp;
       });

    crow::logger::setLogLevel(crow::LogLevel::CRITICAL);
    app.bindaddr("127.0.0.1")
      .port(SERVER_PORT)
      .multithreaded()
      .run();
}

/**
 * Called once at fuzzer start-up, initializes the web-server
 * @return True,
 */
static bool initialize_web_server()
{
    static std::thread ws_th{start_web_server};
    return true;
}

static int send_request_to_web_server(FuzzedDataProvider &fdp)
{
    int rc = -1;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    auto http_msg = fdp.ConsumeRemainingBytesAsString();
    sockaddr_in ws_addr{.sin_family=AF_INET, .sin_port= htons(SERVER_PORT)};
    ws_addr.sin_addr.s_addr = INADDR_ANY;

    if (-1 == sock)
    {
        goto done;
    }

    if (-1 == connect(sock, (struct sockaddr*) &ws_addr, sizeof(ws_addr)))
    {
        close(sock);
        goto done;
    }
    http_msg.insert(0, "GET / HTTP/1.1\r\n");

    send(sock, http_msg.c_str(), http_msg.length(), 0);
    close(sock);
    rc = 0;
done:
    return rc;
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size)
{
    static bool initialized = initialize_web_server();
    FuzzedDataProvider fdp{data, size};

    send_request_to_web_server(fdp);
    return 0;
}
