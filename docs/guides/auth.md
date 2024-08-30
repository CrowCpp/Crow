While Crow doesn't directly support HTTP authentication, it does provide all the tools you need to build your own. This tutorial will show you how to setup basic and token authentication using Crow.

## Shared information
Every way boils down to the same basic flow:
- The handler calls a verification function.
- The handler provides a `request` and \<optionally\> a `response`.
- The function returns a `bool` or `enum` status.
- Handler either continues or stops executing based on the returned status.
- Either the function or handler modify and `end()` the `response` in case of failure.

For the purposes of this tutorial, we will assume that the verification function is defined as `#!cpp bool verify(crow::request req, crow::response res)`

## Basic Auth
Basic HTTP authentication requires the client to send the Username and Password as a single string, separated by a colon (':') and then encoded as Base64. This data needs to be placed in the `Authorization` header of the request. A sample header using the credentials "Username" and "Password" would look like this: `Authorization: Basic VXNlcm5hbWU6UGFzc3dvcmQ=`.<br><br>

We don't need to worry about creating the request, we only need to extract the credentials from the `Authorization` header and verify them.
!!! note

    There are multiple ways to verify the credentials. Most involve checking the username in a database, then checking a hash of the password against the stored password hash for that username. This tutorial will not go over them

<br>

To do this we first need to get the `Authorization` header as a string by using the following code:
```cpp
std::string myauth = req.get_header_value("Authorization");
```
<br>

Next we need to isolate our encoded credentials and decode them as follows:
```cpp
std::string mycreds = myauth.substr(6);
std::string d_mycreds = crow::utility::base64decode(mycreds, mycreds.size());
```
<br>

Now that we have our `username:password` string, we only need to separate it into 2 different strings and verify their validity:
```cpp
size_t found = d_mycreds.find(':');
std::string username = d_mycreds.substr(0, found);
std::string password = d_mycreds.substr(found+1);

/*Verify validity of username and password here*/
return true; //or false if the username/password are invalid
```

## Token Auth
Tokens are some form of unique data that a server can provide to a client in order to verify the client's identity later. While on the surface level they don't provide more security than a strong password, they are designed to be less valuable by being *temporary* and providing *limited access*. Variables like expiration time and access scopes are heavily reliant on the implementation however.<br><br>

### Access Tokens
The kind of the token itself can vary depending on the implementation and project requirements: Many services use randomly generated strings as tokens. Then compare them against a database to retrieve the associated user data. Some services however prefer using data bearing tokens. One example of the latter kind is JWT, which uses JSON strings encoded in Base64 and signed using a private key or an agreed upon secret. While this has the added hassle of signing the token to ensure that it's not been tampered with. It does allow for the client to issue tokens without ever needing to present a password or contact a server. The server would simply be able to verify the signature using the client's public key or secret.<br><br>

### Using an Access Token
Authenticating with an access token usually involves 2 stages: The first being acquiring the access token from an authority (either by providing credentials such as a username and a password to a server or generating a signed token). The scope of the token (what kind of information it can read or change) is usually defined in this step.<br><br>

The second stage is simply presenting the Token to the server when requesting a resource. This is even simpler than using basic authentication. All the client needs to do is provide the `Authorization` header with a keyword (usually `Bearer`) followed by the token itself (for example: `Authorization: Bearer ABC123`). Once the client has done that the server will need to acquire this token, which can easily be done as follows:<br>

```cpp
std::string myauth = req.get_header_value("Authorization");
std::string mycreds = myauth.substr(7); // The length can change based on the keyword used

/*Verify validity of the token here*/
return true; //or false if the token is invalid
```
<br>
The way of verifying the token is largely up to the implementation, and involves either Bearer token decoding and verification, or database access, neither of which is in this tutorial's scope.<br><br>

### Refresh Tokens
Some services may choose to provide a refresh token alongside the access token. This token can be used to request a new access token if the existing one has expired. It provides convenience and security in that it makes it possible to acquire new access tokens without the need to expose a password. The downside however is that it can allow a malicious entity to keep its access to a compromised account. As such refresh tokens need to be handled with care, kept secure, and always invalidated as soon as a client logs out or requests a new access token.

## Sessions
While Crow does not provide built in support for user sessions, a community member was kind enough to provide their own implementation on one of the related issue, their comment along with the code is available [here](https://github.com/CrowCpp/Crow/issues/144#issuecomment-860384771) (Please keep in mind that while we appreciate all efforts to push Crow forward, we cannot provide support for this implementation unless it becomes part of the core project).
