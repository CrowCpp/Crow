#pragma once
#include <iomanip>
#include <memory>
#include "crow/utility.h"
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

            Cookie(const std::string& key):
              Cookie(key, "") {}

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
                       << std::put_time(expires_at_.get(), HTTP_DATE_FORMAT);
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

            const std::string& name()
            {
                return key_;
            }

            template<typename U>
            Cookie& value(U&& value)
            {
                value_ = std::forward<U>(value);
                return *this;
            }

            // Expires attribute
            Cookie& expires(const std::tm& time)
            {
                expires_at_ = std::unique_ptr<std::tm>(new std::tm(time));
                return *this;
            }

            // Max-Age attribute
            Cookie& max_age(long long seconds)
            {
                max_age_ = std::unique_ptr<long long>(new long long(seconds));
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
                same_site_ = std::unique_ptr<SameSitePolicy>(new SameSitePolicy(ssp));
                return *this;
            }

            Cookie(const Cookie& c):
              key_(c.key_),
              value_(c.value_),
              domain_(c.domain_),
              path_(c.path_),
              secure_(c.secure_),
              httponly_(c.httponly_)
            {
                if (c.max_age_)
                    max_age_ = std::unique_ptr<long long>(new long long(*c.max_age_));

                if (c.expires_at_)
                    expires_at_ = std::unique_ptr<std::tm>(new std::tm(*c.expires_at_));

                if (c.same_site_)
                    same_site_ = std::unique_ptr<SameSitePolicy>(new SameSitePolicy(*c.same_site_));
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
            std::unique_ptr<long long> max_age_{};
            std::string domain_ = "";
            std::string path_ = "";
            bool secure_ = false;
            bool httponly_ = false;
            std::unique_ptr<std::tm> expires_at_{};
            std::unique_ptr<SameSitePolicy> same_site_{};

            static constexpr const char* DIVIDER = "; ";
        };


        struct context
        {
            std::unordered_map<std::string, std::string> jar;

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

            Cookie& set_cookie(Cookie cookie)
            {
                cookies_to_add.push_back(std::move(cookie));
                return cookies_to_add.back();
            }

        private:
            friend struct CookieParser;
            std::vector<Cookie> cookies_to_add;
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
                name = utility::trim(name);
                pos = pos_equal + 1;
                if (pos == cookies.size())
                    break;

                size_t pos_semicolon = cookies.find(';', pos);
                std::string value = cookies.substr(pos, pos_semicolon - pos);

                value = utility::trim(value);
                if (value[0] == '"' && value[value.size() - 1] == '"')
                {
                    value = value.substr(1, value.size() - 2);
                }

                ctx.jar.emplace(std::move(name), std::move(value));

                pos = pos_semicolon;
                if (pos == cookies.npos)
                    break;
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

    App::context : private CookieParser::context, ...
    {
        jar

    }

    SimpleApp
    */
} // namespace crow
