#include "catch2/catch_all.hpp"

#include "crow.h"

#include <memory>

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
    CHECK(505 == response(static_cast<int>(status::HTTP_VERSION_NOT_SUPPORTED)).code);

    CHECK(100 == response(100, "xml", "").code);
    CHECK("text/xml" == response(100, "xml", "").get_header_value("Content-Type"));
    CHECK(200 == response(200, "html", "").code);
    CHECK("text/html" == response(200, "html", "").get_header_value("Content-Type"));
    CHECK(500 == response(500, "html", "Internal Error?").code);
    CHECK("text/css" == response(500, "css", "Internal Error?").get_header_value("Content-Type"));
}

TEST_CASE("clear_restores_ordinary_body_framing_after_chunk_provider")
{
    SECTION("synchronous provider")
    {
        response res;
        res.set_chunked_content_provider([](std::string&) {
            return response::chunk_result::done;
        });

        REQUIRE(res.manual_length_header);
        res.clear();
        res.write("sync-body");
        res.end();

        CHECK_FALSE(res.manual_length_header);
        CHECK_FALSE(res.is_chunked_type());
        CHECK(res.get_header_value("Transfer-Encoding").empty());
        CHECK(res.body == "sync-body");
        CHECK(res.is_completed());
    }

    SECTION("asynchronous provider")
    {
        response res;
        res.set_async_chunked_content_provider(
          [](response::async_chunk_completion_t complete) {
              complete(response::chunk_result::done, "");
          });

        REQUIRE(res.manual_length_header);
        res.clear();
        res.write("async-body");
        res.end();

        CHECK_FALSE(res.manual_length_header);
        CHECK_FALSE(res.is_chunked_type());
        CHECK(res.get_header_value("Transfer-Encoding").empty());
        CHECK(res.body == "async-body");
        CHECK(res.is_completed());
    }
}

TEST_CASE("async_chunked_provider_owns_the_response_body_source") {
    SECTION("installation replaces static file and string body") {
        response res;
        res.set_static_file_info("tests/img/cat.jpg");
        res.body = "leftover";

        response::async_chunk_provider_t provider
            = [](response::async_chunk_completion_t complete) { complete(response::chunk_result::done, ""); };
        res.set_async_chunked_content_provider(std::move(provider), "text/plain");

        CHECK(res.is_chunked_type());
        CHECK(!res.is_static_type());
        CHECK(res.body.empty());
        CHECK(res.get_header_value("Content-Length").empty());
        CHECK(res.get_header_value("Transfer-Encoding") == "chunked");
        CHECK(res.get_header_value("Content-Type") == "text/plain");
    }

    SECTION("installation releases synchronous provider") {
        response res;
        auto synchronous_marker                        = std::make_shared<int>(1);
        std::weak_ptr<int> synchronous_marker_observer = synchronous_marker;
        res.set_chunked_content_provider([synchronous_marker](std::string&) { return response::chunk_result::done; });
        synchronous_marker.reset();

        res.set_async_chunked_content_provider(
            [](response::async_chunk_completion_t complete) { complete(response::chunk_result::done, ""); });

        CHECK(synchronous_marker_observer.expired());
    }

    SECTION("an empty callable still selects the asynchronous body source")
    {
        response source;
        response::async_chunk_provider_t empty_provider;

        source.set_async_chunked_content_provider(std::move(empty_provider));

        CHECK(source.is_chunked_type());
        CHECK(source.get_header_value("Transfer-Encoding") == "chunked");

        response destination(std::move(source));

        CHECK(destination.is_chunked_type());
        CHECK(!source.is_chunked_type());

        destination.clear();

        CHECK(!destination.is_chunked_type());
    }

    SECTION("static file releases asynchronous provider") {
        response res;
        auto asynchronous_marker                        = std::make_shared<int>(1);
        std::weak_ptr<int> asynchronous_marker_observer = asynchronous_marker;
        res.set_async_chunked_content_provider([asynchronous_marker](response::async_chunk_completion_t complete) {
            complete(response::chunk_result::done, "");
        });
        asynchronous_marker.reset();

        res.set_static_file_info("tests/img/cat.jpg");

        CHECK(asynchronous_marker_observer.expired());
        CHECK(!res.is_chunked_type());
        CHECK(res.is_static_type());
        CHECK(res.get_header_value("Transfer-Encoding").empty());
    }

    SECTION("synchronous provider releases asynchronous provider") {
        response res;
        auto asynchronous_marker                        = std::make_shared<int>(1);
        std::weak_ptr<int> asynchronous_marker_observer = asynchronous_marker;
        res.set_async_chunked_content_provider([asynchronous_marker](response::async_chunk_completion_t complete) {
            complete(response::chunk_result::done, "");
        });
        asynchronous_marker.reset();

        res.set_chunked_content_provider([](std::string&) { return response::chunk_result::done; });

        CHECK(asynchronous_marker_observer.expired());
        CHECK(res.is_chunked_type());
    }

    SECTION("clear releases moved asynchronous provider") {
        response source;
        auto asynchronous_marker                        = std::make_shared<int>(1);
        std::weak_ptr<int> asynchronous_marker_observer = asynchronous_marker;
        source.set_async_chunked_content_provider([asynchronous_marker](response::async_chunk_completion_t complete) {
            complete(response::chunk_result::done, "");
        });
        asynchronous_marker.reset();

        response destination(std::move(source));

        CHECK(destination.is_chunked_type());
        CHECK(!asynchronous_marker_observer.expired());

        destination.clear();

        CHECK(!destination.is_chunked_type());
        CHECK(asynchronous_marker_observer.expired());
        CHECK(destination.get_header_value("Transfer-Encoding").empty());
    }
}
