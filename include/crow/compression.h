#ifdef CROW_ENABLE_COMPRESSION
#pragma once

#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE
#endif
#include <asio.hpp>
#include <memory>
#include <string>
#include <zlib.h>

// http://zlib.net/manual.html
namespace crow
{
    namespace compression
    {
        // Values used in the 'windowBits' parameter for deflateInit2.
        enum algorithm
        {
            // 15 is the default value for deflate
            DEFLATE = 15,
            // windowBits can also be greater than 15 for optional gzip encoding.
            // Add 16 to windowBits to write a simple gzip header and trailer around the compressed data instead of a zlib wrapper.
            GZIP = 15 | 16,
        };

        inline std::string compress_string(std::string const& str, algorithm algo)
        {
            std::string compressed_str;
            z_stream stream{};
            // Initialize with the default values
            if (::deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, algo, 8, Z_DEFAULT_STRATEGY) == Z_OK)
            {
                char buffer[8192];

                stream.avail_in = str.size();
                // zlib does not take a const pointer. The data is not altered.
                stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(str.c_str()));

                int code = Z_OK;
                do
                {
                    stream.avail_out = sizeof(buffer);
                    stream.next_out = reinterpret_cast<Bytef*>(&buffer[0]);

                    code = ::deflate(&stream, Z_FINISH);
                    // Successful and non-fatal error code returned by deflate when used with Z_FINISH flush
                    if (code == Z_OK || code == Z_STREAM_END)
                    {
                        std::copy(&buffer[0], &buffer[sizeof(buffer) - stream.avail_out], std::back_inserter(compressed_str));
                    }

                } while (code == Z_OK);

                if (code != Z_STREAM_END)
                    compressed_str.clear();

                ::deflateEnd(&stream);
            }

            return compressed_str;
        }

        inline std::string decompress_string(std::string const& deflated_string)
        {
            std::string inflated_string;
            Bytef tmp[8192];

            z_stream zstream{};
            zstream.avail_in = deflated_string.size();
            // Nasty const_cast but zlib won't alter its contents
            zstream.next_in = const_cast<Bytef*>(reinterpret_cast<Bytef const*>(deflated_string.c_str()));
            // Initialize with automatic header detection, for gzip support
            if (::inflateInit2(&zstream, MAX_WBITS | 32) == Z_OK)
            {
                do
                {
                    zstream.avail_out = sizeof(tmp);
                    zstream.next_out = &tmp[0];

                    auto ret = ::inflate(&zstream, Z_NO_FLUSH);
                    if (ret == Z_OK || ret == Z_STREAM_END)
                    {
                        std::copy(&tmp[0], &tmp[sizeof(tmp) - zstream.avail_out], std::back_inserter(inflated_string));
                    }
                    else
                    {
                        // Something went wrong with inflate; make sure we return an empty string
                        inflated_string.clear();
                        break;
                    }

                } while (zstream.avail_out == 0);

                // Free zlib's internal memory
                ::inflateEnd(&zstream);
            }

            return inflated_string;
        }

        class Compressor
        {
        public:
            Compressor(bool reset_before_compress, int window_bits, int level):
              reset_before_compress_(reset_before_compress), window_bits_(window_bits)
            {
                stream_ = std::make_unique<z_stream>();
                stream_->zalloc = 0;
                stream_->zfree = 0;
                stream_->opaque = 0;

                ::deflateInit2(stream_.get(),
                               level,
                               Z_DEFLATED,
                               -window_bits_,
                               8,
                               Z_DEFAULT_STRATEGY);
            }

            ~Compressor()
            {
                ::deflateEnd(stream_.get());
            }

            bool needs_reset() const
            {
                return reset_before_compress_;
            }

            int window_bits() const
            {
                return window_bits_;
            }

            std::string compress(const std::string& src)
            {
                if (reset_before_compress_)
                {
                    ::deflateReset(stream_.get());
                }

                stream_->next_in = reinterpret_cast<uint8_t*>(const_cast<char*>(src.c_str()));
                stream_->avail_in = src.size();

                constexpr const uint64_t bufferSize = 8192;
                asio::streambuf buffer;
                do
                {
                    asio::streambuf::mutable_buffers_type chunk = buffer.prepare(bufferSize);

                    uint8_t* next_out = asio::buffer_cast<uint8_t*>(chunk);

                    stream_->next_out = next_out;
                    stream_->avail_out = bufferSize;

                    ::deflate(stream_.get(), reset_before_compress_ ? Z_FINISH : Z_SYNC_FLUSH);

                    uint64_t outputSize = stream_->next_out - next_out;
                    buffer.commit(outputSize);
                } while (stream_->avail_out == 0);

                uint64_t buffer_size = buffer.size();
                if (!reset_before_compress_)
                {
                    buffer_size -= 4;
                }

                return std::string(asio::buffer_cast<const char*>(buffer.data()), buffer_size);
            }

        private:
            std::unique_ptr<z_stream> stream_;

            bool reset_before_compress_;
            int window_bits_;
        };

        class Decompressor
        {
        public:
            Decompressor(bool reset_before_decompress, int window_bits):
              reset_before_decompress_(reset_before_decompress), window_bits_(window_bits)
            {
                stream_ = std::make_unique<z_stream>();
                stream_->zalloc = 0;
                stream_->zfree = 0;
                stream_->opaque = 0;

                ::inflateInit2(stream_.get(), -window_bits_);
            }

            ~Decompressor()
            {
                inflateEnd(stream_.get());
            }

            bool needs_reset() const
            {
                return reset_before_decompress_;
            }

            int window_bits() const
            {
                return window_bits_;
            }

            std::string decompress(std::string src)
            {
                if (reset_before_decompress_)
                {
                    inflateReset(stream_.get());
                }

                src.push_back('\x00');
                src.push_back('\x00');
                src.push_back('\xff');
                src.push_back('\xff');

                stream_->next_in = reinterpret_cast<uint8_t*>(const_cast<char*>(src.c_str()));
                stream_->avail_in = src.size();

                constexpr const uint64_t bufferSize = 8192;
                asio::streambuf buffer;
                do
                {
                    asio::streambuf::mutable_buffers_type chunk = buffer.prepare(bufferSize);

                    uint8_t* next_out = asio::buffer_cast<uint8_t*>(chunk);

                    stream_->next_out = next_out;
                    stream_->avail_out = bufferSize;

                    ::inflate(stream_.get(), reset_before_decompress_ ? Z_FINISH : Z_SYNC_FLUSH);
                    buffer.commit(stream_->next_out - next_out);
                } while (stream_->avail_out == 0);

                return std::string(asio::buffer_cast<const char*>(buffer.data()), buffer.size());
            }

        private:
            std::unique_ptr<z_stream> stream_;

            bool reset_before_decompress_;
            int window_bits_;
        };
    } // namespace compression
} // namespace crow

#endif
