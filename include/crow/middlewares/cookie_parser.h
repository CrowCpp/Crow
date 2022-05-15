#pragma once
#include <iomanip>
#include <boost/optional.hpp>
#include <boost/algorithm/string/trim.hpp>
#include "crow/http_request.h"
#include "crow/http_response.h"

namespace crow
{
    // Any middleware requires following 3 members:

    // struct context;
    //      storing data for the middleware; can be read from another middleware or handlers

    // before_handle
    //      called before handling the request.
    //      if res.end() is called, the operation is halted.
    //      (still call after_handle of this middleware)
    //      2 signatures:
    //      void before_handle(request& req, response& res, context& ctx)
    //          if you only need to access this middlewares context.
    //      template <typename AllContext>
    //      void before_handle(request& req, response& res, context& ctx, AllContext& all_ctx)
    //          you can access another middlewares' context by calling `all_ctx.template get<MW>()'
    //          ctx == all_ctx.template get<CurrentMiddleware>()

    // after_handle
    //      called after handling the request.
    //      void after_handle(request& req, response& res, context& ctx)
    //      template <typename AllContext>
    //      void after_handle(request& req, response& res, context& ctx, AllContext& all_ctx)

    struct CookieParser
    {
        // Cookie stores key, value and attributes
        struct Cookie
        {
            enum class SameSitePolicy
            {
                Strict,
                Lax,
                None
            };

            template<typename U>
            Cookie(const std::string& key, U&& value):
              Cookie()
            {
                key_ = key;
                value_ = std::forward<U>(value);
            }

            // format cookie to HTTP header format
            std::string dump() const
            {
                const static char* HTTP_DATE_FORMAT = "%a, %d %b %Y %H:%M:%S GMT";

                std::stringstream ss;
                ss << key_ << '=';
                ss << (value_.empty() ? "\"\"" : value_);
                dumpString(ss, !domain_.empty(), "Domain=", domain_);
                dumpString(ss, !path_.empty(), "Path=", path_);
                dumpString(ss, secure_, "Secure");
                dumpString(ss, httponly_, "HttpOnly");
                if (expires_at_)
                {
                    ss << DIVIDER << "Expires="
                       << std::put_time(expires_at_.get_ptr(), HTTP_DATE_FORMAT);
                }
                if (max_age_)
                {
                    ss << DIVIDER << "Max-Age=" << *max_age_;
                }
                if (same_site_)
                {
                    ss << DIVIDER << "SameSite=";
                    switch (*same_site_)
                    {
                        case SameSitePolicy::Strict:
                            ss << "Strict";
                            break;
                        case SameSitePolicy::Lax:
                            ss << "Lax";
                            break;
                        case SameSitePolicy::None:
                            ss << "None";
                            break;
                    }
                }
                return ss.str();
            }

            // Expires attribute
            Cookie& expires(const std::tm& time)
            {
                expires_at_ = time;
                return *this;
            }

            // Max-Age attribute
            Cookie& max_age(long long seconds)
            {
                max_age_ = seconds;
                return *this;
            }

            // Domain attribute
            Cookie& domain(const std::string& name)
            {
                domain_ = name;
                return *this;
            }

            // Path attribute
            Cookie& path(const std::string& path)
            {
                path_ = path;
                return *this;
            }

            // Secured attribute
            Cookie& secure()
            {
                secure_ = true;
                return *this;
            }

            // HttpOnly attribute
            Cookie& httponly()
            {
                httponly_ = true;
                return *this;
            }

            // SameSite attribute
            Cookie& same_site(SameSitePolicy ssp)
            {
                same_site_ = ssp;
                return *this;
            }

        private:
            Cookie() = default;

            static void dumpString(std::stringstream& ss, bool cond, const char* prefix,
                                   const std::string& value = "")
            {
                if (cond)
                {
                    ss << DIVIDER << prefix << value;
                }
            }

        private:
            std::string key_;
            std::string value_;
            boost::optional<long long> max_age_{};
            std::string domain_ = "";
            std::string path_ = "";
            bool secure_ = false;
            bool httponly_ = false;
            boost::optional<std::tm> expires_at_{};
            boost::optional<SameSitePolicy> same_site_{};

            static constexpr const char* DIVIDER = "; ";
        };


        struct context
        {
            std::unordered_map<std::string, std::string> jar;
            std::vector<Cookie> cookies_to_add;

            std::string get_cookie(const std::string& key) const
            {
                auto cookie = jar.find(key);
                if (cookie != jar.end())
                    return cookie->second;
                return {};
            }

            template<typename U>
            Cookie& set_cookie(const std::string& key, U&& value)
            {
                cookies_to_add.emplace_back(key, std::forward<U>(value));
                return cookies_to_add.back();
            }
        };

        void before_handle(request& req, response& res, context& ctx)
        {
            // TODO(dranikpg): remove copies, use string_view with c++17
            int count = req.headers.count("Cookie");
            if (!count)
                return;
            if (count > 1)
            {
                res.code = 400;
                res.end();
                return;
            }
            std::string cookies = req.get_header_value("Cookie");
            size_t pos = 0;
            while (pos < cookies.size())
            {
                size_t pos_equal = cookies.find('=', pos);
                if (pos_equal == cookies.npos)
                    break;
                std::string name = cookies.substr(pos, pos_equal - pos);
                boost::trim(name);
                pos = pos_equal + 1;
                while (pos < cookies.size() && cookies[pos] == ' ')
                    pos++;
                if (pos == cookies.size())
                    break;

                size_t pos_semicolon = cookies.find(';', pos);
                std::string value = cookies.substr(pos, pos_semicolon - pos);

                boost::trim(value);
                if (value[0] == '"' && value[value.size() - 1] == '"')
                {
                    value = value.substr(1, value.size() - 2);
                }

                ctx.jar.emplace(std::move(name), std::move(value));

                pos = pos_semicolon;
                if (pos == cookies.npos)
                    break;
                pos++;
                while (pos < cookies.size() && cookies[pos] == ' ')
                    pos++;
            }
        }

        void after_handle(request& /*req*/, response& res, context& ctx)
        {
            for (const auto& cookie : ctx.cookies_to_add)
            {
                res.add_header("Set-Cookie", cookie.dump());
            }
        }
    };

    /*
    App<CookieParser, AnotherJarMW> app;
    A B C
    A::context
        int aa;

    ctx1 : public A::context
    ctx2 : public ctx1, public B::context
    ctx3 : public ctx2, public C::context

    C depends on A

    C::handle
        context.aaa

    App::context : private CookieParser::contetx, ... 
    {
        jar

    }

    SimpleApp
    */
} // namespace crow
