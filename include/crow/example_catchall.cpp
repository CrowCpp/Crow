// compile with g++ example_catchall.cpp -lpthread -lz -I../crow

#define CROW_CATCHALL
#define CROW_MAIN
#include <crow.h>


void catch_all_func( const crow::request& req_crow, crow::response& res_crow )
{
    res_crow.body = "No action found for url: '" + req_crow.raw_url + "'!";
    res_crow.code = 404;
}



class Catcher
{
public:
    void catch_all_member( const crow::request& req_crow, crow::response& res_crow )
    {
        catch_all_func( req_crow, res_crow );
    }

    static void catch_all_static( const crow::request& req_crow, crow::response& res_crow )
    {
        catch_all_func( req_crow, res_crow );
    }

};



int main()
{
    crow::SimpleApp app;

#if 1 // use member function
    using namespace std::placeholders;
    Catcher c;
    crow::catch_all_func_t fn = std::bind( &Catcher::catch_all_member, c, _1, _2);
    app.catchall_handler( fn );
#elif 1 // use static class function
    app.catchall_handler( Catcher::catch_all_static );
#else // use ordinary function
    app.catchall_handler( catch_all_func );
#endif

    app.port( 12345 ).run();
    return 0;
}
