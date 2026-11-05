Crow supports HTTPS though SSL or TLS.<br><br>

!!! note

    When mentioning SSL in this documentation, it is often a reference to openSSL, which includes TLS.<br><br>


To enable SSL, first your application needs to define either a `.crt` and `.key` files, or a `.pem` file. You can create them using openssl locally or use CA based ones
Once you have your files, you can add them to your app like this:<br>
`#!cpp app.ssl_file("/path/to/cert.crt", "/path/to/keyfile.key")` or `#!cpp app.ssl_file("/path/to/pem_file.pem")`. Please note that this method can be part of the app method chain, which means it can be followed by `.run()` or any other method.<br><br>

If you are using fullchain certificate files such as the ones issued using let's encrypt/certbot/acme.sh, the CA part should be provided to crow and processed accordingly.<br>
To do so, you should not use `#!cpp app.ssl_file("/path/to/cert.crt", "/path/to/keyfile.key")` but rather `#!cpp app.ssl_chainfile("/path/to/fullchain.cer", "/path/to/keyfile.key")` or your application will retrun `[ERROR   ] Could not start adaptor: tlsv1 alert unknown ca (SSL routines)` on request and client side will get a similar error `#!bash ssl.SSLCertVerificationError: [SSL: CERTIFICATE_VERIFY_FAILED] certificate verify failed: unable to get local issuer certificate (_ssl.c:1007)`.<br><br>

You also need to define `CROW_ENABLE_SSL` in your compiler definitions (`g++ main.cpp -DCROW_ENABLE_SSL` for example) or `set(CROW_ENABLE_SSL ON)` in `CMakeLists.txt`.

You can also set your own SSL context (by using `asio::ssl::context ctx`) and then applying it via the `#!cpp app.ssl(ctx)` method.<br><br>

!!! warning

    If you plan on using a proxy like Nginx or Apache2, **DO NOT** use SSL in crow, instead define it in your proxy and keep the connection between the proxy and Crow non-SSL.
