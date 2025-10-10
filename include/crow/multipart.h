#pragma once

#include <string>
#include <vector>
#include <sstream>

#include "crow/http_request.h"
#include "crow/returnable.h"
#include "crow/ci_map.h"
#include "crow/exceptions.h"

namespace crow
{

    /// Encapsulates anything related to processing and organizing `multipart/xyz` messages
    namespace multipart
    {

        const std::string dd = "--";

        /// The first part in a section, contains metadata about the part
        struct header
        {
            std::string value;                                   ///< The first part of the header, usually `Content-Type` or `Content-Disposition`
            std::unordered_map<std::string, std::string> params; ///< The parameters of the header, come after the `value`

            operator int() const { return std::stoi(value); }    ///< Returns \ref value as integer
            operator double() const { return std::stod(value); } ///< Returns \ref value as double
        };

        /// Multipart header map (key is header key).
        using mph_map = std::unordered_multimap<std::string, header, ci_hash, ci_key_eq>;

        /// Find and return the value object associated with the key. (returns an empty class if nothing is found)
        template<typename O, typename T>
        inline const O& get_header_value_object(const T& headers, const std::string& key)
        {
            if (headers.count(key))
            {
                return headers.find(key)->second;
            }
            static O empty;
            return empty;
        }

        /// Same as \ref get_header_value_object() but for \ref multipart.header
        template<typename T>
        inline const header& get_header_object(const T& headers, const std::string& key)
        {
            return get_header_value_object<header>(headers, key);
        }

        ///One part of the multipart message

        ///
        /// It is usually separated from other sections by a `boundary`
        struct part
        {
            mph_map headers;  ///< (optional) The first part before the data, Contains information regarding the type of data and encoding
            std::string body; ///< The actual data in the part

            operator int() const { return std::stoi(body); }    ///< Returns \ref body as integer
            operator double() const { return std::stod(body); } ///< Returns \ref body as double

            const header& get_header_object(const std::string& key) const
            {
                return multipart::get_header_object(headers, key);
            }
        };

        /// Multipart map (key is the name parameter).
        using mp_map = std::unordered_multimap<std::string, part, ci_hash, ci_key_eq>;

        /// The parsed multipart request/response
        struct message : public returnable
        {
            ci_map headers;          ///< The request/response headers
            std::string boundary;    ///< The text boundary that separates different `parts`
            std::vector<part> parts; ///< The individual parts of the message
            mp_map part_map;         ///< The individual parts of the message, organized in a map with the `name` header parameter being the key

            const std::string& get_header_value(const std::string& key) const
            {
                return crow::get_header_value(headers, key);
            }

            part get_part_by_name(const std::string& name)
            {
                mp_map::iterator result = part_map.find(name);
                if (result != part_map.end())
                    return result->second;
                else
                    return {};
            }

            /// Represent all parts as a string (**does not include message headers**)
            std::string dump() const override
            {
                std::stringstream str;
                std::string delimiter = dd + boundary;

                for (unsigned i = 0; i < parts.size(); i++)
                {
                    str << delimiter << crlf;
                    str << dump(i);
                }
                str << delimiter << dd << crlf;
                return str.str();
            }

            /// Represent an individual part as a string
            std::string dump(int part_) const
            {
                std::stringstream str;
                part item = parts[part_];
                for (auto& item_h : item.headers)
                {
                    str << item_h.first << ": " << item_h.second.value;
                    for (auto& it : item_h.second.params)
                    {
                        str << "; " << it.first << '=' << pad(it.second);
                    }
                    str << crlf;
                }
                str << crlf;
                str << item.body << crlf;
                return str.str();
            }

            /// Default constructor using default values
            message(const ci_map& headers_, const std::string& boundary_, const std::vector<part>& sections):
              returnable("multipart/form-data; boundary=CROW-BOUNDARY"), headers(headers_), boundary(boundary_), parts(sections)
            {
                if (!boundary.empty())
                    content_type = "multipart/form-data; boundary=" + boundary;
                for (auto& item : parts)
                {
                    part_map.emplace(
                      (get_header_object(item.headers, "Content-Disposition").params.find("name")->second),
                      item);
                }
            }

            /// Create a multipart message from a request data
            explicit message(const request& req):
              returnable("multipart/form-data; boundary=CROW-BOUNDARY"),
              headers(req.headers),
              boundary(get_boundary(get_header_value("Content-Type")))
            {
                if (!boundary.empty())
                {
                    content_type = "multipart/form-data; boundary=" + boundary;
                    parse_body(req.body);
                }
                else
                {
                    throw bad_request("Empty boundary in multipart message");
                }
            }

        private:
            std::string get_boundary(const std::string& header) const
            {
                constexpr char boundary_text[] = "boundary=";
                size_t found = header.find(boundary_text);
                if (found != std::string::npos)
                {
                    std::string to_return(header.substr(found + strlen(boundary_text)));
                    if (to_return[0] == '\"')
                    {
                        to_return = to_return.substr(1, to_return.length() - 2);
                    }
                    return to_return;
                }
                return std::string();
            }

            void parse_body(std::string body)
            {
                std::string delimiter = dd + boundary;

                // TODO(EDev): Exit on error
                while (body != (crlf))
                {
                    size_t found = body.find(delimiter);
                    if (found == std::string::npos)
                    {
                        // did not find delimiter; probably an ill-formed body; throw to indicate the issue to user
                        throw bad_request("Unable to find delimiter in multipart message. Probably ill-formed body");
                    }
                    std::string section = body.substr(0, found);

                    // +2 is the CRLF.
                    // We don't check it and delete it so that the same delimiter can be used for The last delimiter (--delimiter--CRLF).
                    body.erase(0, found + delimiter.length() + 2);
                    if (!section.empty())
                    {
                        part parsed_section(parse_section(section));
                        part_map.emplace(
                          (get_header_object(parsed_section.headers, "Content-Disposition").params.find("name")->second),
                          parsed_section);
                        parts.push_back(std::move(parsed_section));
                    }
                }
            }

            part parse_section(std::string& section)
            {
                struct part to_return;

                size_t found = section.find(crlf + crlf);
                std::string head_line = section.substr(0, found + 2);
                section.erase(0, found + 4);

                parse_section_head(head_line, to_return);
                to_return.body = section.substr(0, section.length() - 2);
                return to_return;
            }

            void parse_section_head(std::string& lines, part& part)
            {
                while (!lines.empty())
                {
                    header to_add;

                    const size_t found_crlf = lines.find(crlf);
                    std::string line = lines.substr(0, found_crlf);
                    std::string key;
                    lines.erase(0, found_crlf + 2);
                    // Add the header if available
                    if (!line.empty())
                    {
                        const size_t found_semicolon = line.find("; ");
                        std::string header = line.substr(0, found_semicolon);
                        if (found_semicolon != std::string::npos)
                            line.erase(0, found_semicolon + 2);
                        else
                            line = std::string();

                        size_t header_split = header.find(": ");
                        key = header.substr(0, header_split);

                        to_add.value = header.substr(header_split + 2);
                    }

                    // Add the parameters
                    while (!line.empty())
                    {
                        const size_t found_semicolon = line.find("; ");
                        std::string param = line.substr(0, found_semicolon);
                        if (found_semicolon != std::string::npos)
                            line.erase(0, found_semicolon + 2);
                        else
                            line = std::string();

                        size_t param_split = param.find('=');

                        std::string value = param.substr(param_split + 1);

                        to_add.params.emplace(param.substr(0, param_split), trim(value));
                    }
                    part.headers.emplace(key, to_add);
                }
            }

            inline std::string trim(std::string& string, const char& excess = '"') const
            {
                if (string.length() > 1 && string[0] == excess && string[string.length() - 1] == excess)
                    return string.substr(1, string.length() - 2);
                return string;
            }

            inline std::string pad(std::string& string, const char& padding = '"') const
            {
                return (padding + string + padding);
            }
        };
    } // namespace multipart
} // namespace crow
