<span class="tag">[:octicons-feed-tag-16: v0.2](https://github.com/CrowCpp/Crow/releases/0.2)</span>


A static file is any file that resides in the server's storage.

Crow supports returning static files as responses either implicitly or explicitly.

## Implicit
Crow implicitly returns any static files placed in a `static` directory and any subdirectories, as long as the user calls the endpoint `/static/path/to/file`.<br><br>
The static folder or endpoint can be changed by defining the macros `CROW_STATIC_DIRECTORY "alternative_directory/"` and `CROW_STATIC_ENDPOINT "/alternative_endpoint/<path>"`.<br>
static directory changes the directory in the server's file system, while the endpoint changes the URL that the client needs to access.

## Explicit
You can directly return a static file by using the `crow::response` method `#!cpp response.set_static_file_info("path/to/file");`. The path is relative to the working directory.

!!! Warning

    The path to the file is sanitized by default. it should be fine for most circumstances but if you know what you're doing and need the sanitizer off you can use `#!cpp response.set_static_file_info_unsafe("path/to/file")` instead.

!!! note

    Crow sets the `content-type` header automatically based on the file's extension, if an extension is unavailable or undefined,  Crow uses `text/plain`, if you'd like to explicitly set a `content-type`, use `#!cpp response.set_header("content-type", "mime/type");` **AFTER** calling `set_static_file_info`.

!!! note

    Please keep in mind that using the `set_static_file_info` method means any data already in your response body is ignored and not sent to the client.
