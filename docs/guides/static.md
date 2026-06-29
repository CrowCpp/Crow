<span class="tag">[:octicons-feed-tag-16: v0.2](https://github.com/CrowCpp/Crow/releases/0.2)</span>

A static file is any file that resides in the server's storage.

Crow supports returning static files as responses either implicitly or explicitly.

## Implicit
Crow implicitly returns any static files placed in a `static` directory and any subdirectories, as long as the user calls the endpoint `/static/path/to/file`.<br><br>
The static folder or endpoint can be changed by defining the macros :
```cpp
#define CROW_STATIC_DIRECTORY "alternative_directory/"
#define CROW_STATIC_ENDPOINT "/alternative_endpoint/<path>"
```
`CROW_STATIC_DIRECTORY` changes the directory in the server's file system, while `CROW_STATIC_ENDPOINT` changes the URL that the client needs to access.

## Explicit
You can return a static file by using the `CROW_STATIC_FILE(<app>, <EndPoint>, <FilePath>)` macro.</br>
Or by using `app.static_file(<EndPoint>, <FilePath>)` function of Crow::App.</br>
The path is relative to the working directory.

However for a dedicated case, you can directly return a static file by using the `crow::response` and set the file path and mime-type by `#!cpp response.set_static_file_info(<FilePath>,<mime-type>)`.

### Example

```cpp
auto app = crow::SimpleApp(); // or crow::App()

// by Macro
CROW_STATIC_FILE(app, "/"          , "home.html");
CROW_STATIC_FILE(app, "/home.html" , "home.html");

// by Function
app.static_file("/favicon.ico", "images/home.png");
app.static_file("/style.css", "style.css");

// Explicit route to add more information
CROW_ROUTE(app, "/customDataType.xyz")
([](const crow::request&, crow::response& res) {
    res.set_static_file_info(
                "data/customType.xyz",
                "application/octet-stream"
                );
    res.end();
});
```


## Notes

!!! Warning

    The path to the file is sanitized by default. it should be fine for most circumstances but if you know what you're doing and need the sanitizer off you can use `#!cpp response.set_static_file_info_unsafe("path/to/file")` instead.

!!! note

    Crow sets the `content-type` header automatically based on the file's extension, if an extension is unavailable or undefined,  Crow uses `text/plain`, if you'd like to explicitly set a `content-type`, use `#!cpp response.set_header("content-type", "mime/type");` **AFTER** calling `set_static_file_info`.

!!! note

    Please keep in mind that using the `set_static_file_info` method means any data already in your response body is ignored and not sent to the client.
