# Building Crow & the Hello World Example

This guide covers building Crow and the bundled Hello World example on
**macOS**, **Linux**, and **Windows**.

Crow is a **header-only** C++ web framework — there is nothing to compile in
the library itself.  You only compile your own application files.

---

## Repository layout

```
Crow/
├── include/crow/      ← Crow header-only library
├── third_party/       ← Vendored Asio (standalone, no Boost required)
│   ├── asio.hpp
│   └── asio/
├── examples/
│   └── hello_world/
│       ├── main.cpp
│       └── CMakeLists.txt
└── docs/
    └── building.md    ← this file
```

---

## Prerequisites

| Tool | Minimum version | Notes |
|------|----------------|-------|
| C++ compiler | C++17 capable | GCC 9+, Clang 10+, MSVC 2019+ |
| CMake | 3.15 | Required for the CMake build path |

Asio is **already vendored** in `third_party/` — you do **not** need to install
it separately.

---

## macOS

### Install prerequisites

```bash
# Xcode command-line tools (includes clang & make)
xcode-select --install

# CMake via Homebrew (https://brew.sh)
brew install cmake
```

### Build with CMake (recommended)

```bash
git clone https://github.com/RndU53r/Crow.git
cd Crow/examples/hello_world

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .

./hello_world          # server starts on http://localhost:8080
```

### Build without CMake (single compiler command)

```bash
cd Crow
c++ -std=c++17 -O2 \
    -I include \
    -I third_party \
    -DASIO_STANDALONE \
    examples/hello_world/main.cpp \
    -o hello_world \
    -lpthread

./hello_world
```

---

## Linux

### Install prerequisites

**Debian / Ubuntu**

```bash
sudo apt update
sudo apt install -y build-essential cmake
```

**Fedora / RHEL / CentOS**

```bash
sudo dnf install -y gcc-c++ cmake make
```

**Arch Linux**

```bash
sudo pacman -S base-devel cmake
```

### Build with CMake

```bash
git clone https://github.com/RndU53r/Crow.git
cd Crow/examples/hello_world

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .

./hello_world
```

### Build without CMake

```bash
cd Crow
g++ -std=c++17 -O2 \
    -I include \
    -I third_party \
    -DASIO_STANDALONE \
    examples/hello_world/main.cpp \
    -o hello_world \
    -lpthread

./hello_world
```

---

## Windows

### Install prerequisites

1. **Visual Studio 2019 or 2022** with the *Desktop development with C++*
   workload, **or** install the standalone
   [Build Tools for Visual Studio](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022).

2. **CMake** — download from <https://cmake.org/download/> and tick
   *Add CMake to the system PATH*.

3. **Git for Windows** — <https://git-scm.com/download/win>

Open a **Developer Command Prompt** or **Developer PowerShell** for all
commands below.

### Build with CMake (recommended)

```powershell
git clone https://github.com/RndU53r/Crow.git
cd Crow\examples\hello_world

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

.\build\Release\hello_world.exe
```

### Build without CMake (cl.exe)

```bat
cd Crow
cl /std:c++17 /O2 /EHsc ^
   /I include ^
   /I third_party ^
   /DASIO_STANDALONE ^
   examples\hello_world\main.cpp ^
   /Fe:hello_world.exe

hello_world.exe
```

> **Note for MinGW / MSYS2 users**  
> Install `mingw-w64-x86_64-gcc` and `mingw-w64-x86_64-cmake` via `pacman`,
> then use the same command as the Linux g++ example above (replacing
> `-lpthread` with `-lws2_32 -lwsock32`).

---

## Verifying the server

Once `hello_world` is running, open a browser or run:

```bash
curl http://localhost:8080/
# → Hello, World!
```

---

## Writing your own Crow application

1. Add Crow's `include/` and `third_party/` directories to your include path.
2. Define `ASIO_STANDALONE` before including any Crow header
   (the `CMakeLists.txt` in `examples/hello_world/` shows how to do this with
   `target_compile_definitions`).
3. Link against pthreads on POSIX (`-lpthread`) or against `ws2_32`/`wsock32`
   on Windows.

Minimal application skeleton:

```cpp
#include "crow.h"

int main()
{
    crow::SimpleApp app;

    CROW_ROUTE(app, "/")([]() {
        return "Hello, World!";
    });

    app.port(8080).multithreaded().run();
}
```

For full API documentation see <https://crowcpp.org>.

---

## Optional features

| CMake option | Default | Description |
|---|---|---|
| `CROW_USE_BOOST` | OFF | Use Boost.Asio instead of standalone Asio |
| `CROW_ENABLE_SSL` | OFF | HTTPS support (requires OpenSSL) |
| `CROW_ENABLE_COMPRESSION` | OFF | Gzip/deflate (requires zlib) |
| `CROW_BUILD_EXAMPLES` | ON | Build all bundled examples |
| `CROW_BUILD_TESTS` | ON | Build the test suite |

Example enabling SSL:

```bash
cmake .. -DCROW_ENABLE_SSL=ON -DOPENSSL_ROOT_DIR=$(brew --prefix openssl)
```
