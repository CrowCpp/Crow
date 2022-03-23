You can set Crow up behind any HTTP proxy of your liking, but we will be focusing specifically on 2 of the most popular web server software solutions, Apache2 and Nginx.<br><br>

A reverse proxy allows you to use Crow without exposing it directly to the internet. It also allows you to, for example, have crow run on a certain specific domain name, subdomain, or even a path, such as `domain.abc/crow`.<br><br>

We advise that you set crow up behind some form of reverse proxy if you plan on running a production Crow server that isn't local.<br>

!!! warning "SSL"

    When using a proxy, make sure that you **do not** compile Crow with SSL enabled. SSL should be handled by the proxy.

## Apache2

Assuming you have both Apache2 and the modules [proxy](https://httpd.apache.org/docs/2.4/mod/mod_proxy.html), [proxy_http](https://httpd.apache.org/docs/2.4/mod/mod_proxy_http.html), [proxy_html](https://httpd.apache.org/docs/2.4/mod/mod_proxy_html.html) (if you plan on serving HTML pages), and [proxy_wstunnel](https://httpd.apache.org/docs/2.4/mod/mod_proxy_wstunnel.html) (if you plan on using websockets). You will need to enable those modules, which you can do using the following commands:

```sh
a2enmod proxy
a2enmod proxy_http
a2enmod proxy_html
a2enmod proxy_wstunnel
```

Next up you'll need to change your configuration (default is `/etc/apache2/sites-enabled/000-default.conf`) and add the following lines (replace `localhost` and `40080` with the address and port you defined for your Crow App):
```
ProxyPass / http://localhost:40080
ProxyPassReverse / http://localhost:40080
```
If you want crow to run in a subdirectory (such as `domain.abc/crow`) you can use the `location` tag:
```
<Location "/crow">

	ProxyPass http://localhost:40080
	
	ProxyPassReverse http://localhost:40080

</Location>
```

!!! note

    If you're using an Arch Linux based OS. You will have to access `/etc/httpd/conf/httpd.conf` to enable modules and change configuration.

## Nginx

Setting Nginx up is slightly simpler than Apache, all you need is the Nginx package itself. Once you've installed it, go to the configuration file (usually a `.conf` file located in `/etc/nginx`) and add the following lines to your server section (replace `localhost` and `40080` with the address and port you defined for your Crow App):

```
location / {
    proxy_pass http://localhost:40080/;
    proxy_http_version 1.1;
}
```
Remember to remove or comment out any existing `location /` section.<br><br>

Alternatively, if you want to use a subdirectory, you can simply change the location parameter as such:

```
location /crow/ {
    proxy_pass http://localhost:40080/;
    proxy_http_version 1.1;
}
```
