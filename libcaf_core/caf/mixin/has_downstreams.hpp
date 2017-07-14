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

#ifndef CAF_MIXIN_HAS_DOWNSTREAMS_HPP
#define CAF_MIXIN_HAS_DOWNSTREAMS_HPP

#include <cstddef>

#include "caf/sec.hpp"
#include "caf/logger.hpp"
#include "caf/actor_control_block.hpp"

namespace caf {
namespace mixin {

/// Mixin for streams with any number of downstreams.
template <class Base, class Subtype>
class has_downstreams : public Base {
public:
  error add_downstream(strong_actor_ptr& ptr) override {
    CAF_LOG_TRACE(CAF_ARG(ptr));
    CAF_ASSERT(ptr != nullptr);
    if (out().add_path(ptr))
      return none;
    return sec::downstream_already_exists;
  }

  error confirm_downstream(const strong_actor_ptr& rebind_from,
                           strong_actor_ptr& ptr, long initial_demand,
                           bool redeployable) override {
    CAF_LOG_TRACE(CAF_ARG(ptr) << CAF_ARG(initial_demand)
                  << CAF_ARG(redeployable));
    CAF_ASSERT(ptr != nullptr);
    if (out().confirm_path(rebind_from, ptr, redeployable)) {
      auto path = out().find(ptr);
      if (!path) {
        CAF_LOG_ERROR("Unable to find path after confirming it");
        return sec::invalid_downstream;
      }
      downstream_demand(path, initial_demand);
      return none;
    }
    return sec::invalid_downstream;
  }

  error push() override {
    CAF_LOG_TRACE("");
    out().emit_batches();
    return none;
  }

protected:
  virtual void downstream_demand(downstream_path* ptr, long demand) = 0;

private:
  Subtype* dptr() {
    return static_cast<Subtype*>(this);
  }

  downstream_policy& out() {
    return *this->dp();
  }
};

} // namespace mixin
} // namespace caf

#endif // CAF_MIXIN_HAS_DOWNSTREAMS_HPP
