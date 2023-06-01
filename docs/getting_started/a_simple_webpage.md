Hello World is a good start, but what if you want something a bit more fancy.. Something like an HTML document saying "Hello World". If that's what you want, follow along:

## Basic Webpage
Let's start our webpage with.. well.. a webpage. But before we create a webpage we need to place it somewhere Crow recognizes, for now this directory is going to be called `templates`, but we can [change it later](../../guides/templating/#page).

Once our `templates` folder is created, we can create our HTML document inside it, let's call it `fancypage.html`.

After that we can just place something simple inside it like:
``` html title="templates/fancypage.html"
<!DOCTYPE html>
<html>
  <body>
  <p>Hello World!</p>
  </body>
</html>
```
<br>
Now that we have our HTML page ready, let's take our Hello World example from earlier:
``` cpp linenums="1"
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
<br>

And now let's modify it so that it returns our cool page:
``` cpp title="/main.cpp" linenums="1" hl_lines="10 11"
#include "crow.h"
//#include "crow_all.h"

int main()
{
    crow::SimpleApp app;

    //define your endpoint at the root directory
    CROW_ROUTE(app, "/")([](){
        auto page = crow::mustache::load_text("fancypage.html");
        return page;
    });

    app.port(18080).multithreaded().run();
}
```

Your project should look something something like:
```
./
 |-templates/
 |          |-fancypage.html
 |
 |-main.cpp
 |-crow_all.h
```
or
```
./
 |-templates/
 |          |-fancypage.html
 |
 |-crow/
 |     |-include/...
 |     |-crow.h
 |-main.cpp
```


Once the code is done compiling, if we call `http://localhost:18080/` we get our Hello World in an HTML document rather than just plain text.

!!! note

    Compilation instructions are available for [Linux](../setup/linux#compiling-your-project), [MacOS](../setup/macos#compiling-using-a-compiler-directly), and [Windows](../setup/windows#getting-and-compiling-crow)


## Template Webpage with a variable
But we can make things even more exciting, we can greet a user by their name instead!!

Let's start with our webpage, and modify it with a little bit of [mustache](../../guides/templating) syntax:
``` html title="templates/fancypage.html" hl_lines="4"
<!DOCTYPE html>
<html>
  <body>
  <p>Hello {{person}}!</p> <!--(1)-->
  </body>
</html>
```

1. `{{}}` in mustache define a simple variable

<br>
Now let's modify our C++ code to use the variable we just added to our webpage (or template):
``` cpp title="/main.cpp" linenums="1" hl_lines="9-12"
#include "crow.h"
//#include "crow_all.h"

int main()
{
    crow::SimpleApp app;

    //define your endpoint at the root directory
    CROW_ROUTE(app, "/<string>")([](std::string name){ // (1)
        auto page = crow::mustache::load("fancypage.html"); // (2)
        crow::mustache::context ctx ({{"person", name}}); // (3)
        return page.render(ctx); //(4)
    });

    app.port(18080).multithreaded().run();
}
```

1. We are adding a `string` variable to the URL and a counterpart (`std::string name`) to our route - this can be anything the user wants.
2. We are using `load()` instead of `load_text()` since we have an actual variable now.
3. We are creating a new [context](../../guides/templating/#context) containing the `person` variable from our template and the `name` we got from the URL.
4. We are using `render(ctx)` to apply our context to the template.

Now (after compiling the code and running the executable a second time) calling `http://localhost:18080/Bob` should return a webpage containing "Hello Bob!". **We did it!**

For more details on templates and HTML pages in Crow please go [here](../../guides/templating/)
