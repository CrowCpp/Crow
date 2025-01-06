#pragma once

#include "crow/http_request.h"
#include "crow/http_response.h"
#include "crow/json.h"
#include "crow/utility.h"
#include "crow/middlewares/cookie_parser.h"

#include <unordered_map>
#include <unordered_set>
#include <set>
#include <queue>

#include <memory>
#include <string>
#include <cstdio>
#include <mutex>

#include <fstream>
#include <sstream>

#include <type_traits>
#include <functional>
#include <chrono>

#include <variant>

namespace
{
    // convert all integer values to int64_t
    template<typename T>
    using wrap_integral_t = typename std::conditional<
      std::is_integral<T>::value && !std::is_same<bool, T>::value
        // except for uint64_t because that could lead to overflow on conversion
        && !std::is_same<uint64_t, T>::value,
      int64_t, T>::type;

    // convert char[]/char* to std::string
    template<typename T>
    using wrap_char_t = typename std::conditional<
      std::is_same<typename std::decay<T>::type, char*>::value,
      std::string, T>::type;

    // Upgrade to correct type for multi_variant use
    template<typename T>
    using wrap_mv_t = wrap_char_t<wrap_integral_t<T>>;
} // namespace

namespace crow
{
    namespace session
    {

        using multi_value_types = black_magic::S<bool, int64_t, double, std::string>;

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

            template<typename T, typename RT = wrap_mv_t<T>>
            RT get(const T& fallback)
            {
                if (const RT* val = std::get_if<RT>(&v_)) return *val;
                return fallback;
            }

            template<typename T, typename RT = wrap_mv_t<T>>
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
                    if (rv.nt() == num_type::Floating_point || rv.nt() == num_type::Double_precision_floating_point)
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

        /// Expiration tracker keeps track of soonest-to-expire keys
        struct ExpirationTracker
        {
            using DataPair = std::pair<uint64_t /*time*/, std::string /*key*/>;

            /// Add key with time to tracker.
            /// If the key is already present, it will be updated
            void add(std::string key, uint64_t time)
            {
                auto it = times_.find(key);
                if (it != times_.end()) remove(key);
                times_[key] = time;
                queue_.insert({time, std::move(key)});
            }

            void remove(const std::string& key)
            {
                auto it = times_.find(key);
                if (it != times_.end())
                {
                    queue_.erase({it->second, key});
                    times_.erase(it);
                }
            }

            /// Get expiration time of soonest-to-expire entry
            uint64_t peek_first() const
            {
                if (queue_.empty()) return std::numeric_limits<uint64_t>::max();
                return queue_.begin()->first;
            }

            std::string pop_first()
            {
                auto it = times_.find(queue_.begin()->second);
                auto key = it->first;
                times_.erase(it);
                queue_.erase(queue_.begin());
                return key;
            }

            using iterator = typename std::set<DataPair>::const_iterator;

            iterator begin() const { return queue_.cbegin(); }

            iterator end() const { return queue_.cend(); }

        private:
            std::set<DataPair> queue_;
            std::unordered_map<std::string, uint64_t> times_;
        };

        /// CachedSessions are shared across requests
        struct CachedSession
        {
            std::string session_id;
            std::string requested_session_id; // session hasn't been created yet, but a key was requested

            std::unordered_map<std::string, multi_value> entries;
            std::unordered_set<std::string> dirty; // values that were changed after last load

            void* store_data;
            bool requested_refresh;

            // number of references held - used for correctly destroying the cache.
            // No need to be atomic, all SessionMiddleware accesses are synchronized
            int referrers;
            std::recursive_mutex mutex;
        };
    } // namespace session

    // SessionMiddleware allows storing securely and easily small snippets of user information
    template<typename Store>
    struct SessionMiddleware
    {
        using lock = std::scoped_lock<std::mutex>;
        using rc_lock = std::scoped_lock<std::recursive_mutex>;

        struct context
        {
            // Get a mutex for locking this session
            std::recursive_mutex& mutex()
            {
                check_node();
                return node->mutex;
            }

            // Check whether this session is already present
            bool exists() { return bool(node); }

            // Get a value by key or fallback if it doesn't exist or is of another type
            template<typename F>
            auto get(const std::string& key, const F& fallback = F())
              // This trick lets the multi_value deduce the return type from the fallback
              // which allows both:
              //   context.get<std::string>("key")
              //   context.get("key", "") -> char[] is transformed into string by multivalue
              // to return a string
              -> decltype(std::declval<session::multi_value>().get<F>(std::declval<F>()))
            {
                if (!node) return fallback;
                rc_lock l(node->mutex);

                auto it = node->entries.find(key);
                if (it != node->entries.end()) return it->second.get<F>(fallback);
                return fallback;
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

            bool contains(const std::string& key)
            {
                if (!node) return false;
                return node->entries.find(key) != node->entries.end();
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
            void remove(const std::string& key)
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

            // Delay expiration by issuing another cookie with an updated expiration time
            // and notifying the store
            void refresh_expiration()
            {
                if (!node) return;
                node->requested_refresh = true;
            }

        private:
            friend struct SessionMiddleware;

            void check_node()
            {
                if (!node) node = std::make_shared<session::CachedSession>();
            }

            std::shared_ptr<session::CachedSession> node;
        };

        template<typename... Ts>
        SessionMiddleware(
          CookieParser::Cookie cookie,
          int id_length,
          Ts... ts):
          id_length_(id_length),
          cookie_(cookie),
          store_(std::forward<Ts>(ts)...), mutex_(new std::mutex{})
        {}

        template<typename... Ts>
        SessionMiddleware(Ts... ts):
          SessionMiddleware(
            CookieParser::Cookie("session").path("/").max_age(/*month*/ 30 * 24 * 60 * 60),
            /*id_length */ 20, // around 10^34 possible combinations, but small enough to fit into SSO
            std::forward<Ts>(ts)...)
        {}

        template<typename AllContext>
        void before_handle(request& /*req*/, response& /*res*/, context& ctx, AllContext& all_ctx)
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

            auto node = std::make_shared<session::CachedSession>();
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
        void after_handle(request& /*req*/, response& /*res*/, context& ctx, AllContext& all_ctx)
        {
            lock l(*mutex_);
            if (!ctx.node || --ctx.node->referrers > 0) return;
            ctx.node->requested_refresh |= ctx.node->session_id == "";

            // generate new id
            if (ctx.node->session_id == "")
            {
                // check for requested id
                ctx.node->session_id = std::move(ctx.node->requested_session_id);
                if (ctx.node->session_id == "")
                {
                    ctx.node->session_id = utility::random_alphanum(id_length_);
                }
            }
            else
            {
                cache_.erase(ctx.node->session_id);
            }

            if (ctx.node->requested_refresh)
            {
                auto& cookies = all_ctx.template get<CookieParser>();
                store_id(cookies, ctx.node->session_id);
            }

            try
            {
                store_.save(*ctx.node);
            }
            catch (...)
            {
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
            return cookies.get_cookie(cookie_.name());
        }

        void store_id(CookieParser::context& cookies, const std::string& session_id)
        {
            cookie_.value(session_id);
            cookies.set_cookie(cookie_);
        }

    private:
        int id_length_;

        // prototype for cookie
        CookieParser::Cookie cookie_;

        Store store_;

        // mutexes are immovable
        std::unique_ptr<std::mutex> mutex_;
        std::unordered_map<std::string, std::shared_ptr<session::CachedSession>> cache_;
    };

    /// InMemoryStore stores all entries in memory
    struct InMemoryStore
    {
        // Load a value into the session cache.
        // A load is always followed by a save, no loads happen consecutively
        void load(session::CachedSession& cn)
        {
            // load & stores happen sequentially, so moving is safe
            cn.entries = std::move(entries[cn.session_id]);
        }

        // Persist session data
        void save(session::CachedSession& cn)
        {
            entries[cn.session_id] = std::move(cn.entries);
            // cn.dirty is a list of changed keys since the last load
        }

        bool contains(const std::string& key)
        {
            return entries.count(key) > 0;
        }

        std::unordered_map<std::string, std::unordered_map<std::string, session::multi_value>> entries;
    };

    // FileStore stores all data as json files in a folder.
    // Files are deleted after expiration. Expiration refreshes are automatically picked up.
    struct FileStore
    {
        FileStore(const std::string& folder, uint64_t expiration_seconds = /*month*/ 30 * 24 * 60 * 60):
          path_(folder), expiration_seconds_(expiration_seconds)
        {
            std::ifstream ifs(get_filename(".expirations", false));

            auto current_ts = chrono_time();
            std::string key;
            uint64_t time;
            while (ifs >> key >> time)
            {
                if (current_ts > time)
                {
                    evict(key);
                }
                else if (contains(key))
                {
                    expirations_.add(key, time);
                }
            }
        }

        ~FileStore()
        {
            std::ofstream ofs(get_filename(".expirations", false), std::ios::trunc);
            for (const auto& p : expirations_)
                ofs << p.second << " " << p.first << "\n";
        }

        // Delete expired entries
        // At most 3 to prevent freezes
        void handle_expired()
        {
            int deleted = 0;
            auto current_ts = chrono_time();
            while (current_ts > expirations_.peek_first() && deleted < 3)
            {
                evict(expirations_.pop_first());
                deleted++;
            }
        }

        void load(session::CachedSession& cn)
        {
            handle_expired();

            std::ifstream file(get_filename(cn.session_id));

            std::stringstream buffer;
            buffer << file.rdbuf() << std::endl;

            for (const auto& p : json::load(buffer.str()))
                cn.entries[p.key()] = session::multi_value::from_json(p);
        }

        void save(session::CachedSession& cn)
        {
            if (cn.requested_refresh)
                expirations_.add(cn.session_id, chrono_time() + expiration_seconds_);
            if (cn.dirty.empty()) return;

            std::ofstream file(get_filename(cn.session_id));
            json::wvalue jw;
            for (const auto& p : cn.entries)
                jw[p.first] = p.second.json();
            file << jw.dump() << std::flush;
        }

        std::string get_filename(const std::string& key, bool suffix = true)
        {
            return utility::join_path(path_, key + (suffix ? ".json" : ""));
        }

        bool contains(const std::string& key)
        {
            std::ifstream file(get_filename(key));
            return file.good();
        }

        void evict(const std::string& key)
        {
            std::remove(get_filename(key).c_str());
        }

        uint64_t chrono_time() const
        {
            return std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::system_clock::now().time_since_epoch())
              .count();
        }

        std::string path_;
        uint64_t expiration_seconds_;
        session::ExpirationTracker expirations_;
    };

} // namespace crow
