Templating is when you return an HTML page with custom data. You can probably tell why that's useful.<br><br>

Crow supports [mustache](http://mustache.github.io) for templates through its own implementation `crow::mustache`.<br><br>

!!! note

    Currently Crow's Mustache implementation is not linked to the Crow application, meaning if an executable has more than one Crow application they'll be sharing any variables or classes relating to template loading and compiling.

## Components of mustache

There are 2 components of a mustache template implementation:

- Page
- Context

### Page
The HTML page (including the mustache tags). It is usually loaded into `crow::mustache::template_t`. It needs to be placed in the *templates directory* which should be directly inside the current working directory of the crow executable.<br><br>

The templates directory is usually called `templates`, but can be adjusted per Route (via `crow::mustache::set_base("new_templates_directory")`), per [Blueprint](blueprints.md), or globally (via `crow::mustache::set_global_base("new_templates_directory"")`).<br><br>

For more information on how to formulate a template, see [this mustache manual](http://mustache.github.io/mustache.5.html).

### Context
A JSON object containing the tags as keys and their values. `crow::mustache::context` is actually a [crow::json::wvalue](json.md#wvalue).<br><br>

!!! note

    `crow::mustache::context` can take a C++ lambda as a value. The lambda needs to take a string as an argument and return a string, such as `#!cpp ctx[lmd] = [&](std::string){return "Hello World";};`.

!!! note

    The string returned by the lamdba can contain mustache tags, Crow will parse it as any normal template string.

## Returning a template
To return a mustache template, you need to load a page using `#!cpp auto page = crow::mustache::load("path/to/template.html");`. Or just simply load a string using `#!cpp auto page = crow::mustache::compile("my mustache {{value}}");`. Keep in mind that the path is relative to the templates directory.

!!! note

    You can also use `#!cpp auto page = crow::mustache::load_text("path/to/template.html");` if you want to load a template without mustache processing.

!!! Warning

    The path to the template is sanitized by default. it should be fine for most circumstances but if you know what you're doing and need the sanitizer off you can use `#!cpp crow::mustache::load_unsafe()` instead.

<br>
You also need to set up the context by using `#!cpp crow::mustache::context ctx;`. Then you need to assign the keys and values, this can be done the same way you assign values to a JSON write value (`ctx["key"] = value;`).<br>
With your context and page ready, just `#!cpp return page.render(ctx);`. This will use the context data to return a filled template.<br>
Alternatively you could just render the page without a context using `#!cpp return page.render();`.

!!! note

    `#!cpp page.render();` returns a crow::returnable class in order to set the `Content-Type` header. to get a simple string, use `#!cpp page.render_string()` instead.
