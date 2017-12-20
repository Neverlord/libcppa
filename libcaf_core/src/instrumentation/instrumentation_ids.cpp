/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright (C) 2011 - 2017                                                  *
 * Dominik Charousset <dominik.charousset (at) haw-hamburg.de>                *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#include "caf/instrumentation/instrumentation_ids.hpp"

#include "caf/detail/pretty_type_name.hpp"
#include "caf/string_algorithms.hpp"
#include "caf/abstract_actor.hpp"
#include "caf/detail/hash.hpp"
#include "caf/type_nr.hpp"

#include <typeindex>
#include <cstdint>
#include <utility>

namespace caf {
namespace instrumentation {
namespace detail {
 msgtype_id get_from_pair(const rtti_pair& pair) {
 if (pair.first != 0) {
   return pair.first;
 } else {
   return reinterpret_cast<uint64_t>(pair.second);
 }
}
  msgtype_id get(const atom_value& atom) {
    return static_cast<uint64_t>(atom);
  }
}  // namespace detail

bool instrumented_actor_id::operator==(const instrumented_actor_id& other) const noexcept {
  return type == other.type && id == other.id;
}
bool sender::operator==(const sender& other) const noexcept {
  return actor == other.actor && message == other.message;
}
bool aggregate_sender::operator==(const aggregate_sender& other) const noexcept {
  return actor_type == other.actor_type && message == other.message;
}

msgtype_id get_msgtype() {
  return 0;
}

instrumented_actor_id get_instrumented_actor_id(const abstract_actor& actor) {
  actortype_id actortype = typeid(actor);
  actor_id actorid = actor.id();
  return {actortype, actorid};
}

std::string to_string(instrumentation::actortype_id actortype) {
  auto type_name = caf::detail::pretty_type_name(actortype);
  replace_all(type_name, "%20", "_");
  replace_all(type_name, ",", "_");
  return type_name;
}

std::string to_string(instrumentation::msgtype_id msg) {
  if (msg == 0) {
    return "{}";
  }
  if (msg <= type_nrs) {
    return numbered_type_names[msg - 1];
  }
  auto atom_str = to_string(static_cast<atom_value>(msg));
  if (!atom_str.empty()) {
    replace_all(atom_str, " ", "_");
    return atom_str;
  }
  auto ti = reinterpret_cast<const std::type_info*>(msg);
  auto type_name = caf::detail::pretty_type_name(*ti);
  replace_all(type_name, "%20", "_");
  replace_all(type_name, ",", "_");
  return type_name;
}

} // namespace instrumentation
} // namespace caf

namespace std
{
std::size_t
hash<caf::instrumentation::instrumented_actor_id>::
operator()(const caf::instrumentation::instrumented_actor_id& v)
const noexcept {
  std::size_t seed = 0;
  caf::detail::hash_combine(seed, v.type);
  caf::detail::hash_combine(seed, v.id);
  return seed;
}

std::size_t
hash<caf::instrumentation::sender>::
operator()(const caf::instrumentation::sender& v)
const noexcept {
  std::size_t seed = 0;
  caf::detail::hash_combine(seed, v.actor);
  caf::detail::hash_combine(seed, v.message);
  return seed;
}

std::size_t
hash<caf::instrumentation::aggregate_sender>::
operator()(const caf::instrumentation::aggregate_sender& v)
const noexcept {
  std::size_t seed = 0;
  caf::detail::hash_combine(seed, v.actor_type);
  caf::detail::hash_combine(seed, v.message);
  return seed;
}
} // namespace std