//#include "crow.h"
#include <string>
#include <vector>
#include <chrono>
#include <iostream>

using namespace std;

struct Element
{
    virtual void render(std::string& target) const = 0;
    virtual ~Element()=default;
};

struct Text : public Element
{
    std::string text;
};

namespace html
{
    struct CompositeElement : Element
    {
        std::vector<CompositeElement> children;
        
        CompositeElement(): children()
        {
        
        }
        
        CompositeElement(const std::initializer_list<CompositeElement>& children): children(children)
        {}
    
        ~CompositeElement() = default;
        virtual void on_render_start(std::string& target) const  {}
        virtual void on_render_end(std::string& target) const {}
        void render(std::string& target) const override
        {
            this->on_render_start(target);
            for (auto & child : children)
            {
                child.render(target);
            }
            this->on_render_end(target);
            
        }
    };
    template<const char* NAME> 
    struct Tag : public CompositeElement
    {   static constexpr const char* TAG = NAME;

        Tag() = default;

        Tag(const Tag& child) = default;

        Tag(const std::initializer_list<CompositeElement>& children): CompositeElement(children) {
        
        }
        
        ~Tag() = default;

        void on_render_start(std::string& target) const override
        {
            target.append(1, '<').append(TAG).append(1, '>');
        }

        void on_render_end(std::string & target) const override
        {
                target.append("</").append(TAG).append(">");
        }
    };

    constexpr const char html_tag[] = "HTML";
    typedef Tag<html_tag> html;
    constexpr const char body_tag[] = "body";
    typedef Tag<body_tag> body;
    constexpr const char p_tag[] = "p";
    typedef Tag<p_tag> p;
    

}

using html::body;
using html::p;



int main()
{
    std::string out;
    body b;

    b.render(out);

    std::cout << out;
    out = "";

    body b2{html::p()};

    b2.render(out);
    std::cout << out;
    out = "";

    html::html page{
      body{
        html::p()}};
    page.render(out);
    std::cout << out;
    //crow::SimpleApp app;
    //crow::mustache::set_base(".");

    //CROW_ROUTE(app, "/")
    //([] {
    //    crow::response response;
    //    html::html page;
    //    
    //    page.render(response.body);
    //        return response;
    //    });

    //app.port(40080)
    //  //.multithreaded()
    //  .run();
}
