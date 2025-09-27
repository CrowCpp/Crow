#include "catch2/catch_all.hpp"

#include "crow.h"

using namespace std;
using namespace crow;

TEST_CASE("json_read", "[json]")
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
        for (auto s : json_error_tests)
        {
            auto x = json::load(s);
            if (x)
            {
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
    // TODO(ipkn) returning false is better than exception
    // ASSERT_THROW(3 == x["message"]);
    CHECK(12 == x["message"].size());

    std::string s =
      R"({"int":3,     "ints"  :[1,2,3,4,5],	"bigint":1234567890	})";
    auto y = json::load(s);
    CHECK(3 == y["int"]);
//    CHECK(3.0 == y["int"]);
//    CHECK(3.01 != y["int"]);
    CHECK(5 == y["ints"].size());
    CHECK(1 == y["ints"][0]);
    CHECK(2 == y["ints"][1]);
    CHECK(3 == y["ints"][2]);
    CHECK(4 == y["ints"][3]);
    CHECK(5 == y["ints"][4]);
    CHECK(1u == y["ints"][0]);
    REQUIRE_THAT(1.f, Catch::Matchers::WithinAbs(y["ints"][0].d(), 1e-9));

    int q = static_cast<int>(y["ints"][1]);
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
    REQUIRE_THAT(1.2, Catch::Matchers::WithinAbs(z["doubles"][0].d(), 1e-9));
    REQUIRE_THAT(-3.4 , Catch::Matchers::WithinAbs(z["doubles"][1].d(), 1e-9));

    std::string s3 = R"({"uint64": 18446744073709551615})";
    auto z1 = json::load(s3);
    CHECK(18446744073709551615ull == z1["uint64"].u());

    std::ostringstream os;
    os << z1["uint64"];
    CHECK("18446744073709551615" == os.str());
} // json_read

TEST_CASE("json_read_real", "[json]")
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
    for (auto x : v)
    {
        CROW_LOG_DEBUG << x;
        REQUIRE_THAT(json::load(x).d(), Catch::Matchers::WithinAbs(utility::lexical_cast<double>(x), 1e-9));
    }

    auto ret = json::load(
      R"---({"balloons":[{"mode":"ellipse","left":0.036303908355795146,"right":0.18320417789757412,"top":0.05319940476190476,"bottom":0.15224702380952382,"index":"0"},{"mode":"ellipse","left":0.3296201145552561,"right":0.47921580188679247,"top":0.05873511904761905,"bottom":0.1577827380952381,"index":"1"},{"mode":"ellipse","left":0.4996841307277628,"right":0.6425412735849056,"top":0.052113095238095236,"bottom":0.12830357142857143,"index":"2"},{"mode":"ellipse","left":0.7871041105121294,"right":0.954220013477089,"top":0.05869047619047619,"bottom":0.1625,"index":"3"},{"mode":"ellipse","left":0.8144794474393531,"right":0.9721613881401617,"top":0.1399404761904762,"bottom":0.24470238095238095,"index":"4"},{"mode":"ellipse","left":0.04527459568733154,"right":0.2096950808625337,"top":0.35267857142857145,"bottom":0.42791666666666667,"index":"5"},{"mode":"ellipse","left":0.855731974393531,"right":0.9352467991913747,"top":0.3816220238095238,"bottom":0.4282886904761905,"index":"6"},{"mode":"ellipse","left":0.39414167789757415,"right":0.5316079851752021,"top":0.3809375,"bottom":0.4571279761904762,"index":"7"},{"mode":"ellipse","left":0.03522995283018868,"right":0.1915641846361186,"top":0.6164136904761904,"bottom":0.7192708333333333,"index":"8"},{"mode":"ellipse","left":0.05675117924528302,"right":0.21308541105121293,"top":0.7045386904761904,"bottom":0.8016815476190476,"index":"9"}]})---");
    CHECK(ret);
} // json_read_real

TEST_CASE("json_read_unescaping", "[json]")
{
    {
        auto x = json::load(R"({"data":"\ud55c\n\t\r"})");
        if (!x)
        {
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
} // json_read_unescaping

TEST_CASE("json_read_string", "[json]")
{
    auto x = json::load(R"({"message": 53})");
    int y(x["message"]);
    std::string z(x["message"]);
    CHECK(53 == y);
    CHECK("53" == z);
} // json_read_string

TEST_CASE("json_read_container", "[json]")
{
    auto x = json::load(R"({"first": 53, "second": "55", "third": [5,6,7,8,3,2,1,4]})");
    CHECK(std::vector<std::string>({"first", "second", "third"}) == x.keys());
    CHECK(53 == int(x.lo()[0]));
    CHECK("55" == std::string(x.lo()[1]));
    CHECK(8 == int(x.lo()[2].lo()[3]));
} // json_read_container

TEST_CASE("json_write", "[json]")
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
} // json_write

TEST_CASE("json_write_with_indent", "[json]")
{
    static constexpr int IndentationLevelOne = 1;
    static constexpr int IndentationLevelTwo = 2;
    static constexpr int IndentationLevelFour = 4;

    json::wvalue y;

    y["scores"][0] = 1;
    y["scores"][1] = "king";
    y["scores"][2][0] = "real";
    y["scores"][2][1] = false;
    y["scores"][2][2] = true;

    CHECK(R"({
 "scores": [
  1,
  "king",
  [
   "real",
   false,
   true
  ]
 ]
})" == y.dump(IndentationLevelOne));

    CHECK(R"({
  "scores": [
    1,
    "king",
    [
      "real",
      false,
      true
    ]
  ]
})" == y.dump(IndentationLevelTwo));

    CHECK(R"({
    "scores": [
        1,
        "king",
        [
            "real",
            false,
            true
        ]
    ]
})" == y.dump(IndentationLevelFour));

    static constexpr char TabSeparator = '\t';

    // Note: The following string needs to use tabs!
    CHECK(R"({
	"scores": [
		1,
		"king",
		[
			"real",
			false,
			true
		]
	]
})" == y.dump(IndentationLevelOne, TabSeparator));
} // json_write_with_indent


TEST_CASE("json_copy_r_to_w_to_w_to_r", "[json]")
{
    json::rvalue r = json::load(
      R"({"smallint":2,"bigint":2147483647,"fp":23.43,"fpsc":2.343e1,"str":"a string","trueval":true,"falseval":false,"nullval":null,"listval":[1,2,"foo","bar"],"obj":{"member":23,"other":"baz"}})");
    json::wvalue v{r};
    json::wvalue w(v);
    json::rvalue x =
      json::load(w.dump()); // why no copy-ctor wvalue -> rvalue?
    CHECK(2 == x["smallint"]);
    CHECK(2147483647 == x["bigint"]);
    REQUIRE_THAT(23.43, Catch::Matchers::WithinAbs(x["fp"].d(), 1e-9));
    REQUIRE_THAT(23.43, Catch::Matchers::WithinAbs(x["fpsc"].d(), 1e-9));
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
} // json_copy_r_to_w_to_w_to_r
//TODO(EDev): maybe combine these

TEST_CASE("json::wvalue::wvalue(bool)", "[json]")
{
    CHECK(json::wvalue(true).t() == json::type::True);
    CHECK(json::wvalue(false).t() == json::type::False);
} // json::wvalue::wvalue(bool)

TEST_CASE("json::wvalue::wvalue(std::uint8_t)", "[json]")
{
    std::uint8_t i = 42;
    json::wvalue value = i;

    CHECK(value.t() == json::type::Number);
    CHECK(value.dump() == "42");
} // json::wvalue::wvalue(std::uint8_t)

TEST_CASE("json::wvalue::wvalue(std::uint16_t)", "[json]")
{
    std::uint16_t i = 42;
    json::wvalue value = i;

    CHECK(value.t() == json::type::Number);
    CHECK(value.dump() == "42");
} // json::wvalue::wvalue(std::uint16_t)

TEST_CASE("json::wvalue::wvalue(std::uint32_t)", "[json]")
{
    std::uint32_t i = 42;
    json::wvalue value = i;

    CHECK(value.t() == json::type::Number);
    CHECK(value.dump() == "42");
} // json::wvalue::wvalue(std::uint32_t)

TEST_CASE("json::wvalue::wvalue(std::uint64_t)", "[json]")
{
    std::uint64_t i = 42;
    json::wvalue value = i;

    CHECK(value.t() == json::type::Number);
    CHECK(value.dump() == "42");
} // json::wvalue::wvalue(std::uint64_t)

TEST_CASE("json::wvalue::wvalue(std::int8_t)", "[json]")
{
    std::int8_t i = -42;
    json::wvalue value = i;

    CHECK(value.t() == json::type::Number);
    CHECK(value.dump() == "-42");
} // json::wvalue::wvalue(std::int8_t)

TEST_CASE("json::wvalue::wvalue(std::int16_t)", "[json]")
{
    std::int16_t i = -42;
    json::wvalue value = i;

    CHECK(value.t() == json::type::Number);
    CHECK(value.dump() == "-42");
} // json::wvalue::wvalue(std::int16_t)

TEST_CASE("json::wvalue::wvalue(std::int32_t)", "[json]")
{
    std::int32_t i = -42;
    json::wvalue value = i;

    CHECK(value.t() == json::type::Number);
    CHECK(value.dump() == "-42");
} // json::wvalue::wvalue(std::int32_t)

TEST_CASE("json::wvalue::wvalue(std::int64_t)", "[json]")
{
    std::int64_t i = -42;
    json::wvalue value = i;

    CHECK(value.t() == json::type::Number);
    CHECK(value.dump() == "-42");
} // json::wvalue::wvalue(std::int64_t)

TEST_CASE("json::wvalue::wvalue(float)", "[json]")
{
    float f = 4.2;
    json::wvalue value = f;

    CHECK(value.t() == json::type::Number);
    CHECK(value.dump() == "4.2");
} // json::wvalue::wvalue(float)

TEST_CASE("json::wvalue::wvalue(double)", "[json]")
{
    double d = 0.036303908355795146;
    json::wvalue value = d;

    CHECK(value.t() == json::type::Number);
    auto dumped_value = value.dump();
    CROW_LOG_DEBUG << dumped_value;
    CHECK(std::abs(utility::lexical_cast<double>(dumped_value) - d) < numeric_limits<double>::epsilon());
} // json::wvalue::wvalue(double)

TEST_CASE("json::wvalue::wvalue(char const*)", "[json]")
{
    char const* str = "Hello world!";
    json::wvalue value = str;

    CHECK(value.t() == json::type::String);
    CHECK(value.dump() == "\"Hello world!\"");
} // json::wvalue::wvalue(char const*)

TEST_CASE("json::wvalue::wvalue(std::string const&)", "[json]")
{
    std::string str = "Hello world!";
    json::wvalue value = str;

    CHECK(value.t() == json::type::String);
    CHECK(value.dump() == "\"Hello world!\"");
} // json::wvalue::wvalue(std::string const&)

TEST_CASE("json::wvalue::wvalue(std::string&&)", "[json]")
{
    std::string str = "Hello world!";
    json::wvalue value = std::move(str);

    CHECK(value.t() == json::type::String);
    CHECK(value.dump() == "\"Hello world!\"");
} // json::wvalue::wvalue(std::string&&)

TEST_CASE("json::wvalue::wvalue(std::initializer_list<std::pair<std::string const, json::wvalue>>)", "[json]")
{
    json::wvalue integer, number, truth, lie, null;
    integer = 2147483647;
    number = 23.43;
    truth = true;
    lie = false;

    /* initializer-list constructor. */
    json::wvalue value({{"integer", integer},
                        {"number", number},
                        {"truth", truth},
                        {"lie", lie},
                        {"null", null}});

    CHECK(value["integer"].dump() == integer.dump());
    CHECK(value["number"].dump() == number.dump());
    CHECK(value["truth"].dump() == truth.dump());
    CHECK(value["lie"].dump() == lie.dump());
    CHECK(value["null"].dump() == null.dump());
} // json::wvalue::wvalue(std::initializer_list<std::pair<std::string const, json::wvalue>>)

TEST_CASE("json::wvalue::wvalue(std::[unordered_]map<std::string, json::wvalue> const&)", "[json]")
{
    json::wvalue integer, number, truth, lie, null;
    integer = 2147483647;
    number = 23.43;
    truth = true;
    lie = false;

    json::wvalue::object map({{"integer", integer},
                              {"number", number},
                              {"truth", truth},
                              {"lie", lie},
                              {"null", null}});

    json::wvalue value(map); /* copy-constructor. */

    CHECK(value["integer"].dump() == integer.dump());
    CHECK(value["number"].dump() == number.dump());
    CHECK(value["truth"].dump() == truth.dump());
    CHECK(value["lie"].dump() == lie.dump());
    CHECK(value["null"].dump() == null.dump());
} // json::wvalue::wvalue(std::[unordered_]map<std::string, json::wvalue> const&)

TEST_CASE("json::wvalue::wvalue(std::[unordered_]map<std::string, json::wvalue>&&)", "[json]")
{
    json::wvalue integer, number, truth, lie, null;
    integer = 2147483647;
    number = 23.43;
    truth = true;
    lie = false;

    json::wvalue::object map = {{{"integer", integer},
                                 {"number", number},
                                 {"truth", truth},
                                 {"lie", lie},
                                 {"null", null}}};

    json::wvalue value(std::move(map)); /* move constructor. */
    // json::wvalue value = std::move(map); /* move constructor. */

    CHECK(value["integer"].dump() == integer.dump());
    CHECK(value["number"].dump() == number.dump());
    CHECK(value["truth"].dump() == truth.dump());
    CHECK(value["lie"].dump() == lie.dump());
    CHECK(value["null"].dump() == null.dump());
} // json::wvalue::wvalue(std::[unordered_]map<std::string, json::wvalue>&&)

TEST_CASE("json::wvalue::operator=(std::initializer_list<std::pair<std::string const, json::wvalue>>)", "[json]")
{
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
      {"null", null}};

    CHECK(value["integer"].dump() == integer.dump());
    CHECK(value["number"].dump() == number.dump());
    CHECK(value["truth"].dump() == truth.dump());
    CHECK(value["lie"].dump() == lie.dump());
    CHECK(value["null"].dump() == null.dump());
} // json::wvalue::operator=(std::initializer_list<std::pair<std::string const, json::wvalue>>)

TEST_CASE("json::wvalue::operator=(std::[unordered_]map<std::string, json::wvalue> const&)", "[json]")
{
    json::wvalue integer, number, truth, lie, null;
    integer = 2147483647;
    number = 23.43;
    truth = true;
    lie = false;

    json::wvalue::object map({{"integer", integer},
                              {"number", number},
                              {"truth", truth},
                              {"lie", lie},
                              {"null", null}});

    json::wvalue value;
    value = map; /* copy assignment. */

    CHECK(value["integer"].dump() == integer.dump());
    CHECK(value["number"].dump() == number.dump());
    CHECK(value["truth"].dump() == truth.dump());
    CHECK(value["lie"].dump() == lie.dump());
    CHECK(value["null"].dump() == null.dump());
} // json::wvalue::operator=(std::[unordered_]map<std::string, json::wvalue> const&)

TEST_CASE("json::wvalue::operator=(std::[unordered_]map<std::string, json::wvalue>&&)", "[json]")
{
    json::wvalue integer, number, truth, lie, null;
    integer = 2147483647;
    number = 23.43;
    truth = true;
    lie = false;

    json::wvalue::object map({{"integer", integer},
                              {"number", number},
                              {"truth", truth},
                              {"lie", lie},
                              {"null", null}});

    json::wvalue value;
    value = std::move(map); /* move assignment. */

    CHECK(value["integer"].dump() == integer.dump());
    CHECK(value["number"].dump() == number.dump());
    CHECK(value["truth"].dump() == truth.dump());
    CHECK(value["lie"].dump() == lie.dump());
    CHECK(value["null"].dump() == null.dump());
} // json::wvalue::operator=(std::[unordered_]map<std::string, json::wvalue>&&)

TEST_CASE("json_vector", "[json]")
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
} // json_vector

TEST_CASE("json_list", "[json]")
{
    json::wvalue x(json::wvalue::list({5, 6, 7, 8, 4, 3, 2, 1}));

    CHECK(8 == x.size());
    CHECK("[5,6,7,8,4,3,2,1]" == x.dump());
} // json_list

static crow::json::wvalue getValue(int i){
     return crow::json::wvalue(i);
}

TEST_CASE("json Incorrect move of wvalue class #953", "[json]")
{
    {
        crow::json::wvalue int_value(-500);
        crow::json::wvalue copy_value(std::move(int_value));

        REQUIRE(copy_value.dump()=="-500");
    }
    {
         crow::json::wvalue json;
         json["int_value"] = getValue(-500);
         REQUIRE(json["int_value"].dump()=="-500");
    }
}

TEST_CASE("SmallNumber #1042", "[json]")
{
    crow::json::wvalue data;
    const double smallnumber = 1e-10;
    data["testValue"] = smallnumber;
    std::string text = data.dump( 4);
    const auto expected_text ="{\n    \"testValue\": 1e-10\n}";
    REQUIRE(text==expected_text);
}