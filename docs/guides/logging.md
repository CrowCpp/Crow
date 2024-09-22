Crow comes with a simple and easy to use logging system.<br><br>

!!! note

    Currently Crow's Logger is not linked to the Crow application, meaning if an executable has more than one Crow application they'll be sharing any variables or classes relating to Logging.

## Setting up logging level
You can set up the level at which crow displays logs by using the app's `loglevel(crow::LogLevel)` method.<br><br>

The available log levels are as follows (please note that setting a level will also display all logs below this level):

- Debug
- Info
- Warning
- Error
- Critical
<br><br>

To set a logLevel, just use `#!cpp app.loglevel(crow::LogLevel::Warning)`, This will not show any debug or info logs. It will however still show error and critical logs.<br><br>

!!! note

    Setting the Macro `CROW_ENABLE_DEBUG` during compilation will also set the log level to `Debug` (unless otherwise set using `loglevel()`).


## Writing a log
Writing a log is as simple as `#!cpp CROW_LOG_<LOG LEVEL> << "Hello";` (replace&lt;LOG LEVEL&gt; with the actual level in all caps, so you have `CROW_LOG_WARNING`).

!!! note

    Log times are reported in GMT timezone by default. This is because HTTP requires all reported times for requests and responses to be in GMT. This can be changed by using the macro `CROW_USE_LOCALTIMEZONE` which will set **only the log timezone** to the server's local timezone.

## Creating A custom logger
<span class="tag">[:octicons-feed-tag-16: v1.0](https://github.com/CrowCpp/Crow/releases/v1.0)</span>


Assuming you have an existing logger or Crow's default format just doesn't work for you. Crow allows you to use a custom logger for any log made using the `CROW_LOG_<LOG LEVEL>` macro.<br>
All you need is a class extending `#!cpp crow::ILogHandler` containing the method `#!cpp void log(std::string, crow::LogLevel)`.<br>
Once you have your custom logger, you need to set it via `#!cpp crow::logger::setHandler(&MyLogger);`. Here's a full example:<br>
```cpp
class CustomLogger : public crow::ILogHandler {
 public:
  CustomLogger() {}
  void log(std::string message, crow::LogLevel /*level*/) {
    // "message" doesn't contain the timestamp and loglevel
    // prefix the default logger does and it doesn't end
    // in a newline.
    std::cerr << message << std::endl;
  }
};

int main(int argc, char** argv) {
  CustomLogger logger;
  crow::logger::setHandler(&logger);

  crow::SimpleApp app;
  CROW_ROUTE(app, "/")([]() { return "Hello"; });
  app.run();
}
```
