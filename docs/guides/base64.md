## Encoding
Using `#!cpp crow::utility::base64encode(mystring, mystring.size())` will return a Base64 encoded string. For URL safe Base64 `#!cpp crow::utility::base64encode_urlsafe(mystring, mystring.size())` can be used. The key used in the encoding process can be changed, it is a string containing all 64 characters to be used.

## Decoding
<span class="tag">[:octicons-feed-tag-16: v1.0](https://github.com/CrowCpp/Crow/releases/v1.0)</span>


Using `#!cpp crow::utility::base64decode(mystring, mystring.size())` with `mystring` being a Base64 encoded string will return a plain-text string. The function works with both normal and URL safe Base64. However it cannot decode a Base64 string encoded with a custom key.
