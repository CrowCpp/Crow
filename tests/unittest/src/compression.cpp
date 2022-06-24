#include "unittest.h"

TEST_CASE("zlib_compression")
{
    static char buf_deflate[2048];
    static char buf_gzip[2048];

    SimpleApp app_deflate, app_gzip;

    std::string expected_string = "Although moreover mistaken kindness me feelings do be marianne. Son over own nay with tell they cold upon are. "
                                  "Cordial village and settled she ability law herself. Finished why bringing but sir bachelor unpacked any thoughts. "
                                  "Unpleasing unsatiable particular inquietude did nor sir. Get his declared appetite distance his together now families. "
                                  "Friends am himself at on norland it viewing. Suspected elsewhere you belonging continued commanded she.";

    // test deflate
    CROW_ROUTE(app_deflate, "/test_compress")
    ([&]() {
        return expected_string;
    });

    CROW_ROUTE(app_deflate, "/test")
    ([&](const request&, response& res) {
        res.compressed = false;

        res.body = expected_string;
        res.end();
    });

    // test gzip
    CROW_ROUTE(app_gzip, "/test_compress")
    ([&]() {
        return expected_string;
    });

    CROW_ROUTE(app_gzip, "/test")
    ([&](const request&, response& res) {
        res.compressed = false;

        res.body = expected_string;
        res.end();
    });

    auto t1 = app_deflate.bindaddr(LOCALHOST_ADDRESS).port(45451).use_compression(compression::algorithm::DEFLATE).run_async();
    auto t2 = app_gzip.bindaddr(LOCALHOST_ADDRESS).port(45452).use_compression(compression::algorithm::GZIP).run_async();

    app_deflate.wait_for_server_start();
    app_gzip.wait_for_server_start();

    std::string test_compress_msg = "GET /test_compress\r\nAccept-Encoding: gzip, deflate\r\n\r\n";
    std::string test_compress_no_header_msg = "GET /test_compress\r\n\r\n";
    std::string test_none_msg = "GET /test\r\n\r\n";

    auto inflate_string = [](std::string const& deflated_string) -> const std::string {
        std::string inflated_string;
        Bytef tmp[8192];

        z_stream zstream{};
        zstream.avail_in = deflated_string.size();
        // Nasty const_cast but zlib won't alter its contents
        zstream.next_in = const_cast<Bytef*>(reinterpret_cast<Bytef const*>(deflated_string.c_str()));
        // Initialize with automatic header detection, for gzip support
        if (::inflateInit2(&zstream, MAX_WBITS | 32) == Z_OK)
        {
            do
            {
                zstream.avail_out = sizeof(tmp);
                zstream.next_out = &tmp[0];

                auto ret = ::inflate(&zstream, Z_NO_FLUSH);
                if (ret == Z_OK || ret == Z_STREAM_END)
                {
                    std::copy(&tmp[0], &tmp[sizeof(tmp) - zstream.avail_out], std::back_inserter(inflated_string));
                }
                else
                {
                    // Something went wrong with inflate; make sure we return an empty string
                    inflated_string.clear();
                    break;
                }

            } while (zstream.avail_out == 0);

            // Free zlib's internal memory
            ::inflateEnd(&zstream);
        }

        return inflated_string;
    };

    asio::io_service is;
    {
        // Compression
        {
            asio::ip::tcp::socket socket[2] = {asio::ip::tcp::socket(is), asio::ip::tcp::socket(is)};
            socket[0].connect(asio::ip::tcp::endpoint(asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));
            socket[1].connect(asio::ip::tcp::endpoint(asio::ip::address::from_string(LOCALHOST_ADDRESS), 45452));

            socket[0].send(asio::buffer(test_compress_msg));
            socket[1].send(asio::buffer(test_compress_msg));

            socket[0].receive(asio::buffer(buf_deflate, 2048));
            socket[1].receive(asio::buffer(buf_gzip, 2048));

            std::string response_deflate;
            std::string response_gzip;

            for (unsigned i{0}; i < 2048; i++)
            {
                if (buf_deflate[i] == 0)
                {
                    bool end = true;
                    for (unsigned j = i; j < 2048; j++)
                    {
                        if (buf_deflate[j] != 0)
                        {
                            end = false;
                            break;
                        }
                    }
                    if (end)
                    {
                        response_deflate.push_back(0);
                        response_deflate.push_back(0);
                        break;
                    }
                }
                response_deflate.push_back(buf_deflate[i]);
            }

            for (unsigned i{0}; i < 2048; i++)
            {
                if (buf_gzip[i] == 0)
                {
                    bool end = true;
                    for (unsigned j = i; j < 2048; j++)
                    {
                        if (buf_gzip[j] != 0)
                        {
                            end = false;
                            break;
                        }
                    }
                    if (end)
                    {
                        response_deflate.push_back(0);
                        response_deflate.push_back(0);
                        break;
                    }
                }
                response_gzip.push_back(buf_gzip[i]);
            }

            response_deflate = inflate_string(response_deflate.substr(response_deflate.find("\r\n\r\n") + 4));
            response_gzip = inflate_string(response_gzip.substr(response_gzip.find("\r\n\r\n") + 4));

            socket[0].close();
            socket[1].close();
            CHECK(expected_string == response_deflate);
            CHECK(expected_string == response_gzip);
        }
        // No Header (thus no compression)
        {
            asio::ip::tcp::socket socket[2] = {asio::ip::tcp::socket(is), asio::ip::tcp::socket(is)};
            socket[0].connect(asio::ip::tcp::endpoint(asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));
            socket[1].connect(asio::ip::tcp::endpoint(asio::ip::address::from_string(LOCALHOST_ADDRESS), 45452));

            socket[0].send(asio::buffer(test_compress_no_header_msg));
            socket[1].send(asio::buffer(test_compress_no_header_msg));

            socket[0].receive(asio::buffer(buf_deflate, 2048));
            socket[1].receive(asio::buffer(buf_gzip, 2048));

            std::string response_deflate(buf_deflate);
            std::string response_gzip(buf_gzip);
            response_deflate = response_deflate.substr(98);
            response_gzip = response_gzip.substr(98);

            socket[0].close();
            socket[1].close();
            CHECK(expected_string == response_deflate);
            CHECK(expected_string == response_gzip);
        }
        // No compression
        {
            asio::ip::tcp::socket socket[2] = {asio::ip::tcp::socket(is), asio::ip::tcp::socket(is)};
            socket[0].connect(asio::ip::tcp::endpoint(asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));
            socket[1].connect(asio::ip::tcp::endpoint(asio::ip::address::from_string(LOCALHOST_ADDRESS), 45452));

            socket[0].send(asio::buffer(test_none_msg));
            socket[1].send(asio::buffer(test_none_msg));

            socket[0].receive(asio::buffer(buf_deflate, 2048));
            socket[1].receive(asio::buffer(buf_gzip, 2048));

            std::string response_deflate(buf_deflate);
            std::string response_gzip(buf_gzip);
            response_deflate = response_deflate.substr(98);
            response_gzip = response_gzip.substr(98);

            socket[0].close();
            socket[1].close();
            CHECK(expected_string == response_deflate);
            CHECK(expected_string == response_gzip);
        }
    }

    app_deflate.stop();
    app_gzip.stop();
} // zlib_compression
