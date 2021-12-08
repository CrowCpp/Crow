Unit tests can be written in 2 ways for a Crow application.<br><br>

## The handler method
Crow allows users to handle requests that may not come from the network. This is done by calling the `handle(req, res)` method and providing a request and response objects. Which causes crow to identify and run the appropriate handler, returning the resulting response.

```cpp linenums="1"
  CROW_ROUTE(app, "/place")
  ([] { return "hi"; });

  app.validate();  //Used to make sure all the route handlers are in order.

  {
    request req;
    response res;

    req.url = "/place";

    app.handle(req, res); //res will contain a code of 200, and a response body of "hi"
  }
```

!!! note

    This method is the simpler of the two and is usually all you really need to test your routes.


!!! warning

    This method does not send any data, nor does it run any post handle code, so things like static file serving (as far as sending the actual data) or compression cannot be tested using this method.


## The client method
This method involves creating a simple [ASIO](https://think-async.com/Asio/) client that sends the request and receives the response. It is considerably more complex than the earlier method, but it is both more realistic and includes post handle operations.

```cpp linenums="1"
  static char buf[2048];
  SimpleApp app;
  CROW_ROUTE(app, "/")([] { return "A"; });

  auto _ = async(launch::async,[&] { app1.bindaddr("127.0.0.1").port(45451).run(); });
  app.wait_for_server_start();

  std::string sendmsg = "GET /\r\nContent-Length:3\r\nX-HeaderTest: 123\r\n\r\nA=B\r\n";
  asio::io_service is;
  {
    asio::ip::tcp::socket c(is);
    c.connect(asio::ip::tcp::endpoint(asio::ip::address::from_string("127.0.0.1"), 45451));

    c.send(asio::buffer(sendmsg));

    size_t recved = c.receive(asio::buffer(buf, 2048));
    CHECK('A' == buf[recved - 1]); //This is specific to catch2 testing library, but it should give a general idea of how to read the response.
  }

  app.stop(); //THIS MUST RUN
}

```
The first part is straightforward, create an app and add a route.<br>
The second part is launching the app asynchronously and waiting until it starts.<br>
The third is formulating our HTTP request string, the format is:
```
METHOD /
Content-Length:123
header1:value1
header2:value2

BODY

```
Next an `io_service` is created, then a TCP socket is created with the `io_service` and is connected to the application.<br>
Then send the HTTP request string through the socket inside a buffer, and read the result into the buffer in `line 1`.<br>
Finally check the result against the expected one.

!!! warning

    Be absolutely sure that the line `app.stop()` runs, whether the test fails or succeeds. Not running it WILL CAUSE OTHER TESTS TO FAIL AND THE TEST TO HANG UNTIL THE PROCESS IS TERMINATED.
