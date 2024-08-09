<span class="tag">[:octicons-feed-tag-16: v0.2](https://github.com/CrowCpp/Crow/releases/0.2)</span>


Multipart is a way of forming HTTP requests or responses to contain multiple distinct parts.<br>

Such an approach allows a request to contain multiple different pieces of data with potentially conflicting data types in a single response payload.<br>
It is typically used either in HTML forms, or when uploading multiple files.<br><br>

## How multipart messages work
The structure of a multipart request is typically consistent of:<br>

- A Header: Typically `multipart/form-data;boundary=<boundary>`, This defines the HTTP message as being multipart, as well as defining the separator used to distinguish the different parts.<br>
- 1 or more parts:
    - `--<boundary>`
    - Part header: typically `content-disposition: mime/type; name="<fieldname>"` (`mime/type` should be replaced with the actual mime-type), can also contain a `filename` property (separated from the rest by a `;` and structured similarly to the `name` property)
    - Value
- `--<boundary>--`<br><br>

## Multipart messages in Crow
Crow supports multipart requests and responses though `crow::multipart::message` and `crow::multipart::message_view`, where `crow::multipart::message` owns the contents of the message and `crow::multipart::message_view` stores views into its parts.<br>
A message can be created either by defining the headers, boundary, and individual parts and using them to create the message. or simply by reading a `crow::request`.<br><br>

Once a multipart message has been made, the individual parts can be accessed throughout `msg.parts`, `parts` is an `std::vector`.<br><br>

<span class="tag">[:octicons-feed-tag-16: v1.0](https://github.com/CrowCpp/Crow/releases/v1.0)</span>


Part headers are organized in a similar way to request and response headers, and can be retrieved via `crow::multipart::get_header_object("header-key")`. This function returns a `crow::multipart::header` object for owning message and `crow::multipart::header_view` for non-owning message.<br><br>

The message's individual body parts can be accessed by name using `msg.get_part_by_name("part-name")`.<br><br>

For more info on Multipart messages, go [here](../reference/namespacecrow_1_1multipart.html)
