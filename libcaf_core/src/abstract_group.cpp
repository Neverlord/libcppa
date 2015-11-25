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

#include "caf/abstract_group.hpp"

#include "caf/group.hpp"
#include "caf/message.hpp"
#include "caf/actor_cast.hpp"
#include "caf/group_manager.hpp"
#include "caf/detail/shared_spinlock.hpp"

namespace caf {

abstract_group::module::module(actor_system& sys, std::string mname)
    : system_(sys),
      name_(std::move(mname)) {
  // nop
}

void abstract_group::module::stop() {
  // nop
}

const std::string& abstract_group::module::name() const {
  return name_;
}

abstract_group::abstract_group(actor_system& sys,
                               abstract_group::module_ptr mod,
                               std::string id, const node_id& nid)
    : abstract_channel(abstract_channel::is_abstract_group_flag, nid),
      system_(sys),
      module_(mod),
      identifier_(std::move(id)) {
  // nop
}

const std::string& abstract_group::identifier() const {
  return identifier_;
}

abstract_group::module_ptr abstract_group::get_module() const {
  return module_;
}

const std::string& abstract_group::module_name() const {
  return get_module()->name();
}

abstract_group::module::~module() {
  // nop
}

abstract_group::~abstract_group() {
  // nop
}

} // namespace caf
