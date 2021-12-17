/* merged revision: 5b951d74bd66ec9d38448e0a85b1cf8b85d97db3 */
/* updated to     : e13b274770da9b82a1085dec29182acfea72e7a7 */
/* commits not included:
 * 091ebb87783a58b249062540bbea07de2a11e9cf
 * 6132d1fefa03f769a3979355d1f5da0b8889cad2
 * 7ba312397c2a6c851a4b5efe6c1603b1e1bda6ff
 * d7675453a6c03180572f084e95eea0d02df39164
 * dff604db203986e532e5a679bafd0e7382c6bdd9 (Might be useful to actually add)
 * e01811e7f4894d7f0f7f4bd8492cccec6f6b4038 (related to above)
 * 05525c5fde1fc562481f6ae08fa7056185325daf (also related to above)
 * 350258965909f249f9c59823aac240313e0d0120 (cannot be implemented due to upgrade)
 */
/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#ifndef CROW_http_parser_h
#define CROW_http_parser_h
#ifdef __cplusplus
extern "C" {
#endif

/* Also update SONAME in the Makefile whenever you change these. */
#define CROW_HTTP_PARSER_VERSION_MAJOR 2
#define CROW_HTTP_PARSER_VERSION_MINOR 9
#define CROW_HTTP_PARSER_VERSION_PATCH 5

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

/* Compile with -DHTTP_PARSER_STRICT=0 to make less checks, but run
 * faster
 */
#ifndef CROW_HTTP_PARSER_STRICT
# define CROW_HTTP_PARSER_STRICT 1
#endif

/* Maximium header size allowed. If the macro is not defined
 * before including this header then the default is used. To
 * change the maximum header size, define the macro in the build
 * environment (e.g. -DHTTP_MAX_HEADER_SIZE=<value>). To remove
 * the effective limit on the size of the header, define the macro
 * to a very large number (e.g. -DHTTP_MAX_HEADER_SIZE=0x7fffffff)
 */
#ifndef CROW_HTTP_MAX_HEADER_SIZE
# define CROW_HTTP_MAX_HEADER_SIZE (80*1024)
#endif

typedef struct http_parser http_parser;
typedef struct http_parser_settings http_parser_settings;

// TODO "static_cast<x>(y)" should be replaced with "(?enum? x) y" ?

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


/* Status Codes */
#define HTTP_STATUS_MAP(CROW_XX)                                                 \
  CROW_XX(100, CONTINUE,                        Continue)                        \
  CROW_XX(101, SWITCHING_PROTOCOLS,             Switching Protocols)             \
  CROW_XX(102, PROCESSING,                      Processing)                      \
  CROW_XX(200, OK,                              OK)                              \
  CROW_XX(201, CREATED,                         Created)                         \
  CROW_XX(202, ACCEPTED,                        Accepted)                        \
  CROW_XX(203, NON_AUTHORITATIVE_INFORMATION,   Non-Authoritative Information)   \
  CROW_XX(204, NO_CONTENT,                      No Content)                      \
  CROW_XX(205, RESET_CONTENT,                   Reset Content)                   \
  CROW_XX(206, PARTIAL_CONTENT,                 Partial Content)                 \
  CROW_XX(207, MULTI_STATUS,                    Multi-Status)                    \
  CROW_XX(208, ALREADY_REPORTED,                Already Reported)                \
  CROW_XX(226, IM_USED,                         IM Used)                         \
  CROW_XX(300, MULTIPLE_CHOICES,                Multiple Choices)                \
  CROW_XX(301, MOVED_PERMANENTLY,               Moved Permanently)               \
  CROW_XX(302, FOUND,                           Found)                           \
  CROW_XX(303, SEE_OTHER,                       See Other)                       \
  CROW_XX(304, NOT_MODIFIED,                    Not Modified)                    \
  CROW_XX(305, USE_PROXY,                       Use Proxy)                       \
  CROW_XX(307, TEMPORARY_REDIRECT,              Temporary Redirect)              \
  CROW_XX(308, PERMANENT_REDIRECT,              Permanent Redirect)              \
  CROW_XX(400, BAD_REQUEST,                     Bad Request)                     \
  CROW_XX(401, UNAUTHORIZED,                    Unauthorized)                    \
  CROW_XX(402, PAYMENT_REQUIRED,                Payment Required)                \
  CROW_XX(403, FORBIDDEN,                       Forbidden)                       \
  CROW_XX(404, NOT_FOUND,                       Not Found)                       \
  CROW_XX(405, METHOD_NOT_ALLOWED,              Method Not Allowed)              \
  CROW_XX(406, NOT_ACCEPTABLE,                  Not Acceptable)                  \
  CROW_XX(407, PROXY_AUTHENTICATION_REQUIRED,   Proxy Authentication Required)   \
  CROW_XX(408, REQUEST_TIMEOUT,                 Request Timeout)                 \
  CROW_XX(409, CONFLICT,                        Conflict)                        \
  CROW_XX(410, GONE,                            Gone)                            \
  CROW_XX(411, LENGTH_REQUIRED,                 Length Required)                 \
  CROW_XX(412, PRECONDITION_FAILED,             Precondition Failed)             \
  CROW_XX(413, PAYLOAD_TOO_LARGE,               Payload Too Large)               \
  CROW_XX(414, URI_TOO_LONG,                    URI Too Long)                    \
  CROW_XX(415, UNSUPPORTED_MEDIA_TYPE,          Unsupported Media Type)          \
  CROW_XX(416, RANGE_NOT_SATISFIABLE,           Range Not Satisfiable)           \
  CROW_XX(417, EXPECTATION_FAILED,              Expectation Failed)              \
  CROW_XX(421, MISDIRECTED_REQUEST,             Misdirected Request)             \
  CROW_XX(422, UNPROCESSABLE_ENTITY,            Unprocessable Entity)            \
  CROW_XX(423, LOCKED,                          Locked)                          \
  CROW_XX(424, FAILED_DEPENDENCY,               Failed Dependency)               \
  CROW_XX(426, UPGRADE_REQUIRED,                Upgrade Required)                \
  CROW_XX(428, PRECONDITION_REQUIRED,           Precondition Required)           \
  CROW_XX(429, TOO_MANY_REQUESTS,               Too Many Requests)               \
  CROW_XX(431, REQUEST_HEADER_FIELDS_TOO_LARGE, Request Header Fields Too Large) \
  CROW_XX(451, UNAVAILABLE_FOR_LEGAL_REASONS,   Unavailable For Legal Reasons)   \
  CROW_XX(500, INTERNAL_SERVER_ERROR,           Internal Server Error)           \
  CROW_XX(501, NOT_IMPLEMENTED,                 Not Implemented)                 \
  CROW_XX(502, BAD_GATEWAY,                     Bad Gateway)                     \
  CROW_XX(503, SERVICE_UNAVAILABLE,             Service Unavailable)             \
  CROW_XX(504, GATEWAY_TIMEOUT,                 Gateway Timeout)                 \
  CROW_XX(505, HTTP_VERSION_NOT_SUPPORTED,      HTTP Version Not Supported)      \
  CROW_XX(506, VARIANT_ALSO_NEGOTIATES,         Variant Also Negotiates)         \
  CROW_XX(507, INSUFFICIENT_STORAGE,            Insufficient Storage)            \
  CROW_XX(508, LOOP_DETECTED,                   Loop Detected)                   \
  CROW_XX(510, NOT_EXTENDED,                    Not Extended)                    \
  CROW_XX(511, NETWORK_AUTHENTICATION_REQUIRED, Network Authentication Required) \

enum http_status
  {
#define CROW_XX(num, name, string) HTTP_STATUS_##name = num,
  HTTP_STATUS_MAP(CROW_XX)
#undef CROW_XX
  };


/* Request Methods */
#define CROW_HTTP_METHOD_MAP(CROW_XX)    \
  CROW_XX(0,  DELETE,      DELETE)       \
  CROW_XX(1,  GET,         GET)          \
  CROW_XX(2,  HEAD,        HEAD)         \
  CROW_XX(3,  POST,        POST)         \
  CROW_XX(4,  PUT,         PUT)          \
  /* pathological */                     \
  CROW_XX(5,  CONNECT,     CONNECT)      \
  CROW_XX(6,  OPTIONS,     OPTIONS)      \
  CROW_XX(7,  TRACE,       TRACE)        \
  /* RFC-5789 */                         \
  CROW_XX(8, PATCH,       PATCH)         \
  CROW_XX(9, PURGE,       PURGE)         \
  /* WebDAV */                           \
  CROW_XX(10,  COPY,        COPY)        \
  CROW_XX(11,  LOCK,        LOCK)        \
  CROW_XX(12, MKCOL,       MKCOL)        \
  CROW_XX(13, MOVE,        MOVE)         \
  CROW_XX(14, PROPFIND,    PROPFIND)     \
  CROW_XX(15, PROPPATCH,   PROPPATCH)    \
  CROW_XX(16, SEARCH,      SEARCH)       \
  CROW_XX(17, UNLOCK,      UNLOCK)       \
  CROW_XX(18, BIND,        BIND)         \
  CROW_XX(19, REBIND,      REBIND)       \
  CROW_XX(20, UNBIND,      UNBIND)       \
  CROW_XX(21, ACL,         ACL)          \
  /* subversion */                       \
  CROW_XX(22, REPORT,      REPORT)       \
  CROW_XX(23, MKACTIVITY,  MKACTIVITY)   \
  CROW_XX(24, CHECKOUT,    CHECKOUT)     \
  CROW_XX(25, MERGE,       MERGE)        \
  /* upnp */                             \
  CROW_XX(26, MSEARCH,     M-SEARCH)     \
  CROW_XX(27, NOTIFY,      NOTIFY)       \
  CROW_XX(28, SUBSCRIBE,   SUBSCRIBE)    \
  CROW_XX(29, UNSUBSCRIBE, UNSUBSCRIBE)  \
  /* CalDAV */                           \
  CROW_XX(30, MKCALENDAR,  MKCALENDAR)   \
  /* RFC-2068, section 19.6.1.2 */       \
  CROW_XX(31, LINK,        LINK)         \
  CROW_XX(32, UNLINK,      UNLINK)       \
  /* icecast */                          \
  CROW_XX(33, SOURCE,      SOURCE)       \

enum http_method
  {
#define CROW_XX(num, name, string) HTTP_##name = num,
  CROW_HTTP_METHOD_MAP(CROW_XX)
#undef CROW_XX
  };


enum http_parser_type { HTTP_REQUEST, HTTP_RESPONSE, HTTP_BOTH };


/* Flag values for http_parser.flags field */
enum http_connection_flags
  { F_CHUNKED               = 1 << 0
  , F_CONNECTION_KEEP_ALIVE = 1 << 1
  , F_CONNECTION_CLOSE      = 1 << 2
  , F_TRAILING              = 1 << 3
  , F_UPGRADE               = 1 << 4
  , F_SKIPBODY              = 1 << 5
  , F_CONTENTLENGTH         = 1 << 6
  };


/* Map for errno-related constants
 *
 * The provided argument should be a macro that takes 2 arguments.
 */
#define CROW_HTTP_ERRNO_MAP(CROW_XX)                                      \
  /* No error */                                                          \
  CROW_XX(OK, "success")                                                  \
                                                                          \
  /* Callback-related errors */                                           \
  CROW_XX(CB_message_begin, "the on_message_begin callback failed")       \
  CROW_XX(CB_url, "the on_url callback failed")                           \
  CROW_XX(CB_header_field, "the on_header_field callback failed")         \
  CROW_XX(CB_header_value, "the on_header_value callback failed")         \
  CROW_XX(CB_headers_complete, "the on_headers_complete callback failed") \
  CROW_XX(CB_body, "the on_body callback failed")                         \
  CROW_XX(CB_message_complete, "the on_message_complete callback failed") \
  CROW_XX(CB_status, "the on_status callback failed")                     \
                                                                          \
  /* Parsing-related errors */                                            \
  CROW_XX(INVALID_EOF_STATE, "stream ended at an unexpected time")        \
  CROW_XX(HEADER_OVERFLOW,                                                \
     "too many header bytes seen; overflow detected")                     \
  CROW_XX(CLOSED_CONNECTION,                                              \
     "data received after completed connection: close message")           \
  CROW_XX(INVALID_VERSION, "invalid HTTP version")                        \
  CROW_XX(INVALID_STATUS, "invalid HTTP status code")                     \
  CROW_XX(INVALID_METHOD, "invalid HTTP method")                          \
  CROW_XX(INVALID_URL, "invalid URL")                                     \
  CROW_XX(INVALID_HOST, "invalid host")                                   \
  CROW_XX(INVALID_PORT, "invalid port")                                   \
  CROW_XX(INVALID_PATH, "invalid path")                                   \
  CROW_XX(INVALID_QUERY_STRING, "invalid query string")                   \
  CROW_XX(INVALID_FRAGMENT, "invalid fragment")                           \
  CROW_XX(LF_EXPECTED, "CROW_LF character expected")                      \
  CROW_XX(INVALID_HEADER_TOKEN, "invalid character in header")            \
  CROW_XX(INVALID_CONTENT_LENGTH,                                         \
     "invalid character in content-length header")                        \
  CROW_XX(UNEXPECTED_CONTENT_LENGTH,                                      \
     "unexpected content-length header")                                  \
  CROW_XX(INVALID_CHUNK_SIZE,                                             \
     "invalid character in chunk size header")                            \
  CROW_XX(INVALID_CONSTANT, "invalid constant string")                    \
  CROW_XX(INVALID_INTERNAL_STATE, "encountered unexpected internal state")\
  CROW_XX(STRICT, "strict mode assertion failed")                         \
  CROW_XX(PAUSED, "parser is paused")                                     \
  CROW_XX(UNKNOWN, "an unknown error occurred")                           \
  CROW_XX(INVALID_TRANSFER_ENCODING,                                      \
     "request has invalid transfer-encoding")                             \


/* Define HPE_* values for each errno value above */
#define CROW_HTTP_ERRNO_GEN(n, s) HPE_##n,
enum http_errno {
  CROW_HTTP_ERRNO_MAP(CROW_HTTP_ERRNO_GEN)
};
#undef CROW_HTTP_ERRNO_GEN


/* Get an http_errno value from an http_parser */
#define CROW_HTTP_PARSER_ERRNO(p)            ((enum http_errno) (p)->http_errno)


struct http_parser {
  /** PRIVATE **/
  unsigned int type : 2;         /* enum http_parser_type */
  unsigned int flags : 7;        /* F_* values from 'flags' enum; semi-public */
  unsigned int state : 8;        /* enum state from http_parser.c */
  unsigned int header_state : 7; /* enum header_state from http_parser.c */
  unsigned int index : 5;        /* index into current matcher */
  unsigned int uses_transfer_encoding : 1; /* Transfer-Encoding header is present */
  unsigned int allow_chunked_length : 1; /* Allow headers with both
                                          * `Content-Length` and
                                          * `Transfer-Encoding: chunked` set */
  unsigned int lenient_http_headers : 1;

  uint32_t nread;          /* # bytes read in various scenarios */
  uint64_t content_length; /* # bytes in body. `(uint64_t) -1` (all bits one)
                            * if no Content-Length header.
                            */

  /** READ-ONLY **/
  unsigned short http_major;
  unsigned short http_minor;
  unsigned int status_code : 16; /* responses only */
  unsigned int method : 8;       /* requests only */
  unsigned int http_errno : 7;

  /* 1 = Upgrade header was present and the parser has exited because of that.
   * 0 = No upgrade header present.
   * Should be checked when http_parser_execute() returns in addition to
   * error checking.
   */
  unsigned int upgrade : 1;

  /** PUBLIC **/
  void *data; /* A pointer to get hook to the "connection" or "socket" object */
};


struct http_parser_settings {
  http_cb      on_message_begin;
  http_data_cb on_url;
  http_data_cb on_status;
  http_data_cb on_header_field;
  http_data_cb on_header_value;
  http_cb      on_headers_complete;
  http_data_cb on_body;
  http_cb      on_message_complete;
};


enum http_parser_url_fields
  { UF_SCHEMA           = 0
  , UF_HOST             = 1
  , UF_PORT             = 2
  , UF_PATH             = 3
  , UF_QUERY            = 4
  , UF_FRAGMENT         = 5
  , UF_USERINFO         = 6
  , UF_MAX              = 7
  };


/* Result structure for http_parser_parse_url().
 *
 * Callers should index into field_data[] with UF_* values iff field_set
 * has the relevant (1 << UF_*) bit set. As a courtesy to clients (and
 * because we probably have padding left over), we convert any port to
 * a uint16_t.
 */
struct http_parser_url {
  uint16_t field_set;           /* Bitmask of (1 << UF_*) values */
  uint16_t port;                /* Converted UF_PORT string */

  struct {
    uint16_t off;               /* Offset into buffer in which field starts */
    uint16_t len;               /* Length of run in buffer */
  } field_data[UF_MAX];
};


/* Returns the library version. Bits 16-23 contain the major version number,
 * bits 8-15 the minor version number and bits 0-7 the patch level.
 * Usage example:
 *
 *   unsigned long version = http_parser_version();
 *   unsigned major = (version >> 16) & 255;
 *   unsigned minor = (version >> 8) & 255;
 *   unsigned patch = version & 255;
 *   printf("http_parser v%u.%u.%u\n", major, minor, patch);
 */
unsigned long http_parser_version(void);

void http_parser_init(http_parser *parser, enum http_parser_type type);

/* Initialize http_parser_settings members to 0*/
void http_parser_settings_init(http_parser_settings *settings);

/* Executes the parser. Returns number of parsed bytes. Sets
 * `parser->http_errno` on error. */
size_t http_parser_execute(http_parser *parser,
                           const http_parser_settings *settings,
                           const char *data,
                           size_t len);


/* If http_should_keep_alive() in the on_headers_complete or
 * on_message_complete callback returns 0, then this should be
 * the last message on the connection.
 * If you are the server, respond with the "Connection: close" header.
 * If you are the client, close the connection.
 */
int http_should_keep_alive(const http_parser *parser);

/* Returns a string version of the HTTP method. */
const char *http_method_str(enum http_method m);

/* Returns a string version of the HTTP status code. */
const char *http_status_str(enum http_status s);

/* Return a string name of the given error */
const char *http_errno_name(enum http_errno err);

/* Return a string description of the given error */
const char *http_errno_description(enum http_errno err);

/* Initialize all http_parser_url members to 0 */
void http_parser_url_init(struct http_parser_url *u);

/* Parse a URL; return nonzero on failure */
int http_parser_parse_url(const char *buf, size_t buflen,
                          int is_connect,
                          struct http_parser_url *u);

/* Pause or un-pause the parser; a nonzero value pauses */
void http_parser_pause(http_parser *parser, int paused);

/* Checks if this is the final chunk of the body. */
int http_body_is_final(const http_parser *parser);

/* Change the maximum header size provided at compile time. */
void http_parser_set_max_header_size(uint32_t size);

/*#include "http_parser.h"*/
/* Based on src/http/ngx_http_parse.c from NGINX copyright Igor Sysoev
 *
 * Additional changes are licensed under the same terms as NGINX and
 * copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include <assert.h>
#include <stddef.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>

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

#ifndef CROW_ELEM_AT
# define CROW_ELEM_AT(a, i, v) ((unsigned int) (i) < CROW_ARRAY_SIZE(a) ? (a)[(i)] : (v))
#endif

#define CROW_SET_ERRNO(e)                                            \
do {                                                                 \
  parser->nread = nread;                                             \
  parser->http_errno = (e);                                          \
} while(0)

#ifdef __GNUC__
# define CROW_LIKELY(X) __builtin_expect(!!(X), 1)
# define CROW_UNLIKELY(X) __builtin_expect(!!(X), 0)
#else
# define CROW_LIKELY(X) (X)
# define CROW_UNLIKELY(X) (X)
#endif

/* Run the notify callback FOR, returning ER if it fails */
#define CROW_CALLBACK_NOTIFY_(FOR, ER)                               \
do {                                                                 \
  assert(CROW_HTTP_PARSER_ERRNO(parser) == HPE_OK);                  \
                                                                     \
  if (CROW_LIKELY(settings->on_##FOR)) {                             \
    if (CROW_UNLIKELY(0 != settings->on_##FOR(parser))) {            \
      CROW_SET_ERRNO(HPE_CB_##FOR);                                  \
    }                                                                \
                                                                     \
    /* We either errored above or got paused; get out */             \
    if (CROW_UNLIKELY(CROW_HTTP_PARSER_ERRNO(parser) != HPE_OK)) {   \
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
  assert(CROW_HTTP_PARSER_ERRNO(parser) == HPE_OK);                  \
                                                                     \
  if (FOR##_mark) {                                                  \
    if (CROW_LIKELY(settings->on_##FOR)) {                           \
      if (CROW_UNLIKELY(0 !=                                         \
          settings->on_##FOR(parser, FOR##_mark, (LEN)))) {          \
        CROW_SET_ERRNO(HPE_CB_##FOR);                                \
      }                                                              \
                                                                     \
      /* We either errored above or got paused; get out */           \
      if (CROW_UNLIKELY(CROW_HTTP_PARSER_ERRNO(parser) != HPE_OK)) { \
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
    CROW_SET_ERRNO(HPE_HEADER_OVERFLOW);                             \
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
  { s_dead = 1 /* important that this is > 0 */

  , s_start_req_or_res
  , s_res_or_resp_H
  , s_start_res
  , s_res_H
  , s_res_HT
  , s_res_HTT
  , s_res_HTTP
  , s_res_http_major
  , s_res_http_dot
  , s_res_http_minor
  , s_res_http_end
  , s_res_first_status_code
  , s_res_status_code
  , s_res_status_start
  , s_res_status
  , s_res_line_almost_done

  , s_start_req

  , s_req_method
  , s_req_spaces_before_url
  , s_req_schema
  , s_req_schema_slash
  , s_req_schema_slash_slash
  , s_req_server_start
  , s_req_server
  , s_req_server_with_at
  , s_req_path
  , s_req_query_string_start
  , s_req_query_string
  , s_req_fragment_start
  , s_req_fragment
  , s_req_http_start
  , s_req_http_H
  , s_req_http_HT
  , s_req_http_HTT
  , s_req_http_HTTP
  , s_req_http_I
  , s_req_http_IC
  , s_req_http_major
  , s_req_http_dot
  , s_req_http_minor
  , s_req_http_end
  , s_req_line_almost_done

  , s_header_field_start
  , s_header_field
  , s_header_value_discard_ws
  , s_header_value_discard_ws_almost_done
  , s_header_value_discard_lws
  , s_header_value_start
  , s_header_value
  , s_header_value_lws

  , s_header_almost_done

  , s_chunk_size_start
  , s_chunk_size
  , s_chunk_parameters
  , s_chunk_size_almost_done

  , s_headers_almost_done
  , s_headers_done

  /* Important: 's_headers_done' must be the last 'header' state. All
   * states beyond this must be 'body' states. It is used for overflow
   * checking. See the CROW_PARSING_HEADER() macro.
   */

  , s_chunk_data
  , s_chunk_data_almost_done
  , s_chunk_data_done

  , s_body_identity
  , s_body_identity_eof

  , s_message_done
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
#define CROW_CR                  '\r'
#define CROW_LF                  '\n'
#define CROW_LOWER(c)            (unsigned char)(c | 0x20)
#define CROW_IS_ALPHA(c)         (CROW_LOWER(c) >= 'a' && CROW_LOWER(c) <= 'z')
#define CROW_IS_NUM(c)           ((c) >= '0' && (c) <= '9')
#define CROW_IS_ALPHANUM(c)      (CROW_IS_ALPHA(c) || CROW_IS_NUM(c))
#define CROW_IS_HEX(c)           (CROW_IS_NUM(c) || (CROW_LOWER(c) >= 'a' && CROW_LOWER(c) <= 'f'))
#define CROW_IS_MARK(c)          ((c) == '-' || (c) == '_' || (c) == '.' || \
  (c) == '!' || (c) == '~' || (c) == '*' || (c) == '\'' || (c) == '(' || \
  (c) == ')')
#define CROW_IS_USERINFO_CHAR(c) (CROW_IS_ALPHANUM(c) || CROW_IS_MARK(c) || (c) == '%' || \
  (c) == ';' || (c) == ':' || (c) == '&' || (c) == '=' || (c) == '+' || \
  (c) == '$' || (c) == ',')

#if CROW_HTTP_PARSER_STRICT
#define CROW_TOKEN(c)            (tokens[(unsigned char)c])
#define CROW_IS_URL_CHAR(c)      (CROW_BIT_AT(normal_url_char, (unsigned char)c))
#define CROW_IS_HOST_CHAR(c)     (CROW_IS_ALPHANUM(c) || (c) == '.' || (c) == '-')
#else
#define CROW_TOKEN(c)            ((c == ' ') ? ' ' : tokens[(unsigned char)c])
#define CROW_IS_URL_CHAR(c)                                                         \
  (CROW_BIT_AT(normal_url_char, (unsigned char)c) || ((c) & 0x80))
#define CROW_IS_HOST_CHAR(c)                                                        \
  (CROW_IS_ALPHANUM(c) || (c) == '.' || (c) == '-' || (c) == '_')
#endif

  /**
 * Verify that a char is a valid visible (printable) US-ASCII
 * character or %x80-FF
 **/
#define CROW_IS_HEADER_CHAR(ch)                                                     \
  (ch == CROW_CR || ch == CROW_LF || ch == 9 || ((unsigned char)ch > 31 && ch != 127))

#define CROW_start_state (parser->type == HTTP_REQUEST ? s_start_req : s_start_res)


#if CROW_HTTP_PARSER_STRICT
# define CROW_STRICT_CHECK(cond)                                          \
do {                                                                 \
  if (cond) {                                                        \
    CROW_SET_ERRNO(HPE_STRICT);                                           \
    goto error;                                                      \
  }                                                                  \
} while (0)
# define CROW_NEW_MESSAGE() (http_should_keep_alive(parser) ? CROW_start_state : s_dead)
#else
# define CROW_STRICT_CHECK(cond)
# define CROW_NEW_MESSAGE() CROW_start_state
#endif



int http_message_needs_eof(const http_parser *parser);

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
parse_url_char(enum state s, const char ch)
{
#if CROW_HTTP_PARSER_STRICT
# define CROW_T(v) 0
#else
# define CROW_T(v) v
#endif


static const uint8_t normal_url_char[32] = {
/*   0 nul    1 soh    2 stx    3 etx    4 eot    5 enq    6 ack    7 bel  */
        0    |   0    |   0    |   0    |   0    |   0    |   0    |   0,
/*   8 bs     9 ht    10 nl    11 vt    12 np    13 cr    14 so    15 si   */
        0    | CROW_T(2)   |   0    |   0    | CROW_T(16)  |   0    |   0    |   0,
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

#if CROW_HTTP_PARSER_STRICT
  if (ch == '\t' || ch == '\f') {
    return s_dead;
  }
#endif

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

      switch (ch) {
        case '?':
          return s_req_query_string_start;

        case '#':
          return s_req_fragment_start;
      }

      break;

    case s_req_query_string_start:
    case s_req_query_string:
      if (CROW_IS_URL_CHAR(ch)) {
        return s_req_query_string;
      }

      switch (ch) {
        case '?':
          /* allow extra '?' in query string */
          return s_req_query_string;

        case '#':
          return s_req_fragment_start;
      }

      break;

    case s_req_fragment_start:
      if (CROW_IS_URL_CHAR(ch)) {
        return s_req_fragment;
      }

      switch (ch) {
        case '?':
          return s_req_fragment;

        case '#':
          return s;
      }

      break;

    case s_req_fragment:
      if (CROW_IS_URL_CHAR(ch)) {
        return s;
      }

      switch (ch) {
        case '?':
        case '#':
          return s;
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
static const char *method_strings[] =
  {
#define CROW_XX(num, name, string) #string,
  CROW_HTTP_METHOD_MAP(CROW_XX)
#undef CROW_XX
  };

/* Tokens as defined by rfc 2616. Also lowercases them.
 *        token       = 1*<any CHAR except CTLs or separators>
 *     separators     = "(" | ")" | "<" | ">" | "@"
 *                    | "," | ";" | ":" | "\" | <">
 *                    | "/" | "[" | "]" | "?" | "="
 *                    | "{" | "}" | SP | HT
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
  const char *body_mark = 0;
  const char *status_mark = 0;
  const unsigned int lenient = parser->lenient_http_headers;
  const unsigned int allow_chunked_length = parser->allow_chunked_length;
  
  uint32_t nread = parser->nread;

  /* We're in an error state. Don't bother doing anything. */
  if (CROW_HTTP_PARSER_ERRNO(parser) != HPE_OK) {
    return 0;
  }

  if (len == 0) {
    switch (parser->state) {
      case s_body_identity_eof:
        /* Use of CROW_CALLBACK_NOTIFY() here would erroneously return 1 byte read if
         * we got paused.
         */
        CROW_CALLBACK_NOTIFY_NOADVANCE(message_complete);
        return 0;

      case s_dead:
      case s_start_req_or_res:
      case s_start_res:
      case s_start_req:
        return 0;

      default:
        CROW_SET_ERRNO(HPE_INVALID_EOF_STATE);
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
  case s_req_fragment_start:
  case s_req_fragment:
    url_mark = data;
    break;
  case s_res_status:
    status_mark = data;
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
        if (CROW_LIKELY(ch == CROW_CR || ch == CROW_LF))
          break;

        CROW_SET_ERRNO(HPE_CLOSED_CONNECTION);
        goto error;

      case s_start_req_or_res:
      {
        if (ch == CROW_CR || ch == CROW_LF)
          break;
        parser->flags = 0;
        parser->uses_transfer_encoding = 0;
        parser->content_length = CROW_ULLONG_MAX;

        if (ch == 'H') {
          parser->state = s_res_or_resp_H;

          CROW_CALLBACK_NOTIFY(message_begin);
        } else {
          parser->type = HTTP_REQUEST;
          parser->state = s_start_req;
          CROW_REEXECUTE();
        }

        break;
      }

      case s_res_or_resp_H:
        if (ch == 'T') {
          parser->type = HTTP_RESPONSE;
          parser->state = s_res_HT;
        } else {
          if (CROW_UNLIKELY(ch != 'E')) {
            CROW_SET_ERRNO(HPE_INVALID_CONSTANT);
            goto error;
          }

          parser->type = HTTP_REQUEST;
          parser->method = HTTP_HEAD;
          parser->index = 2;
          parser->state = s_req_method;
        }
        break;

      case s_start_res:
      {
        if (ch == CROW_CR || ch == CROW_LF)
          break;
        parser->flags = 0;
        parser->uses_transfer_encoding = 0;
        parser->content_length = CROW_ULLONG_MAX;
        
        if (ch == 'H') {
          parser->state = s_res_H;
        } else {
          CROW_SET_ERRNO(HPE_INVALID_CONSTANT);
          goto error;
        }
        
        CROW_CALLBACK_NOTIFY(message_begin);
        break;
      }

      case s_res_H:
        CROW_STRICT_CHECK(ch != 'T');
        parser->state = s_res_HT;
        break;

      case s_res_HT:
        CROW_STRICT_CHECK(ch != 'T');
        parser->state = s_res_HTT;
        break;

      case s_res_HTT:
        CROW_STRICT_CHECK(ch != 'P');
        parser->state = s_res_HTTP;
        break;

      case s_res_HTTP:
        CROW_STRICT_CHECK(ch != '/');
        parser->state = s_res_http_major;
        break;

      case s_res_http_major:
        if (CROW_UNLIKELY(!CROW_IS_NUM(ch))) {
          CROW_SET_ERRNO(HPE_INVALID_VERSION);
          goto error;
        }

        parser->http_major = ch - '0';
        parser->state = s_res_http_dot;
        break;

      case s_res_http_dot:
      {
        if (CROW_UNLIKELY(ch != '.')) {
          CROW_SET_ERRNO(HPE_INVALID_VERSION);
          goto error;
        }

        parser->state = s_res_http_minor;
        break;
      }

      /* minor HTTP version */
      case s_res_http_minor:
        if (CROW_UNLIKELY(!CROW_IS_NUM(ch))) {
          CROW_SET_ERRNO(HPE_INVALID_VERSION);
          goto error;
        }

        parser->http_minor = ch - '0';
        parser->state = s_res_http_end;
        break;

      /* end of request line */
      case s_res_http_end:
      {
        if (CROW_UNLIKELY(ch != ' ')) {
          CROW_SET_ERRNO(HPE_INVALID_VERSION);
          goto error;
        }

        parser->state = s_res_first_status_code;
        break;
      }

      case s_res_first_status_code:
      {
        if (!CROW_IS_NUM(ch)) {
          if (ch == ' ') {
            break;
          }

          CROW_SET_ERRNO(HPE_INVALID_STATUS);
          goto error;
        }
        parser->status_code = ch - '0';
        parser->state = s_res_status_code;
        break;
      }

      case s_res_status_code:
      {
        if (!CROW_IS_NUM(ch)) {
          switch (ch) {
            case ' ':
              parser->state = s_res_status_start;
              break;
            case CROW_CR:
            case CROW_LF:
              parser->state = s_res_status_start;
              CROW_REEXECUTE();
              break;
            default:
              CROW_SET_ERRNO(HPE_INVALID_STATUS);
              goto error;
          }
          break;
        }

        parser->status_code *= 10;
        parser->status_code += ch - '0';

        if (CROW_UNLIKELY(parser->status_code > 999)) {
          CROW_SET_ERRNO(HPE_INVALID_STATUS);
          goto error;
        }

        break;
      }

      case s_res_status_start:
      {
        CROW_MARK(status);
        parser->state = s_res_status;
        parser->index = 0;

        if (ch == CROW_CR || ch == CROW_CR)
          CROW_REEXECUTE();

        break;
      }

      case s_res_status:
        if (ch == CROW_CR) {
          parser->state = s_res_line_almost_done;
          CROW_CALLBACK_DATA(status);
          break;
        }

        if (ch == CROW_LF) {
          parser->state = s_header_field_start;
          CROW_CALLBACK_DATA(status);
          break;
        }

        break;

      case s_res_line_almost_done:
        CROW_STRICT_CHECK(ch != CROW_LF);
        parser->state = s_header_field_start;
        break;

      case s_start_req:
      {
        if (ch == CROW_CR || ch == CROW_LF)
          break;
        parser->flags = 0;
        parser->uses_transfer_encoding = 0;
        parser->content_length = CROW_ULLONG_MAX;

        if (CROW_UNLIKELY(!CROW_IS_ALPHA(ch))) {
          CROW_SET_ERRNO(HPE_INVALID_METHOD);
          goto error;
        }

        parser->method = static_cast<http_method>(0);
        parser->index = 1;
        switch (ch) {
          case 'A': parser->method = HTTP_ACL; break;
          case 'B': parser->method = HTTP_BIND; break;
          case 'C': parser->method = HTTP_CONNECT; /* or COPY, CHECKOUT */ break;
          case 'D': parser->method = HTTP_DELETE; break;
          case 'G': parser->method = HTTP_GET; break;
          case 'H': parser->method = HTTP_HEAD; break;
          case 'L': parser->method = HTTP_LOCK; /* or LINK */ break;
          case 'M': parser->method = HTTP_MKCOL; /* or MOVE, MKACTIVITY, MERGE, M-SEARCH, MKCALENDAR */ break;
          case 'N': parser->method = HTTP_NOTIFY; break;
          case 'O': parser->method = HTTP_OPTIONS; break;
          case 'P': parser->method = HTTP_POST; /* or PROPFIND|PROPPATCH|PUT|PATCH|PURGE */ break;
          case 'R': parser->method = HTTP_REPORT; /* or REBIND */ break;
          case 'S': parser->method = HTTP_SUBSCRIBE; /* or SEARCH, SOURCE */ break;
          case 'T': parser->method = HTTP_TRACE; break;
          case 'U': parser->method = HTTP_UNLOCK; /* or UNSUBSCRIBE, UNBIND, UNLINK */ break;
          default:
            CROW_SET_ERRNO(HPE_INVALID_METHOD);
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
          CROW_SET_ERRNO(HPE_INVALID_METHOD);
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
            case (HTTP_##meth << 16 | pos << 8 | ch): \
              parser->method = HTTP_##new_meth; break;

            CROW_XX(POST,      1, 'U', PUT)
            CROW_XX(POST,      1, 'A', PATCH)
            CROW_XX(POST,      1, 'R', PROPFIND)
            CROW_XX(PUT,       2, 'R', PURGE)
            CROW_XX(CONNECT,   1, 'H', CHECKOUT)
            CROW_XX(CONNECT,   2, 'P', COPY)
            CROW_XX(MKCOL,     1, 'O', MOVE)
            CROW_XX(MKCOL,     1, 'E', MERGE)
            CROW_XX(MKCOL,     1, '-', MSEARCH)
            CROW_XX(MKCOL,     2, 'A', MKACTIVITY)
            CROW_XX(MKCOL,     3, 'A', MKCALENDAR)
            CROW_XX(SUBSCRIBE, 1, 'E', SEARCH)
            CROW_XX(SUBSCRIBE, 1, 'O', SOURCE)
            CROW_XX(REPORT,    2, 'B', REBIND)
            CROW_XX(PROPFIND,  4, 'P', PROPPATCH)
            CROW_XX(LOCK,      1, 'I', LINK)
            CROW_XX(UNLOCK,    2, 'S', UNSUBSCRIBE)
            CROW_XX(UNLOCK,    2, 'B', UNBIND)
            CROW_XX(UNLOCK,    3, 'I', UNLINK)
#undef CROW_XX
            default:
              CROW_SET_ERRNO(HPE_INVALID_METHOD);
              goto error;
          }
        } else {
          CROW_SET_ERRNO(HPE_INVALID_METHOD);
          goto error;
        }

        ++parser->index;
        break;
      }

      case s_req_spaces_before_url:
      {
        if (ch == ' ') break;

        CROW_MARK(url);
        if (parser->method == HTTP_CONNECT) {
          parser->state = s_req_server_start;
        }

        parser->state = parse_url_char(static_cast<state>(parser->state), ch);
        if (CROW_UNLIKELY(parser->state == s_dead)) {
          CROW_SET_ERRNO(HPE_INVALID_URL);
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
          case CROW_CR:
          case CROW_LF:
            CROW_SET_ERRNO(HPE_INVALID_URL);
            goto error;
          default:
            parser->state = parse_url_char(static_cast<state>(parser->state), ch);
            if (CROW_UNLIKELY(parser->state == s_dead)) {
              CROW_SET_ERRNO(HPE_INVALID_URL);
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
      case s_req_fragment_start:
      case s_req_fragment:
      {
        switch (ch) {
          case ' ':
            parser->state = s_req_http_start;
            CROW_CALLBACK_DATA(url);
            break;
          case CROW_CR:
          case CROW_LF:
            if (parser->method != HTTP_GET)
            {
                parser->state = s_dead;
                CROW_SET_ERRNO(HPE_INVALID_VERSION);
                goto error;
            }
            parser->http_major = 0;
            parser->http_minor = 9;
            parser->state = (ch == CROW_CR) ?
              s_req_line_almost_done :
              s_header_field_start;
            CROW_CALLBACK_DATA(url);
            break;
          default:
            parser->state = parse_url_char(static_cast<state>(parser->state), ch);
            if (CROW_UNLIKELY(parser->state == s_dead)) {
              CROW_SET_ERRNO(HPE_INVALID_URL);
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
            if (parser->method == HTTP_SOURCE) {
              parser->state = s_req_http_I;
              break;
            }
            /* fall through */
          default:
            CROW_SET_ERRNO(HPE_INVALID_CONSTANT);
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
          CROW_SET_ERRNO(HPE_INVALID_VERSION);
          goto error;
        }

        parser->http_major = ch - '0';
        parser->state = s_req_http_dot;
        break;

      case s_req_http_dot:
      {
        if (CROW_UNLIKELY(ch != '.')) {
          CROW_SET_ERRNO(HPE_INVALID_VERSION);
          goto error;
        }

        parser->state = s_req_http_minor;
        break;
      }

      /* minor HTTP version */
      case s_req_http_minor:
        if (CROW_UNLIKELY(!CROW_IS_NUM(ch))) {
          CROW_SET_ERRNO(HPE_INVALID_VERSION);
          goto error;
        }

        parser->http_minor = ch - '0';
        parser->state = s_req_http_end;
        break;

      /* end of request line */
      case s_req_http_end:
      {
        if (ch == CROW_CR) {
          parser->state = s_req_line_almost_done;
          break;
        }

        if (ch == CROW_LF) {
          parser->state = s_header_field_start;
          break;
        }

        CROW_SET_ERRNO(HPE_INVALID_VERSION);
        goto error;
        break;
      }

      /* end of request line */
      case s_req_line_almost_done:
      {
        if (CROW_UNLIKELY(ch != CROW_LF)) {
          CROW_SET_ERRNO(HPE_LF_EXPECTED);
          goto error;
        }

        parser->state = s_header_field_start;
        break;
      }

      case s_header_field_start:
      {
        if (ch == CROW_CR) {
          parser->state = s_headers_almost_done;
          break;
        }

        if (ch == CROW_LF) {
          /* they might be just sending \n instead of \r\n so this would be
           * the second \n to denote the end of headers*/
          parser->state = s_headers_almost_done;
          CROW_REEXECUTE();
        }

        c = CROW_TOKEN(ch);

        if (CROW_UNLIKELY(!c)) {
          CROW_SET_ERRNO(HPE_INVALID_HEADER_TOKEN);
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
              if (parser->index > sizeof(CROW_CONNECTION)-1
                  || c != CROW_CONNECTION[parser->index]) {
                parser->header_state = h_general;
              } else if (parser->index == sizeof(CROW_CONNECTION)-2) {
                parser->header_state = h_connection;
              }
              break;

            /* proxy-connection */

            case h_matching_proxy_connection:
              parser->index++;
              if (parser->index > sizeof(CROW_PROXY_CONNECTION)-1
                  || c != CROW_PROXY_CONNECTION[parser->index]) {
                parser->header_state = h_general;
              } else if (parser->index == sizeof(CROW_PROXY_CONNECTION)-2) {
                parser->header_state = h_connection;
              }
              break;

            /* content-length */

            case h_matching_content_length:
              parser->index++;
              if (parser->index > sizeof(CROW_CONTENT_LENGTH)-1
                  || c != CROW_CONTENT_LENGTH[parser->index]) {
                parser->header_state = h_general;
              } else if (parser->index == sizeof(CROW_CONTENT_LENGTH)-2) {
                parser->header_state = h_content_length;
              }
              break;

            /* transfer-encoding */

            case h_matching_transfer_encoding:
              parser->index++;
              if (parser->index > sizeof(CROW_TRANSFER_ENCODING)-1
                  || c != CROW_TRANSFER_ENCODING[parser->index]) {
                parser->header_state = h_general;
              } else if (parser->index == sizeof(CROW_TRANSFER_ENCODING)-2) {
                parser->header_state = h_transfer_encoding;
                parser->uses_transfer_encoding = 1;
              }
              break;

            /* upgrade */

            case h_matching_upgrade:
              parser->index++;
              if (parser->index > sizeof(CROW_UPGRADE)-1
                  || c != CROW_UPGRADE[parser->index]) {
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
        if (ch == CROW_CR) {
          parser->state = s_header_almost_done;
          CROW_CALLBACK_DATA(header_field);
          break;
        }

        if (ch == CROW_LF) {
          parser->state = s_header_field_start;
          CROW_CALLBACK_DATA(header_field);
          break;
        }
*/
        CROW_SET_ERRNO(HPE_INVALID_HEADER_TOKEN);
        goto error;
      }

      case s_header_value_discard_ws:
        if (ch == ' ' || ch == '\t') break;

        if (ch == CROW_CR) {
          parser->state = s_header_value_discard_ws_almost_done;
          break;
        }

        if (ch == CROW_LF) {
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
              CROW_SET_ERRNO(HPE_INVALID_CONTENT_LENGTH);
              goto error;
            }
            
            if (parser->flags & F_CONTENTLENGTH) {
              CROW_SET_ERRNO(HPE_UNEXPECTED_CONTENT_LENGTH);
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

          if (ch == CROW_CR) {
            parser->state = s_header_almost_done;
            parser->header_state = h_state;
            CROW_CALLBACK_DATA(header_value);
            break;
          }

          if (ch == CROW_LF) {
            parser->state = s_header_almost_done;
            CROW_COUNT_HEADER_SIZE(p - start);
            parser->header_state = h_state;
            CROW_CALLBACK_DATA_NOADVANCE(header_value);
            CROW_REEXECUTE();
          }
          
          if (!lenient && !CROW_IS_HEADER_CHAR(ch)) {
            CROW_SET_ERRNO(HPE_INVALID_HEADER_TOKEN);
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
                  if (ch == CROW_CR || ch == CROW_LF) {
                    --p;
                    break;
                  }
                  if (!lenient && !CROW_IS_HEADER_CHAR(ch)) {
                    CROW_SET_ERRNO(HPE_INVALID_HEADER_TOKEN);
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
                CROW_SET_ERRNO(HPE_INVALID_CONTENT_LENGTH);
                parser->header_state = h_state;
                goto error;
              }

              t = parser->content_length;
              t *= 10;
              t += ch - '0';

              /* Overflow? Test against a conservative limit for simplicity. */
              if (CROW_UNLIKELY((CROW_ULLONG_MAX - 10) / 10 < parser->content_length)) {
                CROW_SET_ERRNO(HPE_INVALID_CONTENT_LENGTH);
                parser->header_state = h_state;
                goto error;
              }

              parser->content_length = t;
              break;
            }
            
            case h_content_length_ws:
              if (ch == ' ') break;
              CROW_SET_ERRNO(HPE_INVALID_CONTENT_LENGTH);
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
              if (parser->index > sizeof(CROW_CHUNKED)-1
                  || c != CROW_CHUNKED[parser->index]) {
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
              if (parser->index > sizeof(CROW_KEEP_ALIVE)-1
                  || c != CROW_KEEP_ALIVE[parser->index]) {
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
        if (CROW_UNLIKELY(ch != CROW_LF)) {
          CROW_SET_ERRNO(HPE_LF_EXPECTED);
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
        CROW_STRICT_CHECK(ch != CROW_LF);
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
        CROW_STRICT_CHECK(ch != CROW_LF);

        if (parser->flags & F_TRAILING) {
          /* End of a chunked request */
          parser->state = CROW_NEW_MESSAGE();
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
              CROW_SET_ERRNO(HPE_UNEXPECTED_CONTENT_LENGTH);
              goto error;
            }
          } else if (!lenient) {
            CROW_SET_ERRNO(HPE_UNEXPECTED_CONTENT_LENGTH);
            goto error;
          }
        }
        
        parser->state = s_headers_done;

        /* Set this here so that on_headers_complete() callbacks can see it */
        parser->upgrade =
          (parser->flags & F_UPGRADE || parser->method == HTTP_CONNECT);

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
              CROW_SET_ERRNO(HPE_CB_headers_complete);
              parser->nread = nread;
              return p - data; /* Error */
          }
        }

        if (CROW_HTTP_PARSER_ERRNO(parser) != HPE_OK) {
          parser->nread = nread;
          return p - data;
        }

        CROW_REEXECUTE();
      }

      case s_headers_done:
      {
        CROW_STRICT_CHECK(ch != CROW_LF);

        parser->nread = 0;
        nread = 0;

        /* Exit, the rest of the connect is in a different protocol. */
        if (parser->upgrade) {
          parser->state = CROW_NEW_MESSAGE();
          CROW_CALLBACK_NOTIFY(message_complete);
          parser->nread = nread;
          return (p - data) + 1;
        }

        if (parser->flags & F_SKIPBODY) {
          parser->state = CROW_NEW_MESSAGE();
          CROW_CALLBACK_NOTIFY(message_complete);
        } else if (parser->flags & F_CHUNKED) {
          /* chunked encoding - ignore Content-Length header,
           * prepare for a chunk */
            parser->state = s_chunk_size_start;
        } else if (parser->uses_transfer_encoding == 1) {
          if (parser->type == HTTP_REQUEST && !lenient) {
            /* RFC 7230 3.3.3 */

            /* If a Transfer-Encoding header field
             * is present in a request and the chunked transfer coding is not
             * the final encoding, the message body length cannot be determined
             * reliably; the server MUST respond with the 400 (Bad Request)
             * status code and then close the connection.
             */
            CROW_SET_ERRNO(HPE_INVALID_TRANSFER_ENCODING);
            parser->nread = nread;
            return (p - data); /* Error */
          } else {
            /* RFC 7230 3.3.3 */

            /* If a Transfer-Encoding header field is present in a response and
             * the chunked transfer coding is not the final encoding, the
             * message body length is determined by reading the connection until
             * it is closed by the server.
             */
            parser->state = s_body_identity_eof;
          }
        } else {
          if (parser->content_length == 0) {
            /* Content-Length header given but zero: Content-Length: 0\r\n */
            parser->state = CROW_NEW_MESSAGE();
            CROW_CALLBACK_NOTIFY(message_complete);
          } else if (parser->content_length != CROW_ULLONG_MAX) {
            /* Content-Length header given and non-zero */
            parser->state = s_body_identity;
          } else {
            if (!http_message_needs_eof(parser)) {
              /* Assume content-length 0 - read the next */
              parser->state = CROW_NEW_MESSAGE();
              CROW_CALLBACK_NOTIFY(message_complete);
            } else {
              /* Read body until EOF */
              parser->state = s_body_identity_eof;
            }
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
        parser->state = CROW_NEW_MESSAGE();
        CROW_CALLBACK_NOTIFY(message_complete);
        break;

      case s_chunk_size_start:
      {
        assert(nread == 1);
        assert(parser->flags & F_CHUNKED);

        unhex_val = unhex[static_cast<unsigned char>(ch)];
        if (CROW_UNLIKELY(unhex_val == -1)) {
          CROW_SET_ERRNO(HPE_INVALID_CHUNK_SIZE);
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

        if (ch == CROW_CR) {
          parser->state = s_chunk_size_almost_done;
          break;
        }

        unhex_val = unhex[static_cast<unsigned char>(ch)];

        if (unhex_val == -1) {
          if (ch == ';' || ch == ' ') {
            parser->state = s_chunk_parameters;
            break;
          }

          CROW_SET_ERRNO(HPE_INVALID_CHUNK_SIZE);
          goto error;
        }

        t = parser->content_length;
        t *= 16;
        t += unhex_val;

        /* Overflow? Test against a conservative limit for simplicity. */
        if (CROW_UNLIKELY((CROW_ULLONG_MAX - 16) / 16 < parser->content_length)) {
          CROW_SET_ERRNO(HPE_INVALID_CONTENT_LENGTH);
          goto error;
        }

        parser->content_length = t;
        break;
      }

      case s_chunk_parameters:
      {
        assert(parser->flags & F_CHUNKED);
        /* just ignore this shit. TODO check for overflow */
        if (ch == CROW_CR) {
          parser->state = s_chunk_size_almost_done;
          break;
        }
        break;
      }

      case s_chunk_size_almost_done:
      {
        assert(parser->flags & F_CHUNKED);
        CROW_STRICT_CHECK(ch != CROW_LF);

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
        CROW_STRICT_CHECK(ch != CROW_CR);
        parser->state = s_chunk_data_done;
        CROW_CALLBACK_DATA(body);
        break;

      case s_chunk_data_done:
        assert(parser->flags & F_CHUNKED);
        CROW_STRICT_CHECK(ch != CROW_LF);
        parser->nread = 0;
        nread = 0;
        parser->state = s_chunk_size_start;
        break;

      default:
        assert(0 && "unhandled state");
        CROW_SET_ERRNO(HPE_INVALID_INTERNAL_STATE);
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
          (body_mark ? 1 : 0) +
          (status_mark ? 1 : 0)) <= 1);

  CROW_CALLBACK_DATA_NOADVANCE(header_field);
  CROW_CALLBACK_DATA_NOADVANCE(header_value);
  CROW_CALLBACK_DATA_NOADVANCE(url);
  CROW_CALLBACK_DATA_NOADVANCE(body);
  CROW_CALLBACK_DATA_NOADVANCE(status);

  parser->nread = nread;
  return len;

error:
  if (CROW_HTTP_PARSER_ERRNO(parser) == HPE_OK) {
    CROW_SET_ERRNO(HPE_UNKNOWN);
  }

  parser->nread = nread;
  return (p - data);
}


/* Does the parser need to see an EOF to find the end of the message? */
inline int
http_message_needs_eof (const http_parser *parser)
{
  if (parser->type == HTTP_REQUEST) {
    return 0;
  }

  /* See RFC 2616 section 4.4 */
  if (parser->status_code / 100 == 1 || /* 1xx e.g. Continue */
      parser->status_code == 204 ||     /* No Content */
      parser->status_code == 304 ||     /* Not Modified */
      parser->flags & F_SKIPBODY) {     /* response to a HEAD request */
    return 0;
  }

  /* RFC 7230 3.3.3, see `s_headers_almost_done` */
  if ((parser->uses_transfer_encoding == 1) &&
      (parser->flags & F_CHUNKED) == 0) {
    return 1;
  }

  if ((parser->flags & F_CHUNKED) || parser->content_length != CROW_ULLONG_MAX) {
    return 0;
  }

  return 1;
}


inline int
http_should_keep_alive (const http_parser *parser)
{
  if (parser->http_major > 0 && parser->http_minor > 0) {
    /* HTTP/1.1 */
    if (parser->flags & F_CONNECTION_CLOSE) {
      return 0;
    }
  } else {
    /* HTTP/1.0 or earlier */
    if (!(parser->flags & F_CONNECTION_KEEP_ALIVE)) {
      return 0;
    }
  }

  return !http_message_needs_eof(parser);
}


inline const char *
http_method_str (enum http_method m)
{
static const char *method_strings[] =
  {
#define CROW_XX(num, name, string) #string,
  CROW_HTTP_METHOD_MAP(CROW_XX)
#undef CROW_XX
  };
  return CROW_ELEM_AT(method_strings, m, "<unknown>");
}

inline const char *
http_status_str (enum http_status s)
{
  switch (s) {
#define CROW_XX(num, name, string) case HTTP_STATUS_##name: return #string;
    HTTP_STATUS_MAP(CROW_XX)
#undef CROW_XX
    default: return "<unknown>";
  }
}

inline void
http_parser_init (http_parser *parser, enum http_parser_type t)
{
  void *data = parser->data; /* preserve application data */
  memset(parser, 0, sizeof(*parser));
  parser->data = data;
  parser->type = t;
  parser->state = (t == HTTP_REQUEST ? s_start_req : (t == HTTP_RESPONSE ? s_start_res : s_start_req_or_res));
  parser->http_errno = HPE_OK;
}

inline void
http_parser_settings_init(http_parser_settings *settings)
{
  memset(settings, 0, sizeof(*settings));
}

inline const char *
http_errno_name(enum http_errno err) {
/* Map errno values to strings for human-readable output */
#define CROW_HTTP_STRERROR_GEN(n, s) { "HPE_" #n, s },
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

inline const char *
http_errno_description(enum http_errno err) {
/* Map errno values to strings for human-readable output */
#define CROW_HTTP_STRERROR_GEN(n, s) { "HPE_" #n, s },
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

inline static enum http_host_state
http_parse_host_char(enum http_host_state s, const char ch) {
  switch(s) {
    case s_http_userinfo:
    case s_http_userinfo_start:
      if (ch == '@') {
        return s_http_host_start;
      }

      if (CROW_IS_USERINFO_CHAR(ch)) {
        return s_http_userinfo;
      }
      break;

    case s_http_host_start:
      if (ch == '[') {
        return s_http_host_v6_start;
      }

      if (CROW_IS_HOST_CHAR(ch)) {
        return s_http_host;
      }

      break;

    case s_http_host:
      if (CROW_IS_HOST_CHAR(ch)) {
        return s_http_host;
      }

    /* fall through */
    case s_http_host_v6_end:
      if (ch == ':') {
        return s_http_host_port_start;
      }

      break;

    case s_http_host_v6:
      if (ch == ']') {
        return s_http_host_v6_end;
      }

    /* fall through */
    case s_http_host_v6_start:
      if (CROW_IS_HEX(ch) || ch == ':' || ch == '.') {
        return s_http_host_v6;
      }
      
      if (s == s_http_host_v6 && ch == '%') {
        return s_http_host_v6_zone_start;
      }
      break;

    case s_http_host_v6_zone:
      if (ch == ']') {
        return s_http_host_v6_end;
      }

    /* fall through */
    case s_http_host_v6_zone_start:
      /* RFC 6874 Zone ID consists of 1*( unreserved / pct-encoded) */
      if (CROW_IS_ALPHANUM(ch) || ch == '%' || ch == '.' || ch == '-' || ch == '_' ||
          ch == '~') {
        return s_http_host_v6_zone;
      }
      break;

    case s_http_host_port:
    case s_http_host_port_start:
      if (CROW_IS_NUM(ch)) {
        return s_http_host_port;
      }

      break;

    default:
      break;
  }
  return s_http_host_dead;
}

inline int
http_parse_host(const char * buf, struct http_parser_url *u, int found_at) {
  enum http_host_state s;

  const char *p;
  size_t buflen = u->field_data[UF_HOST].off + u->field_data[UF_HOST].len;
  
  assert(u->field_set & (1 << UF_HOST));
  
  u->field_data[UF_HOST].len = 0;

  s = found_at ? s_http_userinfo_start : s_http_host_start;

  for (p = buf + u->field_data[UF_HOST].off; p < buf + buflen; p++) {
    enum http_host_state new_s = http_parse_host_char(s, *p);

    if (new_s == s_http_host_dead) {
      return 1;
    }

    switch(new_s) {
      case s_http_host:
        if (s != s_http_host) {
          u->field_data[UF_HOST].off = (uint16_t)(p - buf);
        }
        u->field_data[UF_HOST].len++;
        break;

      case s_http_host_v6:
        if (s != s_http_host_v6) {
          u->field_data[UF_HOST].off = (uint16_t)(p - buf);
        }
        u->field_data[UF_HOST].len++;
        break;
        
      case s_http_host_v6_zone_start:
      case s_http_host_v6_zone:
        u->field_data[UF_HOST].len++;
        break;

      case s_http_host_port:
        if (s != s_http_host_port) {
          u->field_data[UF_PORT].off = (uint16_t)(p - buf);
          u->field_data[UF_PORT].len = 0;
          u->field_set |= (1 << UF_PORT);
        }
        u->field_data[UF_PORT].len++;
        break;

      case s_http_userinfo:
        if (s != s_http_userinfo) {
          u->field_data[UF_USERINFO].off = (uint16_t)(p - buf);
          u->field_data[UF_USERINFO].len = 0;
          u->field_set |= (1 << UF_USERINFO);
        }
        u->field_data[UF_USERINFO].len++;
        break;

      default:
        break;
    }
    s = new_s;
  }

  /* Make sure we don't end somewhere unexpected */
  switch (s) {
    case s_http_host_start:
    case s_http_host_v6_start:
    case s_http_host_v6:
    case s_http_host_v6_zone_start:
    case s_http_host_v6_zone:
    case s_http_host_port_start:
    case s_http_userinfo:
    case s_http_userinfo_start:
      return 1;
    default:
      break;
  }

  return 0;
}

inline void
http_parser_url_init(struct http_parser_url *u) {
  memset(u, 0, sizeof(*u));
}

inline int
http_parser_parse_url(const char *buf, size_t buflen, int is_connect,
                      struct http_parser_url *u)
{
  enum state s;
  const char *p;
  enum http_parser_url_fields uf, old_uf;
  int found_at = 0;
  
  if (buflen == 0) {
    return 1;
  }

  u->port = u->field_set = 0;
  s = is_connect ? s_req_server_start : s_req_spaces_before_url;
  old_uf = UF_MAX;

  for (p = buf; p < buf + buflen; p++) {
    s = parse_url_char(s, *p);

    /* Figure out the next field that we're operating on */
    switch (s) {
      case s_dead:
        return 1;

      /* Skip delimeters */
      case s_req_schema_slash:
      case s_req_schema_slash_slash:
      case s_req_server_start:
      case s_req_query_string_start:
      case s_req_fragment_start:
        continue;

      case s_req_schema:
        uf = UF_SCHEMA;
        break;

      case s_req_server_with_at:
        found_at = 1;
        break;

      /* fall through */
      case s_req_server:
        uf = UF_HOST;
        break;

      case s_req_path:
        uf = UF_PATH;
        break;

      case s_req_query_string:
        uf = UF_QUERY;
        break;

      case s_req_fragment:
        uf = UF_FRAGMENT;
        break;

      default:
        assert(!"Unexpected state");
        return 1;
    }

    /* Nothing's changed; soldier on */
    if (uf == old_uf) {
      u->field_data[uf].len++;
      continue;
    }

    u->field_data[uf].off = (uint16_t)(p - buf);
    u->field_data[uf].len = 1;

    u->field_set |= (1 << uf);
    old_uf = uf;
  }

  /* host must be present if there is a schema */
  /* parsing http:///toto will fail */
  if ((u->field_set & (1 << UF_SCHEMA)) &&
      (u->field_set & (1 << UF_HOST)) == 0) {
    return 1;
  }

  if (u->field_set & (1 << UF_HOST)) {
    if (http_parse_host(buf, u, found_at) != 0) {
      return 1;
    }
  }

  /* CONNECT requests can only contain "hostname:port" */
  if (is_connect && u->field_set != ((1 << UF_HOST)|(1 << UF_PORT))) {
    return 1;
  }

  if (u->field_set & (1 << UF_PORT)) {
    uint16_t off;
    uint16_t len;
    const char* p;
    const char* end;
    unsigned long v;

    off = u->field_data[UF_PORT].off;
    len = u->field_data[UF_PORT].len;
    end = buf + off + len;

    /* NOTE: The characters are already validated and are in the [0-9] range */
    assert((size_t)(off + len) <= buflen && "Port number overflow");
    v = 0;
    for (p = buf + off; p < end; p++) {
      v *= 10;
      v += *p - '0';

      /* Ports have a max value of 2^16 */
      if (v > 0xffff) {
        return 1;
      }
    }

    u->port = static_cast<uint16_t>(v);
  }

  return 0;
}

inline void
http_parser_pause(http_parser *parser, int paused) {
  /* Users should only be pausing/unpausing a parser that is not in an error
   * state. In non-debug builds, there's not much that we can do about this
   * other than ignore it.
   */
  if (CROW_HTTP_PARSER_ERRNO(parser) == HPE_OK ||
      CROW_HTTP_PARSER_ERRNO(parser) == HPE_PAUSED) {
    uint32_t nread = parser->nread; /* used by the CROW_SET_ERRNO macro */
    CROW_SET_ERRNO((paused) ? HPE_PAUSED : HPE_OK);
  } else {
    assert(0 && "Attempting to pause parser in error state");
  }
}

inline int
http_body_is_final(const struct http_parser *parser) {
    return parser->state == s_message_done;
}

inline unsigned long
http_parser_version(void) {
  return CROW_HTTP_PARSER_VERSION_MAJOR * 0x10000 |
         CROW_HTTP_PARSER_VERSION_MINOR * 0x00100 |
         CROW_HTTP_PARSER_VERSION_PATCH * 0x00001;
}

inline void
http_parser_set_max_header_size(uint32_t size) {
  max_header_size = size;
}

#undef CROW_HTTP_METHOD_MAP
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
#undef CROW_CR
#undef CROW_LF
#undef CROW_LOWER
#undef CROW_IS_ALPHA
#undef CROW_IS_NUM
#undef CROW_IS_ALPHANUM
#undef CROW_IS_HEX
#undef CROW_IS_MARK
#undef CROW_IS_USERINFO_CHAR
#undef CROW_TOKEN
#undef CROW_IS_URL_CHAR
#undef CROW_IS_HOST_CHAR
#undef CROW_start_state
#undef CROW_STRICT_CHECK
#undef CROW_NEW_MESSAGE

#ifdef __cplusplus
}
#endif
#endif
