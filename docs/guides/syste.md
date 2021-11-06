Using Systemd allows you to run any executable or script when the system starts. This can be useful when you don't want to re-run your Crow application every single time you restart your server.<br><br>

## Writing the Service Unit File
In order to have Systemd recognize your application, you need to create a `.service` file that explains how Systemd should handle your program.<br><br>

To create a service file, you need to go to `/etc/systemd/system` and create an empty text file with the extension `.service`, the file name can be anything.<br><br>

Once the file is created, open it using your favorite text editor and add the following:

```sh
[Unit]
Description=My revolutionary Crow application

Wants=network.target
After=syslog.target network-online.target

[Service]
Type=simple
ExecStart=/absolute/path/to/your/executable
Restart=on-failure
RestartSec=10
KillMode=process

[Install]
WantedBy=multi-user.target
```

You will then need to give the correct permission, this can be done by using the following command (a `sudo` maybe required):

```sh
chmod 640 /etc/systemd/system/crowthing.service
```

And that's it! You can now use your `systemctl` controls to `enable`, `start`, `stop`, or `disable` your Crow application.<br><br>

If you're not familiar with Systemd, `systemctl enable crowthing.service` will allow your Crow application to run at startup, `start` will start it, and the rest is simple.
