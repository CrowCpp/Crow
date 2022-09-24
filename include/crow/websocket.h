#pragma once
#include <array>
#include "crow/logging.h"
#include "crow/socket_adaptors.h"
#include "crow/http_request.h"
#include "crow/TinySHA1.hpp"
#include "crow/utility.h"

namespace crow
{
    namespace websocket
    {
        enum class WebSocketReadState
        {
            MiniHeader,
            Len16,
            Len64,
            Mask,
            Payload,
        };

        /// A base class for websocket connection.
        struct connection
        {
            virtual void send_binary(std::string msg) = 0;
            virtual void send_text(std::string msg) = 0;
            virtual void send_ping(std::string msg) = 0;
            virtual void send_pong(std::string msg) = 0;
            virtual void close(std::string const& msg = "quit") = 0;
            virtual std::string get_remote_ip() = 0;
            virtual ~connection() = default;

            void userdata(void* u) { userdata_ = u; }
            void* userdata() { return userdata_; }

        private:
            void* userdata_;
        };

        // Modified version of the illustration in RFC6455 Section-5.2
        //
        //
        //  0               1               2               3               -byte
        //  0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 -bit
        // +-+-+-+-+-------+-+-------------+-------------------------------+
        // |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
        // |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
        // |N|V|V|V|       |S|             |   (if payload len==126/127)   |
        // | |1|2|3|       |K|             |                               |
        // +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
        // |     Extended payload length continued, if payload len == 127  |
        // + - - - - - - - - - - - - - - - +-------------------------------+
        // |                               |Masking-key, if MASK set to 1  |
        // +-------------------------------+-------------------------------+
        // | Masking-key (continued)       |          Payload Data         |
        // +-------------------------------- - - - - - - - - - - - - - - - +
        // :                     Payload Data continued ...                :
        // + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
        // |                     Payload Data continued ...                |
        // +---------------------------------------------------------------+
        //

        /// A websocket connection.

        template<typename Adaptor, typename Handler>
        class Connection : public connection
        {
        public:
            /// Constructor for a connection.

            ///
            /// Requires a request with an "Upgrade: websocket" header.<br>
            /// Automatically handles the handshake.
            Connection(const crow::request& req, Adaptor&& adaptor, Handler* handler, uint64_t max_payload,
                       std::function<void(crow::websocket::connection&)> open_handler,
                       std::function<void(crow::websocket::connection&, const std::string&, bool)> message_handler,
                       std::function<void(crow::websocket::connection&, const std::string&)> close_handler,
                       std::function<void(crow::websocket::connection&, const std::string&)> error_handler,
                       std::function<bool(const crow::request&, void**)> accept_handler):
              adaptor_(std::move(adaptor)),
              handler_(handler),
              max_payload_bytes_(max_payload),
              open_handler_(std::move(open_handler)),
              message_handler_(std::move(message_handler)),
              close_handler_(std::move(close_handler)),
              error_handler_(std::move(error_handler)),
              accept_handler_(std::move(accept_handler))
            {
                if (!utility::string_equals(req.get_header_value("upgrade"), "websocket"))
                {
                    adaptor.close();
                    handler_->remove_websocket(this);
                    delete this;
                    return;
                }

                if (accept_handler_)
                {
                    void* ud = nullptr;
                    if (!accept_handler_(req, &ud))
                    {
                        adaptor.close();
                        handler_->remove_websocket(this);
                        delete this;
                        return;
                    }
                    userdata(ud);
                }

                // Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
                // Sec-WebSocket-Version: 13
                std::string magic = req.get_header_value("Sec-WebSocket-Key") + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
                sha1::SHA1 s;
                s.processBytes(magic.data(), magic.size());
                uint8_t digest[20];
                s.getDigestBytes(digest);

                start(crow::utility::base64encode((unsigned char*)digest, 20));
            }

            /// Send data through the socket.
            template<typename CompletionHandler>
            void dispatch(CompletionHandler&& handler)
            {
                asio::dispatch(adaptor_.get_io_service(), std::forward<CompletionHandler>(handler));
            }

            /// Send data through the socket and return immediately.
            template<typename CompletionHandler>
            void post(CompletionHandler&& handler)
            {
                asio::post(adaptor_.get_io_service(), std::forward<CompletionHandler>(handler));
            }

            /// Send a "Ping" message.

            ///
            /// Usually invoked to check if the other point is still online.
            void send_ping(std::string msg) override
            {
                send_data(0x9, std::move(msg));
            }

            /// Send a "Pong" message.

            ///
            /// Usually automatically invoked as a response to a "Ping" message.
            void send_pong(std::string msg) override
            {
                send_data(0xA, std::move(msg));
            }

            /// Send a binary encoded message.
            void send_binary(std::string msg) override
            {
                send_data(0x2, std::move(msg));
            }

            /// Send a plaintext message.
            void send_text(std::string msg) override
            {
                send_data(0x1, std::move(msg));
            }

            /// Send a close signal.

            ///
            /// Sets a flag to destroy the object once the message is sent.
            void close(std::string const& msg) override
            {
                dispatch([this, msg]() mutable {
                    has_sent_close_ = true;
                    if (has_recv_close_ && !is_close_handler_called_)
                    {
                        is_close_handler_called_ = true;
                        if (close_handler_)
                            close_handler_(*this, msg);
                    }
                    auto header = build_header(0x8, msg.size());
                    write_buffers_.emplace_back(std::move(header));
                    write_buffers_.emplace_back(msg);
                    do_write();
                });
            }

            std::string get_remote_ip() override
            {
                return adaptor_.remote_endpoint().address().to_string();
            }

            void set_max_payload_size(uint64_t payload)
            {
                max_payload_bytes_ = payload;
            }

        protected:
            /// Generate the websocket headers using an opcode and the message size (in bytes).
            std::string build_header(int opcode, size_t size)
            {
                char buf[2 + 8] = "\x80\x00";
                buf[0] += opcode;
                if (size < 126)
                {
                    buf[1] += static_cast<char>(size);
                    return {buf, buf + 2};
                }
                else if (size < 0x10000)
                {
                    buf[1] += 126;
                    *(uint16_t*)(buf + 2) = htons(static_cast<uint16_t>(size));
                    return {buf, buf + 4};
                }
                else
                {
                    buf[1] += 127;
                    *reinterpret_cast<uint64_t*>(buf + 2) = ((1 == htonl(1)) ? static_cast<uint64_t>(size) : (static_cast<uint64_t>(htonl((size)&0xFFFFFFFF)) << 32) | htonl(static_cast<uint64_t>(size) >> 32));
                    return {buf, buf + 10};
                }
            }

            /// Send the HTTP upgrade response.

            ///
            /// Finishes the handshake process, then starts reading messages from the socket.
            void start(std::string&& hello)
            {
                static const std::string header =
                  "HTTP/1.1 101 Switching Protocols\r\n"
                  "Upgrade: websocket\r\n"
                  "Connection: Upgrade\r\n"
                  "Sec-WebSocket-Accept: ";
                write_buffers_.emplace_back(header);
                write_buffers_.emplace_back(std::move(hello));
                write_buffers_.emplace_back(crlf);
                write_buffers_.emplace_back(crlf);
                do_write();
                if (open_handler_)
                    open_handler_(*this);
                do_read();
            }

            /// Read a websocket message.

            ///
            /// Involves:<br>
            /// Handling headers (opcodes, size).<br>
            /// Unmasking the payload.<br>
            /// Reading the actual payload.<br>
            void do_read()
            {
                if (has_sent_close_ && has_recv_close_)
                {
                    close_connection_ = true;
                    adaptor_.shutdown_readwrite();
                    adaptor_.close();
                    check_destroy();
                    return;
                }

                is_reading = true;
                switch (state_)
                {
                    case WebSocketReadState::MiniHeader:
                    {
                        mini_header_ = 0;
                        //asio::async_read(adaptor_.socket(), asio::buffer(&mini_header_, 1),
                        adaptor_.socket().async_read_some(
                          asio::buffer(&mini_header_, 2),
                          [this](const asio::error_code& ec, std::size_t
#ifdef CROW_ENABLE_DEBUG
                                                               bytes_transferred
#endif
                          )

                          {
                              is_reading = false;
                              mini_header_ = ntohs(mini_header_);
#ifdef CROW_ENABLE_DEBUG

                              if (!ec && bytes_transferred != 2)
                              {
                                  throw std::runtime_error("WebSocket:MiniHeader:async_read fail:asio bug?");
                              }
#endif

                              if (!ec)
                              {
                                  if ((mini_header_ & 0x80) == 0x80)
                                      has_mask_ = true;
                                  else //if the websocket specification is enforced and the message isn't masked, terminate the connection
                                  {
#ifndef CROW_ENFORCE_WS_SPEC
                                      has_mask_ = false;
#else
                                      close_connection_ = true;
                                      adaptor_.shutdown_readwrite();
                                      adaptor_.close();
                                      if (error_handler_)
                                          error_handler_(*this, "Client connection not masked.");
                                      check_destroy();
#endif
                                  }

                                  if ((mini_header_ & 0x7f) == 127)
                                  {
                                      state_ = WebSocketReadState::Len64;
                                  }
                                  else if ((mini_header_ & 0x7f) == 126)
                                  {
                                      state_ = WebSocketReadState::Len16;
                                  }
                                  else
                                  {
                                      remaining_length_ = mini_header_ & 0x7f;
                                      state_ = WebSocketReadState::Mask;
                                  }
                                  do_read();
                              }
                              else
                              {
                                  close_connection_ = true;
                                  adaptor_.shutdown_readwrite();
                                  adaptor_.close();
                                  if (error_handler_)
                                      error_handler_(*this, ec.message());
                                  check_destroy();
                              }
                          });
                    }
                    break;
                    case WebSocketReadState::Len16:
                    {
                        remaining_length_ = 0;
                        remaining_length16_ = 0;
                        asio::async_read(
                          adaptor_.socket(), asio::buffer(&remaining_length16_, 2),
                          [this](const asio::error_code& ec, std::size_t
#ifdef CROW_ENABLE_DEBUG
                                                               bytes_transferred
#endif
                          ) {
                              is_reading = false;
                              remaining_length16_ = ntohs(remaining_length16_);
                              remaining_length_ = remaining_length16_;
#ifdef CROW_ENABLE_DEBUG
                              if (!ec && bytes_transferred != 2)
                              {
                                  throw std::runtime_error("WebSocket:Len16:async_read fail:asio bug?");
                              }
#endif

                              if (!ec)
                              {
                                  state_ = WebSocketReadState::Mask;
                                  do_read();
                              }
                              else
                              {
                                  close_connection_ = true;
                                  adaptor_.shutdown_readwrite();
                                  adaptor_.close();
                                  if (error_handler_)
                                      error_handler_(*this, ec.message());
                                  check_destroy();
                              }
                          });
                    }
                    break;
                    case WebSocketReadState::Len64:
                    {
                        asio::async_read(
                          adaptor_.socket(), asio::buffer(&remaining_length_, 8),
                          [this](const asio::error_code& ec, std::size_t
#ifdef CROW_ENABLE_DEBUG
                                                               bytes_transferred
#endif
                          ) {
                              is_reading = false;
                              remaining_length_ = ((1 == ntohl(1)) ? (remaining_length_) : (static_cast<uint64_t>(ntohl((remaining_length_)&0xFFFFFFFF)) << 32) | ntohl((remaining_length_) >> 32));
#ifdef CROW_ENABLE_DEBUG
                              if (!ec && bytes_transferred != 8)
                              {
                                  throw std::runtime_error("WebSocket:Len16:async_read fail:asio bug?");
                              }
#endif

                              if (!ec)
                              {
                                  state_ = WebSocketReadState::Mask;
                                  do_read();
                              }
                              else
                              {
                                  close_connection_ = true;
                                  adaptor_.shutdown_readwrite();
                                  adaptor_.close();
                                  if (error_handler_)
                                      error_handler_(*this, ec.message());
                                  check_destroy();
                              }
                          });
                    }
                    break;
                    case WebSocketReadState::Mask:
                        if (remaining_length_ > max_payload_bytes_)
                        {
                            close_connection_ = true;
                            adaptor_.close();
                            if (error_handler_)
                                error_handler_(*this, "Message length exceeds maximum payload.");
                            check_destroy();
                        }
                        else if (has_mask_)
                        {
                            asio::async_read(
                              adaptor_.socket(), asio::buffer((char*)&mask_, 4),
                              [this](const asio::error_code& ec, std::size_t
#ifdef CROW_ENABLE_DEBUG
                                                                   bytes_transferred
#endif
                              ) {
                                  is_reading = false;
#ifdef CROW_ENABLE_DEBUG
                                  if (!ec && bytes_transferred != 4)
                                  {
                                      throw std::runtime_error("WebSocket:Mask:async_read fail:asio bug?");
                                  }
#endif

                                  if (!ec)
                                  {
                                      state_ = WebSocketReadState::Payload;
                                      do_read();
                                  }
                                  else
                                  {
                                      close_connection_ = true;
                                      if (error_handler_)
                                          error_handler_(*this, ec.message());
                                      adaptor_.shutdown_readwrite();
                                      adaptor_.close();
                                      check_destroy();
                                  }
                              });
                        }
                        else
                        {
                            state_ = WebSocketReadState::Payload;
                            do_read();
                        }
                        break;
                    case WebSocketReadState::Payload:
                    {
                        auto to_read = static_cast<std::uint64_t>(buffer_.size());
                        if (remaining_length_ < to_read)
                            to_read = remaining_length_;
                        adaptor_.socket().async_read_some(
                          asio::buffer(buffer_, static_cast<std::size_t>(to_read)),
                          [this](const asio::error_code& ec, std::size_t bytes_transferred) {
                              is_reading = false;

                              if (!ec)
                              {
                                  fragment_.insert(fragment_.end(), buffer_.begin(), buffer_.begin() + bytes_transferred);
                                  remaining_length_ -= bytes_transferred;
                                  if (remaining_length_ == 0)
                                  {
                                      if (handle_fragment())
                                      {
                                          state_ = WebSocketReadState::MiniHeader;
                                          do_read();
                                      }
                                  }
                                  else
                                      do_read();
                              }
                              else
                              {
                                  close_connection_ = true;
                                  if (error_handler_)
                                      error_handler_(*this, ec.message());
                                  adaptor_.shutdown_readwrite();
                                  adaptor_.close();
                                  check_destroy();
                              }
                          });
                    }
                    break;
                }
            }

            /// Check if the FIN bit is set.
            bool is_FIN()
            {
                return mini_header_ & 0x8000;
            }

            /// Extract the opcode from the header.
            int opcode()
            {
                return (mini_header_ & 0x0f00) >> 8;
            }

            /// Process the payload fragment.

            ///
            /// Unmasks the fragment, checks the opcode, merges fragments into 1 message body, and calls the appropriate handler.
            bool handle_fragment()
            {
                if (has_mask_)
                {
                    for (decltype(fragment_.length()) i = 0; i < fragment_.length(); i++)
                    {
                        fragment_[i] ^= ((char*)&mask_)[i % 4];
                    }
                }
                switch (opcode())
                {
                    case 0: // Continuation
                    {
                        message_ += fragment_;
                        if (is_FIN())
                        {
                            if (message_handler_)
                                message_handler_(*this, message_, is_binary_);
                            message_.clear();
                        }
                    }
                    break;
                    case 1: // Text
                    {
                        is_binary_ = false;
                        message_ += fragment_;
                        if (is_FIN())
                        {
                            if (message_handler_)
                                message_handler_(*this, message_, is_binary_);
                            message_.clear();
                        }
                    }
                    break;
                    case 2: // Binary
                    {
                        is_binary_ = true;
                        message_ += fragment_;
                        if (is_FIN())
                        {
                            if (message_handler_)
                                message_handler_(*this, message_, is_binary_);
                            message_.clear();
                        }
                    }
                    break;
                    case 0x8: // Close
                    {
                        has_recv_close_ = true;
                        if (!has_sent_close_)
                        {
                            close(fragment_);
                        }
                        else
                        {
                            adaptor_.shutdown_readwrite();
                            adaptor_.close();
                            close_connection_ = true;
                            if (!is_close_handler_called_)
                            {
                                if (close_handler_)
                                    close_handler_(*this, fragment_);
                                is_close_handler_called_ = true;
                            }
                            check_destroy();
                            return false;
                        }
                    }
                    break;
                    case 0x9: // Ping
                    {
                        send_pong(fragment_);
                    }
                    break;
                    case 0xA: // Pong
                    {
                        pong_received_ = true;
                    }
                    break;
                }

                fragment_.clear();
                return true;
            }

            /// Send the buffers' data through the socket.

            ///
            /// Also destroys the object if the Close flag is set.
            void do_write()
            {
                if (sending_buffers_.empty())
                {
                    sending_buffers_.swap(write_buffers_);
                    std::vector<asio::const_buffer> buffers;
                    buffers.reserve(sending_buffers_.size());
                    for (auto& s : sending_buffers_)
                    {
                        buffers.emplace_back(asio::buffer(s));
                    }
                    asio::async_write(
                      adaptor_.socket(), buffers,
                      [&](const asio::error_code& ec, std::size_t /*bytes_transferred*/) {
                          sending_buffers_.clear();
                          if (!ec && !close_connection_)
                          {
                              if (!write_buffers_.empty())
                                  do_write();
                              if (has_sent_close_)
                                  close_connection_ = true;
                          }
                          else
                          {
                              close_connection_ = true;
                              check_destroy();
                          }
                      });
                }
            }

            /// Destroy the Connection.
            void check_destroy()
            {
                //if (has_sent_close_ && has_recv_close_)
                if (!is_close_handler_called_)
                    if (close_handler_)
                        close_handler_(*this, "uncleanly");
                handler_->remove_websocket(this);
                if (sending_buffers_.empty() && !is_reading)
                    delete this;
            }


            struct SendMessageType
            {
                std::string payload;
                Connection* self;
                int opcode;

                void operator()()
                {
                    self->send_data_impl(this);
                }
            };

            void send_data_impl(SendMessageType* s)
            {
                auto header = build_header(s->opcode, s->payload.size());
                write_buffers_.emplace_back(std::move(header));
                write_buffers_.emplace_back(std::move(s->payload));
                do_write();
            }

            void send_data(int opcode, std::string&& msg)
            {
                SendMessageType event_arg{
                  std::move(msg),
                  this,
                  opcode};

                post(std::move(event_arg));
            }

        private:
            Adaptor adaptor_;
            Handler* handler_;

            std::vector<std::string> sending_buffers_;
            std::vector<std::string> write_buffers_;

            std::array<char, 4096> buffer_;
            bool is_binary_;
            std::string message_;
            std::string fragment_;
            WebSocketReadState state_{WebSocketReadState::MiniHeader};
            uint16_t remaining_length16_{0};
            uint64_t remaining_length_{0};
            uint64_t max_payload_bytes_{UINT64_MAX};
            bool close_connection_{false};
            bool is_reading{false};
            bool has_mask_{false};
            uint32_t mask_;
            uint16_t mini_header_;
            bool has_sent_close_{false};
            bool has_recv_close_{false};
            bool error_occurred_{false};
            bool pong_received_{false};
            bool is_close_handler_called_{false};

            std::function<void(crow::websocket::connection&)> open_handler_;
            std::function<void(crow::websocket::connection&, const std::string&, bool)> message_handler_;
            std::function<void(crow::websocket::connection&, const std::string&)> close_handler_;
            std::function<void(crow::websocket::connection&, const std::string&)> error_handler_;
            std::function<bool(const crow::request&, void**)> accept_handler_;
        };
    } // namespace websocket
} // namespace crow
