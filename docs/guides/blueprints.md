<span class="tag">[:octicons-feed-tag-16: v1.0](https://github.com/CrowCpp/Crow/releases/v1.0)</span>


Crow supports Flask-style blueprints.<br>
A blueprint is a limited app. It cannot handle networking, but it can handle routes.<br>
Blueprints allow developers to compartmentalize their Crow applications, making them much more modular.<br><br>

In order for a blueprint to work, it has to be registered with a Crow app before the app is run. This can be done using `#!cpp app.register_blueprint(blueprint);`.<br><br>

Blueprints let you do the following:<br><br>

### Define Routes
You can define routes in a blueprint, similarly to how `#!cpp CROW_ROUTE(app, "/xyz")` works, you can use `#!cpp CROW_BP_ROUTE(blueprint, "/xyz")` to define a blueprint route.

### Define a Prefix
Blueprints can have a prefix assigned to them. This can be done when creating a new blueprint as in `#!cpp crow::blueprint bp("prefix");`. This prefix will be applied to all routes belonging to the blueprint, turning a route such as `/crow/rocks` into `/prefix/crow/rocks`.

!!! Warning

    Unlike routes, blueprint prefixes should contain no slashes.


### Use a custom Static directory
Blueprints let you define a custom static directory (relative to your working directory). This can be done by initializing a blueprint as `#!cpp crow::blueprint bp("prefix", "custom_static");`. This does not have an effect on `#!cpp set_static_file_info()`, it's only for when you want direct access to a file.

!!! note

    Currently changing which endpoint the blueprint uses isn't possible, so whatever you've set in `CROW_STATIC_ENDPOINT` (default is "static") will be used. Making your final route `/prefix/static/filename`.


### Use a custom Templates directory
Similar to static directories, You can set a custom templates directory (relative to your working directory). To do this you initialize the blueprint as `#!cpp crow::blueprint bp("prefix", "custom_static", "custom_templates");`. Any routes defined for the blueprint will use that directory when calling `#!cpp crow::mustache::load("filename.html")`.

!!! note

    If you want to define a custom templates directory without defining a custom static directory, you can pass the static directory as an empty string. Making your constructor `#!cpp crow::blueprint bp("prefix", "", "custom_templates");`.

### Define a custom Catchall route
You can define a custom catchall route for a blueprint by calling `#!cpp CROW_BP_CATCHALL_ROUTE(blueprint)`. This causes any requests with a URL starting with `/prefix` and no route found to call the blueprint's catchall route. If no catchall route is defined, Crow will default to either the parent blueprint or the app's catchall route.

### Register other Blueprints
Blueprints can also register other blueprints. This is done through `#!cpp blueprint.register_blueprint(blueprint_2);`. The child blueprint's routes become `/prefix/prefix_2/abc/xyz`.
