/******************************************************************************\
 *           ___        __                                                    *
 *          /\_ \    __/\ \                                                   *
 *          \//\ \  /\_\ \ \____    ___   _____   _____      __               *
 *            \ \ \ \/\ \ \ '__`\  /'___\/\ '__`\/\ '__`\  /'__`\             *
 *             \_\ \_\ \ \ \ \L\ \/\ \__/\ \ \L\ \ \ \L\ \/\ \L\.\_           *
 *             /\____\\ \_\ \_,__/\ \____\\ \ ,__/\ \ ,__/\ \__/.\_\          *
 *             \/____/ \/_/\/___/  \/____/ \ \ \/  \ \ \/  \/__/\/_/          *
 *                                          \ \_\   \ \_\                     *
 *                                           \/_/    \/_/                     *
 *                                                                            *
 * Copyright (C) 2011-2013                                                    *
 * Dominik Charousset <dominik.charousset@haw-hamburg.de>                     *
 *                                                                            *
 * This file is part of libcppa.                                              *
 * libcppa is free software: you can redistribute it and/or modify it under   *
 * the terms of the GNU Lesser General Public License as published by the     *
 * Free Software Foundation; either version 2.1 of the License,               *
 * or (at your option) any later version.                                     *
 *                                                                            *
 * libcppa is distributed in the hope that it will be useful,                 *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                       *
 * See the GNU Lesser General Public License for more details.                *
 *                                                                            *
 * You should have received a copy of the GNU Lesser General Public License   *
 * along with libcppa. If not, see <http://www.gnu.org/licenses/>.            *
\******************************************************************************/


#include <map>
#include <cwchar>
#include <limits>
#include <vector>
#include <cstring>
#include <typeinfo>
#include <stdexcept>
#include <algorithm>

#include "cppa/atom.hpp"
#include "cppa/actor.hpp"
#include "cppa/group.hpp"
#include "cppa/channel.hpp"
#include "cppa/any_tuple.hpp"
#include "cppa/any_tuple.hpp"
#include "cppa/message_header.hpp"

#include "cppa/util/algorithm.hpp"
#include "cppa/util/scope_guard.hpp"

#include "cppa/detail/demangle.hpp"
#include "cppa/detail/to_uniform_name.hpp"
#include "cppa/detail/singleton_manager.hpp"
#include "cppa/detail/uniform_type_info_map.hpp"

//#define DEBUG_PARSER

#ifdef DEBUG_PARSER
    namespace { size_t s_indentation = 0; }
#   define PARSER_INIT(message)                                                \
        std::cout << std::string(s_indentation, ' ') << ">>> " << message      \
                  << std::endl;                                                \
        s_indentation += 2;                                                    \
        auto ____sg = cppa::util::make_scope_guard([] { s_indentation -= 2; })
#   define PARSER_OUT(condition, message)                                      \
        if (condition) {                                                       \
            std::cout << std::string(s_indentation, ' ') << "### " << message  \
                      << std::endl;                                            \
        } static_cast<void>(0)
#else
#   define PARSER_INIT(unused) static_cast<void>(0)
#   define PARSER_OUT(unused1, unused2) static_cast<void>(0)
#endif

namespace {

using namespace std;
using namespace cppa;
using namespace detail;

struct platform_int_mapping { const char* name; size_t size; bool is_signed; };

// WARNING: this list is sorted and searched with std::lower_bound;
//          keep ordered when adding elements!
platform_int_mapping platform_dependent_sizes[] = {
    {"char",                sizeof(char),               true},
    {"char16_t",            sizeof(char16_t),           true},
    {"char32_t",            sizeof(char32_t),           true},
    {"int",                 sizeof(int),                true},
    {"long",                sizeof(long),               true},
    {"long int",            sizeof(long int),           true},
    {"long long",           sizeof(long long),          true},
    {"short",               sizeof(short),              true},
    {"short int",           sizeof(short int),          true},
    {"signed char",         sizeof(signed char),        true},
    {"signed int",          sizeof(signed int),         true},
    {"signed long",         sizeof(signed long),        true},
    {"signed long int",     sizeof(signed long int),    true},
    {"signed long long",    sizeof(signed long long),   true},
    {"signed short",        sizeof(signed short),       true},
    {"signed short int",    sizeof(signed short int),   true},
    {"unsigned char",       sizeof(unsigned char),      false},
    {"unsigned int",        sizeof(unsigned int),       false},
    {"unsigned long",       sizeof(unsigned long),      false},
    {"unsigned long int",   sizeof(unsigned long int),  false},
    {"unsigned long long",  sizeof(unsigned long long), false},
    {"unsigned short",      sizeof(unsigned short),     false},
    {"unsigned short int",  sizeof(unsigned short int), false}
};

/*
string map2decorated(const char* name) {
    auto cmp = [](const platform_int_mapping& pim, const char* name) {
        return strcmp(pim.name, name) < 0;
    };
    auto e = end(platform_dependent_sizes);
    auto i = lower_bound(begin(platform_dependent_sizes), e, name, cmp);
    if (i != e && strcmp(i->name, name) == 0) {
        return mapped_int_names[i->size][i->is_signed ? 1 : 0];
    }
    return mapped_name_by_decorated_name(name);
}
*/

string map2decorated(string&& name) {
    auto cmp = [](const platform_int_mapping& pim, const string& str) {
        return strcmp(pim.name, str.c_str()) < 0;
    };
    auto e = end(platform_dependent_sizes);
    auto i = lower_bound(begin(platform_dependent_sizes), e, name, cmp);
    if (i != e && i->name == name) {
        PARSER_OUT(true, name << " => " << mapped_int_names[i->size][i->is_signed ? 1 : 0]);
        return mapped_int_names[i->size][i->is_signed ? 1 : 0];
    }
#   ifdef DEBUG_PARSER
    auto mapped = mapped_name_by_decorated_name(name.c_str());
    PARSER_OUT(mapped != name, name << " => " << string{mapped});
    return mapped;
#   else
    return mapped_name_by_decorated_name(std::move(name));
#   endif
}

class parse_tree {

 public:

    string compile(bool parent_invoked = false) {
        string result;
        propagate_flags();
        if (!parent_invoked) {
            if (m_volatile) result += "volatile ";
            if (m_const) result += "const ";
        }
        if (has_children()) {
            string sub_result = m_children.front().compile(true);
            for (auto i = m_children.begin() + 1; i != m_children.end(); ++i) {
                sub_result += "::";
                sub_result += i->compile(true);
            }
            result += map2decorated(std::move(sub_result));
        }
        else {
            string full_name = map2decorated(std::move(m_name));
            if (is_template()) {
                full_name += "<";
                for (auto& tparam : m_template_parameters) {
                    // decorate each single template parameter
                    if (full_name.back() != '<') full_name += ",";
                    full_name += tparam.compile();
                }
                full_name += ">";
                // decorate full name
            }
            result += map2decorated(std::move(full_name));
        }
        if (!parent_invoked) {
            if (m_pointer) result += "*";
            if (m_lvalue_ref) result += "&";
            if (m_rvalue_ref) result += "&&";
        }
        return map2decorated(std::move(result));
    }

    template<typename Iterator>
    static vector<parse_tree> parse_tpl_args(Iterator first, Iterator last);

    template<typename Iterator>
    static parse_tree parse(Iterator first, Iterator last) {
        PARSER_INIT((std::string{first, last}));
        parse_tree result;
        typedef std::pair<Iterator, Iterator> range;
        std::vector<range> subranges;
        /* lifetime scope of temporary variables needed to fill 'subranges' */ {
            auto find_end = [&](Iterator from) -> Iterator {
                auto open = 1;
                for (auto i = from + 1; i != last && open > 0; ++i) {
                    switch (*i) {
                        default: break;
                        case '<': ++open; break;
                        case '>': if (--open == 0) return i; break;
                    }
                }
                return last;
            };
            auto sub_first = find(first, last, '<');
            while (sub_first != last) {
                auto sub_last = find_end(sub_first);
                subranges.emplace_back(sub_first, sub_last);
                sub_first = find(sub_last + 1, last, '<');
            }
        }
        auto islegal = [](char c) { return isalnum(c) || c == ':' || c == '_'; };
        vector<string> tokens;
        tokens.push_back("");
        vector<Iterator> scope_resolution_ops;
        auto is_in_subrange = [&](Iterator i) -> bool {
            for (auto& r : subranges) {
                if (i >= r.first && i < r.second) return true;
            }
            return false;
        };
        auto add_child = [&](Iterator ch_first, Iterator ch_last) {
            PARSER_OUT(true, "new child: [" << distance(first, ch_first) << ", "
                             << ", " << distance(first, ch_last) << ")");
            result.m_children.push_back(parse(ch_first, ch_last));
        };
        // scan string for "::" separators
        const char* scope_resultion = "::";
        auto sr_first = scope_resultion;
        auto sr_last = sr_first + 2;
        auto scope_iter = search(first, last, sr_first, sr_last);
        if (scope_iter != last) {
            auto itermediate = first;
            if (!is_in_subrange(scope_iter)) {
                add_child(first, scope_iter);
                itermediate = scope_iter + 2;
            }
            while (scope_iter != last) {
                scope_iter = search(scope_iter + 2, last, sr_first, sr_last);
                if (scope_iter != last && !is_in_subrange(scope_iter)) {
                    add_child(itermediate, scope_iter);
                    itermediate = scope_iter + 2;
                }
            }
            if (!result.m_children.empty()) {
                add_child(itermediate, last);
            }
        }
        if (result.m_children.empty()) {
            // no children -> leaf node; parse non-template part now
            CPPA_REQUIRE(subranges.size() < 2);
            vector<range> non_template_ranges;
            if (subranges.empty()) {
                non_template_ranges.emplace_back(first, last);
            }
            else {
                non_template_ranges.emplace_back(first, subranges[0].first);
                for (size_t i = 1; i < subranges.size(); ++i) {
                    non_template_ranges.emplace_back(subranges[i-1].second + 1,
                                                     subranges[i].first);
                }
                non_template_ranges.emplace_back(subranges.back().second + 1, last);
            }
            for (auto& ntr : non_template_ranges) {
                for (auto i = ntr.first; i != ntr.second; ++i) {
                    char c = *i;
                    if (islegal(c)) {
                        if (!tokens.back().empty() && !islegal(tokens.back().back())) {
                            tokens.push_back("");
                        }
                        tokens.back() += c;
                    }
                    else if (c == ' ') {
                        tokens.push_back("");
                    }
                    else if (c == '&') {
                        if (tokens.back().empty() || tokens.back().back() == '&') {
                            tokens.back() += c;
                        }
                        else {
                            tokens.push_back("&");
                        }
                    }
                    else if (c == '*') {
                        tokens.push_back("*");
                    }
                }
                tokens.push_back("");
            }
            if (!subranges.empty()) {
                auto& range0 = subranges.front();
                PARSER_OUT(true, "subrange: [" << distance(first, range0.first + 1)
                                 << "," << distance(first, range0.second) << ")");
                result.m_template_parameters = parse_tpl_args(range0.first + 1,
                                                              range0.second);
            }
            for (auto& token: tokens) {
                if (token == "const") {
                    result.m_const = true;
                }
                else if (token == "volatile") {
                    result.m_volatile = true;
                }
                else if (token == "&") {
                    result.m_lvalue_ref = true;
                }
                else if (token == "&&") {
                    result.m_rvalue_ref = true;
                }
                else if (token == "*") {
                    result.m_pointer = true;
                }
                else if (token == "class" || token == "struct") {
                    // ignored (created by visual c++ compilers)
                }
                else if (!token.empty()) {
                    if (!result.m_name.empty()) result.m_name += " ";
                    result.m_name += token;
                }
            }
        }
        PARSER_OUT(!subranges.empty(), subranges.size() << " subranges");
        PARSER_OUT(!result.m_children.empty(), result.m_children.size() << " children");
        return result;
    }

    inline bool has_children() const {
        return !m_children.empty();
    }

    inline bool is_template() const {
        return !m_template_parameters.empty();
    }

 private:

    void propagate_flags() {
        for (auto& c : m_children) {
            c.propagate_flags();
            if (c.m_volatile) m_volatile = true;
            if (c.m_const) m_const = true;
            if (c.m_pointer) m_pointer = true;
            if (c.m_lvalue_ref) m_lvalue_ref = true;
            if (c.m_rvalue_ref) m_rvalue_ref = true;
        }
    }

    parse_tree()
    : m_const(false), m_pointer(false), m_volatile(false)
    , m_lvalue_ref(false), m_rvalue_ref(false) { }

    bool m_const;
    bool m_pointer;
    bool m_volatile;
    bool m_lvalue_ref;
    bool m_rvalue_ref;
    bool m_nested_type;

    string m_name;

    vector<parse_tree> m_template_parameters;

    vector<parse_tree> m_children;

};

template<typename Iterator>
vector<parse_tree> parse_tree::parse_tpl_args(Iterator first, Iterator last) {
    vector<parse_tree> result;
    long open_brackets = 0;
    auto i0 = first;
    for (; first != last; ++first) {
        switch (*first) {
            case '<':
                ++open_brackets;
                break;
            case '>':
                --open_brackets;
                break;
            case ',':
                if (open_brackets == 0) {
                    result.push_back(parse(i0, first));
                    i0 = first + 1;
                }
                break;
            default: break;
        }
    }
    result.push_back(parse(i0, first));
    return result;
}

template<size_t RawSize>
void replace_all(string& str, const char (&before)[RawSize], const char* after) {
    // end(before) - 1 points to the null-terminator
    auto i = search(begin(str), end(str), begin(before), end(before) - 1);
    while (i != end(str)) {
        str.replace(i, i + RawSize - 1, after);
        i = search(begin(str), end(str), begin(before), end(before) - 1);
    }
}

const char s_rawan[] = "anonymous namespace";
const char s_an[] = "$";

} // namespace <anonymous>

namespace cppa { namespace detail {

std::string to_uniform_name(const std::string& dname) {
    auto r = parse_tree::parse(begin(dname), end(dname)).compile();
    // replace compiler-dependent "anonmyous namespace" with "@_"
    replace_all(r, s_rawan, s_an);
    return r.c_str();
}

std::string to_uniform_name(const std::type_info& tinfo) {
    return to_uniform_name(demangle(tinfo.name()));
}

} } // namespace detail
