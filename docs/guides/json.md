Crow has built in support for JSON data.<br><br>

## type
The types of values that `rvalue and wvalue` can take are as follows:<br>

- `False`: from type `bool`.
- `True`: from type `bool`.
- `Number`
    - `Floating_point`: from type `double`.
    - `Signed_integer`: from type `int`.
    - `Unsigned_integer`: from type `unsigned int`.
- `String`: from type `std::string`.
- `List`: from type `std::vector`.
- `Object`: from type `crow::json::wvalue or crow::json::rvalue`.<br>
This last type means that `rvalue or wvalue` can have keys.

## rvalue
JSON read value, used for taking a JSON string and parsing it into `crow::json`.<br><br>

You can read individual items of the rvalue, but you cannot add items to it.<br>
To do that, you need to convert it to a `wvalue`, which can be done by simply writing `#!cpp crow::json::wvalue wval (rval);` (assuming `rval` is your `rvalue`).<br><br>

For more info on read values go [here](../reference/classcrow_1_1json_1_1rvalue.html).<br><br>

## wvalue
JSON write value, used for creating, editing and converting JSON to a string.<br><br>

!!! note

    setting a `wvalue` to object type can be done by simply assigning a value to whatever string key you like, something like `#!cpp wval["key1"] = val1;`. Keep in mind that val1 can be any of the above types.

A `wvalue` can be treated as an object or even a list (setting a value by using `json[3] = 32` for example). Please note that this will remove the data in the value if it isn't of List type.

!!! warning

    JSON does not allow floating point values like `NaN` or `INF`, Crow will output `null` instead of `NaN` or `INF` when converting `wvalue` to a string. (`{"Key": NaN}` becomes `{"Key": null}`)

<br><br>

Additionally, a `wvalue` can be initialized as an object using an initializer list, an example object would be `wvalue x = {{"a", 1}, {"b", 2}}`. Or as a list using `wvalue x = json::wvalue::list({1, 2, 3})`, lists can include any type that `wvalue` supports.<br><br>

An object type `wvalue` uses `std::unordered_map` by default, if you want to have your returned `wvalue` key value pairs be sorted (using `std::map`) you can add `#!cpp #define CROW_JSON_USE_MAP` to the top of your program.<br><br>
    
A JSON `wvalue` can be returned directly inside a route handler, this will cause the `content-type` header to automatically be set to `Application/json` and the JSON value will be converted to string and placed in the response body. For more information go to [Routes](../routes).<br><br>

For more info on write values go [here](../reference/classcrow_1_1json_1_1wvalue.html).

!!! note

    Crow's json exceptions can be disabled by using the `#!cpp #define CROW_JSON_NO_ERROR_CHECK` macro. This should increase the program speed with the drawback of having unexpected behavious when used incorrectly (e.g. by attempting to parse an invalid json object).
