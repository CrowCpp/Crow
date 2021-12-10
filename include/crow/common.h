#pragma once

#include <vector>
#include <string>
#include <stdexcept>
#include <iostream>
#include "crow/utility.h"

namespace crow
{
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


        InternalMethodCount,
        // should not add an item below this line: used for array count
    };

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
        VARIANT_ALSO_NEGOTIATES       = 506
    };

    inline std::string method_name(HTTPMethod method)
    {
        switch(method)
        {
            case HTTPMethod::Delete: return "DELETE";
            case HTTPMethod::Get: return "GET";
            case HTTPMethod::Head: return "HEAD";
            case HTTPMethod::Post: return "POST";
            case HTTPMethod::Put: return "PUT";
            case HTTPMethod::Connect: return "CONNECT";
            case HTTPMethod::Options: return "OPTIONS";
            case HTTPMethod::Trace: return "TRACE";
            case HTTPMethod::Patch: return "PATCH";
            case HTTPMethod::Purge: return "PURGE";
            default: return "invalid";
        }
        return "invalid";
    }

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
} // namespace crow

#ifndef CROW_MSVC_WORKAROUND
constexpr crow::HTTPMethod operator"" _method(const char* str, size_t /*len*/)
{
    return crow::black_magic::is_equ_p(str, "GET", 3)     ? crow::HTTPMethod::Get :
           crow::black_magic::is_equ_p(str, "DELETE", 6)  ? crow::HTTPMethod::Delete :
           crow::black_magic::is_equ_p(str, "HEAD", 4)    ? crow::HTTPMethod::Head :
           crow::black_magic::is_equ_p(str, "POST", 4)    ? crow::HTTPMethod::Post :
           crow::black_magic::is_equ_p(str, "PUT", 3)     ? crow::HTTPMethod::Put :
           crow::black_magic::is_equ_p(str, "OPTIONS", 7) ? crow::HTTPMethod::Options :
           crow::black_magic::is_equ_p(str, "CONNECT", 7) ? crow::HTTPMethod::Connect :
           crow::black_magic::is_equ_p(str, "TRACE", 5)   ? crow::HTTPMethod::Trace :
           crow::black_magic::is_equ_p(str, "PATCH", 5)   ? crow::HTTPMethod::Patch :
           crow::black_magic::is_equ_p(str, "PURGE", 5)   ? crow::HTTPMethod::Purge :
                                                            throw std::runtime_error("invalid http method");
}
#endif
