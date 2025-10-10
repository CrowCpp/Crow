#include "catch2/catch_all.hpp"

#include "crow.h"

using namespace std;
using namespace crow;

TEST_CASE("base64")
{
    unsigned char sample_bin[] = {0x14, 0xfb, 0x9c, 0x03, 0xd9, 0x7e};
    std::string sample_bin_str(reinterpret_cast<char const*>(sample_bin),
                               sizeof(sample_bin) / sizeof(sample_bin[0]));
    std::string sample_bin_enc = "FPucA9l+";
    std::string sample_bin_enc_url = "FPucA9l-";
    unsigned char sample_bin1[] = {0x14, 0xfb, 0x9c, 0x03, 0xd9};
    std::string sample_bin1_str(reinterpret_cast<char const*>(sample_bin1),
                                sizeof(sample_bin1) / sizeof(sample_bin1[0]));
    std::string sample_bin1_enc = "FPucA9k=";
    std::string sample_bin1_enc_np = "FPucA9k";
    unsigned char sample_bin2[] = {0x14, 0xfb, 0x9c, 0x03};
    std::string sample_bin2_str(reinterpret_cast<char const*>(sample_bin2),
                                sizeof(sample_bin2) / sizeof(sample_bin2[0]));
    std::string sample_bin2_enc = "FPucAw==";
    std::string sample_bin2_enc_np = "FPucAw";
    std::string sample_text = "Crow Allows users to handle requests that may not come from the network. This is done by calling the handle(req, res) method and providing a request and response objects. Which causes crow to identify and run the appropriate handler, returning the resulting response.";
    std::string sample_base64 = "Q3JvdyBBbGxvd3MgdXNlcnMgdG8gaGFuZGxlIHJlcXVlc3RzIHRoYXQgbWF5IG5vdCBjb21lIGZyb20gdGhlIG5ldHdvcmsuIFRoaXMgaXMgZG9uZSBieSBjYWxsaW5nIHRoZSBoYW5kbGUocmVxLCByZXMpIG1ldGhvZCBhbmQgcHJvdmlkaW5nIGEgcmVxdWVzdCBhbmQgcmVzcG9uc2Ugb2JqZWN0cy4gV2hpY2ggY2F1c2VzIGNyb3cgdG8gaWRlbnRpZnkgYW5kIHJ1biB0aGUgYXBwcm9wcmlhdGUgaGFuZGxlciwgcmV0dXJuaW5nIHRoZSByZXN1bHRpbmcgcmVzcG9uc2Uu";


    CHECK(crow::utility::base64encode(sample_text, sample_text.length()) == sample_base64);
    CHECK(crow::utility::base64encode(sample_bin, 6) == sample_bin_enc);
    CHECK(crow::utility::base64encode_urlsafe(sample_bin, 6) == sample_bin_enc_url);
    CHECK(crow::utility::base64encode(sample_bin1, 5) == sample_bin1_enc);
    CHECK(crow::utility::base64encode(sample_bin2, 4) == sample_bin2_enc);

    CHECK(crow::utility::base64decode(sample_base64) == sample_text);
    CHECK(crow::utility::base64decode(sample_base64, sample_base64.length()) == sample_text);
    CHECK(crow::utility::base64decode(sample_bin_enc, 8) == sample_bin_str);
    CHECK(crow::utility::base64decode(sample_bin_enc_url, 8) == sample_bin_str);
    CHECK(crow::utility::base64decode(sample_bin1_enc, 8) == sample_bin1_str);
    CHECK(crow::utility::base64decode(sample_bin1_enc_np, 7) == sample_bin1_str);
    CHECK(crow::utility::base64decode(sample_bin2_enc, 8) == sample_bin2_str);
    CHECK(crow::utility::base64decode(sample_bin2_enc_np, 6) == sample_bin2_str);
} // base64

TEST_CASE("normalize_path")
{
    CHECK(crow::utility::normalize_path("/abc/def") == "/abc/def/");
    CHECK(crow::utility::normalize_path("path\\to\\directory") == "path/to/directory/");
} // normalize_path

TEST_CASE("sanitize_filename")
{
    auto sanitize_filename = [](string s) {
        crow::utility::sanitize_filename(s);
        return s;
    };
    CHECK(sanitize_filename("abc/def") == "abc/def");
    CHECK(sanitize_filename("abc/../def") == "abc/_/def");
    CHECK(sanitize_filename("abc/..\\..\\..//.../def") == "abc/_\\_\\_//_./def");
    CHECK(sanitize_filename("abc/..../def") == "abc/_../def");
    CHECK(sanitize_filename("abc/x../def") == "abc/x../def");
    CHECK(sanitize_filename("../etc/passwd") == "_/etc/passwd");
    CHECK(sanitize_filename("abc/AUX") == "abc/_");
    CHECK(sanitize_filename("abc/AUX/foo") == "abc/_/foo");
    CHECK(sanitize_filename("abc/AUX:") == "abc/__");
    CHECK(sanitize_filename("abc/AUXxy") == "abc/AUXxy");
    CHECK(sanitize_filename("abc/AUX.xy") == "abc/_.xy");
    CHECK(sanitize_filename("abc/NUL") == "abc/_");
    CHECK(sanitize_filename("abc/NU") == "abc/NU");
    CHECK(sanitize_filename("abc/NuL") == "abc/_");
    CHECK(sanitize_filename("abc/LPT1\\") == "abc/_\\");
    CHECK(sanitize_filename("abc/COM1") == "abc/_");
    CHECK(sanitize_filename("ab?<>:*|\"cd") == "ab_______cd");
    CHECK(sanitize_filename("abc/COM9") == "abc/_");
    CHECK(sanitize_filename("abc/COM") == "abc/COM");
    CHECK(sanitize_filename("abc/CON") == "abc/_");
    CHECK(sanitize_filename("/abc/") == "_abc/");
}

TEST_CASE("trim")
{
    CHECK(utility::trim("") == "");
    CHECK(utility::trim("0") == "0");
    CHECK(utility::trim(" a") == "a");
    CHECK(utility::trim("b ") == "b");
    CHECK(utility::trim(" c ") == "c");
    CHECK(utility::trim(" a b ") == "a b");
    CHECK(utility::trim("   ") == "");
}

TEST_CASE("string_equals")
{
    CHECK(utility::string_equals("a", "aa") == false);
    CHECK(utility::string_equals("a", "b") == false);
    CHECK(utility::string_equals("", "") == true);
    CHECK(utility::string_equals("abc", "abc") == true);
    CHECK(utility::string_equals("ABC", "abc") == true);

    CHECK(utility::string_equals("a", "aa", true) == false);
    CHECK(utility::string_equals("a", "b", true) == false);
    CHECK(utility::string_equals("", "", true) == true);
    CHECK(utility::string_equals("abc", "abc", true) == true);
    CHECK(utility::string_equals("ABC", "abc", true) == false);
}

TEST_CASE("lexical_cast")
{
    CHECK(utility::lexical_cast<int>(4) == 4);
    CHECK(utility::lexical_cast<double>(4) == 4.0);
    CHECK(utility::lexical_cast<int>("5") == 5);
    CHECK(utility::lexical_cast<string>(4) == "4");
    CHECK(utility::lexical_cast<float>("10", 2) == 10.0f);
}
