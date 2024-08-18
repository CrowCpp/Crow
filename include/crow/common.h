#pragma once

#include <vector>
#include <string>
#include <stdexcept>
#include <iostream>
#include "crow/utility.h"

namespace crow
{
    const char cr = '\r';
    const char lf = '\n';
    const std::string crlf("\r\n");

    enum class HTTPMethod : char
    {
#ifndef DELETE
        DELETE = 0,
        GET,
        HEAD,
        POST,
        PUT,

        CONNECT,
        OPTIONS,
        TRACE,

        PATCH,
        PURGE,

        COPY,
        LOCK,
        MKCOL,
        MOVE,
        PROPFIND,
        PROPPATCH,
        SEARCH,
        UNLOCK,
        BIND,
        REBIND,
        UNBIND,
        ACL,

        REPORT,
        MKACTIVITY,
        CHECKOUT,
        MERGE,

        MSEARCH,
        NOTIFY,
        SUBSCRIBE,
        UNSUBSCRIBE,

        MKCALENDAR,

        LINK,
        UNLINK,

        SOURCE,
#endif

        Delete = 0,
        Get,
        Head,
        Post,
        Put,

        Connect,
        Options,
        Trace,

        Patch,
        Purge,

        Copy,
        Lock,
        MkCol,
        Move,
        Propfind,
        Proppatch,
        Search,
        Unlock,
        Bind,
        Rebind,
        Unbind,
        Acl,

        Report,
        MkActivity,
        Checkout,
        Merge,

        MSearch,
        Notify,
        Subscribe,
        Unsubscribe,

        MkCalendar,

        Link,
        Unlink,

        Source,


        InternalMethodCount,
        // should not add an item below this line: used for array count
    };

    constexpr const char* method_strings[] =
      {
        "DELETE",
        "GET",
        "HEAD",
        "POST",
        "PUT",

        "CONNECT",
        "OPTIONS",
        "TRACE",

        "PATCH",
        "PURGE",

        "COPY",
        "LOCK",
        "MKCOL",
        "MOVE",
        "PROPFIND",
        "PROPPATCH",
        "SEARCH",
        "UNLOCK",
        "BIND",
        "REBIND",
        "UNBIND",
        "ACL",

        "REPORT",
        "MKACTIVITY",
        "CHECKOUT",
        "MERGE",

        "M-SEARCH",
        "NOTIFY",
        "SUBSCRIBE",
        "UNSUBSCRIBE",

        "MKCALENDAR",

        "LINK",
        "UNLINK",

        "SOURCE"};


    inline std::string method_name(HTTPMethod method)
    {
        if (CROW_LIKELY(method < HTTPMethod::InternalMethodCount))
        {
            return method_strings[static_cast<unsigned int>(method)];
        }
        return "invalid";
    }

    // clang-format off

    enum status
    {
        CONTINUE                      = 100,
        SWITCHING_PROTOCOLS           = 101,

        OK                            = 200,
        CREATED                       = 201,
        ACCEPTED                      = 202,
        NON_AUTHORITATIVE_INFORMATION = 203,
        NO_CONTENT                    = 204,
        RESET_CONTENT                 = 205,
        PARTIAL_CONTENT               = 206,

        MULTIPLE_CHOICES              = 300,
        MOVED_PERMANENTLY             = 301,
        FOUND                         = 302,
        SEE_OTHER                     = 303,
        NOT_MODIFIED                  = 304,
        TEMPORARY_REDIRECT            = 307,
        PERMANENT_REDIRECT            = 308,

        BAD_REQUEST                   = 400,
        UNAUTHORIZED                  = 401,
        FORBIDDEN                     = 403,
        NOT_FOUND                     = 404,
        METHOD_NOT_ALLOWED            = 405,
        NOT_ACCEPTABLE                = 406,
        PROXY_AUTHENTICATION_REQUIRED = 407,
        CONFLICT                      = 409,
        GONE                          = 410,
        PAYLOAD_TOO_LARGE             = 413,
        UNSUPPORTED_MEDIA_TYPE        = 415,
        RANGE_NOT_SATISFIABLE         = 416,
        EXPECTATION_FAILED            = 417,
        PRECONDITION_REQUIRED         = 428,
        TOO_MANY_REQUESTS             = 429,
        UNAVAILABLE_FOR_LEGAL_REASONS = 451,

        INTERNAL_SERVER_ERROR         = 500,
        NOT_IMPLEMENTED               = 501,
        BAD_GATEWAY                   = 502,
        SERVICE_UNAVAILABLE           = 503,
        GATEWAY_TIMEOUT               = 504,
        VARIANT_ALSO_NEGOTIATES       = 506
    };

    // clang-format on

    enum class ParamType : char
    {
        INT,
        UINT,
        DOUBLE,
        STRING,
        PATH,

        MAX
    };

    /// @cond SKIP
    struct routing_params
    {
        std::vector<int64_t> int_params;
        std::vector<uint64_t> uint_params;
        std::vector<double> double_params;
        std::vector<std::string> string_params;

        void debug_print() const
        {
            std::cerr << "routing_params" << std::endl;
            for (auto i : int_params)
                std::cerr << i << ", ";
            std::cerr << std::endl;
            for (auto i : uint_params)
                std::cerr << i << ", ";
            std::cerr << std::endl;
            for (auto i : double_params)
                std::cerr << i << ", ";
            std::cerr << std::endl;
            for (auto& i : string_params)
                std::cerr << i << ", ";
            std::cerr << std::endl;
        }

        template<typename T>
        T get(unsigned) const;
    };

    template<>
    inline int64_t routing_params::get<int64_t>(unsigned index) const
    {
        return int_params[index];
    }

    template<>
    inline uint64_t routing_params::get<uint64_t>(unsigned index) const
    {
        return uint_params[index];
    }

    template<>
    inline double routing_params::get<double>(unsigned index) const
    {
        return double_params[index];
    }

    template<>
    inline std::string routing_params::get<std::string>(unsigned index) const
    {
        return string_params[index];
    }
    /// @endcond

    struct routing_handle_result
    {
        uint16_t rule_index;
        std::vector<uint16_t> blueprint_indices;
        routing_params r_params;
        HTTPMethod method;

        routing_handle_result() {}

        routing_handle_result(uint16_t rule_index_, std::vector<uint16_t> blueprint_indices_, routing_params r_params_):
          rule_index(rule_index_),
          blueprint_indices(blueprint_indices_),
          r_params(r_params_) {}

        routing_handle_result(uint16_t rule_index_, std::vector<uint16_t> blueprint_indices_, routing_params r_params_, HTTPMethod method_):
          rule_index(rule_index_),
          blueprint_indices(blueprint_indices_),
          r_params(r_params_),
          method(method_) {}
    };
} // namespace crow

// clang-format off
#ifndef CROW_MSVC_WORKAROUND
constexpr crow::HTTPMethod method_from_string(const char* str)
{
    return crow::black_magic::is_equ_p(str, "GET", 3)    ? crow::HTTPMethod::Get :
           crow::black_magic::is_equ_p(str, "DELETE", 6) ? crow::HTTPMethod::Delete :
           crow::black_magic::is_equ_p(str, "HEAD", 4)   ? crow::HTTPMethod::Head :
           crow::black_magic::is_equ_p(str, "POST", 4)   ? crow::HTTPMethod::Post :
           crow::black_magic::is_equ_p(str, "PUT", 3)    ? crow::HTTPMethod::Put :

           crow::black_magic::is_equ_p(str, "OPTIONS", 7) ? crow::HTTPMethod::Options :
           crow::black_magic::is_equ_p(str, "CONNECT", 7) ? crow::HTTPMethod::Connect :
           crow::black_magic::is_equ_p(str, "TRACE", 5)   ? crow::HTTPMethod::Trace :

           crow::black_magic::is_equ_p(str, "PATCH", 5)     ? crow::HTTPMethod::Patch :
           crow::black_magic::is_equ_p(str, "PURGE", 5)     ? crow::HTTPMethod::Purge :
           crow::black_magic::is_equ_p(str, "COPY", 4)      ? crow::HTTPMethod::Copy :
           crow::black_magic::is_equ_p(str, "LOCK", 4)      ? crow::HTTPMethod::Lock :
           crow::black_magic::is_equ_p(str, "MKCOL", 5)     ? crow::HTTPMethod::MkCol :
           crow::black_magic::is_equ_p(str, "MOVE", 4)      ? crow::HTTPMethod::Move :
           crow::black_magic::is_equ_p(str, "PROPFIND", 8)  ? crow::HTTPMethod::Propfind :
           crow::black_magic::is_equ_p(str, "PROPPATCH", 9) ? crow::HTTPMethod::Proppatch :
           crow::black_magic::is_equ_p(str, "SEARCH", 6)    ? crow::HTTPMethod::Search :
           crow::black_magic::is_equ_p(str, "UNLOCK", 6)    ? crow::HTTPMethod::Unlock :
           crow::black_magic::is_equ_p(str, "BIND", 4)      ? crow::HTTPMethod::Bind :
           crow::black_magic::is_equ_p(str, "REBIND", 6)    ? crow::HTTPMethod::Rebind :
           crow::black_magic::is_equ_p(str, "UNBIND", 6)    ? crow::HTTPMethod::Unbind :
           crow::black_magic::is_equ_p(str, "ACL", 3)       ? crow::HTTPMethod::Acl :

           crow::black_magic::is_equ_p(str, "REPORT", 6)      ? crow::HTTPMethod::Report :
           crow::black_magic::is_equ_p(str, "MKACTIVITY", 10) ? crow::HTTPMethod::MkActivity :
           crow::black_magic::is_equ_p(str, "CHECKOUT", 8)    ? crow::HTTPMethod::Checkout :
           crow::black_magic::is_equ_p(str, "MERGE", 5)       ? crow::HTTPMethod::Merge :

           crow::black_magic::is_equ_p(str, "MSEARCH", 7)      ? crow::HTTPMethod::MSearch :
           crow::black_magic::is_equ_p(str, "NOTIFY", 6)       ? crow::HTTPMethod::Notify :
           crow::black_magic::is_equ_p(str, "SUBSCRIBE", 9)    ? crow::HTTPMethod::Subscribe :
           crow::black_magic::is_equ_p(str, "UNSUBSCRIBE", 11) ? crow::HTTPMethod::Unsubscribe :

           crow::black_magic::is_equ_p(str, "MKCALENDAR", 10) ? crow::HTTPMethod::MkCalendar :

           crow::black_magic::is_equ_p(str, "LINK", 4)   ? crow::HTTPMethod::Link :
           crow::black_magic::is_equ_p(str, "UNLINK", 6) ? crow::HTTPMethod::Unlink :

           crow::black_magic::is_equ_p(str, "SOURCE", 6) ? crow::HTTPMethod::Source :
                                                           throw std::runtime_error("invalid http method");
}

constexpr crow::HTTPMethod operator""_method(const char* str, size_t /*len*/)
{
    return method_from_string( str );
}
#endif
// clang-format on
