A query string is the part of the URL that comes after a `?` character, it is usually formatted as `key=value&otherkey=othervalue`.
<br><br>

Crow supports query strings through `crow::request::url_params`. The object is of type `crow::query_string` and can has the following functions:<br>

## get(name)
Returns the value (as char*) based on the given key (or name). Returns `nullptr` if the key is not found.
## pop(name)
<span class="tag">[:octicons-feed-tag-16: v0.3](https://github.com/CrowCpp/Crow/releases/v0.3)</span>


Works the same as `get`, but removes the returned value.
!!! note

    `crow::request::url_params` is a const value, therefore for pop (also pop_list and pop_dict) to work, a copy needs to be made.

## get_list(name)
A URL can be `http://example.com?key[]=value1&key[]=value2&key[]=value3`. Using `get_list("key")` on such a URL returns an `std::vector<std::string>` containing `[value1, value2, value3]`.<br><br>

`#!cpp get_list("key", false)` can be used to parse `http://example.com?key=value1&key=value2&key=value3`
## pop_list(name)
<span class="tag">[:octicons-feed-tag-16: v0.3](https://github.com/CrowCpp/Crow/releases/v0.3)</span>


Works the same as `get_list` but removes all instances of values having the given key (`use_brackets` is also available here).
## get_dict(name)
Returns an `std::unordered_map<std::string, std::string>` from a query string such as `?key[sub_key1]=value1&key[sub_key2]=value2&key[sub_key3]=value3`.<br>
The key in the map is what's in the brackets (`sub_key1` for example), and the value being what's after the `=` sign (`value1`). The name passed to the function is not part of the returned value.
## pop_dict(name)
<span class="tag">[:octicons-feed-tag-16: v0.3](https://github.com/CrowCpp/Crow/releases/v0.3)</span>


Works the same as `get_dict` but removing the values from the query string.
!!! warning

    if your query string contains both a list and dictionary with the same key, it is best to use `pop_list` before either `get_dict` or `pop_dict`, since a map cannot contain more than one value per key, each item in the list will override the previous and only the last will remain with an empty key.

<br><br>
For more information take a look [here](../reference/classcrow_1_1query__string.html).
