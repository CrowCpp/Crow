This page explains how to set Crow up for use with your project (***For versions 0.3+4 and lower***).


##Requirements
 - C++ compiler with C++11 support.
    - Crow's CI uses g++-9.3 and clang-7.0 running on AMD64 (x86_64) and ARM64v8
 - boost library (1.64 or later).
 - **(optional)** ZLib for HTTP Compression.
 - **(optional)** CMake and Python3 to build tests and/or examples.
    
<br><br>

##Installing Requirements
!!! note

    The Linux requirements are for developing and compiling a Crow application. Running a built application requires the actual libraries rather than just the development headers.

###Ubuntu
`sudo apt-get install build-essential libboost-all-dev`

###Arch
`sudo pacman -S python boost boost-libs`

###Non Debian based GNU/Linux
Use your package manager to install the following:
 - GCC and G++ (or Clang and Clang++)
 - Boost Development headers (sometimes part of the Boost package itself)


###OSX
`brew install boost`

###Windows
Microsoft Visual Studio 2019 (older versions not tested)

##Downloading
Either run `git clone https://github.com/crowcpp/crow.git` or download `crow_all.h` from the releases section. You can also download a zip of the project on Github.

##Includes folder
1. Copy the `/includes` folder to your project's root folder.
2. Add `#!cpp #include "path/to/includes/crow.h"` to your `.cpp` file.
3. For any middlewares, add `#!cpp #include "path/to/includes/middlewares/some_middleware.h"`.
<br><br>

##Single header file
If you've downloaded `crow_all.h`, you can skip to step **4**.

1. Make sure you have python 3 installed. 
2. Open a terminal (or `cmd.exe`) instance in `/path/to/crow/scripts`.
3. Run `python merge_all.py ../include crow_all.h` (replace `/` with `\` if you're on Windows).
4. Copy the `crow_all.h` file to where you put your libraries (if you don't know where this is, you can put it anywhere).
5. Add `#!cpp #include "path/to/crow_all.h"` to your `.cpp` file.
<br><br>
**Note**: All middlewares are included with the merged header file, if you would like to include or exclude middlewares use the `-e` or `-i` arguments.
<br><br>

##building via CLI
To build a crow Project, do the following:

###GCC (G++)
 - Release: `g++ main.cpp -lpthread -lboost_system`.
 - Debug: `g++ main.cpp -ggdb -lpthread -lboost_system -DCROW_ENABLE_DEBUG`.
 - SSL: `g++ main.cpp -lssl -lcrypto -lpthread -lboost_system -DCROW_ENABLE_SSL`.

###Clang
 - Release: `clang++ main.cpp -lpthread -lboost_system`.
 - Debug: `clang++ main.cpp -g -lpthread -lboost_system -DCROW_ENABLE_DEBUG`.
 - SSL: `clang++ main.cpp -lssl -lcrypto -lpthread -lboost_system -DCROW_ENABLE_SSL`.

###Microsoft Visual Studio 2019
The following guide will use `example_with_all.cpp` as the Crow application for demonstration purposes.

1. Generate `crow_all.h` following [Single header file](#single-header-file).
2. `git clone https://github.com/microsoft/vcpkg.git`
3. `.\vcpkg\bootstrap-vcpkg.bat`
4. `.\vcpkg\vcpkg integrate install`
5. Create empty Visual Studio project.
6. In solution explorer, right click the name of your project then click `Open Folder in File Explorer`.
7. Copy `crow_all.h`, `example_with_all.cpp`, `vcpkg.json` to opened folder.
8. Add `crow_all.h` to `Header Files` and `example_with_all.cpp` to `Source Files`.
9. In solution explorer, right click the name of your project then click `Properties`.
10. Under `vcpkg`, set `Use Vcpkg Manifest` to `Yes` and `Additional Options` to `--feature-flags="versions"`.
11. Set `Debug/Release` and `x64/x86`. 
12. Run.


##building via CMake
Add the following to your `CMakeLists.txt`:
``` cmake linenums="1"
find_package(Threads)
find_package(ZLIB)
find_package(OpenSSL)

if(OPENSSL_FOUND)
	include_directories(${OPENSSL_INCLUDE_DIR})
endif()

if (NOT CMAKE_BUILD_TYPE)
	message(STATUS "No build type selected, default to Release")
	set(CMAKE_BUILD_TYPE "Release")
endif()

if (MSVC)
	set(Boost_USE_STATIC_LIBS "On")
	find_package( Boost 1.70 COMPONENTS system thread regex REQUIRED )
else()
	find_package( Boost 1.70 COMPONENTS system thread REQUIRED )
endif()

include_directories(${Boost_INCLUDE_DIR})

set(PROJECT_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)

include_directories("${PROJECT_INCLUDE_DIR}")
```
!!!note

    The last 2 lines are unnecessary if you're using `crow_all.h`.

##Building Crow tests and examples
Out-of-source build with CMake is recommended.

```
mkdir build
cd build
cmake ..
make
```
Running CMake will create `crow_all.h` file and place it in the build directory.<br>

You can run tests with following command:
```
ctest -V
```

##Installing Crow

if you wish to use Crow globally without copying `crow_all.h` in your projects, you can install Crow on your machine with the procedure below.

```
mkdir build
cd build
cmake ..
make install
```
`make install` will copy `crow_all.h` automatically in your `/usr/local/include` thus making it available globally for use.<br>
