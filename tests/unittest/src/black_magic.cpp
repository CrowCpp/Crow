#include "unittest.h"

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
} // ParameterTagging

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
} // black_magic
