#pragma once

// Opt-in: not included by "crow.h" or "crow/parser.h". A build that has no
// writable filesystem, or that disables RTTI (`-fno-rtti`, `-DASIO_NO_TYPEID`),
// can use `crow::BodySink` directly and never pull this header in.

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "crow/utility.h"
#endif

#include "crow/body_sink.h"
#include "crow/http_request.h"

namespace crow
{
    /// File-backed `BodySink`. Give it to `.body_sink(...)` via `factory()`; read
    /// the result back from the handler with `from()`.
    ///
    /// The descriptor is created `O_CREAT|O_EXCL|O_WRONLY` (Windows: `CREATE_NEW`),
    /// mode 0600, and kept open until `finish()`. The file is unlinked when the
    /// sink is destroyed, unless `keep()` was called; since `request::body_sink`
    /// is a `shared_ptr`, that is when the last copy of the request goes away.
    class FileBodySink : public BodySink
    {
    public:
        /// A `BodySinkFactory` that writes each request's body to a uniquely
        /// named file under `directory`. `directory` is resolved to an absolute
        /// path once, here, and must already exist; empty uses the system
        /// temporary directory. Throws `std::filesystem::filesystem_error` if it
        /// cannot be resolved — call this while setting up the route, not per
        /// request, so a bad directory fails loudly at startup.
        static BodySinkFactory factory(std::string directory = {})
        {
            auto resolved = std::make_shared<std::string>(resolve_directory(directory));
            return [resolved](const request&) -> std::unique_ptr<BodySink> {
                return create(*resolved);
            };
        }

        /// The `FileBodySink` behind `req.body_sink`, or `nullptr` if the route
        /// used a different sink (or none).
        static FileBodySink* from(const request& req)
        {
            return dynamic_cast<FileBodySink*>(req.body_sink.get());
        }

        FileBodySink(const FileBodySink&) = delete;
        FileBodySink& operator=(const FileBodySink&) = delete;

        ~FileBodySink() override
        {
            close_handle();
            if (!keep_ && !path_.empty())
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

        /// The path of the file, valid once the factory has created it.
        const std::string& path() const
        {
            return path_;
        }

        /// Do not delete the file when this sink is destroyed (i.e. when the
        /// last copy of the request that reached the handler goes away).
        void keep()
        {
            keep_ = true;
        }

    private:
        static std::string resolve_directory(const std::string& directory)
        {
            std::error_code ec;
            std::filesystem::path dir = directory.empty() ?
                                           std::filesystem::temp_directory_path(ec) :
                                           std::filesystem::absolute(directory, ec);
            if (ec)
                throw std::filesystem::filesystem_error("body_file directory unavailable", directory, ec);
            if (!std::filesystem::is_directory(dir, ec) || ec)
                throw std::filesystem::filesystem_error(
                  "body_file directory does not exist", dir,
                  std::make_error_code(std::errc::no_such_file_or_directory));
            return dir.string();
        }

        static std::unique_ptr<FileBodySink> create(const std::string& directory)
        {
#ifndef _WIN32
            std::string tmpl = (std::filesystem::path(directory) / "crow-body-XXXXXX").string();
            std::vector<char> buf(tmpl.begin(), tmpl.end());
            buf.push_back('\0');
            const int fd = ::mkostemp(buf.data(), O_CLOEXEC);
            if (fd < 0)
                throw std::system_error(errno, std::generic_category(), "crow::FileBodySink: mkostemp");
            return std::unique_ptr<FileBodySink>(new FileBodySink(std::string(buf.data()), fd));
#else
            const std::filesystem::path dir(directory);
            for (int attempt = 0; attempt < 128; ++attempt)
            {
                const std::string name = "crow-body-" + utility::random_alphanum(16);
                const auto path = dir / name;
                SECURITY_ATTRIBUTES sa{};
                sa.nLength = sizeof(sa);
                sa.bInheritHandle = FALSE;
                HANDLE handle = CreateFileW(path.wstring().c_str(), GENERIC_WRITE, 0, &sa, CREATE_NEW,
                                            FILE_ATTRIBUTE_NORMAL, nullptr);
                if (handle != INVALID_HANDLE_VALUE)
                    return std::unique_ptr<FileBodySink>(new FileBodySink(path.string(), handle));
                if (GetLastError() != ERROR_FILE_EXISTS)
                    throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                            "crow::FileBodySink: CreateFileW");
            }
            throw std::runtime_error("crow::FileBodySink: could not find a unique file name");
#endif
        }

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
        bool keep_{false};
#ifndef _WIN32
        int fd_{-1};
#else
        HANDLE handle_{INVALID_HANDLE_VALUE};
#endif
    };
} // namespace crow
