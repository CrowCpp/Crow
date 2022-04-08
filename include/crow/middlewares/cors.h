#pragma once
#include "crow/http_request.h"
#include "crow/http_response.h"
#include "crow/routing.h"

namespace crow
{
    struct CORSHandler;

    /// Used for tuning CORS policies
    struct CORSRules
    {
        friend struct crow::CORSHandler;

        /// Set Access-Control-Allow-Origin. Default is "*"
        CORSRules& origin(const std::string& origin)
        {
            origin_ = origin;
            return *this;
        }

        /// Set Access-Control-Allow-Methods. Default is "*"
        CORSRules& methods(crow::HTTPMethod method)
        {
            add_list_item(methods_, crow::method_name(method));
            return *this;
        }

        /// Set Access-Control-Allow-Methods. Default is "*"
        template<typename... Methods>
        CORSRules& methods(crow::HTTPMethod method, Methods... method_list)
        {
            add_list_item(methods_, crow::method_name(method));
            methods(method_list...);
            return *this;
        }

        /// Set Access-Control-Allow-Headers. Default is "*"
        CORSRules& headers(const std::string& header)
        {
            add_list_item(headers_, header);
            return *this;
        }

        /// Set Access-Control-Allow-Headers. Default is "*"
        template<typename... Headers>
        CORSRules& headers(const std::string& header, Headers... header_list)
        {
            add_list_item(headers_, header);
            headers(header_list...);
            return *this;
        }

        /// Set Access-Control-Max-Age. Default is none
        CORSRules& max_age(int max_age)
        {
            max_age_ = std::to_string(max_age);
            return *this;
        }

        /// Enable Access-Control-Allow-Credentials
        CORSRules& allow_credentials()
        {
            allow_credentials_ = true;
            return *this;
        }

        /// Ignore CORS and don't send any headers
        void ignore()
        {
            ignore_ = true;
        }

        /// Handle CORS on specific prefix path
        CORSRules& prefix(const std::string& prefix);

        /// Handle CORS for specific blueprint
        CORSRules& blueprint(const Blueprint& bp);

        /// Global CORS policy
        CORSRules& global();

    private:
        CORSRules() = delete;
        CORSRules(CORSHandler* handler):
          handler_(handler) {}

        /// build comma separated list
        void add_list_item(std::string& list, const std::string& val)
        {
            if (list == "*") list = "";
            if (list.size() > 0) list += ", ";
            list += val;
        }

        /// Set header `key` to `value` if it is not set
        void set_header_no_override(const std::string& key, const std::string& value, crow::response& res)
        {
            if (value.size() == 0) return;
            if (!get_header_value(res.headers, key).empty()) return;
            res.add_header(key, value);
        }

        /// Set response headers
        void apply(crow::response& res)
        {
            if (ignore_) return;
            set_header_no_override("Access-Control-Allow-Origin", origin_, res);
            set_header_no_override("Access-Control-Allow-Methods", methods_, res);
            set_header_no_override("Access-Control-Allow-Headers", headers_, res);
            set_header_no_override("Access-Control-Max-Age", max_age_, res);
            if (allow_credentials_) set_header_no_override("Access-Control-Allow-Credentials", "true", res);
        }

        bool ignore_ = false;
        // TODO: support multiple origins that are dynamically selected
        std::string origin_ = "*";
        std::string methods_ = "*";
        std::string headers_ = "*";
        std::string max_age_;
        bool allow_credentials_ = false;

        CORSHandler* handler_;
    };

    /// CORSHandler is a global middleware for setting CORS headers.

    ///
    /// By default, it sets Access-Control-Allow-Origin/Methods/Headers to "*".
    /// The default behaviour can be changed with the `global()` cors rule.
    /// Additional rules for prexies can be added with `prefix()`.
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

        /// Handle CORS on a specific prefix path
        CORSRules& prefix(const std::string& prefix)
        {
            rules.emplace_back(prefix, CORSRules(this));
            return rules.back().second;
        }

        /// Handle CORS for a specific blueprint
        CORSRules& blueprint(const Blueprint& bp)
        {
            rules.emplace_back(bp.prefix(), CORSRules(this));
            return rules.back().second;
        }

        /// Get the global CORS policy
        CORSRules& global()
        {
            return default_;
        }

    private:
        CORSRules& find_rule(const std::string& path)
        {
            // TODO: use a trie in case of many rules
            for (auto& rule : rules)
            {
                // Check if path starts with a rules prefix
                if (path.rfind(rule.first, 0) == 0)
                {
                    return rule.second;
                }
            }
            return default_;
        }

        std::vector<std::pair<std::string, CORSRules>> rules;
        CORSRules default_ = CORSRules(this);
    };

    inline CORSRules& CORSRules::prefix(const std::string& prefix)
    {
        return handler_->prefix(prefix);
    }

    inline CORSRules& CORSRules::blueprint(const Blueprint& bp)
    {
        return handler_->blueprint(bp);
    }

    inline CORSRules& CORSRules::global()
    {
        return handler_->global();
    }

} // namespace crow
