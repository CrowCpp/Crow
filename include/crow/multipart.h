#pragma once
#include <string>
#include <vector>
#include <sstream>
#include "crow/http_request.h"
#include "crow/http_response.h"

namespace crow
{
    ///Encapsulates anything related to processing and organizing `multipart/xyz` messages
    namespace multipart
    {
        const std::string dd = "--";
        const std::string crlf = "\r\n";

        ///The first part in a section, contains metadata about the part
        struct header
        {
                std::pair<std::string, std::string> value; ///< The first part of the header, usually `Content-Type` or `Content-Disposition`
                std::unordered_map<std::string, std::string> params; ///< The parameters of the header, come after the `value`
        };

        ///One part of the multipart message

        ///It is usually separated from other sections by a `boundary`
        ///
        struct part
        {
            std::vector<header> headers; ///< (optional) The first part before the data, Contains information regarding the type of data and encoding
            std::string body; ///< The actual data in the part
        };

        ///The parsed multipart request/response
        struct message
        {
            ci_map headers;
            std::string boundary; ///< The text boundary that separates different `parts`
            std::vector<part> parts; ///< The individual parts of the message

            const std::string& get_header_value(const std::string& key) const
            {
                return crow::get_header_value(headers, key);
            }

            ///Represent all parts as a string (**does not include message headers**)
            const std::string dump()
            {
                std::stringstream str;
                std::string delimiter = dd + boundary;

                for (unsigned i=0 ; i<parts.size(); i++)
                {
                    str << delimiter << crlf;
                    str << dump(i);
                }
                str << delimiter << dd << crlf;
                return str.str();
            }

            ///Represent an individual part as a string
            const std::string dump(int part_)
            {
                std::stringstream str;
                part item = parts[part_];
                for (header item_h: item.headers)
                {
                    str << item_h.value.first << ": " << item_h.value.second;
                    for (auto& it: item_h.params)
                    {
                        str << "; " << it.first << '=' << pad(it.second);
                    }
                    str << crlf;
                }
                str << crlf;
                str << item.body << crlf;
                return str.str();
            }

            ///Default constructor using default values
            message(const ci_map& headers, const std::string& boundary, const std::vector<part>& sections)
              : headers(headers), boundary(boundary), parts(sections){}

            ///Create a multipart message from a request data
            message(const request& req)
              : headers(req.headers),
                boundary(get_boundary(get_header_value("Content-Type"))),
                parts(parse_body(req.body))
            {}

          private:

            std::string get_boundary(const std::string& header)
            {
                size_t found = header.find("boundary=");
                if (found)
                    return header.substr(found+9);
                return std::string();
            }

            std::vector<part> parse_body(std::string body)
            {

                  std::vector<part> sections;

                  std::string delimiter = dd + boundary;

                  while(body != (crlf))
                  {
                      size_t found = body.find(delimiter);
                      std::string section = body.substr(0, found);

                      //+2 is the CRLF
                      //We don't check it and delete it so that the same delimiter can be used for
                      //the last delimiter (--delimiter--CRLF).
                      body.erase(0, found + delimiter.length() + 2);
                      if (!section.empty())
                      {
                          sections.emplace_back(parse_section(section));
                      }
                  }
                  return sections;
            }

            part parse_section(std::string& section)
            {
                struct part to_return;

                size_t found = section.find(crlf+crlf);
                std::string head_line = section.substr(0, found+2);
                section.erase(0, found + 4);

                parse_section_head(head_line, to_return);
                to_return.body = section.substr(0, section.length()-2);
                return to_return;
            }

            void parse_section_head(std::string& lines, part& part)
            {
                while (!lines.empty())
                {
                    header to_add;

                    size_t found = lines.find(crlf);
                    std::string line = lines.substr(0, found);
                    lines.erase(0, found+2);
                    //add the header if available
                    if (!line.empty())
                    {
                        size_t found = line.find("; ");
                        std::string header = line.substr(0, found);
                        if (found != std::string::npos)
                            line.erase(0, found+2);
                        else
                            line = std::string();

                        size_t header_split = header.find(": ");

                        to_add.value = std::pair<std::string, std::string>(header.substr(0, header_split), header.substr(header_split+2));
                    }

                    //add the parameters
                    while (!line.empty())
                    {
                        size_t found = line.find("; ");
                        std::string param = line.substr(0, found);
                        if (found != std::string::npos)
                            line.erase(0, found+2);
                        else
                            line = std::string();

                        size_t param_split = param.find('=');

                        std::string value = param.substr(param_split+1);

                        to_add.params.emplace(param.substr(0, param_split), trim(value));
                    }
                    part.headers.emplace_back(to_add);
                }
            }

            inline std::string trim (std::string& string, const char& excess = '"')
            {
                if (string.length() > 1 && string[0] == excess && string[string.length()-1] == excess)
                    return string.substr(1, string.length()-2);
                return string;
            }

            inline std::string pad (std::string& string, const char& padding = '"')
            {
                    return (padding + string + padding);
            }

        };


        struct sting_view
        {
            sting_view()
                    : c_str(nullptr)
                    , size(0){};

            sting_view(const char *str, int isize)
                    : c_str(str)
                    , size(isize){};

            sting_view(const std::string &str)
                    : c_str(str.c_str())
                    , size(str.size()){};

            void set(const char *str, int isize)
            {
                c_str = str;
                size = isize;
            };

            void erase(int isize)
            {
                int tmp = max(0, size - isize);
                c_str += (size - tmp);
                size = tmp;
            }

            inline std::string substr(int spos, int len)
            {
                assert(spos + len < size);
                char *val = new char[len + 1];
                memcpy(val, c_str + spos, len);
                val[len] = 0;
                std::string ret_str = val;
                delete[] val;
                return ret_str;
            }

            const char *c_str;
            int size;
        };

        inline size_t strpos(sting_view &str1, const std::string &str2)
        {
            void *ret = memmem((const void *)(str1.c_str), str1.size, (const void *)(str2.c_str()), str2.size());
            if (ret) {
                return (const char *)ret - str1.c_str;
            }
            return -1;
        }

        struct part_view
        {
            std::vector<header>
                    headers; ///< (optional) The first part before the data, Contains information regarding the type of data and encoding

            sting_view body; ///< The actual data in the part
        };

        struct message_view
        {
            ci_map headers;
            std::string boundary; ///< The text boundary that separates different `parts`
            std::vector<part_view> parts; ///< The individual parts of the message

            const std::string &get_header_value(const std::string &key) const
            {
                return crow::get_header_value(headers, key);
            }

            ///Represent all parts as a string (**does not include message headers**)
            const std::string dump()
            {
                std::stringstream str;
                std::string delimiter = dd + boundary;

                for (uint i = 0; i < parts.size(); i++) {
                    str << delimiter << crlf;
                    str << dump(i);
                }
                str << delimiter << dd << crlf;
                return str.str();
            }

            ///Represent an individual part as a string
            const std::string dump(int part_)
            {
                std::stringstream str;
                part_view &item = parts[part_];
                for (header item_h : item.headers) {
                    str << item_h.value.first << ": " << item_h.value.second;
                    for (auto &it : item_h.params) {
                        str << "; " << it.first << '=' << pad(it.second);
                    }
                    str << crlf;
                }
                str << crlf;
                str << std::string(item.body.c_str) << crlf;
                return str.str();
            }

            ///Default constructor using default values
            message_view(const ci_map &headers, const std::string &boundary, const std::vector<part_view> &sections)
                    : headers(headers)
                    , boundary(boundary)
                    , parts(sections)
            {
            }

            ///Create a multipart message from a request data
            message_view(const request &req)
                    : headers(req.headers)
                    , boundary(get_boundary(get_header_value("Content-Type")))
                    , parts(parse_body(req.body))
            {
            }

            private:
            std::string get_boundary(const std::string &header)
            {
                size_t found = header.find("boundary=");
                if (found)
                    return header.substr(found + 9);
                return std::string();
            }

            std::vector<part_view> parse_body(const std::string &body)
            {
                std::vector<part_view> sections;

                std::string delimiter = dd + boundary;

                sting_view body_view(body);

                while (memcmp(body_view.c_str, crlf.c_str(), 2)) {
                    size_t found = strpos(body_view, delimiter);

                    sting_view section(body_view.c_str, max(0, static_cast<int>(found)));

                    //+2 is the CRLF
                    //We don't check it and delete it so that the same delimiter can be used for
                    //the last delimiter (--delimiter--CRLF).
                    body_view.erase(found + delimiter.length() + 2);

                    if (section.size) {
                        sections.emplace_back(parse_section(section));
                    }
                }
                return sections;
            }

            part_view parse_section(sting_view &section)
            {
                static const std::string crlf2 = crlf + crlf;
                struct part_view to_return;

                size_t found = strpos(section, crlf2);

                std::string head_line = section.substr(0, found + 2);

                section.erase(found + 4);

                parse_section_head(head_line, to_return);

                //to_return.body(body.substr(section.c_str-body.c_str(), static_cast<int>(max(0, static_cast<int>(section.size - 2))));
                to_return.body.set(section.c_str, static_cast<int>(max(0, static_cast<int>(section.size - 2))));

                return to_return;
            }

            void parse_section_head(std::string &lines, part_view &part)
            {
                while (!lines.empty()) {
                    header to_add;

                    size_t found = lines.find(crlf);
                    std::string line = lines.substr(0, found);
                    lines.erase(0, found + 2);
                    //add the header if available
                    if (!line.empty()) {
                        size_t found = line.find("; ");
                        std::string header = line.substr(0, found);
                        if (found != std::string::npos)
                            line.erase(0, found + 2);
                        else
                            line = std::string();

                        size_t header_split = header.find(": ");

                        to_add.value = std::pair<std::string, std::string>(header.substr(0, header_split),
                                                                           header.substr(header_split + 2));
                    }

                    //add the parameters
                    while (!line.empty()) {
                        size_t found = line.find("; ");
                        std::string param = line.substr(0, found);
                        if (found != std::string::npos)
                            line.erase(0, found + 2);
                        else
                            line = std::string();

                        size_t param_split = param.find('=');

                        std::string value = param.substr(param_split + 1);

                        to_add.params.emplace(param.substr(0, param_split), trim(value));
                    }
                    part.headers.emplace_back(to_add);
                }
            }

            inline std::string trim(std::string &string, const char &excess = '"')
            {
                if (string.length() > 1 && string[0] == excess && string[string.length() - 1] == excess)
                    return string.substr(1, string.length() - 2);
                return string;
            }

            inline std::string pad(std::string &string, const char &padding = '"')
            {
                return (padding + string + padding);
            }
        };
    }
}
