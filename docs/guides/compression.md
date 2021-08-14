**Introduced in: `v0.3`**<br><br>
Crow supports Zlib compression using Gzip or Deflate algorithms.

## HTTP Compression
HTTP compression is by default disabled in crow. Do the following to enable it: <br>
1. Add `#!cpp #define CROW_ENABLE_COMPRESSION` to the top of your main source file.
2. Call `#!cpp use_compression(crow::compression::algorithm)` on your crow app.
3. When compiling your application, make sure that ZLIB is included as a dependency. Either through `-lz` argument or `find_package(ZLIB)` in CMake.

!!! note

    step 3 is not needed for MSVC since `vcpckg.json` already includes zlib as a dependency by default

For the compression algorim you can use `crow::compression::algorithm::DEFLATE` or `crow::compression::algorithm::GZIP`.<br>
And now your HTTP responses will be compressed.

## Websocket Compression
Crow currently does not support Websocket compression.<br>
Feel free to discuss the subject with us on Github if you're feeling adventurous and want to try to implement it. We appreciate all the help.
