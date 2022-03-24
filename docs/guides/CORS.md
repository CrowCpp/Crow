Crow Allows a developer to set CORS policies by using the `CORSHandler` middleware.<br><br>

This middleware can be added to the app simply by defining a `#!cpp crow::App<crow::CORSHandler>`. This will use the default CORS rules globally<br><br>

The CORS rules can be modified by first getting the middleware via `#!cpp auto& cors = app.get_middleware<crow::CORSHandler>();`. The rules can be set per URL prefix using `prefix()`, per blueprint using `blueprint()`, or globally via `global()`. These will return a `CORSRules` object which contains the actual rules for the prefix, blueprint, or application. For more details go [here](../../reference/structcrow_1_1_c_o_r_s_handler.html).

`CORSRules` can  be modified using the methods `origin()`, `methods()`, `headers()`, `max_age()`, `allow_credentials()`, or `ignore()`. For more details on these methods and what default values they take go [here](../../reference/structcrow_1_1_c_o_r_s_rules.html).
