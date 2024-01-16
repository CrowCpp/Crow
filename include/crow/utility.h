#pragma once

#include <cstdint>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <cstring>
#include <cctype>
#include <functional>
#include <string>
#include <sstream>
#include <unordered_map>
#include <random>

#include "crow/settings.h"

#if defined(CROW_CAN_USE_CPP17) && !defined(CROW_FILESYSTEM_IS_EXPERIMENTAL)
#include <filesystem>
#endif

// TODO(EDev): Adding C++20's [[likely]] and [[unlikely]] attributes might be useful
#if defined(__GNUG__) || defined(__clang__)
#define CROW_LIKELY(X) __builtin_expect(!!(X), 1)
#define CROW_UNLIKELY(X) __builtin_expect(!!(X), 0)
#else
#define CROW_LIKELY(X) (X)
#define CROW_UNLIKELY(X) (X)
#endif

namespace crow
{
    /// @cond SKIP
    namespace black_magic
    {
#ifndef CROW_MSVC_WORKAROUND
        /// Out of Range Exception for const_str
        struct OutOfRange
        {
            OutOfRange(unsigned /*pos*/, unsigned /*length*/) {}
        };
        /// Helper function to throw an exception if i is larger than len
        constexpr unsigned requires_in_range(unsigned i, unsigned len)
        {
            return i >= len ? throw OutOfRange(i, len) : i;
        }

        /// A constant string implementation.
        class const_str
        {
            const char* const begin_;
            unsigned size_;

        public:
            template<unsigned N>
            constexpr const_str(const char (&arr)[N]):
              begin_(arr), size_(N - 1)
            {
                static_assert(N >= 1, "not a string literal");
            }
            constexpr char operator[](unsigned i) const
            {
                return requires_in_range(i, size_), begin_[i];
            }

            constexpr operator const char*() const
            {
                return begin_;
            }

            constexpr const char* begin() const { return begin_; }
            constexpr const char* end() const { return begin_ + size_; }

            constexpr unsigned size() const
            {
                return size_;
            }
        };

        constexpr unsigned find_closing_tag(const_str s, unsigned p)
        {
            return s[p] == '>' ? p : find_closing_tag(s, p + 1);
        }

        /// Check that the CROW_ROUTE string is valid
        constexpr bool is_valid(const_str s, unsigned i = 0, int f = 0)
        {
            return i == s.size()   ? f == 0 :
                   f < 0 || f >= 2 ? false :
                   s[i] == '<'     ? is_valid(s, i + 1, f + 1) :
                   s[i] == '>'     ? is_valid(s, i + 1, f - 1) :
                                     is_valid(s, i + 1, f);
        }

        constexpr bool is_equ_p(const char* a, const char* b, unsigned n)
        {
            return *a == 0 && *b == 0 && n == 0 ? true :
                   (*a == 0 || *b == 0)         ? false :
                   n == 0                       ? true :
                   *a != *b                     ? false :
                                                  is_equ_p(a + 1, b + 1, n - 1);
        }

        constexpr bool is_equ_n(const_str a, unsigned ai, const_str b, unsigned bi, unsigned n)
        {
            return ai + n > a.size() || bi + n > b.size() ? false :
                   n == 0                                 ? true :
                   a[ai] != b[bi]                         ? false :
                                                            is_equ_n(a, ai + 1, b, bi + 1, n - 1);
        }

        constexpr bool is_int(const_str s, unsigned i)
        {
            return is_equ_n(s, i, "<int>", 0, 5);
        }

        constexpr bool is_uint(const_str s, unsigned i)
        {
            return is_equ_n(s, i, "<uint>", 0, 6);
        }

        constexpr bool is_float(const_str s, unsigned i)
        {
            return is_equ_n(s, i, "<float>", 0, 7) ||
                   is_equ_n(s, i, "<double>", 0, 8);
        }

        constexpr bool is_str(const_str s, unsigned i)
        {
            return is_equ_n(s, i, "<str>", 0, 5) ||
                   is_equ_n(s, i, "<string>", 0, 8);
        }

        constexpr bool is_path(const_str s, unsigned i)
        {
            return is_equ_n(s, i, "<path>", 0, 6);
        }
#endif
        template<typename T>
        struct parameter_tag
        {
            static const int value = 0;
        };
#define CROW_INTERNAL_PARAMETER_TAG(t, i) \
    template<>                            \
    struct parameter_tag<t>               \
    {                                     \
        static const int value = i;       \
    }
        CROW_INTERNAL_PARAMETER_TAG(int, 1);
        CROW_INTERNAL_PARAMETER_TAG(char, 1);
        CROW_INTERNAL_PARAMETER_TAG(short, 1);
        CROW_INTERNAL_PARAMETER_TAG(long, 1);
        CROW_INTERNAL_PARAMETER_TAG(long long, 1);
        CROW_INTERNAL_PARAMETER_TAG(unsigned int, 2);
        CROW_INTERNAL_PARAMETER_TAG(unsigned char, 2);
        CROW_INTERNAL_PARAMETER_TAG(unsigned short, 2);
        CROW_INTERNAL_PARAMETER_TAG(unsigned long, 2);
        CROW_INTERNAL_PARAMETER_TAG(unsigned long long, 2);
        CROW_INTERNAL_PARAMETER_TAG(double, 3);
        CROW_INTERNAL_PARAMETER_TAG(std::string, 4);
#undef CROW_INTERNAL_PARAMETER_TAG
        template<typename... Args>
        struct compute_parameter_tag_from_args_list;

        template<>
        struct compute_parameter_tag_from_args_list<>
        {
            static const int value = 0;
        };

        template<typename Arg, typename... Args>
        struct compute_parameter_tag_from_args_list<Arg, Args...>
        {
            static const int sub_value =
              compute_parameter_tag_from_args_list<Args...>::value;
            static const int value =
              parameter_tag<typename std::decay<Arg>::type>::value ? sub_value * 6 + parameter_tag<typename std::decay<Arg>::type>::value : sub_value;
        };

        static inline bool is_parameter_tag_compatible(uint64_t a, uint64_t b)
        {
            if (a == 0)
                return b == 0;
            if (b == 0)
                return a == 0;
            int sa = a % 6;
            int sb = a % 6;
            if (sa == 5) sa = 4;
            if (sb == 5) sb = 4;
            if (sa != sb)
                return false;
            return is_parameter_tag_compatible(a / 6, b / 6);
        }

        static inline unsigned find_closing_tag_runtime(const char* s, unsigned p)
        {
            return s[p] == 0   ? throw std::runtime_error("unmatched tag <") :
                   s[p] == '>' ? p :
                                 find_closing_tag_runtime(s, p + 1);
        }

        static inline uint64_t get_parameter_tag_runtime(const char* s, unsigned p = 0)
        {
            return s[p] == 0   ? 0 :
                   s[p] == '<' ? (
                                   std::strncmp(s + p, "<int>", 5) == 0  ? get_parameter_tag_runtime(s, find_closing_tag_runtime(s, p)) * 6 + 1 :
                                   std::strncmp(s + p, "<uint>", 6) == 0 ? get_parameter_tag_runtime(s, find_closing_tag_runtime(s, p)) * 6 + 2 :
                                   (std::strncmp(s + p, "<float>", 7) == 0 ||
                                    std::strncmp(s + p, "<double>", 8) == 0) ?
                                                                           get_parameter_tag_runtime(s, find_closing_tag_runtime(s, p)) * 6 + 3 :
                                   (std::strncmp(s + p, "<str>", 5) == 0 ||
                                    std::strncmp(s + p, "<string>", 8) == 0) ?
                                                                           get_parameter_tag_runtime(s, find_closing_tag_runtime(s, p)) * 6 + 4 :
                                   std::strncmp(s + p, "<path>", 6) == 0 ? get_parameter_tag_runtime(s, find_closing_tag_runtime(s, p)) * 6 + 5 :
                                                                           throw std::runtime_error("invalid parameter type")) :
                                 get_parameter_tag_runtime(s, p + 1);
        }
#ifndef CROW_MSVC_WORKAROUND
        constexpr uint64_t get_parameter_tag(const_str s, unsigned p = 0)
        {
            return p == s.size() ? 0 :
                   s[p] == '<'   ? (
                                   is_int(s, p)   ? get_parameter_tag(s, find_closing_tag(s, p)) * 6 + 1 :
                                     is_uint(s, p)  ? get_parameter_tag(s, find_closing_tag(s, p)) * 6 + 2 :
                                     is_float(s, p) ? get_parameter_tag(s, find_closing_tag(s, p)) * 6 + 3 :
                                     is_str(s, p)   ? get_parameter_tag(s, find_closing_tag(s, p)) * 6 + 4 :
                                     is_path(s, p)  ? get_parameter_tag(s, find_closing_tag(s, p)) * 6 + 5 :
                                                      throw std::runtime_error("invalid parameter type")) :
                                 get_parameter_tag(s, p + 1);
        }
#endif

        template<typename... T>
        struct S
        {
            template<typename U>
            using push = S<U, T...>;
            template<typename U>
            using push_back = S<T..., U>;
            template<template<typename... Args> class U>
            using rebind = U<T...>;
        };

        // Check whether the template function can be called with specific arguments
        template<typename F, typename Set>
        struct CallHelper;
        template<typename F, typename... Args>
        struct CallHelper<F, S<Args...>>
        {
            template<typename F1, typename... Args1, typename = decltype(std::declval<F1>()(std::declval<Args1>()...))>
            static char __test(int);

            template<typename...>
            static int __test(...);

            static constexpr bool value = sizeof(__test<F, Args...>(0)) == sizeof(char);
        };

        // Check Tuple contains type T
        template<typename T, typename Tuple>
        struct has_type;

        template<typename T>
        struct has_type<T, std::tuple<>> : std::false_type
        {};

        template<typename T, typename U, typename... Ts>
        struct has_type<T, std::tuple<U, Ts...>> : has_type<T, std::tuple<Ts...>>
        {};

        template<typename T, typename... Ts>
        struct has_type<T, std::tuple<T, Ts...>> : std::true_type
        {};

        // Find index of type in tuple
        template<class T, class Tuple>
        struct tuple_index;

        template<class T, class... Types>
        struct tuple_index<T, std::tuple<T, Types...>>
        {
            static const int value = 0;
        };

        template<class T, class U, class... Types>
        struct tuple_index<T, std::tuple<U, Types...>>
        {
            static const int value = 1 + tuple_index<T, std::tuple<Types...>>::value;
        };

        // Extract element from forward tuple or get default
#ifdef CROW_CAN_USE_CPP14
        template<typename T, typename Tup>
        typename std::enable_if<has_type<T&, Tup>::value, typename std::decay<T>::type&&>::type
          tuple_extract(Tup& tup)
        {
            return std::move(std::get<T&>(tup));
        }
#else
        template<typename T, typename Tup>
        typename std::enable_if<has_type<T&, Tup>::value, T&&>::type
          tuple_extract(Tup& tup)
        {
            return std::move(std::get<tuple_index<T&, Tup>::value>(tup));
        }
#endif

        template<typename T, typename Tup>
        typename std::enable_if<!has_type<T&, Tup>::value, T>::type
          tuple_extract(Tup&)
        {
            return T{};
        }

        // Kind of fold expressions in C++11
        template<bool...>
        struct bool_pack;
        template<bool... bs>
        using all_true = std::is_same<bool_pack<bs..., true>, bool_pack<true, bs...>>;

        template<int N>
        struct single_tag_to_type
        {};

        template<>
        struct single_tag_to_type<1>
        {
            using type = int64_t;
        };

        template<>
        struct single_tag_to_type<2>
        {
            using type = uint64_t;
        };

        template<>
        struct single_tag_to_type<3>
        {
            using type = double;
        };

        template<>
        struct single_tag_to_type<4>
        {
            using type = std::string;
        };

        template<>
        struct single_tag_to_type<5>
        {
            using type = std::string;
        };


        template<uint64_t Tag>
        struct arguments
        {
            using subarguments = typename arguments<Tag / 6>::type;
            using type =
              typename subarguments::template push<typename single_tag_to_type<Tag % 6>::type>;
        };

        template<>
        struct arguments<0>
        {
            using type = S<>;
        };

        template<typename... T>
        struct last_element_type
        {
            using type = typename std::tuple_element<sizeof...(T) - 1, std::tuple<T...>>::type;
        };


        template<>
        struct last_element_type<>
        {};


        // from http://stackoverflow.com/questions/13072359/c11-compile-time-array-with-logarithmic-evaluation-depth
        template<class T>
        using Invoke = typename T::type;

        template<unsigned...>
        struct seq
        {
            using type = seq;
        };

        template<class S1, class S2>
        struct concat;

        template<unsigned... I1, unsigned... I2>
        struct concat<seq<I1...>, seq<I2...>> : seq<I1..., (sizeof...(I1) + I2)...>
        {};

        template<class S1, class S2>
        using Concat = Invoke<concat<S1, S2>>;

        template<unsigned N>
        struct gen_seq;
        template<unsigned N>
        using GenSeq = Invoke<gen_seq<N>>;

        template<unsigned N>
        struct gen_seq : Concat<GenSeq<N / 2>, GenSeq<N - N / 2>>
        {};

        template<>
        struct gen_seq<0> : seq<>
        {};
        template<>
        struct gen_seq<1> : seq<0>
        {};

        template<typename Seq, typename Tuple>
        struct pop_back_helper;

        template<unsigned... N, typename Tuple>
        struct pop_back_helper<seq<N...>, Tuple>
        {
            template<template<typename... Args> class U>
            using rebind = U<typename std::tuple_element<N, Tuple>::type...>;
        };

        template<typename... T>
        struct pop_back //: public pop_back_helper<typename gen_seq<sizeof...(T)-1>::type, std::tuple<T...>>
        {
            template<template<typename... Args> class U>
            using rebind = typename pop_back_helper<typename gen_seq<sizeof...(T) - 1>::type, std::tuple<T...>>::template rebind<U>;
        };

        template<>
        struct pop_back<>
        {
            template<template<typename... Args> class U>
            using rebind = U<>;
        };

        // from http://stackoverflow.com/questions/2118541/check-if-c0x-parameter-pack-contains-a-type
        template<typename Tp, typename... List>
        struct contains : std::true_type
        {};

        template<typename Tp, typename Head, typename... Rest>
        struct contains<Tp, Head, Rest...> : std::conditional<std::is_same<Tp, Head>::value, std::true_type, contains<Tp, Rest...>>::type
        {};

        template<typename Tp>
        struct contains<Tp> : std::false_type
        {};

        template<typename T>
        struct empty_context
        {};

        template<typename T>
        struct promote
        {
            using type = T;
        };

#define CROW_INTERNAL_PROMOTE_TYPE(t1, t2) \
    template<>                             \
    struct promote<t1>                     \
    {                                      \
        using type = t2;                   \
    }

        CROW_INTERNAL_PROMOTE_TYPE(char, int64_t);
        CROW_INTERNAL_PROMOTE_TYPE(short, int64_t);
        CROW_INTERNAL_PROMOTE_TYPE(int, int64_t);
        CROW_INTERNAL_PROMOTE_TYPE(long, int64_t);
        CROW_INTERNAL_PROMOTE_TYPE(long long, int64_t);
        CROW_INTERNAL_PROMOTE_TYPE(unsigned char, uint64_t);
        CROW_INTERNAL_PROMOTE_TYPE(unsigned short, uint64_t);
        CROW_INTERNAL_PROMOTE_TYPE(unsigned int, uint64_t);
        CROW_INTERNAL_PROMOTE_TYPE(unsigned long, uint64_t);
        CROW_INTERNAL_PROMOTE_TYPE(unsigned long long, uint64_t);
        CROW_INTERNAL_PROMOTE_TYPE(float, double);
#undef CROW_INTERNAL_PROMOTE_TYPE

        template<typename T>
        using promote_t = typename promote<T>::type;

    } // namespace black_magic

    namespace detail
    {

        template<class T, std::size_t N, class... Args>
        struct get_index_of_element_from_tuple_by_type_impl
        {
            static constexpr auto value = N;
        };

        template<class T, std::size_t N, class... Args>
        struct get_index_of_element_from_tuple_by_type_impl<T, N, T, Args...>
        {
            static constexpr auto value = N;
        };

        template<class T, std::size_t N, class U, class... Args>
        struct get_index_of_element_from_tuple_by_type_impl<T, N, U, Args...>
        {
            static constexpr auto value = get_index_of_element_from_tuple_by_type_impl<T, N + 1, Args...>::value;
        };
    } // namespace detail

    namespace utility
    {
        template<class T, class... Args>
        T& get_element_by_type(std::tuple<Args...>& t)
        {
            return std::get<detail::get_index_of_element_from_tuple_by_type_impl<T, 0, Args...>::value>(t);
        }

        template<typename T>
        struct function_traits;

#ifndef CROW_MSVC_WORKAROUND
        template<typename T>
        struct function_traits : public function_traits<decltype(&T::operator())>
        {
            using parent_t = function_traits<decltype(&T::operator())>;
            static const size_t arity = parent_t::arity;
            using result_type = typename parent_t::result_type;
            template<size_t i>
            using arg = typename parent_t::template arg<i>;
        };
#endif

        template<typename ClassType, typename R, typename... Args>
        struct function_traits<R (ClassType::*)(Args...) const>
        {
            static const size_t arity = sizeof...(Args);

            typedef R result_type;

            template<size_t i>
            using arg = typename std::tuple_element<i, std::tuple<Args...>>::type;
        };

        template<typename ClassType, typename R, typename... Args>
        struct function_traits<R (ClassType::*)(Args...)>
        {
            static const size_t arity = sizeof...(Args);

            typedef R result_type;

            template<size_t i>
            using arg = typename std::tuple_element<i, std::tuple<Args...>>::type;
        };

        template<typename R, typename... Args>
        struct function_traits<std::function<R(Args...)>>
        {
            static const size_t arity = sizeof...(Args);

            typedef R result_type;

            template<size_t i>
            using arg = typename std::tuple_element<i, std::tuple<Args...>>::type;
        };
        /// @endcond

        inline static std::string base64encode(const unsigned char* data, size_t size, const char* key = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/")
        {
            std::string ret;
            ret.resize((size + 2) / 3 * 4);
            auto it = ret.begin();
            while (size >= 3)
            {
                *it++ = key[(static_cast<unsigned char>(*data) & 0xFC) >> 2];
                unsigned char h = (static_cast<unsigned char>(*data++) & 0x03) << 4;
                *it++ = key[h | ((static_cast<unsigned char>(*data) & 0xF0) >> 4)];
                h = (static_cast<unsigned char>(*data++) & 0x0F) << 2;
                *it++ = key[h | ((static_cast<unsigned char>(*data) & 0xC0) >> 6)];
                *it++ = key[static_cast<unsigned char>(*data++) & 0x3F];

                size -= 3;
            }
            if (size == 1)
            {
                *it++ = key[(static_cast<unsigned char>(*data) & 0xFC) >> 2];
                unsigned char h = (static_cast<unsigned char>(*data++) & 0x03) << 4;
                *it++ = key[h];
                *it++ = '=';
                *it++ = '=';
            }
            else if (size == 2)
            {
                *it++ = key[(static_cast<unsigned char>(*data) & 0xFC) >> 2];
                unsigned char h = (static_cast<unsigned char>(*data++) & 0x03) << 4;
                *it++ = key[h | ((static_cast<unsigned char>(*data) & 0xF0) >> 4)];
                h = (static_cast<unsigned char>(*data++) & 0x0F) << 2;
                *it++ = key[h];
                *it++ = '=';
            }
            return ret;
        }

        inline static std::string base64encode(std::string data, size_t size, const char* key = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/")
        {
            return base64encode((const unsigned char*)data.c_str(), size, key);
        }

        inline static std::string base64encode_urlsafe(const unsigned char* data, size_t size)
        {
            return base64encode(data, size, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_");
        }

        inline static std::string base64encode_urlsafe(std::string data, size_t size)
        {
            return base64encode((const unsigned char*)data.c_str(), size, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_");
        }

        inline static std::string base64decode(const char* data, size_t size)
        {
            // We accept both regular and url encoding here, as there does not seem to be any downside to that.
            // If we want to distinguish that we should use +/ for non-url and -_ for url.

            // Mapping logic from characters to [0-63]
            auto key = [](char c) -> unsigned char {
                if ((c >= 'A') && (c <= 'Z')) return c - 'A';
                if ((c >= 'a') && (c <= 'z')) return c - 'a' + 26;
                if ((c >= '0') && (c <= '9')) return c - '0' + 52;
                if ((c == '+') || (c == '-')) return 62;
                if ((c == '/') || (c == '_')) return 63;
                return 0;
            };

            // Not padded
            if (size % 4 == 2)             // missing last 2 characters
                size = (size / 4 * 3) + 1; // Not subtracting extra characters because they're truncated in int division
            else if (size % 4 == 3)        // missing last character
                size = (size / 4 * 3) + 2; // Not subtracting extra characters because they're truncated in int division

            // Padded
            else if (data[size - 2] == '=') // padded with '=='
                size = (size / 4 * 3) - 2;  // == padding means the last block only has 1 character instead of 3, hence the '-2'
            else if (data[size - 1] == '=') // padded with '='
                size = (size / 4 * 3) - 1;  // = padding means the last block only has 2 character instead of 3, hence the '-1'

            // Padding not needed
            else
                size = size / 4 * 3;

            std::string ret;
            ret.resize(size);
            auto it = ret.begin();

            // These will be used to decode 1 character at a time
            unsigned char odd;  // char1 and char3
            unsigned char even; // char2 and char4

            // Take 4 character blocks to turn into 3
            while (size >= 3)
            {
                // dec_char1 = (char1 shifted 2 bits to the left) OR ((char2 AND 00110000) shifted 4 bits to the right))
                odd = key(*data++);
                even = key(*data++);
                *it++ = (odd << 2) | ((even & 0x30) >> 4);
                // dec_char2 = ((char2 AND 00001111) shifted 4 bits left) OR ((char3 AND 00111100) shifted 2 bits right))
                odd = key(*data++);
                *it++ = ((even & 0x0F) << 4) | ((odd & 0x3C) >> 2);
                // dec_char3 = ((char3 AND 00000011) shifted 6 bits left) OR (char4)
                even = key(*data++);
                *it++ = ((odd & 0x03) << 6) | (even);

                size -= 3;
            }
            if (size == 2)
            {
                // d_char1 = (char1 shifted 2 bits to the left) OR ((char2 AND 00110000) shifted 4 bits to the right))
                odd = key(*data++);
                even = key(*data++);
                *it++ = (odd << 2) | ((even & 0x30) >> 4);
                // d_char2 = ((char2 AND 00001111) shifted 4 bits left) OR ((char3 AND 00111100) shifted 2 bits right))
                odd = key(*data++);
                *it++ = ((even & 0x0F) << 4) | ((odd & 0x3C) >> 2);
            }
            else if (size == 1)
            {
                // d_char1 = (char1 shifted 2 bits to the left) OR ((char2 AND 00110000) shifted 4 bits to the right))
                odd = key(*data++);
                even = key(*data++);
                *it++ = (odd << 2) | ((even & 0x30) >> 4);
            }
            return ret;
        }

        inline static std::string base64decode(const std::string& data, size_t size)
        {
            return base64decode(data.data(), size);
        }

        inline static std::string base64decode(const std::string& data)
        {
            return base64decode(data.data(), data.length());
        }


        inline static void sanitize_filename(std::string& data, char replacement = '_')
        {
            if (data.length() > 255)
                data.resize(255);

            static const auto toUpper = [](char c) {
                return ((c >= 'a') && (c <= 'z')) ? (c - ('a' - 'A')) : c;
            };
            // Check for special device names. The Windows behavior is really odd here, it will consider both AUX and AUX.txt
            // a special device. Thus we search for the string (case-insensitive), and then check if the string ends or if
            // is has a dangerous follow up character (.:\/)
            auto sanitizeSpecialFile = [](std::string& source, unsigned ofs, const char* pattern, bool includeNumber, char replacement) {
	      unsigned i = ofs;
	      size_t len = source.length();
                const char* p = pattern;
                while (*p)
                {
                    if (i >= len) return;
                    if (toUpper(source[i]) != *p) return;
                    ++i;
                    ++p;
                }
                if (includeNumber)
                {
                    if ((i >= len) || (source[i] < '1') || (source[i] > '9')) return;
                    ++i;
                }
                if ((i >= len) || (source[i] == '.') || (source[i] == ':') || (source[i] == '/') || (source[i] == '\\'))
                {
                    source.erase(ofs + 1, (i - ofs) - 1);
                    source[ofs] = replacement;
                }
            };
            bool checkForSpecialEntries = true;
            for (unsigned i = 0; i < data.length(); ++i)
            {
                // Recognize directory traversals and the special devices CON/PRN/AUX/NULL/COM[1-]/LPT[1-9]
                if (checkForSpecialEntries)
                {
                    checkForSpecialEntries = false;
                    switch (toUpper(data[i]))
                    {
                        case 'A':
                            sanitizeSpecialFile(data, i, "AUX", false, replacement);
                            break;
                        case 'C':
                            sanitizeSpecialFile(data, i, "CON", false, replacement);
                            sanitizeSpecialFile(data, i, "COM", true, replacement);
                            break;
                        case 'L':
                            sanitizeSpecialFile(data, i, "LPT", true, replacement);
                            break;
                        case 'N':
                            sanitizeSpecialFile(data, i, "NUL", false, replacement);
                            break;
                        case 'P':
                            sanitizeSpecialFile(data, i, "PRN", false, replacement);
                            break;
                        case '.':
                            sanitizeSpecialFile(data, i, "..", false, replacement);
                            break;
                    }
                }

                // Sanitize individual characters
                unsigned char c = data[i];
                if ((c < ' ') || ((c >= 0x80) && (c <= 0x9F)) || (c == '?') || (c == '<') || (c == '>') || (c == ':') || (c == '*') || (c == '|') || (c == '\"'))
                {
                    data[i] = replacement;
                }
                else if ((c == '/') || (c == '\\'))
                {
                    if (CROW_UNLIKELY(i == 0)) //Prevent Unix Absolute Paths (Windows Absolute Paths are prevented with `(c == ':')`)
                    {
                        data[i] = replacement;
                    }
                    else
                    {
                        checkForSpecialEntries = true;
                    }
                }
            }
        }

        inline static std::string random_alphanum(std::size_t size)
        {
            static const char alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
            std::random_device dev;
            std::mt19937 rng(dev());
            std::uniform_int_distribution<std::mt19937::result_type> dist(0, sizeof(alphabet) - 2);
            std::string out;
            out.reserve(size);
            for (std::size_t i = 0; i < size; i++)
                out.push_back(alphabet[dist(rng)]);
            return out;
        }

        inline static std::string join_path(std::string path, const std::string& fname)
        {
#if defined(CROW_CAN_USE_CPP17) && !defined(CROW_FILESYSTEM_IS_EXPERIMENTAL)
            return (std::filesystem::path(path) / fname).string();
#else
            if (!(path.back() == '/' || path.back() == '\\'))
                path += '/';
            path += fname;
            return path;
#endif
        }

        /**
         * @brief Checks two string for equality.
         * Always returns false if strings differ in size.
         * Defaults to case-insensitive comparison.
         */
        inline static bool string_equals(const std::string& l, const std::string& r, bool case_sensitive = false)
        {
            if (l.length() != r.length())
                return false;

            for (size_t i = 0; i < l.length(); i++)
            {
                if (case_sensitive)
                {
                    if (l[i] != r[i])
                        return false;
                }
                else
                {
                    if (std::toupper(l[i]) != std::toupper(r[i]))
                        return false;
                }
            }

            return true;
        }

        template<typename T, typename U>
        inline static T lexical_cast(const U& v)
        {
            std::stringstream stream;
            T res;

            stream << v;
            stream >> res;

            return res;
        }

        template<typename T>
        inline static T lexical_cast(const char* v, size_t count)
        {
            std::stringstream stream;
            T res;

            stream.write(v, count);
            stream >> res;

            return res;
        }


        /// Return a copy of the given string with its
        /// leading and trailing whitespaces removed.
        inline static std::string trim(const std::string& v)
        {
            if (v.empty())
                return "";

            size_t begin = 0, end = v.length();

            size_t i;
            for (i = 0; i < v.length(); i++)
            {
                if (!std::isspace(v[i]))
                {
                    begin = i;
                    break;
                }
            }

            if (i == v.length())
                return "";

            for (i = v.length(); i > 0; i--)
            {
                if (!std::isspace(v[i - 1]))
                {
                    end = i;
                    break;
                }
            }

            return v.substr(begin, end - begin);
        }
    } // namespace utility
} // namespace crow
