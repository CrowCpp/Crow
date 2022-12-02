/* merged revision: 5b951d74bd66ec9d38448e0a85b1cf8b85d97db3 */
/* updated to     : e13b274770da9b82a1085dec29182acfea72e7a7 (beyond v2.9.5) */
/* commits not included:
 * 091ebb87783a58b249062540bbea07de2a11e9cf
 * 6132d1fefa03f769a3979355d1f5da0b8889cad2
 * 7ba312397c2a6c851a4b5efe6c1603b1e1bda6ff
 * d7675453a6c03180572f084e95eea0d02df39164
 * dff604db203986e532e5a679bafd0e7382c6bdd9 (Might be useful to actually add [upgrade requests with a body])
 * e01811e7f4894d7f0f7f4bd8492cccec6f6b4038 (related to above)
 * 05525c5fde1fc562481f6ae08fa7056185325daf (also related to above)
 * 350258965909f249f9c59823aac240313e0d0120 (cannot be implemented due to upgrade)
 */

// clang-format off
#pragma once
extern "C" {
#include <stddef.h>
#if defined(_WIN32) && !defined(__MINGW32__) && \
  (!defined(_MSC_VER) || _MSC_VER<1600) && !defined(__WINE__)
#include <BaseTsd.h>
typedef __int8 int8_t;
typedef unsigned __int8 uint8_t;
typedef __int16 int16_t;
typedef unsigned __int16 uint16_t;
typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;
#elif (defined(__sun) || defined(__sun__)) && defined(__SunOS_5_9)
#include <sys/inttypes.h>
#else
#include <stdint.h>
#endif
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>
}

#include "crow/common.h"
namespace crow
{
/* Maximium header size allowed. If the macro is not defined
 * before including this header then the default is used. To
 * change the maximum header size, define the macro in the build
 * environment (e.g. -DHTTP_MAX_HEADER_SIZE=<value>). To remove
 * the effective limit on the size of the header, define the macro
 * to a very large number (e.g. -DCROW_HTTP_MAX_HEADER_SIZE=0x7fffffff)
 */
#ifndef CROW_HTTP_MAX_HEADER_SIZE
# define CROW_HTTP_MAX_HEADER_SIZE (80*1024)
#endif

typedef struct http_parser http_parser;
typedef struct http_parser_settings http_parser_settings;

/* Callbacks should return non-zero to indicate an error. The parser will
 * then halt execution.
 *
 * The one exception is on_headers_complete. In a HTTP_RESPONSE parser
 * returning '1' from on_headers_complete will tell the parser that it
 * should not expect a body. This is used when receiving a response to a
 * HEAD request which may contain 'Content-Length' or 'Transfer-Encoding:
 * chunked' headers that indicate the presence of a body.
 *
 * Returning `2` from on_headers_complete will tell parser that it should not
 * expect neither a body nor any futher responses on this connection. This is
 * useful for handling responses to a CONNECT request which may not contain
 * `Upgrade` or `Connection: upgrade` headers.
 *
 * http_data_cb does not return data chunks. It will be called arbitrarally
 * many times for each string. E.G. you might get 10 callbacks for "on_url"
 * each providing just a few characters more data.
 */
typedef int (*http_data_cb) (http_parser*, const char *at, size_t length);
typedef int (*http_cb) (http_parser*);


/* Flag values for http_parser.flags field */
enum http_connection_flags // This is basically 7 booleans placed into 1 integer. Uses 4 bytes instead of n bytes (7 currently).
  { F_CHUNKED               = 1 << 0 // 00000000 00000000 00000000 00000001
  , F_CONNECTION_KEEP_ALIVE = 1 << 1 // 00000000 00000000 00000000 00000010
  , F_CONNECTION_CLOSE      = 1 << 2 // 00000000 00000000 00000000 00000100
  , F_TRAILING              = 1 << 3 // 00000000 00000000 00000000 00001000
  , F_UPGRADE               = 1 << 4 // 00000000 00000000 00000000 00010000
  , F_SKIPBODY              = 1 << 5 // 00000000 00000000 00000000 00100000
  , F_CONTENTLENGTH         = 1 << 6 // 00000000 00000000 00000000 01000000
  };


/* Map for errno-related constants
 *
 * The provided argument should be a macro that takes 2 arguments.
 */
#define CROW_HTTP_ERRNO_MAP(CROW_XX)                                                    \
  /* No error */                                                                        \
  CROW_XX(OK, "success")                                                                \
                                                                                        \
  /* Callback-related errors */                                                         \
  CROW_XX(CB_message_begin, "the on_message_begin callback failed")                     \
  CROW_XX(CB_method, "the on_method callback failed")                                   \
  CROW_XX(CB_url, "the \"on_url\" callback failed")                                     \
  CROW_XX(CB_header_field, "the \"on_header_field\" callback failed")                   \
  CROW_XX(CB_header_value, "the \"on_header_value\" callback failed")                   \
  CROW_XX(CB_headers_complete, "the \"on_headers_complete\" callback failed")           \
  CROW_XX(CB_body, "the \"on_body\" callback failed")                                   \
  CROW_XX(CB_message_complete, "the \"on_message_complete\" callback failed")           \
  CROW_XX(CB_status, "the \"on_status\" callback failed")                               \
                                                                                        \
  /* Parsing-related errors */                                                          \
  CROW_XX(INVALID_EOF_STATE, "stream ended at an unexpected time")                      \
  CROW_XX(HEADER_OVERFLOW, "too many header bytes seen; overflow detected")             \
  CROW_XX(CLOSED_CONNECTION, "data received after completed connection: close message") \
  CROW_XX(INVALID_VERSION, "invalid HTTP version")                                      \
  CROW_XX(INVALID_STATUS, "invalid HTTP status code")                                   \
  CROW_XX(INVALID_METHOD, "invalid HTTP method")                                        \
  CROW_XX(INVALID_URL, "invalid URL")                                                   \
  CROW_XX(INVALID_HOST, "invalid host")                                                 \
  CROW_XX(INVALID_PORT, "invalid port")                                                 \
  CROW_XX(INVALID_PATH, "invalid path")                                                 \
  CROW_XX(INVALID_QUERY_STRING, "invalid query string")                                 \
  CROW_XX(INVALID_FRAGMENT, "invalid fragment")                                         \
  CROW_XX(LF_EXPECTED, "LF character expected")                                         \
  CROW_XX(INVALID_HEADER_TOKEN, "invalid character in header")                          \
  CROW_XX(INVALID_CONTENT_LENGTH, "invalid character in content-length header")         \
  CROW_XX(UNEXPECTED_CONTENT_LENGTH, "unexpected content-length header")                \
  CROW_XX(INVALID_CHUNK_SIZE, "invalid character in chunk size header")                 \
  CROW_XX(INVALID_CONSTANT, "invalid constant string")                                  \
  CROW_XX(INVALID_INTERNAL_STATE, "encountered unexpected internal state")              \
  CROW_XX(STRICT, "strict mode assertion failed")                                       \
  CROW_XX(UNKNOWN, "an unknown error occurred")                                         \
  CROW_XX(INVALID_TRANSFER_ENCODING, "request has invalid transfer-encoding")           \


/* Define CHPE_* values for each errno value above */
#define CROW_HTTP_ERRNO_GEN(n, s) CHPE_##n,
enum http_errno {
  CROW_HTTP_ERRNO_MAP(CROW_HTTP_ERRNO_GEN)
};
#undef CROW_HTTP_ERRNO_GEN


/* Get an http_errno value from an http_parser */
#define CROW_HTTP_PARSER_ERRNO(p) ((enum http_errno)(p)->http_errno)


    struct http_parser
    {
        /** PRIVATE **/
        unsigned int flags : 7;                  /* F_* values from 'flags' enum; semi-public */
        unsigned int state : 8;                  /* enum state from http_parser.c */
        unsigned int header_state : 7;           /* enum header_state from http_parser.c */
        unsigned int index : 5;                  /* index into current matcher */
        unsigned int uses_transfer_encoding : 1; /* Transfer-Encoding header is present */
        unsigned int allow_chunked_length : 1;   /* Allow headers with both `Content-Length` and `Transfer-Encoding: chunked` set */
        unsigned int lenient_http_headers : 1;

        uint32_t nread;          /* # bytes read in various scenarios */
        uint64_t content_length; /* # bytes in body. `(uint64_t) -1` (all bits one) if no Content-Length header. */
        unsigned long qs_point;

        /** READ-ONLY **/
        unsigned char http_major;
        unsigned char http_minor;
        unsigned int method : 8;       /* requests only */
        unsigned int http_errno : 7;

  /* 1 = Upgrade header was present and the parser has exited because of that.
   * 0 = No upgrade header present.
   * Should be checked when http_parser_execute() returns in addition to
   * error checking.
   */
        unsigned int upgrade : 1;

        /** PUBLIC **/
        void* data; /* A pointer to get hook to the "connection" or "socket" object */
    };


    struct http_parser_settings
    {
        http_cb on_message_begin;
        http_cb on_method;
        http_data_cb on_url;
        http_data_cb on_header_field;
        http_data_cb on_header_value;
        http_cb on_headers_complete;
        http_data_cb on_body;
        http_cb on_message_complete;
    };



// SOURCE (.c) CODE
static uint32_t max_header_size = CROW_HTTP_MAX_HEADER_SIZE;

#ifndef CROW_ULLONG_MAX
# define CROW_ULLONG_MAX ((uint64_t) -1) /* 2^64-1 */
#endif

#ifndef CROW_MIN
# define CROW_MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef CROW_ARRAY_SIZE
# define CROW_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef CROW_BIT_AT
# define CROW_BIT_AT(a, i)                                           \
  (!!((unsigned int) (a)[(unsigned int) (i) >> 3] &                  \
   (1 << ((unsigned int) (i) & 7))))
#endif

#define CROW_SET_ERRNO(e)                                            \
do {                                                                 \
  parser->nread = nread;                                             \
  parser->http_errno = (e);                                          \
} while(0)

/* Run the notify callback FOR, returning ER if it fails */
#define CROW_CALLBACK_NOTIFY_(FOR, ER)                               \
do {                                                                 \
  assert(CROW_HTTP_PARSER_ERRNO(parser) == CHPE_OK);                 \
                                                                     \
  if (CROW_LIKELY(settings->on_##FOR)) {                             \
    if (CROW_UNLIKELY(0 != settings->on_##FOR(parser))) {            \
      CROW_SET_ERRNO(CHPE_CB_##FOR);                                 \
    }                                                                \
                                                                     \
    /* We either errored above or got paused; get out */             \
    if (CROW_UNLIKELY(CROW_HTTP_PARSER_ERRNO(parser) != CHPE_OK)) {  \
      return (ER);                                                   \
    }                                                                \
  }                                                                  \
} while (0)

/* Run the notify callback FOR and consume the current byte */
#define CROW_CALLBACK_NOTIFY(FOR)            CROW_CALLBACK_NOTIFY_(FOR, p - data + 1)

/* Run the notify callback FOR and don't consume the current byte */
#define CROW_CALLBACK_NOTIFY_NOADVANCE(FOR)  CROW_CALLBACK_NOTIFY_(FOR, p - data)

/* Run data callback FOR with LEN bytes, returning ER if it fails */
#define CROW_CALLBACK_DATA_(FOR, LEN, ER)                            \
do {                                                                 \
  assert(CROW_HTTP_PARSER_ERRNO(parser) == CHPE_OK);                 \
                                                                     \
  if (FOR##_mark) {                                                  \
    if (CROW_LIKELY(settings->on_##FOR)) {                           \
      if (CROW_UNLIKELY(0 !=                                         \
          settings->on_##FOR(parser, FOR##_mark, (LEN)))) {          \
        CROW_SET_ERRNO(CHPE_CB_##FOR);                               \
      }                                                              \
                                                                     \
      /* We either errored above or got paused; get out */           \
      if (CROW_UNLIKELY(CROW_HTTP_PARSER_ERRNO(parser) != CHPE_OK)) {\
        return (ER);                                                 \
      }                                                              \
    }                                                                \
    FOR##_mark = NULL;                                               \
  }                                                                  \
} while (0)

/* Run the data callback FOR and consume the current byte */
#define CROW_CALLBACK_DATA(FOR)                                      \
    CROW_CALLBACK_DATA_(FOR, p - FOR##_mark, p - data + 1)

/* Run the data callback FOR and don't consume the current byte */
#define CROW_CALLBACK_DATA_NOADVANCE(FOR)                            \
    CROW_CALLBACK_DATA_(FOR, p - FOR##_mark, p - data)

/* Set the mark FOR; non-destructive if mark is already set */
#define CROW_MARK(FOR)                                               \
do {                                                                 \
  if (!FOR##_mark) {                                                 \
    FOR##_mark = p;                                                  \
  }                                                                  \
} while (0)

/* Don't allow the total size of the HTTP headers (including the status
 * line) to exceed max_header_size.  This check is here to protect
 * embedders against denial-of-service attacks where the attacker feeds
 * us a never-ending header that the embedder keeps buffering.
 *
 * This check is arguably the responsibility of embedders but we're doing
 * it on the embedder's behalf because most won't bother and this way we
 * make the web a little safer.  max_header_size is still far bigger
 * than any reasonable request or response so this should never affect
 * day-to-day operation.
 */
#define CROW_COUNT_HEADER_SIZE(V)                                    \
do {                                                                 \
  nread += (uint32_t)(V);                                            \
  if (CROW_UNLIKELY(nread > max_header_size)) {                      \
    CROW_SET_ERRNO(CHPE_HEADER_OVERFLOW);                            \
    goto error;                                                      \
  }                                                                  \
} while (0)
#define CROW_REEXECUTE()                                             \
  goto reexecute;                                                    \

#define CROW_PROXY_CONNECTION "proxy-connection"
#define CROW_CONNECTION "connection"
#define CROW_CONTENT_LENGTH "content-length"
#define CROW_TRANSFER_ENCODING "transfer-encoding"
#define CROW_UPGRADE "upgrade"
#define CROW_CHUNKED "chunked"
#define CROW_KEEP_ALIVE "keep-alive"
#define CROW_CLOSE "close"



    enum state
    {
        s_dead = 1 /* important that this is > 0 */

        ,
        s_start_req

        ,
        s_req_method,
        s_req_spaces_before_url,
        s_req_schema,
        s_req_schema_slash,
        s_req_schema_slash_slash,
        s_req_server_start,
        s_req_server,             // }
        s_req_server_with_at,     // |
        s_req_path,               // | The parser recognizes how to switch between these states,
        s_req_query_string_start, // | however it doesn't process them any differently.
        s_req_query_string,       // }
        s_req_http_start,
        s_req_http_H,
        s_req_http_HT,
        s_req_http_HTT,
        s_req_http_HTTP,
        s_req_http_I,
        s_req_http_IC,
        s_req_http_major,
        s_req_http_dot,
        s_req_http_minor,
        s_req_http_end,
        s_req_line_almost_done

        ,
        s_header_field_start,
        s_header_field,
        s_header_value_discard_ws,
        s_header_value_discard_ws_almost_done,
        s_header_value_discard_lws,
        s_header_value_start,
        s_header_value,
        s_header_value_lws

        ,
        s_header_almost_done

        ,
        s_chunk_size_start,
        s_chunk_size,
        s_chunk_parameters,
        s_chunk_size_almost_done

        ,
        s_headers_almost_done,
        s_headers_done

        /* Important: 's_headers_done' must be the last 'header' state. All
         * states beyond this must be 'body' states. It is used for overflow
         * checking. See the CROW_PARSING_HEADER() macro.
         */

        ,
        s_chunk_data,
        s_chunk_data_almost_done,
        s_chunk_data_done

        ,
        s_body_identity,
        s_body_identity_eof

        ,
        s_message_done
    };


#define CROW_PARSING_HEADER(state) (state <= s_headers_done)


enum header_states
  { h_general = 0
  , h_C
  , h_CO
  , h_CON

  , h_matching_connection
  , h_matching_proxy_connection
  , h_matching_content_length
  , h_matching_transfer_encoding
  , h_matching_upgrade

  , h_connection
  , h_content_length
  , h_content_length_num
  , h_content_length_ws
  , h_transfer_encoding
  , h_upgrade

  , h_matching_transfer_encoding_token_start
  , h_matching_transfer_encoding_chunked
  , h_matching_transfer_encoding_token

  , h_matching_connection_keep_alive
  , h_matching_connection_close

  , h_transfer_encoding_chunked
  , h_connection_keep_alive
  , h_connection_close
  };

enum http_host_state
  {
    s_http_host_dead = 1
  , s_http_userinfo_start
  , s_http_userinfo
  , s_http_host_start
  , s_http_host_v6_start
  , s_http_host
  , s_http_host_v6
  , s_http_host_v6_end
  , s_http_host_v6_zone_start
  , s_http_host_v6_zone
  , s_http_host_port_start
  , s_http_host_port
};

/* Macros for character classes; depends on strict-mode  */
#define CROW_LOWER(c)            (unsigned char)(c | 0x20)
#define CROW_IS_ALPHA(c)         (CROW_LOWER(c) >= 'a' && CROW_LOWER(c) <= 'z')
#define CROW_IS_NUM(c)           ((c) >= '0' && (c) <= '9')
#define CROW_IS_ALPHANUM(c)      (CROW_IS_ALPHA(c) || CROW_IS_NUM(c))
//#define CROW_IS_HEX(c)           (CROW_IS_NUM(c) || (CROW_LOWER(c) >= 'a' && CROW_LOWER(c) <= 'f'))
#define CROW_IS_MARK(c)          ((c) == '-' || (c) == '_' || (c) == '.' || \
  (c) == '!' || (c) == '~' || (c) == '*' || (c) == '\'' || (c) == '(' ||    \
  (c) == ')')
#define CROW_IS_USERINFO_CHAR(c) (CROW_IS_ALPHANUM(c) || CROW_IS_MARK(c) || (c) == '%' || \
  (c) == ';' || (c) == ':' || (c) == '&' || (c) == '=' || (c) == '+' ||                   \
  (c) == '$' || (c) == ',')

#define CROW_TOKEN(c)            (tokens[(unsigned char)c])
#define CROW_IS_URL_CHAR(c)      (CROW_BIT_AT(normal_url_char, (unsigned char)c))
//#define CROW_IS_HOST_CHAR(c)     (CROW_IS_ALPHANUM(c) || (c) == '.' || (c) == '-')

  /**
 * Verify that a char is a valid visible (printable) US-ASCII
 * character or %x80-FF
 **/
#define CROW_IS_HEADER_CHAR(ch)                                                     \
  (ch == cr || ch == lf || ch == 9 || ((unsigned char)ch > 31 && ch != 127))

#define CROW_start_state s_start_req

# define CROW_STRICT_CHECK(cond)                                     \
do {                                                                 \
  if (cond) {                                                        \
    CROW_SET_ERRNO(CHPE_STRICT);                                     \
    goto error;                                                      \
  }                                                                  \
} while (0)
#define CROW_NEW_MESSAGE() (CROW_start_state)

/* Our URL parser.
 *
 * This is designed to be shared by http_parser_execute() for URL validation,
 * hence it has a state transition + byte-for-byte interface. In addition, it
 * is meant to be embedded in http_parser_parse_url(), which does the dirty
 * work of turning state transitions URL components for its API.
 *
 * This function should only be invoked with non-space characters. It is
 * assumed that the caller cares about (and can detect) the transition between
 * URL and non-URL states by looking for these.
 */
inline enum state
parse_url_char(enum state s, const char ch, http_parser *parser, const char* url_mark, const char* p)
{
# define CROW_T(v) 0


static const uint8_t normal_url_char[32] = {
/*   0 nul    1 soh    2 stx    3 etx    4 eot    5 enq    6 ack    7 bel  */
        0    |   0    |   0    |   0    |   0    |   0    |   0    |   0,
/*   8 bs     9 ht    10 nl    11 vt    12 np    13 cr    14 so    15 si   */
        0    |CROW_T(2)|  0    |   0    |CROW_T(16)| 0    |   0    |   0,
/*  16 dle   17 dc1   18 dc2   19 dc3   20 dc4   21 nak   22 syn   23 etb */
        0    |   0    |   0    |   0    |   0    |   0    |   0    |   0,
/*  24 can   25 em    26 sub   27 esc   28 fs    29 gs    30 rs    31 us  */
        0    |   0    |   0    |   0    |   0    |   0    |   0    |   0,
/*  32 sp    33  !    34  "    35  #    36  $    37  %    38  &    39  '  */
        0    |   2    |   4    |   0    |   16   |   32   |   64   |  128,
/*  40  (    41  )    42  *    43  +    44  ,    45  -    46  .    47  /  */
        1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
/*  48  0    49  1    50  2    51  3    52  4    53  5    54  6    55  7  */
        1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
/*  56  8    57  9    58  :    59  ;    60  <    61  =    62  >    63  ?  */
        1    |   2    |   4    |   8    |   16   |   32   |   64   |   0,
/*  64  @    65  A    66  B    67  C    68  D    69  E    70  F    71  G  */
        1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
/*  72  H    73  I    74  J    75  K    76  L    77  M    78  N    79  O  */
        1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
/*  80  P    81  Q    82  R    83  S    84  CROW_T    85  U    86  V    87  W  */
        1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
/*  88  X    89  Y    90  Z    91  [    92  \    93  ]    94  ^    95  _  */
        1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
/*  96  `    97  a    98  b    99  c   100  d   101  e   102  f   103  g  */
        1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
/* 104  h   105  i   106  j   107  k   108  l   109  m   110  n   111  o  */
        1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
/* 112  p   113  q   114  r   115  s   116  t   117  u   118  v   119  w  */
        1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
/* 120  x   121  y   122  z   123  {   124  |   125  }   126  ~   127 del */
        1    |   2    |   4    |   8    |   16   |   32   |   64   |   0, };

#undef CROW_T

  if (ch == ' ' || ch == '\r' || ch == '\n') {
    return s_dead;
  }
  if (ch == '\t' || ch == '\f') {
    return s_dead;
  }

  switch (s) {
    case s_req_spaces_before_url:
      /* Proxied requests are followed by scheme of an absolute URI (alpha).
       * All methods except CONNECT are followed by '/' or '*'.
       */

      if (ch == '/' || ch == '*') {
        return s_req_path;
      }

      if (CROW_IS_ALPHA(ch)) {
        return s_req_schema;
      }

      break;

    case s_req_schema:
      if (CROW_IS_ALPHA(ch)) {
        return s;
      }

      if (ch == ':') {
        return s_req_schema_slash;
      }

      break;

    case s_req_schema_slash:
      if (ch == '/') {
        return s_req_schema_slash_slash;
      }

      break;

    case s_req_schema_slash_slash:
      if (ch == '/') {
        return s_req_server_start;
      }

      break;

    case s_req_server_with_at:
      if (ch == '@') {
        return s_dead;
      }

    /* fall through */
    case s_req_server_start:
    case s_req_server:
      if (ch == '/') {
        return s_req_path;
      }

      if (ch == '?') {
          parser->qs_point = p - url_mark;
        return s_req_query_string_start;
      }

      if (ch == '@') {
        return s_req_server_with_at;
      }

      if (CROW_IS_USERINFO_CHAR(ch) || ch == '[' || ch == ']') {
        return s_req_server;
      }

      break;

    case s_req_path:
      if (CROW_IS_URL_CHAR(ch)) {
        return s;
      }
      else if (ch == '?')
      {
          parser->qs_point = p - url_mark;
          return s_req_query_string_start;
      }

      break;

    case s_req_query_string_start:
    case s_req_query_string:
      if (CROW_IS_URL_CHAR(ch)) {
        return s_req_query_string;
      }
      else if (ch == '?')
      {
          return s_req_query_string;
      }

      break;

    default:
      break;
  }

  /* We should never fall out of the switch above unless there's an error */
  return s_dead;
}

inline size_t http_parser_execute (http_parser *parser,
                            const http_parser_settings *settings,
                            const char *data,
                            size_t len)
{

/* Tokens as defined by rfc 2616. Also lowercases them.
 *        token       = 1*<any CHAR except CTLs or separators>
 *     separators     = "(" | ")" | "<" | ">" | "@"
 *                    | "," | ";" | ":" | "\" | <">
 *                    | "/" | "[" | "]" | "?" | "="
 *                    | "{" | "}" | SP  | HT
 */
static const char tokens[256] = {
/*   0 nul    1 soh    2 stx    3 etx    4 eot    5 enq    6 ack    7 bel  */
        0,       0,       0,       0,       0,       0,       0,       0,
/*   8 bs     9 ht    10 nl    11 vt    12 np    13 cr    14 so    15 si   */
        0,       0,       0,       0,       0,       0,       0,       0,
/*  16 dle   17 dc1   18 dc2   19 dc3   20 dc4   21 nak   22 syn   23 etb */
        0,       0,       0,       0,       0,       0,       0,       0,
/*  24 can   25 em    26 sub   27 esc   28 fs    29 gs    30 rs    31 us  */
        0,       0,       0,       0,       0,       0,       0,       0,
/*  32 sp    33  !    34  "    35  #    36  $    37  %    38  &    39  '  */
        0,      '!',      0,      '#',     '$',     '%',     '&',    '\'',
/*  40  (    41  )    42  *    43  +    44  ,    45  -    46  .    47  /  */
        0,       0,      '*',     '+',      0,      '-',     '.',      0,
/*  48  0    49  1    50  2    51  3    52  4    53  5    54  6    55  7  */
       '0',     '1',     '2',     '3',     '4',     '5',     '6',     '7',
/*  56  8    57  9    58  :    59  ;    60  <    61  =    62  >    63  ?  */
       '8',     '9',      0,       0,       0,       0,       0,       0,
/*  64  @    65  A    66  B    67  C    68  D    69  E    70  F    71  G  */
        0,      'a',     'b',     'c',     'd',     'e',     'f',     'g',
/*  72  H    73  I    74  J    75  K    76  L    77  M    78  N    79  O  */
       'h',     'i',     'j',     'k',     'l',     'm',     'n',     'o',
/*  80  P    81  Q    82  R    83  S    84  T    85  U    86  V    87  W  */
       'p',     'q',     'r',     's',     't',     'u',     'v',     'w',
/*  88  X    89  Y    90  Z    91  [    92  \    93  ]    94  ^    95  _  */
       'x',     'y',     'z',      0,       0,       0,      '^',     '_',
/*  96  `    97  a    98  b    99  c   100  d   101  e   102  f   103  g  */
       '`',     'a',     'b',     'c',     'd',     'e',     'f',     'g',
/* 104  h   105  i   106  j   107  k   108  l   109  m   110  n   111  o  */
       'h',     'i',     'j',     'k',     'l',     'm',     'n',     'o',
/* 112  p   113  q   114  r   115  s   116  t   117  u   118  v   119  w  */
       'p',     'q',     'r',     's',     't',     'u',     'v',     'w',
/* 120  x   121  y   122  z   123  {   124  |   125  }   126  ~   127 del */
       'x',     'y',     'z',      0,      '|',      0,      '~',       0 };


static const int8_t unhex[256] =
  {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
  ,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
  ,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
  , 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,-1,-1,-1,-1,-1,-1
  ,-1,10,11,12,13,14,15,-1,-1,-1,-1,-1,-1,-1,-1,-1
  ,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
  ,-1,10,11,12,13,14,15,-1,-1,-1,-1,-1,-1,-1,-1,-1
  ,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
  };



  char c, ch;
  int8_t unhex_val;
  const char *p = data;
  const char *header_field_mark = 0;
  const char *header_value_mark = 0;
  const char *url_mark = 0;
  const char *url_start_mark = 0;
  const char *body_mark = 0;
  const unsigned int lenient = parser->lenient_http_headers;
  const unsigned int allow_chunked_length = parser->allow_chunked_length;
  
  uint32_t nread = parser->nread;

  /* We're in an error state. Don't bother doing anything. */
  if (CROW_HTTP_PARSER_ERRNO(parser) != CHPE_OK) {
    return 0;
  }

  if (len == 0) {
    switch (parser->state) {
      case s_body_identity_eof:
        /* Use of CROW_CALLBACK_NOTIFY() here would erroneously return 1 byte read if we got paused. */
        CROW_CALLBACK_NOTIFY_NOADVANCE(message_complete);
        return 0;

      case s_dead:
      case s_start_req:
        return 0;

      default:
        CROW_SET_ERRNO(CHPE_INVALID_EOF_STATE);
        return 1;
    }
  }


  if (parser->state == s_header_field)
    header_field_mark = data;
  if (parser->state == s_header_value)
    header_value_mark = data;
  switch (parser->state) {
  case s_req_path:
  case s_req_schema:
  case s_req_schema_slash:
  case s_req_schema_slash_slash:
  case s_req_server_start:
  case s_req_server:
  case s_req_server_with_at:
  case s_req_query_string_start:
  case s_req_query_string:
    url_mark = data;
    break;
  default:
    break;
  }

  for (p=data; p != data + len; p++) {
    ch = *p;

    if (CROW_PARSING_HEADER(parser->state))
      CROW_COUNT_HEADER_SIZE(1);

reexecute:
    switch (parser->state) {

      case s_dead:
        /* this state is used after a 'Connection: close' message
         * the parser will error out if it reads another message
         */
        if (CROW_LIKELY(ch == cr || ch == lf))
          break;

        CROW_SET_ERRNO(CHPE_CLOSED_CONNECTION);
        goto error;

      case s_start_req:
      {
        if (ch == cr || ch == lf)
          break;
        parser->flags = 0;
        parser->uses_transfer_encoding = 0;
        parser->content_length = CROW_ULLONG_MAX;

        if (CROW_UNLIKELY(!CROW_IS_ALPHA(ch))) {
          CROW_SET_ERRNO(CHPE_INVALID_METHOD);
          goto error;
        }

        parser->method = 0;
        parser->index = 1;
        switch (ch) {
          case 'A': parser->method = (unsigned)HTTPMethod::Acl;                                                              break;
          case 'B': parser->method = (unsigned)HTTPMethod::Bind;                                                             break;
          case 'C': parser->method = (unsigned)HTTPMethod::Connect;   /* or COPY, CHECKOUT */                                break;
          case 'D': parser->method = (unsigned)HTTPMethod::Delete;                                                           break;
          case 'G': parser->method = (unsigned)HTTPMethod::Get;                                                              break;
          case 'H': parser->method = (unsigned)HTTPMethod::Head;                                                             break;
          case 'L': parser->method = (unsigned)HTTPMethod::Lock;      /* or LINK */                                          break;
          case 'M': parser->method = (unsigned)HTTPMethod::MkCol;     /* or MOVE, MKACTIVITY, MERGE, M-SEARCH, MKCALENDAR */ break;
          case 'N': parser->method = (unsigned)HTTPMethod::Notify;                                                           break;
          case 'O': parser->method = (unsigned)HTTPMethod::Options;                                                          break;
          case 'P': parser->method = (unsigned)HTTPMethod::Post;      /* or PROPFIND|PROPPATCH|PUT|PATCH|PURGE */            break;
          case 'R': parser->method = (unsigned)HTTPMethod::Report;    /* or REBIND */                                        break;
          case 'S': parser->method = (unsigned)HTTPMethod::Subscribe; /* or SEARCH, SOURCE */                                break;
          case 'T': parser->method = (unsigned)HTTPMethod::Trace;                                                            break;
          case 'U': parser->method = (unsigned)HTTPMethod::Unlock;    /* or UNSUBSCRIBE, UNBIND, UNLINK */                   break;
          default:
            CROW_SET_ERRNO(CHPE_INVALID_METHOD);
            goto error;
        }
        parser->state = s_req_method;

        CROW_CALLBACK_NOTIFY(message_begin);

        break;
      }

      case s_req_method:
      {
        const char *matcher;
        if (CROW_UNLIKELY(ch == '\0')) {
          CROW_SET_ERRNO(CHPE_INVALID_METHOD);
          goto error;
        }

        matcher = method_strings[parser->method];
        if (ch == ' ' && matcher[parser->index] == '\0') {
          parser->state = s_req_spaces_before_url;
        } else if (ch == matcher[parser->index]) {
          ; /* nada */
        } else if ((ch >= 'A' && ch <= 'Z') || ch == '-') {

          switch (parser->method << 16 | parser->index << 8 | ch) {
#define CROW_XX(meth, pos, ch, new_meth) \
            case ((unsigned)HTTPMethod::meth << 16 | pos << 8 | ch): \
              parser->method = (unsigned)HTTPMethod::new_meth; break;

            CROW_XX(Post,      1, 'U', Put)
            CROW_XX(Post,      1, 'A', Patch)
            CROW_XX(Post,      1, 'R', Propfind)
            CROW_XX(Put,       2, 'R', Purge)
            CROW_XX(Connect,   1, 'H', Checkout)
            CROW_XX(Connect,   2, 'P', Copy)
            CROW_XX(MkCol,     1, 'O', Move)
            CROW_XX(MkCol,     1, 'E', Merge)
            CROW_XX(MkCol,     1, '-', MSearch)
            CROW_XX(MkCol,     2, 'A', MkActivity)
            CROW_XX(MkCol,     3, 'A', MkCalendar)
            CROW_XX(Subscribe, 1, 'E', Search)
            CROW_XX(Subscribe, 1, 'O', Source)
            CROW_XX(Report,    2, 'B', Rebind)
            CROW_XX(Propfind,  4, 'P', Proppatch)
            CROW_XX(Lock,      1, 'I', Link)
            CROW_XX(Unlock,    2, 'S', Unsubscribe)
            CROW_XX(Unlock,    2, 'B', Unbind)
            CROW_XX(Unlock,    3, 'I', Unlink)
#undef CROW_XX
            default:
              CROW_SET_ERRNO(CHPE_INVALID_METHOD);
              goto error;
          }
        } else {
          CROW_SET_ERRNO(CHPE_INVALID_METHOD);
          goto error;
        }

        CROW_CALLBACK_NOTIFY_NOADVANCE(method);

        ++parser->index;
        break;
      }

      case s_req_spaces_before_url:
      {
        if (ch == ' ') break;

        CROW_MARK(url);
        CROW_MARK(url_start);
        if (parser->method == (unsigned)HTTPMethod::Connect) {
          parser->state = s_req_server_start;
        }

        parser->state = parse_url_char(static_cast<state>(parser->state), ch, parser, url_start_mark, p);
        if (CROW_UNLIKELY(parser->state == s_dead)) {
          CROW_SET_ERRNO(CHPE_INVALID_URL);
          goto error;
        }

        break;
      }

      case s_req_schema:
      case s_req_schema_slash:
      case s_req_schema_slash_slash:
      case s_req_server_start:
      {
        switch (ch) {
          /* No whitespace allowed here */
          case ' ':
          case cr:
          case lf:
            CROW_SET_ERRNO(CHPE_INVALID_URL);
            goto error;
          default:
            parser->state = parse_url_char(static_cast<state>(parser->state), ch, parser, url_start_mark, p);
            if (CROW_UNLIKELY(parser->state == s_dead)) {
              CROW_SET_ERRNO(CHPE_INVALID_URL);
              goto error;
            }
        }

        break;
      }

      case s_req_server:
      case s_req_server_with_at:
      case s_req_path:
      case s_req_query_string_start:
      case s_req_query_string:
      {
        switch (ch) {
          case ' ':
            parser->state = s_req_http_start;
            CROW_CALLBACK_DATA(url);
            break;
          case cr: // No space after URL means no HTTP version. Which means the request is using HTTP/0.9
          case lf:
            if (CROW_UNLIKELY(parser->method != (unsigned)HTTPMethod::Get)) // HTTP/0.9 doesn't define any method other than GET
            {
              parser->state = s_dead;
              CROW_SET_ERRNO(CHPE_INVALID_VERSION);
              goto error;
            }
            parser->http_major = 0;
            parser->http_minor = 9;
            parser->state = (ch == cr) ?
              s_req_line_almost_done :
              s_header_field_start;
            CROW_CALLBACK_DATA(url);
            break;
          default:
            parser->state = parse_url_char(static_cast<state>(parser->state), ch, parser, url_start_mark, p);
            if (CROW_UNLIKELY(parser->state == s_dead)) {
              CROW_SET_ERRNO(CHPE_INVALID_URL);
              goto error;
            }
        }
        break;
      }

      case s_req_http_start:
        switch (ch) {
          case ' ':
            break;
          case 'H':
            parser->state = s_req_http_H;
            break;
          case 'I':
            if (parser->method == (unsigned)HTTPMethod::Source) {
              parser->state = s_req_http_I;
              break;
            }
            /* fall through */
          default:
            CROW_SET_ERRNO(CHPE_INVALID_CONSTANT);
            goto error;
        }
        break;

      case s_req_http_H:
        CROW_STRICT_CHECK(ch != 'T');
        parser->state = s_req_http_HT;
        break;

      case s_req_http_HT:
        CROW_STRICT_CHECK(ch != 'T');
        parser->state = s_req_http_HTT;
        break;

      case s_req_http_HTT:
        CROW_STRICT_CHECK(ch != 'P');
        parser->state = s_req_http_HTTP;
        break;

      case s_req_http_I:
        CROW_STRICT_CHECK(ch != 'C');
        parser->state = s_req_http_IC;
        break;

      case s_req_http_IC:
        CROW_STRICT_CHECK(ch != 'E');
        parser->state = s_req_http_HTTP;  /* Treat "ICE" as "HTTP". */
        break;

      case s_req_http_HTTP:
        CROW_STRICT_CHECK(ch != '/');
        parser->state = s_req_http_major;
        break;

      /* dot */
      case s_req_http_major:
        if (CROW_UNLIKELY(!CROW_IS_NUM(ch))) {
          CROW_SET_ERRNO(CHPE_INVALID_VERSION);
          goto error;
        }

        parser->http_major = ch - '0';
        parser->state = s_req_http_dot;
        break;

      case s_req_http_dot:
      {
        if (CROW_UNLIKELY(ch != '.')) {
          CROW_SET_ERRNO(CHPE_INVALID_VERSION);
          goto error;
        }

        parser->state = s_req_http_minor;
        break;
      }

      /* minor HTTP version */
      case s_req_http_minor:
        if (CROW_UNLIKELY(!CROW_IS_NUM(ch))) {
          CROW_SET_ERRNO(CHPE_INVALID_VERSION);
          goto error;
        }

        parser->http_minor = ch - '0';
        parser->state = s_req_http_end;
        break;

      /* end of request line */
      case s_req_http_end:
      {
        if (ch == cr) {
          parser->state = s_req_line_almost_done;
          break;
        }

        if (ch == lf) {
          parser->state = s_header_field_start;
          break;
        }

        CROW_SET_ERRNO(CHPE_INVALID_VERSION);
        goto error;
        break;
      }

      /* end of request line */
      case s_req_line_almost_done:
      {
        if (CROW_UNLIKELY(ch != lf)) {
          CROW_SET_ERRNO(CHPE_LF_EXPECTED);
          goto error;
        }

        parser->state = s_header_field_start;
        break;
      }

      case s_header_field_start:
      {
        if (ch == cr) {
          parser->state = s_headers_almost_done;
          break;
        }

        if (ch == lf) {
          /* they might be just sending \n instead of \r\n so this would be
           * the second \n to denote the end of headers*/
          parser->state = s_headers_almost_done;
          CROW_REEXECUTE();
        }

        c = CROW_TOKEN(ch);

        if (CROW_UNLIKELY(!c)) {
          CROW_SET_ERRNO(CHPE_INVALID_HEADER_TOKEN);
          goto error;
        }

        CROW_MARK(header_field);

        parser->index = 0;
        parser->state = s_header_field;

        switch (c) {
          case 'c':
            parser->header_state = h_C;
            break;

          case 'p':
            parser->header_state = h_matching_proxy_connection;
            break;

          case 't':
            parser->header_state = h_matching_transfer_encoding;
            break;

          case 'u':
            parser->header_state = h_matching_upgrade;
            break;

          default:
            parser->header_state = h_general;
            break;
        }
        break;
      }

      case s_header_field:
      {        
        const char* start = p;
        for (; p != data + len; p++) {
          ch = *p;
          c = CROW_TOKEN(ch);

          if (!c)
            break;
          
          switch (parser->header_state) {
            case h_general: {
              size_t left = data + len - p;
              const char* pe = p + CROW_MIN(left, max_header_size);
              while (p+1 < pe && CROW_TOKEN(p[1])) {
                p++;
              }
              break;
            }

            case h_C:
              parser->index++;
              parser->header_state = (c == 'o' ? h_CO : h_general);
              break;

            case h_CO:
              parser->index++;
              parser->header_state = (c == 'n' ? h_CON : h_general);
              break;

            case h_CON:
              parser->index++;
              switch (c) {
                case 'n':
                  parser->header_state = h_matching_connection;
                  break;
                case 't':
                  parser->header_state = h_matching_content_length;
                  break;
                default:
                  parser->header_state = h_general;
                  break;
              }
              break;

            /* connection */

            case h_matching_connection:
              parser->index++;
              if (parser->index > sizeof(CROW_CONNECTION)-1 || c != CROW_CONNECTION[parser->index]) {
                parser->header_state = h_general;
              } else if (parser->index == sizeof(CROW_CONNECTION)-2) {
                parser->header_state = h_connection;
              }
              break;

            /* proxy-connection */

            case h_matching_proxy_connection:
              parser->index++;
              if (parser->index > sizeof(CROW_PROXY_CONNECTION)-1 || c != CROW_PROXY_CONNECTION[parser->index]) {
                parser->header_state = h_general;
              } else if (parser->index == sizeof(CROW_PROXY_CONNECTION)-2) {
                parser->header_state = h_connection;
              }
              break;

            /* content-length */

            case h_matching_content_length:
              parser->index++;
              if (parser->index > sizeof(CROW_CONTENT_LENGTH)-1 || c != CROW_CONTENT_LENGTH[parser->index]) {
                parser->header_state = h_general;
              } else if (parser->index == sizeof(CROW_CONTENT_LENGTH)-2) {
                parser->header_state = h_content_length;
              }
              break;

            /* transfer-encoding */

            case h_matching_transfer_encoding:
              parser->index++;
              if (parser->index > sizeof(CROW_TRANSFER_ENCODING)-1 || c != CROW_TRANSFER_ENCODING[parser->index]) {
                parser->header_state = h_general;
              } else if (parser->index == sizeof(CROW_TRANSFER_ENCODING)-2) {
                parser->header_state = h_transfer_encoding;
                parser->uses_transfer_encoding = 1;
              }
              break;

            /* upgrade */

            case h_matching_upgrade:
              parser->index++;
              if (parser->index > sizeof(CROW_UPGRADE)-1 || c != CROW_UPGRADE[parser->index]) {
                parser->header_state = h_general;
              } else if (parser->index == sizeof(CROW_UPGRADE)-2) {
                parser->header_state = h_upgrade;
              }
              break;

            case h_connection:
            case h_content_length:
            case h_transfer_encoding:
            case h_upgrade:
              if (ch != ' ') parser->header_state = h_general;
              break;

            default:
              assert(0 && "Unknown header_state");
              break;
          }
        }

        if (p == data + len) {
          --p;
          CROW_COUNT_HEADER_SIZE(p - start);
          break;
        }

        CROW_COUNT_HEADER_SIZE(p - start);

        if (ch == ':') {
          parser->state = s_header_value_discard_ws;
          CROW_CALLBACK_DATA(header_field);
          break;
        }
/* RFC-7230 Sec 3.2.4 expressly forbids line-folding in header field-names.
        if (ch == cr) {
          parser->state = s_header_almost_done;
          CROW_CALLBACK_DATA(header_field);
          break;
        }

        if (ch == lf) {
          parser->state = s_header_field_start;
          CROW_CALLBACK_DATA(header_field);
          break;
        }
*/
        CROW_SET_ERRNO(CHPE_INVALID_HEADER_TOKEN);
        goto error;
      }

      case s_header_value_discard_ws:
        if (ch == ' ' || ch == '\t') break;

        if (ch == cr) {
          parser->state = s_header_value_discard_ws_almost_done;
          break;
        }

        if (ch == lf) {
          parser->state = s_header_value_discard_lws;
          break;
        }

        /* fall through */

      case s_header_value_start:
      {
        CROW_MARK(header_value);

        parser->state = s_header_value;
        parser->index = 0;

        c = CROW_LOWER(ch);

        switch (parser->header_state) {
          case h_upgrade:
            parser->flags |= F_UPGRADE;
            parser->header_state = h_general;
            break;

          case h_transfer_encoding:
            /* looking for 'Transfer-Encoding: chunked' */
            if ('c' == c) {
              parser->header_state = h_matching_transfer_encoding_chunked;
            } else {
              parser->header_state = h_matching_transfer_encoding_token;
            }
            break;
            
          /* Multi-value `Transfer-Encoding` header */
          case h_matching_transfer_encoding_token_start:
            break;

          case h_content_length:
            if (CROW_UNLIKELY(!CROW_IS_NUM(ch))) {
              CROW_SET_ERRNO(CHPE_INVALID_CONTENT_LENGTH);
              goto error;
            }
            
            if (parser->flags & F_CONTENTLENGTH) {
              CROW_SET_ERRNO(CHPE_UNEXPECTED_CONTENT_LENGTH);
              goto error;
            }
            parser->flags |= F_CONTENTLENGTH;
            parser->content_length = ch - '0';
            parser->header_state = h_content_length_num;
            break;

          /* when obsolete line folding is encountered for content length
           * continue to the s_header_value state */
          case h_content_length_ws:
            break;

          case h_connection:
            /* looking for 'Connection: keep-alive' */
            if (c == 'k') {
              parser->header_state = h_matching_connection_keep_alive;
            /* looking for 'Connection: close' */
            } else if (c == 'c') {
              parser->header_state = h_matching_connection_close;
            } else if (c == ' ' || c == '\t') {
              /* Skip lws */
            } else {
              parser->header_state = h_general;
            }
            break;

          default:
            parser->header_state = h_general;
            break;
        }
        break;
      }

      case s_header_value:
      {
        const char* start = p;
        enum header_states h_state = static_cast<header_states>(parser->header_state);
        for (; p != data + len; p++) {
          ch = *p;

          if (ch == cr) {
            parser->state = s_header_almost_done;
            parser->header_state = h_state;
            CROW_CALLBACK_DATA(header_value);
            break;
          }

          if (ch == lf) {
            parser->state = s_header_almost_done;
            CROW_COUNT_HEADER_SIZE(p - start);
            parser->header_state = h_state;
            CROW_CALLBACK_DATA_NOADVANCE(header_value);
            CROW_REEXECUTE();
          }
          
          if (!lenient && !CROW_IS_HEADER_CHAR(ch)) {
            CROW_SET_ERRNO(CHPE_INVALID_HEADER_TOKEN);
            goto error;
          }
          
          c = CROW_LOWER(ch);

          switch (h_state) {
            case h_general:
              {
                size_t left = data + len - p;
                const char* pe = p + CROW_MIN(left, max_header_size);

                for (; p != pe; p++) {
                  ch = *p;
                  if (ch == cr || ch == lf) {
                    --p;
                    break;
                  }
                  if (!lenient && !CROW_IS_HEADER_CHAR(ch)) {
                    CROW_SET_ERRNO(CHPE_INVALID_HEADER_TOKEN);
                    goto error;
                  }
                }
                if (p == data + len)
                  --p;
                break;
              }

            case h_connection:
            case h_transfer_encoding:
              assert(0 && "Shouldn't get here.");
              break;

            case h_content_length:
              if (ch == ' ') break;
              h_state = h_content_length_num;
              /* fall through */

            case h_content_length_num:
            {
              uint64_t t;

              if (ch == ' ') {
                h_state = h_content_length_ws;
                break;
              }

              if (CROW_UNLIKELY(!CROW_IS_NUM(ch))) {
                CROW_SET_ERRNO(CHPE_INVALID_CONTENT_LENGTH);
                parser->header_state = h_state;
                goto error;
              }

              t = parser->content_length;
              t *= 10;
              t += ch - '0';

              /* Overflow? Test against a conservative limit for simplicity. */
              if (CROW_UNLIKELY((CROW_ULLONG_MAX - 10) / 10 < parser->content_length)) {
                CROW_SET_ERRNO(CHPE_INVALID_CONTENT_LENGTH);
                parser->header_state = h_state;
                goto error;
              }

              parser->content_length = t;
              break;
            }
            
            case h_content_length_ws:
              if (ch == ' ') break;
              CROW_SET_ERRNO(CHPE_INVALID_CONTENT_LENGTH);
              parser->header_state = h_state;
              goto error;

            /* Transfer-Encoding: chunked */
            case h_matching_transfer_encoding_token_start:
              /* looking for 'Transfer-Encoding: chunked' */
              if ('c' == c) {
                h_state = h_matching_transfer_encoding_chunked;
              } else if (CROW_TOKEN(c)) {
                /* TODO(indutny): similar code below does this, but why?
                 * At the very least it seems to be inconsistent given that
                 * h_matching_transfer_encoding_token does not check for
                 * `STRICT_TOKEN`
                 */
                h_state = h_matching_transfer_encoding_token;
              } else if (c == ' ' || c == '\t') {
                /* Skip lws */
              } else {
                h_state = h_general;
              }
              break;

            case h_matching_transfer_encoding_chunked:
              parser->index++;
              if (parser->index > sizeof(CROW_CHUNKED)-1 || c != CROW_CHUNKED[parser->index]) {
                h_state = h_matching_transfer_encoding_token;
              } else if (parser->index == sizeof(CROW_CHUNKED)-2) {
                h_state = h_transfer_encoding_chunked;
              }
              break;

            case h_matching_transfer_encoding_token:
              if (ch == ',') {
                h_state = h_matching_transfer_encoding_token_start;
                parser->index = 0;
              }
              break;

            /* looking for 'Connection: keep-alive' */
            case h_matching_connection_keep_alive:
              parser->index++;
              if (parser->index > sizeof(CROW_KEEP_ALIVE)-1 || c != CROW_KEEP_ALIVE[parser->index]) {
                h_state = h_general;
              } else if (parser->index == sizeof(CROW_KEEP_ALIVE)-2) {
                h_state = h_connection_keep_alive;
              }
              break;

            /* looking for 'Connection: close' */
            case h_matching_connection_close:
              parser->index++;
              if (parser->index > sizeof(CROW_CLOSE)-1 || c != CROW_CLOSE[parser->index]) {
                h_state = h_general;
              } else if (parser->index == sizeof(CROW_CLOSE)-2) {
                h_state = h_connection_close;
              }
              break;

              // Edited from original (because of commits that werent included)
            case h_transfer_encoding_chunked:
              if (ch != ' ') h_state = h_matching_transfer_encoding_token;
              break;
            case h_connection_keep_alive:
            case h_connection_close:
              if (ch != ' ') h_state = h_general;
              break;

            default:
              parser->state = s_header_value;
              h_state = h_general;
              break;
          }
        }
        parser->header_state = h_state;
        
        
        if (p == data + len)
          --p;
        
        CROW_COUNT_HEADER_SIZE(p - start);
        break;
      }

      case s_header_almost_done:
      {
        if (CROW_UNLIKELY(ch != lf)) {
          CROW_SET_ERRNO(CHPE_LF_EXPECTED);
          goto error;
        }

        parser->state = s_header_value_lws;
        break;
      }

      case s_header_value_lws:
      {
        if (ch == ' ' || ch == '\t') {
          if (parser->header_state == h_content_length_num) {
              /* treat obsolete line folding as space */
              parser->header_state = h_content_length_ws;
          }
          parser->state = s_header_value_start;
          CROW_REEXECUTE();
        }

        /* finished the header */
        switch (parser->header_state) {
          case h_connection_keep_alive:
            parser->flags |= F_CONNECTION_KEEP_ALIVE;
            break;
          case h_connection_close:
            parser->flags |= F_CONNECTION_CLOSE;
            break;
          case h_transfer_encoding_chunked:
            parser->flags |= F_CHUNKED;
            break;
          default:
            break;
        }

        parser->state = s_header_field_start;
        CROW_REEXECUTE();
      }

      case s_header_value_discard_ws_almost_done:
      {
        CROW_STRICT_CHECK(ch != lf);
        parser->state = s_header_value_discard_lws;
        break;
      }

      case s_header_value_discard_lws:
      {
        if (ch == ' ' || ch == '\t') {
          parser->state = s_header_value_discard_ws;
          break;
        } else {
          /* header value was empty */
          CROW_MARK(header_value);
          parser->state = s_header_field_start;
          CROW_CALLBACK_DATA_NOADVANCE(header_value);
          CROW_REEXECUTE();
        }
      }

      case s_headers_almost_done:
      {
        CROW_STRICT_CHECK(ch != lf);

        if (parser->flags & F_TRAILING) {
          /* End of a chunked request */
          CROW_CALLBACK_NOTIFY(message_complete);
          break;
        }
        
        /* Cannot use transfer-encoding and a content-length header together
           per the HTTP specification. (RFC 7230 Section 3.3.3) */
        if ((parser->uses_transfer_encoding == 1) &&
            (parser->flags & F_CONTENTLENGTH)) {
          /* Allow it for lenient parsing as long as `Transfer-Encoding` is
           * not `chunked` or allow_length_with_encoding is set
           */
          if (parser->flags & F_CHUNKED) {
            if (!allow_chunked_length) {
              CROW_SET_ERRNO(CHPE_UNEXPECTED_CONTENT_LENGTH);
              goto error;
            }
          } else if (!lenient) {
            CROW_SET_ERRNO(CHPE_UNEXPECTED_CONTENT_LENGTH);
            goto error;
          }
        }
        
        parser->state = s_headers_done;

        /* Set this here so that on_headers_complete() callbacks can see it */
        parser->upgrade =
          (parser->flags & F_UPGRADE || parser->method == (unsigned)HTTPMethod::Connect);

        /* Here we call the headers_complete callback. This is somewhat
         * different than other callbacks because if the user returns 1, we
         * will interpret that as saying that this message has no body. This
         * is needed for the annoying case of recieving a response to a HEAD
         * request.
         *
         * We'd like to use CROW_CALLBACK_NOTIFY_NOADVANCE() here but we cannot, so
         * we have to simulate it by handling a change in errno below.
         */
        if (settings->on_headers_complete) {
          switch (settings->on_headers_complete(parser)) {
            case 0:
              break;

            case 2:
              parser->upgrade = 1;
              //break;

            /* fall through */
            case 1:
              parser->flags |= F_SKIPBODY;
              break;

            default:
              CROW_SET_ERRNO(CHPE_CB_headers_complete);
              parser->nread = nread;
              return p - data; /* Error */
          }
        }

        if (CROW_HTTP_PARSER_ERRNO(parser) != CHPE_OK) {
          parser->nread = nread;
          return p - data;
        }

        CROW_REEXECUTE();
      }

      case s_headers_done:
      {
        CROW_STRICT_CHECK(ch != lf);

        parser->nread = 0;
        nread = 0;

        /* Exit, the rest of the connect is in a different protocol. */
        if (parser->upgrade) {
          CROW_CALLBACK_NOTIFY(message_complete);
          parser->nread = nread;
          return (p - data) + 1;
        }

        if (parser->flags & F_SKIPBODY) {
          CROW_CALLBACK_NOTIFY(message_complete);
        } else if (parser->flags & F_CHUNKED) {
          /* chunked encoding - ignore Content-Length header,
           * prepare for a chunk */
            parser->state = s_chunk_size_start;
        }
        else if (parser->uses_transfer_encoding == 1)
        {
            if (!lenient)
            {
                /* RFC 7230 3.3.3 */

                /* If a Transfer-Encoding header field
             * is present in a request and the chunked transfer coding is not
             * the final encoding, the message body length cannot be determined
             * reliably; the server MUST respond with the 400 (Bad Request)
             * status code and then close the connection.
             */
                CROW_SET_ERRNO(CHPE_INVALID_TRANSFER_ENCODING);
                parser->nread = nread;
                return (p - data); /* Error */
            }
            else
            {
                /* RFC 7230 3.3.3 */

                /* If a Transfer-Encoding header field is present in a response and
             * the chunked transfer coding is not the final encoding, the
             * message body length is determined by reading the connection until
             * it is closed by the server.
             */
                parser->state = s_body_identity_eof;
            }
        }
        else
        {
            if (parser->content_length == 0)
            {
                /* Content-Length header given but zero: Content-Length: 0\r\n */
                CROW_CALLBACK_NOTIFY(message_complete);
            }
            else if (parser->content_length != CROW_ULLONG_MAX)
            {
                /* Content-Length header given and non-zero */
                parser->state = s_body_identity;
            }
            else
            {
                /* Assume content-length 0 - read the next */
                CROW_CALLBACK_NOTIFY(message_complete);
            }
        }

        break;
      }

      case s_body_identity:
      {
        uint64_t to_read = CROW_MIN(parser->content_length,
                               (uint64_t) ((data + len) - p));

        assert(parser->content_length != 0
            && parser->content_length != CROW_ULLONG_MAX);

        /* The difference between advancing content_length and p is because
         * the latter will automaticaly advance on the next loop iteration.
         * Further, if content_length ends up at 0, we want to see the last
         * byte again for our message complete callback.
         */
        CROW_MARK(body);
        parser->content_length -= to_read;
        p += to_read - 1;

        if (parser->content_length == 0) {
          parser->state = s_message_done;

          /* Mimic CROW_CALLBACK_DATA_NOADVANCE() but with one extra byte.
           *
           * The alternative to doing this is to wait for the next byte to
           * trigger the data callback, just as in every other case. The
           * problem with this is that this makes it difficult for the test
           * harness to distinguish between complete-on-EOF and
           * complete-on-length. It's not clear that this distinction is
           * important for applications, but let's keep it for now.
           */
          CROW_CALLBACK_DATA_(body, p - body_mark + 1, p - data);
          CROW_REEXECUTE();
        }

        break;
      }

      /* read until EOF */
      case s_body_identity_eof:
        CROW_MARK(body);
        p = data + len - 1;

        break;

      case s_message_done:
        CROW_CALLBACK_NOTIFY(message_complete);
        break;

      case s_chunk_size_start:
      {
        assert(nread == 1);
        assert(parser->flags & F_CHUNKED);

        unhex_val = unhex[static_cast<unsigned char>(ch)];
        if (CROW_UNLIKELY(unhex_val == -1)) {
          CROW_SET_ERRNO(CHPE_INVALID_CHUNK_SIZE);
          goto error;
        }

        parser->content_length = unhex_val;
        parser->state = s_chunk_size;
        break;
      }

      case s_chunk_size:
      {
        uint64_t t;

        assert(parser->flags & F_CHUNKED);

        if (ch == cr) {
          parser->state = s_chunk_size_almost_done;
          break;
        }

        unhex_val = unhex[static_cast<unsigned char>(ch)];

        if (unhex_val == -1) {
          if (ch == ';' || ch == ' ') {
            parser->state = s_chunk_parameters;
            break;
          }

          CROW_SET_ERRNO(CHPE_INVALID_CHUNK_SIZE);
          goto error;
        }

        t = parser->content_length;
        t *= 16;
        t += unhex_val;

        /* Overflow? Test against a conservative limit for simplicity. */
        if (CROW_UNLIKELY((CROW_ULLONG_MAX - 16) / 16 < parser->content_length)) {
          CROW_SET_ERRNO(CHPE_INVALID_CONTENT_LENGTH);
          goto error;
        }

        parser->content_length = t;
        break;
      }

      case s_chunk_parameters:
      {
        assert(parser->flags & F_CHUNKED);
        /* just ignore this shit. TODO check for overflow */
        if (ch == cr) {
          parser->state = s_chunk_size_almost_done;
          break;
        }
        break;
      }

      case s_chunk_size_almost_done:
      {
        assert(parser->flags & F_CHUNKED);
        CROW_STRICT_CHECK(ch != lf);

        parser->nread = 0;
        nread = 0;

        if (parser->content_length == 0) {
          parser->flags |= F_TRAILING;
          parser->state = s_header_field_start;
        } else {
          parser->state = s_chunk_data;
        }
        break;
      }

      case s_chunk_data:
      {
        uint64_t to_read = CROW_MIN(parser->content_length,
                               (uint64_t) ((data + len) - p));

        assert(parser->flags & F_CHUNKED);
        assert(parser->content_length != 0
            && parser->content_length != CROW_ULLONG_MAX);

        /* See the explanation in s_body_identity for why the content
         * length and data pointers are managed this way.
         */
        CROW_MARK(body);
        parser->content_length -= to_read;
        p += to_read - 1;

        if (parser->content_length == 0) {
          parser->state = s_chunk_data_almost_done;
        }

        break;
      }

      case s_chunk_data_almost_done:
        assert(parser->flags & F_CHUNKED);
        assert(parser->content_length == 0);
        CROW_STRICT_CHECK(ch != cr);
        parser->state = s_chunk_data_done;
        CROW_CALLBACK_DATA(body);
        break;

      case s_chunk_data_done:
        assert(parser->flags & F_CHUNKED);
        CROW_STRICT_CHECK(ch != lf);
        parser->nread = 0;
        nread = 0;
        parser->state = s_chunk_size_start;
        break;

      default:
        assert(0 && "unhandled state");
        CROW_SET_ERRNO(CHPE_INVALID_INTERNAL_STATE);
        goto error;
    }
  }

  /* Run callbacks for any marks that we have leftover after we ran out of
   * bytes. There should be at most one of these set, so it's OK to invoke
   * them in series (unset marks will not result in callbacks).
   *
   * We use the NOADVANCE() variety of callbacks here because 'p' has already
   * overflowed 'data' and this allows us to correct for the off-by-one that
   * we'd otherwise have (since CROW_CALLBACK_DATA() is meant to be run with a 'p'
   * value that's in-bounds).
   */

  assert(((header_field_mark ? 1 : 0) +
          (header_value_mark ? 1 : 0) +
          (url_mark ? 1 : 0)  +
          (body_mark ? 1 : 0)) <= 1);

  CROW_CALLBACK_DATA_NOADVANCE(header_field);
  CROW_CALLBACK_DATA_NOADVANCE(header_value);
  CROW_CALLBACK_DATA_NOADVANCE(url);
  CROW_CALLBACK_DATA_NOADVANCE(body);

  parser->nread = nread;
  return len;

error:
  if (CROW_HTTP_PARSER_ERRNO(parser) == CHPE_OK) {
    CROW_SET_ERRNO(CHPE_UNKNOWN);
  }

  parser->nread = nread;
  return (p - data);
}

inline void
  http_parser_init(http_parser* parser)
{
  void *data = parser->data; /* preserve application data */
  memset(parser, 0, sizeof(*parser));
  parser->data = data;
  parser->state = s_start_req;
  parser->http_errno = CHPE_OK;
}

/* Return a string name of the given error */
inline const char *
http_errno_name(enum http_errno err) {
/* Map errno values to strings for human-readable output */
#define CROW_HTTP_STRERROR_GEN(n, s) { "CHPE_" #n, s },
static struct {
  const char *name;
  const char *description;
} http_strerror_tab[] = {
  CROW_HTTP_ERRNO_MAP(CROW_HTTP_STRERROR_GEN)
};
#undef CROW_HTTP_STRERROR_GEN
  assert(((size_t) err) < CROW_ARRAY_SIZE(http_strerror_tab));
  return http_strerror_tab[err].name;
}

/* Return a string description of the given error */
inline const char *
http_errno_description(enum http_errno err) {
/* Map errno values to strings for human-readable output */
#define CROW_HTTP_STRERROR_GEN(n, s) { "CHPE_" #n, s },
static struct {
  const char *name;
  const char *description;
} http_strerror_tab[] = {
  CROW_HTTP_ERRNO_MAP(CROW_HTTP_STRERROR_GEN)
};
#undef CROW_HTTP_STRERROR_GEN
  assert(((size_t) err) < CROW_ARRAY_SIZE(http_strerror_tab));
  return http_strerror_tab[err].description;
}

/* Checks if this is the final chunk of the body. */
inline int
http_body_is_final(const struct http_parser *parser) {
    return parser->state == s_message_done;
}

/* Change the maximum header size provided at compile time. */
inline void
http_parser_set_max_header_size(uint32_t size) {
  max_header_size = size;
}

#undef CROW_HTTP_ERRNO_MAP
#undef CROW_SET_ERRNO
#undef CROW_CALLBACK_NOTIFY_
#undef CROW_CALLBACK_NOTIFY
#undef CROW_CALLBACK_NOTIFY_NOADVANCE
#undef CROW_CALLBACK_DATA_
#undef CROW_CALLBACK_DATA
#undef CROW_CALLBACK_DATA_NOADVANCE
#undef CROW_MARK
#undef CROW_PROXY_CONNECTION
#undef CROW_CONNECTION
#undef CROW_CONTENT_LENGTH
#undef CROW_TRANSFER_ENCODING
#undef CROW_UPGRADE
#undef CROW_CHUNKED
#undef CROW_KEEP_ALIVE
#undef CROW_CLOSE
#undef CROW_PARSING_HEADER
#undef CROW_LOWER
#undef CROW_IS_ALPHA
#undef CROW_IS_NUM
#undef CROW_IS_ALPHANUM
//#undef CROW_IS_HEX
#undef CROW_IS_MARK
#undef CROW_IS_USERINFO_CHAR
#undef CROW_TOKEN
#undef CROW_IS_URL_CHAR
//#undef CROW_IS_HOST_CHAR
#undef CROW_STRICT_CHECK

}

// clang-format on
