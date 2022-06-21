#pragma once

#include "crow/http_request.h"
#include "crow/http_response.h"
#include "crow/TinySHA1.hpp"
#include "crow/json.h"
#include "crow/utility.h"
#include "crow/middlewares/cookie_parser.h"

#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <string>
#include <fstream>
#include <sstream>
#include <atomic>
#include <type_traits>
#include <functional>

// REQUIRE C++17
#include <variant>

namespace crow
{
    namespace detail
    {
        using multi_value_types = black_magic::S<bool, int64_t, double, std::string>;

        template<typename T>
        using wrap_integral_t = std::conditional_t<std::is_integral_v<T>, int64_t, T>;

        /// A multi_value is a safe variant wrapper with json conversion support
        struct multi_value
        {
            json::wvalue json() const
            {
                // clang-format off
                return std::visit([](auto arg) {
                    return json::wvalue(arg);
                }, v_);
                // clang-format on
            }

            static multi_value from_json(const json::rvalue&);

            std::string string() const
            {
                // clang-format off
                return std::visit([](auto arg) {
                    if constexpr (std::is_same_v<decltype(arg), std::string>)
                        return arg;
                    else
                        return std::to_string(arg);
                }, v_);
                // clang-format on
            }

            template<typename T, typename RT = wrap_integral_t<T>>
            RT get(const T& fallback)
            {
                if (const RT* val = std::get_if<RT>(&v_)) return *val;
                return fallback;
            }

            template<typename T, typename RT = wrap_integral_t<T>>
            void set(T val)
            {
                v_ = RT(std::move(val));
            }

            typename multi_value_types::rebind<std::variant> v_;
        };

        inline multi_value multi_value::from_json(const json::rvalue& rv)
        {
            using namespace json;
            switch (rv.t())
            {
                case type::Number:
                {
                    if (rv.nt() == num_type::Floating_point)
                        return multi_value{rv.d()};
                    else if (rv.nt() == num_type::Unsigned_integer)
                        return multi_value{int64_t(rv.u())};
                    else
                        return multi_value{rv.i()};
                }
                case type::False: return multi_value{false};
                case type::True: return multi_value{true};
                case type::String: return multi_value{std::string(rv)};
                default: return multi_value{false};
            }
        }

        /// CachedSessions are shared across requests
        struct CachedSession
        {
            std::string session_id;
            std::string requested_session_id;
            std::unordered_map<std::string, multi_value> entries;

            // values that were changed after last load
            std::unordered_set<std::string> dirty;

            void* store_data;

            // number for references held - used for correctly destroying the cache
            // no need to be atomic, all SessionMiddleware accesses are synchronized
            int referrers;
            std::recursive_mutex mutex;
        };
    }; // namespace detail

    // SessionMiddleware allows storing securely and easily small snippets of user information
    template<typename Store>
    struct SessionMiddleware
    {
#ifdef CROW_CAN_USE_CPP17
        using lock = std::scoped_lock<std::mutex>;
        using rc_lock = std::scoped_lock<std::recursive_mutex>;
#else
        using lock = std::lock_guard<std::mutex>;
        using rc_lock = std::lock_guard<std::recursive_mutex>;
#endif

        struct context
        {
            // Get a mutex for locking this session
            std::recursive_mutex& mutex()
            {
                check_node();
                return node->mutex;
            }

            // Check wheter this session is already present
            bool exists() { return bool(node); }

            // Get a value by key or fallback if it doesn't exist or is of another type
            template<typename T>
            T get(const std::string& key, const T& fallback = T())
            {
                if (!node) return fallback;
                rc_lock l(node->mutex);

                auto it = node->entries.find(key);
                if (it != node->entries.end()) return it->second.get<T>(fallback);
                return fallback;
            }

            // Request a special session id for the store
            // WARNING: it does not check for collisions!
            void preset_id(std::string id)
            {
                check_node();
                node->requested_session_id = std::move(id);
            }

            // Set a value by key
            template<typename T>
            void set(const std::string& key, T value)
            {
                check_node();
                rc_lock l(node->mutex);

                node->dirty.insert(key);
                node->entries[key].set(std::move(value));
            }

            // Atomically mutate a value with a function
            template<typename Func>
            void apply(const std::string& key, const Func& f)
            {
                using traits = utility::function_traits<Func>;
                using arg = typename std::decay<typename traits::template arg<0>>::type;
                using retv = typename std::decay<typename traits::result_type>::type;
                check_node();
                rc_lock l(node->mutex);
                node->dirty.insert(key);
                node->entries[key].set<retv>(f(node->entries[key].get(arg{})));
            }

            // Remove a value from the session
            void evict(const std::string& key)
            {
                if (!node) return;
                rc_lock l(node->mutex);
                node->dirty.insert(key);
                node->entries.erase(key);
            }

            // Format value by key as a string
            std::string string(const std::string& key)
            {
                if (!node) return "";
                rc_lock l(node->mutex);

                auto it = node->entries.find(key);
                if (it != node->entries.end()) return it->second.string();
                return "";
            }

            // Get a list of keys present in session
            std::vector<std::string> keys()
            {
                if (!node) return {};
                rc_lock l(node->mutex);

                std::vector<std::string> out;
                for (const auto& p : node->entries)
                    out.push_back(p.first);
                return out;
            }

        private:
            friend class SessionMiddleware;

            void check_node()
            {
                if (!node) node = std::make_shared<detail::CachedSession>();
            }

            std::shared_ptr<detail::CachedSession> node;
        };

        template<typename... Ts>
        SessionMiddleware(const std::string& secret_key, CookieParser::Cookie cookie, int id_length, Ts... ts):
          secret_key_(secret_key), id_length_(id_length), cookie_(cookie), store_(std::forward<Ts>(ts)...),
          mutex_(new std::mutex{})
        {}

        template<typename... Ts>
        SessionMiddleware(const std::string& secret_key, Ts... ts):
          SessionMiddleware(secret_key,
                            CookieParser::Cookie("session").path("/").max_age(/*month*/ 30 * 24 * 60 * 60),
                            10, std::forward<Ts>(ts)...)
        {}

        template<typename AllContext>
        void before_handle(request& /*req*/, response& /*rsp*/, context& ctx, AllContext& all_ctx)
        {
            lock l(*mutex_);

            auto& cookies = all_ctx.template get<CookieParser>();
            auto session_id = load_id(cookies);
            if (session_id == "") return;

            // search entry in cache
            auto it = cache_.find(session_id);
            if (it != cache_.end())
            {
                it->second->referrers++;
                ctx.node = it->second;
                return;
            }

            // check this is a valid entry before loading
            if (!store_.contains(session_id)) return;

            auto node = std::make_shared<detail::CachedSession>();
            node->session_id = session_id;
            node->referrers = 1;

            try
            {
                store_.load(*node);
            }
            catch (...)
            {
                CROW_LOG_ERROR << "Exception occurred during session load";
                return;
            }



            ctx.node = node;
            cache_[session_id] = node;
        }

        template<typename AllContext>
        void after_handle(request& /*req*/, response& /*rsp*/, context& ctx, AllContext& all_ctx)
        {
            lock l(*mutex_);
            if (!ctx.node || --ctx.node->referrers > 0) return;

            // generate new id
            if (ctx.node->session_id == "")
            {
                // check for requested id
                ctx.node->session_id = std::move(ctx.node->requested_session_id);
                if (ctx.node->session_id == "")
                {
                    ctx.node->session_id = utility::random_alphanum(id_length_);
                }
                auto& cookies = all_ctx.template get<CookieParser>();
                store_id(cookies, ctx.node->session_id);
            }
            else
            {
                cache_.erase(ctx.node->session_id);
            }

            try
            {
                store_.save(*ctx.node);
            }
            catch (...)
            {
                // TODO(dranikpg): better handling?
                CROW_LOG_ERROR << "Exception occurred during session save";
                return;
            }
        }

    private:
        std::string next_id()
        {
            std::string id;
            do
            {
                id = utility::random_alphanum(id_length_);
            } while (store_.contains(id));
            return id;
        }

        std::string load_id(const CookieParser::context& cookies)
        {
            auto session_raw = cookies.get_cookie(cookie_.name());

            auto pos = session_raw.find(":");
            if (pos <= 0 || pos >= session_raw.size() - 1) return "";

            auto session_id = session_raw.substr(0, pos);
            auto session_hmac = session_raw.substr(pos + 1, session_raw.size() - pos - 1);

            return hash_id(session_id) == session_hmac ? session_id : "";
        }

        void store_id(CookieParser::context& cookies, const std::string& session_id)
        {
            cookie_.value(session_id + ":" + hash_id(session_id));
            cookies.set_cookie(cookie_);
        }

        std::string hash_id(const std::string& key) const
        {
            sha1::SHA1 s;
            std::string input = key + "/" + secret_key_;
            s.processBytes(input.data(), input.size());
            uint8_t digest[20];
            s.getDigestBytes(digest);
            return utility::base64encode((unsigned char*)digest, 20);
        }

    private:
        std::string secret_key_;
        int id_length_;

        // prototype for cookie
        CookieParser::Cookie cookie_;

        Store store_;

        // mutexes are immovable
        std::unique_ptr<std::mutex> mutex_;
        std::unordered_map<std::string, std::shared_ptr<detail::CachedSession>> cache_;
    };

    /// InMemoryStore stores all entries in memory
    struct InMemoryStore
    {
        // Load a value into the session cache.
        // A load is always followed by a save, no loads happen consecutively
        void load(detail::CachedSession& cn)
        {
            // load & stores happen sequentially, so moving is safe
            cn.entries = std::move(entries[cn.session_id]);
        }

        // Persist session data
        void save(detail::CachedSession& cn)
        {
            entries[cn.session_id] = std::move(cn.entries);
            // cn.dirty is a list of changed keys since the last load
        }

        bool contains(const std::string& key)
        {
            return entries.count(key) > 0;
        }

        std::unordered_map<std::string, std::unordered_map<std::string, detail::multi_value>> entries;
    };

    // FileStore stores all data as json files in a folder
    struct FileStore
    {
        FileStore(const std::string& folder):
          path(folder)
        {}

        void load(detail::CachedSession& cn)
        {
            std::ifstream file(get_filename(cn.session_id));

            std::stringstream buffer;
            buffer << file.rdbuf() << std::endl;

            for (const auto& p : json::load(buffer.str()))
                cn.entries[p.key()] = detail::multi_value::from_json(p);
        }

        void save(detail::CachedSession& cn)
        {
            if (cn.dirty.empty()) return;

            std::ofstream file(get_filename(cn.session_id));
            json::wvalue jw;
            for (const auto& p : cn.entries)
                jw[p.first] = p.second.json();
            file << jw.dump() << std::flush;
        }

        std::string get_filename(const std::string& key)
        {
            return utility::join_path(path, key + ".json");
        }

        bool contains(const std::string& key)
        {
            std::ifstream file(get_filename(key));
            return file.good();
        }

        std::string path;
    };

} // namespace crow
