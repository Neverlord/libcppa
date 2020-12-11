/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright 2011-2020 Dominik Charousset                                     *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#pragma once

#include <string>
#include <type_traits>

#include "caf/string_view.hpp"

namespace caf {

/// Convenience function for providing a default inspection scaffold for custom
/// enumeration types.
///
/// The enumeration type must provide the following interface based on free
/// functions:
///
/// ~~~(cpp)
/// enum class Enumeration : ... { ... };
/// std::string to_string(Enumeration);
/// bool from_string(string_view, Enumeration&);
/// bool from_integer(std::underlying_type_t<Enumeration>, Enumeration&);
/// ~~~
template <class Inspector, class Enumeration>
bool default_enum_inspect(Inspector& f, Enumeration& x) {
  using integer_type = std::underlying_type_t<Enumeration>;
  if (f.has_human_readable_format()) {
    auto get = [&x] { return to_string(x); };
    auto set = [&x](string_view str) { return from_string(str, x); };
    return f.apply(get, set);
  } else {
    auto get = [&x] { return static_cast<integer_type>(x); };
    auto set = [&x](integer_type val) { return from_integer(val, x); };
    return f.apply(get, set);
  }
}

} // namespace caf