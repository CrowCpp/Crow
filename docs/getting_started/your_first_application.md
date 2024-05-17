This page shows how you can get started with a simple hello world application.

## 1. Include
Starting with an empty `main.cpp` file, first add `#!cpp #include "crow.h"` or `#!cpp #include "crow_all.h"` if you're using the single header file.

!!! note

    If you are using version v0.3, then you have to put `#!cpp #define CROW_MAIN` at the top of one and only one source file.

## 2. App declaration
Next Create a `main()` and declare a `#!cpp crow::SimpleApp` inside, your code should look like this
``` cpp
int main()
{
    crow::SimpleApp app;
}
```
The App (or SimpleApp) class organizes all the different parts of Crow and provides the developer (you) a simple interface to interact with these parts.
For more information, please go [here](../guides/app.md).

## 3. Adding routes
Once you have your app, the next step is to add routes (or endpoints). You can do so with the `CROW_ROUTE` macro.
``` cpp
CROW_ROUTE(app, "/")([](){
    return "Hello world";
});
```
For more details on routes, please go [here](../guides/routes.md).

## 4. Running the app
Once you're happy with how you defined all your routes, you're going to want to instruct Crow to run your app. This is done using the `run()` method.
``` cpp
app.port(18080).multithreaded().run();
```
Please note that the `port()` and `multithreaded()` methods aren't needed, though not using `port()` will cause the default port (`80`) to be used.<br>

## Putting it all together

Once you've followed all the steps above, your code should look similar to this

``` cpp title="main.cpp" linenums="1"
#include "crow.h"
//#include "crow_all.h"

int main()
{
    crow::SimpleApp app; //define your crow application

    //define your endpoint at the root directory
    CROW_ROUTE(app, "/")([](){
        return "Hello world";
    });

    //set the port, set the app to run on multiple threads, and run the app
    app.port(18080).multithreaded().run();
}
```

You then need to compile your code on your 
[Linux](setup/linux.md#compiling-your-project), 
[MacOS](setup/macos.md#compiling-using-a-compiler-directly), or 
[Windows](setup/windows.md#getting-and-compiling-crow) machine

After building your `.cpp` file and running the resulting executable, you should be able to access your endpoint at [http://localhost:18080](http://localhost:18080). Opening this URL in your browser will show a white screen with "Hello world" typed on it.
