#include "unittest.h"

TEST_CASE("simple_url_params")
{
    static char buf[2048];

    SimpleApp app;

    query_string last_url_params;

    CROW_ROUTE(app, "/params")
    ([&last_url_params](const crow::request& req) {
        last_url_params = std::move(req.url_params);
        return "OK";
    });

    /// params?h=1&foo=bar&lol&count[]=1&count[]=4&pew=5.2

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    app.wait_for_server_start();
    asio::io_service is;
    std::string sendmsg;

    // check empty params
    sendmsg = "GET /params\r\n\r\n";
    {
        asio::ip::tcp::socket c(is);
        c.connect(asio::ip::tcp::endpoint(
          asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));
        c.send(asio::buffer(sendmsg));
        c.receive(asio::buffer(buf, 2048));
        c.close();

        stringstream ss;
        ss << last_url_params;

        CHECK("[  ]" == ss.str());
    }
    // check single presence
    sendmsg = "GET /params?foobar\r\n\r\n";
    {
        asio::ip::tcp::socket c(is);
        c.connect(asio::ip::tcp::endpoint(
          asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));
        c.send(asio::buffer(sendmsg));
        c.receive(asio::buffer(buf, 2048));
        c.close();

        CHECK(last_url_params.get("missing") == nullptr);
        CHECK(last_url_params.get("foobar") != nullptr);
        CHECK(last_url_params.get_list("missing").empty());
    }
    // check multiple presence
    sendmsg = "GET /params?foo&bar&baz\r\n\r\n";
    {
        asio::ip::tcp::socket c(is);
        c.connect(asio::ip::tcp::endpoint(
          asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));
        c.send(asio::buffer(sendmsg));
        c.receive(asio::buffer(buf, 2048));
        c.close();

        CHECK(last_url_params.get("missing") == nullptr);
        CHECK(last_url_params.get("foo") != nullptr);
        CHECK(last_url_params.get("bar") != nullptr);
        CHECK(last_url_params.get("baz") != nullptr);
    }
    // check single value
    sendmsg = "GET /params?hello=world\r\n\r\n";
    {
        asio::ip::tcp::socket c(is);
        c.connect(asio::ip::tcp::endpoint(
          asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));
        c.send(asio::buffer(sendmsg));
        c.receive(asio::buffer(buf, 2048));
        c.close();

        CHECK(string(last_url_params.get("hello")) == "world");
    }
    // check multiple value
    sendmsg = "GET /params?hello=world&left=right&up=down\r\n\r\n";
    {
        asio::ip::tcp::socket c(is);
        c.connect(asio::ip::tcp::endpoint(
          asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));
        c.send(asio::buffer(sendmsg));
        c.receive(asio::buffer(buf, 2048));
        c.close();

        query_string mutable_params(last_url_params);

        CHECK(string(mutable_params.get("hello")) == "world");
        CHECK(string(mutable_params.get("left")) == "right");
        CHECK(string(mutable_params.get("up")) == "down");

        std::string z = mutable_params.pop("left");
        CHECK(z == "right");
        CHECK(mutable_params.get("left") == nullptr);
    }
    // check multiple value, multiple types
    sendmsg = "GET /params?int=100&double=123.45&boolean=1\r\n\r\n";
    {
        asio::ip::tcp::socket c(is);
        c.connect(asio::ip::tcp::endpoint(
          asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));
        c.send(asio::buffer(sendmsg));
        c.receive(asio::buffer(buf, 2048));
        c.close();

        CHECK(utility::lexical_cast<int>(last_url_params.get("int")) == 100);
        CHECK(utility::lexical_cast<double>(last_url_params.get("double")) ==
              123.45);
        CHECK(utility::lexical_cast<bool>(last_url_params.get("boolean")));
    }
    // check single array value
    sendmsg = "GET /params?tmnt[]=leonardo\r\n\r\n";
    {
        asio::ip::tcp::socket c(is);

        c.connect(asio::ip::tcp::endpoint(
          asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));
        c.send(asio::buffer(sendmsg));
        c.receive(asio::buffer(buf, 2048));
        c.close();

        CHECK(last_url_params.get("tmnt") == nullptr);
        CHECK(last_url_params.get_list("tmnt").size() == 1);
        CHECK(string(last_url_params.get_list("tmnt")[0]) == "leonardo");
    }
    // check multiple array value
    sendmsg = "GET /params?tmnt[]=leonardo&tmnt[]=donatello&tmnt[]=raphael\r\n\r\n";
    {
        asio::ip::tcp::socket c(is);

        c.connect(asio::ip::tcp::endpoint(
          asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));
        c.send(asio::buffer(sendmsg));
        c.receive(asio::buffer(buf, 2048));
        c.close();

        CHECK(last_url_params.get_list("tmnt").size() == 3);
        CHECK(string(last_url_params.get_list("tmnt")[0]) == "leonardo");
        CHECK(string(last_url_params.get_list("tmnt")[1]) == "donatello");
        CHECK(string(last_url_params.get_list("tmnt")[2]) == "raphael");
        CHECK(last_url_params.pop_list("tmnt").size() == 3);
        CHECK(last_url_params.get_list("tmnt").size() == 0);
    }
    // check dictionary value
    sendmsg = "GET /params?kees[one]=vee1&kees[two]=vee2&kees[three]=vee3\r\n\r\n";
    {
        asio::ip::tcp::socket c(is);

        c.connect(asio::ip::tcp::endpoint(asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));
        c.send(asio::buffer(sendmsg));
        c.receive(asio::buffer(buf, 2048));
        c.close();

        CHECK(last_url_params.get_dict("kees").size() == 3);
        CHECK(string(last_url_params.get_dict("kees")["one"]) == "vee1");
        CHECK(string(last_url_params.get_dict("kees")["two"]) == "vee2");
        CHECK(string(last_url_params.get_dict("kees")["three"]) == "vee3");
        CHECK(last_url_params.pop_dict("kees").size() == 3);
        CHECK(last_url_params.get_dict("kees").size() == 0);
    }
    app.stop();
} // simple_url_params
