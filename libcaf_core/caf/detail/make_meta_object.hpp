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

#include <algorithm>
#include <cstddef>

#include "caf/binary_deserializer.hpp"
#include "caf/binary_serializer.hpp"
#include "caf/byte.hpp"
#include "caf/deserializer.hpp"
#include "caf/detail/meta_object.hpp"
#include "caf/detail/padded_size.hpp"
#include "caf/detail/stringification_inspector.hpp"
#include "caf/error.hpp"
#include "caf/serializer.hpp"

namespace caf::detail {

template <class T>
meta_object make_meta_object(const char* type_name) {
  return {
    type_name,
    padded_size_v<T>,
    [](void* ptr) noexcept { reinterpret_cast<T*>(ptr)->~T(); },
    [](void* ptr) { new (ptr) T(); },
    [](const void* src, void* dst) {
      new (dst) T(*reinterpret_cast<const T*>(src));
    },
    [](caf::binary_serializer& sink, const void* ptr) {
      return sink(*reinterpret_cast<const T*>(ptr));
    },
    [](caf::binary_deserializer& source, void* ptr) {
      return source(*reinterpret_cast<T*>(ptr));
    },
    [](caf::serializer& sink, const void* ptr) {
      return sink(*reinterpret_cast<const T*>(ptr));
    },
    [](caf::deserializer& source, void* ptr) {
      return source(*reinterpret_cast<T*>(ptr));
    },
    [](std::string& buf, const void* ptr) {
      stringification_inspector f{buf};
      f(*reinterpret_cast<const T*>(ptr));
    },
  };
}

} // namespace caf::detail
