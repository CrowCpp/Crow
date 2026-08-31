#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <system_error>

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "crow/http_request.h"
#include "crow/utility.h"

namespace crow
{
    /// Destination for request body bytes as they arrive from the parser.
    struct BodySink
    {
        virtual ~BodySink() = default;
        virtual bool write(const char* data, std::size_t length) = 0;
        virtual bool finish() = 0;
    };

    using BodySinkFactory = std::function<std::unique_ptr<BodySink>(const request&)>;

    /// File-backed sink used by `.body_file()`. Owns the descriptor until `finish()`.
    class FileBodySink : public BodySink
    {
    public:
        static std::unique_ptr<FileBodySink> create(const std::string& directory)
        {
            std::error_code ec;
            std::filesystem::path dir = directory.empty() ? std::filesystem::temp_directory_path(ec) :
                                                            std::filesystem::path(directory);
            if (ec)
                return nullptr;

            for (int attempt = 0; attempt < 128; ++attempt)
            {
                std::string name;
                try
                {
                    name = "crow-body-" + utility::random_alphanum(16);
                }
                catch (...)
                {
                    return nullptr;
                }
                const auto path = dir / name;
#ifndef _WIN32
                const int fd = ::open(path.string().c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
                if (fd >= 0)
                    return std::unique_ptr<FileBodySink>(new FileBodySink(path.string(), fd));
                if (errno != EEXIST)
                    return nullptr;
#else
                SECURITY_ATTRIBUTES sa{};
                sa.nLength = sizeof(sa);
                sa.bInheritHandle = FALSE;
                HANDLE handle = CreateFileA(path.string().c_str(), GENERIC_WRITE, 0, &sa, CREATE_NEW,
                                            FILE_ATTRIBUTE_NORMAL, nullptr);
                if (handle != INVALID_HANDLE_VALUE)
                    return std::unique_ptr<FileBodySink>(new FileBodySink(path.string(), handle));
                if (GetLastError() != ERROR_FILE_EXISTS)
                    return nullptr;
#endif
            }
            return nullptr;
        }

        FileBodySink(const FileBodySink&) = delete;
        FileBodySink& operator=(const FileBodySink&) = delete;

        ~FileBodySink() override
        {
            close_handle();
            if (!persist_ && !path_.empty())
            {
                std::error_code ec;
                std::filesystem::remove(path_, ec);
            }
        }

        bool write(const char* data, std::size_t length) override
        {
#ifndef _WIN32
            if (fd_ < 0)
                return false;
            std::size_t written = 0;
            while (written < length)
            {
                const ssize_t n = ::write(fd_, data + written, length - written);
                if (n < 0)
                {
                    if (errno == EINTR)
                        continue;
                    return false;
                }
                if (n == 0)
                    return false;
                written += static_cast<std::size_t>(n);
            }
            return true;
#else
            if (handle_ == INVALID_HANDLE_VALUE)
                return false;
            std::size_t written = 0;
            while (written < length)
            {
                DWORD n = 0;
                const DWORD chunk = static_cast<DWORD>(
                  std::min<std::size_t>(length - written, static_cast<std::size_t>(MAXDWORD)));
                if (!WriteFile(handle_, data + written, chunk, &n, nullptr) || n == 0)
                    return false;
                written += n;
            }
            return true;
#endif
        }

        bool finish() override
        {
            return close_handle();
        }

        const std::string& path() const
        {
            return path_;
        }

        void persist()
        {
            persist_ = true;
        }

    private:
#ifndef _WIN32
        FileBodySink(std::string path, int fd):
          path_(std::move(path)), fd_(fd)
        {}
#else
        FileBodySink(std::string path, HANDLE handle):
          path_(std::move(path)), handle_(handle)
        {}
#endif

        bool close_handle()
        {
#ifndef _WIN32
            if (fd_ < 0)
                return true;
            const int fd = fd_;
            fd_ = -1;
            return ::close(fd) == 0;
#else
            if (handle_ == INVALID_HANDLE_VALUE)
                return true;
            const HANDLE handle = handle_;
            handle_ = INVALID_HANDLE_VALUE;
            return CloseHandle(handle) != 0;
#endif
        }

        std::string path_;
        bool persist_{false};
#ifndef _WIN32
        int fd_{-1};
#else
        HANDLE handle_{INVALID_HANDLE_VALUE};
#endif
    };
} // namespace crow
