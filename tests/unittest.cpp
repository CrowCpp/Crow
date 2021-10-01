#define CATCH_CONFIG_MAIN
#define CROW_ENABLE_DEBUG
#define CROW_LOG_LEVEL 0
#define CROW_MAIN
#include <sys/stat.h>

#include <iostream>
#include <sstream>
#include <vector>
#include <thread>
#include <chrono>

#include "catch.hpp"
#include "crow.h"
#include "crow/middlewares/cookie_parser.h"

using namespace std;
using namespace crow;

#define LOCALHOST_ADDRESS "127.0.0.1"

TEST_CASE("Rule")
{
  TaggedRule<> r("/http/");
  r.name("abc");

  // empty handler - fail to validate
  try {
    r.validate();
    FAIL_CHECK("empty handler should fail to validate");
  } catch (runtime_error& e) {
  }

  int x = 0;

  // registering handler
  r([&x] {
    x = 1;
    return "";
  });

  r.validate();

  response res;

  // executing handler
  CHECK(0 == x);
  r.handle(request(), res, routing_params());
  CHECK(1 == x);

  // registering handler with request argument
  r([&x](const crow::request&) {
    x = 2;
    return "";
  });

  r.validate();

  // executing handler
  CHECK(1 == x);
  r.handle(request(), res, routing_params());
  CHECK(2 == x);
}

TEST_CASE("ParameterTagging")
{
  static_assert(black_magic::is_valid("<int><int><int>"), "valid url");
  static_assert(!black_magic::is_valid("<int><int<<int>"), "invalid url");
  static_assert(!black_magic::is_valid("nt>"), "invalid url");
  CHECK(1 == black_magic::get_parameter_tag("<int>"));
  CHECK(2 == black_magic::get_parameter_tag("<uint>"));
  CHECK(3 == black_magic::get_parameter_tag("<float>"));
  CHECK(3 == black_magic::get_parameter_tag("<double>"));
  CHECK(4 == black_magic::get_parameter_tag("<str>"));
  CHECK(4 == black_magic::get_parameter_tag("<string>"));
  CHECK(5 == black_magic::get_parameter_tag("<path>"));
  CHECK(6 * 6 + 6 + 1 == black_magic::get_parameter_tag("<int><int><int>"));
  CHECK(6 * 6 + 6 + 2 == black_magic::get_parameter_tag("<uint><int><int>"));
  CHECK(6 * 6 + 6 * 3 + 2 ==
          black_magic::get_parameter_tag("<uint><double><int>"));

  // url definition parsed in compile time, build into *one number*, and given
  // to template argument
  static_assert(
      std::is_same<black_magic::S<uint64_t, double, int64_t>,
                   black_magic::arguments<6 * 6 + 6 * 3 + 2>::type>::value,
      "tag to type container");
}

TEST_CASE("PathRouting")
{
  SimpleApp app;

  CROW_ROUTE(app, "/file")
  ([] { return "file"; });

  CROW_ROUTE(app, "/path/")
  ([] { return "path"; });

  app.validate();

  {
    request req;
    response res;

    req.url = "/file";

    app.handle(req, res);

    CHECK(200 == res.code);
  }
  {
    request req;
    response res;

    req.url = "/file/";

    app.handle(req, res);
    CHECK(404 == res.code);
  }
  {
    request req;
    response res;

    req.url = "/path";

    app.handle(req, res);
    CHECK(404 != res.code);
  }
  {
    request req;
    response res;

    req.url = "/path/";

    app.handle(req, res);
    CHECK(200 == res.code);
  }
}

TEST_CASE("RoutingTest")
{
  SimpleApp app;
  int A{};
  uint32_t B{};
  double C{};
  string D{};
  string E{};

  CROW_ROUTE(app, "/0/<uint>")
  ([&](uint32_t b) {
    B = b;
    return "OK";
  });

  CROW_ROUTE(app, "/1/<int>/<uint>")
  ([&](int a, uint32_t b) {
    A = a;
    B = b;
    return "OK";
  });

  CROW_ROUTE(app, "/4/<int>/<uint>/<double>/<string>")
  ([&](int a, uint32_t b, double c, string d) {
    A = a;
    B = b;
    C = c;
    D = d;
    return "OK";
  });

  CROW_ROUTE(app, "/5/<int>/<uint>/<double>/<string>/<path>")
  ([&](int a, uint32_t b, double c, string d, string e) {
    A = a;
    B = b;
    C = c;
    D = d;
    E = e;
    return "OK";
  });

  app.validate();
  // app.debug_print();
  {
    request req;
    response res;

    req.url = "/-1";

    app.handle(req, res);

    CHECK(404 == res.code);
  }

  {
    request req;
    response res;

    req.url = "/0/1001999";

    app.handle(req, res);

    CHECK(200 == res.code);

    CHECK(1001999 == B);
  }

  {
    request req;
    response res;

    req.url = "/1/-100/1999";

    app.handle(req, res);

    CHECK(200 == res.code);

    CHECK(-100 == A);
    CHECK(1999 == B);
  }
  {
    request req;
    response res;

    req.url = "/4/5000/3/-2.71828/hellhere";
    req.add_header("TestHeader", "Value");

    app.handle(req, res);

    CHECK(200 == res.code);

    CHECK(5000 == A);
    CHECK(3 == B);
    CHECK(-2.71828 == C);
    CHECK("hellhere" == D);
  }
  {
    request req;
    response res;

    req.url = "/5/-5/999/3.141592/hello_there/a/b/c/d";
    req.add_header("TestHeader", "Value");

    app.handle(req, res);

    CHECK(200 == res.code);

    CHECK(-5 == A);
    CHECK(999 == B);
    CHECK(3.141592 == C);
    CHECK("hello_there" == D);
    CHECK("a/b/c/d" == E);
  }
}

TEST_CASE("simple_response_routing_params")
{
  CHECK(100 == response(100).code);
  CHECK(200 == response("Hello there").code);
  CHECK(500 == response(500, "Internal Error?").code);

  CHECK(100 == response(100, "xml", "").code);
  CHECK("text/xml" == response(100, "xml", "").get_header_value("Content-Type"));
  CHECK(200 == response(200, "html", "").code);
  CHECK("text/html" == response(200, "html", "").get_header_value("Content-Type"));
  CHECK(500 == response(500, "html", "Internal Error?").code);
  CHECK("text/css" == response(500, "css", "Internal Error?").get_header_value("Content-Type"));

  routing_params rp;
  rp.int_params.push_back(1);
  rp.int_params.push_back(5);
  rp.uint_params.push_back(2);
  rp.double_params.push_back(3);
  rp.string_params.push_back("hello");
  CHECK(1 == rp.get<int64_t>(0));
  CHECK(5 == rp.get<int64_t>(1));
  CHECK(2 == rp.get<uint64_t>(0));
  CHECK(3 == rp.get<double>(0));
  CHECK("hello" == rp.get<string>(0));
}

TEST_CASE("handler_with_response")
{
  SimpleApp app;
  CROW_ROUTE(app, "/")([](const crow::request&, crow::response&) {});
}

TEST_CASE("http_method")
{
  SimpleApp app;

  CROW_ROUTE(app, "/").methods("POST"_method,
                               "GET"_method)([](const request& req) {
    if (req.method == "GET"_method)
      return "2";
    else
      return "1";
  });

  CROW_ROUTE(app, "/get_only")
      .methods("GET"_method)([](const request& /*req*/) { return "get"; });
  CROW_ROUTE(app, "/post_only")
      .methods("POST"_method)([](const request& /*req*/) { return "post"; });
  CROW_ROUTE(app, "/patch_only")
      .methods("PATCH"_method)([](const request& /*req*/) { return "patch"; });
  CROW_ROUTE(app, "/purge_only")
      .methods("PURGE"_method)([](const request& /*req*/) { return "purge"; });

  app.validate();
  app.debug_print();

  // cannot have multiple handlers for the same url
  // CROW_ROUTE(app, "/")
  //.methods("GET"_method)
  //([]{ return "2"; });

  {
    request req;
    response res;

    req.url = "/";
    app.handle(req, res);

    CHECK("2" == res.body);
  }
  {
    request req;
    response res;

    req.url = "/";
    req.method = "POST"_method;
    app.handle(req, res);

    CHECK("1" == res.body);
  }

  {
    request req;
    response res;

    req.url = "/get_only";
    app.handle(req, res);

    CHECK("get" == res.body);
  }

  {
    request req;
    response res;

    req.url = "/patch_only";
    req.method = "PATCH"_method;
    app.handle(req, res);

    CHECK("patch" == res.body);
  }

  {
    request req;
    response res;

    req.url = "/purge_only";
    req.method = "PURGE"_method;
    app.handle(req, res);

    CHECK("purge" == res.body);
  }

  {
    request req;
    response res;

    req.url = "/get_only";
    req.method = "POST"_method;
    app.handle(req, res);

    CHECK("get" != res.body);
  }

  {
    request req;
    response res;

    req.url = "/get_only";
    req.method = "POST"_method;
    app.handle(req, res);

    CHECK(405 == res.code);
  }

  {
    request req;
    response res;

    req.url = "/get_only";
    req.method = "HEAD"_method;
    app.handle(req, res);

    CHECK(200 == res.code);
    CHECK("" == res.body);
  }

  {
    request req;
    response res;

    req.url = "/";
    req.method = "OPTIONS"_method;
    app.handle(req, res);

    CHECK(204 == res.code);
    CHECK("OPTIONS, HEAD, GET, POST" == res.get_header_value("Allow"));
  }

  {
    request req;
    response res;

    req.url = "/does_not_exist";
    req.method = "OPTIONS"_method;
    app.handle(req, res);

    CHECK(404 == res.code);
  }

  {
    request req;
    response res;

    req.url = "/*";
    req.method = "OPTIONS"_method;
    app.handle(req, res);

    CHECK(204 == res.code);
    CHECK("OPTIONS, HEAD, GET, POST, PATCH, PURGE" == res.get_header_value("Allow"));

  }
}

TEST_CASE("server_handling_error_request")
{
  static char buf[2048];
  SimpleApp app;
  CROW_ROUTE(app, "/")([] { return "A"; });
  // Server<SimpleApp> server(&app, LOCALHOST_ADDRESS, 45451);
  // auto _ = async(launch::async, [&]{server.run();});
  auto _ = async(launch::async,
                 [&] { app.bindaddr(LOCALHOST_ADDRESS).port(45451).run(); });
  app.wait_for_server_start();
  std::string sendmsg = "POX";
  asio::io_service is;
  {
    asio::ip::tcp::socket c(is);
    c.connect(asio::ip::tcp::endpoint(
        asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));

    c.send(asio::buffer(sendmsg));

    try {
      c.receive(asio::buffer(buf, 2048));
      FAIL_CHECK();
    } catch (std::exception& e) {
      // std::cerr << e.what() << std::endl;
    }
  }
  app.stop();
}

TEST_CASE("multi_server")
{
  static char buf[2048];
  SimpleApp app1, app2;
  CROW_ROUTE(app1, "/").methods("GET"_method,
                                "POST"_method)([] { return "A"; });
  CROW_ROUTE(app2, "/").methods("GET"_method,
                                "POST"_method)([] { return "B"; });

  auto _ = async(launch::async,
                 [&] { app1.bindaddr(LOCALHOST_ADDRESS).port(45451).run(); });
  auto _2 = async(launch::async,
                  [&] { app2.bindaddr(LOCALHOST_ADDRESS).port(45452).run(); });
  app1.wait_for_server_start();
  app2.wait_for_server_start();

  std::string sendmsg =
      "POST /\r\nContent-Length:3\r\nX-HeaderTest: 123\r\n\r\nA=B\r\n";
  asio::io_service is;
  {
    asio::ip::tcp::socket c(is);
    c.connect(asio::ip::tcp::endpoint(
        asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));

    c.send(asio::buffer(sendmsg));

    size_t recved = c.receive(asio::buffer(buf, 2048));
    CHECK('A' == buf[recved - 1]);
  }

  {
    asio::ip::tcp::socket c(is);
    c.connect(asio::ip::tcp::endpoint(
        asio::ip::address::from_string(LOCALHOST_ADDRESS), 45452));

    for (auto ch : sendmsg) {
      char buf[1] = {ch};
      c.send(asio::buffer(buf));
    }

    size_t recved = c.receive(asio::buffer(buf, 2048));
    CHECK('B' == buf[recved - 1]);
  }

  app1.stop();
  app2.stop();
}

TEST_CASE("json_read")
{
  {
    const char* json_error_tests[] = {
        "{} 3",
        "{{}",
        "{3}",
        "3.4.5",
        "+3",
        "3-2",
        "00",
        "03",
        "1e3e3",
        "1e+.3",
        "nll",
        "f",
        "t",
        "{\"x\":3,}",
        "{\"x\"}",
        "{\"x\":3   q}",
        "{\"x\":[3 4]}",
        "{\"x\":[\"",
        "{\"x\":[[], 4],\"y\",}",
        "{\"x\":[3",
        "{\"x\":[ null, false, true}",
    };
    for (auto s : json_error_tests) {
      auto x = json::load(s);
      if (x) {
        FAIL_CHECK(std::string("should fail to parse ") + s);
        return;
      }
    }
  }

  auto x = json::load(R"({"message":"hello, world"})");
  if (!x) FAIL_CHECK("fail to parse");
  CHECK("hello, world" == x["message"]);
  CHECK(1 == x.size());
  CHECK(false == x.has("mess"));
  REQUIRE_THROWS(x["mess"]);
  // TODO returning false is better than exception
  // ASSERT_THROW(3 == x["message"]);
  CHECK(12 == x["message"].size());

  std::string s =
      R"({"int":3,     "ints"  :[1,2,3,4,5],	"bigint":1234567890	})";
  auto y = json::load(s);
  CHECK(3 == y["int"]);
  CHECK(3.0 == y["int"]);
  CHECK(3.01 != y["int"]);
  CHECK(5 == y["ints"].size());
  CHECK(1 == y["ints"][0]);
  CHECK(2 == y["ints"][1]);
  CHECK(3 == y["ints"][2]);
  CHECK(4 == y["ints"][3]);
  CHECK(5 == y["ints"][4]);
  CHECK(1u == y["ints"][0]);
  CHECK(1.f == y["ints"][0]);

  int q = (int)y["ints"][1];
  CHECK(2 == q);
  q = y["ints"][2].i();
  CHECK(3 == q);
  CHECK(1234567890 == y["bigint"]);

  std::string s2 = R"({"bools":[true, false], "doubles":[1.2, -3.4]})";
  auto z = json::load(s2);
  CHECK(2 == z["bools"].size());
  CHECK(2 == z["doubles"].size());
  CHECK(true == z["bools"][0].b());
  CHECK(false == z["bools"][1].b());
  CHECK(1.2 == z["doubles"][0].d());
  CHECK(-3.4 == z["doubles"][1].d());

  std::string s3 = R"({"uint64": 18446744073709551615})";
  auto z1 = json::load(s3);
  CHECK(18446744073709551615ull == z1["uint64"].u());

  std::ostringstream os;
  os << z1["uint64"];
  CHECK("18446744073709551615" == os.str());
}

TEST_CASE("json_read_real")
{
  vector<std::string> v{"0.036303908355795146",
                        "0.18320417789757412",
                        "0.05319940476190476",
                        "0.15224702380952382",
                        "0",
                        "0.3296201145552561",
                        "0.47921580188679247",
                        "0.05873511904761905",
                        "0.1577827380952381",
                        "0.4996841307277628",
                        "0.6425412735849056",
                        "0.052113095238095236",
                        "0.12830357142857143",
                        "0.7871041105121294",
                        "0.954220013477089",
                        "0.05869047619047619",
                        "0.1625",
                        "0.8144794474393531",
                        "0.9721613881401617",
                        "0.1399404761904762",
                        "0.24470238095238095",
                        "0.04527459568733154",
                        "0.2096950808625337",
                        "0.35267857142857145",
                        "0.42791666666666667",
                        "0.855731974393531",
                        "0.9352467991913747",
                        "0.3816220238095238",
                        "0.4282886904761905",
                        "0.39414167789757415",
                        "0.5316079851752021",
                        "0.3809375",
                        "0.4571279761904762",
                        "0.03522995283018868",
                        "0.1915641846361186",
                        "0.6164136904761904",
                        "0.7192708333333333",
                        "0.05675117924528302",
                        "0.21308541105121293",
                        "0.7045386904761904",
                        "0.8016815476190476"};
  for (auto x : v) {
    CROW_LOG_DEBUG << x;
    CHECK(json::load(x).d() == boost::lexical_cast<double>(x));
  }

  auto ret = json::load(
      R"---({"balloons":[{"mode":"ellipse","left":0.036303908355795146,"right":0.18320417789757412,"top":0.05319940476190476,"bottom":0.15224702380952382,"index":"0"},{"mode":"ellipse","left":0.3296201145552561,"right":0.47921580188679247,"top":0.05873511904761905,"bottom":0.1577827380952381,"index":"1"},{"mode":"ellipse","left":0.4996841307277628,"right":0.6425412735849056,"top":0.052113095238095236,"bottom":0.12830357142857143,"index":"2"},{"mode":"ellipse","left":0.7871041105121294,"right":0.954220013477089,"top":0.05869047619047619,"bottom":0.1625,"index":"3"},{"mode":"ellipse","left":0.8144794474393531,"right":0.9721613881401617,"top":0.1399404761904762,"bottom":0.24470238095238095,"index":"4"},{"mode":"ellipse","left":0.04527459568733154,"right":0.2096950808625337,"top":0.35267857142857145,"bottom":0.42791666666666667,"index":"5"},{"mode":"ellipse","left":0.855731974393531,"right":0.9352467991913747,"top":0.3816220238095238,"bottom":0.4282886904761905,"index":"6"},{"mode":"ellipse","left":0.39414167789757415,"right":0.5316079851752021,"top":0.3809375,"bottom":0.4571279761904762,"index":"7"},{"mode":"ellipse","left":0.03522995283018868,"right":0.1915641846361186,"top":0.6164136904761904,"bottom":0.7192708333333333,"index":"8"},{"mode":"ellipse","left":0.05675117924528302,"right":0.21308541105121293,"top":0.7045386904761904,"bottom":0.8016815476190476,"index":"9"}]})---");
  CHECK(ret);
}

TEST_CASE("json_read_unescaping")
{
  {
    auto x = json::load(R"({"data":"\ud55c\n\t\r"})");
    if (!x) {
      FAIL_CHECK("fail to parse");
      return;
    }
    CHECK(6 == x["data"].size());
    CHECK("한\n\t\r" == x["data"]);
  }
  {
    // multiple r_string instance
    auto x = json::load(R"({"data":"\ud55c\n\t\r"})");
    auto a = x["data"].s();
    auto b = x["data"].s();
    CHECK(6 == a.size());
    CHECK(6 == b.size());
    CHECK(6 == x["data"].size());
  }
}

TEST_CASE("json_read_string")
{
    auto x = json::load(R"({"message": 53})");
    int y (x["message"]);
    std::string z(x["message"]);
    CHECK(53 == y);
    CHECK("53" == z);
}

TEST_CASE("json_read_container")
{
    auto x = json::load(R"({"first": 53, "second": "55", "third": [5,6,7,8,3,2,1,4]})");
    CHECK(std::vector<std::string>({"first", "second", "third"}) == x.keys());
    CHECK(53 == int(x.lo()[0]));
    CHECK("55" == std::string(x.lo()[1]));
    CHECK (8 == int(x.lo()[2].lo()[3]));
}

TEST_CASE("json_write")
{
  json::wvalue x;
  x["message"] = "hello world";
  CHECK(R"({"message":"hello world"})" == x.dump());
  x["message"] = std::string("string value");
  CHECK(R"({"message":"string value"})" == x.dump());
  x["message"]["x"] = 3;
  CHECK(R"({"message":{"x":3}})" == x.dump());
  x["message"]["y"] = 5;
  CHECK((R"({"message":{"x":3,"y":5}})" == x.dump() ||
           R"({"message":{"y":5,"x":3}})" == x.dump()));
  x["message"] = 5.5;
  CHECK(R"({"message":5.5})" == x.dump());
  x["message"] = 1234567890;
  CHECK(R"({"message":1234567890})" == x.dump());

  json::wvalue y;
  y["scores"][0] = 1;
  y["scores"][1] = "king";
  y["scores"][2] = 3.5;
  CHECK(R"({"scores":[1,"king",3.5]})" == y.dump());

  y["scores"][2][0] = "real";
  y["scores"][2][1] = false;
  y["scores"][2][2] = true;
  CHECK(R"({"scores":[1,"king",["real",false,true]]})" == y.dump());

  y["scores"]["a"]["b"]["c"] = nullptr;
  CHECK(R"({"scores":{"a":{"b":{"c":null}}}})" == y.dump());

  y["scores"] = std::vector<int>{1, 2, 3};
  CHECK(R"({"scores":[1,2,3]})" == y.dump());
}

TEST_CASE("json_copy_r_to_w_to_w_to_r")
{
  json::rvalue r = json::load(
      R"({"smallint":2,"bigint":2147483647,"fp":23.43,"fpsc":2.343e1,"str":"a string","trueval":true,"falseval":false,"nullval":null,"listval":[1,2,"foo","bar"],"obj":{"member":23,"other":"baz"}})");
  json::wvalue v{r};
  json::wvalue w(v);
  json::rvalue x =
      json::load(w.dump());  // why no copy-ctor wvalue -> rvalue?
  CHECK(2 == x["smallint"]);
  CHECK(2147483647 == x["bigint"]);
  CHECK(23.43 == x["fp"]);
  CHECK(23.43 == x["fpsc"]);
  CHECK("a string" == x["str"]);
  CHECK(x["trueval"].b());
  REQUIRE_FALSE(x["falseval"].b());
  CHECK(json::type::Null == x["nullval"].t());
  CHECK(4u == x["listval"].size());
  CHECK(1 == x["listval"][0]);
  CHECK(2 == x["listval"][1]);
  CHECK("foo" == x["listval"][2]);
  CHECK("bar" == x["listval"][3]);
  CHECK(23 == x["obj"]["member"]);
  CHECK("member" == x["obj"]["member"].key());
  CHECK("baz" == x["obj"]["other"]);
  CHECK("other" == x["obj"]["other"].key());
}

//TODO maybe combine these

TEST_CASE("json::wvalue::wvalue(bool)") {
  CHECK(json::wvalue(true).t() == json::type::True);
  CHECK(json::wvalue(false).t() == json::type::False);
}

TEST_CASE("json::wvalue::wvalue(std::uint8_t)") {
  std::uint8_t i = 42;
  json::wvalue value = i;

  CHECK(value.t() == json::type::Number);
  CHECK(value.dump() == "42");
}

TEST_CASE("json::wvalue::wvalue(std::uint16_t)") {
  std::uint16_t i = 42;
  json::wvalue value = i;

  CHECK(value.t() == json::type::Number);
  CHECK(value.dump() == "42");
}

TEST_CASE("json::wvalue::wvalue(std::uint32_t)") {
  std::uint32_t i = 42;
  json::wvalue value = i;

  CHECK(value.t() == json::type::Number);
  CHECK(value.dump() == "42");
}

TEST_CASE("json::wvalue::wvalue(std::uint64_t)") {
  std::uint64_t i = 42;
  json::wvalue value = i;

  CHECK(value.t() == json::type::Number);
  CHECK(value.dump() == "42");
}

TEST_CASE("json::wvalue::wvalue(std::int8_t)") {
  std::int8_t i = -42;
  json::wvalue value = i;

  CHECK(value.t() == json::type::Number);
  CHECK(value.dump() == "-42");
}

TEST_CASE("json::wvalue::wvalue(std::int16_t)") {
  std::int16_t i = -42;
  json::wvalue value = i;

  CHECK(value.t() == json::type::Number);
  CHECK(value.dump() == "-42");
}

TEST_CASE("json::wvalue::wvalue(std::int32_t)") {
  std::int32_t i = -42;
  json::wvalue value = i;

  CHECK(value.t() == json::type::Number);
  CHECK(value.dump() == "-42");
}

TEST_CASE("json::wvalue::wvalue(std::int64_t)") {
  std::int64_t i = -42;
  json::wvalue value = i;

  CHECK(value.t() == json::type::Number);
  CHECK(value.dump() == "-42");
}

TEST_CASE("json::wvalue::wvalue(float)") {
  float f = 4.2;
  json::wvalue value = f;

  CHECK(value.t() == json::type::Number);
  CHECK(value.dump() == "4.2");
}

TEST_CASE("json::wvalue::wvalue(double)") {
  double d = 4.2;
  json::wvalue value = d;

  CHECK(value.t() == json::type::Number);
  CHECK(value.dump() == "4.2");
}

TEST_CASE("json::wvalue::wvalue(char const*)") {
  char const* str = "Hello world!";
  json::wvalue value = str;

  CHECK(value.t() == json::type::String);
  CHECK(value.dump() == "\"Hello world!\"");
}

TEST_CASE("json::wvalue::wvalue(std::string const&)") {
  std::string str = "Hello world!";
  json::wvalue value = str;

  CHECK(value.t() == json::type::String);
  CHECK(value.dump() == "\"Hello world!\"");
}

TEST_CASE("json::wvalue::wvalue(std::string&&)") {
  std::string str = "Hello world!";
  json::wvalue value = std::move(str);

  CHECK(value.t() == json::type::String);
  CHECK(value.dump() == "\"Hello world!\"");
}

TEST_CASE("json::wvalue::wvalue(std::initializer_list<std::pair<std::string const, json::wvalue>>)") {
  json::wvalue integer, number, truth, lie, null;
  integer = 2147483647;
  number = 23.43;
  truth = true;
  lie = false;

  /* initializer-list constructor. */
  json::wvalue value({
    {"integer", integer},
    {"number", number},
    {"truth", truth},
    {"lie", lie},
    {"null", null}
  });

  CHECK(value["integer"].dump() == integer.dump());
  CHECK(value["number"].dump() == number.dump());
  CHECK(value["truth"].dump() == truth.dump());
  CHECK(value["lie"].dump() == lie.dump());
  CHECK(value["null"].dump() == null.dump());
}

TEST_CASE("json::wvalue::wvalue(std::[unordered_]map<std::string, json::wvalue> const&)") {
  json::wvalue integer, number, truth, lie, null;
  integer = 2147483647;
  number = 23.43;
  truth = true;
  lie = false;

  json::wvalue::object map({
    {"integer", integer},
    {"number", number},
    {"truth", truth},
    {"lie", lie},
    {"null", null}
  });

  json::wvalue value(map); /* copy-constructor. */

  CHECK(value["integer"].dump() == integer.dump());
  CHECK(value["number"].dump() == number.dump());
  CHECK(value["truth"].dump() == truth.dump());
  CHECK(value["lie"].dump() == lie.dump());
  CHECK(value["null"].dump() == null.dump());
}

TEST_CASE("json::wvalue::wvalue(std::[unordered_]map<std::string, json::wvalue>&&)") {
  json::wvalue integer, number, truth, lie, null;
  integer = 2147483647;
  number = 23.43;
  truth = true;
  lie = false;

  json::wvalue::object map = {{
    {"integer", integer},
    {"number", number},
    {"truth", truth},
    {"lie", lie},
    {"null", null}
  }};

  json::wvalue value(std::move(map)); /* move constructor. */
  // json::wvalue value = std::move(map); /* move constructor. */

  CHECK(value["integer"].dump() == integer.dump());
  CHECK(value["number"].dump() == number.dump());
  CHECK(value["truth"].dump() == truth.dump());
  CHECK(value["lie"].dump() == lie.dump());
  CHECK(value["null"].dump() == null.dump());
}

TEST_CASE("json::wvalue::operator=(std::initializer_list<std::pair<std::string const, json::wvalue>>)") {
  json::wvalue integer, number, truth, lie, null;
  integer = 2147483647;
  number = 23.43;
  truth = true;
  lie = false;

  json::wvalue value;
  /* initializer-list assignment. */
  value = {
    {"integer", integer},
    {"number", number},
    {"truth", truth},
    {"lie", lie},
    {"null", null}
  };

  CHECK(value["integer"].dump() == integer.dump());
  CHECK(value["number"].dump() == number.dump());
  CHECK(value["truth"].dump() == truth.dump());
  CHECK(value["lie"].dump() == lie.dump());
  CHECK(value["null"].dump() == null.dump());
}

TEST_CASE("json::wvalue::operator=(std::[unordered_]map<std::string, json::wvalue> const&)") {
  json::wvalue integer, number, truth, lie, null;
  integer = 2147483647;
  number = 23.43;
  truth = true;
  lie = false;

  json::wvalue::object map({
    {"integer", integer},
    {"number", number},
    {"truth", truth},
    {"lie", lie},
    {"null", null}
  });

  json::wvalue value;
  value = map; /* copy assignment. */

  CHECK(value["integer"].dump() == integer.dump());
  CHECK(value["number"].dump() == number.dump());
  CHECK(value["truth"].dump() == truth.dump());
  CHECK(value["lie"].dump() == lie.dump());
  CHECK(value["null"].dump() == null.dump());
}

TEST_CASE("json::wvalue::operator=(std::[unordered_]map<std::string, json::wvalue>&&)") {
  json::wvalue integer, number, truth, lie, null;
  integer = 2147483647;
  number = 23.43;
  truth = true;
  lie = false;

  json::wvalue::object map({
    {"integer", integer},
    {"number", number},
    {"truth", truth},
    {"lie", lie},
    {"null", null}
  });

  json::wvalue value;
  value = std::move(map); /* move assignment. */

  CHECK(value["integer"].dump() == integer.dump());
  CHECK(value["number"].dump() == number.dump());
  CHECK(value["truth"].dump() == truth.dump());
  CHECK(value["lie"].dump() == lie.dump());
  CHECK(value["null"].dump() == null.dump());
}

TEST_CASE("json_vector")
{
    json::wvalue a;
    json::wvalue b;
    json::wvalue c;
    json::wvalue d;
    json::wvalue e;
    json::wvalue f;
    json::wvalue g;
    json::wvalue h;
    a = 5;
    b = 6;
    c = 7;
    d = 8;
    e = 4;
    f = 3;
    g = 2;
    h = 1;
    std::vector<json::wvalue> nums;
    nums.emplace_back(a);
    nums.emplace_back(b);
    nums.emplace_back(c);
    nums.emplace_back(d);
    nums.emplace_back(e);
    nums.emplace_back(f);
    nums.emplace_back(g);
    nums.emplace_back(h);
    json::wvalue x(nums);

    CHECK(8 == x.size());
    CHECK("[5,6,7,8,4,3,2,1]" == x.dump());
}

TEST_CASE("json_list")
{
    json::wvalue x(json::wvalue::list({5,6,7,8,4,3,2,1}));

    CHECK(8 == x.size());
    CHECK("[5,6,7,8,4,3,2,1]" == x.dump());
}

TEST_CASE("template_basic")
{
  auto t = crow::mustache::compile(R"---(attack of {{name}})---");
  crow::mustache::context ctx;
  ctx["name"] = "killer tomatoes";
  auto result = t.render(ctx);
  CHECK("attack of killer tomatoes" == result);
}

TEST_CASE("template_load")
{
  crow::mustache::set_base(".");
  ofstream("test.mustache") << R"---(attack of {{name}})---";
  auto t = crow::mustache::load("test.mustache");
  crow::mustache::context ctx;
  ctx["name"] = "killer tomatoes";
  auto result = t.render(ctx);
  CHECK("attack of killer tomatoes" == result);
  unlink("test.mustache");
}

TEST_CASE("black_magic")
{
  using namespace black_magic;
  static_assert(
      std::is_same<void, last_element_type<int, char, void>::type>::value,
      "last_element_type");
  static_assert(std::is_same<char, pop_back<int, char, void>::rebind<
                                       last_element_type>::type>::value,
                "pop_back");
  static_assert(
      std::is_same<int, pop_back<int, char, void>::rebind<pop_back>::rebind<
                            last_element_type>::type>::value,
      "pop_back");
}

struct NullMiddleware
{
  struct context
  {};

  template <typename AllContext>
  void before_handle(request&, response&, context&, AllContext&)
  {}

  template <typename AllContext>
  void after_handle(request&, response&, context&, AllContext&)
  {}
};

struct NullSimpleMiddleware
{
  struct context
  {};

  void before_handle(request& /*req*/, response& /*res*/, context& /*ctx*/) {}

  void after_handle(request& /*req*/, response& /*res*/, context& /*ctx*/) {}
};

TEST_CASE("middleware_simple")
{
  App<NullMiddleware, NullSimpleMiddleware> app;
  decltype(app)::server_t server(&app, LOCALHOST_ADDRESS, 45451);
  CROW_ROUTE(app, "/")
  ([&](const crow::request& req) {
    app.get_context<NullMiddleware>(req);
    app.get_context<NullSimpleMiddleware>(req);
    return "";
  });
}

struct IntSettingMiddleware
{
  struct context
  {
    int val;
  };

  template <typename AllContext>
  void before_handle(request&, response&, context& ctx, AllContext&)
  {
    ctx.val = 1;
  }

  template <typename AllContext>
  void after_handle(request&, response&, context& ctx, AllContext&)
  {
    ctx.val = 2;
  }
};

std::vector<std::string> test_middleware_context_vector;

struct FirstMW
{
  struct context
  {
    std::vector<string> v;
  };

  void before_handle(request& /*req*/, response& /*res*/, context& ctx)
  {
    ctx.v.push_back("1 before");
  }

  void after_handle(request& /*req*/, response& /*res*/, context& ctx)
  {
    ctx.v.push_back("1 after");
    test_middleware_context_vector = ctx.v;
  }
};

struct SecondMW
{
  struct context
  {};
  template <typename AllContext>
  void before_handle(request& req, response& res, context&, AllContext& all_ctx)
  {
    all_ctx.template get<FirstMW>().v.push_back("2 before");
    if (req.url == "/break") res.end();
  }

  template <typename AllContext>
  void after_handle(request&, response&, context&, AllContext& all_ctx)
  {
    all_ctx.template get<FirstMW>().v.push_back("2 after");
  }
};

struct ThirdMW
{
  struct context
  {};
  template <typename AllContext>
  void before_handle(request&, response&, context&, AllContext& all_ctx)
  {
    all_ctx.template get<FirstMW>().v.push_back("3 before");
  }

  template <typename AllContext>
  void after_handle(request&, response&, context&, AllContext& all_ctx)
  {
    all_ctx.template get<FirstMW>().v.push_back("3 after");
  }
};

TEST_CASE("middleware_context")
{
  static char buf[2048];
  // SecondMW depends on FirstMW (it uses all_ctx.get<FirstMW>)
  // so it leads to compile error if we remove FirstMW from definition
  // App<IntSettingMiddleware, SecondMW> app;
  // or change the order of FirstMW and SecondMW
  // App<IntSettingMiddleware, SecondMW, FirstMW> app;

  App<IntSettingMiddleware, FirstMW, SecondMW, ThirdMW> app;

  int x{};
  CROW_ROUTE(app, "/")
  ([&](const request& req) {
    {
      auto& ctx = app.get_context<IntSettingMiddleware>(req);
      x = ctx.val;
    }
    {
      auto& ctx = app.get_context<FirstMW>(req);
      ctx.v.push_back("handle");
    }

    return "";
  });
  CROW_ROUTE(app, "/break")
  ([&](const request& req) {
    {
      auto& ctx = app.get_context<FirstMW>(req);
      ctx.v.push_back("handle");
    }

    return "";
  });

  auto _ = async(launch::async,
                 [&] { app.bindaddr(LOCALHOST_ADDRESS).port(45451).run(); });
  app.wait_for_server_start();
  std::string sendmsg = "GET /\r\n\r\n";
  asio::io_service is;
  {
    asio::ip::tcp::socket c(is);
    c.connect(asio::ip::tcp::endpoint(
        asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));

    c.send(asio::buffer(sendmsg));

    c.receive(asio::buffer(buf, 2048));
    c.close();
  }
  {
    auto& out = test_middleware_context_vector;
    CHECK(1 == x);
    CHECK(7 == out.size());
    CHECK("1 before" == out[0]);
    CHECK("2 before" == out[1]);
    CHECK("3 before" == out[2]);
    CHECK("handle" == out[3]);
    CHECK("3 after" == out[4]);
    CHECK("2 after" == out[5]);
    CHECK("1 after" == out[6]);
  }
  std::string sendmsg2 = "GET /break\r\n\r\n";
  {
    asio::ip::tcp::socket c(is);
    c.connect(asio::ip::tcp::endpoint(
        asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));

    c.send(asio::buffer(sendmsg2));

    c.receive(asio::buffer(buf, 2048));
    c.close();
  }
  {
    auto& out = test_middleware_context_vector;
    CHECK(4 == out.size());
    CHECK("1 before" == out[0]);
    CHECK("2 before" == out[1]);
    CHECK("2 after" == out[2]);
    CHECK("1 after" == out[3]);
  }
  app.stop();
}

TEST_CASE("middleware_cookieparser")
{
  static char buf[2048];

  App<CookieParser> app;

  std::string value1;
  std::string value2;
  std::string value3;
  std::string value4;

  CROW_ROUTE(app, "/")
  ([&](const request& req) {
    {
      auto& ctx = app.get_context<CookieParser>(req);
      value1 = ctx.get_cookie("key1");
      value2 = ctx.get_cookie("key2");
      value3 = ctx.get_cookie("key3");
      value4 = ctx.get_cookie("key4");
    }

    return "";
  });

  auto _ = async(launch::async,
                 [&] { app.bindaddr(LOCALHOST_ADDRESS).port(45451).run(); });
  app.wait_for_server_start();
  std::string sendmsg =
      "GET /\r\nCookie: key1=value1; key2=\"val=ue2\"; key3=\"val\"ue3\"; "
      "key4=\"val\"ue4\"\r\n\r\n";
  asio::io_service is;
  {
    asio::ip::tcp::socket c(is);
    c.connect(asio::ip::tcp::endpoint(
        asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));

    c.send(asio::buffer(sendmsg));

    c.receive(asio::buffer(buf, 2048));
    c.close();
  }
  {
    CHECK("value1" == value1);
    CHECK("val=ue2" == value2);
    CHECK("val\"ue3" == value3);
    CHECK("val\"ue4" == value4);
  }
  app.stop();
}

TEST_CASE("bug_quick_repeated_request")
{
  static char buf[2048];

  SimpleApp app;

  CROW_ROUTE(app, "/")([&] { return "hello"; });

  auto _ = async(launch::async,
                 [&] { app.bindaddr(LOCALHOST_ADDRESS).port(45451).run(); });
  app.wait_for_server_start();
  std::string sendmsg = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
  asio::io_service is;
  {
    std::vector<std::future<void>> v;
    for (int i = 0; i < 5; i++) {
      v.push_back(async(launch::async, [&] {
        asio::ip::tcp::socket c(is);
        c.connect(asio::ip::tcp::endpoint(
            asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));

        for (int j = 0; j < 5; j++) {
          c.send(asio::buffer(sendmsg));

          size_t received = c.receive(asio::buffer(buf, 2048));
          CHECK("hello" == std::string(buf + received - 5, buf + received));
        }
        c.close();
      }));
    }
  }
  app.stop();
}

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

  auto _ = async(launch::async,
                 [&] { app.bindaddr(LOCALHOST_ADDRESS).port(45451).run(); });
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

    CHECK(boost::lexical_cast<int>(last_url_params.get("int")) == 100);
    CHECK(boost::lexical_cast<double>(last_url_params.get("double")) ==
            123.45);
    CHECK(boost::lexical_cast<bool>(last_url_params.get("boolean")));
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
}

TEST_CASE("route_dynamic")
{
  SimpleApp app;
  int x = 1;
  app.route_dynamic("/")([&] {
    x = 2;
    return "";
  });

  app.route_dynamic("/set4")([&](const request&) {
    x = 4;
    return "";
  });
  app.route_dynamic("/set5")([&](const request&, response& res) {
    x = 5;
    res.end();
  });

  app.route_dynamic("/set_int/<int>")([&](int y) {
    x = y;
    return "";
  });

  try {
    app.route_dynamic("/invalid_test/<double>/<path>")([]() { return ""; });
    FAIL_CHECK();
  } catch (std::exception&) {
  }

  // app is in an invalid state when route_dynamic throws an exception.
  try {
    app.validate();
    FAIL_CHECK();
  } catch (std::exception&) {
  }

  {
    request req;
    response res;
    req.url = "/";
    app.handle(req, res);
    CHECK(x == 2);
  }
  {
    request req;
    response res;
    req.url = "/set_int/42";
    app.handle(req, res);
    CHECK(x == 42);
  }
  {
    request req;
    response res;
    req.url = "/set5";
    app.handle(req, res);
    CHECK(x == 5);
  }
  {
    request req;
    response res;
    req.url = "/set4";
    app.handle(req, res);
    CHECK(x == 4);
  }
}

TEST_CASE("multipart")
{
  std::string test_string = "--CROW-BOUNDARY\r\nContent-Disposition: form-data; name=\"hello\"\r\n\r\nworld\r\n--CROW-BOUNDARY\r\nContent-Disposition: form-data; name=\"world\"\r\n\r\nhello\r\n--CROW-BOUNDARY\r\nContent-Disposition: form-data; name=\"multiline\"\r\n\r\ntext\ntext\ntext\r\n--CROW-BOUNDARY--\r\n";

  SimpleApp app;

  CROW_ROUTE(app, "/multipart")
  ([](const crow::request& req, crow::response& res)
  {
    multipart::message msg(req);
    res.add_header("Content-Type", "multipart/form-data; boundary=CROW-BOUNDARY");
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
  }
}

TEST_CASE("send_file")
{

  struct stat statbuf;
  stat("tests/img/cat.jpg", &statbuf);

  SimpleApp app;

  CROW_ROUTE(app, "/jpg")
  ([](const crow::request&, crow::response& res) {
    res.set_static_file_info("tests/img/cat.jpg");
    res.end();
  });

  CROW_ROUTE(app, "/jpg2")
  ([](const crow::request&, crow::response& res) {
    res.set_static_file_info(
        "tests/img/cat2.jpg");  // This file is nonexistent on purpose
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

    app.handle(req, res);

    CHECK(200 == res.code);
    CHECK("image/jpeg" == res.headers.find("Content-Type")->second);
    CHECK(to_string(statbuf.st_size) ==
            res.headers.find("Content-Length")->second);
  }

  //TODO test content

}

TEST_CASE("stream_response")
{

    SimpleApp app;


    std::string keyword_ = "hello";
    std::string key_response;
    for (unsigned int i = 0; i<250000; i++)
      key_response += keyword_;

    CROW_ROUTE(app, "/test")
    ([&key_response](const crow::request&, crow::response& res)
    {
      res.body = key_response;
      res.end();
    });

    app.validate();

    //running the test on a separate thread to allow the client to sleep
   std::thread runTest([&app, &key_response](){

    auto _ = async(launch::async,
                   [&] { app.bindaddr(LOCALHOST_ADDRESS).port(45451).run(); });
    app.wait_for_server_start();
    asio::io_service is;
    std::string sendmsg;

    //Total bytes received
    unsigned int received = 0;
    sendmsg = "GET /test\r\n\r\n";
    {
      asio::streambuf b;

      asio::ip::tcp::socket c(is);
      c.connect(asio::ip::tcp::endpoint(
          asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));
      c.send(asio::buffer(sendmsg));

      //consuming the headers, since we don't need those for the test
      static char buf[2048];
      c.receive(asio::buffer(buf, 2048));

      //"hello" is 5 bytes, (5*250000)/16384 = 76.2939
      for (unsigned int i = 0; i<76; i++)
      {
        asio::streambuf::mutable_buffers_type bufs = b.prepare(16384);
        size_t n = c.receive(bufs);
        b.commit(n);
        received += n;
        std::istream is(&b);
        std::string s;
        is >> s;
        CHECK(key_response.substr(received-n, n) == s);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

      }

      //handle the 0.2 and any errors in the earlier loop
      while (c.available() > 0)
      {
          asio::streambuf::mutable_buffers_type bufs = b.prepare(16384);
          size_t n = c.receive(bufs);
          b.commit(n);
          received += n;
          std::istream is(&b);
          std::string s;
          is >> s;
          CHECK(key_response.substr(received-n, n) == s);
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }


    }
    app.stop();
    });
    runTest.join();
}

TEST_CASE("websocket")
{
  static std::string http_message = "GET /ws HTTP/1.1\r\nConnection: keep-alive, Upgrade\r\nupgrade: websocket\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";

  static bool connected{false};

  SimpleApp app;

  CROW_ROUTE(app, "/ws").websocket()
  .onopen([&](websocket::connection&){
      connected = true;
      CROW_LOG_INFO << "Connected websocket and value is " << connected;
  })
  .onmessage([&](websocket::connection& conn, const std::string& message, bool isbin){
      CROW_LOG_INFO << "Message is \"" << message << '\"';
      if (!isbin && message == "PINGME")
          conn.send_ping("");
      else if (!isbin && message == "Hello")
          conn.send_text("Hello back");
      else if (isbin && message == "Hello bin")
          conn.send_binary("Hello back bin");
  })
  .onclose([&](websocket::connection&, const std::string&){
      CROW_LOG_INFO << "Closing websocket";
  });

  app.validate();

  auto _ = async(launch::async,
                 [&] { app.bindaddr(LOCALHOST_ADDRESS).port(45451).run(); });
  app.wait_for_server_start();
  asio::io_service is;

  asio::ip::tcp::socket c(is);
  c.connect(asio::ip::tcp::endpoint(
      asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));


  char buf[2048];

  //----------Handshake----------
  {
    std::fill_n (buf, 2048, 0);
    c.send(asio::buffer(http_message));

    c.receive(asio::buffer(buf, 2048));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(connected);
  }
  //----------Pong----------
  {
    std::fill_n (buf, 2048, 0);
    char ping_message[2]("\x89");

    c.send(asio::buffer(ping_message, 2));
    c.receive(asio::buffer(buf, 2048));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK((int)(unsigned char)buf[0] == 0x8A);
  }
  //----------Ping----------
  {
    std::fill_n (buf, 2048, 0);
    char not_ping_message[2+6+1]("\x81\x06"
                                 "PINGME");

    c.send(asio::buffer(not_ping_message, 8));
    c.receive(asio::buffer(buf, 2048));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK((int)(unsigned char)buf[0] == 0x89);
  }
  //----------Text----------
  {
    std::fill_n (buf, 2048, 0);
    char text_message[2+5+1]("\x81\x05"
                             "Hello");

    c.send(asio::buffer(text_message, 7));
    c.receive(asio::buffer(buf, 2048));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::string checkstring(std::string(buf).substr(0, 12));
    CHECK(checkstring == "\x81\x0AHello back");
  }
  //----------Binary----------
  {
    std::fill_n (buf, 2048, 0);
    char bin_message[2+9+1]("\x82\x09"
                            "Hello bin");

    c.send(asio::buffer(bin_message, 11));
    c.receive(asio::buffer(buf, 2048));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::string checkstring2(std::string(buf).substr(0, 16));
    CHECK(checkstring2 == "\x82\x0EHello back bin");
  }
  //----------Masked Text----------
  {
    std::fill_n (buf, 2048, 0);
    char text_masked_message[2+4+5+1]("\x81\x85"
                                      "\x67\xc6\x69\x73"
                                      "\x2f\xa3\x05\x1f\x08");

    c.send(asio::buffer(text_masked_message, 11));
    c.receive(asio::buffer(buf, 2048));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::string checkstring3(std::string(buf).substr(0, 12));
    CHECK(checkstring3 == "\x81\x0AHello back");
  }
  //----------Masked Binary----------
  {
    std::fill_n (buf, 2048, 0);
    char bin_masked_message[2+4+9+1]("\x82\x89"
                                     "\x67\xc6\x69\x73"
                                     "\x2f\xa3\x05\x1f\x08\xe6\x0b\x1a\x09");

    c.send(asio::buffer(bin_masked_message, 15));
    c.receive(asio::buffer(buf, 2048));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::string checkstring4(std::string(buf).substr(0, 16));
    CHECK(checkstring4 == "\x82\x0EHello back bin");
  }
  //----------Close----------
  {
    std::fill_n (buf, 2048, 0);
    char close_message[10]("\x88"); //I do not know why, but the websocket code does not read this unless it's longer than 4 or so bytes

    c.send(asio::buffer(close_message, 10));
    c.receive(asio::buffer(buf, 2048));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK((int)(unsigned char)buf[0] == 0x88);
  }

  app.stop();
}

#ifdef CROW_ENABLE_COMPRESSION
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
    CROW_ROUTE(app_deflate, "/test_compress")([&](){
        return expected_string;
    });

    CROW_ROUTE(app_deflate, "/test")([&](const request&, response& res){
        res.compressed = false;

        res.body = expected_string;
        res.end();
    });

    // test gzip
    CROW_ROUTE(app_gzip, "/test_compress")([&](){
        return expected_string;
    });

    CROW_ROUTE(app_gzip, "/test")([&](const request&, response& res){
        res.compressed = false;

        res.body = expected_string;
        res.end();
    });

    auto t1 = async(launch::async, [&]{ app_deflate.bindaddr(LOCALHOST_ADDRESS).port(45451).use_compression(compression::algorithm::DEFLATE).run(); });
    auto t2 = async(launch::async, [&]{ app_gzip.bindaddr(LOCALHOST_ADDRESS).port(45452).use_compression(compression::algorithm::GZIP).run(); });

    app_deflate.wait_for_server_start();
    app_gzip.wait_for_server_start();

    std::string test_compress_msg = "GET /test_compress\r\nAccept-Encoding: gzip, deflate\r\n\r\n";
    std::string test_compress_no_header_msg = "GET /test_compress\r\n\r\n";
    std::string test_none_msg = "GET /test\r\n\r\n";

    auto inflate_string = [](std::string const & deflated_string) -> const std::string
    {
        std::string inflated_string;
        Bytef tmp[8192];

        z_stream zstream{};
        zstream.avail_in = deflated_string.size();
        // Nasty const_cast but zlib won't alter its contents
        zstream.next_in = const_cast<Bytef *>(reinterpret_cast<Bytef const *>(deflated_string.c_str()));
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

    std::string response_deflate;
    std::string response_gzip;
    std::string response_deflate_no_header;
    std::string response_gzip_no_header;
    std::string response_deflate_none;
    std::string response_gzip_none;

    auto on_body = [](http_parser * parser, const char * body, size_t body_length) -> int
    {
        std::string * body_ptr = reinterpret_cast<std::string *>(parser->data);
        *body_ptr = std::string(body, body + body_length);

        return 0;
    };

    http_parser_settings settings{};
    settings.on_body = on_body;

    asio::io_service is;
    {
        // Compression
        {
            asio::ip::tcp::socket socket[2] = { asio::ip::tcp::socket(is), asio::ip::tcp::socket(is) };
            socket[0].connect(asio::ip::tcp::endpoint(asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));
            socket[1].connect(asio::ip::tcp::endpoint(asio::ip::address::from_string(LOCALHOST_ADDRESS), 45452));

            socket[0].send(asio::buffer(test_compress_msg));
            socket[1].send(asio::buffer(test_compress_msg));

            size_t bytes_deflate = socket[0].receive(asio::buffer(buf_deflate, 2048));
            size_t bytes_gzip = socket[1].receive(asio::buffer(buf_gzip, 2048));

            http_parser parser[2] = { {}, {} };
            http_parser_init(&parser[0], HTTP_RESPONSE);
            http_parser_init(&parser[1], HTTP_RESPONSE);
            parser[0].data = reinterpret_cast<void *>(&response_deflate);
            parser[1].data = reinterpret_cast<void *>(&response_gzip);

            http_parser_execute(&parser[0], &settings, buf_deflate, bytes_deflate);
            http_parser_execute(&parser[1], &settings, buf_gzip, bytes_gzip);

            response_deflate = inflate_string(response_deflate);
            response_gzip = inflate_string(response_gzip);

            socket[0].close();
            socket[1].close();
        }
        // No Header (thus no compression)
        {
            asio::ip::tcp::socket socket[2] = { asio::ip::tcp::socket(is), asio::ip::tcp::socket(is) };
            socket[0].connect(asio::ip::tcp::endpoint(asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));
            socket[1].connect(asio::ip::tcp::endpoint(asio::ip::address::from_string(LOCALHOST_ADDRESS), 45452));

            socket[0].send(asio::buffer(test_compress_no_header_msg));
            socket[1].send(asio::buffer(test_compress_no_header_msg));

            size_t bytes_deflate = socket[0].receive(asio::buffer(buf_deflate, 2048));
            size_t bytes_gzip = socket[1].receive(asio::buffer(buf_gzip, 2048));

            http_parser parser[2] = { {}, {} };
            http_parser_init(&parser[0], HTTP_RESPONSE);
            http_parser_init(&parser[1], HTTP_RESPONSE);
            parser[0].data = reinterpret_cast<void *>(&response_deflate_no_header);
            parser[1].data = reinterpret_cast<void *>(&response_gzip_no_header);

            http_parser_execute(&parser[0], &settings, buf_deflate, bytes_deflate);
            http_parser_execute(&parser[1], &settings, buf_gzip, bytes_gzip);

            socket[0].close();
            socket[1].close();
        }
        // No compression
        {
            asio::ip::tcp::socket socket[2] = { asio::ip::tcp::socket(is), asio::ip::tcp::socket(is) };
            socket[0].connect(asio::ip::tcp::endpoint(asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));
            socket[1].connect(asio::ip::tcp::endpoint(asio::ip::address::from_string(LOCALHOST_ADDRESS), 45452));

            socket[0].send(asio::buffer(test_none_msg));
            socket[1].send(asio::buffer(test_none_msg));

            size_t bytes_deflate = socket[0].receive(asio::buffer(buf_deflate, 2048));
            size_t bytes_gzip = socket[1].receive(asio::buffer(buf_gzip, 2048));

            http_parser parser[2] = { {}, {} };
            http_parser_init(&parser[0], HTTP_RESPONSE);
            http_parser_init(&parser[1], HTTP_RESPONSE);
            parser[0].data = reinterpret_cast<void *>(&response_deflate_none);
            parser[1].data = reinterpret_cast<void *>(&response_gzip_none);

            http_parser_execute(&parser[0], &settings, buf_deflate, bytes_deflate);
            http_parser_execute(&parser[1], &settings, buf_gzip, bytes_gzip);

            socket[0].close();
            socket[1].close();
        }
    }
    {
        CHECK(expected_string == response_deflate);
        CHECK(expected_string == response_gzip);

        CHECK(expected_string == response_deflate_no_header);
        CHECK(expected_string == response_gzip_no_header);

        CHECK(expected_string == response_deflate_none);
        CHECK(expected_string == response_gzip_none);
    }

    app_deflate.stop();
    app_gzip.stop();
}
#endif

TEST_CASE("catchall")
{
    SimpleApp app;
    SimpleApp app2;

    CROW_ROUTE(app, "/place")([](){return "place";});

    CROW_CATCHALL_ROUTE(app)([](response& res){res.body = "!place";});

    CROW_ROUTE(app2, "/place")([](){return "place";});

    app.validate();
    app2.validate();

    {
      request req;
      response res;

      req.url = "/place";

      app.handle(req, res);

      CHECK(200 == res.code);
    }

    {
      request req;
      response res;

      req.url = "/another_place";

      app.handle(req, res);

      CHECK(404 == res.code);
      CHECK("!place" == res.body);
    }

    {
      request req;
      response res;

      req.url = "/place";

      app2.handle(req, res);

      CHECK(200 == res.code);
    }

    {
      request req;
      response res;

      req.url = "/another_place";

      app2.handle(req, res);

      CHECK(404 == res.code);
    }
}

TEST_CASE("blueprint")
{
    SimpleApp app;
    crow::Blueprint bp("bp_prefix", "cstat", "ctemplate");
    crow::Blueprint bp_not_sub("bp_prefix_second");
    crow::Blueprint sub_bp("bp2", "csstat", "cstemplate");
    crow::Blueprint sub_sub_bp("bp3");

    CROW_BP_ROUTE(sub_bp, "/hello")
    ([]() {
        return "Hello world!";
    });

    CROW_BP_ROUTE(bp_not_sub, "/hello")
    ([]() {
        return "Hello world!";
    });

    CROW_BP_ROUTE(sub_sub_bp, "/hi")
    ([]() {
        return "Hi world!";
    });

    CROW_BP_CATCHALL_ROUTE(sub_bp)([](){return response(200, "WRONG!!");});

    app.   register_blueprint(bp);
    app.   register_blueprint(bp_not_sub);
    bp.    register_blueprint(sub_bp);
    sub_bp.register_blueprint(sub_sub_bp);

    app.validate();

    {
      request req;
      response res;

      req.url = "/bp_prefix/bp2/hello";

      app.handle(req, res);

      CHECK("Hello world!" == res.body);
    }

    {
      request req;
      response res;

      req.url = "/bp_prefix_second/hello";

      app.handle(req, res);

      CHECK("Hello world!" == res.body);
    }

    {
      request req;
      response res;

      req.url = "/bp_prefix/bp2/bp3/hi";

      app.handle(req, res);

      CHECK("Hi world!" == res.body);
    }

    {
      request req;
      response res;

      req.url = "/bp_prefix/nonexistent";

      app.handle(req, res);

      CHECK(404 == res.code);
    }

    {
      request req;
      response res;

      req.url = "/bp_prefix_second/nonexistent";

      app.handle(req, res);

      CHECK(404 == res.code);
    }

    {
      request req;
      response res;

      req.url = "/bp_prefix/bp2/nonexistent";

      app.handle(req, res);

      CHECK(200 == res.code);
      CHECK("WRONG!!" == res.body);
    }

    {
      request req;
      response res;

      req.url = "/bp_prefix/bp2/bp3/nonexistent";

      app.handle(req, res);

      CHECK(200 == res.code);
      CHECK("WRONG!!" == res.body);
    }
}
