/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright (C) 2011 - 2015                                                  *
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

#include "caf/uniform_type_info_map.hpp"

#include <ios> // std::ios_base::failure
#include <array>
#include <tuple>
#include <limits>
#include <string>
#include <vector>
#include <cstring> // memcmp
#include <algorithm>
#include <type_traits>

#include "caf/locks.hpp"
#include "caf/string_algorithms.hpp"

#include "caf/group.hpp"
#include "caf/logger.hpp"
#include "caf/channel.hpp"
#include "caf/message.hpp"
#include "caf/duration.hpp"
#include "caf/actor_cast.hpp"
#include "caf/actor_system.hpp"
#include "caf/actor_factory.hpp"
#include "caf/abstract_group.hpp"
#include "caf/proxy_registry.hpp"
#include "caf/message_builder.hpp"

#include "caf/detail/type_nr.hpp"
#include "caf/detail/safe_equal.hpp"
#include "caf/detail/scope_guard.hpp"
#include "caf/detail/shared_spinlock.hpp"

namespace caf {

namespace detail {

const char* numbered_type_names[] = {
  "@actor",
  "@actorvec",
  "@addr",
  "@addrvec",
  "@atom",
  "@channel",
  "@charbuf",
  "@down",
  "@duration",
  "@exit",
  "@group",
  "@group_down",
  "@i16",
  "@i32",
  "@i64",
  "@i8",
  "@ldouble",
  "@message",
  "@message_id",
  "@node",
  "@str",
  "@strmap",
  "@strset",
  "@strvec",
  "@sync_exited",
  "@sync_timeout",
  "@timeout",
  "@u16",
  "@u16str",
  "@u32",
  "@u32str",
  "@u64",
  "@u8",
  "@unit",
  "bool",
  "double",
  "float"
};

} // namespace detail

namespace {

using builtins = std::array<uniform_type_info_map::value_factory_kvp,
                            detail::type_nrs - 1>;

void fill_builtins(builtins&, detail::type_list<>, size_t) {
  // end of recursion
}

template <class List>
void fill_builtins(builtins& arr, List, size_t pos) {
  using type = typename detail::tl_head<List>::type;
  typename detail::tl_tail<List>::type next;
  arr[pos].first = detail::numbered_type_names[pos];
  arr[pos].second = &make_type_erased<type>;
  fill_builtins(arr, next, pos + 1);
}

} // namespace <anonymous>

type_erased_value_ptr uniform_type_info_map::make_value(uint16_t nr) const {
  return builtin_[nr - 1].second();
}

type_erased_value_ptr
uniform_type_info_map::make_value(const std::string& x) const {
  auto pred = [&](const value_factory_kvp& kvp) {
    return kvp.first == x;
  };
  auto e = builtin_.end();
  auto i = std::find_if(builtin_.begin(), e, pred);
  if (i != e)
    return i->second();
  auto j = custom_by_name_.find(x);
  if (j != custom_by_name_.end())
    return j->second();
  return nullptr;
}

type_erased_value_ptr
uniform_type_info_map::make_value(const std::type_info& x) const {
  auto i = custom_by_rtti_.find(std::type_index(x));
  if (i != custom_by_rtti_.end())
    return i->second();
  return nullptr;
}

const std::string*
uniform_type_info_map::portable_name(uint16_t nr,
                                     const std::type_info* ti) const {
  if (nr != 0)
    return &builtin_names_[nr - 1];
  if (! ti)
    return nullptr;
  auto i = custom_names_.find(std::type_index(*ti));
  if (i != custom_names_.end())
    return &(i->second);
  return nullptr;
}

actor_factory_result uniform_type_info_map::make_actor(const std::string& name,
                                                       actor_config& cfg,
                                                       message& msg) const {
  actor_addr res;
  std::set<std::string> ifs;
  auto i = factories_.find(name);
  if (i != factories_.end())
    std::tie(res, ifs) = i->second(cfg, msg);
  return std::make_pair(std::move(res), std::move(ifs));
}

uniform_type_info_map::uniform_type_info_map(actor_system& sys) : system_(sys) {
  detail::sorted_builtin_types list;
  fill_builtins(builtin_, list, 0);
  for (size_t i = 0; i < builtin_names_.size(); ++i)
    builtin_names_[i] = detail::numbered_type_names[i];
}

} // namespace caf
