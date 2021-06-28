/*
Copyright (c) 2014-2017, ipkn
              2020-2021, CrowCpp
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the author nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

#include <vector>
#include <string>
#include <stdexcept>
#include <iostream>
#include "crow/utility.h"

namespace crow
{
    enum class HTTPMethod
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

    inline std::string method_name(HTTPMethod method)
    {
        switch(method)
        {
            case HTTPMethod::Delete:
                return "DELETE";
            case HTTPMethod::Get:
                return "GET";
            case HTTPMethod::Head:
                return "HEAD";
            case HTTPMethod::Post:
                return "POST";
            case HTTPMethod::Put:
                return "PUT";
            case HTTPMethod::Connect:
                return "CONNECT";
            case HTTPMethod::Options:
                return "OPTIONS";
            case HTTPMethod::Trace:
                return "TRACE";
            case HTTPMethod::Patch:
                return "PATCH";
            case HTTPMethod::Purge:
                return "PURGE";
            default:
                return "invalid";
        }
        return "invalid";
    }

    enum class ParamType
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
            for(auto i:int_params)
                std::cerr<<i <<", " ;
            std::cerr<<std::endl;
            for(auto i:uint_params)
                std::cerr<<i <<", " ;
            std::cerr<<std::endl;
            for(auto i:double_params)
                std::cerr<<i <<", " ;
            std::cerr<<std::endl;
            for(auto& i:string_params)
                std::cerr<<i <<", " ;
            std::cerr<<std::endl;
        }

        template <typename T>
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
}

#ifndef CROW_MSVC_WORKAROUND
constexpr crow::HTTPMethod operator "" _method(const char* str, size_t /*len*/)
{
    return
        crow::black_magic::is_equ_p(str, "GET", 3) ? crow::HTTPMethod::Get :
        crow::black_magic::is_equ_p(str, "DELETE", 6) ? crow::HTTPMethod::Delete :
        crow::black_magic::is_equ_p(str, "HEAD", 4) ? crow::HTTPMethod::Head :
        crow::black_magic::is_equ_p(str, "POST", 4) ? crow::HTTPMethod::Post :
        crow::black_magic::is_equ_p(str, "PUT", 3) ? crow::HTTPMethod::Put :
        crow::black_magic::is_equ_p(str, "OPTIONS", 7) ? crow::HTTPMethod::Options :
        crow::black_magic::is_equ_p(str, "CONNECT", 7) ? crow::HTTPMethod::Connect :
        crow::black_magic::is_equ_p(str, "TRACE", 5) ? crow::HTTPMethod::Trace :
        crow::black_magic::is_equ_p(str, "PATCH", 5) ? crow::HTTPMethod::Patch :
        crow::black_magic::is_equ_p(str, "PURGE", 5) ? crow::HTTPMethod::Purge :
        throw std::runtime_error("invalid http method");
}
#endif
