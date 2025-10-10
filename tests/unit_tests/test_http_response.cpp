#include "catch2/catch_all.hpp"

#include "crow.h"

using namespace crow;

TEST_CASE("custom_content_types")
{
    // standard behaviour: content type is a key of mime_types
    CHECK("text/html" == response("html", "").get_header_value("Content-Type"));
    CHECK("image/jpeg" == response("jpg", "").get_header_value("Content-Type"));
    CHECK("video/mpeg" == response("mpg", "").get_header_value("Content-Type"));

    // content type is already a valid mime type
    CHECK("text/csv" == response("text/csv", "").get_header_value("Content-Type"));
    CHECK("application/xhtml+xml" == response("application/xhtml+xml", "").get_header_value("Content-Type"));
    CHECK("font/custom;parameters=ok" == response("font/custom;parameters=ok", "").get_header_value("Content-Type"));

    // content type looks like a mime type, but is invalid
    // note: RFC6838 only allows a limited set of parent types:
    // https://datatracker.ietf.org/doc/html/rfc6838#section-4.2.7
    //
    // These types are: application, audio, font, example, image, message,
    //                  model, multipart, text, video

    CHECK("text/plain" == response("custom/type", "").get_header_value("Content-Type"));

    // content type does not look like a mime type.
    CHECK("text/plain" == response("notarealextension", "").get_header_value("Content-Type"));
    CHECK("text/plain" == response("image/", "").get_header_value("Content-Type"));
    CHECK("text/plain" == response("/json", "").get_header_value("Content-Type"));

} // custom_content_types

TEST_CASE("simple_response")
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
}