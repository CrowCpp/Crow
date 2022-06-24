#include "unittest.h"

TEST_CASE("handler_with_response")
{
    SimpleApp app;
    CROW_ROUTE(app, "/")
    ([](const crow::request&, crow::response&) {});
} // handler_with_response

TEST_CASE("multipart")
{
    //
    //--CROW-BOUNDARY
    //Content-Disposition: form-data; name=\"hello\"
    //
    //world
    //--CROW-BOUNDARY
    //Content-Disposition: form-data; name=\"world\"
    //
    //hello
    //--CROW-BOUNDARY
    //Content-Disposition: form-data; name=\"multiline\"
    //
    //text
    //text
    //text
    //--CROW-BOUNDARY--
    //

    std::string test_string = "--CROW-BOUNDARY\r\nContent-Disposition: form-data; name=\"hello\"\r\n\r\nworld\r\n--CROW-BOUNDARY\r\nContent-Disposition: form-data; name=\"world\"\r\n\r\nhello\r\n--CROW-BOUNDARY\r\nContent-Disposition: form-data; name=\"multiline\"\r\n\r\ntext\ntext\ntext\r\n--CROW-BOUNDARY--\r\n";

    SimpleApp app;

    CROW_ROUTE(app, "/multipart")
    ([](const crow::request& req, crow::response& res) {
        multipart::message msg(req);
        res.body = msg.dump();
        res.end();
    });

    app.validate();

    {
        request req;
        response res;

        req.url = "/multipart";
        req.add_header("Content-Type", "multipart/form-data; boundary=CROW-BOUNDARY");
        req.body = test_string;

        app.handle(req, res);

        CHECK(test_string == res.body);


        multipart::message msg(req);
        CHECK("hello" == msg.get_part_by_name("world").body);
        CHECK("form-data" == msg.get_part_by_name("hello").get_header_object("Content-Disposition").value);
    }

    // Check with `"` encapsulating the boundary (.NET clients do this)
    {
        request req;
        response res;

        req.url = "/multipart";
        req.add_header("Content-Type", "multipart/form-data; boundary=\"CROW-BOUNDARY\"");
        req.body = test_string;

        app.handle(req, res);

        CHECK(test_string == res.body);
    }
} // multipart

TEST_CASE("send_file")
{

    struct stat statbuf_cat;
    stat("tests/img/cat.jpg", &statbuf_cat);
    struct stat statbuf_badext;
    stat("tests/img/filewith.badext", &statbuf_badext);

    SimpleApp app;

    CROW_ROUTE(app, "/jpg")
    ([](const crow::request&, crow::response& res) {
        res.set_static_file_info("tests/img/cat.jpg");
        res.end();
    });

    CROW_ROUTE(app, "/jpg2")
    ([](const crow::request&, crow::response& res) {
        res.set_static_file_info(
          "tests/img/cat2.jpg"); // This file is nonexistent on purpose
        res.end();
    });

    CROW_ROUTE(app, "/filewith.badext")
    ([](const crow::request&, crow::response& res) {
        res.set_static_file_info("tests/img/filewith.badext");
        res.end();
    });

    app.validate();

    //File not found check
    {
        request req;
        response res;

        req.url = "/jpg2";

        app.handle(req, res);


        CHECK(404 == res.code);
    }

    //Headers check
    {
        request req;
        response res;

        req.url = "/jpg";
        req.http_ver_major = 1;

        app.handle(req, res);

        CHECK(200 == res.code);

        CHECK(res.headers.count("Content-Type"));
        if (res.headers.count("Content-Type"))
            CHECK("image/jpeg" == res.headers.find("Content-Type")->second);

        CHECK(res.headers.count("Content-Length"));
        if (res.headers.count("Content-Length"))
            CHECK(to_string(statbuf_cat.st_size) == res.headers.find("Content-Length")->second);
    }

    //Unknown extension check
    {
        request req;
        response res;

        req.url = "/filewith.badext";
        req.http_ver_major = 1;

        CHECK_NOTHROW(app.handle(req, res));
        CHECK(200 == res.code);
        CHECK(res.headers.count("Content-Type"));
        if (res.headers.count("Content-Type"))
            CHECK("text/plain" == res.headers.find("Content-Type")->second);

        CHECK(res.headers.count("Content-Length"));
        if (res.headers.count("Content-Length"))
            CHECK(to_string(statbuf_badext.st_size) == res.headers.find("Content-Length")->second);
    }
} // send_file

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
    std::thread runTest([&app, &key_response, key_response_size]() {
        auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
        app.wait_for_server_start();
        asio::io_service is;
        std::string sendmsg;

        //Total bytes received
        unsigned int received = 0;
        sendmsg = "GET /test HTTP/1.0\r\n\r\n";
        {
            asio::streambuf b;

            asio::ip::tcp::socket c(is);
            c.connect(asio::ip::tcp::endpoint(
              asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));
            c.send(asio::buffer(sendmsg));

            //consuming the headers, since we don't need those for the test
            static char buf[2048];
            size_t received_headers_bytes = 0;

            // magic number is 102 (it's the size of the headers, which is how much this line below needs to read)
            const size_t headers_bytes = 102;
            while (received_headers_bytes < headers_bytes)
                received_headers_bytes += c.receive(asio::buffer(buf, 102));
            received += received_headers_bytes - headers_bytes; //add any extra that might have been received to the proper received count

            while (received < key_response_size)
            {
                asio::streambuf::mutable_buffers_type bufs = b.prepare(16384);

                size_t n(0);
                n = c.receive(bufs);
                b.commit(n);
                received += n;

                std::istream is(&b);
                std::string s;
                is >> s;

                CHECK(key_response.substr(received - n, n) == s);
            }
        }
        app.stop();
    });
    runTest.join();
} // stream_response
