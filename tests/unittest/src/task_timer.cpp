#include "unittest.h"

TEST_CASE("task_timer")
{
    using work_guard_type = asio::executor_work_guard<asio::io_service::executor_type>;

    asio::io_service io_service;
    work_guard_type work_guard(io_service.get_executor());
    thread io_thread([&io_service]() {
        io_service.run();
    });

    bool a = false;
    bool b = false;

    crow::detail::task_timer timer(io_service);
    CHECK(timer.get_default_timeout() == 5);
    timer.set_default_timeout(7);
    CHECK(timer.get_default_timeout() == 7);

    timer.schedule([&a]() {
        a = true;
    },
                   5);
    timer.schedule([&b]() {
        b = true;
    });

    this_thread::sleep_for(chrono::seconds(4));
    CHECK(a == false);
    CHECK(b == false);
    this_thread::sleep_for(chrono::seconds(2));
    CHECK(a == true);
    CHECK(b == false);
    this_thread::sleep_for(chrono::seconds(2));
    CHECK(a == true);
    CHECK(b == true);

    io_service.stop();
    io_thread.join();
} // task_timer
