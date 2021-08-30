#pragma once
#include <string>
#include <unordered_map>
#include <ios>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

#include "crow/http_request.h"
#include "crow/ci_map.h"
#include "crow/socket_adaptors.h"
#include "crow/logging.h"
#include "crow/mime_types.h"
#include "crow/returnable.h"


namespace crow
{
    template <typename Adaptor, typename Handler, typename ... Middlewares>
    class Connection;

    /// HTTP response
    struct response
    {
        template <typename Adaptor, typename Handler, typename ... Middlewares>
        friend class crow::Connection;

        int code{200}; ///< The Status code for the response.
        std::string body; ///< The actual payload containing the response data.
        ci_map headers; ///< HTTP headers.

#ifdef CROW_ENABLE_COMPRESSION
        bool compressed = true; ///< If compression is enabled and this is false, the individual response will not be compressed.
#endif
        bool is_head_response = false; ///< Whether this is a response to a HEAD request.
        bool manual_length_header = false; ///< Whether Crow should automatically add a "Content-Length" header.

        /// Set the value of an existing header in the response.
        void set_header(std::string key, std::string value)
        {
            headers.erase(key);
            headers.emplace(std::move(key), std::move(value));
        }

        /// Add a new header to the response.
        void add_header(std::string key, std::string value)
        {
            headers.emplace(std::move(key), std::move(value));
        }

        const std::string& get_header_value(const std::string& key)
        {
            return crow::get_header_value(headers, key);
        }


        response() {}
        explicit response(int code) : code(code) {}
        response(std::string body) : body(std::move(body)) {}
        response(int code, std::string body) : code(code), body(std::move(body)) {}
        response (returnable&& value)
        {
            body = value.dump();
            set_header("Content-Type",value.content_type);
        }
        response (returnable& value)
        {
            body = value.dump();
            set_header("Content-Type",value.content_type);
        }
        response (int code, returnable& value) : code(code)
        {
            body = value.dump();
            set_header("Content-Type",value.content_type);
        }

        response(response&& r)
        {
            *this = std::move(r);
        }
        
        response(std::string contentType, std::string body) : body(std::move(body))
        {
            set_header("Content-Type",mime_types[contentType]);
        }

        response(int code, std::string contentType, std::string body): code(code),body(std::move(body))
        {
            set_header("Content-Type",mime_types[contentType]);
        }

        response& operator = (const response& r) = delete;

        response& operator = (response&& r) noexcept
        {
            body = std::move(r.body);
            code = r.code;
            headers = std::move(r.headers);
            completed_ = r.completed_;
            file_info = std::move(r.file_info);
            return *this;
        }

        /// Check if the response has completed (whether response.end() has been called)
        bool is_completed() const noexcept
        {
            return completed_;
        }

        void clear()
        {
            body.clear();
            code = 200;
            headers.clear();
            completed_ = false;
            file_info = static_file_info{};
        }

        /// Return a "Temporary Redirect" response.
        ///
        /// Location can either be a route or a full URL.
        void redirect(const std::string& location)
        {
            code = 307;
            set_header("Location", location);
        }

        /// Return a "Permanent Redirect" response.
        ///
        /// Location can either be a route or a full URL.
        void redirect_perm(const std::string& location)
        {
            code = 308;
            set_header("Location", location);
        }

        /// Return a "Found (Moved Temporarily)" response.
        ///
        /// Location can either be a route or a full URL.
        void moved(const std::string& location)
        {
            code = 302;
            set_header("Location", location);
        }

        /// Return a "Moved Permanently" response.
        ///
        /// Location can either be a route or a full URL.
        void moved_perm(const std::string& location)
        {
            code = 301;
            set_header("Location", location);
        }

        void write(const std::string& body_part)
        {
            body += body_part;
        }

        /// Set the response completion flag and call the handler (to send the response).
        void end()
        {
            if (!completed_)
            {
                completed_ = true;
                if (is_head_response)
                {
                    set_header("Content-Length", std::to_string(body.size()));
                    body = "";
                    manual_length_header = true;
                }
                if (complete_request_handler_)
                {
                    complete_request_handler_();
                }
            }
        }

        /// Same as end() except it adds a body part right before ending.
        void end(const std::string& body_part)
        {
            body += body_part;
            end();
        }

        /// Check if the connection is still alive (usually by checking the socket status).
        bool is_alive()
        {
            return is_alive_helper_ && is_alive_helper_();
        }

        /// Check whether the response has a static file defined.
        bool is_static_type()
        {
            return file_info.path.size();
        }

        /// This constains metadata (coming from the `stat` command) related to any static files associated with this response.

        /// Either a static file or a string body can be returned as 1 response.
        ///
        struct static_file_info{
            std::string path = "";
            struct stat statbuf;
            int statResult;
        };

        ///Return a static file as the response body
        void set_static_file_info(std::string path){
            file_info.path = path;
            file_info.statResult = stat(file_info.path.c_str(), &file_info.statbuf);
#ifdef CROW_ENABLE_COMPRESSION
            compressed = false;
#endif
            if (file_info.statResult == 0)
            {
                std::size_t last_dot = path.find_last_of(".");
                std::string extension = path.substr(last_dot+1);
                std::string mimeType = "";
                code = 200;
                this->add_header("Content-length", std::to_string(file_info.statbuf.st_size));

                if (extension != ""){
                    mimeType = mime_types[extension];
                    if (mimeType != "")
                        this-> add_header("Content-Type", mimeType);
                    else
                        this-> add_header("content-Type", "text/plain");
                }
            }
            else
            {
                code = 404;
                this->end();
            }
        }

        /// Stream a static file.
        template<typename Adaptor>
        void do_stream_file(Adaptor& adaptor)
        {
            if (file_info.statResult == 0)
            {
                std::ifstream is(file_info.path.c_str(), std::ios::in | std::ios::binary);
                write_streamed(is, adaptor);
            }
        }

        /// Stream the response body (send the body in chunks).
        template<typename Adaptor>
        void do_stream_body(Adaptor& adaptor)
        {
            if (body.length() > 0)
            {
                write_streamed_string(body, adaptor);
            }
        }

        private:
            bool completed_{};
            std::function<void()> complete_request_handler_;
            std::function<bool()> is_alive_helper_;
            static_file_info file_info;

            template<typename Stream, typename Adaptor>
            void write_streamed(Stream& is, Adaptor& adaptor)
            {
                char buf[16384];
                while (is.read(buf, sizeof(buf)).gcount() > 0)
                {
                    std::vector<asio::const_buffer> buffers;
                    buffers.push_back(boost::asio::buffer(buf));
                    write_buffer_list(buffers, adaptor);
                }
            }

            //THIS METHOD DOES MODIFY THE BODY, AS IN IT EMPTIES IT
            template<typename Adaptor>
            void write_streamed_string(std::string& is, Adaptor& adaptor)
            {
                std::string buf;
                std::vector<asio::const_buffer> buffers;

                while (is.length() > 16384)
                {
                    //buf.reserve(16385);
                    buf = is.substr(0, 16384);
                    is = is.substr(16384);
                    push_and_write(buffers, buf, adaptor);
                }
                //Collect whatever is left (less than 16KB) and send it down the socket
                //buf.reserve(is.length());
                buf = is;
                is.clear();
                push_and_write(buffers, buf, adaptor);
            }

            template<typename Adaptor>
            inline void push_and_write(std::vector<asio::const_buffer>& buffers, std::string& buf, Adaptor& adaptor)
            {
                buffers.clear();
                buffers.push_back(boost::asio::buffer(buf));
                write_buffer_list(buffers, adaptor);
            }

            template<typename Adaptor>
            inline void write_buffer_list(std::vector<asio::const_buffer>& buffers, Adaptor& adaptor)
            {
                boost::asio::write(adaptor.socket(), buffers, [this](std::error_code ec, std::size_t)
                {
                    if (!ec)
                    {
                        return false;
                    }
                    else
                    {
                        CROW_LOG_ERROR << ec << " - happened while sending buffers";
                        this->end();
                        return true;
                    }
                });
            }

    };
}
