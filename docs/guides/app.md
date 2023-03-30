A Crow app defines an interface to allow the developer access to all the different parts of the framework, without having to manually deal with each one.<br><br>
An app allows access to the HTTP server (for handling connections), router (for handling URLs and requests), Middlewares (for extending Crow), among many others.<br><br>

Crow has 2 different app types:

## SimpleApp
Has no middlewares.

## App&lt;m1, m2, ...&gt;
Has middlewares.

## Using the app
To use a Crow app, simply define `#!cpp crow::SimpleApp` or `#!cpp crow::App<m1, m2 ...>` if you're using middlewares.<br>
The methods of an app can be chained. That means that you can configure and run your app in the same code line.
``` cpp
app.bindaddr("192.168.1.2").port(443).ssl_file("certfile.crt","keyfile.key").multithreaded().run();
```
Or if you like your code neat
``` cpp
app.bindaddr("192.168.1.2")
.port(443)
.ssl_file("certfile.crt","keyfile.key")
.multithreaded()
.run();
```

!!! note

    The `run()` method is blocking. To run a Crow app asynchronously `run_async()` should be used instead.
    
!!! warning

    When using `run_async()`, make sure to use a variable to save the function's output (such as `#!cpp auto _a = app.run_async()`). Otherwise the app will run synchronously.

<br><br>

For more info on middlewares, check out [this page](../middleware).<br><br>
For more info on what functions are available to a Crow app, go [here](../../reference/classcrow_1_1_crow.html).
