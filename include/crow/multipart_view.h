#pragma once

#include <charconv>
#include <string>
#include <vector>
#include <string_view>
#include <sstream>

#include "crow/http_request.h"
// for crow::multipart::dd
#include "crow/multipart.h"
#include "crow/ci_map.h"

namespace crow
{

    /// Encapsulates anything related to processing and organizing `multipart/xyz` messages
    namespace multipart
    {
        /// The first part in a section, contains metadata about the part
        struct header_view
        {
            std::string_view value;                                        ///< The first part of the header, usually `Content-Type` or `Content-Disposition`
            std::unordered_map<std::string_view, std::string_view> params; ///< The parameters of the header, come after the `value`

            /// Returns \ref value as integer
            operator int() const
            {
                int result = 0;
                std::from_chars(value.data(), value.data() + value.size(), result);
                return result;
            }

            /// Returns \ref value as double
            operator double() const
            {
                // There's no std::from_chars for floating-point types in a lot of STLs
                return std::stod(static_cast<std::string>(value));
            }
        };

        /// Multipart header map (key is header key).
        using mph_view_map = std::unordered_multimap<std::string_view, header_view, ci_hash, ci_key_eq>;

        /// Finds and returns the header with the specified key. (returns an empty header if nothing is found)
        inline const header_view& get_header_object(const mph_view_map& headers, const std::string_view key)
        {
            const auto header = headers.find(key);
            if (header != headers.cend())
            {
                return header->second;
            }

            static header_view empty;
            return empty;
        }

        /// String padded with the specified padding (double quotes by default)
        struct padded
        {
            std::string_view value;   ///< String to pad
            const char padding = '"'; ///< Padding to use

            /// Outputs padded value to the stream
            friend std::ostream& operator<<(std::ostream& stream, const padded value_)
            {
                return stream << value_.padding << value_.value << value_.padding;
            }
        };

        ///One part of the multipart message

        ///
        /// It is usually separated from other sections by a `boundary`
        struct part_view
        {
            mph_view_map headers;  ///< (optional) The first part before the data, Contains information regarding the type of data and encoding
            std::string_view body; ///< The actual data in the part

            /// Returns \ref body as integer
            operator int() const
            {
                int result = 0;
                std::from_chars(body.data(), body.data() + body.size(), result);
                return result;
            }

            /// Returns \ref body as double
            operator double() const
            {
                // There's no std::from_chars for floating-point types in a lot of STLs
                return std::stod(static_cast<std::string>(body));
            }

            const header_view& get_header_object(const std::string_view key) const
            {
                return multipart::get_header_object(headers, key);
            }

            friend std::ostream& operator<<(std::ostream& stream, const part_view& part)
            {
                for (const auto& [header_key, header_value] : part.headers)
                {
                    stream << header_key << ": " << header_value.value;
                    for (const auto& [param_key, param_value] : header_value.params)
                    {
                        stream << "; " << param_key << '=' << padded{param_value};
                    }
                    stream << crlf;
                }
                stream << crlf;
                stream << part.body << crlf;
                return stream;
            }
        };

        /// Multipart map (key is the name parameter).
        using mp_view_map = std::unordered_multimap<std::string_view, part_view, ci_hash, ci_key_eq>;

        /// The parsed multipart request/response
        struct message_view
        {
            std::reference_wrapper<const ci_map> headers; ///< The request/response headers
            std::string boundary;                         ///< The text boundary that separates different `parts`
            std::vector<part_view> parts;                 ///< The individual parts of the message
            mp_view_map part_map;                         ///< The individual parts of the message, organized in a map with the `name` header parameter being the key

            const std::string& get_header_value(const std::string& key) const
            {
                return crow::get_header_value(headers.get(), key);
            }

            part_view get_part_by_name(const std::string_view name)
            {
                mp_view_map::iterator result = part_map.find(name);
                if (result != part_map.end())
                    return result->second;
                else
                    return {};
            }

            friend std::ostream& operator<<(std::ostream& stream, const message_view message)
            {
                std::string delimiter = dd + message.boundary;

                for (const part_view& part : message.parts)
                {
                    stream << delimiter << crlf;
                    stream << part;
                }
                stream << delimiter << dd << crlf;

                return stream;
            }

            /// Represent all parts as a string (**does not include message headers**)
            std::string dump() const
            {
                std::ostringstream str;
                str << *this;
                return std::move(str).str();
            }

            /// Represent an individual part as a string
            std::string dump(int part_) const
            {
                std::ostringstream str;
                str << parts.at(part_);
                return std::move(str).str();
            }

            /// Default constructor using default values
            message_view(const ci_map& headers_, const std::string& boundary_, const std::vector<part_view>& sections):
              headers(headers_), boundary(boundary_), parts(sections)
            {
                for (const part_view& item : parts)
                {
                    part_map.emplace(
                      (get_header_object(item.headers, "Content-Disposition").params.find("name")->second),
                      item);
                }
            }

            /// Create a multipart message from a request data
            explicit message_view(const request& req):
              headers(req.headers),
              boundary(get_boundary(get_header_value("Content-Type")))
            {
                parse_body(req.body);
            }

        private:
            std::string_view get_boundary(const std::string_view header) const
            {
                constexpr std::string_view boundary_text = "boundary=";
                const size_t found = header.find(boundary_text);
                if (found == std::string_view::npos)
                {
                    return std::string_view();
                }

                const std::string_view to_return = header.substr(found + boundary_text.size());
                if (to_return[0] == '\"')
                {
                    return to_return.substr(1, to_return.length() - 2);
                }
                return to_return;
            }

            void parse_body(std::string_view body)
            {
                const std::string delimiter = dd + boundary;

                // TODO(EDev): Exit on error
                while (body != (crlf))
                {
                    const size_t found = body.find(delimiter);
                    if (found == std::string_view::npos)
                    {
                        // did not find delimiter; probably an ill-formed body; ignore the rest
                        break;
                    }

                    const std::string_view section = body.substr(0, found);

                    // +2 is the CRLF.
                    // We don't check it and delete it so that the same delimiter can be used for The last delimiter (--delimiter--CRLF).
                    body = body.substr(found + delimiter.length() + 2);
                    if (!section.empty())
                    {
                        part_view parsed_section = parse_section(section);
                        part_map.emplace(
                          (get_header_object(parsed_section.headers, "Content-Disposition").params.find("name")->second),
                          parsed_section);
                        parts.push_back(std::move(parsed_section));
                    }
                }
            }

            part_view parse_section(std::string_view section)
            {
                constexpr static std::string_view crlf2 = "\r\n\r\n";

                const size_t found = section.find(crlf2);
                const std::string_view head_line = section.substr(0, found + 2);
                section = section.substr(found + 4);

                return part_view{
                  parse_section_head(head_line),
                  section.substr(0, section.length() - 2),
                };
            }

            mph_view_map parse_section_head(std::string_view lines)
            {
                mph_view_map result;

                while (!lines.empty())
                {
                    header_view to_add;

                    const size_t found_crlf = lines.find(crlf);
                    std::string_view line = lines.substr(0, found_crlf);
                    std::string_view key;
                    lines = lines.substr(found_crlf + 2);
                    // Add the header if available
                    if (!line.empty())
                    {
                        const size_t found_semicolon = line.find("; ");
                        std::string_view header = line.substr(0, found_semicolon);
                        if (found_semicolon != std::string_view::npos)
                            line = line.substr(found_semicolon + 2);
                        else
                            line = std::string_view();

                        const size_t header_split = header.find(": ");
                        key = header.substr(0, header_split);

                        to_add.value = header.substr(header_split + 2);
                    }

                    // Add the parameters
                    while (!line.empty())
                    {
                        const size_t found_semicolon = line.find("; ");
                        std::string_view param = line.substr(0, found_semicolon);
                        if (found_semicolon != std::string_view::npos)
                            line = line.substr(found_semicolon + 2);
                        else
                            line = std::string_view();

                        const size_t param_split = param.find('=');

                        const std::string_view value = param.substr(param_split + 1);

                        to_add.params.emplace(param.substr(0, param_split), trim(value));
                    }
                    result.emplace(key, to_add);
                }

                return result;
            }

            inline std::string_view trim(const std::string_view string, const char excess = '"') const
            {
                if (string.length() > 1 && string[0] == excess && string[string.length() - 1] == excess)
                    return string.substr(1, string.length() - 2);
                return string;
            }
        };
    } // namespace multipart
} // namespace crow
