/*
Copyright (c) 2014-2017, ipkn
              2020-2021, CrowCpp
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the author nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#define CROW_MAIN
#include "crow.h"
#include <string>
#include <vector>
#include <chrono>

using namespace std;

vector<string> msgs;
vector<pair<crow::response*, decltype(chrono::steady_clock::now())>> ress;

void broadcast(const string& msg)
{
    msgs.push_back(msg);
    crow::json::wvalue x;
    x["msgs"][0] = msgs.back();
    x["last"] = msgs.size();
    string body = x.dump();
    for(auto p : ress)
    {
        auto* res = p.first;
        CROW_LOG_DEBUG << res << " replied: " << body;
        res->end(body);
    }
    ress.clear();
}
// To see how it works go on {ip}:40080 but I just got it working with external build (not directly in IDE, I guess a problem with dependency)
int main()
{
    crow::SimpleApp app;
    crow::mustache::set_base(".");

    CROW_ROUTE(app, "/")
    ([]{
        crow::mustache::context ctx;
        return crow::mustache::load("example_chat.html").render();
    });

    CROW_ROUTE(app, "/logs")
    ([]{
        CROW_LOG_INFO << "logs requested";
        crow::json::wvalue x;
        int start = max(0, (int)msgs.size()-100);
        for(int i = start; i < (int)msgs.size(); i++)
            x["msgs"][i-start] = msgs[i];
        x["last"] = msgs.size();
        CROW_LOG_INFO << "logs completed";
        return x;
    });

    CROW_ROUTE(app, "/logs/<int>")
    ([](const crow::request& /*req*/, crow::response& res, int after){
        CROW_LOG_INFO << "logs with last " << after;
        if (after < (int)msgs.size())
        {
            crow::json::wvalue x;
            for(int i = after; i < (int)msgs.size(); i ++)
                x["msgs"][i-after] = msgs[i];
            x["last"] = msgs.size();

            res.write(x.dump());
            res.end();
        }
        else
        {
            vector<pair<crow::response*, decltype(chrono::steady_clock::now())>> filtered;
            for(auto p : ress)
            {
                if (p.first->is_alive() && chrono::steady_clock::now() - p.second < chrono::seconds(30))
                    filtered.push_back(p);
                else
                    p.first->end();
            }
            ress.swap(filtered);
            ress.push_back({&res, chrono::steady_clock::now()});
            CROW_LOG_DEBUG << &res << " stored " << ress.size();
        }
    });

    CROW_ROUTE(app, "/send")
        .methods("GET"_method, "POST"_method)
    ([](const crow::request& req)
    {
        CROW_LOG_INFO << "msg from client: " << req.body;
        broadcast(req.body);
        return "";
    });

    app.port(40080)
        //.multithreaded()
        .run();
}
