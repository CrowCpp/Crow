#pragma once
#include "crow/http_request.h"
#include "crow/http_response.h"

namespace crow
{
    struct CORSHandler;

    // CORSRules is used for tuning cors policies
    struct CORSRules
    {
        friend struct crow::CORSHandler;
        // Set Access-Control-Allow-Origin. Default is "*"

        CORSRules& origin(const std::string& origin)
        {
            origin_ = origin;
            return *this;
        }

        // Set Access-Control-Allow-Methods. Default is "*"
        CORSRules& methods(crow::HTTPMethod method)
        {
            add_list_item(methods_, crow::method_name(method));
            return *this;
        }

        // Set Access-Control-Allow-Methods. Default is "*"
        template<typename... Methods>
        CORSRules& methods(crow::HTTPMethod method, Methods... method_list)
        {
            add_list_item(methods_, crow::method_name(method));
            methods(method_list...);
            return *this;
        }

        // Set Access-Control-Allow-Headers. Default is "*"
        CORSRules& headers(const std::string& header)
        {
            add_list_item(headers_, header);
            return *this;
        }

        // Set Access-Control-Allow-Headers. Default is "*"
        template<typename... Headers>
        CORSRules& headers(const std::string& header, Headers... header_list)
        {
            add_list_item(headers_, header);
            headers(header_list...);
            return *this;
        }

        // Set Access-Control-Max-Age. Default is none
        CORSRules& max_age(int max_age)
        {
            max_age_ = std::to_string(max_age);
            return *this;
        }

        // Enable Access-Control-Allow-Credentials
        CORSRules& allow_credentials()
        {
            allow_credentials_ = true;
            return *this;
        }

        // Ignore CORS
        void ignore()
        {
            ignore_ = true;
        }

    private:
        void add_list_item(std::string& list, const std::string& val)
        {
            if (list == "*") list = "";
            if (list.size() > 0) list += ", ";
            list += val;
        }

        void set_header(const std::string& key, const std::string& value, crow::response& res)
        {
            if (value.size() == 0) return;
            if (!get_header_value(res.headers, key).empty()) return;
            res.add_header(key, value);
        }

        void apply(crow::response& res)
        {
            if (ignore_) return;
            set_header("Access-Control-Allow-Origin", origin_, res);
            set_header("Access-Control-Allow-Methods", methods_, res);
            set_header("Access-Control-Allow-Headers", headers_, res);
            set_header("Access-Control-Max-Age", max_age_, res);
            if (allow_credentials_) set_header("Access-Control-Allow-Credentials", "true", res);
        }

        bool ignore_ = false;
        std::string origin_ = "*";
        std::string methods_ = "*";
        std::string headers_ = "*";
        std::string max_age_;
        bool allow_credentials_ = false;
    };

    // CORSHandler is used for enforcing CORS policies
    struct CORSHandler
    {
        struct context
        {};

        void before_handle(crow::request& /*req*/, crow::response& /*res*/, context& /*ctx*/)
        {}

        void after_handle(crow::request& req, crow::response& res, context& /*ctx*/)
        {
            auto& rule = find_rule(req.url);
            rule.apply(res);
        }

        // Handle CORS on specific prefix path
        CORSRules& prefix(const std::string& prefix)
        {
            rules.emplace_back(prefix, CORSRules{});
            return rules.back().second;
        }

        // Global CORS policy
        CORSRules& global()
        {
            return default_;
        }

    private:
        CORSRules& find_rule(const std::string& path)
        {
            for (auto& rule : rules)
            {
                if (path.rfind(rule.first, 0) == 0)
                {
                    return rule.second;
                }
            }
            return default_;
        }

        std::vector<std::pair<std::string, CORSRules>> rules;
        CORSRules default_;
    };

} // namespace crow
