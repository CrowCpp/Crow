Websockets are a way of connecting a client and a server without the request response nature of HTTP.<br><br>

## Routes
To create a websocket in Crow, you need a websocket route.<br>
A websocket route differs from a normal route quite a bit. It uses a slightly altered `CROW_WEBSOCKET_ROUTE(app, "/url")` macro, which is then followed by a series of methods (with handlers inside) for each event. These are (sorted by order of execution):


- `#!cpp onaccept([&](const crow::request& req, void** userdata){handler code goes here})`
- `#!cpp onopen([&](crow::websocket::connection& conn){handler code goes here})`
- `#!cpp onmessage([&](crow::websocket::connection& conn, const std::string& message, bool is_binary){handler code goes here})`
- `#!cpp onerror([&](crow::websocket::connection& conn, const std::string& error_message){handler code goes here})`
- `#!cpp onclose([&](crow::websocket::connection& conn, const std::string& reason, uint16_t with_status_code ){handler code goes here})`

!!! note

    `onaccept` must return a boolean. In case `false` is returned, the connection is shut down, deleted, and no further communication is done.

!!! Warning

    By default, Crow allows clients to send unmasked websocket messages. This is useful for debugging, but goes against the protocol specifications. Production Crow applications should enforce the protocol by adding `#!cpp #define CROW_ENFORCE_WS_SPEC` to their source code.

These event methods and their handlers can be chained. The full route should look similar to this:
```cpp
CROW_WEBSOCKET_ROUTE(app, "/ws")
    .onopen([&](crow::websocket::connection& conn){
            do_something();
            })
    .onclose([&](crow::websocket::connection& conn, const std::string& reason, uint16_t){
            do_something();
            })
    .onmessage([&](crow::websocket::connection& /*conn*/, const std::string& data, bool is_binary){
                if (is_binary)
                    do_something(data);
                else
                    do_something_else(data);
            });
```

## Maximum payload size
<span class="tag">[:octicons-feed-tag-16: master](https://github.com/CrowCpp/Crow)</span>

The maximum payload size that a connection accepts can be adjusted either globally by using `#!cpp app.websocket_max_payload(<value in bytes>)` or per route by using `#!cpp CROW_WEBSOCKET_ROUTE(app, "/url").max_payload(<value in bytes>)`. In case a message was sent that exceeded the limit. The connection would be shut down and `onerror` would be triggered.

!!! note

    By default, this limit is disabled. To disable the global setting in specific routes, you only need to call `#!cpp CROW_WEBSOCKET_ROUTE(app, "/url").max_payload(UINT64_MAX)`.

## Subprotocols
<span class="tag">[:octicons-feed-tag-16: master](https://github.com/CrowCpp/Crow)</span>

Specifies the possible subprotocols that are available for the client. If specified, the first match with the client's requested subprotocols will be returned in the "Sec-WebSocket-Protocol" header of the handshake response. Otherwise, the connection will be closed. If no subprotocol are specified on both the client and the server side, the connection process will continue normally. It can be specified by using `#!cpp CROW_WEBSOCKET_ROUTE(app, "/url").subprotocols(<values>)`.


For more info about websocket routes go [here](../reference/classcrow_1_1_web_socket_rule.html).

For more info about websocket connections go [here](../reference/classcrow_1_1websocket_1_1_connection.html).
