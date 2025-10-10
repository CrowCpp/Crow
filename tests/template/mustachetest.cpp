#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "crow/json.h"
#include "crow/mustache.h"

using namespace std;
using namespace crow;
using namespace crow::mustache;

string read_all(const string& filename)
{
    ifstream is(filename);
    return {istreambuf_iterator<char>(is), istreambuf_iterator<char>()};
}

int main()
{
    try {
        auto data = json::load(read_all("data"));
        auto templ = compile(read_all("template"));
        auto partials = json::load(read_all("partials"));
        set_loader([&](std::string name) -> std::string {
            if (partials.count(name))
            {
                return partials[name].s();
            }
            return "";
        });
        context ctx(data);
        cout << templ.render_string(ctx);
    }
    // catch and return compile errors as text, for the python test to compare
    catch (invalid_template_exception & err) {
        cout << "COMPILE EXCEPTION: " << err.what();
    }
    return 0;
}
