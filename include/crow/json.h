#pragma once

//#define CROW_JSON_NO_ERROR_CHECK
//#define CROW_JSON_USE_MAP

#include <string>
#ifdef CROW_JSON_USE_MAP
#include <map>
#else
#include <unordered_map>
#endif
#include <iostream>
#include <algorithm>
#include <memory>
#include <vector>
#include <cmath>
#include <cfloat>

#include "crow/utility.h"
#include "crow/settings.h"
#include "crow/returnable.h"
#include "crow/logging.h"

using std::isinf;
using std::isnan;


namespace crow // NOTE: Already documented in "crow/app.h"
{
    namespace mustache
    {
        class template_t;
    }

    namespace json
    {
        static inline char to_hex(char c)
        {
            c = c & 0xf;
            if (c < 10)
                return '0' + c;
            return 'a' + c - 10;
        }

        inline void escape(const std::string& str, std::string& ret)
        {
            ret.reserve(ret.size() + str.size() + str.size() / 4);
            for (auto c : str)
            {
                switch (c)
                {
                    case '"': ret += "\\\""; break;
                    case '\\': ret += "\\\\"; break;
                    case '\n': ret += "\\n"; break;
                    case '\b': ret += "\\b"; break;
                    case '\f': ret += "\\f"; break;
                    case '\r': ret += "\\r"; break;
                    case '\t': ret += "\\t"; break;
                    default:
                        if (c >= 0 && c < 0x20)
                        {
                            ret += "\\u00";
                            ret += to_hex(c / 16);
                            ret += to_hex(c % 16);
                        }
                        else
                            ret += c;
                        break;
                }
            }
        }
        inline std::string escape(const std::string& str)
        {
            std::string ret;
            escape(str, ret);
            return ret;
        }

        enum class type : char
        {
            Null,
            False,
            True,
            Number,
            String,
            List,
            Object,
            Function
        };

        inline const char* get_type_str(type t)
        {
            switch (t)
            {
                case type::Number: return "Number";
                case type::False: return "False";
                case type::True: return "True";
                case type::List: return "List";
                case type::String: return "String";
                case type::Object: return "Object";
                case type::Function: return "Function";
                default: return "Unknown";
            }
        }

        enum class num_type : char
        {
            Signed_integer,
            Unsigned_integer,
            Floating_point,
            Null,
            Double_precision_floating_point
        };

        class rvalue;
        rvalue load(const char* data, size_t size);

        namespace detail
        {
            /// A read string implementation with comparison functionality.
            struct r_string
            {
                r_string(){}
                r_string(char* s, char* e):
                  s_(s), e_(e){}
                ~r_string()
                {
                    if (owned_)
                        delete[] s_;
                }

                r_string(const r_string& r)
                {
                    *this = r;
                }

                r_string(r_string&& r)
                {
                    *this = r;
                }

                r_string& operator=(r_string&& r)
                {
                    s_ = r.s_;
                    e_ = r.e_;
                    owned_ = r.owned_;
                    if (r.owned_)
                        r.owned_ = 0;
                    return *this;
                }

                r_string& operator=(const r_string& r)
                {
                    s_ = r.s_;
                    e_ = r.e_;
                    owned_ = 0;
                    return *this;
                }

                operator std::string() const
                {
                    return std::string(s_, e_);
                }


                const char* begin() const { return s_; }
                const char* end() const { return e_; }
                size_t size() const { return end() - begin(); }

                using iterator = const char*;
                using const_iterator = const char*;

                char* s_;         ///< Start.
                mutable char* e_; ///< End.
                uint8_t owned_{0};
                friend std::ostream& operator<<(std::ostream& os, const r_string& s)
                {
                    os << static_cast<std::string>(s);
                    return os;
                }

            private:
                void force(char* s, uint32_t length)
                {
                    s_ = s;
                    e_ = s_ + length;
                    owned_ = 1;
                }
                friend rvalue crow::json::load(const char* data, size_t size);

                friend bool operator==(const r_string& l, const r_string& r);
                friend bool operator==(const std::string& l, const r_string& r);
                friend bool operator==(const r_string& l, const std::string& r);

                template<typename T, typename U>
                inline static bool equals(const T& l, const U& r)
                {
                    if (l.size() != r.size())
                        return false;

                    for (size_t i = 0; i < l.size(); i++)
                    {
                        if (*(l.begin() + i) != *(r.begin() + i))
                            return false;
                    }

                    return true;
                }
            };

            inline bool operator<(const r_string& l, const r_string& r)
            {
                return std::lexicographical_compare(l.begin(), l.end(), r.begin(), r.end());
            }

            inline bool operator<(const r_string& l, const std::string& r)
            {
                return std::lexicographical_compare(l.begin(), l.end(), r.begin(), r.end());
            }

            inline bool operator<(const std::string& l, const r_string& r)
            {
                return std::lexicographical_compare(l.begin(), l.end(), r.begin(), r.end());
            }

            inline bool operator>(const r_string& l, const r_string& r)
            {
                return std::lexicographical_compare(l.begin(), l.end(), r.begin(), r.end());
            }

            inline bool operator>(const r_string& l, const std::string& r)
            {
                return std::lexicographical_compare(l.begin(), l.end(), r.begin(), r.end());
            }

            inline bool operator>(const std::string& l, const r_string& r)
            {
                return std::lexicographical_compare(l.begin(), l.end(), r.begin(), r.end());
            }

            inline bool operator==(const r_string& l, const r_string& r)
            {
                return r_string::equals(l, r);
            }

            inline bool operator==(const r_string& l, const std::string& r)
            {
                return r_string::equals(l, r);
            }

            inline bool operator==(const std::string& l, const r_string& r)
            {
                return r_string::equals(l, r);
            }

            inline bool operator!=(const r_string& l, const r_string& r)
            {
                return !(l == r);
            }

            inline bool operator!=(const r_string& l, const std::string& r)
            {
                return !(l == r);
            }

            inline bool operator!=(const std::string& l, const r_string& r)
            {
                return !(l == r);
            }
        } // namespace detail

        /// JSON read value.

        ///
        /// Value can mean any json value, including a JSON object.
        /// Read means this class is used to primarily read strings into a JSON value.
        class rvalue
        {
            static const int cached_bit = 2;
            static const int error_bit = 4;

        public:
            rvalue() noexcept:
              option_{error_bit}
            {
            }
            rvalue(type t) noexcept:
              lsize_{}, lremain_{}, t_{t}
            {
            }
            rvalue(type t, char* s, char* e) noexcept:
              start_{s}, end_{e}, t_{t}
            {
                determine_num_type();
            }

            rvalue(const rvalue& r):
              start_(r.start_), end_(r.end_), key_(r.key_), t_(r.t_), nt_(r.nt_), option_(r.option_)
            {
                copy_l(r);
            }

            rvalue(rvalue&& r) noexcept
            {
                *this = std::move(r);
            }

            rvalue& operator=(const rvalue& r)
            {
                start_ = r.start_;
                end_ = r.end_;
                key_ = r.key_;
                t_ = r.t_;
                nt_ = r.nt_;
                option_ = r.option_;
                copy_l(r);
                return *this;
            }
            rvalue& operator=(rvalue&& r) noexcept
            {
                start_ = r.start_;
                end_ = r.end_;
                key_ = std::move(r.key_);
                l_ = std::move(r.l_);
                lsize_ = r.lsize_;
                lremain_ = r.lremain_;
                t_ = r.t_;
                nt_ = r.nt_;
                option_ = r.option_;
                return *this;
            }

            explicit operator bool() const noexcept
            {
                return (option_ & error_bit) == 0;
            }

            explicit operator int64_t() const
            {
                return i();
            }

            explicit operator uint64_t() const
            {
                return u();
            }

            explicit operator int() const
            {
                return static_cast<int>(i());
            }

            /// Return any json value (not object or list) as a string.
            explicit operator std::string() const
            {
#ifndef CROW_JSON_NO_ERROR_CHECK
                if (t() == type::Object || t() == type::List)
                    throw std::runtime_error("json type container");
#endif
                switch (t())
                {
                    case type::String:
                        return std::string(s());
                    case type::Null:
                        return std::string("null");
                    case type::True:
                        return std::string("true");
                    case type::False:
                        return std::string("false");
                    default:
                        return std::string(start_, end_ - start_);
                }
            }

            /// The type of the JSON value.
            type t() const
            {
#ifndef CROW_JSON_NO_ERROR_CHECK
                if (option_ & error_bit)
                {
                    throw std::runtime_error("invalid json object");
                }
#endif
                return t_;
            }

            /// The number type of the JSON value.
            num_type nt() const
            {
#ifndef CROW_JSON_NO_ERROR_CHECK
                if (option_ & error_bit)
                {
                    throw std::runtime_error("invalid json object");
                }
#endif
                return nt_;
            }

            /// The integer value.
            int64_t i() const
            {
#ifndef CROW_JSON_NO_ERROR_CHECK
                switch (t())
                {
                    case type::Number:
                    case type::String:
                        return utility::lexical_cast<int64_t>(start_, end_ - start_);
                    default:
                        const std::string msg = "expected number, got: " + std::string(get_type_str(t()));
                        throw std::runtime_error(msg);
                }
#endif
                return utility::lexical_cast<int64_t>(start_, end_ - start_);
            }

            /// The unsigned integer value.
            uint64_t u() const
            {
#ifndef CROW_JSON_NO_ERROR_CHECK
                switch (t())
                {
                    case type::Number:
                    case type::String:
                        return utility::lexical_cast<uint64_t>(start_, end_ - start_);
                    default:
                        throw std::runtime_error(std::string("expected number, got: ") + get_type_str(t()));
                }
#endif
                return utility::lexical_cast<uint64_t>(start_, end_ - start_);
            }

            /// The double precision floating-point number value.
            double d() const
            {
#ifndef CROW_JSON_NO_ERROR_CHECK
                if (t() != type::Number)
                    throw std::runtime_error("value is not number");
#endif
                return utility::lexical_cast<double>(start_, end_ - start_);
            }

            /// The boolean value.
            bool b() const
            {
#ifndef CROW_JSON_NO_ERROR_CHECK
                if (t() != type::True && t() != type::False)
                    throw std::runtime_error("value is not boolean");
#endif
                return t() == type::True;
            }

            /// The string value.
            detail::r_string s() const
            {
#ifndef CROW_JSON_NO_ERROR_CHECK
                if (t() != type::String)
                    throw std::runtime_error("value is not string");
#endif
                unescape();
                return detail::r_string{start_, end_};
            }

            /// The list or object value
            std::vector<rvalue> lo() const
            {
#ifndef CROW_JSON_NO_ERROR_CHECK
                if (t() != type::Object && t() != type::List)
                    throw std::runtime_error("value is not a container");
#endif
                std::vector<rvalue> ret;
                ret.reserve(lsize_);
                for (uint32_t i = 0; i < lsize_; i++)
                {
                    ret.emplace_back(l_[i]);
                }
                return ret;
            }

            /// Convert escaped string character to their original form ("\\n" -> '\n').
            void unescape() const
            {
                if (*(start_ - 1))
                {
                    char* head = start_;
                    char* tail = start_;
                    while (head != end_)
                    {
                        if (*head == '\\')
                        {
                            switch (*++head)
                            {
                                case '"': *tail++ = '"'; break;
                                case '\\': *tail++ = '\\'; break;
                                case '/': *tail++ = '/'; break;
                                case 'b': *tail++ = '\b'; break;
                                case 'f': *tail++ = '\f'; break;
                                case 'n': *tail++ = '\n'; break;
                                case 'r': *tail++ = '\r'; break;
                                case 't': *tail++ = '\t'; break;
                                case 'u':
                                {
                                    auto from_hex = [](char c) {
                                        if (c >= 'a')
                                            return c - 'a' + 10;
                                        if (c >= 'A')
                                            return c - 'A' + 10;
                                        return c - '0';
                                    };
                                    unsigned int code =
                                      (from_hex(head[1]) << 12) +
                                      (from_hex(head[2]) << 8) +
                                      (from_hex(head[3]) << 4) +
                                      from_hex(head[4]);
                                    if (code >= 0x800)
                                    {
                                        *tail++ = 0xE0 | (code >> 12);
                                        *tail++ = 0x80 | ((code >> 6) & 0x3F);
                                        *tail++ = 0x80 | (code & 0x3F);
                                    }
                                    else if (code >= 0x80)
                                    {
                                        *tail++ = 0xC0 | (code >> 6);
                                        *tail++ = 0x80 | (code & 0x3F);
                                    }
                                    else
                                    {
                                        *tail++ = code;
                                    }
                                    head += 4;
                                }
                                break;
                            }
                        }
                        else
                            *tail++ = *head;
                        head++;
                    }
                    end_ = tail;
                    *end_ = 0;
                    *(start_ - 1) = 0;
                }
            }

            /// Check if the json object has the passed string as a key.
            bool has(const char* str) const
            {
                return has(std::string(str));
            }

            bool has(const std::string& str) const
            {
                struct Pred
                {
                    bool operator()(const rvalue& l, const rvalue& r) const
                    {
                        return l.key_ < r.key_;
                    }
                    bool operator()(const rvalue& l, const std::string& r) const
                    {
                        return l.key_ < r;
                    }
                    bool operator()(const std::string& l, const rvalue& r) const
                    {
                        return l < r.key_;
                    }
                };
                if (!is_cached())
                {
                    std::sort(begin(), end(), Pred());
                    set_cached();
                }
                auto it = lower_bound(begin(), end(), str, Pred());
                return it != end() && it->key_ == str;
            }

            int count(const std::string& str) const
            {
                return has(str) ? 1 : 0;
            }

            rvalue* begin() const
            {
#ifndef CROW_JSON_NO_ERROR_CHECK
                if (t() != type::Object && t() != type::List)
                    throw std::runtime_error("value is not a container");
#endif
                return l_.get();
            }
            rvalue* end() const
            {
#ifndef CROW_JSON_NO_ERROR_CHECK
                if (t() != type::Object && t() != type::List)
                    throw std::runtime_error("value is not a container");
#endif
                return l_.get() + lsize_;
            }

            const detail::r_string& key() const
            {
                return key_;
            }

            size_t size() const
            {
                if (t() == type::String)
                    return s().size();
#ifndef CROW_JSON_NO_ERROR_CHECK
                if (t() != type::Object && t() != type::List)
                    throw std::runtime_error("value is not a container");
#endif
                return lsize_;
            }

            const rvalue& operator[](int index) const
            {
#ifndef CROW_JSON_NO_ERROR_CHECK
                if (t() != type::List)
                    throw std::runtime_error("value is not a list");
                if (index >= static_cast<int>(lsize_) || index < 0)
                    throw std::runtime_error("list out of bound");
#endif
                return l_[index];
            }

            const rvalue& operator[](size_t index) const
            {
#ifndef CROW_JSON_NO_ERROR_CHECK
                if (t() != type::List)
                    throw std::runtime_error("value is not a list");
                if (index >= lsize_)
                    throw std::runtime_error("list out of bound");
#endif
                return l_[index];
            }

            const rvalue& operator[](const char* str) const
            {
                return this->operator[](std::string(str));
            }

            const rvalue& operator[](const std::string& str) const
            {
#ifndef CROW_JSON_NO_ERROR_CHECK
                if (t() != type::Object)
                    throw std::runtime_error("value is not an object");
#endif
                struct Pred
                {
                    bool operator()(const rvalue& l, const rvalue& r) const
                    {
                        return l.key_ < r.key_;
                    }
                    bool operator()(const rvalue& l, const std::string& r) const
                    {
                        return l.key_ < r;
                    }
                    bool operator()(const std::string& l, const rvalue& r) const
                    {
                        return l < r.key_;
                    }
                };
                if (!is_cached())
                {
                    std::sort(begin(), end(), Pred());
                    set_cached();
                }
                auto it = lower_bound(begin(), end(), str, Pred());
                if (it != end() && it->key_ == str)
                    return *it;
#ifndef CROW_JSON_NO_ERROR_CHECK
                throw std::runtime_error("cannot find key: " + str);
#else
                static rvalue nullValue;
                return nullValue;
#endif
            }

            void set_error()
            {
                option_ |= error_bit;
            }

            bool error() const
            {
                return (option_ & error_bit) != 0;
            }

            std::vector<std::string> keys() const
            {
#ifndef CROW_JSON_NO_ERROR_CHECK
                if (t() != type::Object)
                    throw std::runtime_error("value is not an object");
#endif
                std::vector<std::string> ret;
                ret.reserve(lsize_);
                for (uint32_t i = 0; i < lsize_; i++)
                {
                    ret.emplace_back(std::string(l_[i].key()));
                }
                return ret;
            }

        private:
            bool is_cached() const
            {
                return (option_ & cached_bit) != 0;
            }
            void set_cached() const
            {
                option_ |= cached_bit;
            }
            void copy_l(const rvalue& r)
            {
                if (r.t() != type::Object && r.t() != type::List)
                    return;
                lsize_ = r.lsize_;
                lremain_ = 0;
                l_.reset(new rvalue[lsize_]);
                std::copy(r.begin(), r.end(), begin());
            }

            void emplace_back(rvalue&& v)
            {
                if (!lremain_)
                {
                    int new_size = lsize_ + lsize_;
                    if (new_size - lsize_ > 60000)
                        new_size = lsize_ + 60000;
                    if (new_size < 4)
                        new_size = 4;
                    rvalue* p = new rvalue[new_size];
                    rvalue* p2 = p;
                    for (auto& x : *this)
                        *p2++ = std::move(x);
                    l_.reset(p);
                    lremain_ = new_size - lsize_;
                }
                l_[lsize_++] = std::move(v);
                lremain_--;
            }

            /// Determines num_type from the string.
            void determine_num_type()
            {
                if (t_ != type::Number)
                {
                    nt_ = num_type::Null;
                    return;
                }

                const std::size_t len = end_ - start_;
                const bool has_minus = std::memchr(start_, '-', len) != nullptr;
                const bool has_e = std::memchr(start_, 'e', len) != nullptr || std::memchr(start_, 'E', len) != nullptr;
                const bool has_dec_sep = std::memchr(start_, '.', len) != nullptr;
                if (has_dec_sep || has_e)
                    nt_ = num_type::Floating_point;
                else if (has_minus)
                    nt_ = num_type::Signed_integer;
                else
                    nt_ = num_type::Unsigned_integer;
            }

            mutable char* start_;
            mutable char* end_;
            detail::r_string key_;
            std::unique_ptr<rvalue[]> l_;
            uint32_t lsize_;
            uint16_t lremain_;
            type t_;
            num_type nt_{num_type::Null};
            mutable uint8_t option_{0};

            friend rvalue load_nocopy_internal(char* data, size_t size);
            friend rvalue load(const char* data, size_t size);
            friend std::ostream& operator<<(std::ostream& os, const rvalue& r)
            {
                switch (r.t_)
                {

                    case type::Null: os << "null"; break;
                    case type::False: os << "false"; break;
                    case type::True: os << "true"; break;
                    case type::Number:
                    {
                        switch (r.nt())
                        {
                            case num_type::Floating_point: os << r.d(); break;
                            case num_type::Double_precision_floating_point: os << r.d(); break;
                            case num_type::Signed_integer: os << r.i(); break;
                            case num_type::Unsigned_integer: os << r.u(); break;
                            case num_type::Null: throw std::runtime_error("Number with num_type Null");
                        }
                    }
                    break;
                    case type::String: os << '"' << r.s() << '"'; break;
                    case type::List:
                    {
                        os << '[';
                        bool first = true;
                        for (auto& x : r)
                        {
                            if (!first)
                                os << ',';
                            first = false;
                            os << x;
                        }
                        os << ']';
                    }
                    break;
                    case type::Object:
                    {
                        os << '{';
                        bool first = true;
                        for (auto& x : r)
                        {
                            if (!first)
                                os << ',';
                            os << '"' << escape(x.key_) << "\":";
                            first = false;
                            os << x;
                        }
                        os << '}';
                    }
                    break;
                    case type::Function: os << "custom function"; break;
                }
                return os;
            }
        };
        namespace detail
        {
        }

        inline bool operator==(const rvalue& l, const std::string& r)
        {
            return l.s() == r;
        }

        inline bool operator==(const std::string& l, const rvalue& r)
        {
            return l == r.s();
        }

        inline bool operator!=(const rvalue& l, const std::string& r)
        {
            return l.s() != r;
        }

        inline bool operator!=(const std::string& l, const rvalue& r)
        {
            return l != r.s();
        }

        inline bool operator==(const rvalue& l, const int& r)
        {
          return l.i() == r;
        }

        inline bool operator==(const int& l, const rvalue& r)
        {
          return l == r.i();
        }

        inline bool operator!=(const rvalue& l, const int& r)
        {
          return l.i() != r;
        }

        inline bool operator!=(const int& l, const rvalue& r)
        {
          return l != r.i();
        }


        inline rvalue load_nocopy_internal(char* data, size_t size)
        {
            // Defend against excessive recursion
            static constexpr unsigned max_depth = 10000;

            //static const char* escaped = "\"\\/\b\f\n\r\t";
            struct Parser
            {
                Parser(char* data_, size_t /*size*/):
                  data(data_)
                {
                }

                bool consume(char c)
                {
                    if (CROW_UNLIKELY(*data != c))
                        return false;
                    data++;
                    return true;
                }

                void ws_skip()
                {
                    while (*data == ' ' || *data == '\t' || *data == '\r' || *data == '\n')
                        ++data;
                }

                rvalue decode_string()
                {
                    if (CROW_UNLIKELY(!consume('"')))
                        return {};
                    char* start = data;
                    uint8_t has_escaping = 0;
                    while (1)
                    {
                        if (CROW_LIKELY(*data != '"' && *data != '\\' && *data != '\0'))
                        {
                            data++;
                        }
                        else if (*data == '"')
                        {
                            *data = 0;
                            *(start - 1) = has_escaping;
                            data++;
                            return {type::String, start, data - 1};
                        }
                        else if (*data == '\\')
                        {
                            has_escaping = 1;
                            data++;
                            switch (*data)
                            {
                                case 'u':
                                {
                                    auto check = [](char c) {
                                        return ('0' <= c && c <= '9') ||
                                               ('a' <= c && c <= 'f') ||
                                               ('A' <= c && c <= 'F');
                                    };
                                    if (!(check(*(data + 1)) &&
                                          check(*(data + 2)) &&
                                          check(*(data + 3)) &&
                                          check(*(data + 4))))
                                        return {};
                                }
                                    data += 5;
                                    break;
                                case '"':
                                case '\\':
                                case '/':
                                case 'b':
                                case 'f':
                                case 'n':
                                case 'r':
                                case 't':
                                    data++;
                                    break;
                                default:
                                    return {};
                            }
                        }
                        else
                            return {};
                    }
                    return {};
                }

                rvalue decode_list(unsigned depth)
                {
                    rvalue ret(type::List);
                    if (CROW_UNLIKELY(!consume('[')) || CROW_UNLIKELY(depth > max_depth))
                    {
                        ret.set_error();
                        return ret;
                    }
                    ws_skip();
                    if (CROW_UNLIKELY(*data == ']'))
                    {
                        data++;
                        return ret;
                    }

                    while (1)
                    {
                        auto v = decode_value(depth + 1);
                        if (CROW_UNLIKELY(!v))
                        {
                            ret.set_error();
                            break;
                        }
                        ws_skip();
                        ret.emplace_back(std::move(v));
                        if (*data == ']')
                        {
                            data++;
                            break;
                        }
                        if (CROW_UNLIKELY(!consume(',')))
                        {
                            ret.set_error();
                            break;
                        }
                        ws_skip();
                    }
                    return ret;
                }

                rvalue decode_number()
                {
                    char* start = data;

                    enum NumberParsingState
                    {
                        Minus,
                        AfterMinus,
                        ZeroFirst,
                        Digits,
                        DigitsAfterPoints,
                        E,
                        DigitsAfterE,
                        Invalid,
                    } state{Minus};
                    while (CROW_LIKELY(state != Invalid))
                    {
                        switch (*data)
                        {
                            case '0':
                                state = static_cast<NumberParsingState>("\2\2\7\3\4\6\6"[state]);
                                /*if (state == NumberParsingState::Minus || state == NumberParsingState::AfterMinus)
                                {
                                    state = NumberParsingState::ZeroFirst;
                                }
                                else if (state == NumberParsingState::Digits ||
                                    state == NumberParsingState::DigitsAfterE ||
                                    state == NumberParsingState::DigitsAfterPoints)
                                {
                                    // ok; pass
                                }
                                else if (state == NumberParsingState::E)
                                {
                                    state = NumberParsingState::DigitsAfterE;
                                }
                                else
                                    return {};*/
                                break;
                            case '1':
                            case '2':
                            case '3':
                            case '4':
                            case '5':
                            case '6':
                            case '7':
                            case '8':
                            case '9':
                                state = static_cast<NumberParsingState>("\3\3\7\3\4\6\6"[state]);
                                while (*(data + 1) >= '0' && *(data + 1) <= '9')
                                    data++;
                                /*if (state == NumberParsingState::Minus || state == NumberParsingState::AfterMinus)
                                {
                                    state = NumberParsingState::Digits;
                                }
                                else if (state == NumberParsingState::Digits ||
                                    state == NumberParsingState::DigitsAfterE ||
                                    state == NumberParsingState::DigitsAfterPoints)
                                {
                                    // ok; pass
                                }
                                else if (state == NumberParsingState::E)
                                {
                                    state = NumberParsingState::DigitsAfterE;
                                }
                                else
                                    return {};*/
                                break;
                            case '.':
                                state = static_cast<NumberParsingState>("\7\7\4\4\7\7\7"[state]);
                                /*
                                if (state == NumberParsingState::Digits || state == NumberParsingState::ZeroFirst)
                                {
                                    state = NumberParsingState::DigitsAfterPoints;
                                }
                                else
                                    return {};
                                */
                                break;
                            case '-':
                                state = static_cast<NumberParsingState>("\1\7\7\7\7\6\7"[state]);
                                /*if (state == NumberParsingState::Minus)
                                {
                                    state = NumberParsingState::AfterMinus;
                                }
                                else if (state == NumberParsingState::E)
                                {
                                    state = NumberParsingState::DigitsAfterE;
                                }
                                else
                                    return {};*/
                                break;
                            case '+':
                                state = static_cast<NumberParsingState>("\7\7\7\7\7\6\7"[state]);
                                /*if (state == NumberParsingState::E)
                                {
                                    state = NumberParsingState::DigitsAfterE;
                                }
                                else
                                    return {};*/
                                break;
                            case 'e':
                            case 'E':
                                state = static_cast<NumberParsingState>("\7\7\7\5\5\7\7"[state]);
                                /*if (state == NumberParsingState::Digits ||
                                    state == NumberParsingState::DigitsAfterPoints)
                                {
                                    state = NumberParsingState::E;
                                }
                                else
                                    return {};*/
                                break;
                            default:
                                if (CROW_LIKELY(state == NumberParsingState::ZeroFirst ||
                                                state == NumberParsingState::Digits ||
                                                state == NumberParsingState::DigitsAfterPoints ||
                                                state == NumberParsingState::DigitsAfterE))
                                    return {type::Number, start, data};
                                else
                                    return {};
                        }
                        data++;
                    }

                    return {};
                }


                rvalue decode_value(unsigned depth)
                {
                    switch (*data)
                    {
                        case '[':
                            return decode_list(depth + 1);
                        case '{':
                            return decode_object(depth + 1);
                        case '"':
                            return decode_string();
                        case 't':
                            if ( //e-data >= 4 &&
                              data[1] == 'r' &&
                              data[2] == 'u' &&
                              data[3] == 'e')
                            {
                                data += 4;
                                return {type::True};
                            }
                            else
                                return {};
                        case 'f':
                            if ( //e-data >= 5 &&
                              data[1] == 'a' &&
                              data[2] == 'l' &&
                              data[3] == 's' &&
                              data[4] == 'e')
                            {
                                data += 5;
                                return {type::False};
                            }
                            else
                                return {};
                        case 'n':
                            if ( //e-data >= 4 &&
                              data[1] == 'u' &&
                              data[2] == 'l' &&
                              data[3] == 'l')
                            {
                                data += 4;
                                return {type::Null};
                            }
                            else
                                return {};
                        //case '1': case '2': case '3':
                        //case '4': case '5': case '6':
                        //case '7': case '8': case '9':
                        //case '0': case '-':
                        default:
                            return decode_number();
                    }
                    return {};
                }

                rvalue decode_object(unsigned depth)
                {
                    rvalue ret(type::Object);
                    if (CROW_UNLIKELY(!consume('{')) || CROW_UNLIKELY(depth > max_depth))
                    {
                        ret.set_error();
                        return ret;
                    }

                    ws_skip();

                    if (CROW_UNLIKELY(*data == '}'))
                    {
                        data++;
                        return ret;
                    }

                    while (1)
                    {
                        auto t = decode_string();
                        if (CROW_UNLIKELY(!t))
                        {
                            ret.set_error();
                            break;
                        }

                        ws_skip();
                        if (CROW_UNLIKELY(!consume(':')))
                        {
                            ret.set_error();
                            break;
                        }

                        // TODO(ipkn) caching key to speed up (flyweight?)
                        // I have no idea how flyweight could apply here, but maybe some speedup can happen if we stopped checking type since decode_string returns a string anyway
                        auto key = t.s();

                        ws_skip();
                        auto v = decode_value(depth + 1);
                        if (CROW_UNLIKELY(!v))
                        {
                            ret.set_error();
                            break;
                        }
                        ws_skip();

                        v.key_ = std::move(key);
                        ret.emplace_back(std::move(v));
                        if (CROW_UNLIKELY(*data == '}'))
                        {
                            data++;
                            break;
                        }
                        if (CROW_UNLIKELY(!consume(',')))
                        {
                            ret.set_error();
                            break;
                        }
                        ws_skip();
                    }
                    return ret;
                }

                rvalue parse()
                {
                    ws_skip();
                    auto ret = decode_value(0); // or decode object?
                    ws_skip();
                    if (ret && *data != '\0')
                        ret.set_error();
                    return ret;
                }

                char* data;
            };
            return Parser(data, size).parse();
        }
        inline rvalue load(const char* data, size_t size)
        {
            char* s = new char[size + 1];
            memcpy(s, data, size);
            s[size] = 0;
            auto ret = load_nocopy_internal(s, size);
            if (ret)
                ret.key_.force(s, size);
            else
                delete[] s;
            return ret;
        }

        inline rvalue load(const char* data)
        {
            return load(data, strlen(data));
        }

        inline rvalue load(const std::string& str)
        {
            return load(str.data(), str.size());
        }

        struct wvalue_reader;

        /// JSON write value.

        ///
        /// Value can mean any json value, including a JSON object.<br>
        /// Write means this class is used to primarily assemble JSON objects using keys and values and export those into a string.
        class wvalue : public returnable
        {
            friend class crow::mustache::template_t;
            friend struct wvalue_reader;

        public:
            using object =
#ifdef CROW_JSON_USE_MAP
              std::map<std::string, wvalue>;
#else
              std::unordered_map<std::string, wvalue>;
#endif

            using list = std::vector<wvalue>;

            type t() const { return t_; }

            /// Create an empty json value (outputs "{}" instead of a "null" string)
            static crow::json::wvalue empty_object() { return crow::json::wvalue::object(); }

        private:
            type t_{type::Null};         ///< The type of the value.
            num_type nt{num_type::Null}; ///< The specific type of the number if \ref t_ is a number.
            union number
            {
                double d;
                int64_t si;
                uint64_t ui;

            public:
                constexpr number() noexcept:
                  ui() {} /* default constructor initializes unsigned integer. */
                constexpr number(std::uint64_t value) noexcept:
                  ui(value) {}
                constexpr number(std::int64_t value) noexcept:
                  si(value) {}
                explicit constexpr number(double value) noexcept:
                  d(value) {}
                explicit constexpr number(float value) noexcept:
                  d(value) {}
            } num;                                      ///< Value if type is a number.
            std::string s;                              ///< Value if type is a string.
            std::unique_ptr<list> l;                    ///< Value if type is a list.
            std::unique_ptr<object> o;                  ///< Value if type is a JSON object.
            std::function<std::string(std::string&)> f; ///< Value if type is a function (C++ lambda)

        public:
            wvalue():
              returnable("application/json") {}

            wvalue(std::nullptr_t):
              returnable("application/json"), t_(type::Null) {}

            wvalue(bool value):
              returnable("application/json"), t_(value ? type::True : type::False) {}

            wvalue(std::uint8_t value):
              returnable("application/json"), t_(type::Number), nt(num_type::Unsigned_integer), num(static_cast<std::uint64_t>(value)) {}
            wvalue(std::uint16_t value):
              returnable("application/json"), t_(type::Number), nt(num_type::Unsigned_integer), num(static_cast<std::uint64_t>(value)) {}
            wvalue(std::uint32_t value):
              returnable("application/json"), t_(type::Number), nt(num_type::Unsigned_integer), num(static_cast<std::uint64_t>(value)) {}
            wvalue(std::uint64_t value):
              returnable("application/json"), t_(type::Number), nt(num_type::Unsigned_integer), num(static_cast<std::uint64_t>(value)) {}

            wvalue(std::int8_t value):
              returnable("application/json"), t_(type::Number), nt(num_type::Signed_integer), num(static_cast<std::int64_t>(value)) {}
            wvalue(std::int16_t value):
              returnable("application/json"), t_(type::Number), nt(num_type::Signed_integer), num(static_cast<std::int64_t>(value)) {}
            wvalue(std::int32_t value):
              returnable("application/json"), t_(type::Number), nt(num_type::Signed_integer), num(static_cast<std::int64_t>(value)) {}
            wvalue(std::int64_t value):
              returnable("application/json"), t_(type::Number), nt(num_type::Signed_integer), num(static_cast<std::int64_t>(value)) {}

            wvalue(float value):
              returnable("application/json"), t_(type::Number), nt(num_type::Floating_point), num(static_cast<double>(value)) {}
            wvalue(double value):
              returnable("application/json"), t_(type::Number), nt(num_type::Double_precision_floating_point), num(static_cast<double>(value)) {}

            wvalue(char const* value):
              returnable("application/json"), t_(type::String), s(value) {}

            wvalue(std::string const& value):
              returnable("application/json"), t_(type::String), s(value) {}
            wvalue(std::string&& value):
              returnable("application/json"), t_(type::String), s(std::move(value)) {}

            wvalue(std::initializer_list<std::pair<std::string const, wvalue>> initializer_list):
              returnable("application/json"), t_(type::Object), o(new object(initializer_list)) {}

            wvalue(object const& value):
              returnable("application/json"), t_(type::Object), o(new object(value)) {}
            wvalue(object&& value):
              returnable("application/json"), t_(type::Object), o(new object(std::move(value))) {}

            wvalue(const list& r):
              returnable("application/json")
            {
                t_ = type::List;
                l = std::unique_ptr<list>(new list{});
                l->reserve(r.size());
                for (auto it = r.begin(); it != r.end(); ++it)
                    l->emplace_back(*it);
            }
            wvalue(list& r):
              returnable("application/json")
            {
                t_ = type::List;
                l = std::unique_ptr<list>(new list{});
                l->reserve(r.size());
                for (auto it = r.begin(); it != r.end(); ++it)
                    l->emplace_back(*it);
            }

            /// Create a write value from a read value (useful for editing JSON strings).
            wvalue(const rvalue& r):
              returnable("application/json")
            {
                t_ = r.t();
                switch (r.t())
                {
                    case type::Null:
                    case type::False:
                    case type::True:
                    case type::Function:
                        return;
                    case type::Number:
                        nt = r.nt();
                        if (nt == num_type::Floating_point || nt == num_type::Double_precision_floating_point)
                            num.d = r.d();
                        else if (nt == num_type::Signed_integer)
                            num.si = r.i();
                        else
                            num.ui = r.u();
                        return;
                    case type::String:
                        s = r.s();
                        return;
                    case type::List:
                        l = std::unique_ptr<list>(new list{});
                        l->reserve(r.size());
                        for (auto it = r.begin(); it != r.end(); ++it)
                            l->emplace_back(*it);
                        return;
                    case type::Object:
                        o = std::unique_ptr<object>(new object{});
                        for (auto it = r.begin(); it != r.end(); ++it)
                            o->emplace(it->key(), *it);
                        return;
                }
            }

            wvalue(const wvalue& r):
              returnable("application/json")
            {
                t_ = r.t();
                switch (r.t())
                {
                    case type::Null:
                    case type::False:
                    case type::True:
                        return;
                    case type::Number:
                        nt = r.nt;
                        if (nt == num_type::Floating_point || nt == num_type::Double_precision_floating_point)
                            num.d = r.num.d;
                        else if (nt == num_type::Signed_integer)
                            num.si = r.num.si;
                        else
                            num.ui = r.num.ui;
                        return;
                    case type::String:
                        s = r.s;
                        return;
                    case type::List:
                        l = std::unique_ptr<list>(new list{});
                        l->reserve(r.size());
                        for (auto it = r.l->begin(); it != r.l->end(); ++it)
                            l->emplace_back(*it);
                        return;
                    case type::Object:
                        o = std::unique_ptr<object>(new object{});
                        o->insert(r.o->begin(), r.o->end());
                        return;
                    case type::Function:
                        f = r.f;
                }
            }

            wvalue(wvalue&& r):
              returnable("application/json")
            {
                *this = std::move(r);
            }

            wvalue& operator=(wvalue&& r)
            {
                t_ = r.t_;
                nt = r.nt;
                num = r.num;
                s = std::move(r.s);
                l = std::move(r.l);
                o = std::move(r.o);
                return *this;
            }

            /// Used for compatibility, same as \ref reset()
            void clear()
            {
                reset();
            }

            void reset()
            {
                t_ = type::Null;
                l.reset();
                o.reset();
            }

            wvalue& operator=(std::nullptr_t)
            {
                reset();
                return *this;
            }
            wvalue& operator=(bool value)
            {
                reset();
                if (value)
                    t_ = type::True;
                else
                    t_ = type::False;
                return *this;
            }

            wvalue& operator=(float value)
            {
                reset();
                t_ = type::Number;
                num.d = value;
                nt = num_type::Floating_point;
                return *this;
            }

            wvalue& operator=(double value)
            {
                reset();
                t_ = type::Number;
                num.d = value;
                nt = num_type::Double_precision_floating_point;
                return *this;
            }

            wvalue& operator=(unsigned short value)
            {
                reset();
                t_ = type::Number;
                num.ui = value;
                nt = num_type::Unsigned_integer;
                return *this;
            }

            wvalue& operator=(short value)
            {
                reset();
                t_ = type::Number;
                num.si = value;
                nt = num_type::Signed_integer;
                return *this;
            }

            wvalue& operator=(long long value)
            {
                reset();
                t_ = type::Number;
                num.si = value;
                nt = num_type::Signed_integer;
                return *this;
            }

            wvalue& operator=(long value)
            {
                reset();
                t_ = type::Number;
                num.si = value;
                nt = num_type::Signed_integer;
                return *this;
            }

            wvalue& operator=(int value)
            {
                reset();
                t_ = type::Number;
                num.si = value;
                nt = num_type::Signed_integer;
                return *this;
            }

            wvalue& operator=(unsigned long long value)
            {
                reset();
                t_ = type::Number;
                num.ui = value;
                nt = num_type::Unsigned_integer;
                return *this;
            }

            wvalue& operator=(unsigned long value)
            {
                reset();
                t_ = type::Number;
                num.ui = value;
                nt = num_type::Unsigned_integer;
                return *this;
            }

            wvalue& operator=(unsigned int value)
            {
                reset();
                t_ = type::Number;
                num.ui = value;
                nt = num_type::Unsigned_integer;
                return *this;
            }

            wvalue& operator=(const char* str)
            {
                reset();
                t_ = type::String;
                s = str;
                return *this;
            }

            wvalue& operator=(const std::string& str)
            {
                reset();
                t_ = type::String;
                s = str;
                return *this;
            }

            wvalue& operator=(list&& v)
            {
                if (t_ != type::List)
                    reset();
                t_ = type::List;
                if (!l)
                    l = std::unique_ptr<list>(new list{});
                l->clear();
                l->resize(v.size());
                size_t idx = 0;
                for (auto& x : v)
                {
                    (*l)[idx++] = std::move(x);
                }
                return *this;
            }

            template<typename T>
            wvalue& operator=(const std::vector<T>& v)
            {
                if (t_ != type::List)
                    reset();
                t_ = type::List;
                if (!l)
                    l = std::unique_ptr<list>(new list{});
                l->clear();
                l->resize(v.size());
                size_t idx = 0;
                for (auto& x : v)
                {
                    (*l)[idx++] = x;
                }
                return *this;
            }

            wvalue& operator=(std::initializer_list<std::pair<std::string const, wvalue>> initializer_list)
            {
                if (t_ != type::Object)
                {
                    reset();
                    t_ = type::Object;
                    o = std::unique_ptr<object>(new object(initializer_list));
                }
                else
                {
#if defined(__APPLE__) || defined(__MACH__) || defined(__FreeBSD__) || defined(__ANDROID__) || defined(_LIBCPP_VERSION)
                    o = std::unique_ptr<object>(new object(initializer_list));
#else
                    (*o) = initializer_list;
#endif
                }
                return *this;
            }

            wvalue& operator=(object const& value)
            {
                if (t_ != type::Object)
                {
                    reset();
                    t_ = type::Object;
                    o = std::unique_ptr<object>(new object(value));
                }
                else
                {
#if defined(__APPLE__) || defined(__MACH__) || defined(__FreeBSD__) || defined(__ANDROID__) || defined(_LIBCPP_VERSION)
                    o = std::unique_ptr<object>(new object(value));
#else
                    (*o) = value;
#endif
                }
                return *this;
            }

            wvalue& operator=(object&& value)
            {
                if (t_ != type::Object)
                {
                    reset();
                    t_ = type::Object;
                    o = std::unique_ptr<object>(new object(std::move(value)));
                }
                else
                {
                    (*o) = std::move(value);
                }
                return *this;
            }

            wvalue& operator=(std::function<std::string(std::string&)>&& func)
            {
                reset();
                t_ = type::Function;
                f = std::move(func);
                return *this;
            }

            wvalue& operator[](unsigned index)
            {
                if (t_ != type::List)
                    reset();
                t_ = type::List;
                if (!l)
                    l = std::unique_ptr<list>(new list{});
                if (l->size() < index + 1)
                    l->resize(index + 1);
                return (*l)[index];
            }

            const wvalue& operator[](unsigned index) const
            {
                return const_cast<wvalue*>(this)->operator[](index);
            }

            int count(const std::string& str) const
            {
                if (t_ != type::Object)
                    return 0;
                if (!o)
                    return 0;
                return o->count(str);
            }

            wvalue& operator[](const std::string& str)
            {
                if (t_ != type::Object)
                    reset();
                t_ = type::Object;
                if (!o)
                    o = std::unique_ptr<object>(new object{});
                return (*o)[str];
            }

            const wvalue& operator[](const std::string& str) const
            {
                return const_cast<wvalue*>(this)->operator[](str);
            }

            std::vector<std::string> keys() const
            {
                if (t_ != type::Object)
                    return {};
                std::vector<std::string> result;
                for (auto& kv : *o)
                {
                    result.push_back(kv.first);
                }
                return result;
            }

            std::string execute(std::string txt = "") const //Not using reference because it cannot be used with a default rvalue
            {
                if (t_ != type::Function)
                    return "";
                return f(txt);
            }

            /// If the wvalue is a list, it returns the length of the list, otherwise it returns 1.
            std::size_t size() const
            {
                if (t_ != type::List)
                    return 1;
                return l->size();
            }

            /// Returns an estimated size of the value in bytes.
            size_t estimate_length() const
            {
                switch (t_)
                {
                    case type::Null: return 4;
                    case type::False: return 5;
                    case type::True: return 4;
                    case type::Number: return 30;
                    case type::String: return 2 + s.size() + s.size() / 2;
                    case type::List:
                    {
                        size_t sum{};
                        if (l)
                        {
                            for (auto& x : *l)
                            {
                                sum += 1;
                                sum += x.estimate_length();
                            }
                        }
                        return sum + 2;
                    }
                    case type::Object:
                    {
                        size_t sum{};
                        if (o)
                        {
                            for (auto& kv : *o)
                            {
                                sum += 2;
                                sum += 2 + kv.first.size() + kv.first.size() / 2;
                                sum += kv.second.estimate_length();
                            }
                        }
                        return sum + 2;
                    }
                    case type::Function:
                        return 0;
                }
                return 1;
            }

        private:
            inline void dump_string(const std::string& str, std::string& out) const
            {
                out.push_back('"');
                escape(str, out);
                out.push_back('"');
            }

            inline void dump_indentation_part(std::string& out, const int indent, const char separator, const int indent_level) const
            {
                out.push_back('\n');
                out.append(indent_level * indent, separator);
            }


            inline void dump_internal(const wvalue& v, std::string& out, const int indent, const char separator, const int indent_level = 0) const
            {
                switch (v.t_)
                {
                    case type::Null: out += "null"; break;
                    case type::False: out += "false"; break;
                    case type::True: out += "true"; break;
                    case type::Number:
                    {
                        if (v.nt == num_type::Floating_point || v.nt == num_type::Double_precision_floating_point)
                        {
                            if (isnan(v.num.d) || isinf(v.num.d))
                            {
                                out += "null";
                                CROW_LOG_WARNING << "Invalid JSON value detected (" << v.num.d << "), value set to null";
                                break;
                            }
                            enum
                            {
                                start,
                                decp, // Decimal point
                                zero
                            } f_state;
                            char outbuf[128];
                            if (v.nt == num_type::Double_precision_floating_point)
                            {
#ifdef _MSC_VER
                                sprintf_s(outbuf, sizeof(outbuf), "%.*g", DECIMAL_DIG, v.num.d);
#else
                                snprintf(outbuf, sizeof(outbuf), "%.*g", DECIMAL_DIG, v.num.d);
#endif
                            }
                            else
                            {
#ifdef _MSC_VER
                                sprintf_s(outbuf, sizeof(outbuf), "%f", v.num.d);
#else
                                snprintf(outbuf, sizeof(outbuf), "%f", v.num.d);
#endif
                            }
                            char* p = &outbuf[0];
                            char* pos_first_trailing_0 = nullptr;
                            f_state = start;
                            while (*p != '\0')
                            {
                                //std::cout << *p << std::endl;
                                char ch = *p;
                                switch (f_state)
                                {
                                    case start: // Loop and lookahead until a decimal point is found
                                        if (ch == '.')
                                        {
                                            char fch = *(p + 1);
                                            // if the first character is 0, leave it be (this is so that "1.00000" becomes "1.0" and not "1.")
                                            if (fch != '\0' && fch == '0') p++;
                                            f_state = decp;
                                        }
                                        p++;
                                        break;
                                    case decp: // Loop until a 0 is found, if found, record its position
                                        if (ch == '0')
                                        {
                                            f_state = zero;
                                            pos_first_trailing_0 = p;
                                        }
                                        p++;
                                        break;
                                    case zero: // if a non 0 is found (e.g. 1.00004) remove the earlier recorded 0 position and look for more trailing 0s
                                        if (ch != '0')
                                        {
                                            pos_first_trailing_0 = nullptr;
                                            f_state = decp;
                                        }
                                        p++;
                                        break;
                                }
                            }
                            if (pos_first_trailing_0 != nullptr) // if any trailing 0s are found, terminate the string where they begin
                                *pos_first_trailing_0 = '\0';
                            out += outbuf;
                        }
                        else if (v.nt == num_type::Signed_integer)
                        {
                            out += std::to_string(v.num.si);
                        }
                        else
                        {
                            out += std::to_string(v.num.ui);
                        }
                    }
                    break;
                    case type::String: dump_string(v.s, out); break;
                    case type::List:
                    {
                        out.push_back('[');

                        if (indent >= 0)
                        {
                            dump_indentation_part(out, indent, separator, indent_level + 1);
                        }

                        if (v.l)
                        {
                            bool first = true;
                            for (auto& x : *v.l)
                            {
                                if (!first)
                                {
                                    out.push_back(',');

                                    if (indent >= 0)
                                    {
                                        dump_indentation_part(out, indent, separator, indent_level + 1);
                                    }
                                }
                                first = false;
                                dump_internal(x, out, indent, separator, indent_level + 1);
                            }
                        }

                        if (indent >= 0)
                        {
                            dump_indentation_part(out, indent, separator, indent_level);
                        }

                        out.push_back(']');
                    }
                    break;
                    case type::Object:
                    {
                        out.push_back('{');

                        if (indent >= 0)
                        {
                            dump_indentation_part(out, indent, separator, indent_level + 1);
                        }

                        if (v.o)
                        {
                            bool first = true;
                            for (auto& kv : *v.o)
                            {
                                if (!first)
                                {
                                    out.push_back(',');
                                    if (indent >= 0)
                                    {
                                        dump_indentation_part(out, indent, separator, indent_level + 1);
                                    }
                                }
                                first = false;
                                dump_string(kv.first, out);
                                out.push_back(':');

                                if (indent >= 0)
                                {
                                    out.push_back(' ');
                                }

                                dump_internal(kv.second, out, indent, separator, indent_level + 1);
                            }
                        }

                        if (indent >= 0)
                        {
                            dump_indentation_part(out, indent, separator, indent_level);
                        }

                        out.push_back('}');
                    }
                    break;

                    case type::Function:
                        out += "custom function";
                        break;
                }
            }

        public:
            std::string dump(const int indent, const char separator = ' ') const
            {
                std::string ret;
                ret.reserve(estimate_length());
                dump_internal(*this, ret, indent, separator);
                return ret;
            }

            std::string dump() const override
            {
                static constexpr int DontIndent = -1;

                return dump(DontIndent);
            }
        };

        // Used for accessing the internals of a wvalue
        struct wvalue_reader
        {
            int64_t get(int64_t fallback)
            {
                if (ref.t() != type::Number || ref.nt == num_type::Floating_point ||
                    ref.nt == num_type::Double_precision_floating_point)
                    return fallback;
                return ref.num.si;
            }

            double get(double fallback)
            {
                if (ref.t() != type::Number || ref.nt != num_type::Floating_point ||
                    ref.nt == num_type::Double_precision_floating_point)
                    return fallback;
                return ref.num.d;
            }

            bool get(bool fallback)
            {
                if (ref.t() == type::True) return true;
                if (ref.t() == type::False) return false;
                return fallback;
            }

            std::string get(const std::string& fallback)
            {
                if (ref.t() != type::String) return fallback;
                return ref.s;
            }

            const wvalue& ref;
        };

        //std::vector<asio::const_buffer> dump_ref(wvalue& v)
        //{
        //}
    } // namespace json
} // namespace crow
