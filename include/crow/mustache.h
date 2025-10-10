/**
 * \file crow/mustache.h
 * \brief This file includes the definition of the crow::mustache
 * namespace and its members.
 */

#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <iterator>
#include <functional>
#include "crow/json.h"
#include "crow/logging.h"
#include "crow/returnable.h"
#include "crow/utility.h"

namespace crow // NOTE: Already documented in "crow/app.h"
{
    /**
     * \namespace crow::mustache
     * \brief In this namespace is defined most of the functions and
     * classes related to template rendering.
     *
     * If you are here you might want to read these functions and
     * classes:
     *
     * - \ref template_t
     * - \ref load_text
     * - \ref load_text_unsafe
     * - \ref load
     * - \ref load_unsafe
     *
     * As name suggest, crow uses [mustache](https://en.wikipedia.org/wiki/Mustache_(template_system))
     * as main template rendering system.
     *
     * You may be interested in taking a look at the [Templating guide
     * page](https://crowcpp.org/master/guides/templating/).
     */
    namespace mustache
    {
        using context = json::wvalue;

        template_t load(const std::string& filename);

        /**
         * \class invalid_template_exception
         * \brief Represents compilation error of an template. Throwed
         * specially at mustache compile time.
         */
        class invalid_template_exception : public std::exception
        {
        public:
            invalid_template_exception(const std::string& msg_):
              msg("crow::mustache error: " + msg_)
            {}
            virtual const char* what() const throw() override
            {
                return msg.c_str();
            }
            std::string msg;
        };

        /**
         * \struct rendered_template
         * \brief Returned object after call the
         * \ref template_t::render() method. Its intended to be
         * returned during a **rule declaration**.
         *
         * \see \ref CROW_ROUTE
         * \see \ref CROW_BP_ROUTE
         */
        struct rendered_template : returnable
        {
            rendered_template():
              returnable("text/html") {}

            rendered_template(std::string& body):
              returnable("text/html"), body_(std::move(body)) {}

            std::string body_;

            std::string dump() const override
            {
                return body_;
            }
        };

        /**
         * \enum ActionType
         * \brief Used in \ref Action to represent different parsing
         * behaviors.
         *
         * \see \ref Action
         */
        enum class ActionType
        {
            Ignore,
            Tag,
            UnescapeTag,
            OpenBlock,
            CloseBlock,
            ElseBlock,
            Partial,
        };

        /**
         * \struct Action
         * \brief Used during mustache template compilation to
         * represent parsing actions.
         *
         * \see \ref compile
         * \see \ref template_t
         */
        struct Action
        {
            bool has_end_match;
            char tag_char;
            int start;
            int end;
            int pos;
            ActionType t;

            Action(char tag_char_, ActionType t_, size_t start_, size_t end_, size_t pos_ = 0):
              has_end_match(false), tag_char(tag_char_), start(static_cast<int>(start_)), end(static_cast<int>(end_)), pos(static_cast<int>(pos_)), t(t_)
            {
            }

            bool missing_end_pair() const {
                switch (t)
                {
                    case ActionType::Ignore:
                    case ActionType::Tag:
                    case ActionType::UnescapeTag:
                    case ActionType::CloseBlock:
                    case ActionType::Partial:
                        return false;

                    // requires a match
                    case ActionType::OpenBlock:
                    case ActionType::ElseBlock:
                        return !has_end_match;

                    default:
                        throw std::logic_error("invalid type");
                }
            }
        };

        /**
         * \class template_t
         * \brief Compiled mustache template object.
         *
         * \warning Use \ref compile instead.
         */
        class template_t
        {
        public:
            template_t(std::string body):
              body_(std::move(body))
            {
                // {{ {{# {{/ {{^ {{! {{> {{=
                parse();
            }

        private:
            std::string tag_name(const Action& action) const
            {
                return body_.substr(action.start, action.end - action.start);
            }
            auto find_context(const std::string& name, const std::vector<const context*>& stack, bool shouldUseOnlyFirstStackValue = false) const -> std::pair<bool, const context&>
            {
                if (name == ".")
                {
                    return {true, *stack.back()};
                }
                static json::wvalue empty_str;
                empty_str = "";

                int dotPosition = name.find(".");
                if (dotPosition == static_cast<int>(name.npos))
                {
                    for (auto it = stack.rbegin(); it != stack.rend(); ++it)
                    {
                        if ((*it)->t() == json::type::Object)
                        {
                            if ((*it)->count(name))
                                return {true, (**it)[name]};
                        }
                    }
                }
                else
                {
                    std::vector<int> dotPositions;
                    dotPositions.push_back(-1);
                    while (dotPosition != static_cast<int>(name.npos))
                    {
                        dotPositions.push_back(dotPosition);
                        dotPosition = name.find(".", dotPosition + 1);
                    }
                    dotPositions.push_back(name.size());
                    std::vector<std::string> names;
                    names.reserve(dotPositions.size() - 1);
                    for (int i = 1; i < static_cast<int>(dotPositions.size()); i++)
                        names.emplace_back(name.substr(dotPositions[i - 1] + 1, dotPositions[i] - dotPositions[i - 1] - 1));

                    for (auto it = stack.rbegin(); it != stack.rend(); ++it)
                    {
                        const context* view = *it;
                        bool found = true;
                        for (auto jt = names.begin(); jt != names.end(); ++jt)
                        {
                            if (view->t() == json::type::Object &&
                                view->count(*jt))
                            {
                                view = &(*view)[*jt];
                            }
                            else
                            {
                                if (shouldUseOnlyFirstStackValue)
                                {
                                    return {false, empty_str};
                                }
                                found = false;
                                break;
                            }
                        }
                        if (found)
                            return {true, *view};
                    }
                }

                return {false, empty_str};
            }

            void escape(const std::string& in, std::string& out) const
            {
                out.reserve(out.size() + in.size());
                for (auto it = in.begin(); it != in.end(); ++it)
                {
                    switch (*it)
                    {
                        case '&': out += "&amp;"; break;
                        case '<': out += "&lt;"; break;
                        case '>': out += "&gt;"; break;
                        case '"': out += "&quot;"; break;
                        case '\'': out += "&#39;"; break;
                        case '/': out += "&#x2F;"; break;
                        case '`': out += "&#x60;"; break;
                        case '=': out += "&#x3D;"; break;
                        default: out += *it; break;
                    }
                }
            }

            bool isTagInsideObjectBlock(const int& current, const std::vector<const context*>& stack) const
            {
                int openedBlock = 0;
                for (int i = current; i > 0; --i)
                {
                    auto& action = actions_[i - 1];

                    if (action.t == ActionType::OpenBlock)
                    {
                        if (openedBlock == 0 && (*stack.rbegin())->t() == json::type::Object)
                        {
                            return true;
                        }
                        --openedBlock;
                    }
                    else if (action.t == ActionType::CloseBlock)
                    {
                        ++openedBlock;
                    }
                }

                return false;
            }

            void render_internal(int actionBegin, int actionEnd, std::vector<const context*>& stack, std::string& out, int indent) const
            {
                int current = actionBegin;

                if (indent)
                    out.insert(out.size(), indent, ' ');

                while (current < actionEnd)
                {
                    auto& fragment = fragments_[current];
                    auto& action = actions_[current];
                    render_fragment(fragment, indent, out);
                    switch (action.t)
                    {
                        case ActionType::Ignore:
                            // do nothing
                            break;
                        case ActionType::Partial:
                        {
                            std::string partial_name = tag_name(action);
                            auto partial_templ = load(partial_name);
                            int partial_indent = action.pos;
                            partial_templ.render_internal(0, partial_templ.fragments_.size() - 1, stack, out, partial_indent ? indent + partial_indent : 0);
                        }
                        break;
                        case ActionType::UnescapeTag:
                        case ActionType::Tag:
                        {
                            bool shouldUseOnlyFirstStackValue = false;
                            if (isTagInsideObjectBlock(current, stack))
                            {
                                shouldUseOnlyFirstStackValue = true;
                            }
                            auto optional_ctx = find_context(tag_name(action), stack, shouldUseOnlyFirstStackValue);
                            auto& ctx = optional_ctx.second;
                            switch (ctx.t())
                            {
                                case json::type::False:
                                case json::type::True:
                                case json::type::Number:
                                    out += ctx.dump();
                                    break;
                                case json::type::String:
                                    if (action.t == ActionType::Tag)
                                        escape(ctx.s, out);
                                    else
                                        out += ctx.s;
                                    break;
                                case json::type::Function:
                                {
                                    std::string execute_result = ctx.execute();
                                    while (execute_result.find("{{") != std::string::npos)
                                    {
                                        template_t result_plug(execute_result);
                                        execute_result = result_plug.render_string(*(stack[0]));
                                    }

                                    if (action.t == ActionType::Tag)
                                        escape(execute_result, out);
                                    else
                                        out += execute_result;
                                }
                                break;
                                default:
                                    throw std::runtime_error("not implemented tag type" + utility::lexical_cast<std::string>(static_cast<int>(ctx.t())));
                            }
                        }
                        break;
                        case ActionType::ElseBlock:
                        {
                            static context nullContext;
                            auto optional_ctx = find_context(tag_name(action), stack);
                            if (!optional_ctx.first)
                            {
                                stack.emplace_back(&nullContext);
                                break;
                            }

                            auto& ctx = optional_ctx.second;
                            switch (ctx.t())
                            {
                                case json::type::List:
                                    if (ctx.l && !ctx.l->empty())
                                        current = action.pos;
                                    else
                                        stack.emplace_back(&nullContext);
                                    break;
                                case json::type::False:
                                case json::type::Null:
                                    stack.emplace_back(&nullContext);
                                    break;
                                default:
                                    current = action.pos;
                                    break;
                            }
                            break;
                        }
                        case ActionType::OpenBlock:
                        {
                            auto optional_ctx = find_context(tag_name(action), stack);
                            if (!optional_ctx.first)
                            {
                                current = action.pos;
                                break;
                            }

                            auto& ctx = optional_ctx.second;
                            switch (ctx.t())
                            {
                                case json::type::List:
                                    if (ctx.l)
                                        for (auto it = ctx.l->begin(); it != ctx.l->end(); ++it)
                                        {
                                            stack.push_back(&*it);
                                            render_internal(current + 1, action.pos, stack, out, indent);
                                            stack.pop_back();
                                        }
                                    current = action.pos;
                                    break;
                                case json::type::Number:
                                case json::type::String:
                                case json::type::Object:
                                case json::type::True:
                                    stack.push_back(&ctx);
                                    break;
                                case json::type::False:
                                case json::type::Null:
                                    current = action.pos;
                                    break;
                                default:
                                    throw std::runtime_error("{{#: not implemented context type: " + utility::lexical_cast<std::string>(static_cast<int>(ctx.t())));
                                    break;
                            }
                            break;
                        }
                        case ActionType::CloseBlock:
                            stack.pop_back();
                            break;
                        default:
                            throw std::runtime_error("not implemented " + utility::lexical_cast<std::string>(static_cast<int>(action.t)));
                    }
                    current++;
                }
                auto& fragment = fragments_[actionEnd];
                render_fragment(fragment, indent, out);
            }
            void render_fragment(const std::pair<int, int> fragment, int indent, std::string& out) const
            {
                if (indent)
                {
                    for (int i = fragment.first; i < fragment.second; i++)
                    {
                        out += body_[i];
                        if (body_[i] == '\n' && i + 1 != static_cast<int>(body_.size()))
                            out.insert(out.size(), indent, ' ');
                    }
                }
                else
                    out.insert(out.size(), body_, fragment.first, fragment.second - fragment.first);
            }

        public:
            /// Output a returnable template from this mustache template
            rendered_template render() const
            {
                context empty_ctx;
                std::vector<const context*> stack;
                stack.emplace_back(&empty_ctx);

                std::string ret;
                render_internal(0, fragments_.size() - 1, stack, ret, 0);
                return rendered_template(ret);
            }

            /// Apply the values from the context provided and output a returnable template from this mustache template
            rendered_template render(const context& ctx) const
            {
                std::vector<const context*> stack;
                stack.emplace_back(&ctx);

                std::string ret;
                render_internal(0, fragments_.size() - 1, stack, ret, 0);
                return rendered_template(ret);
            }

            /// Apply the values from the context provided and output a returnable template from this mustache template
            rendered_template render(const context&& ctx) const
            {
                return render(ctx);
            }

            /// Output a returnable template from this mustache template
            std::string render_string() const
            {
                context empty_ctx;
                std::vector<const context*> stack;
                stack.emplace_back(&empty_ctx);

                std::string ret;
                render_internal(0, fragments_.size() - 1, stack, ret, 0);
                return ret;
            }

            /// Apply the values from the context provided and output a returnable template from this mustache template
            std::string render_string(const context& ctx) const
            {
                std::vector<const context*> stack;
                stack.emplace_back(&ctx);

                std::string ret;
                render_internal(0, fragments_.size() - 1, stack, ret, 0);
                return ret;
            }

        private:
            void parse()
            {
                std::string tag_open = "{{";
                std::string tag_close = "}}";

                std::vector<int> blockPositions;

                size_t current = 0;
                while (1)
                {
                    size_t idx = body_.find(tag_open, current);
                    if (idx == body_.npos)
                    {
                        fragments_.emplace_back(static_cast<int>(current), static_cast<int>(body_.size()));
                        actions_.emplace_back('!', ActionType::Ignore, 0, 0);
                        break;
                    }
                    fragments_.emplace_back(static_cast<int>(current), static_cast<int>(idx));

                    idx += tag_open.size();
                    size_t endIdx = body_.find(tag_close, idx);
                    if (endIdx == idx)
                    {
                        throw invalid_template_exception("empty tag is not allowed");
                    }
                    if (endIdx == body_.npos)
                    {
                        // error, no matching tag
                        throw invalid_template_exception("not matched opening tag");
                    }
                    current = endIdx + tag_close.size();
                    char tag_char = body_[idx];
                    switch (tag_char)
                    {
                        case '#':
                            idx++;
                            while (body_[idx] == ' ')
                                idx++;
                            while (body_[endIdx - 1] == ' ')
                                endIdx--;
                            blockPositions.emplace_back(static_cast<int>(actions_.size()));
                            actions_.emplace_back(tag_char, ActionType::OpenBlock, idx, endIdx);
                            break;
                        case '/':
                            idx++;
                            while (body_[idx] == ' ')
                                idx++;
                            while (body_[endIdx - 1] == ' ')
                                endIdx--;
                            {
                                if (blockPositions.empty())
                                {
                                    throw invalid_template_exception(
                                             std::string("unexpected closing tag: ")
                                             + body_.substr(idx, endIdx - idx)
                                             );
                                }
                                auto& matched = actions_[blockPositions.back()];
                                if (body_.compare(idx, endIdx - idx,
                                                  body_, matched.start, matched.end - matched.start) != 0)
                                {
                                     throw invalid_template_exception(
                                             std::string("not matched {{")
                                             + matched.tag_char
                                             + "{{/ pair: "
                                             + body_.substr(matched.start, matched.end - matched.start) + ", "
                                             + body_.substr(idx, endIdx - idx)
                                             );
                                }
                                matched.pos = static_cast<int>(actions_.size());
                                matched.has_end_match = true;
                            }
                            actions_.emplace_back(tag_char, ActionType::CloseBlock, idx, endIdx, blockPositions.back());
                            blockPositions.pop_back();
                            break;
                        case '^':
                            idx++;
                            while (body_[idx] == ' ')
                                idx++;
                            while (body_[endIdx - 1] == ' ')
                                endIdx--;
                            blockPositions.emplace_back(static_cast<int>(actions_.size()));
                            actions_.emplace_back(tag_char, ActionType::ElseBlock, idx, endIdx);
                            break;
                        case '!':
                            // do nothing action
                            actions_.emplace_back(tag_char, ActionType::Ignore, idx + 1, endIdx);
                            break;
                        case '>': // partial
                            idx++;
                            while (body_[idx] == ' ')
                                idx++;
                            while (body_[endIdx - 1] == ' ')
                                endIdx--;
                            actions_.emplace_back(tag_char, ActionType::Partial, idx, endIdx);
                            break;
                        case '{':
                            if (tag_open != "{{" || tag_close != "}}")
                                throw invalid_template_exception("cannot use triple mustache when delimiter changed");

                            idx++;
                            if (body_[endIdx + 2] != '}')
                            {
                                throw invalid_template_exception("{{{: }}} not matched");
                            }
                            while (body_[idx] == ' ')
                                idx++;
                            while (body_[endIdx - 1] == ' ')
                                endIdx--;
                            actions_.emplace_back(tag_char, ActionType::UnescapeTag, idx, endIdx);
                            current++;
                            break;
                        case '&':
                            idx++;
                            while (body_[idx] == ' ')
                                idx++;
                            while (body_[endIdx - 1] == ' ')
                                endIdx--;
                            actions_.emplace_back(tag_char, ActionType::UnescapeTag, idx, endIdx);
                            break;
                        case '=':
                            // tag itself is no-op
                            idx++;
                            actions_.emplace_back(tag_char, ActionType::Ignore, idx, endIdx);
                            endIdx--;
                            if (body_[endIdx] != '=')
                                throw invalid_template_exception("{{=: not matching = tag: " + body_.substr(idx, endIdx - idx));
                            endIdx--;
                            while (body_[idx] == ' ')
                                idx++;
                            while (body_[endIdx] == ' ')
                                endIdx--;
                            endIdx++;
                            {
                                bool succeeded = false;
                                for (size_t i = idx; i < endIdx; i++)
                                {
                                    if (body_[i] == ' ')
                                    {
                                        tag_open = body_.substr(idx, i - idx);
                                        while (body_[i] == ' ')
                                            i++;
                                        tag_close = body_.substr(i, endIdx - i);
                                        if (tag_open.empty())
                                            throw invalid_template_exception("{{=: empty open tag");
                                        if (tag_close.empty())
                                            throw invalid_template_exception("{{=: empty close tag");

                                        if (tag_close.find(" ") != tag_close.npos)
                                            throw invalid_template_exception("{{=: invalid open/close tag: " + tag_open + " " + tag_close);
                                        succeeded = true;
                                        break;
                                    }
                                }
                                if (!succeeded)
                                    throw invalid_template_exception("{{=: cannot find space between new open/close tags");
                            }
                            break;
                        default:
                            // normal tag case;
                            while (body_[idx] == ' ')
                                idx++;
                            while (body_[endIdx - 1] == ' ')
                                endIdx--;
                            actions_.emplace_back(tag_char, ActionType::Tag, idx, endIdx);
                            break;
                    }
                }

                // ensure no unmatched tags
                for (int i = 0; i < static_cast<int>(actions_.size()); i++)
                {
                    if (actions_[i].missing_end_pair())
                    {
                        throw invalid_template_exception(
                                std::string("open tag has no matching end tag {{")
                                + actions_[i].tag_char
                                + " {{/ pair: "
                                + body_.substr(actions_[i].start, actions_[i].end - actions_[i].start)
                                );
                    }
                }

                // removing standalones
                for (int i = static_cast<int>(actions_.size()) - 2; i >= 0; i--)
                {
                    if (actions_[i].t == ActionType::Tag || actions_[i].t == ActionType::UnescapeTag)
                        continue;
                    auto& fragment_before = fragments_[i];
                    auto& fragment_after = fragments_[i + 1];
                    bool is_last_action = i == static_cast<int>(actions_.size()) - 2;
                    bool all_space_before = true;
                    int j, k;
                    for (j = fragment_before.second - 1; j >= fragment_before.first; j--)
                    {
                        if (body_[j] != ' ')
                        {
                            all_space_before = false;
                            break;
                        }
                    }
                    if (all_space_before && i > 0)
                        continue;
                    if (!all_space_before && body_[j] != '\n')
                        continue;
                    bool all_space_after = true;
                    for (k = fragment_after.first; k < static_cast<int>(body_.size()) && k < fragment_after.second; k++)
                    {
                        if (body_[k] != ' ')
                        {
                            all_space_after = false;
                            break;
                        }
                    }
                    if (all_space_after && !is_last_action)
                        continue;
                    if (!all_space_after &&
                        !(
                          body_[k] == '\n' ||
                          (body_[k] == '\r' &&
                           k + 1 < static_cast<int>(body_.size()) &&
                           body_[k + 1] == '\n')))
                        continue;
                    if (actions_[i].t == ActionType::Partial)
                    {
                        actions_[i].pos = fragment_before.second - j - 1;
                    }
                    fragment_before.second = j + 1;
                    if (!all_space_after)
                    {
                        if (body_[k] == '\n')
                            k++;
                        else
                            k += 2;
                        fragment_after.first = k;
                    }
                }
            }

            std::vector<std::pair<int, int>> fragments_;
            std::vector<Action> actions_;
            std::string body_;
        };

        /// \brief The function that compiles a source into a mustache
        /// template.
        inline template_t compile(const std::string& body)
        {
            return template_t(body);
        }

        namespace detail
        {
            inline std::string& get_template_base_directory_ref()
            {
                static std::string template_base_directory = "templates";
                return template_base_directory;
            }

            /// A base directory not related to any blueprint
            inline std::string& get_global_template_base_directory_ref()
            {
                static std::string template_base_directory = "templates";
                return template_base_directory;
            }
        } // namespace detail

        /// \brief The default way that \ref load, \ref load_unsafe,
        /// \ref load_text and \ref load_text_unsafe use to read the
        /// contents of a file.
        inline std::string default_loader(const std::string& filename)
        {
            std::string path = detail::get_template_base_directory_ref();
            std::ifstream inf(utility::join_path(path, filename));
            if (!inf)
            {
                CROW_LOG_WARNING << "Template \"" << filename << "\" not found.";
                return {};
            }
            return {std::istreambuf_iterator<char>(inf), std::istreambuf_iterator<char>()};
        }

        namespace detail
        {
            inline std::function<std::string(std::string)>& get_loader_ref()
            {
                static std::function<std::string(std::string)> loader = default_loader;
                return loader;
            }
        } // namespace detail

        /// \brief Defines the templates directory path at **route
        /// level**. By default is `templates/`.
        inline void set_base(const std::string& path)
        {
            auto& base = detail::get_template_base_directory_ref();
            base = path;
            if (base.back() != '\\' &&
                base.back() != '/')
            {
                base += '/';
            }
        }

        /// \brief Defines the templates directory path at **global
        /// level**. By default is `templates/`.
        inline void set_global_base(const std::string& path)
        {
            auto& base = detail::get_global_template_base_directory_ref();
            base = path;
            if (base.back() != '\\' &&
                base.back() != '/')
            {
                base += '/';
            }
        }

        /// \brief Change the way that \ref load, \ref load_unsafe,
        /// \ref load_text and \ref load_text_unsafe reads a file.
        ///
        /// By default, the previously mentioned functions load files
        /// using \ref default_loader, that only reads a file and
        /// returns a std::string.
        inline void set_loader(std::function<std::string(std::string)> loader)
        {
            detail::get_loader_ref() = std::move(loader);
        }

        /// \brief Open, read and sanitize a file but returns a
        /// std::string without a previous rendering process.
        ///
        /// Except for the **sanitize process** this function does the
        /// almost the same thing that \ref load_text_unsafe.
        inline std::string load_text(const std::string& filename)
        {
            std::string filename_sanitized(filename);
            utility::sanitize_filename(filename_sanitized);
            return detail::get_loader_ref()(filename_sanitized);
        }

        /// \brief Open and read a file but returns a std::string
        /// without a previous rendering process.
        ///
        /// This function is more like a helper to reduce code like
        /// this...
        ///
        /// ```cpp
        /// std::ifstream file("home.html");
        /// return std::string({std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()});
        /// ```
        ///
        /// ... Into this...
        ///
        /// ```cpp
        /// return load("home.html");
        /// ```
        ///
        /// \warning Usually \ref load_text is more recommended to use
        /// instead because it may prevent some [XSS Attacks](https://en.wikipedia.org/wiki/Cross-site_scripting).
        /// **Never blindly trust your users!**
        inline std::string load_text_unsafe(const std::string& filename)
        {
            return detail::get_loader_ref()(filename);
        }

        /// \brief Open, read and renders a file using a mustache
        /// compiler. It also sanitize the input before compilation.
        inline template_t load(const std::string& filename)
        {
            std::string filename_sanitized(filename);
            utility::sanitize_filename(filename_sanitized);
            return compile(detail::get_loader_ref()(filename_sanitized));
        }

        /// \brief Open, read and renders a file using a mustache
        /// compiler. But it **do not** sanitize the input before
        /// compilation.
        ///
        /// \warning Usually \ref load is more recommended to use
        /// instead because it may prevent some [XSS Attacks](https://en.wikipedia.org/wiki/Cross-site_scripting).
        /// **Never blindly trust your users!**
        inline template_t load_unsafe(const std::string& filename)
        {
            return compile(detail::get_loader_ref()(filename));
        }
    } // namespace mustache
} // namespace crow
