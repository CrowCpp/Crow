<span class="tag">[:octicons-feed-tag-16: v0.3](https://github.com/CrowCpp/Crow/releases/v0.3)</span>


Crow supports Zlib compression using Gzip or Deflate algorithms.

## HTTP Compression
HTTP compression is by default disabled in crow. Do the following to enable it: <br>
- Define `CROW_ENABLE_COMPRESSION` in your compiler definitions (`g++ main.cpp -DCROW_ENABLE_COMPRESSION` for example) or `set(CROW_FEATURES compression)` in `CMakeLists.txt`.
- Call `#!cpp use_compression(crow::compression::algorithm)` on your Crow app.
- When compiling your application, make sure that ZLIB is included as a dependency. Either through `-lz` compiler argument or `find_package(ZLIB)` in CMake.

!!! note

    3<sup>rd</sup> point is not needed for MSVC or CMake projects using `Crow::Crow` since `vcpkg.json` and Crow's target already include zlib as a dependency.

For the compression algorithm you can use `crow::compression::algorithm::DEFLATE` or `crow::compression::algorithm::GZIP`.<br>
And now your HTTP responses will be compressed.

## Websocket Compression
Crow currently does not support Websocket compression.<br>
Feel free to discuss the subject with us on GitHub if you're feeling adventurous and want to try to implement it. We appreciate all the help.
