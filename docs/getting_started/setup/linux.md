Here's how you can install Crow on your favorite GNU/Linux distro.
## Getting Crow

### Requirements
 - C++ compiler with at least C++11 support.
 - Asio development headers (1.10.9 or later).
 - **(optional)** ZLib for HTTP Compression.
 - **(optional)** OpenSSL for HTTPS support.
 - **(optional)** CMake for building tests, examples, and/or installing Crow.
 - **(optional)** Python3 to build tests and/or examples.
!!! note

    Crow's CI uses `g++-9.4` and `clang-10.0` running on AMD64 (x86_64) and ARM64v8 architectures.


<br><br>

### Using a package Manager
You can install Crow on GNU/Linux as a pre-made package
=== "Debian/Ubuntu"

    Simply download Crow's `.deb` file from the [release section](https://github.com/CrowCpp/Crow/releases/latest) and Install it.

=== "Arch"

    Crow is available for Arch based distros through the AUR package `crow`.


<br><br>
### Release package
Crow provides an archive containing the framework and CMake files, just copy the `include` folder to `/usr/local/include` and `lib` folder to `/usr/local/lib`.<br><br>
You can also download the `crow_all.h` file and simply include that into your project.
<br><br>
### Installing from source
#### Using CMake
1. Download Crow's source code (Either through Github's UI or by using<br> `git clone https://github.com/CrowCpp/Crow.git`).
2. Run `mkdir build` inside of crow's source directory.
3. Navigate to the new "build" directory and run the following:<br>
`cmake .. -DCROW_BUILD_EXAMPLES=OFF -DCROW_BUILD_TESTS=OFF`
4. Run `make install`.

!!! note

    You can ignore `-DCROW_BUILD_EXAMPLES=OFF -DCROW_BUILD_TESTS=OFF` if you want to build the Examples and Unit Tests.

!!! note

    While building you can set:
	  the `CROW_ENABLE_SSL` variable to enable the support for https
	  the `CROW_ENABLE_COMPRESSION` variable to enable the support for http compression

!!! note

    You can uninstall Crow at a later time using `make uninstall`.

<br>
#### Manually
Crow can be installed manually on your Linux computer.
##### Multiple header files
=== "Project Only"

    Copy Crow's `include` directory to your project's `include` directory.

=== "System wide"

    Copy Crow's `include` directory to the `/usr/local/include` directory.

##### Single header (crow_all.h)
!!! warning

    `crow_all.h` is recommended only for small, possibly single source file projects, and ideally should not be installed on your system.

navigate to the `scripts` directory and run `./merge_all.py ../include crow_all.h`. This will generate a `crow_all.h` file that you can use in your projects.
!!! note

    You can also include or exclude middlewares from your `crow_all.h` by using `-i` or `-e` followed by the middleware header file names separated by a comma (e.g. `merge_all.py ../include crow_all.h -e cookie_parser` to exclude the cookie parser middleware).

## Compiling your project
### Using CMake
In order to get your CMake project to work with Crow, all you need are the following lines in your CMakeLists.txt:
```
find_package(Crow)
target_link_libraries(your_project PUBLIC Crow::Crow)
```
From there CMake should handle compiling and linking your project.
!!! note

    For optional features like HTTP Compression or HTTPS you can set

	  the `CROW_ENABLE_SSL` variable to enable the support for https
	  the `CROW_ENABLE_COMPRESSION` variable to enable the support for http compression

### Directly using a compiler
All you need to do is run the following command:
```
g++ main.cpp -lpthread
```
You can use arguments like `-DCROW_ENABLE_DEBUG`, `-DCROW_ENABLE_COMPRESSION -lz` for HTTP Compression, or `-DCROW_ENABLE_SSL -lssl` for HTTPS support, or even replace g++ with clang++.
